#pragma once
// The one API the UI (and presets, serialization, MIDI mapping) use to read and change the
// session. Message thread only. Every mutation updates the Session model, republishes what
// the audio thread needs, and notifies listeners.
#include <JuceHeader.h>
#include "Model/Session.h"
#include "Engine/PluginHost.h"
#include <functional>

namespace el {

class AudioEngine;
class LinkClock;

enum class Change : uint8_t {
    Structure,   // tracks/loops added, removed, reordered, bounced; routing changed
    Track,       // id = track: name, colour, input, monitor, plugins, sends...
    Loop,        // id = loop: name, colour, settings, takes, recording finished
    Clip,        // id = loop: MIDI clip content changed
    Automation,  // id = loop id, or track id for track lanes
    Transport,   // tempo, signature, master bars, link, mode, metronome
    Timeline,    // timeline clips
    Session      // whole session replaced (new / load)
};

struct LoopStatus {
    LoopState state = LoopState::Empty;
    PendingAction pending = PendingAction::None;
    double phaseBeats = 0, lengthBeats = 0, recordedBeats = 0;
    float progress = 0;          // 0..1 position in loop (or record progress)
    int activeTake = 0, numTakes = 0;
    float peak = 0;
};

struct TransportStatus {
    bool playing = false;
    double beat = 0;             // musical position
    double tempo = 120;
    int bar = 0;                 // 0-based
    double beatInBar = 0;
    int linkPeers = 0;
    bool linkEnabled = false;
    double cpuLoad = 0;          // 0..1
    int xruns = 0;
    double sampleRate = 0;
    int blockSize = 0;
    int latencyIn = 0, latencyOut = 0;
};

struct MeterStatus { float left = 0, right = 0, input = 0; bool inputAudible = true; };

// Where automation lanes live: a loop's automation layer, or the track's timeline lanes.
struct AutomationOwner {
    Id trackId = kInvalidId;
    Id loopId = kInvalidId;      // kInvalidId => the track's own lanes
};

// A parameter that can be automated / mapped on a track, with a display name.
struct ParamInfo {
    ParamTarget target;
    juce::String name;           // e.g. "Volume", "EQ 1k", "Send Reverb", "Serum: Cutoff"
    juce::String group;          // "Mixer", "EQ", "Sends", or the plugin name
};

class EngineController {
public:
    explicit EngineController (bool openAudioDevice = true);
    ~EngineController();

    // ---- access ------------------------------------------------------------------------
    Session& session() noexcept { return *sessionPtr; }
    const Session& session() const noexcept { return *sessionPtr; }
    AudioEngine& engine() noexcept { return *audioEngine; }
    juce::AudioDeviceManager& deviceManager() noexcept { return devices; }
    PluginHost& pluginHost() noexcept { return plugins; }

    struct Listener { virtual ~Listener() = default; virtual void sessionChanged (Change what, Id id) = 0; };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // ---- session -----------------------------------------------------------------------
    void newSession();                          // master + 1 audio track + 1 instrument track
    // Replace the whole session (used by SessionSerializer). Instantiates plugins from
    // PluginSlot::description + pendingState, loads LoopModel::pendingAudio. onDone(errors).
    void loadSession (std::unique_ptr<Session> s, std::function<void (juce::StringArray errors)> onDone);

    // ---- tracks ------------------------------------------------------------------------
    Id addTrack (TrackKind kind, const juce::String& name = {});   // Audio / Instrument / Bus
    void removeTrack (Id trackId);
    void moveTrack (Id trackId, int newIndex);
    void setTrackName (Id trackId, const juce::String& name);
    void setTrackColour (Id trackId, juce::Colour c);              // loops without own colour follow
    void setTrackInput (Id trackId, int firstChannel, bool stereo);  // -1 = no audio input
    void setTrackMidiInput (Id trackId, const juce::String& deviceIdentifier, int channel);
    void setMonitorMode (Id trackId, MonitorMode m);
    void setArmed (Id trackId, bool armed);
    bool setTrackOutput (Id trackId, Id busId);                    // false if it would create a cycle
    void setMasterHardwareOutput (int firstChannel);
    int  addSend (Id trackId, Id busId, bool preFader = false);   // returns send index or -1 (cycle/full)
    void removeSend (Id trackId, int sendIndex);
    void setSendPreFader (Id trackId, int sendIndex, bool pre);

    // Mixer & any other automatable parameter (normalised 0..1, see Core/ParamMapping.h).
    void  setParam (Id trackId, const ParamTarget& p, float normalised);
    float getParam (Id trackId, const ParamTarget& p) const;
    std::vector<ParamInfo> listParams (Id trackId) const;          // everything automatable on the track
    // UI gesture hooks: while a loop on this track records/overdubs, touched values are written
    // into that loop's automation layer (and into track lanes when timeline-recording).
    void beginParamGesture (Id trackId, const ParamTarget& p);
    void endParamGesture (Id trackId, const ParamTarget& p);

    // ---- plugins -----------------------------------------------------------------------
    // Inserts asynchronously; onDone(slotId or kInvalidId, error). index -1 = append.
    // FxChain::Instrument replaces the instrument. Multi-out instruments get all outputs enabled.
    void insertPlugin (Id trackId, FxChain chain, int index, const juce::PluginDescription& desc,
                       std::function<void (Id slotId, juce::String error)> onDone = {});
    void removePlugin (Id slotId);
    void movePlugin (Id slotId, FxChain chain, int newIndex);
    void setPluginBypass (Id slotId, bool bypassed);
    void showPluginEditor (Id slotId);
    juce::MemoryBlock getPluginState (Id slotId) const;
    void setPluginState (Id slotId, const juce::MemoryBlock& state);
    // Multi-out: creates AuxOut tracks for every extra stereo pair of the track's instrument.
    void createMultiOutTracks (Id trackId);

    // ---- presets -----------------------------------------------------------------------
    ChannelPreset captureChannelPreset (Id trackId) const;
    void applyChannelPreset (Id trackId, const ChannelPreset& preset, std::function<void (juce::String errors)> onDone = {});

    // ---- loops -------------------------------------------------------------------------
    Id addLoop (Id trackId, LoopType type);          // uses transport.defaultLoopSettings
    void removeLoop (Id loopId);
    void setLoopName (Id loopId, const juce::String& name);
    void setLoopColour (Id loopId, std::optional<juce::Colour> c);
    juce::Colour loopColour (Id loopId) const;       // own colour or the track's
    void setLoopSettings (Id loopId, const LoopSettings& s);
    void setLoopVolume (Id loopId, float linearGain);
    void setLoopMuted (Id loopId, bool muted);

    // Actions (quantised per loop settings; start the transport automatically when stopped).
    void triggerLoop (Id loopId);    // smart tap: empty->record, recording->close, playing->stop, stopped->play
    void recordLoop (Id loopId);
    void playLoop (Id loopId);
    void stopLoop (Id loopId);
    void overdubLoop (Id loopId);
    void clearLoop (Id loopId);
    void selectTake (Id loopId, int take);
    void deleteTake (Id loopId, int take);
    void stopAllLoops();
    void duplicateLoop (Id loopId);                  // MIDI/automation: copy content to a new loop

    LoopStatus loopStatus (Id loopId) const;         // cheap; call from timers

    // MIDI editing (piano roll / step sequencer). Applies to the active take.
    const MidiClip* midiClip (Id loopId) const;      // null if not a MIDI loop / no take
    void setMidiClip (Id loopId, const MidiClip& clip);   // creates take 0 if the loop is empty

    // Audio take import/export (serialization, drag & drop).
    bool exportTakeAudio (Id loopId, int take, juce::AudioBuffer<float>& out, double& lengthBeats) const;

    // ---- automation --------------------------------------------------------------------
    const std::vector<AutomationLane>* automationLanes (const AutomationOwner& o) const;
    int  addAutomationLane (const AutomationOwner& o, const ParamTarget& target); // returns lane index
    void removeAutomationLane (const AutomationOwner& o, int laneIndex);
    void setAutomationLane (const AutomationOwner& o, int laneIndex, const AutomationLane& lane);
    double automationLengthBeats (const AutomationOwner& o) const;   // loop length, or timeline length

    // ---- bounce ------------------------------------------------------------------------
    // Resamples the track's full output (all effects) for one cycle of its longest loop into a
    // new audio track/loop, then stops + hides the original on the Bounced page with its
    // plugins suspended. onDone(newTrackId) when the new loop starts playing.
    void bounceTrack (Id trackId, std::function<void (Id newTrackId)> onDone = {});
    void restoreBouncedTrack (Id trackId);   // brings a bounced track back to the main page

    // ---- transport ---------------------------------------------------------------------
    void play();
    void stop();
    void togglePlay();
    void setTempo (double bpm);
    void tapTempo();
    void setTimeSignature (int beatsPerBar, int beatUnit);
    void setMasterBars (int bars);
    void setMetronome (bool on, float volume = -1.0f);
    void setLinkEnabled (bool on);
    void setLinkStartStopSync (bool on);
    void setTransportMode (TransportMode m);
    void setTimelineLoop (bool on, double startBeat, double endBeat);
    void locate (double beat);                       // timeline mode
    void setDefaultLoopSettings (const LoopSettings& s);
    void setRecordLatencyOffset (int samples);
    TransportStatus transportStatus() const;
    MeterStatus meter (Id trackId) const;

    // ---- timeline ----------------------------------------------------------------------
    Id addTimelineClip (Id loopId, double startBeat, double lengthBeats = 0);   // 0 = loop length
    void moveTimelineClip (Id clipId, double startBeat, Id trackId = kInvalidId);
    void resizeTimelineClip (Id clipId, double lengthBeats);
    void removeTimelineClip (Id clipId);
    // While enabled in Loops mode, loop launches are written to the timeline (arrangement record).
    void setArrangementRecord (bool on);
    bool isArrangementRecording() const noexcept { return arrangementRecord; }

    // Drains engine notifications now (normally done by a 30 Hz timer). Offline renders / tests.
    void processPendingEvents() { tick(); }

    // ---- MIDI input --------------------------------------------------------------------
    // Called by MidiMapping for every incoming message (message thread) before mapping.
    std::function<void (const juce::MidiMessage&, const juce::String& deviceId)> onMidiInput;

private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
    std::unique_ptr<Session> sessionPtr;
    juce::AudioDeviceManager devices;
    PluginHost plugins;
    std::unique_ptr<AudioEngine> audioEngine;
    juce::ListenerList<Listener> listeners;
    bool arrangementRecord = false;
    double preparedRate = 0; int preparedBlock = 0;   // what plugins were last prepared with
    void notify (Change c, Id id = kInvalidId);

    // internals (EngineController.cpp)
    int maxBlock() const;
    std::shared_ptr<LoopRt> makeLoopRt (Id loopId, Id trackId, LoopType type);
    void rebuildGraph();
    void publishTransport();
    void deviceChanged();
    void retireSession (std::unique_ptr<Session> old);
    TrackPtr makeTrack (TrackKind kind, const juce::String& name, Id forcedId = kInvalidId);
    void removeSendInternal (TrackModel& t, int index);
    void recordAutomation();
    void recordArrangement();
    void preparePlugin (PluginSlot& slot, std::unique_ptr<juce::AudioPluginInstance> inst);
    std::vector<PluginSlotPtr>* chainOf (TrackModel& t, FxChain c);
    void placeSlot (TrackModel& t, FxChain chain, int index, PluginSlotPtr slot);
    void sendLoop (LoopModel& l, const LoopCommand& c);
    void loopAction (Id loopId, LoopCommandType type);
    void publishMidiTake (LoopModel& l, int take);
    void publishLoopAutomation (LoopModel& l);
    std::vector<AutomationLane>* lanesOf (const AutomationOwner& o) const;
    void publishLanes (const AutomationOwner& o);
    void finishBounce (Id loopId);
    void tick();
};

} // namespace el
