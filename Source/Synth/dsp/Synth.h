#pragma once
// Chimera voice engine: a 64-voice pool shared by up to 4 layers; per-note voice per matching
// layer (key / velocity zones), unison, glide, mono / legato, sustain pedal, modulation matrix
// evaluated every kSubBlock samples, per-layer output pairs, master FX on the main pair.
// Pure C++17, real-time safe: process() never allocates, locks or frees.
#include "Synth/dsp/Modulation.h"
#include <atomic>

namespace el::chimera {

struct MidiEvent { int offset; uint8_t status, data1, data2; };

// Hands patches from the message thread to the audio thread with no locks and no frees on the
// audio thread. A replaced patch is kept alive until the audio thread has finished a block that
// started after the swap.
class PatchExchange {
public:
    // ---- message thread
    void publish (std::shared_ptr<const Patch> p);
    std::shared_ptr<const Patch> latest() const { return current; }
    void collect();                              // frees patches the audio thread can't be using
    // ---- audio thread
    const Patch* acquire() const noexcept { return live.load (std::memory_order_acquire); }
    void blockDone() noexcept { epoch.fetch_add (1, std::memory_order_acq_rel); }
private:
    struct Retired { std::shared_ptr<const Patch> patch; uint64_t at; };
    std::shared_ptr<const Patch> current;
    std::vector<Retired> retired;
    std::atomic<const Patch*> live { nullptr };
    std::atomic<uint64_t> epoch { 0 };
};

class Synth {
public:
    Synth();
    ~Synth();
    void prepare (double sampleRate, int maxBlock);   // allocates; not real-time
    PatchExchange& patches() noexcept { return exchange; }

    // Audio thread. outs = numChannels channel pointers (2 per output pair, up to 8); a layer
    // routed to a pair that isn't connected plays through the main pair. Events sorted by offset.
    void process (float* const* outs, int numChannels, int numSamples, const MidiEvent* events, int numEvents,
                  double bpm, const LiveParams& live) noexcept;
    void allNotesOff() noexcept;                       // audio thread: release everything
    void panic() noexcept;                             // audio thread: silence now, clear FX

    int activeVoiceCount() const noexcept { return activeCount.load (std::memory_order_relaxed); }
    float peakLevel() const noexcept { return peak.load (std::memory_order_relaxed); }

private:
    struct Voice;
    struct LayerState { int monoStack[16] {}; int stackSize = 0; float lastPitch = -1.0f; };

    void handleEvent (const MidiEvent&, const Patch&) noexcept;
    void noteOn (int note, int velocity, const Patch&) noexcept;
    void noteOff (int note, const Patch&) noexcept;
    void startVoice (Voice&, int layer, int note, float velocity, const Patch&, bool glideFromLast) noexcept;
    void releaseVoice (Voice&) noexcept;
    Voice* allocateVoice (int layer, const Patch&) noexcept;
    void renderVoice (Voice&, const Patch&, const LiveParams&, int offset, int len, int connectedPairs) noexcept;
    void renderChunk (const Patch&, const LiveParams&, int offset, int len, int connectedPairs) noexcept;

    PatchExchange exchange;
    std::vector<std::unique_ptr<Voice>> voices;
    LayerState layerState[kMaxLayers];
    std::vector<float> bus[kOutputPairs * 2];
    Chorus chorus;
    Delay delay;
    Reverb reverb;
    Lfo globalLfo[kGlobalLfos];
    float globalLfoValue[kGlobalLfos] {};
    double sampleRate = 48000.0, bpmNow = 120.0;
    int maxBlock = 512;
    uint64_t noteCounter = 0;
    float modWheel = 0, aftertouch = 0, bend = 0;
    bool sustainPedal = false;
    Rng rng;
    std::atomic<int> activeCount { 0 };
    std::atomic<float> peak { 0.0f };
    bool prepared = false;
};

} // namespace el::chimera
