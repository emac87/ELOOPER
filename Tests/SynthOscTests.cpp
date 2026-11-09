// Tests for Chimera oscillators and filters.
#include "TestFramework.h"
#include "Synth/dsp/Oscillators.h"
#include "Synth/dsp/Filters.h"
#include <algorithm>

using namespace el::chimera;

namespace {
constexpr float SR = 48000.0f;

int upCrossings (const std::vector<float>& x)
{
    int c = 0;
    for (size_t i = 1; i < x.size(); ++i) if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++c;
    return c;
}
float peakOf (const std::vector<float>& x) { float p = 0; for (float v : x) p = std::max (p, std::fabs (v)); return p; }
bool allFinite (const std::vector<float>& x) { for (float v : x) if (! std::isfinite (v)) return false; return true; }
double rms (const float* x, size_t n) { double s = 0; for (size_t i = 0; i < n; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / (double) n); }

// Power (amplitude^2 / 2) of the sinusoidal component at freq.
double tonePower (const std::vector<float>& x, double freq)
{
    double re = 0, im = 0;
    const double w = 2.0 * 3.14159265358979323846 * freq / SR;
    for (size_t i = 0; i < x.size(); ++i) { re += x[i] * std::cos (w * (double) i); im += x[i] * std::sin (w * (double) i); }
    const double n = (double) x.size();
    return 2.0 * (re * re + im * im) / (n * n);
}

std::vector<float> renderAnalog (Wave w, float hz, int n, AnalogParams p = {})
{
    p.wave1 = w;
    AnalogOsc o; o.reset (SR, 1234, 0.0f);
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; i += 256) o.render (x.data() + i, std::min (256, n - i), hz, p, 0.0f);
    return x;
}

// Fraction of power sitting on non-harmonic 1 kHz bins (5 kHz fundamental at 48 kHz => alias bins).
double aliasRatio (const std::vector<float>& x)
{
    double alias = 0, total = 0;
    for (float v : x) total += (double) v * v;
    total /= (double) x.size();
    for (int k = 1; k <= 23; ++k)
        if (k % 5 != 0) alias += tonePower (x, k * 1000.0);
    return alias / total;
}
}

EL_TEST (analog_waves_frequency_and_level)
{
    for (Wave w : { Wave::Sine, Wave::Triangle, Wave::Saw, Wave::Square }) {
        auto x = renderAnalog (w, 440.0f, 48000);
        EL_CHECK (allFinite (x));
        EL_CHECK (peakOf (x) <= 1.5f);
        EL_CHECK (peakOf (x) > 0.7f);
        const int c = upCrossings (x);
        EL_CHECK (std::abs (c - 440) <= 2);
    }
    auto nz = renderAnalog (Wave::Noise, 440.0f, 48000);
    EL_CHECK (peakOf (nz) <= 1.5f && rms (nz.data(), nz.size()) > 0.3);
}

EL_TEST (analog_mix_sub_noise_bounded)
{
    AnalogParams p; p.mix = 0.5f; p.sub = 1.0f; p.noise = 0.5f; p.osc2Semi = 7.0f; p.osc2Fine = 12.0f;
    p.wave2 = Wave::Square; p.sync = true;
    auto x = renderAnalog (Wave::Saw, 220.0f, 48000, p);
    EL_CHECK (allFinite (x));
    EL_CHECK (peakOf (x) < 3.0f);
    // sub is one octave down: power at 110 Hz present with sub on, absent without
    AnalogParams q; q.sub = 1.0f;
    auto a = renderAnalog (Wave::Sine, 220.0f, 48000, q);
    EL_CHECK (tonePower (a, 110.0) > 0.05);
    EL_CHECK (tonePower (renderAnalog (Wave::Sine, 220.0f, 48000), 110.0) < 1e-4);
}

EL_TEST (analog_saw_aliasing_vs_naive)
{
    auto x = renderAnalog (Wave::Saw, 5000.0f, 48000);
    std::vector<float> naive (48000);
    float ph = 0;
    for (auto& v : naive) { v = 2.0f * ph - 1.0f; ph += 5000.0f / SR; if (ph >= 1.0f) ph -= 1.0f; }
    const double a = aliasRatio (x), b = aliasRatio (naive);
    std::printf ("  alias ratio polyblep %.5f naive %.5f\n", a, b);
    EL_CHECK (a < 0.5 * b);
    EL_CHECK (a < 0.05);
}

EL_TEST (analog_hard_sync_locks_to_osc1)
{
    AnalogParams p; p.mix = 1.0f; p.wave2 = Wave::Saw; p.osc2Semi = 7.0f; p.sync = true;
    auto x = renderAnalog (Wave::Saw, 440.0f, 48000, p);
    EL_CHECK (allFinite (x) && peakOf (x) <= 1.5f);
    const double p440 = tonePower (x, 440.0), p660 = tonePower (x, 660.0);
    EL_CHECK (p660 < 0.02 * p440);
    p.sync = false;
    auto y = renderAnalog (Wave::Saw, 440.0f, 48000, p);
    EL_CHECK (tonePower (y, 440.0 * std::pow (2.0, 7.0 / 12.0)) > 0.05);
}

EL_TEST (wavetable_tables_finite_and_peak)
{
    const auto& b = WavetableBank::get();
    EL_CHECK (b.numTables() == 8);
    for (int t = 0; t < b.numTables(); ++t) {
        EL_CHECK (b.tableName (t) != nullptr && b.tableName (t)[0] != 0);
        for (int f = 0; f < WavetableBank::kFrames; ++f)
            for (int m = 0; m < WavetableBank::kMipLevels; ++m) {
                const float* d = b.frame (t, f, m);
                bool fin = true; float pk = 0; double dc = 0;
                for (int i = 0; i < WavetableBank::kFrameSize; ++i) { fin = fin && std::isfinite (d[i]); pk = std::max (pk, std::fabs (d[i])); dc += d[i]; }
                EL_CHECK (fin);
                EL_CHECK (pk <= 1.001f);
                if (m == 0) EL_CHECK (pk >= 0.99f);
                EL_CHECK (std::fabs (dc / WavetableBank::kFrameSize) < 0.01);
            }
    }
}

EL_TEST (wavetable_frequency_and_morph)
{
    WavetableOsc o; o.reset (SR, 0.0f);
    std::vector<float> x (48000);
    for (int i = 0; i < 48000; i += 128) o.render (x.data() + i, 128, 440.0f, 0, 0.1f);
    EL_CHECK (allFinite (x));
    EL_CHECK (std::abs (upCrossings (x) - 440) <= 2);

    std::vector<float> a (48000), c (48000);
    WavetableOsc o0, o1; o0.reset (SR, 0.0f); o1.reset (SR, 0.0f);
    o0.render (a.data(), 48000, 440.0f, 0, 0.0f);
    o1.render (c.data(), 48000, 440.0f, 0, 1.0f);
    // sine has no 3rd harmonic, square does
    EL_CHECK (tonePower (a, 1320.0) < 1e-4);
    EL_CHECK (tonePower (c, 1320.0) > 0.01);
    EL_CHECK (peakOf (a) <= 1.001f && peakOf (c) <= 1.001f);
}

EL_TEST (wavetable_band_limited_at_high_pitch)
{
    // 8 kHz saw-ish table (frame 10 of Basic Shapes ~ saw): nothing above fundamental's harmonics folds back
    WavetableOsc o; o.reset (SR, 0.0f);
    std::vector<float> x (48000);
    o.render (x.data(), 48000, 5000.0f, 0, 2.0f / 3.0f);
    EL_CHECK (allFinite (x));
    EL_CHECK (aliasRatio (x) < 0.01);
}

EL_TEST (pluck_pitch_and_decay)
{
    for (float hz : { 220.0f, 440.0f }) {
        PluckVoice v; v.prepare (SR);
        PluckParams p;
        v.start (hz, 1.0f, p, 77);
        std::vector<float> x (48000 * 3);
        for (size_t i = 0; i < x.size(); i += 64) v.render (x.data() + i, 64, hz, p);
        EL_CHECK (allFinite (x));
        // autocorrelation on a window after the attack
        const int start = 4800, len = 8192;
        const double target = SR / hz;
        const int lo = (int) (target * 0.8), hi = (int) (target * 1.2);
        std::vector<double> r ((size_t) hi + 2, 0.0);
        for (int lag = lo - 1; lag <= hi + 1; ++lag) {
            double s = 0;
            for (int i = 0; i < len; ++i) s += (double) x[(size_t) (start + i)] * x[(size_t) (start + i + lag)];
            r[(size_t) lag] = s;
        }
        int best = lo;
        for (int lag = lo; lag <= hi; ++lag) if (r[(size_t) lag] > r[(size_t) best]) best = lag;
        const double y0 = r[(size_t) best - 1], y1 = r[(size_t) best], y2 = r[(size_t) best + 1];
        const double off = 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
        const double est = SR / ((double) best + off);
        const double cents = 1200.0 * std::log2 (est / hz);
        std::printf ("  pluck %.0f Hz -> %.3f Hz (%.2f cents)\n", hz, est, cents);
        EL_CHECK (std::fabs (cents) < 5.0);
        const double early = rms (x.data() + 4800, 4800), late = rms (x.data() + 48000 * 3 - 4800, 4800);
        EL_CHECK (early > 0.01);
        EL_CHECK (late < 0.2 * early);
    }
}

EL_TEST (pluck_velocity_and_params)
{
    PluckVoice v; v.prepare (SR);
    PluckParams p;
    std::vector<float> a (9600), b (9600);
    v.start (330.0f, 1.0f, p, 5);  v.render (a.data(), 9600, 330.0f, p);
    v.start (330.0f, 0.25f, p, 5); v.render (b.data(), 9600, 330.0f, p);
    EL_CHECK (peakOf (b) < 0.5f * peakOf (a));
    for (float hz : { 20.0f, 55.0f, 1000.0f, 4000.0f }) {
        p.damping = 1.0f; p.decay = 1.0f; p.pickPos = 0.5f; p.noiseColor = 0.0f;
        v.start (hz, 1.0f, p, 9);
        std::vector<float> x (24000);
        v.render (x.data(), 24000, hz, p);
        EL_CHECK (allFinite (x) && peakOf (x) < 2.0f);
    }
}

namespace {
double filterGainDb (FilterType t, float cut, float res, float freq)
{
    Filter f; f.prepare (SR);
    const int n = 48000;
    std::vector<float> x ((size_t) n), in ((size_t) n);
    for (int i = 0; i < n; ++i) in[(size_t) i] = x[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979323846 * freq * i / SR);
    for (int i = 0; i < n; i += 32) { f.set (t, cut, res, 0.0f); f.process (x.data() + i, 32); }
    return 20.0 * std::log10 (rms (x.data() + 24000, 24000) / rms (in.data() + 24000, 24000));
}
}

EL_TEST (filter_lowpass24)
{
    const double hi = filterGainDb (FilterType::LowPass24, 1000.0f, 0.0f, 8000.0f);
    const double lo = filterGainDb (FilterType::LowPass24, 1000.0f, 0.0f, 200.0f);
    std::printf ("  LP24: 8k %.1f dB, 200 Hz %.2f dB\n", hi, lo);
    EL_CHECK (hi < -30.0);
    EL_CHECK (std::fabs (lo) < 1.0);
    EL_CHECK (filterGainDb (FilterType::LowPass12, 1000.0f, 0.0f, 8000.0f) < -20.0);
}

EL_TEST (filter_highpass_bandpass_notch)
{
    EL_CHECK (std::fabs (filterGainDb (FilterType::HighPass24, 1000.0f, 0.0f, 8000.0f)) < 1.0);
    EL_CHECK (filterGainDb (FilterType::HighPass24, 1000.0f, 0.0f, 100.0f) < -30.0);
    EL_CHECK (std::fabs (filterGainDb (FilterType::HighPass12, 1000.0f, 0.0f, 8000.0f)) < 1.0);
    EL_CHECK (filterGainDb (FilterType::HighPass12, 1000.0f, 0.0f, 100.0f) < -25.0);
    EL_CHECK (filterGainDb (FilterType::BandPass, 1000.0f, 0.5f, 1000.0f) > -3.0);
    EL_CHECK (filterGainDb (FilterType::BandPass, 1000.0f, 0.5f, 100.0f) < -15.0);
    EL_CHECK (filterGainDb (FilterType::Notch, 1000.0f, 0.5f, 1000.0f) < -20.0);
    EL_CHECK (std::fabs (filterGainDb (FilterType::Notch, 1000.0f, 0.5f, 100.0f)) < 1.0);
    EL_CHECK (std::fabs (filterGainDb (FilterType::Off, 1000.0f, 0.5f, 100.0f)) < 0.01);
    EL_CHECK (filterGainDb (FilterType::Ladder, 1000.0f, 0.0f, 8000.0f) < -30.0);
    EL_CHECK (std::fabs (filterGainDb (FilterType::Ladder, 1000.0f, 0.0f, 100.0f)) < 1.5);
}

EL_TEST (filter_comb_rings_at_pitch)
{
    EL_CHECK (filterGainDb (FilterType::Comb, 1000.0f, 0.9f, 1000.0f) > 6.0);
    EL_CHECK (filterGainDb (FilterType::Comb, 1000.0f, 0.9f, 1500.0f) < filterGainDb (FilterType::Comb, 1000.0f, 0.9f, 1000.0f) - 6.0);
}

EL_TEST (filter_names)
{
    EL_CHECK (std::string (filterTypeName (FilterType::LowPass24)) == "LP 24");
    EL_CHECK (std::string (filterTypeName (FilterType::Ladder)) == "Ladder");
    for (int i = 0; i < kNumFilterTypes; ++i) EL_CHECK (filterTypeName ((FilterType) i)[0] != 0);
}

EL_TEST (ladder_resonance_one_stays_finite)
{
    Filter f; f.prepare (SR);
    Rng rng; rng.seed (99);
    std::vector<float> x (32);
    float pk = 0; bool fin = true;
    for (int blk = 0; blk < 48000 * 10 / 32; ++blk) {
        for (auto& v : x) v = rng.bi();
        f.set (FilterType::Ladder, 300.0f + 4000.0f * (float) (blk % 200) / 200.0f, 1.0f, 0.5f);
        f.process (x.data(), 32);
        for (float v : x) { fin = fin && std::isfinite (v); pk = std::max (pk, std::fabs (v)); }
    }
    EL_CHECK (fin);
    EL_CHECK (pk < 50.0f);
}

EL_TEST (filter_random_changes_never_blow_up)
{
    for (uint32_t seed : { 1u, 2u, 3u }) {
        Filter f; f.prepare (SR);
        Rng rng; rng.seed (seed);
        std::vector<float> x (32);
        float pk = 0; bool fin = true;
        for (int blk = 0; blk < 48000 * 6 / 32; ++blk) {
            for (auto& v : x) v = rng.bi();
            const FilterType t = (FilterType) (rng.nextU() % (uint32_t) kNumFilterTypes);
            const float cut = 20.0f * std::pow (1000.0f, rng.uni());
            f.set (t, cut, rng.uni(), rng.uni());
            f.process (x.data(), 32);
            for (float v : x) { fin = fin && std::isfinite (v); pk = std::max (pk, std::fabs (v)); }
        }
        EL_CHECK (fin);
        EL_CHECK (pk <= 50.0f);
        std::printf ("  random filter peak %.2f\n", pk);
    }
}
