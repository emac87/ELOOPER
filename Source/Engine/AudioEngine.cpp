#include "AudioEngine.h"
#include <algorithm>
#include <cmath>

namespace el {

namespace {
    constexpr float kSmooth = 0.0015f;   // one-pole smoothing coefficient per sample for gains

    inline void smoothTowards (float& v, float target, int n) noexcept {
        // Exact block-wise one-pole step: v += (target - v) * (1 - (1-k)^n)
        const float a = 1.0f - std::pow (1.0f - kSmooth, (float) n);
        v += (target - v) * a;
        if (std::abs (v - target) < 1.0e-5f) v = target;
    }

    // Multiply in place with a linear gain ramp from g0 to g1.
    inline void rampGain (float* d, int n, float g0, float g1) noexcept {
        if (g0 == g1) { if (g0 != 1.0f) for (int i = 0; i < n; ++i) d[i] *= g0; return; }
        const float step = (g1 - g0) / (float) n;
        for (int i = 0; i < n; ++i) d[i] *= g0 + step * (float) i;
    }

    inline void addRamp (float* dst, const float* src, int n, float g0, float g1) noexcept {
        if (g0 == 0.0f && g1 == 0.0f) return;
        const float step = (g1 - g0) / (float) n;
        for (int i = 0; i < n; ++i) dst[i] += src[i] * (g0 + step * (float) i);
    }

    inline float peakOf (const float* d, int n) noexcept {
        float p = 0; for (int i = 0; i < n; ++i) p = std::max (p, std::abs (d[i])); return p;
    }

    inline void meterStore (std::atomic<float>& a, float v) noexcept {
        // The UI decays meters; the audio thread only raises them (max-hold since last read).
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
}

LoopRt* RenderGraph::findLoop (Id id) const noexcept {
    auto it = std::lower_bound (loopIndex.begin(), loopIndex.end(), id,
                                [] (const std::pair<Id, LoopRt*>& p, Id i) { return p.first < i; });
    return it != loopIndex.end() && it->first == id ? it->second : nullptr;
}

// -----------------------------------------------------------------------------------------
AudioEngine::AudioEngine() {
    transport = std::make_unique<TransportSettings>();
    transportPtr.store (transport.get());
    pool.reserve (64);   // ~23 s of stereo audio ready before the first recording
    loopCtx.pool = &pool;
    loopCtx.events = &events;
}

AudioEngine::~AudioEngine() {
    // The device callback must already be removed (EngineController does that first).
    graphPtr.store (nullptr);
    garbage.collect (true);
}

void AudioEngine::publishGraph (std::unique_ptr<RenderGraph> g) {
    RenderGraph* raw = g.get();
    graphPtr.store (raw, std::memory_order_release);
    if (graph) garbage.retire (std::shared_ptr<RenderGraph> (graph.release()));
    graph = std::move (g);
}

void AudioEngine::setTransportSettings (const TransportSettings& t) {
    auto fresh = std::make_unique<TransportSettings> (t);
    transportPtr.store (fresh.get(), std::memory_order_release);
    if (transport) garbage.retire (std::shared_ptr<TransportSettings> (transport.release()));
    transport = std::move (fresh);
    link.setQuantum (t.beatsPerBar);
}

void AudioEngine::setMidiInputDevices (const juce::StringArray& ids) {
    std::lock_guard<std::mutex> l (midiDeviceMutex);
    midiDevices = ids;
}

void AudioEngine::collectGarbage() {
    garbage.collect();
    // Keep enough free chunks for ~30 s of recording headroom on several loops at once.
    const double sr = currentRate.load();
    const int wanted = std::max (32, (int) std::ceil (sr * 30.0 / kChunkFrames) * 2);
    if (pool.available() < wanted) pool.reserve (wanted + 16);
}

// -----------------------------------------------------------------------------------------
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* d) {
    currentRate = d->getCurrentSampleRate();
    currentBlock = d->getCurrentBufferSizeSamples();
    latIn = d->getInputLatencyInSamples();
    latOut = d->getOutputLatencyInSamples();
    silence.setSize (2, std::max (currentBlock.load(), 4096));
    silence.clear();
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::prepareOffline (double sr, int block) {
    currentRate = sr; currentBlock = block; latIn = 0; latOut = 0;
    link.setOffline (true);
    silence.setSize (2, std::max (block, 4096));
    silence.clear();
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput* src, const juce::MidiMessage& m) {
    if (m.getRawDataSize() > 3 || m.isSysEx()) return;
    int device = 0;
    {
        std::lock_guard<std::mutex> l (midiDeviceMutex);
        if (src != nullptr) device = std::max (0, midiDevices.indexOf (src->getIdentifier()));
    }
    injectMidi (m, device);
}

void AudioEngine::injectMidi (const juce::MidiMessage& m, int deviceIndex) noexcept {
    RawMidi r {};
    const auto* d = m.getRawData();
    r.size = (uint8_t) std::min (3, m.getRawDataSize());
    for (int i = 0; i < r.size; ++i) r.data[i] = d[i];
    r.device = (int8_t) std::clamp (deviceIndex, 0, 7);
    r.time = juce::Time::getMillisecondCounterHiRes() * 0.001;
    midiIn.push (r);
}

void AudioEngine::collectMidi (int numSamples) {
    for (auto& b : liveMidi) b.clear();
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double sr = currentRate.load (std::memory_order_relaxed);
    RawMidi r;
    while (midiIn.pop (r)) {
        // Events that arrived during the last block keep their relative timing (less jitter).
        int s = numSamples - 1 - (int) ((now - r.time) * sr);
        s = std::clamp (s, 0, numSamples - 1);
        liveMidi[r.device].add (s, r.data, r.size);
    }
    // Keep each device buffer ordered by sample.
    // Insertion sort: stable, in place, no allocation (std::stable_sort may allocate).
    for (auto& b : liveMidi)
        for (int i = 1; i < b.count; ++i) {
            const TimedMidi e = b.events[i];
            int j = i - 1;
            while (j >= 0 && b.events[j].sample > e.sample) { b.events[j + 1] = b.events[j]; --j; }
            b.events[j + 1] = e;
        }
}

// -----------------------------------------------------------------------------------------
void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&) {
    const auto t0 = juce::Time::getHighResolutionTicks();
    render (in, numIn, out, numOut, numSamples);
    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const double budget = numSamples / std::max (1.0, currentRate.load (std::memory_order_relaxed));
    const double load = elapsed / budget;
    const double prev = cpuLoad.load (std::memory_order_relaxed);
    cpuLoad.store (load > prev ? load : prev * 0.95 + load * 0.05, std::memory_order_relaxed);
    if (load > 1.0) xruns.fetch_add (1, std::memory_order_relaxed);
}

void AudioEngine::render (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept {
    epoch.enter();
    for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], numSamples);
    RenderGraph* g = graphPtr.load (std::memory_order_acquire);
    const TransportSettings* ts = transportPtr.load (std::memory_order_acquire);
    if (g == nullptr || ts == nullptr || g->maxBlock <= 0) { epoch.exit(); return; }

    // Devices may deliver bigger blocks than the graph was prepared for: split.
    const float* subIn[64] {}; float* subOut[64] {};
    numIn = std::min (numIn, 64); numOut = std::min (numOut, 64);
    for (int offset = 0; offset < numSamples;) {
        const int n = std::min (g->maxBlock, numSamples - offset);
        for (int c = 0; c < numIn; ++c) subIn[c] = in[c] != nullptr ? in[c] + offset : nullptr;
        for (int c = 0; c < numOut; ++c) subOut[c] = out[c] != nullptr ? out[c] + offset : nullptr;

        collectMidi (n);
        const double sr = currentRate.load (std::memory_order_relaxed);
        BlockTime t = link.beginBlock (sampleClock, n, sr, latOut.load (std::memory_order_relaxed),
                                       ts->beatsPerBar, (double) ts->masterBars * ts->beatsPerBar);

        // Transport edges.
        if (t.playing && ! wasPlaying) {
            for (auto& [id, l] : g->loopIndex) { (void) id; l->transportStarted(); }
            EngineEvent e; e.type = EngineEventType::TransportStarted; events.push (e);
        } else if (! t.playing && wasPlaying) {
            for (auto& node : g->nodes)
                for (auto* l : node.loops) l->transportStopped (loopCtx, nullptr);
            for (auto& node : g->nodes) { node.midiGateOpen = false; }   // forces note-offs below
            EngineEvent e; e.type = EngineEventType::TransportStopped; events.push (e);
        }
        wasPlaying = t.playing;

        loopCtx.recordLatency = std::max (0, latIn.load (std::memory_order_relaxed) + latOut.load (std::memory_order_relaxed)
                                                 + latencyOffset.load (std::memory_order_relaxed));
        loopCtx.midiLatency = latOut.load (std::memory_order_relaxed);

        processCommands (t);
        updateBounces (*g, t);
        if (ts->mode == TransportMode::Timeline) applyTimeline (*g, t);
        if (ts->mode == TransportMode::Timeline && ts->timelineLoop && t.playing
            && ts->timelineLoopEnd > ts->timelineLoopStart && t.beatEnd() >= ts->timelineLoopEnd)
            link.locateFromAudio (ts->timelineLoopStart);

        g->anySolo = false;
        for (auto& node : g->nodes) { node.bus.clear (0, n); g->anySolo |= node.rt->solo.load (std::memory_order_relaxed); }
        for (auto& node : g->nodes) processNode (*g, node, t, subIn, numIn, n);

        if (g->masterIndex >= 0) {
            auto& m = g->nodes[(size_t) g->masterIndex];
            const int ch = std::max (0, m.hardwareOutput);
            for (int c = 0; c < 2; ++c)
                if (ch + c < numOut && subOut[ch + c] != nullptr)
                    juce::FloatVectorOperations::copy (subOut[ch + c], m.output.getReadPointer (c), n);
            if (ts->metronome && t.playing) renderMetronome (subOut, numOut, ch, t);
        }

        statusBeat.store (t.beatEnd(), std::memory_order_relaxed);
        statusPlaying.store (t.playing, std::memory_order_relaxed);
        statusTempo.store (t.tempo, std::memory_order_relaxed);
        sampleClock += n;
        offset += n;
    }
    epoch.exit();
}

// -----------------------------------------------------------------------------------------
TrackNode* AudioEngine::findNode (RenderGraph& g, Id id) noexcept {
    for (auto& n : g.nodes) if (n.id == id) return &n;
    return nullptr;
}

void AudioEngine::processCommands (const BlockTime& t) {
    RenderGraph* g = graphPtr.load (std::memory_order_acquire);
    EngineCommand c;
    while (commands.pop (c)) {
        switch (c.type) {
            case EngineCommandType::Loop:
                if (c.loop == nullptr) break;
                // Launching anything while stopped starts the transport; the action fires on beat 0.
                if (! t.playing && (c.loopCmd.type == LoopCommandType::Trigger || c.loopCmd.type == LoopCommandType::Record
                                    || c.loopCmd.type == LoopCommandType::Play || c.loopCmd.type == LoopCommandType::Overdub)) {
                    link.startFromAudio();
                    BlockTime zero = t; zero.beatStart = 0;
                    c.loop->command (c.loopCmd, zero, loopCtx);
                } else {
                    c.loop->command (c.loopCmd, t, loopCtx);
                }
                break;
            case EngineCommandType::Play:  link.startFromAudio(); break;
            case EngineCommandType::Stop:  link.stopFromAudio(); break;
            case EngineCommandType::Locate: link.locateFromAudio (c.beat); break;
            case EngineCommandType::Bounce: if (g != nullptr) startBounce (*g, c, t); break;
            case EngineCommandType::StopTrackLoops:
                if (g != nullptr)
                    if (auto* n = findNode (*g, c.trackId))
                        for (auto* l : n->loops) { LoopCommand s { LoopCommandType::StopNow }; l->command (s, t, loopCtx); }
                break;
            case EngineCommandType::AllLoopsStop:
                if (g != nullptr)
                    for (auto& [id, l] : g->loopIndex) { (void) id; LoopCommand s { LoopCommandType::Stop }; l->command (s, t, loopCtx); }
                break;
        }
    }
}

// ---- bounce ------------------------------------------------------------------------------
bool AudioEngine::isBounceSource (Id trackId) const noexcept {
    for (const auto& b : bounces) if (b.target != nullptr && b.capturing && b.sourceTrack == trackId) return true;
    return false;
}

void AudioEngine::startBounce (RenderGraph& g, const EngineCommand& c, const BlockTime& t) {
    TrackNode* src = findNode (g, c.trackId);
    if (src == nullptr || c.loop == nullptr) return;
    Bounce* slot = nullptr;
    for (auto& b : bounces) if (b.target == nullptr) { slot = &b; break; }
    if (slot == nullptr) return;

    // One cycle of the longest loop on the source track, aligned to that loop's phase.
    double len = 0, origin = 0;
    for (auto* l : src->loops)
        if (l->hasContent() && l->type != LoopType::Automation && l->lengthBeats() > len) { len = l->lengthBeats(); origin = l->anchor(); }
    if (len <= 0) { len = t.masterBeats; origin = 0; }
    if (! t.playing) { link.startFromAudio(); origin = 0; }
    const double now = t.playing ? t.beatStart : 0.0;
    double start = nextBoundary (now, len, origin);
    LoopSettings s = c.loop->settingsCopy();
    s.afterRecord = AfterRecord::Stop;        // we hand over manually for a seamless switch
    s.recordMode = RecordMode::Replace;
    s.cycleTakes = false;
    LoopCommand set { LoopCommandType::SetSettings }; set.settings = s;
    c.loop->command (set, t, loopCtx);
    LoopCommand rec { LoopCommandType::RecordAt }; rec.beat = start; rec.lengthBeats = len;
    BlockTime bt = t; if (! t.playing) bt.beatStart = 0;
    c.loop->command (rec, bt, loopCtx);
    *slot = { c.loop, c.trackId, c.loop->trackId, true };
}

void AudioEngine::updateBounces (RenderGraph& g, const BlockTime& t) {
    for (auto& b : bounces) {
        if (b.target == nullptr) continue;
        if (g.findLoop (b.target->id) != b.target) { b = {}; continue; }   // removed meanwhile
        const auto st = b.target->currentState();
        if (st == LoopState::Recording || b.target->uiPending.load (std::memory_order_relaxed) == PendingAction::Record) continue;
        if (st == LoopState::Stopped && b.target->hasContent()) {
            // Capture complete: the source played the same material up to this block, so the
            // target continues seamlessly with phase 0 at the moment recording ended.
            LoopCommand play { LoopCommandType::PlayAt };
            play.beat = t.beatStart - positiveMod (t.beatStart - b.target->anchor(), b.target->lengthBeats());
            // RecordAt finished with anchor = start of the capture; recompute phase from it.
            b.target->command (play, t, loopCtx);
            if (auto* src = findNode (g, b.sourceTrack)) {
                for (auto* l : src->loops) { LoopCommand s { LoopCommandType::StopNow }; l->command (s, t, loopCtx); }
                src->rt->bounced.store (true, std::memory_order_relaxed);
            }
            EngineEvent e; e.type = EngineEventType::BounceFinished; e.loopId = b.target->id; e.trackId = b.sourceTrack;
            events.push (e);
        }
        b = {};   // finished or aborted
    }
}

// ---- timeline ----------------------------------------------------------------------------
void AudioEngine::applyTimeline (RenderGraph& g, const BlockTime& t) {
    if (! t.playing) return;
    const double beat = t.beatStart;
    for (auto* loop : g.timelineLoops) {
        const TimelineEntry* active = nullptr;
        for (const auto& e : g.timeline) {
            if (e.start > beat) break;
            if (e.loop == loop && beat < e.start + e.length) active = &e;
        }
        if (active != nullptr) {
            if (! loop->isAudible() || std::abs (loop->anchor() - active->start) > 1.0e-9) {
                LoopCommand c { LoopCommandType::PlayAt }; c.beat = active->start;
                loop->command (c, t, loopCtx);
            }
        } else if (loop->isAudible()) {
            LoopCommand c { LoopCommandType::StopNow };
            loop->command (c, t, loopCtx);
        }
    }
}

// ---- automation --------------------------------------------------------------------------
static void applyTarget (RenderGraph& g, TrackNode& n, const ParamTarget& p, float v) noexcept {
    auto& rt = *n.rt;
    if (rt.isTouched (p)) return;   // the user is holding this control
    switch (p.kind) {
        case ParamKind::Volume:    rt.volume.store (v, std::memory_order_relaxed); break;
        case ParamKind::Pan:       rt.pan.store (v, std::memory_order_relaxed); break;
        case ParamKind::InputGain: rt.inputGain.store (v, std::memory_order_relaxed); break;
        case ParamKind::Mute:      rt.mute.store (v > 0.5f, std::memory_order_relaxed); break;
        case ParamKind::EqBand:    if (p.index >= 0 && p.index < kEqBands) rt.eq[p.index].store (v, std::memory_order_relaxed); break;
        case ParamKind::SendLevel:
            for (auto& s : n.sends)
                if (s.destIndex >= 0 && g.nodes[(size_t) s.destIndex].id == p.objectId && s.sendIndex < kMaxSends)
                    rt.sendLevel[s.sendIndex].store (v, std::memory_order_relaxed);
            break;
        case ParamKind::PluginParam:
        case ParamKind::PluginBypass: {
            auto apply = [&] (PluginNode& pn) {
                if (pn.slot == nullptr || pn.slot->id != p.objectId) return false;
                if (p.kind == ParamKind::PluginBypass) { pn.slot->bypassed.store (v > 0.5f, std::memory_order_relaxed); return true; }
                const auto& params = pn.proc->getParameters();
                if (p.index >= 0 && p.index < params.size()) {
                    auto* prm = params.getUnchecked (p.index);
                    if (std::abs (prm->getValue() - v) > 1.0e-5f) prm->setValue (v);
                }
                return true;
            };
            bool done = n.hasInstrument && apply (n.instrument);
            for (auto* chain : { &n.midiFx, &n.pre, &n.post })
                for (auto& pn : *chain) if (! done) done = apply (pn);
            break;
        }
        case ParamKind::LoopVolume:
            for (auto* l : n.loops) if (l->id == p.objectId) l->volume.store (pm::volumeToGain (v), std::memory_order_relaxed);
            break;
    }
}

void AudioEngine::applyAutomation (RenderGraph& g, TrackNode& n, const BlockTime& t) {
    if (! t.playing) return;
    float v = 0;
    if (const auto* set = n.rt->automation.get())
        for (const auto& lane : set->lanes)
            if (lane.enabled && lane.evaluate (t.beatStart, v)) applyTarget (g, n, lane.target, v);
    for (auto* l : n.loops) {
        if (! l->isAudible()) continue;
        const auto* set = l->automation.get();
        if (set == nullptr || set->lanes.empty()) continue;
        const double phase = l->phaseBeatsAt (t.beatStart);
        for (const auto& lane : set->lanes)
            if (lane.enabled && lane.evaluate (phase, v)) applyTarget (g, n, lane.target, v);
    }
}

// ---- plugins -----------------------------------------------------------------------------
void AudioEngine::runPlugins (std::vector<PluginNode>& chain, juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi, int n) {
    for (auto& p : chain) {
        if (p.proc == nullptr || p.slot->bypassed.load (std::memory_order_relaxed)) continue;
        const int chans = std::min (buf.getNumChannels(), std::max (p.numIn, p.numOut));
        for (int c = 2; c < chans; ++c) buf.clear (c, 0, n);   // sidechains / extra inputs silent
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), std::max (chans, 1), n);
        const juce::ScopedTryLock lock (p.proc->getCallbackLock());
        if (! lock.isLocked() || p.proc->isSuspended()) continue;
        p.proc->processBlock (view, midi);
    }
}

// ---- per track ---------------------------------------------------------------------------
void AudioEngine::processNode (RenderGraph& g, TrackNode& n, const BlockTime& t, const float* const* in, int numIn, int ns) {
    TrackRt& rt = *n.rt;
    n.output.clear (0, ns);
    if (rt.bounced.load (std::memory_order_relaxed)) {
        rt.peakL.store (0); rt.peakR.store (0);
        return;   // bounced away: silent, plugins not processed (suspended)
    }
    applyAutomation (g, n, t);

    float* inL = n.input.getWritePointer (0);
    float* inR = n.input.getWritePointer (1);
    n.input.clear (0, ns);
    n.work.clear (0, ns);

    // Monitoring decision (Auto mutes the live input once a loop holds the material).
    bool anyRecording = false, anyContent = false;
    for (auto* l : n.loops) {
        if (l->type == LoopType::Automation) continue;
        anyRecording |= l->isRecordingAny();
        anyContent |= l->hasContent();
    }
    const auto mode = rt.monitor.load (std::memory_order_relaxed);
    bool monitor = mode == MonitorMode::On || rt.armed.load (std::memory_order_relaxed)
                   || (mode == MonitorMode::Auto && (anyRecording || ! anyContent));
    if (isBounceSource (n.id)) monitor = false;          // bounce bakes the loops, not the live input
    rt.inputAudible.store (monitor, std::memory_order_relaxed);

    const float inGainTarget = pm::inputGainToGain (rt.inputGain.load (std::memory_order_relaxed));
    const float g0 = n.smoothInGain; smoothTowards (n.smoothInGain, inGainTarget, ns);

    // ---- source signal -------------------------------------------------------------------
    if (n.kind == TrackKind::Instrument) {
        // Live MIDI for this track (device + channel filter).
        loopMidiScratch.clear();
        BlockMidi& filtered = trackMidiScratch;
        filtered.clear();
        for (int d = 0; d < 8; ++d) {
            if (n.midiDeviceIndex >= 0 && d != n.midiDeviceIndex) continue;
            for (int i = 0; i < liveMidi[d].count; ++i) {
                const auto& e = liveMidi[d].events[i];
                if (n.midiChannel > 0 && (e.data[0] & 0xF0) != 0xF0 && (e.data[0] & 0x0F) != n.midiChannel - 1) continue;
                filtered.add (e.sample, e.data, e.size);
            }
        }
        // MIDI loops record the live input and play into loopMidiScratch.
        for (auto* l : n.loops) {
            if (l->type != LoopType::Midi) continue;
            LoopIo io; io.midiIn = &filtered; io.midiOut = &loopMidiScratch;
            l->process (t, io, loopCtx);
        }
        n.midi.clear();
        for (int i = 0; i < loopMidiScratch.count; ++i) {
            const auto& e = loopMidiScratch.events[i];
            n.midi.addEvent (e.data, e.size, std::clamp (e.sample, 0, ns - 1));
        }
        if (monitor) {
            for (int i = 0; i < filtered.count; ++i)
                n.midi.addEvent (filtered.events[i].data, filtered.events[i].size, filtered.events[i].sample);
        } else if (n.midiGateOpen) {
            for (int ch = 1; ch <= 16; ++ch) n.midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);   // gate closed: no hanging notes
        }
        n.midiGateOpen = monitor;

        runPlugins (n.midiFx, n.work, n.midi, ns);
        if (n.hasInstrument && n.instrument.proc != nullptr && ! n.instrument.slot->bypassed.load (std::memory_order_relaxed)) {
            n.work.clear (0, ns);
            const int chans = std::min (n.work.getNumChannels(), std::max (n.instrument.numIn, n.instrument.numOut));
            juce::AudioBuffer<float> view (n.work.getArrayOfWritePointers(), std::max (2, chans), ns);
            const juce::ScopedTryLock lock (n.instrument.proc->getCallbackLock());
            if (lock.isLocked() && ! n.instrument.proc->isSuspended()) n.instrument.proc->processBlock (view, n.midi);
            // Extra output pairs feed AuxOut tracks.
            for (int child : n.auxChildren) {
                auto& c = g.nodes[(size_t) child];
                const int ch0 = c.auxPair * 2;
                if (ch0 + 1 < chans) {
                    c.bus.addFrom (0, 0, n.work, ch0, 0, ns);
                    c.bus.addFrom (1, 0, n.work, ch0 + 1, 0, ns);
                }
            }
        } else {
            n.work.clear (0, ns);
        }
        n.input.copyFrom (0, 0, n.work, 0, 0, ns);
        n.input.copyFrom (1, 0, n.work, 1, 0, ns);
        n.work.clear (0, ns);
    } else if (n.recordSourceIndex >= 0) {
        // Resampling another track (bounce target): its final output is our input.
        const auto& src = g.nodes[(size_t) n.recordSourceIndex].output;
        n.input.copyFrom (0, 0, src, 0, 0, ns);
        n.input.copyFrom (1, 0, src, 1, 0, ns);
    } else if (n.isBusLikeOrAux()) {
        n.input.copyFrom (0, 0, n.bus, 0, 0, ns);
        n.input.copyFrom (1, 0, n.bus, 1, 0, ns);
    } else if (n.inputChannel >= 0 && n.inputChannel < numIn && in[n.inputChannel] != nullptr) {
        juce::FloatVectorOperations::copy (inL, in[n.inputChannel], ns);
        const int rc = n.stereoInput ? n.inputChannel + 1 : n.inputChannel;
        juce::FloatVectorOperations::copy (inR, rc < numIn && in[rc] != nullptr ? in[rc] : in[n.inputChannel], ns);
    }
    rampGain (inL, ns, g0, n.smoothInGain);
    rampGain (inR, ns, g0, n.smoothInGain);
    meterStore (rt.inputPeak, std::max (peakOf (inL, ns), peakOf (inR, ns)));

    // ---- audio loops ----------------------------------------------------------------------
    // Resampling targets (and buses) record internal audio: no device latency to compensate.
    LoopContext ctx = loopCtx;
    if (n.recordSourceIndex >= 0 || n.isBusLikeOrAux() || n.kind == TrackKind::Instrument) ctx.recordLatency = 0;
    for (auto* l : n.loops) {
        if (l->type == LoopType::Midi) continue;   // handled above
        LoopIo io;
        io.in[0] = inL; io.in[1] = inR;
        io.out[0] = n.work.getWritePointer (0); io.out[1] = n.work.getWritePointer (1);
        l->process (t, io, ctx);
    }

    // Live input through the monitor gate (ramped to avoid clicks).
    const float m0 = n.smoothMonitor; n.smoothMonitor = monitor ? 1.0f : 0.0f;
    if (n.recordSourceIndex < 0) {   // resample targets never pass their source through
        addRamp (n.work.getWritePointer (0), inL, ns, m0, n.smoothMonitor);
        addRamp (n.work.getWritePointer (1), inR, ns, m0, n.smoothMonitor);
    }

    // ---- inserts, EQ, mute, sends, fader --------------------------------------------------
    n.fxMidi.clear();
    runPlugins (n.pre, n.work, n.fxMidi, ns);

    if (rt.eqEnabled.load (std::memory_order_relaxed)) {
        float db[kEqBands];
        for (int b = 0; b < kEqBands; ++b) db[b] = pm::eqToDb (rt.eq[b].load (std::memory_order_relaxed));
        n.eq.setGains (db);
        n.eq.process (n.work.getWritePointer (0), n.work.getWritePointer (1), ns);
    }

    const bool soloMuted = g.anySolo && ! rt.solo.load (std::memory_order_relaxed) && ! n.isBusLikeOrAux();
    const float muteTarget = (rt.mute.load (std::memory_order_relaxed) || soloMuted) ? 0.0f : 1.0f;
    const float mu0 = n.smoothMute; smoothTowards (n.smoothMute, muteTarget, ns);
    if (muteTarget == 0.0f && n.smoothMute < 1.0e-3f) n.smoothMute = 0.0f;
    rampGain (n.work.getWritePointer (0), ns, mu0, n.smoothMute);
    rampGain (n.work.getWritePointer (1), ns, mu0, n.smoothMute);

    auto doSends = [&] (bool pre) {
        for (auto& s : n.sends) {
            if (s.preFader != pre || s.destIndex < 0 || s.sendIndex >= kMaxSends) continue;
            const float target = pm::volumeToGain (rt.sendLevel[s.sendIndex].load (std::memory_order_relaxed));
            const float s0 = n.smoothSend[s.sendIndex]; smoothTowards (n.smoothSend[s.sendIndex], target, ns);
            auto& dest = g.nodes[(size_t) s.destIndex].bus;
            addRamp (dest.getWritePointer (0), n.work.getReadPointer (0), ns, s0, n.smoothSend[s.sendIndex]);
            addRamp (dest.getWritePointer (1), n.work.getReadPointer (1), ns, s0, n.smoothSend[s.sendIndex]);
        }
    };
    doSends (true);

    const float vol = pm::volumeToGain (rt.volume.load (std::memory_order_relaxed));
    float pl, pr; pm::panGains (rt.pan.load (std::memory_order_relaxed), pl, pr);
    const float v0 = n.smoothVol, l0 = n.smoothPanL, r0 = n.smoothPanR;
    smoothTowards (n.smoothVol, vol, ns); smoothTowards (n.smoothPanL, pl, ns); smoothTowards (n.smoothPanR, pr, ns);
    rampGain (n.work.getWritePointer (0), ns, v0 * l0, n.smoothVol * n.smoothPanL);
    rampGain (n.work.getWritePointer (1), ns, v0 * r0, n.smoothVol * n.smoothPanR);

    n.fxMidi.clear();
    runPlugins (n.post, n.work, n.fxMidi, ns);
    doSends (false);

    n.output.copyFrom (0, 0, n.work, 0, 0, ns);
    n.output.copyFrom (1, 0, n.work, 1, 0, ns);
    if (n.outputIndex >= 0) {
        auto& dest = g.nodes[(size_t) n.outputIndex].bus;
        dest.addFrom (0, 0, n.output, 0, 0, ns);
        dest.addFrom (1, 0, n.output, 1, 0, ns);
    }
    meterStore (rt.peakL, peakOf (n.output.getReadPointer (0), ns));
    meterStore (rt.peakR, peakOf (n.output.getReadPointer (1), ns));
}

// ---- metronome ---------------------------------------------------------------------------
void AudioEngine::renderMetronome (float* const* out, int numOut, int ch, const BlockTime& t) {
    const TransportSettings* ts = transportPtr.load (std::memory_order_acquire);
    const float vol = ts != nullptr ? ts->metronomeVolume : 0.5f;
    const double sr = t.sampleRate;
    double nextBeat = std::ceil (t.beatStart - 1.0e-9);
    int nextSample = sampleOffsetForBeat (t, nextBeat);
    for (int i = 0; i < t.numSamples; ++i) {
        if (i == nextSample && nextSample < t.numSamples) {
            const bool downbeat = std::fmod (nextBeat, t.beatsPerBar) < 1.0e-6;
            metroFreq = downbeat ? 1600.0f : 1000.0f;
            metroRemaining = (int) (0.03 * sr);
            metroPhase = 0;
            nextBeat += 1.0;
            nextSample = sampleOffsetForBeat (t, nextBeat);
            if (nextSample <= i) nextSample = t.numSamples;
        }
        if (metroRemaining > 0) {
            const float env = (float) metroRemaining / (float) (0.03 * sr);
            const float s = (float) std::sin (metroPhase) * env * env * vol * 0.5f;
            metroPhase += 2.0 * juce::MathConstants<double>::pi * metroFreq / sr;
            --metroRemaining;
            for (int c = 0; c < 2; ++c) if (ch + c < numOut && out[ch + c] != nullptr) out[ch + c][i] += s;
        }
    }
}

} // namespace el
