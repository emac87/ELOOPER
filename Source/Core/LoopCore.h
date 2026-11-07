#pragma once
// Real-time loop: chunked audio takes, quantised state machine, MIDI record/playback,
// latency-compensated recording and take folders. Everything in LoopRt::process* runs on
// the audio thread and never allocates or locks.
#include "Types.h"
#include "RtUtil.h"
#include "TimeMath.h"
#include "MidiClip.h"
#include "Automation.h"
#include <atomic>

namespace el {

// --- audio thread -> message thread notifications ---------------------------------------
enum class EngineEventType : uint8_t {
    RecordStarted,    // loopId, take
    RecordFinished,   // loopId, take, beat = length in beats
    RecordAborted,    // loopId, take
    MidiRecorded,     // loopId, take, midi (beat relative to loop start)
    OutOfMemory,      // loopId: chunk pool ran dry while recording
    TakeDeleted,      // loopId, take
    BounceFinished,   // loopId = new loop, trackId = source track
    TransportStarted,
    TransportStopped,
    MidiLearn         // midi = incoming event (for controller learn); trackId unused
};

struct EngineEvent {
    EngineEventType type;
    Id loopId = kInvalidId;
    Id trackId = kInvalidId;
    int take = 0;
    double beat = 0;
    MidiEvent midi;
};

using EventQueue = SpscQueue<EngineEvent>;

// A MIDI message at a sample offset inside the current block.
struct TimedMidi {
    int sample = 0;
    uint8_t data[3] = { 0, 0, 0 };
    uint8_t size = 3;
};

// Fixed-capacity MIDI buffer used on the audio thread.
template <int Capacity>
struct TimedMidiBuffer {
    TimedMidi events[Capacity];
    int count = 0;
    void clear() noexcept { count = 0; }
    bool add (int sample, const uint8_t* d, int size) noexcept {
        if (count >= Capacity) return false;
        auto& e = events[count++]; e.sample = sample; e.size = (uint8_t) size;
        e.data[0] = d[0]; e.data[1] = size > 1 ? d[1] : 0; e.data[2] = size > 2 ? d[2] : 0;
        return true;
    }
};
using BlockMidi = TimedMidiBuffer<1024>;

struct TakeAudio {
    AudioChunk* chunks[kMaxChunksPerTake] {};
    int numChunks = 0;
    int64_t lengthFrames = 0;
    double lengthBeats = 0;
    double sampleRate = 0;      // rate the audio was recorded / loaded at (0 = same as now)
    bool hasContent = false;

    float sample (int ch, int64_t f) const noexcept {
        const auto* c = chunks[f >> kChunkShift];
        return c != nullptr ? c->data[ch][f & kChunkMask] : 0.0f;
    }
    float* writePtr (int ch, int64_t f) noexcept {
        auto* c = chunks[f >> kChunkShift];
        return c != nullptr ? &c->data[ch][f & kChunkMask] : nullptr;
    }
    // Makes sure the chunk holding `frame` exists. False if out of memory / max length.
    bool ensure (int64_t frame, ChunkPool& pool) noexcept;
    void release (ChunkPool& pool) noexcept;
};

struct LoopContext {
    ChunkPool* pool = nullptr;
    EventQueue* events = nullptr;
    int recordLatency = 0;      // frames to shift audio recording earlier (round-trip latency)
    int midiLatency = 0;        // frames to shift MIDI recording earlier (output latency)
};

struct LoopIo {
    const float* in[2] { nullptr, nullptr };  // record source for audio loops (block-sized)
    float* out[2] { nullptr, nullptr };       // playback is ADDED here
    const BlockMidi* midiIn = nullptr;        // live MIDI for MIDI loop recording
    BlockMidi* midiOut = nullptr;             // MIDI loop playback is appended here (unsorted)
};

enum class LoopCommandType : uint8_t {
    Trigger, Record, Play, Stop, Overdub, Clear, SelectTake, DeleteTake, SetSettings,
    RecordAt,   // start recording exactly at `beat` for `lengthBeats` (used by bounce)
    DefineTake, // MIDI/automation: make take `take` exist with `lengthBeats` (drawn, not recorded)
    PlayAt,     // play now, phase 0 at `beat` (timeline mode / bounce handoff), no quantise
    StopNow     // stop immediately, no quantise
};

struct LoopCommand {
    LoopCommandType type;
    int take = 0;
    double beat = 0;
    double lengthBeats = 0;
    LoopSettings settings;
};

class LoopRt {
public:
    LoopRt (Id loopId, Id trackId, LoopType type);
    LoopRt (const LoopRt&) = delete;

    const Id id;
    const Id trackId;
    const LoopType type;

    // ---- shared, lock-free -----------------------------------------------------------
    std::atomic<float> volume { 1.0f };                 // linear gain
    std::atomic<bool>  muted { false };
    // Published for the UI (written by the audio thread once per block).
    std::atomic<LoopState>     uiState { LoopState::Empty };
    std::atomic<PendingAction> uiPending { PendingAction::None };
    std::atomic<double> uiPhaseBeats { 0 };   // position inside the loop, in beats
    std::atomic<double> uiLengthBeats { 0 };  // current (or target, while recording) length
    std::atomic<double> uiRecordedBeats { 0 };// beats recorded so far while recording
    std::atomic<int>    uiActiveTake { 0 };
    std::atomic<int>    uiNumTakes { 0 };
    std::atomic<float>  uiPeak { 0 };

    // Message thread publishes these; the audio thread reads them.
    RtPtr<MidiSequence>  midiSequences[kMaxTakes];
    RtPtr<AutomationSet> automation;   // this loop's automation layer

    // ---- audio thread ------------------------------------------------------------------
    void command (const LoopCommand& c, const BlockTime& t, const LoopContext& ctx) noexcept;
    // Audio and MIDI loops: record from io.in / io.midiIn and play into io.out / io.midiOut.
    void process (const BlockTime& t, LoopIo& io, const LoopContext& ctx) noexcept;
    // Transport stopped: closes recordings, silences playback, sends note-offs.
    void transportStopped (const LoopContext& ctx, BlockMidi* midiOut) noexcept;
    // Transport (re)started at beat 0: playing loops restart from their top.
    void transportStarted() noexcept;
    // Releases audio memory (message thread, only when no longer reachable by the audio thread).
    void releaseAll (ChunkPool& pool) noexcept;

    // Whether this loop is currently making sound / driving automation.
    bool isAudible() const noexcept { return state == LoopState::Playing || state == LoopState::Overdubbing; }
    bool isRecordingAny() const noexcept { return state == LoopState::Recording || state == LoopState::Overdubbing || pending == PendingAction::Record; }
    bool hasContent() const noexcept { return numTakes > 0; }
    LoopState currentState() const noexcept { return state; }
    double phaseBeatsAt (double beat) const noexcept;
    double lengthBeats() const noexcept;
    const TakeAudio& take (int i) const noexcept { return takes[i]; }
    int activeTakeIndex() const noexcept { return activeTake; }
    int takeCount() const noexcept { return numTakes; }
    LoopSettings settingsCopy() const noexcept { return settings; }
    double anchor() const noexcept { return anchorBeat; }

    // Message thread only, while the loop is NOT reachable by the audio thread
    // (before first publish / after load): install audio for a take.
    bool loadTake (int takeIndex, const float* const* channels, int numChannels, int64_t frames,
                   double beats, ChunkPool& pool, double sampleRate = 0);
    void setActiveTakeOffline (int t) noexcept { activeTake = t; uiActiveTake = t; }
    void setSettingsOffline (const LoopSettings& s) noexcept { settings = s; }
    void setStoppedOffline() noexcept { if (numTakes > 0) state = LoopState::Stopped; publish (0); }

private:
    void schedule (PendingAction a, double beat) noexcept { pending = a; pendingBeat = beat; }
    void applyPending (double beat, int sampleInBlock, const BlockTime& t, const LoopContext& ctx, LoopIo& io) noexcept;
    void startRecording (double beat, double targetBeats, const BlockTime& t, const LoopContext& ctx) noexcept;
    void finishRecording (double endBeat, bool userClosed, const BlockTime& t, const LoopContext& ctx) noexcept;
    void abortRecording (const LoopContext& ctx) noexcept;
    void renderSegment (int from, int to, const BlockTime& t, LoopIo& io, const LoopContext& ctx) noexcept;
    int  recordAudio (int from, int to, LoopIo& io, const LoopContext& ctx) noexcept;  // returns sample of OOM or -1
    void writeTail (int from, int to, LoopIo& io) noexcept;
    void playAudio (int from, int to, const BlockTime& t, LoopIo& io, bool overdub, const LoopContext& ctx) noexcept;
    void playMidi (int from, int to, const BlockTime& t, LoopIo& io) noexcept;
    double bestGrainStart (const TakeAudio& tk, double synced, double target, double pitchRate, int searchRadius) const noexcept;
    void recordMidi (int from, int to, const BlockTime& t, LoopIo& io, const LoopContext& ctx, double originBeat) noexcept;
    void allNotesOff (BlockMidi* out, int sample) noexcept;
    void deleteTake (int index, const LoopContext& ctx) noexcept;
    void post (const LoopContext& ctx, EngineEventType type, int takeIdx, double beat = 0) noexcept;
    void publish (double beatNow) noexcept;
    double quantizeGrid (Quantize q, const BlockTime& t) const noexcept;

    LoopSettings settings;
    LoopState state = LoopState::Empty;
    PendingAction pending = PendingAction::None;
    double pendingBeat = 0;
    double pendingLength = 0;       // RecordAt
    bool   closeAtCycleEnd = false; // cycle-take recording: stop after this cycle

    double anchorBeat = 0;          // loop phase 0 sits at this beat
    double recordTargetBeats = 0;   // 0 = free length
    int64_t recFrames = 0;          // frames since recording anchor
    int recordTake = 0;
    int recordLatency = 0;

    int tailTake = -1;              // previous take still receiving its latency tail
    int64_t tailIndex = 0;
    int tailRemaining = 0;

    TakeAudio takes[kMaxTakes];
    int numTakes = 0;
    int activeTake = 0;

    // Time-stretch (keep pitch on tempo changes): two overlapping Hann-windowed grains, each
    // started at the beat-synced position and read at the original pitch.
    struct Grain { double src = 0; int age = 0; bool on = false; };
    Grain grains[2];
    int grainClock = 0, nextGrain = 0;
    bool stretching = false;

    float env = 0;                  // click-free start/stop envelope
    float lastGain = 0;
    const MidiSequence* lastSeq = nullptr;
    bool flushNotes = false;        // send note-offs for held notes at the next process()
    uint64_t heldNotes[16][2] {};   // bitsets of notes this loop has switched on
};

} // namespace el
