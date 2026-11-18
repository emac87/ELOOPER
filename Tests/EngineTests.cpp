// Headless integration tests: drive the real EngineController + AudioEngine offline.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/AudioEngine.h"
#include "Engine/SessionSerializer.h"
#include "Engine/PluginHost.h"
#include "Synth/plugin/ChimeraProcessor.h"
#include <cstdio>
#include <vector>

using namespace el;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("  %s:%d CHECK(%s)\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct Offline {
    EngineController c { false };
    double sr = 48000; int block = 512; double phase = 0;
    float inputLevel = 0.5f;
    // Renders `seconds`; returns RMS of master left over the rendered span.
    double run (double seconds, bool feed = true) {
        const int total = (int) (seconds * sr);
        std::vector<float> in0 (block), in1 (block), o0 (block), o1 (block);
        double sum = 0; long count = 0;
        for (int done = 0; done < total; done += block) {
            for (int i = 0; i < block; ++i) { in0[i] = feed ? inputLevel * (float) std::sin (phase) : 0.0f; phase += 2 * juce::MathConstants<double>::pi * 440.0 / sr; }
            in1 = in0;
            const float* ins[2] = { in0.data(), in1.data() };
            float* outs[2] = { o0.data(), o1.data() };
            c.engine().render (ins, 2, outs, 2, block);
            c.processPendingEvents();
            for (int i = 0; i < block; ++i) { sum += o0[i] * o0[i]; ++count; }
        }
        return std::sqrt (sum / std::max (1L, count));
    }
};

static Id firstTrack (EngineController& c, TrackKind k) {
    for (auto& t : c.session().tracks) if (t->kind == k) return t->id;
    return kInvalidId;
}

static void testRecordLoopAndAutoMute() {
    std::printf ("record loop + auto-mute\n");
    Offline o;
    auto& c = o.c;
    c.setMasterBars (1);                       // 1 bar @120 = 2 s
    const Id track = firstTrack (c, TrackKind::Audio);
    LoopSettings s = c.session().transport.defaultLoopSettings; s.startQuantize = Quantize::None;
    c.setDefaultLoopSettings (s);
    const Id loop = c.addLoop (track, LoopType::Audio);
    const double live = o.run (0.5);
    CHECK (live > 0.2);                        // input is monitored before anything is recorded
    c.recordLoop (loop);
    o.run (2.2);
    auto st = c.loopStatus (loop);
    CHECK (st.state == LoopState::Playing);
    CHECK (std::abs (st.lengthBeats - 4.0) < 1e-6);
    // Input now silent: only the loop should be heard, at the same level (no doubling).
    const double looped = o.run (2.0, false);
    CHECK (looped > 0.2 && looped < 0.45);
    const double withInput = o.run (2.0, true);
    CHECK (std::abs (withInput - looped) < 0.05);   // Auto monitor muted the live input
    CHECK (! c.meter (track).inputAudible);
}

static void testBusRoutingAndCycles() {
    std::printf ("bus routing + cycle rejection\n");
    Offline o;
    auto& c = o.c;
    const Id track = firstTrack (c, TrackKind::Audio);
    const Id busA = c.addTrack (TrackKind::Bus, "A");
    const Id busB = c.addTrack (TrackKind::Bus, "B");
    CHECK (c.setTrackOutput (track, busA));
    CHECK (c.setTrackOutput (busA, busB));
    CHECK (! c.setTrackOutput (busB, busA));        // would loop B -> A -> B
    CHECK (c.addSend (busB, busA) == -1);           // send cycle rejected too
    const double viaBuses = o.run (0.5);
    CHECK (viaBuses > 0.2);
    c.setParam (busB, { ParamKind::Mute, 0, -1 }, 1.0f);
    o.run (0.1);   // mute fades out (click-free)
    const double muted = o.run (0.5);
    CHECK (muted < 0.01);
    c.setParam (busB, { ParamKind::Mute, 0, -1 }, 0.0f);
    c.setTrackOutput (track, kMasterId);
    const int send = c.addSend (track, busA);
    CHECK (send == 0);
}

static void testBounce() {
    std::printf ("bounce\n");
    Offline o;
    auto& c = o.c;
    c.setMasterBars (1);
    const Id track = firstTrack (c, TrackKind::Audio);
    LoopSettings s = c.session().transport.defaultLoopSettings; s.startQuantize = Quantize::None;
    c.setDefaultLoopSettings (s);
    const Id loop = c.addLoop (track, LoopType::Audio);
    c.setParam (track, { ParamKind::Volume, 0, -1 }, pm::gainToVolume (0.5f));   // the bounce must bake this
    c.recordLoop (loop);
    o.run (2.2);
    const double before = o.run (2.0, false);
    Id newTrack = kInvalidId;
    c.bounceTrack (track, [&] (Id t) { newTrack = t; });
    o.run (4.5, false);
    CHECK (newTrack != kInvalidId);
    auto src = c.session().findTrack (track);
    CHECK (src && src->bounced);
    auto dst = c.session().findTrack (newTrack);
    CHECK (dst && dst->loops.size() == 1);
    if (dst && ! dst->loops.empty()) CHECK (c.loopStatus (dst->loops[0]->id).state == LoopState::Playing);
    const double after = o.run (2.0, false);
    std::printf ("  rms before %.4f after %.4f\n", before, after);
    CHECK (std::abs (after - before) < 0.02);       // same level: effects/fader baked, no doubling
    c.restoreBouncedTrack (track);
    CHECK (! c.session().findTrack (track)->bounced);
}

static void testTimeline() {
    std::printf ("timeline\n");
    Offline o;
    auto& c = o.c;
    c.setMasterBars (1);
    const Id track = firstTrack (c, TrackKind::Audio);
    LoopSettings s = c.session().transport.defaultLoopSettings; s.startQuantize = Quantize::None;
    c.setDefaultLoopSettings (s);
    const Id loop = c.addLoop (track, LoopType::Audio);
    c.recordLoop (loop);
    o.run (2.2);
    c.stop(); o.run (0.1, false);
    c.setTransportMode (TransportMode::Timeline);
    c.addTimelineClip (loop, 8.0, 4.0);             // bar 3 only
    c.locate (0); c.play();
    const double first = o.run (3.95, false);     // beats 0 .. 7.9
    o.run (0.1, false);
    const double during = o.run (1.9, false);     // inside the clip (beats 8 .. 12)
    o.run (0.1, false);
    const double afterClip = o.run (2.0, false);  // after it
    std::printf ("  rms %.3f / %.3f / %.3f\n", first, during, afterClip);
    CHECK (first < 0.01);
    CHECK (during > 0.2);
    CHECK (afterClip < 0.05);
}

static void testMidiClipDefinesTake() {
    std::printf ("midi clip editing\n");
    Offline o;
    auto& c = o.c;
    const Id inst = firstTrack (c, TrackKind::Instrument);
    const Id loop = c.addLoop (inst, LoopType::Midi);
    MidiClip clip; clip.lengthBeats = 8; clip.notes.push_back ({ 0, 1, 60, 100, 1, false });
    c.setMidiClip (loop, clip);
    o.run (0.05, false);
    CHECK (c.loopStatus (loop).numTakes == 1);
    CHECK (c.midiClip (loop) != nullptr && c.midiClip (loop)->notes.size() == 1);
    c.playLoop (loop);
    o.run (0.5, false);
    CHECK (c.loopStatus (loop).state == LoopState::Playing);
}

static void testSaveLoadRoundTrip() {
    std::printf ("save / load round trip\n");
    Offline o;
    auto& c = o.c;
    c.setMasterBars (1);
    c.setTempo (100);
    const Id track = firstTrack (c, TrackKind::Audio);
    LoopSettings s = c.session().transport.defaultLoopSettings; s.startQuantize = Quantize::None; s.recordMode = RecordMode::TakeFolder;
    c.setDefaultLoopSettings (s);
    const Id loop = c.addLoop (track, LoopType::Audio);
    c.setLoopColour (loop, juce::Colour (0xff123456));
    c.recordLoop (loop);
    o.run (2.6);
    const Id bus = c.addTrack (TrackKind::Bus, "Verb");
    c.addSend (track, bus, true);
    c.setParam (track, { ParamKind::Pan, 0, -1 }, 0.25f);
    c.setParam (track, { ParamKind::EqBand, 0, 3 }, 0.8f);
    const Id inst = firstTrack (c, TrackKind::Instrument);
    const Id mloop = c.addLoop (inst, LoopType::Midi);
    MidiClip clip; clip.lengthBeats = 8; clip.notes.push_back ({ 1, 0.5, 64, 90, 1, false });
    clip.notes[0].ratchet = 3;
    clip.stepLanes.push_back ({ 64, "Hat A" });
    clip.stepLanes.push_back ({ 36, "" });
    clip.stepMapName = "Tekno (18 voices)";
    c.setMidiClip (mloop, clip);
    AutomationOwner owner { track, loop };
    const int lane = c.addAutomationLane (owner, { ParamKind::Volume, 0, -1 });
    AutomationLane l = (*c.automationLanes (owner))[(size_t) lane]; l.setPoint (0, 0.2f); l.setPoint (2, 0.9f);
    c.setAutomationLane (owner, lane, l);
    c.addTimelineClip (loop, 4, 8);
    o.run (0.1);
    juce::AudioBuffer<float> before; double beats = 0;
    CHECK (c.exportTakeAudio (loop, 0, before, beats));

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("elooper-test.elooper");
    dir.deleteRecursively();
    auto r = SessionSerializer::save (c, dir);
    CHECK (r.wasOk());
    if (! r.wasOk()) std::printf ("  save error: %s\n", r.getErrorMessage().toRawUTF8());

    Offline o2;
    bool done = false; juce::Result lr = juce::Result::fail ("not called");
    SessionSerializer::load (o2.c, dir, [&] (juce::Result res) { done = true; lr = res; });
    for (int i = 0; i < 50 && ! done; ++i) o2.run (0.02, false);
    CHECK (done && lr.wasOk());
    auto& c2 = o2.c;
    auto t2 = c2.session().findTrack (track);
    CHECK (t2 != nullptr);
    if (t2) {
        CHECK (std::abs (t2->rt->pan.load() - 0.25f) < 1e-4f);
        // The old track EQ moves into an ELooper EQ plug-in at the end of Pre FX (0.8 normalised = +7.2 dB).
        CHECK (std::abs (t2->rt->eq[3].load() - 0.5f) < 1e-4f);
        CHECK (t2->preFx.size() == 1 && t2->preFx[0]->instance != nullptr);
        if (t2->preFx.size() == 1 && t2->preFx[0]->instance != nullptr)
            CHECK (std::abs (t2->preFx[0]->instance->getParameters()[3]->getValue() - 0.8f) < 1e-3f);
        CHECK (t2->sends.size() == 1 && t2->sends[0].busId == bus && t2->sends[0].preFader);
    }
    auto l2 = c2.session().findLoop (loop);
    CHECK (l2 && l2->colour && *l2->colour == juce::Colour (0xff123456));
    CHECK (l2 && l2->settings.recordMode == RecordMode::TakeFolder);
    CHECK (l2 && l2->automation.size() == 1 && l2->automation[0].points.size() == 2);
    juce::AudioBuffer<float> after; double beats2 = 0;
    CHECK (c2.exportTakeAudio (loop, 0, after, beats2));
    CHECK (after.getNumSamples() == before.getNumSamples() && std::abs (beats2 - beats) < 1e-9);
    if (after.getNumSamples() == before.getNumSamples() && after.getNumSamples() > 1000)
        CHECK (std::abs (after.getSample (0, 1000) - before.getSample (0, 1000)) < 1e-6f);
    CHECK (c2.midiClip (mloop) && c2.midiClip (mloop)->notes.size() == 1 && c2.midiClip (mloop)->notes[0].pitch == 64);
    if (auto* mc = c2.midiClip (mloop)) {
        CHECK (mc->notes.size() == 1 && mc->notes[0].ratchet == 3);
        CHECK (mc->stepLanes.size() == 2 && mc->stepLanes[0].pitch == 64 && mc->stepLanes[0].name == "Hat A");
        CHECK (mc->stepLanes.size() == 2 && mc->stepLanes[1].pitch == 36 && mc->stepLanes[1].name.empty());
        CHECK (mc->stepMapName == "Tekno (18 voices)");
    }
    CHECK (c2.session().timeline.size() == 1);
    CHECK (std::abs (c2.session().transport.tempo - 100.0) < 1e-6);
    CHECK (c2.loopStatus (loop).numTakes == 1 && c2.loopStatus (loop).state == LoopState::Stopped);
    dir.deleteRecursively();
}


// Multi-out: outputs + a sum folder that the instrument and every output feed; idempotent;
// removing the instrument removes its outputs and the folder; folder survives save/load.
static void testMultiOutSumFolder() {
    Offline o;
    auto& c = o.c;
    const Id inst = firstTrack (c, TrackKind::Instrument);
    auto t = c.session().findTrack (inst);
    CHECK (t != nullptr);
    if (t == nullptr) return;
    auto slot = std::make_shared<PluginSlot>();
    slot->id = c.session().allocateId();
    slot->description.name = "FakeKit"; slot->description.isInstrument = true;
    slot->numOutputChannels = 8;   // 4 stereo pairs, no real instance (headless)
    t->instrument = slot;
    c.createMultiOutTracks (inst);
    int outs = 0; Id folder = kInvalidId;
    for (auto& x : c.session().tracks) {
        if (x->auxParent == inst) ++outs;
        if (x->kind == TrackKind::Bus && x->folderOf == inst) folder = x->id;
    }
    CHECK (outs == 3);
    CHECK (folder != kInvalidId);
    CHECK (c.folderBusOf (inst) != nullptr);
    CHECK (t->outputBus == folder);
    for (auto& x : c.session().tracks) if (x->auxParent == inst) CHECK (x->outputBus == folder);
    if (auto f = c.session().findTrack (folder)) CHECK (f->outputBus == kMasterId);
    o.run (0.1);   // graph with the folder renders without trouble

    const size_t before = c.session().tracks.size();
    c.createMultiOutTracks (inst);   // repeat: nothing new
    CHECK (c.session().tracks.size() == before);

    // save / load keeps the folder link
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("elooper_folder_test.elooper");
    dir.deleteRecursively();
    CHECK (SessionSerializer::save (c, dir).wasOk());
    {
        Offline o2;
        bool done = false;
        SessionSerializer::load (o2.c, dir, [&] (juce::Result) { done = true; });
        for (int i = 0; i < 20 && ! done; ++i) o2.run (0.02, false);
        bool found = false;
        for (auto& x : o2.c.session().tracks) if (x->kind == TrackKind::Bus && x->folderOf == inst) found = true;
        CHECK (found);   // (the fake plug-in never finishes loading headless, so `done` may stay false)
    }
    dir.deleteRecursively();
    c.removeTrack (inst);
    for (auto& x : c.session().tracks) { CHECK (x->auxParent != inst); CHECK (x->folderOf != inst); }
    o.run (0.05);
}

// Scenes: scene N plays the N-th loop of every track and stops the others; automation loops
// are not counted; empty slots stay empty.
static void testScenes() {
    std::printf ("scenes\n");
    Offline o;
    auto& c = o.c;
    const Id inst = firstTrack (c, TrackKind::Instrument);
    const Id auto0 = c.addLoop (inst, LoopType::Automation);   // hidden: not a scene slot
    const Id a = c.addLoop (inst, LoopType::Midi);
    const Id b = c.addLoop (inst, LoopType::Midi);
    const Id inst2 = c.addTrack (TrackKind::Instrument);
    const Id a2 = c.addLoop (inst2, LoopType::Midi);
    const Id empty2 = c.addLoop (inst2, LoopType::Midi);        // scene 2 slot left empty on track 2
    MidiClip clip; clip.lengthBeats = 4; clip.notes.push_back ({ 0, 1, 60, 100, 1, false });
    c.setMidiClip (a, clip); c.setMidiClip (b, clip); c.setMidiClip (a2, clip);
    o.run (0.05, false);
    CHECK (c.numScenes() == 2);
    (void) auto0;

    c.launchScene (0);
    o.run (2.5, false);   // past the next bar
    CHECK (c.loopStatus (a).state == LoopState::Playing);
    CHECK (c.loopStatus (a2).state == LoopState::Playing);
    CHECK (c.loopStatus (b).state == LoopState::Stopped);

    c.launchScene (1);
    o.run (2.5, false);
    CHECK (c.loopStatus (a).state == LoopState::Stopped);
    CHECK (c.loopStatus (b).state == LoopState::Playing);
    CHECK (c.loopStatus (a2).state == LoopState::Stopped);
    CHECK (c.loopStatus (empty2).state == LoopState::Empty);   // never starts recording
}

// Automation "Write": with the loop armed for writing, moving a control while the loop just
// plays writes a lane into its automation layer; with it off, nothing is written.
static void testAutomationWrite() {
    std::printf ("automation write\n");
    Offline o;
    auto& c = o.c;
    const Id inst = firstTrack (c, TrackKind::Instrument);
    const Id loop = c.addLoop (inst, LoopType::Midi);
    MidiClip clip; clip.lengthBeats = 4; clip.notes.push_back ({ 0, 1, 60, 100, 1, false });
    c.setMidiClip (loop, clip);
    o.run (0.05, false);
    c.playLoop (loop);
    o.run (2.5, false);
    CHECK (c.loopStatus (loop).state == LoopState::Playing);
    const ParamTarget vol { ParamKind::Volume, kInvalidId, -1 };
    AutomationOwner owner; owner.trackId = inst; owner.loopId = loop;

    c.beginParamGesture (inst, vol);   // not armed: nothing written
    for (int i = 0; i < 10; ++i) { c.setParam (inst, vol, 0.3f + 0.02f * (float) i); o.run (0.05, false); }
    c.endParamGesture (inst, vol);
    const auto* lanes = c.automationLanes (owner);
    CHECK (lanes == nullptr || lanes->empty());

    c.setAutomationWriteLoop (loop);
    c.beginParamGesture (inst, vol);
    for (int i = 0; i < 10; ++i) { c.setParam (inst, vol, 0.3f + 0.02f * (float) i); o.run (0.05, false); }
    c.endParamGesture (inst, vol);
    lanes = c.automationLanes (owner);
    CHECK (lanes != nullptr && lanes->size() == 1 && ! (*lanes)[0].points.empty());
    c.setAutomationWriteLoop (kInvalidId);
}

// Chimera as a hosted instrument: plays MIDI, every factory preset makes sound and stays sane,
// state round-trips through get/setStateInformation, multi-out buses can be enabled.
static void testChimeraProcessor() {
    std::printf ("chimera processor\n");
    using namespace el::chimera;
    auto proc = std::make_unique<ChimeraProcessor>();
    CHECK (PluginHost::isLegacyBuiltIn (proc->getPluginDescription()));   // old saved projects map to the AU
    CHECK (PluginHost::enableMultiOut (*proc) == 8);
    proc->prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buf (8, 256);
    auto renderRms = [&] (bool noteOn, int blocks) {
        double sum = 0; long n = 0;
        for (int b = 0; b < blocks; ++b) {
            juce::MidiBuffer midi;
            if (noteOn && b == 0) { midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90), 0); }
            buf.clear();
            proc->processBlock (buf, midi);
            for (int i = 0; i < 256; ++i) { const double v = buf.getSample (0, i); sum += v * v; ++n; }
        }
        return std::sqrt (sum / std::max (1L, n));
    };
    for (int i = 0; i < ChimeraProcessor::numFactoryPresets(); ++i) {
        proc->loadFactoryPreset (i);
        renderRms (false, 2);   // let the patch arrive
        const double rms = renderRms (true, 60);
        const bool isSampler = proc->patch().layers[0].engine == Engine::Sampler;   // no sample loaded: silent
        if (! isSampler && ! (rms > 0.001)) std::printf ("    preset %s rms %f\n", ChimeraProcessor::factoryPresetName (i).toRawUTF8(), rms);
        CHECK (isSampler || rms > 0.001);
        CHECK (rms < 2.0 && std::isfinite (rms));
        juce::MidiBuffer off; off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        buf.clear(); proc->processBlock (buf, off);
        renderRms (false, 400);
    }
    // state round trip
    proc->loadFactoryPreset (3);
    proc->editPatch ([] (Patch& p) { p.layers[1].engine = Engine::Wavetable; p.layers[1].wtTable = 4; p.layers[1].mods[2] = { ModSrc::Lfo2, ModDst::Pan, -0.5f }; p.reverb.size = 0.33f; });
    proc->macroParam (2)->setValueNotifyingHost (0.4f);
    juce::MemoryBlock state;
    proc->getStateInformation (state);
    auto other = std::make_unique<ChimeraProcessor>();
    other->setStateInformation (state.getData(), (int) state.getSize());
    const auto& a = proc->patch(); const auto& b = other->patch();
    CHECK (a.name == b.name);
    CHECK (b.layers[1].engine == Engine::Wavetable && b.layers[1].wtTable == 4);
    CHECK (b.layers[1].mods[2].src == ModSrc::Lfo2 && b.layers[1].mods[2].dst == ModDst::Pan && std::abs (b.layers[1].mods[2].amount + 0.5f) < 1e-4f);
    CHECK (std::abs (b.reverb.size - 0.33f) < 1e-4f);
    CHECK (std::abs (a.layers[0].cutoff - b.layers[0].cutoff) < 0.5f);
    CHECK (std::abs (other->macroParam (2)->get() - 0.4f) < 1e-3f);
    CHECK (std::memcmp (dx7ToVced (a.layers[0].fm).data(), dx7ToVced (b.layers[0].fm).data(), 155) == 0);
}

// ---- MIDI tracks, input focus, moving / copying loops ---------------------------------------------
// A test "instrument": outputs a steady 0.5 on both channels while any note is held.
struct NoteMeter : public juce::AudioPluginInstance {
    NoteMeter() : juce::AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}
    int held = 0;
    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = "NoteMeter"; d.isInstrument = true; d.pluginFormatName = "Test"; }
    const juce::String getName() const override { return "NoteMeter"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override {
        for (const auto meta : m) {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn()) ++held;
            else if (msg.isNoteOff()) held = std::max (0, held - 1);
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) held = 0;
        }
        b.clear();
        if (held > 0) for (int ch = 0; ch < b.getNumChannels(); ++ch) juce::FloatVectorOperations::fill (b.getWritePointer (ch), 0.5f, b.getNumSamples());
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

// Puts a NoteMeter straight into a track's instrument slot (tests only; the app goes through insertPlugin).
static void giveNoteMeter (EngineController& c, Id track) {
    auto t = c.session().findTrack (track);
    auto slot = std::make_shared<PluginSlot>();
    slot->id = c.session().allocateId();
    slot->instance = std::make_unique<NoteMeter>();
    slot->description.name = "NoteMeter"; slot->description.isInstrument = true;
    slot->instance->setRateAndBufferSizeDetails (48000.0, 512);
    slot->instance->prepareToPlay (48000.0, 512);
    t->instrument = slot;
    c.setTrackMidiInput (track, {}, 0);   // rebuilds the graph
}

// Meters hold their peak until read (and decay per read): drain, render, then read.
static void drainMeters (EngineController& c) { for (auto& t : c.session().tracks) for (int i = 0; i < 40; ++i) c.meter (t->id); }
static float trackLevel (Offline& o, Id track, double seconds = 0.1) {
    o.run (0.05, false);
    drainMeters (o.c);
    o.run (seconds, false);
    return o.c.meter (track).left;
}

static void testMidiTrackRouting() {
    std::printf ("midi track -> instrument\n");
    Offline o;
    auto& c = o.c;
    const Id inst = firstTrack (c, TrackKind::Instrument);
    giveNoteMeter (c, inst);
    const Id midi = c.addTrack (TrackKind::Midi);
    auto mt = c.session().findTrack (midi);
    CHECK (mt != nullptr && mt->kind == TrackKind::Midi);
    CHECK (mt != nullptr && mt->playsMidiOn (inst));   // a new MIDI track plays the first instrument
    CHECK (! c.setMidiTarget (midi, firstTrack (c, TrackKind::Audio)));   // audio tracks can't take MIDI
    CHECK (! c.setMidiTarget (inst, midi));                                  // only MIDI tracks have a MIDI To
    // A MIDI loop on the MIDI track sounds on the instrument.
    const Id loop = c.addLoop (midi, LoopType::Midi);
    MidiClip clip; clip.lengthBeats = 4; clip.notes.push_back ({ 0, 4, 36, 100, 1, false });
    c.setMidiClip (loop, clip);
    c.playLoop (loop);
    CHECK (trackLevel (o, inst, 0.6) > 0.3f);
    CHECK (c.meter (midi).left == 0.0f);                // the MIDI track itself makes no sound
    // Unrouted: silence (and no hanging note on the old target).
    c.setMidiTarget (midi, kInvalidId);
    o.run (0.2, false);
    CHECK (trackLevel (o, inst, 0.3) < 0.01f);
    // Live input on the MIDI track reaches the instrument too.
    c.stopAllLoops(); c.stop();
    c.setMidiTarget (midi, inst);
    c.setSelectedTrack (midi);
    c.setMonitorMode (midi, MonitorMode::On);   // Auto would mute live input: the track already holds a loop
    o.run (0.1, false);
    c.engine().injectMidi (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100));
    CHECK (trackLevel (o, inst) > 0.3f);
    c.engine().injectMidi (juce::MidiMessage::noteOff (1, 40));
    CHECK (trackLevel (o, inst, 0.3) < 0.01f);
    // A MIDI -> MIDI chain can't loop back on itself.
    const Id midi2 = c.addTrack (TrackKind::Midi);
    CHECK (c.setMidiTarget (midi2, midi));
    CHECK (! c.setMidiTarget (midi, midi2));
    // Several outputs: a second instrument plays the same MIDI, and each can be switched off.
    const Id inst2 = c.addTrack (TrackKind::Instrument);
    giveNoteMeter (c, inst2);
    CHECK (c.setMidiTargetOn (midi, inst2, true));
    CHECK (c.session().findTrack (midi)->midiTargets.size() == 2);
    c.engine().injectMidi (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100));
    CHECK (trackLevel (o, inst) > 0.3f);
    CHECK (trackLevel (o, inst2) > 0.3f);
    c.engine().injectMidi (juce::MidiMessage::noteOff (1, 40));
    o.run (0.3, false);
    CHECK (c.setMidiTargetOn (midi, inst, false));
    c.engine().injectMidi (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100));
    CHECK (trackLevel (o, inst2) > 0.3f);
    CHECK (trackLevel (o, inst, 0.3) < 0.01f);
    c.engine().injectMidi (juce::MidiMessage::noteOff (1, 40));
    o.run (0.3, false);
    CHECK (! c.setMidiTargetOn (midi, midi2, true));   // midi2 already plays on midi: would loop
    CHECK (c.setMidiTargetOn (midi, inst, true));
    c.removeTrack (inst2);
    CHECK (c.session().findTrack (midi)->midiTargets.size() == 1);
    // Removing the target clears MIDI To.
    c.removeTrack (inst);
    CHECK (c.session().findTrack (midi)->midiTargets.empty());
}

static void testMidiFollowsSelection() {
    std::printf ("live midi follows the selected track\n");
    Offline o;
    auto& c = o.c;
    const Id a = firstTrack (c, TrackKind::Instrument);
    const Id b = c.addTrack (TrackKind::Instrument);
    giveNoteMeter (c, a); giveNoteMeter (c, b);
    c.setMonitorMode (a, MonitorMode::On); c.setMonitorMode (b, MonitorMode::On);
    auto play = [&] { drainMeters (c); c.engine().injectMidi (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)); o.run (0.1, false); };
    auto release = [&] { c.engine().injectMidi (juce::MidiMessage::allNotesOff (1)); o.run (0.3, false); drainMeters (c); };
    // Nothing selected yet: every all-devices track listens (as before).
    play();
    CHECK (c.meter (a).left > 0.3f && c.meter (b).left > 0.3f);
    release();
    c.setSelectedTrack (b);
    CHECK (c.midiFocusTrack() == b);
    play();
    CHECK (c.meter (a).left < 0.01f && c.meter (b).left > 0.3f);
    release();
    c.setSelectedTrack (firstTrack (c, TrackKind::Audio));   // an audio track keeps the last MIDI focus
    CHECK (c.midiFocusTrack() == b);
    // A track set to one particular device keeps listening to it whatever is selected.
    c.setTrackMidiInput (a, "some-device", 0);
    CHECK (c.session().findTrack (a)->midiInputDevice == "some-device");
    // Follow off: every all-devices track listens again.
    c.setTrackMidiInput (a, {}, 0);
    c.setMidiFollowsSelection (false);
    play();
    CHECK (c.meter (a).left > 0.3f && c.meter (b).left > 0.3f);
    release();
    // Removing the focus track: everyone listens again.
    c.setMidiFollowsSelection (true);
    c.setSelectedTrack (a);
    c.removeTrack (a);
    CHECK (c.midiFocusTrack() == kInvalidId);
    play();
    CHECK (c.meter (b).left > 0.3f);
}

static void testMoveAndCopyLoops() {
    std::printf ("move / copy loops between tracks\n");
    Offline o;
    auto& c = o.c;
    c.setMasterBars (1);
    LoopSettings s = c.session().transport.defaultLoopSettings; s.startQuantize = Quantize::None;
    c.setDefaultLoopSettings (s);
    const Id audio = firstTrack (c, TrackKind::Audio);
    const Id inst = firstTrack (c, TrackKind::Instrument);
    const Id inst2 = c.addTrack (TrackKind::Instrument);
    const Id midi = c.addTrack (TrackKind::Midi);
    const Id audio2 = c.addTrack (TrackKind::Audio);
    // MIDI loops
    const Id m1 = c.addLoop (inst, LoopType::Midi);
    const Id m2 = c.addLoop (inst, LoopType::Midi);
    MidiClip clip; clip.lengthBeats = 8; clip.notes.push_back ({ 0, 1, 60, 100, 1, false });
    c.setMidiClip (m1, clip);
    c.addTimelineClip (m1, 0, 8);
    CHECK (! c.moveLoop (m1, audio));                  // MIDI can't go on an audio track
    CHECK (c.moveLoop (m1, midi));
    CHECK (c.session().trackOfLoop (m1)->id == midi);
    CHECK (c.session().findLoop (m1)->trackId == midi);
    CHECK (c.session().timeline.size() == 1 && c.session().timeline[0].trackId == midi);   // its timeline clip follows
    CHECK (c.midiClip (m1) && c.midiClip (m1)->notes.size() == 1);
    c.playLoop (m1);
    o.run (0.3, false);
    CHECK (c.loopStatus (m1).state == LoopState::Playing);   // keeps playing on its new track
    const Id copy = c.copyLoop (m1, inst2, 0);
    CHECK (copy != kInvalidId && c.session().trackOfLoop (copy)->id == inst2);
    CHECK (c.midiClip (copy) && c.midiClip (copy)->notes.size() == 1 && c.midiClip (copy)->notes[0].pitch == 60);
    CHECK (c.loopStatus (copy).numTakes == 1 && c.loopStatus (copy).state == LoopState::Stopped);
    CHECK (c.midiClip (m1) && c.midiClip (m1)->notes.size() == 1);   // the original is untouched
    // Reorder within a track: [m2, m3] -> move m2 below m3.
    const Id m3 = c.addLoop (inst, LoopType::Midi);
    CHECK (c.moveLoop (m2, inst, 2));
    auto it = c.session().findTrack (inst);
    CHECK (it->loops.size() == 2 && it->loops[0]->id == m3 && it->loops[1]->id == m2);
    CHECK (! c.moveLoop (m2, inst, 2));   // already there
    // Audio loops: record one, copy it to another audio track, move it.
    const Id a1 = c.addLoop (audio, LoopType::Audio);
    c.recordLoop (a1);
    o.run (2.4);
    CHECK (c.loopStatus (a1).numTakes == 1);
    CHECK (c.copyLoop (a1, inst) == kInvalidId);      // audio can't go on an instrument track
    const Id a2 = c.copyLoop (a1, audio2);
    CHECK (a2 != kInvalidId);
    juce::AudioBuffer<float> x, y; double bx = 0, by = 0;
    CHECK (c.exportTakeAudio (a1, 0, x, bx) && c.exportTakeAudio (a2, 0, y, by));
    CHECK (x.getNumSamples() == y.getNumSamples() && std::abs (bx - by) < 1e-9);
    if (x.getNumSamples() == y.getNumSamples() && x.getNumSamples() > 2000)
        CHECK (std::abs (x.getSample (0, 2000) - y.getSample (0, 2000)) < 1e-6f);
    c.playLoop (a2);
    o.run (0.5, false);
    CHECK (c.loopStatus (a2).state == LoopState::Playing);
    CHECK (c.moveLoop (a1, audio2, 0));
    CHECK (c.session().findTrack (audio)->loops.empty() && c.session().findTrack (audio2)->loops.size() == 2);
}

static void testMidiTrackSaveLoad() {
    std::printf ("midi track save / load\n");
    Offline o;
    auto& c = o.c;
    const Id inst = firstTrack (c, TrackKind::Instrument);
    const Id midi = c.addTrack (TrackKind::Midi, "Drums in");
    CHECK (c.session().findTrack (midi)->playsMidiOn (inst));
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("elooper-midi-test.elooper");
    dir.deleteRecursively();
    CHECK (SessionSerializer::save (c, dir).wasOk());
    Offline o2;
    bool done = false;
    SessionSerializer::load (o2.c, dir, [&] (juce::Result) { done = true; });
    for (int i = 0; i < 50 && ! done; ++i) o2.run (0.02, false);
    auto t = o2.c.session().findTrack (midi);
    CHECK (t != nullptr && t->kind == TrackKind::Midi && t->playsMidiOn (inst) && t->name == "Drums in");
    dir.deleteRecursively();
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    testRecordLoopAndAutoMute();
    testBusRoutingAndCycles();
    testBounce();
    testTimeline();
    testMidiClipDefinesTake();
    testSaveLoadRoundTrip();
    testMultiOutSumFolder();
    testScenes();
    testAutomationWrite();
    testChimeraProcessor();
    testMidiTrackRouting();
    testMidiFollowsSelection();
    testMoveAndCopyLoops();
    testMidiTrackSaveLoad();
    std::printf (failures == 0 ? "engine tests passed\n" : "%d engine check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
