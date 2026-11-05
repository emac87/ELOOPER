#pragma once
// Lock-free primitives shared between the message thread and the audio thread.
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <functional>
#include <type_traits>

namespace el {

// Single-producer / single-consumer ring buffer. Capacity is rounded up to a power of two.
template <typename T>
class SpscQueue {
    static_assert (std::is_trivially_copyable<T>::value, "SpscQueue holds trivially copyable items");
public:
    explicit SpscQueue (size_t capacity) {
        size_t c = 2; while (c < capacity) c <<= 1;
        buffer.resize (c); mask = c - 1;
    }
    bool push (const T& v) noexcept {
        const auto w = writePos.load (std::memory_order_relaxed);
        if (w - readPos.load (std::memory_order_acquire) > mask) return false;
        buffer[w & mask] = v;
        writePos.store (w + 1, std::memory_order_release);
        return true;
    }
    bool pop (T& out) noexcept {
        const auto r = readPos.load (std::memory_order_relaxed);
        if (r == writePos.load (std::memory_order_acquire)) return false;
        out = buffer[r & mask];
        readPos.store (r + 1, std::memory_order_release);
        return true;
    }
    bool empty() const noexcept { return readPos.load() == writePos.load(); }
private:
    std::vector<T> buffer;
    size_t mask = 0;
    alignas (64) std::atomic<size_t> writePos { 0 };
    alignas (64) std::atomic<size_t> readPos { 0 };
};

// Several non-realtime producers, one realtime consumer. Producers serialise on a mutex
// (never taken by the consumer), so the audio thread stays wait-free.
template <typename T>
class MpscQueue {
public:
    explicit MpscQueue (size_t capacity) : q (capacity) {}
    bool push (const T& v) { std::lock_guard<std::mutex> l (m); return q.push (v); }
    bool pop (T& out) noexcept { return q.pop (out); }
private:
    std::mutex m;
    SpscQueue<T> q;
};

// Tracks how far the audio thread has got, so the message thread knows when an object it
// unpublished can no longer be referenced by a callback in flight.
class RtEpoch {
public:
    // Audio thread: call at the start and end of every callback.
    void enter() noexcept { active.store (counter.load (std::memory_order_relaxed) + 1, std::memory_order_seq_cst); }
    void exit()  noexcept { counter.fetch_add (1, std::memory_order_release); active.store (0, std::memory_order_release); }
    // Message thread: any callback that started after this value was read can't see old pointers.
    uint64_t now() const noexcept { return counter.load (std::memory_order_acquire); }
    // True once every callback that may have seen state from before `stamp` has finished.
    bool safeToFree (uint64_t stamp) const noexcept {
        const auto a = active.load (std::memory_order_acquire);
        return counter.load (std::memory_order_acquire) > stamp && (a == 0 || a > stamp + 1);
    }
private:
    std::atomic<uint64_t> counter { 0 };
    std::atomic<uint64_t> active { 0 };
};

// Deferred deleter: the message thread retires objects it has unpublished; they are destroyed
// on the message thread once the audio thread has moved past them.
class Retirer {
public:
    explicit Retirer (const RtEpoch& e) : epoch (e) {}
    ~Retirer() { garbage.clear(); }
    template <typename T> void retire (std::shared_ptr<T> p) {
        if (p) garbage.push_back ({ epoch.now(), std::shared_ptr<void> (std::move (p)) });
    }
    template <typename T> void retire (T* raw) { retire (std::shared_ptr<T> (raw)); }
    // Message thread, e.g. from a timer. Pass force=true when no audio callback can run.
    void collect (bool force = false) {
        for (size_t i = 0; i < garbage.size();) {
            if (force || epoch.safeToFree (garbage[i].stamp)) { garbage[i] = std::move (garbage.back()); garbage.pop_back(); }
            else ++i;
        }
    }
    size_t pending() const noexcept { return garbage.size(); }
private:
    struct Item { uint64_t stamp; std::shared_ptr<void> obj; };
    const RtEpoch& epoch;
    std::vector<Item> garbage;
};

// An immutable value the message thread replaces wholesale and the audio thread reads.
template <typename T>
class RtPtr {
public:
    RtPtr() = default;
    RtPtr (const RtPtr&) = delete;
    ~RtPtr() { delete ptr.load(); }
    const T* get() const noexcept { return ptr.load (std::memory_order_acquire); }
    // Message thread: swap in a new value; the old one is retired.
    void publish (std::unique_ptr<T> v, Retirer& r) {
        if (auto* old = ptr.exchange (v.release(), std::memory_order_acq_rel)) r.retire (old);
    }
private:
    std::atomic<T*> ptr { nullptr };
};

// ---------------------------------------------------------------------------------------
// Chunked audio memory for loops. The message thread keeps the pool topped up, the audio
// thread grabs chunks while recording and hands them back when a take is discarded. No
// allocation ever happens on the audio thread.
// ---------------------------------------------------------------------------------------
constexpr int kChunkShift  = 14;
constexpr int kChunkFrames = 1 << kChunkShift;         // 16384 frames
constexpr int kChunkMask   = kChunkFrames - 1;
constexpr int kMaxChunksPerTake = 4096;                // ~23 min stereo at 48 kHz

struct AudioChunk {
    float data[2][kChunkFrames];
    AudioChunk* next = nullptr;
};

class ChunkPool {
public:
    ChunkPool() = default;
    ChunkPool (const ChunkPool&) = delete;

    // Audio thread (the single consumer). Returns nullptr when the pool is exhausted.
    AudioChunk* pop() noexcept {
        auto* h = head.load (std::memory_order_acquire);
        while (h != nullptr && ! head.compare_exchange_weak (h, h->next, std::memory_order_acq_rel, std::memory_order_acquire)) {}
        if (h != nullptr) { count.fetch_sub (1, std::memory_order_relaxed); h->next = nullptr; }
        return h;
    }
    // Any thread.
    void push (AudioChunk* c) noexcept {
        if (c == nullptr) return;
        auto* h = head.load (std::memory_order_relaxed);
        do { c->next = h; } while (! head.compare_exchange_weak (h, c, std::memory_order_release, std::memory_order_relaxed));
        count.fetch_add (1, std::memory_order_relaxed);
    }
    // Message thread: make sure at least `minFree` chunks are available.
    void reserve (int minFree) {
        std::lock_guard<std::mutex> l (allocMutex);
        while (count.load() < minFree) {
            owned.push_back (std::make_unique<AudioChunk>());
            push (owned.back().get());
        }
    }
    int available() const noexcept { return count.load (std::memory_order_relaxed); }
    size_t totalAllocated() const noexcept { return owned.size(); }
private:
    std::atomic<AudioChunk*> head { nullptr };
    std::atomic<int> count { 0 };
    std::mutex allocMutex;
    std::vector<std::unique_ptr<AudioChunk>> owned;
};

} // namespace el
