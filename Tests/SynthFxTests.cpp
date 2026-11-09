#include "TestFramework.h"
#include "Synth/dsp/Effects.h"
#include "Synth/dsp/Sampler.h"
#include <algorithm>

using namespace el::chimera;

namespace {
SampleData makeSine (int len, double sr, double hz, int root) {
    SampleData s; s.sampleRate = sr; s.rootNote = root;
    s.left.resize ((size_t) len);
    for (int i = 0; i < len; ++i) s.left[(size_t) i] = (float) std::sin (2.0 * 3.141592653589793 * hz * i / sr);
    return s;
}
int crossings (const std::vector<float>& x) {
    int c = 0;
    for (size_t i = 1; i < x.size(); ++i) if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++c;
    return c;
}
std::vector<float> renderAll (SamplerVoice& v, int total, float note, const SamplerParams& p, int block = 256) {
    std::vector<float> out ((size_t) total), tmpR ((size_t) block), tmpL ((size_t) block);
    for (int i = 0; i < total; i += block) {
        const int n = std::min (block, total - i);
        v.render (tmpL.data(), tmpR.data(), n, note, p);
        std::copy (tmpL.begin(), tmpL.begin() + n, out.begin() + i);
    }
    return out;
}
bool finite (const std::vector<float>& x) { for (float f : x) if (! std::isfinite (f)) return false; return true; }
}

EL_TEST (sampler_octave_up) {
    auto s = makeSine (48000, 48000.0, 440.0, 69);
    SamplerVoice v; SamplerParams p;
    v.start (&s, 81.0f, 1.0f, p, 48000.0);
    auto out = renderAll (v, 20000, 81.0f, p);
    EL_CHECK_NEAR (crossings (out), 880.0 * 20000 / 48000.0, 2.0);
}

EL_TEST (sampler_unison_and_outrate) {
    auto s = makeSine (48000, 48000.0, 440.0, 69);
    SamplerVoice v; SamplerParams p;
    v.start (&s, 69.0f, 1.0f, p, 96000.0);
    auto out = renderAll (v, 40000, 69.0f, p);
    EL_CHECK_NEAR (crossings (out), 440.0 * 40000 / 96000.0, 2.0);
    v.start (&s, 69.0f, 1.0f, p, 48000.0);
    out = renderAll (v, 40000, 69.0f, p);
    EL_CHECK_NEAR (crossings (out), 440.0 * 40000 / 48000.0, 2.0);
}

EL_TEST (sampler_loops_no_oob) {
    for (LoopMode lm : { LoopMode::Forward, LoopMode::PingPong }) {
        for (int rev = 0; rev < 2; ++rev) {
            auto s = makeSine (1000, 48000.0, 440.0, 69);
            s.right = s.left;
            SamplerVoice v; SamplerParams p; p.loop = lm; p.reverse = rev != 0;
            p.loopStart = 0.2f; p.loopEnd = 0.8f;
            v.start (&s, 69.0f, 1.0f, p, 48000.0);
            std::vector<float> l (256), r (256);
            bool alive = true; bool nonzero = false; bool ok = true;
            for (int b = 0; b < 20; ++b) {
                alive = v.render (l.data(), r.data(), 256, 69.0f + (b % 5) * 7.0f, p);
                for (int i = 0; i < 256; ++i) { ok = ok && std::isfinite (l[i]) && std::fabs (l[i]) < 2.0f; nonzero = nonzero || l[i] != 0.0f; }
            }
            EL_CHECK (alive); EL_CHECK (ok); EL_CHECK (nonzero);
        }
    }
}

EL_TEST (sampler_moving_loop_points_and_extremes) {
    auto s = makeSine (500, 44100.0, 300.0, 60);
    Rng rng; rng.seed (7);
    for (LoopMode lm : { LoopMode::Off, LoopMode::Forward, LoopMode::PingPong }) {
        SamplerVoice v; SamplerParams p; p.loop = lm; p.reverse = lm == LoopMode::PingPong;
        v.start (&s, 60.0f, 1.0f, p, 48000.0);
        std::vector<float> l (64), r (64);
        bool ok = true;
        for (int b = 0; b < 2000; ++b) {
            p.loopStart = rng.uni(); p.loopEnd = rng.uni(); p.start = rng.uni();
            if (lm == LoopMode::Off && ! v.active()) v.start (&s, 60.0f, 1.0f, p, 48000.0);
            v.render (l.data(), r.data(), 64, rng.uni() * 140.0f - 20.0f, p);
            for (int i = 0; i < 64; ++i) ok = ok && std::isfinite (l[i]) && std::isfinite (r[i]);
        }
        EL_CHECK (ok);
    }
}

EL_TEST (sampler_oneshot_ends) {
    auto s = makeSine (100, 48000.0, 440.0, 69);
    SamplerVoice v; SamplerParams p;
    v.start (&s, 69.0f, 1.0f, p, 48000.0);
    std::vector<float> l (300, 1.0f), r (300, 1.0f);
    const bool alive = v.render (l.data(), r.data(), 300, 69.0f, p);
    EL_CHECK (! alive); EL_CHECK (! v.active());
    bool zeros = true;
    for (int i = 100; i < 300; ++i) zeros = zeros && l[i] == 0.0f && r[i] == 0.0f;
    EL_CHECK (zeros);
    EL_CHECK (! v.render (l.data(), r.data(), 10, 69.0f, p));
    EL_CHECK (l[0] == 0.0f);
}

EL_TEST (sampler_reverse_starts_at_end) {
    SampleData s; s.sampleRate = 48000; s.rootNote = 60;
    for (int i = 0; i < 100; ++i) s.left.push_back ((float) i);
    SamplerVoice v; SamplerParams p; p.reverse = true;
    v.start (&s, 60.0f, 1.0f, p, 48000.0);
    auto out = renderAll (v, 50, 60.0f, p, 50);
    EL_CHECK_NEAR (out[0], 99.0, 1.0);
    EL_CHECK (out[10] < out[0]);
    EL_CHECK_NEAR (out[10], 89.0, 0.01);
    // mono duplicated
    std::vector<float> l (8), r (8);
    v.start (&s, 60.0f, 1.0f, p, 48000.0);
    v.render (l.data(), r.data(), 8, 60.0f, p);
    EL_CHECK (l == r);
}

namespace {
std::vector<float> runDelay (Delay& d, const DelayParams& p, double bpm, int total, std::vector<float> inL) {
    std::vector<float> out;
    std::vector<float> r;
    inL.resize ((size_t) total, 0.0f);
    for (int i = 0; i < total; i += 256) {
        const int n = std::min (256, total - i);
        std::vector<float> a (inL.begin() + i, inL.begin() + i + n), b = a;
        d.process (a.data(), b.data(), n, p, bpm);
        out.insert (out.end(), a.begin(), a.end());
    }
    return out;
}
}

EL_TEST (delay_impulse_time_and_decay) {
    for (int pp = 0; pp < 2; ++pp) {
        Delay d; d.prepare (48000.0, 256);
        DelayParams p; p.on = true; p.sync = true; p.beats = 0.5f; p.feedback = 0.5f; p.mix = 1.0f; p.pingPong = pp != 0; p.tone = 1.0f;
        auto out = runDelay (d, p, 120.0, 48000 * 6, { 1.0f });
        int first = -1;
        for (size_t i = 1; i < out.size(); ++i) if (std::fabs (out[i]) > 0.1f) { first = (int) i; break; }
        EL_CHECK (first >= 0);
        EL_CHECK_NEAR (first, 12000, 2);
        float peak1 = 0, late = 0;
        for (int i = 11990; i < 12010; ++i) peak1 = std::max (peak1, std::fabs (out[(size_t) i]));
        for (size_t i = 48000 * 5; i < out.size(); ++i) late = std::max (late, std::fabs (out[i]));
        EL_CHECK (peak1 > 0.5f);
        EL_CHECK (late < 0.01f * peak1);
    }
}

EL_TEST (delay_ms_mode_and_time_change_smooth) {
    Delay d; d.prepare (48000.0, 256);
    DelayParams p; p.on = true; p.sync = false; p.ms = 100.0f; p.feedback = 0.9f; p.mix = 0.5f;
    std::vector<float> l (256), r (256);
    Rng rng; rng.seed (3);
    float maxJump = 0.0f, prev = 0.0f; bool ok = true;
    for (int b = 0; b < 400; ++b) {
        for (int i = 0; i < 256; ++i) { l[i] = 0.3f * std::sin (0.05f * (float) (b * 256 + i)); r[i] = l[i]; }
        p.ms = b % 2 ? 100.0f : 1500.0f;
        d.process (l.data(), r.data(), 256, p, 120.0);
        for (int i = 0; i < 256; ++i) { ok = ok && std::isfinite (l[i]) && std::fabs (l[i]) < 20.0f; if (b > 5) maxJump = std::max (maxJump, std::fabs (l[i] - prev)); prev = l[i]; }
    }
    EL_CHECK (ok);
    EL_CHECK (maxJump < 1.0f);
}

EL_TEST (reverb_tail_decays_and_finite) {
    Reverb rv; rv.prepare (48000.0, 512);
    ReverbParams p; p.on = true; p.mix = 1.0f; p.size = 0.6f;
    const int total = 48000 * 10;
    std::vector<float> l (512), r (512);
    double e1 = 0, e2 = 0, e3 = 0; bool ok = true;
    for (int pos = 0; pos < total; pos += 512) {
        std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
        if (pos == 0) { l[0] = 1.0f; r[0] = 1.0f; }
        rv.process (l.data(), r.data(), 512, p);
        for (int i = 0; i < 512; ++i) {
            const int t = pos + i; const double e = (double) l[i] * l[i] + (double) r[i] * r[i];
            ok = ok && std::isfinite (e);
            if (t >= 4800 && t < 48000) e1 += e;
            else if (t >= 48000 * 4 && t < 48000 * 5) e2 += e;
            else if (t >= 48000 * 9) e3 += e;
        }
    }
    EL_CHECK (ok); EL_CHECK (e1 > 0.0); EL_CHECK (e2 < e1); EL_CHECK (e3 < e2 * 0.5);
}

EL_TEST (reverb_noise_10s_finite) {
    for (int sr : { 48000, 192000 }) {
        Reverb rv; rv.prepare ((double) sr, 512);
        ReverbParams p; p.on = true; p.size = 1.0f; p.damping = 0.0f; p.mix = 0.5f; p.predelayMs = 200.0f; p.width = 2.0f;
        Rng rng; rng.seed (11);
        std::vector<float> l (512), r (512); bool ok = true;
        for (int pos = 0; pos < sr * 10; pos += 512) {
            for (int i = 0; i < 512; ++i) { l[i] = rng.bi(); r[i] = rng.bi(); }
            rv.process (l.data(), r.data(), 512, p);
            for (int i = 0; i < 512; ++i) ok = ok && std::isfinite (l[i]) && std::isfinite (r[i]) && std::fabs (l[i]) < 50.0f;
        }
        EL_CHECK (ok);
    }
}

EL_TEST (effects_off_is_transparent_and_reenable_quiet) {
    Rng rng; rng.seed (5);
    Chorus c; Delay d; Reverb rv;
    c.prepare (48000.0, 256); d.prepare (48000.0, 256); rv.prepare (48000.0, 256);
    ChorusParams cp; DelayParams dp; ReverbParams rp;   // all on=false
    std::vector<float> l (256), r (256);
    for (int i = 0; i < 256; ++i) { l[i] = rng.bi(); r[i] = rng.bi(); }
    auto l0 = l, r0 = r;
    c.process (l.data(), r.data(), 256, cp); d.process (l.data(), r.data(), 256, dp, 120.0); rv.process (l.data(), r.data(), 256, rp);
    EL_CHECK (l == l0 && r == r0);

    cp.on = dp.on = rp.on = true; dp.mix = rp.mix = cp.mix = 1.0f; dp.feedback = 0.9f;
    for (int b = 0; b < 100; ++b) {
        for (int i = 0; i < 256; ++i) { l[i] = rng.bi(); r[i] = rng.bi(); }
        c.process (l.data(), r.data(), 256, cp); d.process (l.data(), r.data(), 256, dp, 120.0); rv.process (l.data(), r.data(), 256, rp);
    }
    cp.on = dp.on = rp.on = false;
    c.process (l.data(), r.data(), 256, cp); d.process (l.data(), r.data(), 256, dp, 120.0); rv.process (l.data(), r.data(), 256, rp);
    cp.on = dp.on = rp.on = true;
    std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
    c.process (l.data(), r.data(), 256, cp); d.process (l.data(), r.data(), 256, dp, 120.0); rv.process (l.data(), r.data(), 256, rp);
    float m = 0; for (int i = 0; i < 256; ++i) m = std::max ({ m, std::fabs (l[i]), std::fabs (r[i]) });
    EL_CHECK (m < 1e-6f);
}

EL_TEST (effects_random_params_no_nan) {
    Rng rng; rng.seed (99);
    Chorus c; Delay d; Reverb rv;
    c.prepare (44100.0, 512); d.prepare (44100.0, 512); rv.prepare (44100.0, 512);
    std::vector<float> l (512), r (512); bool ok = true;
    for (int b = 0; b < 1500; ++b) {
        ChorusParams cp; cp.on = rng.uni() > 0.1f; cp.rate = rng.uni() * 5.5f; cp.depth = rng.uni(); cp.mix = rng.uni();
        DelayParams dp; dp.on = rng.uni() > 0.1f; dp.sync = rng.uni() > 0.5f; dp.beats = rng.uni() * 4.0f; dp.ms = rng.uni() * 2000.0f;
        dp.feedback = rng.uni(); dp.mix = rng.uni(); dp.tone = rng.uni(); dp.pingPong = rng.uni() > 0.5f;
        ReverbParams rp; rp.on = rng.uni() > 0.1f; rp.size = rng.uni(); rp.damping = rng.uni(); rp.mix = rng.uni();
        rp.predelayMs = rng.uni() * 200.0f; rp.width = rng.uni() * 1.5f;
        const int n = 1 + (int) (rng.uni() * 511.0f);
        for (int i = 0; i < n; ++i) { l[i] = rng.bi(); r[i] = rng.bi(); }
        c.process (l.data(), r.data(), n, cp); d.process (l.data(), r.data(), n, dp, 40.0 + rng.uni() * 200.0); rv.process (l.data(), r.data(), n, rp);
        for (int i = 0; i < n; ++i) ok = ok && std::isfinite (l[i]) && std::isfinite (r[i]) && std::fabs (l[i]) < 100.0f;
    }
    EL_CHECK (ok);
}

EL_TEST (chorus_changes_signal_and_bounded) {
    Chorus c; c.prepare (48000.0, 256);
    ChorusParams p; p.on = true; p.mix = 0.5f;
    std::vector<float> l (4800), r;
    for (int i = 0; i < 4800; ++i) l[(size_t) i] = 0.5f * std::sin (0.1f * (float) i);
    r = l; auto l0 = l;
    c.process (l.data(), r.data(), 4800, p);
    EL_CHECK (l != l0); EL_CHECK (l != r); EL_CHECK (finite (l));
}
