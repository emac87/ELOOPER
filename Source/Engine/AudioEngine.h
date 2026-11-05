#pragma once
// Real-time renderer. Owned by EngineController. The audio callback reads an immutable
// RenderGraph snapshot published by the message thread and applies commands from a
// lock-free queue; it never allocates, locks or calls into the message thread.
#include <JuceHeader.h>
#include "Model/Session.h"
#include "Core/GraphicEq.h"
#include "Engine/LinkClock.h"

namespace el {

// ---- render graph (built on the message thread, immutable once published) ----------------
struct PluginNode {
    PluginSlot* slot = nullptr;
    juce::AudioPluginInstance* proc = nullptr;
    bool acceptsMidi = false, producesMidi = false;
    int numIn = 2, numOut = 2;
};

struct SendNode { int destIndex = -1; int sendIndex = 0; bool preFader = false; };

struct TrackNode {
    Id id = kInvalidId;
    TrackKind kind = TrackKind::Audio;
    TrackRt* rt = nullptr;
    int inputChannel = -1; bool stereoInput = false;
    juce::String midiDevice; int midiDeviceIndex = -1; int midiChannel = 0;
    int recordSourceIndex = -1;           // resample source node (bounce)
    int auxParentIndex = -1; int auxPair = 0;
    std::vector<PluginNode> midiFx, pre, post;
    PluginNode instrument; bool hasInstrument = false;
    std::vector<LoopRt*> loops;
    std::vector<SendNode> sends;
    int outputIndex = -1;                 // node index of the output bus (-1 = hardware, master)
    int hardwareOutput = 0;
    std::vector<int> auxChildren;         // node indices fed by this node's instrument outputs
    // Per-node scratch, sized at build time.
    juce::AudioBuffer<float> bus;          // summed input for buses / aux tracks (2 ch)
    juce::AudioBuffer<float> input;        // input signal after input gain (2 ch): what audio loops record
    juce::AudioBuffer<float> work;         // processing buffer (max plugin channels)
    juce::AudioBuffer<float> output;       // final post-fader output (2 ch), read by resamplers
    juce::MidiBuffer midi;
    juce::MidiBuffer fxMidi;               // scratch for audio-effect plugins
    GraphicEq eq;
    float smoothMonitor = 1, smoothMute = 1;
    bool isBusLikeOrAux() const noexcept { return kind == TrackKind::Bus || kind == TrackKind::Master || kind == TrackKind::AuxOut; }
    bool midiGateOpen = true;
    float smoothVol = 0, smoothPanL = 1, smoothPanR = 1, smoothInGain = 1;
    float smoothSend[kMaxSends] {};
};

struct TimelineEntry { int nodeIndex; LoopRt* loop; double start, length; };

struct RenderGraph {
    std::vector<TrackNode> nodes;          // processing order (topological)
    std::vector<std::pair<Id, LoopRt*>> loopIndex;   // sorted by id, for command lookup
    std::vector<TimelineEntry> timeline;   // sorted by start
    std::vector<LoopRt*> timelineLoops;    // unique loops referenced by the timeline
    int masterIndex = -1;
    bool anySolo = false;
    int maxBlock = 0;
    double sampleRate = 48000;
    LoopRt* findLoop (Id id) const noexcept;
};

// ---- commands (message -> audio) ---------------------------------------------------------
enum class EngineCommandType : uint8_t {
    Loop,             // loop + loopCmd
    Play, Stop,
    Locate,           // beat
    Bounce,           // trackId = source; loop = new loop on new track (beat = target track id): resample one cycle
    StopTrackLoops,   // trackId: stop every loop on a track now
    AllLoopsStop
};

struct EngineCommand {
    EngineCommandType type;
    LoopRt* loop = nullptr;
    LoopCommand loopCmd { LoopCommandType::Trigger };
    Id trackId = kInvalidId;
    double beat = 0;
};

class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback {
public:
    AudioEngine();
    ~AudioEngine() override;

    // ---- message thread ----------------------------------------------------------------
    void publishGraph (std::unique_ptr<RenderGraph> g);
    const RenderGraph* currentGraph() const noexcept { return graph.get(); }
    bool send (const EngineCommand& c) noexcept { return commands.push (c); }
    bool popEvent (EngineEvent& e) noexcept { return events.pop (e); }
    void collectGarbage();                 // call from a timer; also tops up the chunk pool
    Retirer& retirer() noexcept { return garbage; }
    ChunkPool& chunkPool() noexcept { return pool; }
    LinkClock& clock() noexcept { return link; }

    void setTransportSettings (const TransportSettings& t);   // tempo is set via clock()
    void setMidiInputDevices (const juce::StringArray& identifiers);  // index = TrackNode::midiDeviceIndex
    void setRecordLatencyOffset (int samples) noexcept { latencyOffset.store (samples); }

    double sampleRate() const noexcept { return currentRate.load(); }
    int blockSize() const noexcept { return currentBlock.load(); }
    int inputLatency() const noexcept { return latIn.load(); }
    int outputLatency() const noexcept { return latOut.load(); }

    // Status for the UI.
    std::atomic<double> statusBeat { 0 };
    std::atomic<bool>   statusPlaying { false };
    std::atomic<double> statusTempo { 120 };
    std::atomic<double> cpuLoad { 0 };
    std::atomic<int>    xruns { 0 };

    // ---- offline / tests ---------------------------------------------------------------
    void prepareOffline (double sampleRate, int blockSize);
    void render (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept;
    void injectMidi (const juce::MidiMessage& m, int deviceIndex = 0) noexcept;

    // ---- juce callbacks ----------------------------------------------------------------
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

private:
    struct RawMidi { uint8_t data[3]; uint8_t size; int8_t device; double time; };
    void processCommands (const BlockTime& t);
    void processNode (RenderGraph& g, TrackNode& n, const BlockTime& t, const float* const* in, int numIn, int numSamples);
    void runPlugins (std::vector<PluginNode>& chain, juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi, int numSamples);
    void applyAutomation (RenderGraph& g, TrackNode& n, const BlockTime& t);
    void applyTimeline (RenderGraph& g, const BlockTime& t);
    void renderMetronome (float* const* out, int numOut, int ch, const BlockTime& t);
    void collectMidi (int numSamples);

    RtEpoch epoch;
    Retirer garbage { epoch };
    ChunkPool pool;
    SpscQueue<EngineCommand> commands { 1024 };
    EventQueue events { 8192 };
    MpscQueue<RawMidi> midiIn { 2048 };
    std::atomic<RenderGraph*> graphPtr { nullptr };
    std::unique_ptr<RenderGraph> graph;    // message-thread copy of the live pointer (ownership)
    LinkClock link;
    LoopContext loopCtx;

    std::atomic<TransportSettings*> transportPtr { nullptr };
    std::unique_ptr<TransportSettings> transport;

    juce::StringArray midiDevices;
    std::mutex midiDeviceMutex;            // message thread + MIDI thread only
    BlockMidi liveMidi[8];                 // per input device, this block
    BlockMidi loopMidiScratch;
    BlockMidi trackMidiScratch;            // live MIDI filtered for the track being processed

    std::atomic<double> currentRate { 48000 };
    std::atomic<int> currentBlock { 512 };
    std::atomic<int> latIn { 0 }, latOut { 0 }, latencyOffset { 0 };
    std::atomic<double> pendingLocate { -1 };

    bool wasPlaying = false;
    int64_t sampleClock = 0;
    double metroPhase = 0; int metroRemaining = 0; float metroFreq = 1000;
    juce::AudioBuffer<float> silence;

    struct Bounce { LoopRt* target = nullptr; Id sourceTrack = kInvalidId; Id targetTrack = kInvalidId; bool capturing = false; };
    Bounce bounces[8];
    void startBounce (RenderGraph& g, const EngineCommand& c, const BlockTime& t);
    void updateBounces (RenderGraph& g, const BlockTime& t);
    bool isBounceSource (Id trackId) const noexcept;
    TrackNode* findNode (RenderGraph& g, Id id) noexcept;
};

} // namespace el
