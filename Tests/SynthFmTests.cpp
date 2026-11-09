// Unit tests for the Chimera FM engine (Source/Synth/dsp/Fm.*).
#include "TestFramework.h"
#include "Synth/dsp/Fm.h"
#include <chrono>
#include <cstring>

using namespace el::chimera;

namespace {

std::vector<float> renderVoice (FmVoice& v, int n, float semis = 0.0f, float fm = 1.0f) {
    std::vector<float> buf ((size_t) n);
    v.render (buf.data(), n, semis, fm);
    return buf;
}

bool allFinite (const std::vector<float>& b) {
    for (float x : b) if (! std::isfinite (x)) return false;
    return true;
}

float peakOf (const std::vector<float>& b, size_t from = 0) {
    float p = 0.0f;
    for (size_t i = from; i < b.size(); ++i) p = std::max (p, std::fabs (b[i]));
    return p;
}

// Frequency estimate from positive-going zero crossings.
double estimateHz (const std::vector<float>& b, size_t from, double sr) {
    int count = 0;
    double first = -1.0, last = -1.0;
    for (size_t i = from + 1; i < b.size(); ++i) {
        if (b[i - 1] < 0.0f && b[i] >= 0.0f) {
            const double t = (double) (i - 1) + (double) (-b[i - 1]) / (double) (b[i] - b[i - 1]);
            if (first < 0.0) first = t;
            last = t;
            ++count;
        }
    }
    if (count < 2) return 0.0;
    return (double) (count - 1) * sr / (last - first);
}

Dx7Patch randomPatch (Rng& r) {
    Dx7Patch p;
    auto u = [&] (int hi) { return (uint8_t) (r.nextU() % (uint32_t) (hi + 1)); };
    for (auto& o : p.op) {
        for (int i = 0; i < 4; ++i) { o.egRate[i] = u (99); o.egLevel[i] = u (99); }
        o.kbdBreakPoint = u (99); o.kbdLeftDepth = u (99); o.kbdRightDepth = u (99);
        o.kbdLeftCurve = u (3); o.kbdRightCurve = u (3); o.kbdRateScaling = u (7);
        o.ampModSens = u (3); o.keyVelSens = u (7); o.outputLevel = u (99);
        o.oscMode = u (1); o.freqCoarse = u (31); o.freqFine = u (99); o.detune = u (14);
    }
    for (int i = 0; i < 4; ++i) { p.pitchEgRate[i] = u (99); p.pitchEgLevel[i] = u (99); }
    p.algorithm = u (31); p.feedback = u (7); p.oscKeySync = u (1);
    p.lfoSpeed = u (99); p.lfoDelay = u (99); p.lfoPitchModDepth = u (99); p.lfoAmpModDepth = u (99);
    p.lfoKeySync = u (1); p.lfoWave = u (5); p.lfoPitchModSens = u (7); p.transpose = u (48);
    for (int i = 0; i < 10; ++i) p.name[i] = (char) (65 + u (25));
    p.name[10] = 0;
    return p;
}

bool samePatch (const Dx7Patch& a, const Dx7Patch& b) {
    return dx7ToVced (a) == dx7ToVced (b) && std::strncmp (a.name, b.name, 10) == 0;
}

} // namespace

EL_TEST (fm_init_voice_is_440_sine) {
    FmVoice v;
    v.prepare (48000.0f);
    const Dx7Patch p = dx7InitVoice();
    v.noteOn (p, 69, 100);
    const auto b = renderVoice (v, 48000);
    EL_CHECK (allFinite (b));
    EL_CHECK_NEAR (peakOf (b, 2000), 1.0, 0.05);
    EL_CHECK_NEAR (estimateHz (b, 2000, 48000.0), 440.0, 1.0);
    // pure sine: compare with the reference after the (very short) attack
    double maxErr = 0.0;
    for (size_t i = 3000; i < 6000; ++i)
        maxErr = std::max (maxErr, std::abs ((double) b[i] - std::sin (2.0 * 3.14159265358979 * 440.0 * (double) i / 48000.0)));
    EL_CHECK (maxErr < 0.01);
}

EL_TEST (fm_pitch_semis_doubles_frequency) {
    FmVoice v;
    v.prepare (48000.0f);
    const Dx7Patch p = dx7InitVoice();
    v.noteOn (p, 69, 100);
    const auto b = renderVoice (v, 48000, 12.0f);
    EL_CHECK_NEAR (estimateHz (b, 2000, 48000.0), 880.0, 2.0);
    FmVoice w;
    w.prepare (48000.0f);
    w.noteOn (p, 57, 100);
    EL_CHECK_NEAR (estimateHz (renderVoice (w, 48000), 2000, 48000.0), 220.0, 1.0);
}

EL_TEST (fm_note_off_goes_inactive) {
    FmVoice v;
    v.prepare (48000.0f);
    const Dx7Patch p = dx7InitVoice();
    EL_CHECK (! v.active());
    v.noteOn (p, 60, 100);
    EL_CHECK (v.active());
    renderVoice (v, 4800);
    EL_CHECK (v.active());
    v.noteOff();
    bool went = false;
    for (int i = 0; i < 100 && ! went; ++i) {
        const auto b = renderVoice (v, 480);
        EL_CHECK (allFinite (b));
        if (! v.active()) went = true;
    }
    EL_CHECK (went);
    // a slow release lasts longer than a fast one
    Dx7Patch slow = dx7InitVoice();
    slow.op[0].egRate[3] = 40;
    FmVoice s;
    s.prepare (48000.0f);
    s.noteOn (slow, 60, 100);
    renderVoice (s, 4800);
    s.noteOff();
    renderVoice (s, 4800);
    EL_CHECK (s.active());
}

EL_TEST (fm_algorithm_32_all_ops_bounded) {
    Dx7Patch p = dx7InitVoice();
    p.algorithm = 31;
    for (auto& o : p.op) o.outputLevel = 99;
    p.feedback = 7;
    FmVoice v;
    v.prepare (48000.0f);
    v.noteOn (p, 60, 127);
    const auto b = renderVoice (v, 48000);
    EL_CHECK (allFinite (b));
    EL_CHECK (peakOf (b) <= 2.0f);
    EL_CHECK (peakOf (b) > 0.5f);
}

EL_TEST (fm_amount_zero_removes_modulation) {
    Dx7Patch p = dx7InitVoice();    // algorithm 1: OP2 modulates OP1
    p.op[1].outputLevel = 99;
    FmVoice a, c;
    a.prepare (48000.0f); c.prepare (48000.0f);
    a.noteOn (p, 69, 100);
    c.noteOn (p, 69, 100);
    const auto dry = renderVoice (a, 9600, 0.0f, 0.0f);
    const auto wet = renderVoice (c, 9600, 0.0f, 1.0f);
    double maxErr = 0.0, diff = 0.0;
    for (size_t i = 2000; i < dry.size(); ++i) {
        maxErr = std::max (maxErr, std::abs ((double) dry[i] - std::sin (2.0 * 3.14159265358979 * 440.0 * (double) i / 48000.0)));
        diff += std::abs ((double) dry[i] - (double) wet[i]);
    }
    EL_CHECK (maxErr < 0.01);
    EL_CHECK (diff / (double) dry.size() > 0.05);
    // fmAmount 2 is allowed and stays bounded
    FmVoice d;
    d.prepare (48000.0f);
    d.noteOn (p, 69, 100);
    const auto big = renderVoice (d, 9600, 0.0f, 2.0f);
    EL_CHECK (allFinite (big));
    EL_CHECK (peakOf (big) <= 1.01f);
}

EL_TEST (fm_feedback_changes_spectrum) {
    Dx7Patch p = dx7InitVoice();
    p.algorithm = 31;
    for (auto& o : p.op) o.outputLevel = 0;
    p.op[5].outputLevel = 99;
    FmVoice a, c;
    a.prepare (48000.0f); c.prepare (48000.0f);
    a.noteOn (p, 57, 100);
    p.feedback = 7;
    c.noteOn (p, 57, 100);
    const auto s0 = renderVoice (a, 9600);
    const auto s7 = renderVoice (c, 9600);
    EL_CHECK (allFinite (s0));
    EL_CHECK (allFinite (s7));
    EL_CHECK (peakOf (s7) <= 1.01f);
    // High-frequency content (first-difference energy relative to signal energy) rises with feedback.
    auto hf = [] (const std::vector<float>& b) {
        double e = 0.0, d = 0.0;
        for (size_t i = 2001; i < b.size(); ++i) { e += (double) b[i] * b[i]; const double x = b[i] - b[i - 1]; d += x * x; }
        return d / e;
    };
    EL_CHECK (hf (s7) > hf (s0) * 3.0);
}

EL_TEST (fm_vced_round_trip) {
    Rng r;
    r.seed (42);
    for (int n = 0; n < 50; ++n) {
        const Dx7Patch p = randomPatch (r);
        const auto v = dx7ToVced (p);
        const Dx7Patch q = dx7FromVced (v.data());
        EL_CHECK (samePatch (p, q));
        EL_CHECK (dx7ToVced (q) == v);
    }
    // layout spot checks: OP6 first, 21 bytes per operator
    Dx7Patch p = dx7InitVoice();
    const auto v = dx7ToVced (p);
    EL_CHECK (v[21 * 5 + 16] == 99);   // OP1 output level is the last operator block
    EL_CHECK (v[16] == 0);
    EL_CHECK (v[134] == 0 && v[144] == 24);
    EL_CHECK (std::strncmp (reinterpret_cast<const char*> (&v[145]), "INIT VOICE", 10) == 0);
    EL_CHECK (v[126] == 99 && v[130] == 50);
}

EL_TEST (fm_cart_round_trip) {
    Rng r;
    r.seed (7);
    std::vector<Dx7Patch> in { randomPatch (r), randomPatch (r), randomPatch (r) };
    std::memcpy (in[0].name, "BRASS  1  ", 10);
    std::memcpy (in[1].name, "E.PIANO 1 ", 10);
    const auto cart = dx7MakeCart (in);
    EL_CHECK (cart.size() == 4104);
    EL_CHECK (cart[0] == 0xF0 && cart[1] == 0x43 && cart[3] == 0x09 && cart[4] == 0x20 && cart[5] == 0x00);
    EL_CHECK (cart[4103] == 0xF7);
    int sum = 0;
    for (size_t i = 6; i < 6 + 4096; ++i) { sum += cart[i]; EL_CHECK (cart[i] < 0x80); }
    EL_CHECK (((sum + cart[4102]) & 0x7F) == 0);

    std::vector<Dx7Patch> out;
    std::string err;
    EL_CHECK (parseDx7SysEx (cart.data(), cart.size(), out, err));
    EL_CHECK (out.size() == 32);
    if (out.size() == 32) {
        for (size_t i = 0; i < 3; ++i) EL_CHECK (samePatch (in[i], out[i]));
        for (size_t i = 3; i < 32; ++i) EL_CHECK (samePatch (dx7InitVoice(), out[i]));
        EL_CHECK (std::strncmp (out[0].name, "BRASS  1  ", 10) == 0);
    }
    // raw 4096 packed bytes
    std::vector<Dx7Patch> raw;
    EL_CHECK (parseDx7SysEx (cart.data() + 6, 4096, raw, err));
    EL_CHECK (raw.size() == 32);
    if (raw.size() == 32) EL_CHECK (samePatch (raw[1], in[1]));
}

EL_TEST (fm_single_voice_dump) {
    Rng r;
    r.seed (99);
    const Dx7Patch p = randomPatch (r);
    const auto v = dx7ToVced (p);
    std::vector<uint8_t> msg { 0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B };
    msg.insert (msg.end(), v.begin(), v.end());
    int sum = 0;
    for (uint8_t b : v) sum += b;
    msg.push_back ((uint8_t) ((-sum) & 0x7F));
    msg.push_back (0xF7);
    EL_CHECK (msg.size() == 163);
    std::vector<Dx7Patch> out;
    std::string err;
    EL_CHECK (parseDx7SysEx (msg.data(), msg.size(), out, err));
    EL_CHECK (out.size() == 1);
    if (out.size() == 1) EL_CHECK (samePatch (out[0], p));
}

EL_TEST (fm_rejects_garbage) {
    std::vector<Dx7Patch> out;
    std::string err;
    EL_CHECK (! parseDx7SysEx (nullptr, 0, out, err));
    EL_CHECK (! err.empty());
    uint8_t few[10] { 1, 2, 3 };
    EL_CHECK (! parseDx7SysEx (few, sizeof few, out, err));
    EL_CHECK (out.empty());
    Rng r;
    r.seed (3);
    std::vector<uint8_t> junk (5000);
    for (auto& b : junk) b = (uint8_t) (r.nextU() & 0xFF);
    EL_CHECK (! parseDx7SysEx (junk.data(), junk.size(), out, err));
    EL_CHECK (! parseDx7SysEx (junk.data(), 4100, out, err));
    // right size, wrong manufacturer
    auto cart = dx7MakeCart ({});
    cart[1] = 0x41;
    EL_CHECK (! parseDx7SysEx (cart.data(), cart.size(), out, err));
    EL_CHECK (! err.empty());
    // Yamaha but another format
    cart[1] = 0x43; cart[3] = 0x04;
    EL_CHECK (! parseDx7SysEx (cart.data(), cart.size(), out, err));
    // truncated bulk
    cart[3] = 0x09;
    EL_CHECK (! parseDx7SysEx (cart.data(), 3000, out, err));
}

EL_TEST (fm_tolerates_bad_checksum_and_sloppy_values) {
    Rng r;
    r.seed(11);
    std::vector<Dx7Patch> in { randomPatch (r), randomPatch (r) };
    auto cart = dx7MakeCart (in);
    cart[4102] ^= 0x55;
    std::vector<Dx7Patch> out;
    std::string err;
    EL_CHECK (parseDx7SysEx (cart.data(), cart.size(), out, err));
    EL_CHECK (out.size() == 32);
    if (out.size() == 32) EL_CHECK (samePatch (out[0], in[0]));
    // wildly out-of-range bytes are clamped
    for (size_t i = 6; i < 6 + 4096; ++i) cart[i] = 0xFF;
    EL_CHECK (parseDx7SysEx (cart.data(), cart.size(), out, err));
    EL_CHECK (out.size() == 32);
    for (const auto& p : out) {
        for (const auto& o : p.op) {
            EL_CHECK (o.outputLevel <= 99 && o.egRate[0] <= 99 && o.detune <= 14 && o.freqCoarse <= 31 && o.kbdRateScaling <= 7);
        }
        EL_CHECK (p.algorithm <= 31 && p.feedback <= 7 && p.lfoWave <= 5 && p.transpose <= 48);
    }
    FmVoice v;
    v.prepare (48000.0f);
    v.noteOn (out[0], 60, 100);
    EL_CHECK (allFinite (renderVoice (v, 4800)));
}

EL_TEST (fm_all_algorithms_finite_and_bounded) {
    Rng r;
    r.seed (2024);
    for (int alg = 0; alg < 32; ++alg) {
        for (int trial = 0; trial < 6; ++trial) {
            Dx7Patch p = randomPatch (r);
            p.algorithm = (uint8_t) alg;
            if (trial == 0) for (auto& o : p.op) { o.outputLevel = 99; o.oscMode = 0; for (int k = 0; k < 3; ++k) o.egLevel[k] = 99; }
            FmVoice v;
            v.prepare (44100.0f);
            v.noteOn (p, 20 + (int) (r.nextU() % 90), 1 + (int) (r.nextU() % 127));
            for (int blk = 0; blk < 12; ++blk) {
                if (blk == 8) v.noteOff();
                const auto b = renderVoice (v, 1000 + blk, (float) blk - 6.0f, (float) (blk % 3));
                EL_CHECK (allFinite (b));
                EL_CHECK (peakOf (b) <= 2.0f);
            }
        }
    }
}

EL_TEST (fm_routing_and_carriers) {
    // Alg 5 (index 4): three carriers 1,3,5. Only OP5 audible -> a plain sine at its own ratio.
    Dx7Patch p = dx7InitVoice();
    p.algorithm = 4;
    p.op[0].outputLevel = 0;
    p.op[4].outputLevel = 99;
    p.op[4].freqCoarse = 2;
    FmVoice v;
    v.prepare (48000.0f);
    v.noteOn (p, 69, 100);
    EL_CHECK_NEAR (estimateHz (renderVoice (v, 48000), 2000, 48000.0), 880.0, 2.0);
    // Alg 1: OP6 is a modulator of a chain, silent output on its own when it is the only op on
    Dx7Patch q = dx7InitVoice();
    q.op[0].outputLevel = 0;
    q.op[5].outputLevel = 99;
    FmVoice w;
    w.prepare (48000.0f);
    w.noteOn (q, 69, 100);
    EL_CHECK (peakOf (renderVoice (w, 4800)) < 1e-4f);
}

EL_TEST (fm_ratio_fixed_and_detune) {
    Dx7Patch p = dx7InitVoice();
    p.op[0].freqCoarse = 0;   // 0.5
    FmVoice v;
    v.prepare (48000.0f);
    v.noteOn (p, 69, 100);
    EL_CHECK_NEAR (estimateHz (renderVoice (v, 48000), 2000, 48000.0), 220.0, 0.8);
    p.op[0].freqCoarse = 1; p.op[0].freqFine = 50;   // 1.5
    v.noteOn (p, 69, 100);
    EL_CHECK_NEAR (estimateHz (renderVoice (v, 48000), 2000, 48000.0), 660.0, 1.5);
    p.op[0].freqFine = 0; p.op[0].oscMode = 1; p.op[0].freqCoarse = 2;   // fixed 100 Hz
    v.noteOn (p, 40, 100);
    const auto b = renderVoice (v, 48000, 7.0f);
    EL_CHECK_NEAR (estimateHz (b, 2000, 48000.0), 100.0, 0.5);
    p.op[0].oscMode = 0; p.op[0].freqCoarse = 1; p.op[0].detune = 14;
    v.noteOn (p, 69, 100);
    const double f = estimateHz (renderVoice (v, 96000), 2000, 48000.0);
    EL_CHECK (f > 440.5 && f < 450.0);
    p.transpose = 36;   // +12 semitones
    p.op[0].detune = 7;
    v.noteOn (p, 69, 100);
    EL_CHECK_NEAR (estimateHz (renderVoice (v, 48000), 2000, 48000.0), 880.0, 2.0);
}

EL_TEST (fm_velocity_and_level_scaling) {
    Dx7Patch p = dx7InitVoice();
    FmVoice a, b;
    a.prepare (48000.0f); b.prepare (48000.0f);
    p.op[0].keyVelSens = 7;
    a.noteOn (p, 60, 127);
    b.noteOn (p, 60, 30);
    EL_CHECK (peakOf (renderVoice (b, 4800), 1000) < 0.5f * peakOf (renderVoice (a, 4800), 1000));
    // velocity sensitivity 0: velocity is irrelevant
    p.op[0].keyVelSens = 0;
    a.noteOn (p, 60, 127);
    b.noteOn (p, 60, 30);
    EL_CHECK_NEAR (peakOf (renderVoice (a, 4800), 1000), peakOf (renderVoice (b, 4800), 1000), 0.01);
    // keyboard level scaling: -LIN right depth makes high notes quieter
    p.op[0].kbdRightDepth = 99; p.op[0].kbdRightCurve = 0; p.op[0].kbdBreakPoint = 39;
    a.noteOn (p, 60, 100);
    b.noteOn (p, 96, 100);
    EL_CHECK (peakOf (renderVoice (b, 4800), 1000) < 0.5f * peakOf (renderVoice (a, 4800), 1000));
    // output level lowers amplitude by roughly 0.75 dB per step
    Dx7Patch q = dx7InitVoice();
    q.op[0].outputLevel = 79;
    a.noteOn (q, 60, 100);
    const float pk = peakOf (renderVoice (a, 4800), 1000);
    EL_CHECK (pk > 0.1f && pk < 0.35f);
}

EL_TEST (fm_envelope_shape) {
    Dx7Patch p = dx7InitVoice();
    p.op[0].egRate[0] = 60;     // slower attack
    p.op[0].egLevel[0] = 99;
    FmVoice v;
    v.prepare (48000.0f);
    v.noteOn (p, 69, 100);
    const auto b = renderVoice (v, 48000);
    EL_CHECK (peakOf (b, 0) > 0.95f);
    EL_CHECK (peakOf (std::vector<float> (b.begin(), b.begin() + 100)) < 0.2f);
    // decay to a lower sustain level
    Dx7Patch d = dx7InitVoice();
    d.op[0].egRate[1] = 70; d.op[0].egLevel[1] = 79; d.op[0].egLevel[2] = 79;
    v.noteOn (d, 69, 100);
    const auto c = renderVoice (v, 48000);
    const float late = peakOf (std::vector<float> (c.end() - 2000, c.end()));
    EL_CHECK (late < 0.4f && late > 0.1f);
}

EL_TEST (fm_lfo_and_pitch_eg) {
    Dx7Patch p = dx7InitVoice();
    p.lfoSpeed = 40; p.lfoPitchModDepth = 99; p.lfoPitchModSens = 7; p.lfoWave = 4;
    FmVoice v;
    v.prepare (48000.0f);
    v.noteOn (p, 69, 100);
    const auto b = renderVoice (v, 96000);
    EL_CHECK (allFinite (b));
    // vibrato: first half-second zero-crossing counts differ between windows
    int lo = 1000000, hi = 0;
    for (size_t w = 0; w + 4800 <= b.size(); w += 4800) {
        int c = 0;
        for (size_t i = w + 1; i < w + 4800; ++i) if (b[i - 1] < 0 && b[i] >= 0) ++c;
        lo = std::min (lo, c); hi = std::max (hi, c);
    }
    EL_CHECK (hi - lo > 10);
    // amp mod: tremolo makes amplitude vary
    Dx7Patch t = dx7InitVoice();
    t.lfoSpeed = 40; t.lfoAmpModDepth = 99; t.op[0].ampModSens = 3; t.lfoWave = 4;
    v.noteOn (t, 69, 100);
    const auto tb = renderVoice (v, 96000);
    float mn = 10.0f, mx = 0.0f;
    for (size_t w = 4800; w + 480 <= tb.size(); w += 480) {
        float pk = 0.0f;
        for (size_t i = w; i < w + 480; ++i) pk = std::max (pk, std::fabs (tb[i]));
        mn = std::min (mn, pk); mx = std::max (mx, pk);
    }
    EL_CHECK (mx > 0.9f && mn < 0.5f);
    // pitch EG: starts an octave up (level 74 ~ +? semitones), settles to 50 = no change
    Dx7Patch e = dx7InitVoice();
    e.pitchEgLevel[0] = 99; e.pitchEgRate[0] = 99; e.pitchEgLevel[1] = 50; e.pitchEgRate[1] = 70;
    e.pitchEgLevel[2] = 50; e.pitchEgLevel[3] = 50;
    v.noteOn (e, 69, 100);
    const auto eb = renderVoice (v, 96000);
    EL_CHECK_NEAR (estimateHz (std::vector<float> (eb.end() - 24000, eb.end()), 0, 48000.0), 440.0, 1.0);
    EL_CHECK (allFinite (eb));
}

EL_TEST (fm_cpu_cost_is_small) {
    Dx7Patch p = dx7InitVoice();
    p.algorithm = 31;
    for (auto& o : p.op) o.outputLevel = 99;
    p.feedback = 5;
    FmVoice v;
    v.prepare (48000.0f);
    v.noteOn (p, 60, 100);
    std::vector<float> buf (480);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) v.render (buf.data(), 480, 0.0f, 1.0f);   // 10 s of audio
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  [fm] 10 s of 6-op audio rendered in %.1f ms (%.2f%% of one core)\n", secs * 1000.0, secs / 10.0 * 100.0);
    EL_CHECK (secs / 10.0 < 0.05);
}
