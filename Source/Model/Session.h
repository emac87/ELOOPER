#pragma once
// Session document. Owned and mutated ONLY on the message thread, via EngineController.
// Anything the audio thread needs lives in the shared *Rt objects (atomics / RtPtr).
#include <JuceHeader.h>
#include "Core/Types.h"
#include "Core/LoopCore.h"
#include "Core/MidiClip.h"
#include "Core/Automation.h"
#include "Core/ParamMapping.h"
#include <memory>
#include <optional>
#include <vector>

namespace el {

// ---- plugins -----------------------------------------------------------------------------
enum class FxChain : uint8_t { MidiFx, Instrument, Pre /* pre-fader inserts */, Post /* post-fader inserts */ };

struct PluginSlot {
    Id id = kInvalidId;
    juce::PluginDescription description;
    std::unique_ptr<juce::AudioPluginInstance> instance;   // null while loading / if missing
    std::atomic<bool> bypassed { false };
    juce::MemoryBlock pendingState;                          // state to apply once instantiated (load)
    int numOutputChannels = 2;                               // > 2 => multi-out instrument
    int latencySamples = 0;
    juce::String name() const { return description.name.isNotEmpty() ? description.name : juce::String ("Plugin"); }
    bool isInstrument() const { return description.isInstrument; }
};
using PluginSlotPtr = std::shared_ptr<PluginSlot>;

// ---- real-time mirror of a track's mixer state (UI writes, audio thread reads) ------------
struct TrackRt {
    explicit TrackRt (Id i) : id (i) { for (auto& e : eq) e = 0.5f; for (auto& s : sendLevel) s = 0.0f; }
    const Id id;
    std::atomic<float> inputGain { 0.5f };              // normalised, 0.5 = 0 dB
    std::atomic<float> volume { pm::kUnityVolume };     // normalised fader
    std::atomic<float> pan { 0.5f };
    std::atomic<bool>  mute { false }, solo { false };
    std::atomic<bool>  eqEnabled { true };
    std::atomic<float> eq[kEqBands];                    // normalised, 0.5 = flat
    std::atomic<float> sendLevel[kMaxSends];            // normalised fader law, index matches TrackModel::sends
    std::atomic<MonitorMode> monitor { MonitorMode::Auto };
    std::atomic<bool>  armed { false };                 // record-arm: forces input monitoring
    std::atomic<bool>  bounced { false };               // bounced away: silent, plugins suspended
    // Meters (audio thread writes, UI reads and decays).
    std::atomic<float> peakL { 0 }, peakR { 0 }, inputPeak { 0 };
    std::atomic<bool>  inputAudible { true };           // is live input currently monitored (Auto logic result)
    RtPtr<AutomationSet> automation;                    // track (timeline) automation lanes
    // Parameters the user is currently holding (UI gesture): automation playback leaves them alone.
    static constexpr int kTouchSlots = 4;
    std::atomic<uint64_t> touched[kTouchSlots] {};
    static uint64_t touchKey (const ParamTarget& p) noexcept {
        return (((uint64_t) p.kind + 1) << 56) | (((uint64_t) p.objectId & 0xffffffu) << 24) | ((uint64_t) (p.index + 1) & 0xffffffu);
    }
    bool isTouched (const ParamTarget& p) const noexcept {
        const auto k = touchKey (p);
        for (auto& t : touched) if (t.load (std::memory_order_relaxed) == k) return true;
        return false;
    }
};
using TrackRtPtr = std::shared_ptr<TrackRt>;

// ---- loops ---------------------------------------------------------------------------------
struct LoopModel {
    Id id = kInvalidId;
    Id trackId = kInvalidId;
    LoopType type = LoopType::Audio;
    juce::String name;
    std::optional<juce::Colour> colour;       // nullopt => use the track colour
    LoopSettings settings;
    std::vector<MidiClip> midiTakes;          // MIDI loops: one editable clip per take (index = take)
    std::vector<AutomationLane> automation;   // automation layer (every loop type has one)
    double automationLengthBeats = 16;        // automation-only loops: length when not recorded yet
    float volume = 1.0f;                      // linear
    std::shared_ptr<LoopRt> rt;

    // Filled by SessionSerializer when loading; consumed by EngineController::loadSession.
    struct PendingTake { juce::AudioBuffer<float> audio; double beats = 0; };
    std::vector<PendingTake> pendingAudio;
    int pendingActiveTake = 0;
};
using LoopPtr = std::shared_ptr<LoopModel>;

struct SendSpec {
    Id busId = kInvalidId;
    bool preFader = false;
};

struct TrackModel {
    Id id = kInvalidId;
    TrackKind kind = TrackKind::Audio;
    juce::String name;
    juce::Colour colour { 0xff4fc3f7 };

    // Input
    int inputChannel = 0;           // first hardware input channel (0-based); -1 = none
    bool stereoInput = false;       // use inputChannel and inputChannel + 1
    juce::String midiInputDevice;   // device identifier; empty = all devices
    int midiChannel = 0;            // 0 = omni, 1..16
    Id recordSource = kInvalidId;   // resample: record another track's output (used by bounce)

    // Processing chain:  [MIDI FX] -> [instrument] -> input gain -> loops + monitored input
    //                    -> pre-fader inserts -> graphic EQ -> fader / pan -> post-fader inserts
    std::vector<PluginSlotPtr> midiFx;
    PluginSlotPtr instrument;
    std::vector<PluginSlotPtr> preFx;
    std::vector<PluginSlotPtr> postFx;

    std::vector<SendSpec> sends;     // max kMaxSends; levels in rt->sendLevel[i]
    Id outputBus = kMasterId;        // for Master: ignored
    int hardwareOutput = 0;          // Master only: first output channel of the stereo pair

    // Multi-out: AuxOut tracks carry the plugin output pair they take from their parent.
    Id auxParent = kInvalidId;
    int auxPair = 0;                 // 1 = channels 2/3 of the parent's instrument, etc.

    std::vector<LoopPtr> loops;
    std::vector<AutomationLane> automation;   // track/timeline lanes (beats from song start)

    bool bounced = false;            // shown on the "Bounced" page instead of the main page
    Id bouncedInto = kInvalidId;     // the track that holds the bounce

    TrackRtPtr rt;

    bool isBusLike() const noexcept { return kind == TrackKind::Bus || kind == TrackKind::Master; }
    PluginSlotPtr findPlugin (Id slotId) const;
    std::vector<PluginSlotPtr> allPlugins() const;  // midiFx, instrument, pre, post
};
using TrackPtr = std::shared_ptr<TrackModel>;

// ---- timeline ------------------------------------------------------------------------------
struct TimelineClip {
    Id id = kInvalidId;
    Id trackId = kInvalidId;
    Id loopId = kInvalidId;
    double startBeat = 0;
    double lengthBeats = 16;
};

struct TransportSettings {
    double tempo = 120.0;
    int beatsPerBar = 4;
    int beatUnit = 4;
    int masterBars = 4;                  // default record length for new loops
    bool metronome = false;
    float metronomeVolume = 0.5f;
    bool linkEnabled = false;
    bool linkStartStopSync = true;
    TransportMode mode = TransportMode::Loops;
    bool timelineLoop = false;           // timeline mode: cycle between loopStart/loopEnd
    double timelineLoopStart = 0, timelineLoopEnd = 64;
    int recordLatencyOffset = 0;         // user tweak, samples added to measured round-trip latency
    LoopSettings defaultLoopSettings;    // applied to new loops
};

struct Session {
    std::vector<TrackPtr> tracks;        // display order; master is always present
    TransportSettings transport;
    std::vector<TimelineClip> timeline;
    Id nextId = 2;                       // 1 = master
    juce::File file;                     // project folder, if saved

    TrackPtr findTrack (Id id) const;
    LoopPtr findLoop (Id loopId) const;
    TrackPtr trackOfLoop (Id loopId) const;
    std::pair<TrackPtr, PluginSlotPtr> findPlugin (Id slotId) const;
    TrackPtr master() const { return findTrack (kMasterId); }
    std::vector<TrackPtr> buses() const;          // Bus kinds (not master)
    Id allocateId() { return nextId++; }
};

// ---- presets -------------------------------------------------------------------------------
struct PluginPresetData {
    juce::PluginDescription description;
    juce::MemoryBlock state;
    bool bypassed = false;
};

struct ChannelPreset {
    juce::String name;
    float inputGain = 0.5f, volume = pm::kUnityVolume, pan = 0.5f;
    bool eqEnabled = true;
    float eq[kEqBands] { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    MonitorMode monitor = MonitorMode::Auto;
    std::vector<PluginPresetData> midiFx;
    std::optional<PluginPresetData> instrument;
    std::vector<PluginPresetData> preFx, postFx;
};

// Default palette for new channels.
juce::Colour paletteColour (int index);

} // namespace el
