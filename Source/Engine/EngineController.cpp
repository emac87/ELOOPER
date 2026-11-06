#include "EngineController.h"
#include "AudioEngine.h"
#include "Core/Routing.h"
#include <algorithm>
#include <map>
#include <set>

namespace el {

namespace {
    juce::File deviceStateFile() { return PluginHost::appDataDir().getChildFile ("audio-device.xml"); }

    // Builds lane sets for the audio thread.
    std::unique_ptr<AutomationSet> makeSet (const std::vector<AutomationLane>& lanes, double len) {
        auto s = std::make_unique<AutomationSet>();
        s->lanes = lanes; s->lengthBeats = len;
        for (auto& l : s->lanes) l.sort();
        return s;
    }
}

// =========================================================================================
struct EngineController::Impl : public juce::Timer, public juce::ChangeListener {
    explicit Impl (EngineController& c) : owner (c) {}
    EngineController& owner;

    // MIDI recorded by loops, waiting to be merged into clips: loopId -> take -> events.
    std::map<Id, std::map<int, std::vector<MidiEvent>>> pendingMidi;
    // Bounce bookkeeping: target loop id -> (new track, callback).
    struct PendingBounce { Id source; Id target; std::function<void (Id)> done; };
    std::map<Id, PendingBounce> bounces;
    // Active UI gestures for automation recording.
    struct Gesture { Id track; ParamTarget target; double lastPhase = -1; };
    std::vector<Gesture> gestures;
    // Arrangement recording: loopId -> open clip id.
    std::map<Id, Id> openClips;
    std::vector<double> taps;
    juce::StringArray midiDeviceIds;
    int deviceTick = 0;
    bool graphDirty = false;

    void timerCallback() override { owner.tick(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { owner.deviceChanged(); }
};

// =========================================================================================
EngineController::EngineController (bool openAudioDevice)
    : impl (std::make_unique<Impl> (*this)),
      sessionPtr (std::make_unique<Session>()),
      audioEngine (std::make_unique<AudioEngine>()) {
    if (openAudioDevice) {
        std::unique_ptr<juce::XmlElement> saved;
        if (deviceStateFile().existsAsFile()) saved = juce::XmlDocument::parse (deviceStateFile());
        const auto err = devices.initialise (2, 2, saved.get(), true);
        if (err.isNotEmpty()) DBG ("Audio device: " << err);
        if (saved == nullptr) {
            // Low-latency default: 128-sample buffer if the device supports it.
            auto setup = devices.getAudioDeviceSetup();
            setup.bufferSize = 128;
            devices.setAudioDeviceSetup (setup, true);
        }
        devices.addAudioCallback (audioEngine.get());
        preparedRate = audioEngine->sampleRate(); preparedBlock = maxBlock();
        devices.addMidiInputDeviceCallback ({}, audioEngine.get());
        devices.addChangeListener (impl.get());
    } else {
        audioEngine->prepareOffline (48000.0, 512);
    }
    newSession();
    impl->startTimerHz (30);
}

EngineController::~EngineController() {
    impl->stopTimer();
    devices.removeChangeListener (impl.get());
    if (auto xml = devices.createStateXml()) xml->writeTo (deviceStateFile());
    devices.removeMidiInputDeviceCallback ({}, audioEngine.get());
    devices.removeAudioCallback (audioEngine.get());
    devices.closeAudioDevice();
    plugins.closeAllEditors();
    audioEngine->publishGraph (nullptr);
    audioEngine->retirer().collect (true);
    sessionPtr.reset();
    audioEngine->retirer().collect (true);
}

void EngineController::notify (Change c, Id id) {
    listeners.call ([c, id] (Listener& l) { l.sessionChanged (c, id); });
}

int EngineController::maxBlock() const {
    return std::max (256, audioEngine->blockSize());
}

// =========================================================================================
// Graph building
// =========================================================================================
std::shared_ptr<LoopRt> EngineController::makeLoopRt (Id loopId, Id trackId, LoopType type) {
    ChunkPool* pool = &audioEngine->chunkPool();
    return std::shared_ptr<LoopRt> (new LoopRt (loopId, trackId, type), [pool] (LoopRt* l) { l->releaseAll (*pool); delete l; });
}

static std::vector<RouteEdge> routeEdges (const Session& s, Id skipFrom = kInvalidId) {
    std::vector<RouteEdge> e;
    for (auto& t : s.tracks) {
        if (t->id == skipFrom) continue;
        if (t->kind != TrackKind::Master) e.push_back ({ t->id, t->outputBus != kInvalidId ? t->outputBus : kMasterId });
        for (auto& snd : t->sends) e.push_back ({ t->id, snd.busId });
        if (t->auxParent != kInvalidId) e.push_back ({ t->auxParent, t->id });
        if (t->recordSource != kInvalidId) e.push_back ({ t->recordSource, t->id });
    }
    return e;
}

void EngineController::rebuildGraph() {
    auto& s = *sessionPtr;
    auto g = std::make_unique<RenderGraph>();
    const int block = maxBlock();
    g->maxBlock = block;
    g->sampleRate = audioEngine->sampleRate();

    std::vector<Id> ids; for (auto& t : s.tracks) ids.push_back (t->id);
    std::vector<Id> order;
    routing::topologicalOrder (ids, routeEdges (s), order);

    std::map<Id, int> indexOf;
    for (size_t i = 0; i < order.size(); ++i) indexOf[order[i]] = (int) i;
    const RenderGraph* old = audioEngine->currentGraph();

    g->nodes.resize (order.size());
    for (size_t i = 0; i < order.size(); ++i) {
        auto t = s.findTrack (order[i]);
        auto& n = g->nodes[i];
        n.id = t->id; n.kind = t->kind; n.rt = t->rt.get();
        n.inputChannel = t->kind == TrackKind::Audio ? t->inputChannel : -1;
        n.stereoInput = t->stereoInput;
        n.midiDevice = t->midiInputDevice;
        n.midiDeviceIndex = t->midiInputDevice.isEmpty() ? -1 : std::min (7, impl->midiDeviceIds.indexOf (t->midiInputDevice));
        if (t->midiInputDevice.isNotEmpty() && n.midiDeviceIndex < 0) n.midiDeviceIndex = 7;   // unplugged: listen to nothing useful
        n.midiChannel = t->midiChannel;
        n.recordSourceIndex = t->recordSource != kInvalidId && indexOf.count (t->recordSource) ? indexOf[t->recordSource] : -1;
        n.auxParentIndex = t->auxParent != kInvalidId && indexOf.count (t->auxParent) ? indexOf[t->auxParent] : -1;
        n.auxPair = t->auxPair;

        int maxCh = 2;
        auto addPlugins = [&] (const std::vector<PluginSlotPtr>& src, std::vector<PluginNode>& dst) {
            for (auto& p : src) {
                if (! p || ! p->instance) continue;
                PluginNode pn; pn.slot = p.get(); pn.proc = p->instance.get();
                pn.numIn = pn.proc->getTotalNumInputChannels(); pn.numOut = pn.proc->getTotalNumOutputChannels();
                pn.acceptsMidi = pn.proc->acceptsMidi(); pn.producesMidi = pn.proc->producesMidi();
                maxCh = std::max (maxCh, std::max (pn.numIn, pn.numOut));
                dst.push_back (pn);
            }
        };
        addPlugins (t->midiFx, n.midiFx);
        addPlugins (t->preFx, n.pre);
        addPlugins (t->postFx, n.post);
        if (t->instrument && t->instrument->instance) {
            std::vector<PluginNode> one; addPlugins ({ t->instrument }, one);
            n.instrument = one.front(); n.hasInstrument = true;
        }
        for (auto& l : t->loops) if (l->rt) { n.loops.push_back (l->rt.get()); g->loopIndex.push_back ({ l->id, l->rt.get() }); }
        for (size_t k = 0; k < t->sends.size() && k < (size_t) kMaxSends; ++k)
            if (indexOf.count (t->sends[k].busId)) n.sends.push_back ({ indexOf[t->sends[k].busId], (int) k, t->sends[k].preFader });
        if (t->kind == TrackKind::Master) { n.outputIndex = -1; n.hardwareOutput = t->hardwareOutput; g->masterIndex = (int) i; }
        else {
            const Id out = t->outputBus != kInvalidId && indexOf.count (t->outputBus) ? t->outputBus : kMasterId;
            n.outputIndex = indexOf.count (out) ? indexOf[out] : -1;
        }

        n.bus.setSize (2, block); n.input.setSize (2, block); n.output.setSize (2, block);
        n.work.setSize (maxCh, block);
        n.bus.clear(); n.input.clear(); n.output.clear(); n.work.clear();
        n.midi.ensureSize (8192); n.fxMidi.ensureSize (2048);
        n.eq.prepare (g->sampleRate);

        // Carry DSP state over from the previous graph so rebuilds are glitch-free.
        bool carried = false;
        if (old != nullptr)
            for (auto& on : old->nodes)
                if (on.id == n.id) {
                    n.eq = on.eq; n.smoothVol = on.smoothVol; n.smoothPanL = on.smoothPanL; n.smoothPanR = on.smoothPanR;
                    n.smoothInGain = on.smoothInGain; n.smoothMonitor = on.smoothMonitor; n.smoothMute = on.smoothMute;
                    n.midiGateOpen = on.midiGateOpen;
                    std::copy (std::begin (on.smoothSend), std::end (on.smoothSend), std::begin (n.smoothSend));
                    carried = true; break;
                }
        if (! carried) {
            n.smoothVol = pm::volumeToGain (t->rt->volume.load());
            pm::panGains (t->rt->pan.load(), n.smoothPanL, n.smoothPanR);
            n.smoothInGain = pm::inputGainToGain (t->rt->inputGain.load());
            n.smoothMute = t->rt->mute.load() ? 0.0f : 1.0f;
        }
    }
    for (size_t i = 0; i < g->nodes.size(); ++i)
        if (g->nodes[i].auxParentIndex >= 0) g->nodes[(size_t) g->nodes[i].auxParentIndex].auxChildren.push_back ((int) i);
    std::sort (g->loopIndex.begin(), g->loopIndex.end());

    for (auto& c : s.timeline) {
        auto l = s.findLoop (c.loopId);
        if (! l || ! l->rt || ! indexOf.count (c.trackId)) continue;
        g->timeline.push_back ({ indexOf[c.trackId], l->rt.get(), c.startBeat, c.lengthBeats });
        if (std::find (g->timelineLoops.begin(), g->timelineLoops.end(), l->rt.get()) == g->timelineLoops.end())
            g->timelineLoops.push_back (l->rt.get());
    }
    std::sort (g->timeline.begin(), g->timeline.end(), [] (const TimelineEntry& a, const TimelineEntry& b) { return a.start < b.start; });

    audioEngine->publishGraph (std::move (g));
    impl->graphDirty = false;
}

void EngineController::publishTransport() {
    audioEngine->setTransportSettings (sessionPtr->transport);
}

void EngineController::deviceChanged() {
    // Remember the user's choice immediately (not only on a clean quit).
    if (auto xml = devices.createStateXml()) xml->writeTo (deviceStateFile());
    auto* dev = devices.getCurrentAudioDevice();
    if (dev == nullptr) return;
    const double sr = dev->getCurrentSampleRate();
    const int block = std::max (256, dev->getCurrentBufferSizeSamples());
    if (sr == preparedRate && block == preparedBlock) { notify (Change::Transport); return; }  // only channels changed
    // Rate or buffer size changed: re-prepare plugins with the callback detached.
    devices.removeAudioCallback (audioEngine.get());
    for (auto& t : sessionPtr->tracks)
        for (auto& p : t->allPlugins())
            if (p && p->instance) {
                p->instance->releaseResources();
                p->instance->setRateAndBufferSizeDetails (sr, block);
                p->instance->prepareToPlay (sr, block);
            }
    preparedRate = sr; preparedBlock = block;
    rebuildGraph();
    devices.addAudioCallback (audioEngine.get());
    notify (Change::Transport);
}

// =========================================================================================
// Session
// =========================================================================================
void EngineController::retireSession (std::unique_ptr<Session> old) {
    if (! old) return;
    plugins.closeAllEditors();
    audioEngine->retirer().retire (std::shared_ptr<Session> (old.release()));
}

TrackPtr EngineController::makeTrack (TrackKind kind, const juce::String& name, Id forcedId) {
    auto& s = *sessionPtr;
    auto t = std::make_shared<TrackModel>();
    t->id = forcedId != kInvalidId ? forcedId : s.allocateId();
    t->kind = kind;
    int count = 0;
    for (auto& o : s.tracks) if (o->kind == kind) ++count;
    const juce::String base = kind == TrackKind::Audio ? "Audio" : kind == TrackKind::Instrument ? "Inst"
                            : kind == TrackKind::Bus ? "Bus" : kind == TrackKind::Master ? "Master" : "Out";
    t->name = name.isNotEmpty() ? name : (kind == TrackKind::Master ? base : base + " " + juce::String (count + 1));
    t->colour = kind == TrackKind::Master ? juce::Colour (0xffe0e0e0) : paletteColour ((int) s.tracks.size());
    t->inputChannel = kind == TrackKind::Audio ? 0 : -1;
    t->outputBus = kMasterId;
    t->rt = std::make_shared<TrackRt> (t->id);
    return t;
}

void EngineController::newSession() {
    auto fresh = std::make_unique<Session>();
    std::swap (fresh, sessionPtr);
    impl->pendingMidi.clear(); impl->bounces.clear(); impl->gestures.clear(); impl->openClips.clear();
    auto& s = *sessionPtr;
    s.tracks.push_back (makeTrack (TrackKind::Master, "Master", kMasterId));
    s.tracks.push_back (makeTrack (TrackKind::Audio, {}));
    s.tracks.push_back (makeTrack (TrackKind::Instrument, {}));
    rebuildGraph();
    retireSession (std::move (fresh));
    publishTransport();
    audioEngine->clock().setTempo (s.transport.tempo);
    audioEngine->clock().setEnabled (s.transport.linkEnabled);
    audioEngine->clock().setStartStopSync (s.transport.linkStartStopSync);
    notify (Change::Session);
}

void EngineController::loadSession (std::unique_ptr<Session> s, std::function<void (juce::StringArray)> onDone) {
    stop();
    auto old = std::move (sessionPtr);
    sessionPtr = std::move (s);
    impl->pendingMidi.clear(); impl->bounces.clear(); impl->gestures.clear(); impl->openClips.clear();
    auto& ss = *sessionPtr;
    if (! ss.master()) ss.tracks.insert (ss.tracks.begin(), makeTrack (TrackKind::Master, "Master", kMasterId));
    BlockTime offlineTime; offlineTime.beatsPerBar = ss.transport.beatsPerBar;
    LoopContext ctx; ctx.pool = &audioEngine->chunkPool();
    Id maxId = 1;
    for (auto& t : ss.tracks) {
        maxId = std::max (maxId, t->id);
        if (! t->rt) t->rt = std::make_shared<TrackRt> (t->id);
        t->rt->bounced = t->bounced;
        t->rt->automation.publish (makeSet (t->automation, 0), audioEngine->retirer());
        for (auto& l : t->loops) {
            maxId = std::max (maxId, l->id);
            l->trackId = t->id;
            l->rt = makeLoopRt (l->id, t->id, l->type);
            l->rt->setSettingsOffline (l->settings);
            l->rt->volume = l->volume;
            for (size_t k = 0; k < l->pendingAudio.size() && k < (size_t) kMaxTakes; ++k) {
                auto& pa = l->pendingAudio[k];
                if (pa.audio.getNumSamples() > 0)
                    l->rt->loadTake ((int) k, pa.audio.getArrayOfReadPointers(), pa.audio.getNumChannels(), pa.audio.getNumSamples(), pa.beats, audioEngine->chunkPool());
            }
            l->pendingAudio.clear();
            for (size_t k = 0; k < l->midiTakes.size() && k < (size_t) kMaxTakes; ++k) {
                LoopCommand def { LoopCommandType::DefineTake }; def.take = (int) k; def.lengthBeats = l->midiTakes[k].lengthBeats;
                l->rt->command (def, offlineTime, ctx);   // safe: not reachable by the audio thread yet
                l->rt->midiSequences[k].publish (std::make_unique<MidiSequence> (MidiSequence::fromClip (l->midiTakes[k])), audioEngine->retirer());
            }
            if (l->type == LoopType::Automation && ! l->automation.empty()) {
                LoopCommand def { LoopCommandType::DefineTake }; def.take = 0; def.lengthBeats = l->automationLengthBeats;
                l->rt->command (def, offlineTime, ctx);
            }
            l->rt->setActiveTakeOffline (std::clamp (l->pendingActiveTake, 0, std::max (0, l->rt->takeCount() - 1)));
            l->rt->setStoppedOffline();
            publishLoopAutomation (*l);
        }
    }
    ss.nextId = std::max (ss.nextId, maxId + 1);
    rebuildGraph();
    retireSession (std::move (old));
    publishTransport();
    audioEngine->clock().setTempo (ss.transport.tempo);
    audioEngine->clock().setEnabled (ss.transport.linkEnabled);
    audioEngine->clock().setStartStopSync (ss.transport.linkStartStopSync);
    notify (Change::Session);

    // Instantiate plugins; the graph is rebuilt as each one arrives.
    struct Pending { std::shared_ptr<int> remaining; std::shared_ptr<juce::StringArray> errors; };
    auto remaining = std::make_shared<int> (0);
    auto errors = std::make_shared<juce::StringArray>();
    std::vector<std::pair<Id, PluginSlotPtr>> todo;
    for (auto& t : ss.tracks) for (auto& p : t->allPlugins()) if (p && ! p->instance) todo.push_back ({ t->id, p });
    *remaining = (int) todo.size();
    if (todo.empty()) { if (onDone) onDone ({}); return; }
    Session* expected = sessionPtr.get();
    for (auto& [trackId, slot] : todo) {
        PluginSlotPtr sp = slot;
        plugins.createInstance (sp->description, audioEngine->sampleRate(), maxBlock(),
            [this, sp, expected, remaining, errors, onDone] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err) {
                if (sessionPtr.get() != expected) return;     // a different session was loaded meanwhile
                if (inst) {
                    preparePlugin (*sp, std::move (inst));
                    if (sp->pendingState.getSize() > 0) sp->instance->setStateInformation (sp->pendingState.getData(), (int) sp->pendingState.getSize());
                    sp->pendingState.reset();
                    impl->graphDirty = true;
                } else {
                    errors->add (sp->name() + ": " + (err.isNotEmpty() ? err : juce::String ("could not be loaded")));
                }
                if (--*remaining == 0) {
                    rebuildGraph();
                    notify (Change::Structure);
                    if (onDone) onDone (*errors);
                }
            });
    }
}

// =========================================================================================
// Tracks
// =========================================================================================
Id EngineController::addTrack (TrackKind kind, const juce::String& name) {
    if (kind == TrackKind::Master) return kInvalidId;
    auto t = makeTrack (kind, name);
    auto& tr = sessionPtr->tracks;
    if (kind == TrackKind::Bus) tr.push_back (t);
    else {
        // Keep buses after regular tracks in the default order.
        auto it = std::find_if (tr.begin(), tr.end(), [] (const TrackPtr& x) { return x->kind == TrackKind::Bus; });
        tr.insert (it, t);
    }
    rebuildGraph();
    notify (Change::Structure, t->id);
    return t->id;
}

void EngineController::removeTrack (Id trackId) {
    auto& s = *sessionPtr;
    if (trackId == kMasterId) return;
    auto t = s.findTrack (trackId);
    if (! t) return;
    for (auto& p : t->allPlugins()) if (p) plugins.closeEditor (p->id);
    // Detach everything that referenced it.
    std::vector<Id> aux;
    for (auto& o : s.tracks) {
        if (o->outputBus == trackId) o->outputBus = kMasterId;
        for (int k = (int) o->sends.size() - 1; k >= 0; --k) if (o->sends[(size_t) k].busId == trackId) removeSendInternal (*o, k);
        if (o->auxParent == trackId) aux.push_back (o->id);
        if (o->recordSource == trackId) o->recordSource = kInvalidId;
        if (o->bouncedInto == trackId) o->bouncedInto = kInvalidId;
    }
    std::set<Id> loops; for (auto& l : t->loops) loops.insert (l->id);
    s.timeline.erase (std::remove_if (s.timeline.begin(), s.timeline.end(), [&] (const TimelineClip& c) { return c.trackId == trackId || loops.count (c.loopId); }), s.timeline.end());
    s.tracks.erase (std::remove (s.tracks.begin(), s.tracks.end(), t), s.tracks.end());
    rebuildGraph();
    audioEngine->retirer().retire (t);
    for (auto a : aux) removeTrack (a);
    notify (Change::Structure, trackId);
}

void EngineController::moveTrack (Id trackId, int newIndex) {
    auto& tr = sessionPtr->tracks;
    auto it = std::find_if (tr.begin(), tr.end(), [trackId] (const TrackPtr& t) { return t->id == trackId; });
    if (it == tr.end()) return;
    auto t = *it; tr.erase (it);
    newIndex = std::clamp (newIndex, 0, (int) tr.size());
    tr.insert (tr.begin() + newIndex, t);
    notify (Change::Structure, trackId);
}

void EngineController::setTrackName (Id id, const juce::String& n) {
    if (auto t = sessionPtr->findTrack (id)) { t->name = n; notify (Change::Track, id); }
}

void EngineController::setTrackColour (Id id, juce::Colour c) {
    if (auto t = sessionPtr->findTrack (id)) {
        t->colour = c;
        notify (Change::Track, id);
        for (auto& l : t->loops) if (! l->colour) notify (Change::Loop, l->id);
    }
}

void EngineController::setTrackInput (Id id, int ch, bool stereo) {
    auto t = sessionPtr->findTrack (id);
    if (! t) return;
    t->inputChannel = ch; t->stereoInput = stereo;
    // Make sure the chosen hardware input(s) are actually enabled on the device.
    if (ch >= 0 && devices.getCurrentAudioDevice() != nullptr) {
        auto setup = devices.getAudioDeviceSetup();
        const bool missing = ! setup.inputChannels[ch] || (stereo && ! setup.inputChannels[ch + 1]);
        if (missing) {
            setup.useDefaultInputChannels = false;
            setup.inputChannels.setBit (ch);
            if (stereo) setup.inputChannels.setBit (ch + 1);
            devices.setAudioDeviceSetup (setup, true);
        }
    }
    rebuildGraph();
    notify (Change::Track, id);
}

void EngineController::setTrackMidiInput (Id id, const juce::String& dev, int channel) {
    if (auto t = sessionPtr->findTrack (id)) { t->midiInputDevice = dev; t->midiChannel = std::clamp (channel, 0, 16); rebuildGraph(); notify (Change::Track, id); }
}

void EngineController::setMonitorMode (Id id, MonitorMode m) {
    if (auto t = sessionPtr->findTrack (id)) { t->rt->monitor = m; notify (Change::Track, id); }
}

void EngineController::setArmed (Id id, bool a) {
    if (auto t = sessionPtr->findTrack (id)) { t->rt->armed = a; notify (Change::Track, id); }
}

bool EngineController::setTrackOutput (Id id, Id busId) {
    auto& s = *sessionPtr;
    auto t = s.findTrack (id); auto b = s.findTrack (busId);
    if (! t || ! b || ! b->isBusLike() || t->kind == TrackKind::Master) return false;
    auto edges = routeEdges (s, id);
    for (auto& snd : t->sends) edges.push_back ({ id, snd.busId });
    if (t->auxParent != kInvalidId) edges.push_back ({ t->auxParent, id });
    if (routing::wouldCreateCycle (edges, id, busId)) return false;
    t->outputBus = busId;
    rebuildGraph();
    notify (Change::Structure, id);
    return true;
}

void EngineController::setMasterHardwareOutput (int ch) {
    if (auto m = sessionPtr->master()) { m->hardwareOutput = std::max (0, ch); rebuildGraph(); notify (Change::Track, kMasterId); }
}

int EngineController::addSend (Id id, Id busId, bool pre) {
    auto& s = *sessionPtr;
    auto t = s.findTrack (id); auto b = s.findTrack (busId);
    if (! t || ! b || b->kind != TrackKind::Bus || (int) t->sends.size() >= kMaxSends) return -1;
    for (auto& snd : t->sends) if (snd.busId == busId) return -1;
    if (routing::wouldCreateCycle (routeEdges (s), id, busId)) return -1;
    t->sends.push_back ({ busId, pre });
    const int k = (int) t->sends.size() - 1;
    t->rt->sendLevel[k] = pm::kUnityVolume * 0.85f;
    rebuildGraph();
    notify (Change::Track, id);
    return k;
}

void EngineController::removeSendInternal (TrackModel& t, int k) {
    if (k < 0 || k >= (int) t.sends.size()) return;
    t.sends.erase (t.sends.begin() + k);
    for (int i = k; i < kMaxSends - 1; ++i) t.rt->sendLevel[i] = t.rt->sendLevel[i + 1].load();
    t.rt->sendLevel[kMaxSends - 1] = 0;
}

void EngineController::removeSend (Id id, int k) {
    if (auto t = sessionPtr->findTrack (id)) { removeSendInternal (*t, k); rebuildGraph(); notify (Change::Track, id); }
}

void EngineController::setSendPreFader (Id id, int k, bool pre) {
    if (auto t = sessionPtr->findTrack (id))
        if (k >= 0 && k < (int) t->sends.size()) { t->sends[(size_t) k].preFader = pre; rebuildGraph(); notify (Change::Track, id); }
}

// =========================================================================================
// Parameters
// =========================================================================================
static juce::AudioProcessorParameter* pluginParam (const TrackModel& t, Id slotId, int index) {
    auto p = t.findPlugin (slotId);
    if (! p || ! p->instance) return nullptr;
    const auto& params = p->instance->getParameters();
    return index >= 0 && index < params.size() ? params.getUnchecked (index) : nullptr;
}

void EngineController::setParam (Id trackId, const ParamTarget& p, float v) {
    auto t = sessionPtr->findTrack (trackId);
    if (! t) return;
    v = juce::jlimit (0.0f, 1.0f, v);
    auto& rt = *t->rt;
    switch (p.kind) {
        case ParamKind::Volume: rt.volume = v; break;
        case ParamKind::Pan: rt.pan = v; break;
        case ParamKind::InputGain: rt.inputGain = v; break;
        case ParamKind::Mute: rt.mute = v > 0.5f; break;
        case ParamKind::EqBand: if (p.index >= 0 && p.index < kEqBands) rt.eq[p.index] = v; break;
        case ParamKind::SendLevel:
            for (size_t k = 0; k < t->sends.size(); ++k) if (t->sends[k].busId == p.objectId) rt.sendLevel[k] = v;
            break;
        case ParamKind::PluginParam: if (auto* prm = pluginParam (*t, p.objectId, p.index)) prm->setValueNotifyingHost (v); break;
        case ParamKind::PluginBypass: if (auto s = t->findPlugin (p.objectId)) s->bypassed = v > 0.5f; break;
        case ParamKind::LoopVolume:
            for (auto& l : t->loops) if (l->id == p.objectId) { l->volume = pm::volumeToGain (v); if (l->rt) l->rt->volume = l->volume; }
            break;
    }
}

float EngineController::getParam (Id trackId, const ParamTarget& p) const {
    auto t = sessionPtr->findTrack (trackId);
    if (! t) return 0;
    auto& rt = *t->rt;
    switch (p.kind) {
        case ParamKind::Volume: return rt.volume;
        case ParamKind::Pan: return rt.pan;
        case ParamKind::InputGain: return rt.inputGain;
        case ParamKind::Mute: return rt.mute ? 1.0f : 0.0f;
        case ParamKind::EqBand: return p.index >= 0 && p.index < kEqBands ? rt.eq[p.index].load() : 0.5f;
        case ParamKind::SendLevel:
            for (size_t k = 0; k < t->sends.size(); ++k) if (t->sends[k].busId == p.objectId) return rt.sendLevel[k];
            return 0;
        case ParamKind::PluginParam: if (auto* prm = pluginParam (*t, p.objectId, p.index)) return prm->getValue(); return 0;
        case ParamKind::PluginBypass: if (auto s = t->findPlugin (p.objectId)) return s->bypassed ? 1.0f : 0.0f; return 0;
        case ParamKind::LoopVolume:
            for (auto& l : t->loops) if (l->id == p.objectId) return pm::gainToVolume (l->rt ? l->rt->volume.load() : l->volume);
            return 0;
    }
    return 0;
}

std::vector<ParamInfo> EngineController::listParams (Id trackId) const {
    std::vector<ParamInfo> v;
    auto t = sessionPtr->findTrack (trackId);
    if (! t) return v;
    auto add = [&] (ParamKind k, Id obj, int idx, const juce::String& name, const juce::String& group) {
        ParamInfo i; i.target = { k, obj, idx }; i.name = name; i.group = group; v.push_back (i);
    };
    add (ParamKind::Volume, 0, -1, "Volume", "Mixer");
    add (ParamKind::Pan, 0, -1, "Pan", "Mixer");
    add (ParamKind::InputGain, 0, -1, "Input Gain", "Mixer");
    add (ParamKind::Mute, 0, -1, "Mute", "Mixer");
    static const char* bandNames[kEqBands] = { "31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k" };
    for (int b = 0; b < kEqBands; ++b) add (ParamKind::EqBand, 0, b, juce::String ("EQ ") + bandNames[b], "EQ");
    for (auto& snd : t->sends)
        if (auto b = sessionPtr->findTrack (snd.busId)) add (ParamKind::SendLevel, snd.busId, -1, "Send " + b->name, "Sends");
    for (auto& l : t->loops)
        if (l->type != LoopType::Automation) add (ParamKind::LoopVolume, l->id, -1, (l->name.isNotEmpty() ? l->name : "Loop " + juce::String (l->id)) + " Volume", "Loops");
    for (auto& p : t->allPlugins()) {
        if (! p || ! p->instance) continue;
        add (ParamKind::PluginBypass, p->id, -1, "Bypass", p->name());
        const auto& params = p->instance->getParameters();
        for (int i = 0; i < params.size(); ++i)
            if (params[i]->isAutomatable()) add (ParamKind::PluginParam, p->id, i, params[i]->getName (64), p->name());
    }
    return v;
}

void EngineController::beginParamGesture (Id trackId, const ParamTarget& p) {
    auto t = sessionPtr->findTrack (trackId);
    if (! t) return;
    for (auto& g : impl->gestures) if (g.track == trackId && g.target == p) return;
    impl->gestures.push_back ({ trackId, p, -1 });
    const auto key = TrackRt::touchKey (p);
    for (auto& slot : t->rt->touched) { uint64_t z = 0; if (slot.compare_exchange_strong (z, key)) break; }
}

void EngineController::endParamGesture (Id trackId, const ParamTarget& p) {
    auto& gs = impl->gestures;
    gs.erase (std::remove_if (gs.begin(), gs.end(), [&] (const Impl::Gesture& g) { return g.track == trackId && g.target == p; }), gs.end());
    if (auto t = sessionPtr->findTrack (trackId)) {
        const auto key = TrackRt::touchKey (p);
        for (auto& slot : t->rt->touched) { uint64_t k = key; slot.compare_exchange_strong (k, 0); }
    }
}

// Writes held controls into the automation layer of every loop on the track that is
// recording or overdubbing (and into track lanes while timeline-recording).
void EngineController::recordAutomation() {
    for (auto& g : impl->gestures) {
        auto t = sessionPtr->findTrack (g.track);
        if (! t) continue;
        const float v = getParam (g.track, g.target);
        for (auto& l : t->loops) {
            if (! l->rt) continue;
            const auto st = l->rt->uiState.load();
            if (st != LoopState::Recording && st != LoopState::Overdubbing) continue;
            const double phase = l->rt->uiPhaseBeats.load();
            auto it = std::find_if (l->automation.begin(), l->automation.end(), [&] (const AutomationLane& a) { return a.target == g.target; });
            if (it == l->automation.end()) { AutomationLane lane; lane.target = g.target; l->automation.push_back (lane); it = l->automation.end() - 1; }
            // Touch mode: replace what was there since the last write.
            if (g.lastPhase >= 0 && phase > g.lastPhase) it->erase (g.lastPhase + 1.0e-6, phase);
            it->setPoint (phase, v);
            publishLoopAutomation (*l);
            notify (Change::Automation, l->id);
        }
        const auto ts = transportStatus();
        if (arrangementRecord && sessionPtr->transport.mode == TransportMode::Timeline && ts.playing) {
            auto it = std::find_if (t->automation.begin(), t->automation.end(), [&] (const AutomationLane& a) { return a.target == g.target; });
            if (it == t->automation.end()) { AutomationLane lane; lane.target = g.target; t->automation.push_back (lane); it = t->automation.end() - 1; }
            it->setPoint (ts.beat, v);
            t->rt->automation.publish (makeSet (t->automation, 0), audioEngine->retirer());
            notify (Change::Automation, t->id);
        }
        g.lastPhase = 0;
        for (auto& l : t->loops) if (l->rt && (l->rt->uiState.load() == LoopState::Recording || l->rt->uiState.load() == LoopState::Overdubbing)) g.lastPhase = l->rt->uiPhaseBeats.load();
    }
}

// =========================================================================================
// Plugins
// =========================================================================================
void EngineController::preparePlugin (PluginSlot& slot, std::unique_ptr<juce::AudioPluginInstance> inst) {
    if (slot.description.isInstrument) slot.numOutputChannels = PluginHost::enableMultiOut (*inst);
    else slot.numOutputChannels = inst->getTotalNumOutputChannels();
    preparedRate = audioEngine->sampleRate(); preparedBlock = maxBlock();
    inst->setRateAndBufferSizeDetails (preparedRate, preparedBlock);
    inst->prepareToPlay (preparedRate, preparedBlock);
    slot.latencySamples = inst->getLatencySamples();
    slot.instance = std::move (inst);
}

std::vector<PluginSlotPtr>* EngineController::chainOf (TrackModel& t, FxChain c) {
    switch (c) {
        case FxChain::MidiFx: return &t.midiFx;
        case FxChain::Pre: return &t.preFx;
        case FxChain::Post: return &t.postFx;
        case FxChain::Instrument: return nullptr;
    }
    return nullptr;
}

void EngineController::placeSlot (TrackModel& t, FxChain chain, int index, PluginSlotPtr slot) {
    if (chain == FxChain::Instrument) {
        if (t.instrument) { plugins.closeEditor (t.instrument->id); audioEngine->retirer().retire (t.instrument); }
        t.instrument = slot;
    } else {
        auto* v = chainOf (t, chain);
        index = index < 0 ? (int) v->size() : std::min (index, (int) v->size());
        v->insert (v->begin() + index, slot);
    }
}

void EngineController::insertPlugin (Id trackId, FxChain chain, int index, const juce::PluginDescription& desc,
                                     std::function<void (Id, juce::String)> onDone) {
    Session* expected = sessionPtr.get();
    plugins.createInstance (desc, audioEngine->sampleRate(), maxBlock(),
        [this, trackId, chain, index, desc, onDone, expected] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err) {
            auto t = sessionPtr.get() == expected ? sessionPtr->findTrack (trackId) : nullptr;
            if (! inst || ! t) { if (onDone) onDone (kInvalidId, inst ? juce::String ("Track was removed") : err); return; }
            auto slot = std::make_shared<PluginSlot>();
            slot->id = sessionPtr->allocateId();
            slot->description = desc;
            preparePlugin (*slot, std::move (inst));
            placeSlot (*t, chain, index, slot);
            rebuildGraph();
            notify (Change::Track, trackId);
            if (chain == FxChain::Instrument && slot->numOutputChannels > 2) notify (Change::Structure, trackId);
            if (onDone) onDone (slot->id, {});
        });
}

void EngineController::removePlugin (Id slotId) {
    auto [t, slot] = sessionPtr->findPlugin (slotId);
    if (! t) return;
    plugins.closeEditor (slotId);
    if (t->instrument == slot) t->instrument.reset();
    for (auto* v : { &t->midiFx, &t->preFx, &t->postFx }) v->erase (std::remove (v->begin(), v->end(), slot), v->end());
    rebuildGraph();
    audioEngine->retirer().retire (slot);
    notify (Change::Track, t->id);
}

void EngineController::movePlugin (Id slotId, FxChain chain, int newIndex) {
    auto [t, slot] = sessionPtr->findPlugin (slotId);
    if (! t || chain == FxChain::Instrument || t->instrument == slot) return;
    for (auto* v : { &t->midiFx, &t->preFx, &t->postFx }) v->erase (std::remove (v->begin(), v->end(), slot), v->end());
    placeSlot (*t, chain, newIndex, slot);
    rebuildGraph();
    notify (Change::Track, t->id);
}

void EngineController::setPluginBypass (Id slotId, bool b) {
    auto [t, slot] = sessionPtr->findPlugin (slotId);
    if (slot) { slot->bypassed = b; notify (Change::Track, t->id); }
}

void EngineController::showPluginEditor (Id slotId) {
    auto [t, slot] = sessionPtr->findPlugin (slotId);
    if (slot && slot->instance) plugins.showEditor (slot);
}

juce::MemoryBlock EngineController::getPluginState (Id slotId) const {
    juce::MemoryBlock mb;
    auto [t, slot] = sessionPtr->findPlugin (slotId);
    if (slot && slot->instance) slot->instance->getStateInformation (mb);
    else if (slot) mb = slot->pendingState;
    return mb;
}

void EngineController::setPluginState (Id slotId, const juce::MemoryBlock& state) {
    auto [t, slot] = sessionPtr->findPlugin (slotId);
    if (! slot) return;
    if (slot->instance) slot->instance->setStateInformation (state.getData(), (int) state.getSize());
    else slot->pendingState = state;
    notify (Change::Track, t->id);
}

void EngineController::createMultiOutTracks (Id trackId) {
    auto& s = *sessionPtr;
    auto t = s.findTrack (trackId);
    if (! t || ! t->instrument || t->instrument->numOutputChannels <= 2) return;
    const int pairs = t->instrument->numOutputChannels / 2;
    auto insertAt = std::find (s.tracks.begin(), s.tracks.end(), t) + 1;
    for (int k = 1; k < pairs; ++k) {
        bool exists = false;
        for (auto& o : s.tracks) if (o->auxParent == trackId && o->auxPair == k) exists = true;
        if (exists) continue;
        auto a = makeTrack (TrackKind::AuxOut, t->name + " " + juce::String (k * 2 + 1) + "/" + juce::String (k * 2 + 2));
        a->auxParent = trackId; a->auxPair = k; a->colour = t->colour;
        insertAt = s.tracks.insert (insertAt, a) + 1;
    }
    rebuildGraph();
    notify (Change::Structure, trackId);
}

// =========================================================================================
// Presets
// =========================================================================================
ChannelPreset EngineController::captureChannelPreset (Id trackId) const {
    ChannelPreset p;
    auto t = sessionPtr->findTrack (trackId);
    if (! t) return p;
    p.name = t->name;
    p.inputGain = t->rt->inputGain; p.volume = t->rt->volume; p.pan = t->rt->pan;
    p.eqEnabled = t->rt->eqEnabled; for (int b = 0; b < kEqBands; ++b) p.eq[b] = t->rt->eq[b];
    p.monitor = t->rt->monitor;
    auto cap = [this] (const PluginSlotPtr& s) { PluginPresetData d; d.description = s->description; d.state = getPluginState (s->id); d.bypassed = s->bypassed; return d; };
    for (auto& s : t->midiFx) p.midiFx.push_back (cap (s));
    if (t->instrument) p.instrument = cap (t->instrument);
    for (auto& s : t->preFx) p.preFx.push_back (cap (s));
    for (auto& s : t->postFx) p.postFx.push_back (cap (s));
    return p;
}

void EngineController::applyChannelPreset (Id trackId, const ChannelPreset& p, std::function<void (juce::String)> onDone) {
    auto t = sessionPtr->findTrack (trackId);
    if (! t) { if (onDone) onDone ("Track not found"); return; }
    t->rt->inputGain = p.inputGain; t->rt->volume = p.volume; t->rt->pan = p.pan;
    t->rt->eqEnabled = p.eqEnabled; for (int b = 0; b < kEqBands; ++b) t->rt->eq[b] = p.eq[b];
    t->rt->monitor = p.monitor;
    for (auto& s : t->allPlugins()) if (s) removePlugin (s->id);

    struct Item { FxChain chain; PluginPresetData data; };
    auto items = std::make_shared<std::vector<Item>>();
    for (auto& d : p.midiFx) items->push_back ({ FxChain::MidiFx, d });
    if (p.instrument && t->kind == TrackKind::Instrument) items->push_back ({ FxChain::Instrument, *p.instrument });
    for (auto& d : p.preFx) items->push_back ({ FxChain::Pre, d });
    for (auto& d : p.postFx) items->push_back ({ FxChain::Post, d });
    auto errors = std::make_shared<juce::StringArray>();
    // Insert sequentially so the chain order is preserved.
    auto next = std::make_shared<std::function<void (size_t)>>();
    *next = [this, trackId, items, errors, onDone, next] (size_t i) {
        if (i >= items->size()) { notify (Change::Track, trackId); if (onDone) onDone (errors->joinIntoString ("\n")); return; }
        const auto item = (*items)[i];
        insertPlugin (trackId, item.chain, -1, item.data.description, [this, item, i, errors, next] (Id slot, juce::String err) {
            if (slot == kInvalidId) errors->add (item.data.description.name + ": " + err);
            else { if (item.data.state.getSize() > 0) setPluginState (slot, item.data.state); setPluginBypass (slot, item.data.bypassed); }
            (*next) (i + 1);
        });
    };
    (*next) (0);
}

// =========================================================================================
// Loops
// =========================================================================================
Id EngineController::addLoop (Id trackId, LoopType type) {
    auto t = sessionPtr->findTrack (trackId);
    if (! t) return kInvalidId;
    auto l = std::make_shared<LoopModel>();
    l->id = sessionPtr->allocateId();
    l->trackId = trackId;
    l->type = type;
    l->settings = sessionPtr->transport.defaultLoopSettings;
    const char* base = type == LoopType::Audio ? "Audio" : type == LoopType::Midi ? "MIDI" : "Auto";
    l->name = juce::String (base) + " " + juce::String ((int) t->loops.size() + 1);
    l->automationLengthBeats = (double) sessionPtr->transport.masterBars * sessionPtr->transport.beatsPerBar;
    l->rt = makeLoopRt (l->id, trackId, type);
    l->rt->setSettingsOffline (l->settings);
    t->loops.push_back (l);
    publishLoopAutomation (*l);
    rebuildGraph();
    notify (Change::Structure, l->id);
    return l->id;
}

void EngineController::removeLoop (Id loopId) {
    auto& s = *sessionPtr;
    auto t = s.trackOfLoop (loopId);
    if (! t) return;
    auto l = s.findLoop (loopId);
    t->loops.erase (std::remove (t->loops.begin(), t->loops.end(), l), t->loops.end());
    s.timeline.erase (std::remove_if (s.timeline.begin(), s.timeline.end(), [loopId] (const TimelineClip& c) { return c.loopId == loopId; }), s.timeline.end());
    impl->pendingMidi.erase (loopId);
    rebuildGraph();
    audioEngine->retirer().retire (l);
    notify (Change::Structure, loopId);
}

void EngineController::setLoopName (Id id, const juce::String& n) { if (auto l = sessionPtr->findLoop (id)) { l->name = n; notify (Change::Loop, id); } }
void EngineController::setLoopColour (Id id, std::optional<juce::Colour> c) { if (auto l = sessionPtr->findLoop (id)) { l->colour = c; notify (Change::Loop, id); } }

juce::Colour EngineController::loopColour (Id id) const {
    auto l = sessionPtr->findLoop (id);
    if (! l) return juce::Colours::grey;
    if (l->colour) return *l->colour;
    auto t = sessionPtr->findTrack (l->trackId);
    return t ? t->colour : juce::Colours::grey;
}

void EngineController::setLoopSettings (Id id, const LoopSettings& s) {
    auto l = sessionPtr->findLoop (id);
    if (! l) return;
    l->settings = s;
    LoopCommand c { LoopCommandType::SetSettings }; c.settings = s;
    sendLoop (*l, c);
    notify (Change::Loop, id);
}

void EngineController::setLoopVolume (Id id, float g) { if (auto l = sessionPtr->findLoop (id)) { l->volume = g; l->rt->volume = g; notify (Change::Loop, id); } }
void EngineController::setLoopMuted (Id id, bool m) { if (auto l = sessionPtr->findLoop (id)) { l->rt->muted = m; notify (Change::Loop, id); } }

void EngineController::sendLoop (LoopModel& l, const LoopCommand& c) {
    if (! l.rt) return;
    EngineCommand e { EngineCommandType::Loop }; e.loop = l.rt.get(); e.loopCmd = c;
    audioEngine->send (e);
}

void EngineController::loopAction (Id id, LoopCommandType type) {
    auto l = sessionPtr->findLoop (id);
    if (! l) return;
    LoopCommand c { type };
    sendLoop (*l, c);
}

void EngineController::triggerLoop (Id id) { loopAction (id, LoopCommandType::Trigger); }
void EngineController::recordLoop (Id id)  { loopAction (id, LoopCommandType::Record); }
void EngineController::playLoop (Id id)    { loopAction (id, LoopCommandType::Play); }
void EngineController::stopLoop (Id id)    { loopAction (id, LoopCommandType::Stop); }
void EngineController::overdubLoop (Id id) { loopAction (id, LoopCommandType::Overdub); }

void EngineController::clearLoop (Id id) {
    auto l = sessionPtr->findLoop (id);
    if (! l) return;
    loopAction (id, LoopCommandType::Clear);
    l->midiTakes.clear();
    for (int k = 0; k < kMaxTakes; ++k) l->rt->midiSequences[k].publish (nullptr, audioEngine->retirer());
    impl->pendingMidi.erase (id);
    notify (Change::Loop, id);
    notify (Change::Clip, id);
}

void EngineController::selectTake (Id id, int take) {
    auto l = sessionPtr->findLoop (id);
    if (! l) return;
    LoopCommand c { LoopCommandType::SelectTake }; c.take = take;
    sendLoop (*l, c);
    notify (Change::Loop, id);
    notify (Change::Clip, id);
}

void EngineController::deleteTake (Id id, int take) {
    auto l = sessionPtr->findLoop (id);
    if (! l) return;
    LoopCommand c { LoopCommandType::DeleteTake }; c.take = take;
    sendLoop (*l, c);   // MIDI clips are shifted when the engine confirms (TakeDeleted)
}

void EngineController::stopAllLoops() {
    EngineCommand e { EngineCommandType::AllLoopsStop };
    audioEngine->send (e);
}

void EngineController::duplicateLoop (Id id) {
    auto src = sessionPtr->findLoop (id);
    if (! src || src->type == LoopType::Audio) return;
    const Id nid = addLoop (src->trackId, src->type);
    auto dst = sessionPtr->findLoop (nid);
    dst->settings = src->settings; dst->colour = src->colour; dst->name = src->name + " copy";
    dst->automation = src->automation; dst->automationLengthBeats = src->automationLengthBeats;
    setLoopSettings (nid, dst->settings);
    if (auto* clip = midiClip (id)) setMidiClip (nid, *clip);
    if (src->type == LoopType::Automation) {
        LoopCommand def { LoopCommandType::DefineTake }; def.take = 0;
        def.lengthBeats = std::max (src->rt->uiLengthBeats.load(), dst->automationLengthBeats);
        sendLoop (*dst, def);
    }
    publishLoopAutomation (*dst);
    notify (Change::Loop, nid);
}

LoopStatus EngineController::loopStatus (Id id) const {
    LoopStatus s;
    auto l = sessionPtr->findLoop (id);
    if (! l || ! l->rt) return s;
    auto& r = *l->rt;
    s.state = r.uiState.load(); s.pending = r.uiPending.load();
    s.phaseBeats = r.uiPhaseBeats.load(); s.lengthBeats = r.uiLengthBeats.load(); s.recordedBeats = r.uiRecordedBeats.load();
    s.activeTake = r.uiActiveTake.load(); s.numTakes = r.uiNumTakes.load();
    s.peak = r.uiPeak.load(); r.uiPeak.store (s.peak * 0.85f);
    if (s.state == LoopState::Recording) {
        const double len = s.lengthBeats > 0 ? s.lengthBeats : sessionPtr->transport.beatsPerBar;
        s.progress = (float) (s.lengthBeats > 0 ? std::min (1.0, s.recordedBeats / len) : std::fmod (s.recordedBeats, len) / len);
    } else if (s.lengthBeats > 0) {
        s.progress = (float) (s.phaseBeats / s.lengthBeats);
    }
    return s;
}

const MidiClip* EngineController::midiClip (Id id) const {
    auto l = sessionPtr->findLoop (id);
    if (! l || l->type != LoopType::Midi || l->midiTakes.empty()) return nullptr;
    const int take = std::clamp (l->rt->uiActiveTake.load(), 0, (int) l->midiTakes.size() - 1);
    return &l->midiTakes[(size_t) take];
}

void EngineController::setMidiClip (Id id, const MidiClip& clip) {
    auto l = sessionPtr->findLoop (id);
    if (! l || l->type != LoopType::Midi) return;
    int take = l->rt->uiNumTakes.load() > 0 ? l->rt->uiActiveTake.load() : 0;
    if ((int) l->midiTakes.size() <= take) l->midiTakes.resize ((size_t) take + 1);
    l->midiTakes[(size_t) take] = clip;
    publishMidiTake (*l, take);
    LoopCommand def { LoopCommandType::DefineTake }; def.take = take; def.lengthBeats = std::max (0.25, clip.lengthBeats);
    sendLoop (*l, def);
    notify (Change::Clip, id);
}

void EngineController::publishMidiTake (LoopModel& l, int take) {
    if (take < 0 || take >= kMaxTakes) return;
    if (take < (int) l.midiTakes.size())
        l.rt->midiSequences[take].publish (std::make_unique<MidiSequence> (MidiSequence::fromClip (l.midiTakes[(size_t) take])), audioEngine->retirer());
    else
        l.rt->midiSequences[take].publish (nullptr, audioEngine->retirer());
}

bool EngineController::exportTakeAudio (Id id, int take, juce::AudioBuffer<float>& out, double& beats) const {
    auto l = sessionPtr->findLoop (id);
    if (! l || l->type != LoopType::Audio || take < 0 || take >= l->rt->takeCount()) return false;
    // Reading while the audio thread may overdub: benign (worst case a partially updated sample).
    const auto& tk = l->rt->take (take);
    if (! tk.hasContent || tk.lengthFrames <= 0) return false;
    out.setSize (2, (int) tk.lengthFrames);
    for (int c = 0; c < 2; ++c) {
        float* d = out.getWritePointer (c);
        for (int64_t f = 0; f < tk.lengthFrames; ++f) d[f] = tk.sample (c, f);
    }
    beats = tk.lengthBeats;
    return true;
}

// =========================================================================================
// Automation
// =========================================================================================
void EngineController::publishLoopAutomation (LoopModel& l) {
    if (! l.rt) return;
    const double len = l.rt->uiLengthBeats.load() > 0 ? l.rt->uiLengthBeats.load() : l.automationLengthBeats;
    l.rt->automation.publish (makeSet (l.automation, len), audioEngine->retirer());
}

std::vector<AutomationLane>* EngineController::lanesOf (const AutomationOwner& o) const {
    if (o.loopId != kInvalidId) { auto l = sessionPtr->findLoop (o.loopId); return l ? &l->automation : nullptr; }
    auto t = sessionPtr->findTrack (o.trackId);
    return t ? &t->automation : nullptr;
}

void EngineController::publishLanes (const AutomationOwner& o) {
    if (o.loopId != kInvalidId) {
        auto l = sessionPtr->findLoop (o.loopId);
        if (! l) return;
        // An automation loop drawn by hand becomes playable.
        if (l->type == LoopType::Automation && l->rt->uiNumTakes.load() == 0 && ! l->automation.empty()) {
            LoopCommand def { LoopCommandType::DefineTake }; def.take = 0; def.lengthBeats = l->automationLengthBeats;
            sendLoop (*l, def);
        }
        publishLoopAutomation (*l);
        notify (Change::Automation, o.loopId);
    } else if (auto t = sessionPtr->findTrack (o.trackId)) {
        t->rt->automation.publish (makeSet (t->automation, 0), audioEngine->retirer());
        notify (Change::Automation, o.trackId);
    }
}

const std::vector<AutomationLane>* EngineController::automationLanes (const AutomationOwner& o) const { return lanesOf (o); }

int EngineController::addAutomationLane (const AutomationOwner& o, const ParamTarget& target) {
    auto* lanes = lanesOf (o);
    if (! lanes) return -1;
    for (size_t i = 0; i < lanes->size(); ++i) if ((*lanes)[i].target == target) return (int) i;
    AutomationLane lane; lane.target = target;
    lanes->push_back (lane);
    publishLanes (o);
    return (int) lanes->size() - 1;
}

void EngineController::removeAutomationLane (const AutomationOwner& o, int i) {
    auto* lanes = lanesOf (o);
    if (! lanes || i < 0 || i >= (int) lanes->size()) return;
    lanes->erase (lanes->begin() + i);
    publishLanes (o);
}

void EngineController::setAutomationLane (const AutomationOwner& o, int i, const AutomationLane& lane) {
    auto* lanes = lanesOf (o);
    if (! lanes || i < 0 || i >= (int) lanes->size()) return;
    (*lanes)[(size_t) i] = lane;
    (*lanes)[(size_t) i].sort();
    publishLanes (o);
}

double EngineController::automationLengthBeats (const AutomationOwner& o) const {
    if (o.loopId != kInvalidId) {
        auto l = sessionPtr->findLoop (o.loopId);
        if (! l) return 16;
        const double len = l->rt->uiLengthBeats.load();
        return len > 0 ? len : l->automationLengthBeats;
    }
    double end = 0;
    for (auto& c : sessionPtr->timeline) end = std::max (end, c.startBeat + c.lengthBeats);
    return std::max (end, 32.0 * sessionPtr->transport.beatsPerBar);
}

// =========================================================================================
// Bounce
// =========================================================================================
void EngineController::bounceTrack (Id trackId, std::function<void (Id)> onDone) {
    auto& s = *sessionPtr;
    auto src = s.findTrack (trackId);
    if (! src || src->bounced || src->kind == TrackKind::Master) return;
    for (auto& [loop, b] : impl->bounces) if (b.source == trackId) return;   // already bouncing

    auto dst = makeTrack (TrackKind::Audio, src->name + " (bounce)");
    dst->colour = src->colour;
    dst->inputChannel = -1;
    dst->recordSource = trackId;
    dst->outputBus = src->kind == TrackKind::Bus ? src->outputBus : src->outputBus;
    dst->sends = src->sends;
    for (size_t k = 0; k < src->sends.size(); ++k) dst->rt->sendLevel[k] = src->rt->sendLevel[k].load();
    dst->rt->monitor = MonitorMode::Off;
    auto it = std::find (s.tracks.begin(), s.tracks.end(), src);
    s.tracks.insert (it + 1, dst);

    auto loop = std::make_shared<LoopModel>();
    loop->id = s.allocateId(); loop->trackId = dst->id; loop->type = LoopType::Audio;
    loop->name = "Bounce"; loop->settings = s.transport.defaultLoopSettings;
    loop->rt = makeLoopRt (loop->id, dst->id, LoopType::Audio);
    loop->rt->setSettingsOffline (loop->settings);
    dst->loops.push_back (loop);
    publishLoopAutomation (*loop);
    rebuildGraph();

    impl->bounces[loop->id] = { trackId, dst->id, onDone };
    EngineCommand e { EngineCommandType::Bounce }; e.trackId = trackId; e.loop = loop->rt.get();
    audioEngine->send (e);
    notify (Change::Structure, dst->id);
}

void EngineController::finishBounce (Id loopId) {
    auto it = impl->bounces.find (loopId);
    if (it == impl->bounces.end()) return;
    auto b = it->second; impl->bounces.erase (it);
    auto& s = *sessionPtr;
    auto src = s.findTrack (b.source); auto dst = s.findTrack (b.target);
    if (! src || ! dst) return;
    src->bounced = true; src->bouncedInto = dst->id;
    src->rt->bounced = true;
    dst->recordSource = kInvalidId;
    if (auto l = s.findLoop (loopId)) l->settings = l->rt->settingsCopy();
    rebuildGraph();
    notify (Change::Structure, dst->id);
    if (b.done) b.done (dst->id);
}

void EngineController::restoreBouncedTrack (Id trackId) {
    auto t = sessionPtr->findTrack (trackId);
    if (! t || ! t->bounced) return;
    t->bounced = false; t->bouncedInto = kInvalidId;
    t->rt->bounced = false;
    rebuildGraph();
    notify (Change::Structure, trackId);
}

// =========================================================================================
// Transport
// =========================================================================================
void EngineController::play() { EngineCommand e { EngineCommandType::Play }; audioEngine->send (e); }
void EngineController::stop() { EngineCommand e { EngineCommandType::Stop }; audioEngine->send (e); }
void EngineController::togglePlay() { if (audioEngine->statusPlaying.load()) stop(); else play(); }

void EngineController::setTempo (double bpm) {
    bpm = juce::jlimit (20.0, 999.0, bpm);
    sessionPtr->transport.tempo = bpm;
    audioEngine->clock().setTempo (bpm);
    notify (Change::Transport);
}

void EngineController::tapTempo() {
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    auto& taps = impl->taps;
    if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2) setTempo (60.0 * (double) (taps.size() - 1) / (taps.back() - taps.front()));
}

void EngineController::setTimeSignature (int bpb, int unit) {
    sessionPtr->transport.beatsPerBar = juce::jlimit (1, 32, bpb);
    sessionPtr->transport.beatUnit = unit;
    publishTransport(); notify (Change::Transport);
}

void EngineController::setMasterBars (int bars) { sessionPtr->transport.masterBars = juce::jlimit (1, 256, bars); publishTransport(); notify (Change::Transport); }

void EngineController::setMetronome (bool on, float vol) {
    sessionPtr->transport.metronome = on;
    if (vol >= 0) sessionPtr->transport.metronomeVolume = vol;
    publishTransport(); notify (Change::Transport);
}

void EngineController::setLinkEnabled (bool on) { sessionPtr->transport.linkEnabled = on; audioEngine->clock().setEnabled (on); notify (Change::Transport); }
void EngineController::setLinkStartStopSync (bool on) { sessionPtr->transport.linkStartStopSync = on; audioEngine->clock().setStartStopSync (on); notify (Change::Transport); }

void EngineController::setTransportMode (TransportMode m) {
    if (sessionPtr->transport.mode == m) return;
    sessionPtr->transport.mode = m;
    publishTransport();
    if (m == TransportMode::Loops) locate (0);
    notify (Change::Transport);
}

void EngineController::setTimelineLoop (bool on, double a, double b) {
    auto& t = sessionPtr->transport;
    t.timelineLoop = on; t.timelineLoopStart = std::max (0.0, std::min (a, b)); t.timelineLoopEnd = std::max (a, b);
    publishTransport(); notify (Change::Transport);
}

void EngineController::locate (double beat) { EngineCommand e { EngineCommandType::Locate }; e.beat = std::max (0.0, beat); audioEngine->send (e); }
void EngineController::setDefaultLoopSettings (const LoopSettings& s) { sessionPtr->transport.defaultLoopSettings = s; notify (Change::Transport); }
void EngineController::setRecordLatencyOffset (int n) { sessionPtr->transport.recordLatencyOffset = n; audioEngine->setRecordLatencyOffset (n); notify (Change::Transport); }

TransportStatus EngineController::transportStatus() const {
    TransportStatus s;
    s.playing = audioEngine->statusPlaying.load();
    s.beat = s.playing ? audioEngine->statusBeat.load() : 0.0;
    s.tempo = s.playing ? audioEngine->statusTempo.load() : audioEngine->clock().tempo();
    const int bpb = std::max (1, sessionPtr->transport.beatsPerBar);
    s.bar = (int) std::floor (s.beat / bpb);
    s.beatInBar = s.beat - s.bar * bpb;
    s.linkEnabled = audioEngine->clock().isEnabled();
    s.linkPeers = audioEngine->clock().numPeers();
    s.cpuLoad = audioEngine->cpuLoad.load();
    s.xruns = audioEngine->xruns.load();
    s.sampleRate = audioEngine->sampleRate();
    s.blockSize = audioEngine->blockSize();
    s.latencyIn = audioEngine->inputLatency();
    s.latencyOut = audioEngine->outputLatency();
    return s;
}

MeterStatus EngineController::meter (Id trackId) const {
    MeterStatus m;
    auto t = sessionPtr->findTrack (trackId);
    if (! t) return m;
    auto& r = *t->rt;
    m.left = r.peakL.load(); m.right = r.peakR.load(); m.input = r.inputPeak.load();
    r.peakL.store (m.left * 0.8f); r.peakR.store (m.right * 0.8f); r.inputPeak.store (m.input * 0.8f);
    m.inputAudible = r.inputAudible.load();
    return m;
}

// =========================================================================================
// Timeline
// =========================================================================================
Id EngineController::addTimelineClip (Id loopId, double start, double len) {
    auto l = sessionPtr->findLoop (loopId);
    if (! l) return kInvalidId;
    TimelineClip c;
    c.id = sessionPtr->allocateId(); c.trackId = l->trackId; c.loopId = loopId; c.startBeat = std::max (0.0, start);
    const double loopLen = l->rt->uiLengthBeats.load();
    c.lengthBeats = len > 0 ? len : (loopLen > 0 ? loopLen : (double) sessionPtr->transport.masterBars * sessionPtr->transport.beatsPerBar);
    sessionPtr->timeline.push_back (c);
    rebuildGraph();
    notify (Change::Timeline, c.id);
    return c.id;
}

void EngineController::moveTimelineClip (Id id, double start, Id trackId) {
    for (auto& c : sessionPtr->timeline)
        if (c.id == id) {
            c.startBeat = std::max (0.0, start);
            if (trackId != kInvalidId) if (auto t = sessionPtr->trackOfLoop (c.loopId)) if (t->id == trackId) c.trackId = trackId;
        }
    rebuildGraph(); notify (Change::Timeline, id);
}

void EngineController::resizeTimelineClip (Id id, double len) {
    for (auto& c : sessionPtr->timeline) if (c.id == id) c.lengthBeats = std::max (0.25, len);
    rebuildGraph(); notify (Change::Timeline, id);
}

void EngineController::removeTimelineClip (Id id) {
    auto& tl = sessionPtr->timeline;
    tl.erase (std::remove_if (tl.begin(), tl.end(), [id] (const TimelineClip& c) { return c.id == id; }), tl.end());
    rebuildGraph(); notify (Change::Timeline, id);
}

void EngineController::setArrangementRecord (bool on) {
    arrangementRecord = on;
    impl->openClips.clear();
    notify (Change::Transport);
}

// Loops-mode arrangement recording: every audible span of a loop becomes a timeline clip.
void EngineController::recordArrangement() {
    if (! arrangementRecord || sessionPtr->transport.mode != TransportMode::Loops) return;
    const auto ts = transportStatus();
    if (! ts.playing) { impl->openClips.clear(); return; }
    bool changed = false;
    for (auto& t : sessionPtr->tracks)
        for (auto& l : t->loops) {
            const auto st = l->rt->uiState.load();
            const bool audible = st == LoopState::Playing || st == LoopState::Overdubbing;
            auto open = impl->openClips.find (l->id);
            if (audible && open == impl->openClips.end()) {
                const double start = std::max (0.0, std::round (ts.beat - l->rt->uiPhaseBeats.load()));
                TimelineClip c; c.id = sessionPtr->allocateId(); c.trackId = t->id; c.loopId = l->id;
                c.startBeat = start; c.lengthBeats = std::max (0.25, ts.beat - start);
                sessionPtr->timeline.push_back (c);
                impl->openClips[l->id] = c.id; changed = true;
            } else if (open != impl->openClips.end()) {
                for (auto& c : sessionPtr->timeline)
                    if (c.id == open->second) {
                        // Round the end to whole loop cycles once it stops.
                        c.lengthBeats = std::max (0.25, ts.beat - c.startBeat);
                        if (! audible) {
                            const double L = l->rt->uiLengthBeats.load();
                            if (L > 0) c.lengthBeats = std::max (L, std::round (c.lengthBeats / L) * L);
                        }
                    }
                if (! audible) impl->openClips.erase (open);
                changed = true;
            }
        }
    if (changed) notify (Change::Timeline);
}

// =========================================================================================
// Message-thread housekeeping (30 Hz)
// =========================================================================================
void EngineController::tick() {
    auto& s = *sessionPtr;
    EngineEvent e;
    std::set<Id> clipsToMerge;
    while (audioEngine->popEvent (e)) {
        switch (e.type) {
            case EngineEventType::RecordStarted:
                if (auto l = s.findLoop (e.loopId)) {
                    if (l->type == LoopType::Midi) impl->pendingMidi[e.loopId][e.take].clear();
                    notify (Change::Loop, e.loopId);
                }
                break;
            case EngineEventType::MidiRecorded:
                impl->pendingMidi[e.loopId][e.take].push_back (e.midi);
                if (auto l = s.findLoop (e.loopId))
                    if (l->rt->uiState.load() == LoopState::Overdubbing) clipsToMerge.insert (e.loopId);
                break;
            case EngineEventType::RecordFinished:
                if (auto l = s.findLoop (e.loopId)) {
                    if (l->type == LoopType::Midi) {
                        auto& ev = impl->pendingMidi[e.loopId][e.take];
                        if ((int) l->midiTakes.size() <= e.take) l->midiTakes.resize ((size_t) e.take + 1);
                        auto clip = MidiClip::fromEvents (ev, e.beat);
                        clip.stepBeats = l->midiTakes[(size_t) e.take].stepBeats;
                        l->midiTakes[(size_t) e.take] = clip;
                        ev.clear();
                        publishMidiTake (*l, e.take);
                        notify (Change::Clip, e.loopId);
                    }
                    if (l->type == LoopType::Automation) l->automationLengthBeats = e.beat;
                    publishLoopAutomation (*l);   // length is now known
                    notify (Change::Loop, e.loopId);
                }
                break;
            case EngineEventType::RecordAborted:
                impl->pendingMidi[e.loopId].erase (e.take);
                notify (Change::Loop, e.loopId);
                break;
            case EngineEventType::TakeDeleted:
                if (auto l = s.findLoop (e.loopId)) {
                    if (e.take < (int) l->midiTakes.size()) {
                        l->midiTakes.erase (l->midiTakes.begin() + e.take);
                        for (int k = e.take; k < kMaxTakes; ++k) publishMidiTake (*l, k);
                    }
                    notify (Change::Loop, e.loopId);
                    notify (Change::Clip, e.loopId);
                }
                break;
            case EngineEventType::OutOfMemory:
                DBG ("Loop " << (int) e.loopId << " ran out of recording memory");
                notify (Change::Loop, e.loopId);
                break;
            case EngineEventType::BounceFinished:
                finishBounce (e.loopId);
                break;
            case EngineEventType::TransportStarted:
            case EngineEventType::TransportStopped:
                notify (Change::Transport);
                break;
            case EngineEventType::MidiLearn: break;
        }
    }
    // Overdubbed MIDI is merged into the clip as it arrives.
    for (auto id : clipsToMerge)
        if (auto l = s.findLoop (id)) {
            const int take = l->rt->uiActiveTake.load();
            auto& ev = impl->pendingMidi[id][take];
            if (ev.empty()) continue;
            if ((int) l->midiTakes.size() <= take) { l->midiTakes.resize ((size_t) take + 1); l->midiTakes[(size_t) take].lengthBeats = l->rt->uiLengthBeats.load(); }
            l->midiTakes[(size_t) take].mergeEvents (ev);
            ev.clear();
            publishMidiTake (*l, take);
            notify (Change::Clip, id);
        }

    recordAutomation();
    recordArrangement();
    if (impl->graphDirty) rebuildGraph();
    audioEngine->collectGarbage();

    // MIDI device list (hot-plug) every ~2 s.
    if (++impl->deviceTick >= 60) {
        impl->deviceTick = 0;
        juce::StringArray ids;
        for (auto& d : juce::MidiInput::getAvailableDevices()) ids.add (d.identifier);
        if (ids != impl->midiDeviceIds) {
            impl->midiDeviceIds = ids;
            audioEngine->setMidiInputDevices (ids);
            rebuildGraph();
        }
    }
}

} // namespace el
