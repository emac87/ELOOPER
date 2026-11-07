// Headless integration tests: drive the real EngineController + AudioEngine offline.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/AudioEngine.h"
#include "Engine/SessionSerializer.h"
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
        CHECK (std::abs (t2->rt->eq[3].load() - 0.8f) < 1e-4f);
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

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    testRecordLoopAndAutoMute();
    testBusRoutingAndCycles();
    testBounce();
    testTimeline();
    testMidiClipDefinesTake();
    testSaveLoadRoundTrip();
    std::printf (failures == 0 ? "engine tests passed\n" : "%d engine check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
