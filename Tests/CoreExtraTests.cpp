// Additional core unit tests: automation, MIDI clip/sequence, routing graph, graphic EQ,
// time math, parameter mapping, and the lock-free primitives. Pure C++17, no JUCE.
#include "TestFramework.h"
#include "Core/Automation.h"
#include "Core/GraphicEq.h"
#include "Core/MidiClip.h"
#include "Core/ParamMapping.h"
#include "Core/Routing.h"
#include "Core/RtUtil.h"
#include "Core/TimeMath.h"
#include <cmath>
#include <cstdint>
#include <vector>

using namespace el;

namespace {

MidiEvent mkEv (double beat, uint8_t status, uint8_t d1, uint8_t d2) {
    MidiEvent e;
    e.beat = beat;
    e.data[0] = status; e.data[1] = d1; e.data[2] = d2;
    return e;
}

MidiNote mkNote (double start, double length, uint8_t pitch, bool muted = false) {
    MidiNote n;
    n.start = start; n.length = length; n.pitch = pitch; n.muted = muted;
    return n;
}

size_t indexOf (const std::vector<Id>& v, Id id) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == id) return i;
    return v.size();
}

void makeSine (std::vector<float>& x, double freq, double sr, float amp) {
    const double pi = 3.14159265358979323846;
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = amp * (float) std::sin (2.0 * pi * freq * (double) i / sr);
}

double rms (const std::vector<float>& x, size_t from, size_t to) {
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += (double) x[i] * (double) x[i];
    return std::sqrt (s / (double) (to - from));
}

} // namespace

// ---------------------------------------------------------------------------------------
// AutomationLane
// ---------------------------------------------------------------------------------------

EL_TEST (automation_empty_lane_and_held_values) {
    AutomationLane lane;
    float out = -1.0f;
    EL_CHECK (! lane.evaluate (0.0, out));
    lane.setPoint (2.0, 0.25f);
    lane.setPoint (6.0, 0.75f);
    EL_CHECK (lane.evaluate (-1.0, out)); EL_CHECK_NEAR (out, 0.25, 1e-6);   // before first: held
    EL_CHECK (lane.evaluate (1.0, out));  EL_CHECK_NEAR (out, 0.25, 1e-6);
    EL_CHECK (lane.evaluate (10.0, out)); EL_CHECK_NEAR (out, 0.75, 1e-6);   // after last: held
}

EL_TEST (automation_linear_interpolation) {
    AutomationLane lane;
    lane.setPoint (0.0, 0.0f);
    lane.setPoint (4.0, 1.0f);
    float out = -1.0f;
    EL_CHECK (lane.evaluate (1.0, out)); EL_CHECK_NEAR (out, 0.25, 1e-6);
    EL_CHECK (lane.evaluate (2.0, out)); EL_CHECK_NEAR (out, 0.5, 1e-6);    // midpoint
    EL_CHECK (lane.evaluate (3.0, out)); EL_CHECK_NEAR (out, 0.75, 1e-6);
}

EL_TEST (automation_curve_segment_bends_from_start_point) {
    AutomationLane lane;
    lane.setPoint (0.0, 0.0f);
    lane.setPoint (4.0, 1.0f);
    float out = 0.0f;
    lane.points[0].curve = 0.8f;   // curve belongs to the segment starting at point 0
    EL_CHECK (lane.evaluate (2.0, out));
    EL_CHECK_NEAR (out, applyCurve (0.5f, 0.8f), 1e-6);
    EL_CHECK (out > 0.5f);         // positive curve bows upward (fast start)
    lane.points[0].curve = -0.8f;
    EL_CHECK (lane.evaluate (2.0, out));
    EL_CHECK (out < 0.5f);         // negative curve bows downward
}

EL_TEST (automation_setpoint_keeps_sorted_and_replaces_same_beat) {
    AutomationLane lane;
    lane.setPoint (3.0, 0.3f);
    lane.setPoint (1.0, 0.1f);
    lane.setPoint (2.0, 0.2f);
    EL_CHECK (lane.points.size() == 3);
    EL_CHECK_NEAR (lane.points[0].beat, 1.0, 1e-12);
    EL_CHECK_NEAR (lane.points[1].beat, 2.0, 1e-12);
    EL_CHECK_NEAR (lane.points[2].beat, 3.0, 1e-12);
    lane.setPoint (2.0, 0.9f);                       // same beat: replaced, not inserted
    EL_CHECK (lane.points.size() == 3);
    EL_CHECK_NEAR (lane.points[1].value, 0.9, 1e-6);
    lane.setPoint (2.0 + 5.0e-5, 0.4f);              // within the default merge window
    EL_CHECK (lane.points.size() == 3);
    EL_CHECK_NEAR (lane.points[1].value, 0.4, 1e-6);
    lane.setPoint (5.0, 1.7f);                       // values clamp to 0..1
    EL_CHECK (lane.points.size() == 4);
    EL_CHECK_NEAR (lane.points[3].value, 1.0, 1e-6);
}

EL_TEST (automation_erase_half_open_range) {
    AutomationLane lane;
    for (int b = 0; b <= 4; ++b) lane.setPoint ((double) b, (float) b / 4.0f);
    lane.erase (1.0, 3.0);                           // [from, to): 1 and 2 go, 3 stays
    EL_CHECK (lane.points.size() == 3);
    EL_CHECK_NEAR (lane.points[0].beat, 0.0, 1e-12);
    EL_CHECK_NEAR (lane.points[1].beat, 3.0, 1e-12);
    EL_CHECK_NEAR (lane.points[2].beat, 4.0, 1e-12);
}

EL_TEST (automation_apply_curve_identity_and_endpoints) {
    const float ts[] = { 0.0f, 0.25f, 0.5f, 0.9f, 1.0f };
    for (float t : ts) EL_CHECK_NEAR (applyCurve (t, 0.0f), t, 1e-7);
    const float curves[] = { 0.5f, -0.7f, 1.0f, -1.0f };
    for (float c : curves) {
        EL_CHECK_NEAR (applyCurve (0.0f, c), 0.0, 1e-5);
        EL_CHECK_NEAR (applyCurve (1.0f, c), 1.0, 1e-5);
    }
}

// ---------------------------------------------------------------------------------------
// MidiClip / MidiSequence
// ---------------------------------------------------------------------------------------

EL_TEST (midi_fromevents_note_pair_fields) {
    std::vector<MidiEvent> evs;
    evs.push_back (mkEv (1.0, 0x91, 64, 90));   // note on, channel 2
    evs.push_back (mkEv (2.5, 0x81, 64, 0));    // note off, channel 2
    MidiClip clip = MidiClip::fromEvents (evs, 16.0);
    EL_CHECK (clip.notes.size() == 1);
    EL_CHECK (clip.controllers.empty());
    if (clip.notes.size() == 1) {
        const MidiNote& n = clip.notes[0];
        EL_CHECK_NEAR (n.start, 1.0, 1e-12);
        EL_CHECK_NEAR (n.length, 1.5, 1e-12);
        EL_CHECK (n.pitch == 64);
        EL_CHECK (n.velocity == 90);
        EL_CHECK (n.channel == 2);
    }
    // Note-on with velocity 0 is a note-off.
    std::vector<MidiEvent> zv;
    zv.push_back (mkEv (0.0, 0x90, 60, 100));
    zv.push_back (mkEv (0.5, 0x90, 60, 0));
    MidiClip c2 = MidiClip::fromEvents (zv, 16.0);
    EL_CHECK (c2.notes.size() == 1);
    if (c2.notes.size() == 1) {
        EL_CHECK_NEAR (c2.notes[0].length, 0.5, 1e-12);
        EL_CHECK (c2.notes[0].channel == 1);
        EL_CHECK (c2.notes[0].velocity == 100);
    }
}

EL_TEST (midi_hanging_note_closed_at_loop_end) {
    std::vector<MidiEvent> evs;
    evs.push_back (mkEv (2.0, 0x90, 48, 80));       // never released
    MidiClip clip = MidiClip::fromEvents (evs, 16.0);
    EL_CHECK (clip.notes.size() == 1);
    if (clip.notes.size() == 1) {
        EL_CHECK_NEAR (clip.notes[0].start, 2.0, 1e-12);
        EL_CHECK_NEAR (clip.notes[0].length, 14.0, 1e-9);   // closed at lengthBeats
    }
    MidiClip shortClip = MidiClip::fromEvents (evs, 4.0);
    EL_CHECK (shortClip.notes.size() == 1);
    if (shortClip.notes.size() == 1) EL_CHECK_NEAR (shortClip.notes[0].length, 2.0, 1e-9);
}

EL_TEST (midi_controllers_kept_and_events_wrap) {
    std::vector<MidiEvent> evs;
    evs.push_back (mkEv (3.0, 0xB0, 7, 100));       // CC7 volume, channel 1
    evs.push_back (mkEv (17.0, 0x90, 50, 90));      // past a 16-beat loop: wraps to beat 1
    evs.push_back (mkEv (18.0, 0x80, 50, 0));
    MidiClip clip = MidiClip::fromEvents (evs, 16.0);
    EL_CHECK (clip.controllers.size() == 1);
    if (clip.controllers.size() == 1) {
        EL_CHECK_NEAR (clip.controllers[0].beat, 3.0, 1e-12);
        EL_CHECK (clip.controllers[0].data[1] == 7);
        EL_CHECK (clip.controllers[0].data[2] == 100);
    }
    EL_CHECK (clip.notes.size() == 1);
    if (clip.notes.size() == 1) {
        EL_CHECK_NEAR (clip.notes[0].start, 1.0, 1e-12);
        EL_CHECK_NEAR (clip.notes[0].length, 1.0, 1e-12);
    }
}

EL_TEST (midi_quantize_moves_starts_to_grid) {
    MidiClip clip;
    clip.lengthBeats = 16.0;
    clip.notes.push_back (mkNote (0.13, 0.5, 60));
    clip.quantize (0.25);                            // full strength
    EL_CHECK (clip.notes.size() == 1);
    if (clip.notes.size() == 1) {
        EL_CHECK_NEAR (clip.notes[0].start, 0.25, 1e-9);
        EL_CHECK_NEAR (clip.notes[0].start + clip.notes[0].length, 0.63, 1e-9);  // end kept
    }

    MidiClip half;
    half.lengthBeats = 16.0;
    half.notes.push_back (mkNote (0.13, 0.5, 60));
    half.quantize (0.25, 0.5f);                      // half strength: halfway to 0.25
    EL_CHECK (half.notes.size() == 1);
    if (half.notes.size() == 1) EL_CHECK_NEAR (half.notes[0].start, 0.19, 1e-9);

    MidiClip noGrid;
    noGrid.lengthBeats = 16.0;
    noGrid.notes.push_back (mkNote (0.13, 0.5, 60));
    noGrid.quantize (0.0);                           // grid <= 0 is a no-op
    EL_CHECK (noGrid.notes.size() == 1);
    if (noGrid.notes.size() == 1) EL_CHECK_NEAR (noGrid.notes[0].start, 0.13, 1e-12);
}

EL_TEST (midi_sequence_sorted_and_note_off_clipped_before_loop_end) {
    MidiClip clip;
    clip.lengthBeats = 16.0;
    clip.notes.push_back (mkNote (2.0, 1.0, 60));
    clip.notes.push_back (mkNote (0.5, 1.0, 62));
    clip.notes.push_back (mkNote (15.0, 4.0, 64));   // runs past the loop end
    clip.controllers.push_back (mkEv (3.0, 0xB0, 1, 2));
    clip.controllers.push_back (mkEv (16.5, 0xB0, 1, 3));   // at/after end: dropped
    MidiSequence seq = MidiSequence::fromClip (clip);
    EL_CHECK (seq.lengthBeats == 16.0);
    EL_CHECK (seq.events.size() == 7);               // 3 notes -> 6 events, + 1 controller

    bool sorted = true;
    for (size_t i = 1; i < seq.events.size(); ++i)
        if (seq.events[i].beat < seq.events[i - 1].beat) sorted = false;
    EL_CHECK (sorted);

    bool found = false;
    for (const auto& e : seq.events)
        if (e.isNoteOff() && e.data[1] == 64) {
            found = true;
            EL_CHECK (e.beat < 16.0);
            EL_CHECK (e.beat > 15.999);
        }
    EL_CHECK (found);
}

EL_TEST (midi_sequence_excludes_muted_and_orders_offs_before_ons) {
    MidiClip clip;
    clip.lengthBeats = 16.0;
    clip.notes.push_back (mkNote (0.0, 1.0, 60));
    clip.notes.push_back (mkNote (1.0, 1.0, 62));
    clip.notes.push_back (mkNote (3.0, 1.0, 64, true));   // muted
    MidiSequence seq = MidiSequence::fromClip (clip);
    EL_CHECK (seq.events.size() == 4);               // muted note contributes nothing
    for (const auto& e : seq.events) EL_CHECK (e.data[1] != 64);

    // At beat 1 the note-off of pitch 60 must precede the note-on of pitch 62.
    int offIdx = -1, onIdx = -1;
    for (size_t i = 0; i < seq.events.size(); ++i) {
        const auto& e = seq.events[i];
        if (e.beat == 1.0 && e.isNoteOff() && e.data[1] == 60) offIdx = (int) i;
        if (e.beat == 1.0 && e.isNoteOn() && e.data[1] == 62) onIdx = (int) i;
    }
    EL_CHECK (offIdx >= 0 && onIdx >= 0);
    EL_CHECK (offIdx < onIdx);
}

// ---------------------------------------------------------------------------------------
// Routing graph
// ---------------------------------------------------------------------------------------

EL_TEST (routing_cycle_detection_self_direct_transitive) {
    std::vector<RouteEdge> edges = { { 1, 2 }, { 2, 3 } };
    EL_CHECK (routing::wouldCreateCycle (edges, 5, 5));    // self edge
    EL_CHECK (routing::wouldCreateCycle (edges, 2, 1));    // direct back edge
    EL_CHECK (routing::wouldCreateCycle (edges, 3, 1));    // transitive back edge
    EL_CHECK (! routing::wouldCreateCycle (edges, 1, 3));  // shortcut is fine
    EL_CHECK (! routing::wouldCreateCycle (edges, 4, 1));  // unrelated node
    EL_CHECK (! routing::wouldCreateCycle (std::vector<RouteEdge>(), 1, 2));
}

EL_TEST (routing_topological_order_feeders_before_buses) {
    // Two tracks feed bus 20, which feeds master 1. Input lists the master first.
    const std::vector<Id> nodes = { 1, 20, 10, 11 };
    const std::vector<RouteEdge> edges = { { 10, 20 }, { 11, 20 }, { 20, 1 } };
    std::vector<Id> order;
    EL_CHECK (routing::topologicalOrder (nodes, edges, order));
    EL_CHECK (order.size() == nodes.size());
    EL_CHECK (indexOf (order, 10) < indexOf (order, 20));
    EL_CHECK (indexOf (order, 11) < indexOf (order, 20));
    EL_CHECK (indexOf (order, 20) < indexOf (order, 1));
}

EL_TEST (routing_topological_order_reports_cycle) {
    const std::vector<Id> nodes = { 1, 2, 3 };
    const std::vector<RouteEdge> edges = { { 1, 2 }, { 2, 1 }, { 2, 3 } };
    std::vector<Id> order;
    EL_CHECK (! routing::topologicalOrder (nodes, edges, order));
    EL_CHECK (order.size() == nodes.size());          // every node is still emitted
    EL_CHECK (order == nodes);                        // cycle nodes appended in input order
}

// ---------------------------------------------------------------------------------------
// GraphicEq
// ---------------------------------------------------------------------------------------

EL_TEST (eq_all_zero_gains_is_flat_and_passthrough) {
    GraphicEq eq;
    eq.prepare (48000.0);
    EL_CHECK (eq.isFlat());
    float zeros[kEqBands] = {};
    eq.setGains (zeros);
    EL_CHECK (eq.isFlat());

    std::vector<float> L (512), R (512);
    makeSine (L, 440.0, 48000.0, 0.7f);
    R = L;
    const std::vector<float> origL = L, origR = R;
    eq.process (L.data(), R.data(), (int) L.size());
    bool identical = true;
    for (size_t i = 0; i < L.size(); ++i)
        if (L[i] != origL[i] || R[i] != origR[i]) identical = false;
    EL_CHECK (identical);
}

EL_TEST (eq_12db_at_1khz_boosts_sine_about_4x) {
    GraphicEq eq;
    eq.prepare (48000.0);
    float g[kEqBands] = {};
    g[5] = 12.0f;                                     // band 5 = 1 kHz
    eq.setGains (g);
    EL_CHECK (! eq.isFlat());

    const size_t N = 96000;                           // 2 s; 1 kHz is an exact 2000 cycles
    std::vector<float> in (N);
    makeSine (in, 1000.0, 48000.0, 0.5f);
    std::vector<float> L = in, R = in;
    eq.process (L.data(), R.data(), (int) N);
    const double rIn = rms (in, N / 2, N);           // second half: filter has settled
    const double rOut = rms (L, N / 2, N);
    EL_CHECK (rIn > 0.0);
    const double ratio = rOut / rIn;
    EL_CHECK (ratio > 3.0 && ratio < 4.5);

    // Back to flat: the EQ reports flat again.
    float zeros[kEqBands] = {};
    eq.setGains (zeros);
    EL_CHECK (eq.isFlat());
}

EL_TEST (eq_reset_clears_filter_state) {
    GraphicEq eq;
    eq.prepare (48000.0);
    float g[kEqBands] = {};
    g[5] = 12.0f;
    eq.setGains (g);
    std::vector<float> burst (4800);
    makeSine (burst, 1000.0, 48000.0, 0.5f);
    eq.process (burst.data(), nullptr, (int) burst.size());

    GraphicEq stale = eq;                             // copy that keeps the filter state
    eq.reset();

    std::vector<float> silence (256, 0.0f);
    std::vector<float> silenceCopy = silence;
    stale.process (silenceCopy.data(), nullptr, (int) silenceCopy.size());
    double staleTail = 0.0;
    for (float v : silenceCopy) staleTail += std::abs (v);
    EL_CHECK (staleTail > 0.0);                       // premise: un-reset state rings on

    eq.process (silence.data(), nullptr, (int) silence.size());
    bool allZero = true;
    for (float v : silence) if (v != 0.0f) allZero = false;
    EL_CHECK (allZero);
}

// ---------------------------------------------------------------------------------------
// TimeMath
// ---------------------------------------------------------------------------------------

EL_TEST (timemath_next_boundary_on_and_after_grid) {
    EL_CHECK_NEAR (nextBoundary (4.0, 4.0), 4.0, 1e-12);          // exactly on a line
    EL_CHECK_NEAR (nextBoundary (4.0 + 1.0e-7, 4.0), 4.0, 1e-12); // tolerance: still that line
    EL_CHECK_NEAR (nextBoundary (4.0 + 1.0e-5, 4.0), 8.0, 1e-12); // past tolerance: next one
    EL_CHECK_NEAR (nextBoundary (3.5, 4.0), 4.0, 1e-12);
    EL_CHECK_NEAR (nextBoundary (5.0, 4.0, 1.0), 5.0, 1e-12);     // grid origin shifted
    EL_CHECK_NEAR (nextBoundary (4.5, 0.0), 4.5, 1e-12);          // no grid: unchanged
}

EL_TEST (timemath_quantize_beats_per_mode) {
    const double bpb = 4.0, master = 16.0;
    EL_CHECK_NEAR (quantizeBeats (Quantize::None, bpb, master), 0.0, 1e-12);
    EL_CHECK_NEAR (quantizeBeats (Quantize::Beat, bpb, master), 1.0, 1e-12);
    EL_CHECK_NEAR (quantizeBeats (Quantize::Bar, bpb, master), 4.0, 1e-12);
    EL_CHECK_NEAR (quantizeBeats (Quantize::TwoBars, bpb, master), 8.0, 1e-12);
    EL_CHECK_NEAR (quantizeBeats (Quantize::FourBars, bpb, master), 16.0, 1e-12);
    EL_CHECK_NEAR (quantizeBeats (Quantize::Master, bpb, master), 16.0, 1e-12);
    EL_CHECK_NEAR (quantizeBeats (Quantize::Master, bpb, 12.0), 12.0, 1e-12);  // follows masterBeats
}

EL_TEST (timemath_sample_offset_clamps_to_block) {
    BlockTime t;
    t.beatStart = 8.0;
    t.beatsPerSample = 0.5;                           // two samples per beat
    t.numSamples = 64;                                // block covers beats 8 .. 40
    EL_CHECK (sampleOffsetForBeat (t, 8.0) == 0);
    EL_CHECK (sampleOffsetForBeat (t, 10.0) == 4);
    EL_CHECK (sampleOffsetForBeat (t, 8.25) == 1);
    EL_CHECK (sampleOffsetForBeat (t, 7.0) == 0);      // before the block: clamped
    EL_CHECK (sampleOffsetForBeat (t, 1000.0) == 64);  // after the block: clamped
    EL_CHECK (sampleOffsetForBeat (t, 40.0) == 64);

    BlockTime stopped;                                 // transport stopped: beatsPerSample == 0
    stopped.beatStart = 8.0;
    stopped.numSamples = 64;
    EL_CHECK (sampleOffsetForBeat (stopped, 7.9) == 0);
    EL_CHECK (sampleOffsetForBeat (stopped, 8.1) == 64);
}

EL_TEST (timemath_positive_mod) {
    EL_CHECK_NEAR (positiveMod (-1.0, 4.0), 3.0, 1e-12);
    EL_CHECK_NEAR (positiveMod (5.5, 4.0), 1.5, 1e-12);
    EL_CHECK_NEAR (positiveMod (-0.5, 4.0), 3.5, 1e-12);
    EL_CHECK_NEAR (positiveMod (-8.0, 4.0), 0.0, 1e-12);
    EL_CHECK_NEAR (positiveMod (7.0, 0.0), 0.0, 1e-12);   // non-positive modulus
}

// ---------------------------------------------------------------------------------------
// ParamMapping
// ---------------------------------------------------------------------------------------

EL_TEST (param_volume_unity_and_inverse) {
    EL_CHECK_NEAR (pm::volumeToGain (pm::kUnityVolume), 1.0, 1e-5);
    EL_CHECK_NEAR (pm::volumeToGain (0.0f), 0.0, 1e-9);
    EL_CHECK_NEAR (pm::volumeToGain (1.0f), 2.0, 1e-6);
    EL_CHECK_NEAR (pm::volumeToDb (pm::kUnityVolume), 0.0, 1e-3);
    const float vs[] = { 0.0f, 0.2f, 0.5f, 0.9f, 1.0f };
    for (float v : vs) EL_CHECK_NEAR (pm::gainToVolume (pm::volumeToGain (v)), v, 1e-5);
    EL_CHECK_NEAR (pm::gainToVolume (-1.0f), 0.0, 1e-9);
    EL_CHECK_NEAR (pm::gainToDb (0.0f), -120.0, 1e-6);
}

EL_TEST (param_pan_centre_and_hard_edges) {
    float l = -1.0f, r = -1.0f;
    pm::panGains (0.5f, l, r);
    EL_CHECK_NEAR (l, 1.0, 1e-4);                     // centre is unity on both sides
    EL_CHECK_NEAR (r, 1.0, 1e-4);
    pm::panGains (0.0f, l, r);                        // hard left
    EL_CHECK_NEAR (l, 1.0, 1e-6);
    EL_CHECK_NEAR (r, 0.0, 1e-6);
    pm::panGains (1.0f, l, r);                        // hard right
    EL_CHECK_NEAR (l, 0.0, 1e-6);
    EL_CHECK_NEAR (r, 1.0, 1e-6);
    EL_CHECK_NEAR (pm::panToBipolar (0.0f), -1.0, 1e-9);
    EL_CHECK_NEAR (pm::panToBipolar (0.5f), 0.0, 1e-9);
    EL_CHECK_NEAR (pm::panToBipolar (1.0f), 1.0, 1e-9);
}

EL_TEST (param_eq_and_input_gain_mapping) {
    EL_CHECK_NEAR (pm::eqToDb (0.5f), 0.0, 1e-6);
    EL_CHECK_NEAR (pm::eqToDb (1.0f), 12.0, 1e-6);
    EL_CHECK_NEAR (pm::eqToDb (0.0f), -12.0, 1e-6);
    const float ns[] = { 0.0f, 0.3f, 0.5f, 0.77f, 1.0f };
    for (float n : ns) EL_CHECK_NEAR (pm::dbToEq (pm::eqToDb (n)), n, 1e-6);
    EL_CHECK_NEAR (pm::dbToEq (20.0f), 1.0, 1e-6);    // clamps
    EL_CHECK_NEAR (pm::inputGainToDb (0.5f), 0.0, 1e-9);
    EL_CHECK_NEAR (pm::inputGainToDb (1.0f), 24.0, 1e-6);
    EL_CHECK_NEAR (pm::inputGainToDb (0.0f), -24.0, 1e-6);
}

// ---------------------------------------------------------------------------------------
// Lock-free primitives
// ---------------------------------------------------------------------------------------

EL_TEST (spsc_queue_fifo_order_and_full) {
    SpscQueue<int> q (5);                             // rounds up to 8 slots
    EL_CHECK (q.empty());
    int v = -1;
    EL_CHECK (! q.pop (v));
    for (int i = 0; i < 8; ++i) EL_CHECK (q.push (i));
    EL_CHECK (! q.push (8));                          // full
    EL_CHECK (! q.empty());
    for (int i = 0; i < 8; ++i) {
        EL_CHECK (q.pop (v));
        EL_CHECK (v == i);
    }
    EL_CHECK (! q.pop (v));
    EL_CHECK (q.empty());
    // Wrap-around keeps order.
    for (int i = 100; i < 1100; ++i) {
        EL_CHECK (q.push (i));
        EL_CHECK (q.pop (v));
        EL_CHECK (v == i);
    }
}

EL_TEST (chunk_pool_reserve_pop_push_counts) {
    ChunkPool pool;
    pool.reserve (4);
    EL_CHECK (pool.available() == 4);
    EL_CHECK (pool.totalAllocated() == 4);
    AudioChunk* got[4] = {};
    for (int i = 0; i < 4; ++i) {
        got[i] = pool.pop();
        EL_CHECK (got[i] != nullptr);
    }
    EL_CHECK (pool.available() == 0);
    EL_CHECK (pool.pop() == nullptr);                 // exhausted
    pool.push (got[0]);
    EL_CHECK (pool.available() == 1);
    pool.push (nullptr);                              // ignored
    EL_CHECK (pool.available() == 1);
    pool.reserve (1);                                 // already enough: no allocation
    EL_CHECK (pool.totalAllocated() == 4);
    pool.reserve (6);                                 // tops up to 6 free
    EL_CHECK (pool.available() == 6);
    EL_CHECK (pool.totalAllocated() == 9);
}
