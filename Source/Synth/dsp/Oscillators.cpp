// Chimera oscillators: Analog (PolyBLEP), Wavetable (band-limited mips), Pluck (Karplus-Strong).
#include "Synth/dsp/Oscillators.h"
#include <algorithm>
#include <complex>

namespace el::chimera {

namespace {

// ---------------------------------------------------------------- PolyBLEP helpers
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt) { const float x = t / dt; return x + x - x * x - 1.0f; }
    if (t > 1.0f - dt) { const float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
    return 0.0f;
}
// Residual for the sample at fractional time x (0..1) after a unit-height (step = 2) edge.
inline float blepPost (float x) noexcept { return x + x - x * x - 1.0f; }
inline float wrap01 (float x) noexcept { return x - std::floor (x); }

struct SyncEvent { bool on = false; float f = 0.0f, pBefore = 0.0f; };

inline float triAt (float ph) noexcept { return ph < 0.5f ? -1.0f + 4.0f * ph : 3.0f - 4.0f * ph; }

// One sample of a band-limited waveform. ph is the phase AFTER advancing (and after a sync
// reset). For a sync event the discontinuity is corrected one-sided (post-edge residual).
inline float tickWave (Wave w, float ph, float dt, float pw, float& tri, Rng& rng, const SyncEvent& s) noexcept
{
    switch (w) {
    case Wave::Sine: {
        float v = std::sin (kTwoPi * ph);
        if (s.on) v += 0.5f * (0.0f - std::sin (kTwoPi * s.pBefore)) * blepPost (s.f);
        return v; }
    case Wave::Saw: {
        float v = 2.0f * ph - 1.0f - polyBlep (ph, dt);
        if (s.on) v += (1.0f - s.pBefore) * blepPost (s.f);   // natural term assumed a -2 step
        return v; }
    case Wave::Square: {
        float v = (ph < pw ? 1.0f : -1.0f) + polyBlep (ph, dt) - polyBlep (wrap01 (ph - pw + 1.0f), dt);
        if (s.on) v += ((s.pBefore < pw ? 0.0f : 1.0f) - 1.0f) * blepPost (s.f);   // (d/2 - 1), d = 0 or 2
        return v; }
    case Wave::Triangle: {
        float sq = (ph < 0.5f ? 1.0f : -1.0f) + polyBlep (ph, dt) - polyBlep (wrap01 (ph + 0.5f), dt);
        if (s.on) sq += ((s.pBefore < 0.5f ? 0.0f : 1.0f) - 1.0f) * blepPost (s.f);
        tri = tri * (1.0f - 0.05f * dt) + 4.0f * dt * sq;
        return tri; }
    case Wave::Noise: return rng.bi();
    }
    return 0.0f;
}
} // namespace

// =================================================================== AnalogOsc
void AnalogOsc::reset (float sampleRate, uint32_t seed, float startPhase) noexcept
{
    sr = sampleRate > 1000.0f ? sampleRate : 48000.0f;
    ph1 = ph2 = wrap01 (startPhase);
    phSub = 0.0f;
    triState1 = triState2 = triAt (ph1);
    rng.seed (seed);
}

void AnalogOsc::render (float* out, int n, float hz, const AnalogParams& p, float pwMod) noexcept
{
    const float dt1 = clampv (hz / sr, 1.0e-6f, 0.49f);
    const float ratio = semisToRatio (p.osc2Semi + p.osc2Fine * 0.01f);
    const float dt2 = clampv (dt1 * ratio, 1.0e-6f, 0.49f);
    const float pw1 = clampv (p.pw1 + pwMod, 0.05f, 0.95f), pw2 = clampv (p.pw2 + pwMod, 0.05f, 0.95f);
    const float mixA = std::cos (clamp01 (p.mix) * kPi * 0.5f), mixB = std::sin (clamp01 (p.mix) * kPi * 0.5f);
    const float subG = 0.5f * clamp01 (p.sub), noiseG = 0.5f * clamp01 (p.noise);
    const SyncEvent none;

    for (int i = 0; i < n; ++i) {
        // osc 1
        ph1 += dt1;
        bool wrapped = false;
        float f = 0.0f;
        if (ph1 >= 1.0f) { ph1 -= 1.0f; wrapped = true; f = std::min (ph1 / dt1, 0.9999f); phSub = phSub > 0.25f ? 0.0f : 0.5f; }
        // osc 2 (hard sync resets it at osc 1's wrap, sub-sample accurate)
        SyncEvent ev;
        const float p2old = ph2;
        ph2 += dt2;
        if (p.sync && wrapped) {
            ev.on = true; ev.f = f;
            ev.pBefore = wrap01 (p2old + dt2 * (1.0f - f));
            ph2 = f * dt2;
        } else if (ph2 >= 1.0f) ph2 -= 1.0f;

        const float s1 = tickWave (p.wave1, ph1, dt1, pw1, triState1, rng, none);
        const float s2 = tickWave (p.wave2, ph2, dt2, pw2, triState2, rng, ev);
        float v = mixA * s1 + mixB * s2;

        if (subG > 0.0f) {
            float sp = 0.5f * ph1 + phSub;
            if (sp >= 1.0f) sp -= 1.0f;
            const float sdt = 0.5f * dt1;
            const float sq = (sp < 0.5f ? 1.0f : -1.0f) + polyBlep (sp, sdt) - polyBlep (wrap01 (sp + 0.5f), sdt);
            v += subG * sq;
        }
        if (noiseG > 0.0f) v += noiseG * rng.bi();
        out[i] = v;
    }
}

// =================================================================== WavetableBank
namespace {
using cd = std::complex<double>;

void fft (std::vector<cd>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = (inverse ? 2.0 : -2.0) * 3.14159265358979323846 / (double) len;
        const cd wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) {
            cd w (1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j) {
                const cd u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

constexpr int N = WavetableBank::kFrameSize;
constexpr int H = N / 2;   // 1024 harmonics

// x[n] = sum amp[h] * sin(2 pi h n / N), h = 1..H-1, via one inverse FFT.
void fromHarmonics (const std::vector<double>& amp, std::vector<double>& x)
{
    std::vector<cd> S ((size_t) N, cd (0, 0));
    for (int h = 1; h < H; ++h) {
        S[(size_t) h] = cd (0.0, -0.5 * amp[(size_t) h]);
        S[(size_t) (N - h)] = std::conj (S[(size_t) h]);
    }
    fft (S, true);
    x.resize ((size_t) N);
    for (int i = 0; i < N; ++i) x[(size_t) i] = S[(size_t) i].real();
}

double lerpd (double a, double b, double t) { return a + (b - a) * t; }

double shapeAmp (int shape, int h)
{
    const double pi = 3.14159265358979323846;
    switch (shape) {
        case 0: return h == 1 ? 1.0 : 0.0;
        case 1: return (h & 1) ? (8.0 / (pi * pi)) / ((double) h * h) * (((h - 1) / 2) & 1 ? -1.0 : 1.0) : 0.0;
        case 2: return (2.0 / pi) / (double) h;
        default: return (h & 1) ? (4.0 / pi) / (double) h : 0.0;
    }
}

void genFrame (int table, int f, std::vector<double>& x)
{
    const double t = (double) f / (double) (WavetableBank::kFrames - 1);
    const double pi = 3.14159265358979323846;
    std::vector<double> amp ((size_t) H + 1, 0.0);
    x.assign ((size_t) N, 0.0);
    switch (table) {
    case 0: {   // Basic Shapes
        const double s = t * 3.0;
        const int i = std::min (2, (int) s);
        const double fr = s - i;
        for (int h = 1; h < H; ++h) amp[(size_t) h] = lerpd (shapeAmp (i, h), shapeAmp (i + 1, h), fr);
        fromHarmonics (amp, x);
        break; }
    case 1:     // PWM
        for (int n = 0; n < N; ++n) x[(size_t) n] = ((double) n / N < 0.5 - 0.45 * t) ? 1.0 : -1.0;
        break;
    case 2: {   // Harmonics sweep
        const double c = 1.0 + t * 30.0;
        for (int h = 1; h < H; ++h) amp[(size_t) h] = std::min(1.0, std::max(0.0, c - h + 1.0)) / (double) h;
        fromHarmonics (amp, x);
        break; }
    case 3: {   // Formant
        static const double F[5][3] = { { 800, 1150, 2800 }, { 400, 1600, 2700 }, { 350, 2300, 3000 },
                                        { 450, 800, 2830 }, { 325, 700, 2530 } };
        const double s = t * 4.0;
        const int i = std::min (3, (int) s);
        const double fr = s - i;
        const double wts[3] = { 1.0, 0.6, 0.3 };
        for (int h = 1; h < H; ++h) {
            double a = 0.01 / h;
            for (int j = 0; j < 3; ++j) {
                const double fc = lerpd (F[i][j], F[i + 1][j], fr), bw = 80.0 + 0.1 * fc;
                const double d = ((double) h * 100.0 - fc) / bw;
                a += wts[j] * std::exp (-0.5 * d * d);
            }
            amp[(size_t) h] = a;
        }
        fromHarmonics (amp, x);
        break; }
    case 4: {   // Digital: phase-modulated sine, bit-reduced
        const double L = 4.0 + 2.0 * (WavetableBank::kFrames - 1 - f);
        for (int n = 0; n < N; ++n) {
            const double th = 2.0 * pi * n / N;
            const double v = std::sin (th + t * 3.0 * std::sin (2.0 * th));
            x[(size_t) n] = std::round (v * L) / L;
        }
        break; }
    case 5: {   // Organ drawbars
        static const int hs[8] = { 1, 2, 3, 4, 6, 8, 10, 12 };
        for (int i = 0; i < 8; ++i)
            amp[(size_t) hs[i]] = 0.5 + 0.5 * std::sin (2.0 * pi * (t * 0.5 * (i + 1) + i * 0.37));
        amp[1] = std::max (amp[1], 0.3);
        fromHarmonics (amp, x);
        break; }
    case 6: {   // Bell
        static const int hs[12] = { 1, 2, 3, 4, 5, 6, 8, 9, 11, 13, 16, 19 };
        for (int i = 0; i < 12; ++i)
            amp[(size_t) hs[i]] = std::exp (-0.12 * i) * (0.65 + 0.35 * std::sin (2.0 * pi * (t * (1 + i % 3) + i * 0.21)));
        fromHarmonics (amp, x);
        break; }
    default: {  // Sync Sweep
        const double r = 1.0 + 7.0 * t;
        for (int n = 0; n < N; ++n) {
            const double v = r * n / N;
            x[(size_t) n] = 2.0 * (v - std::floor (v)) - 1.0;
        }
        break; }
    }
}
} // namespace

static const char* const kTableNames[8] = { "Basic Shapes", "PWM", "Harmonics", "Formant", "Digital", "Organ", "Bell", "Sync Sweep" };

WavetableBank::WavetableBank()
{
    tables = 8;
    data.assign ((size_t) tables * kFrames * kMipLevels * kFrameSize, 0.0f);
    std::vector<double> x;
    std::vector<cd> X ((size_t) N), Y ((size_t) N);
    for (int t = 0; t < tables; ++t)
        for (int f = 0; f < kFrames; ++f) {
            genFrame (t, f, x);
            for (int i = 0; i < N; ++i) X[(size_t) i] = cd (x[(size_t) i], 0.0);
            fft (X, false);
            double gain0 = 1.0;
            for (int m = 0; m < kMipLevels; ++m) {
                const int K = std::max (1, (N / 2) >> m);
                std::fill (Y.begin(), Y.end(), cd (0, 0));
                for (int k = 1; k <= K; ++k) {
                    Y[(size_t) k] = X[(size_t) k];
                    if (k < N / 2) Y[(size_t) (N - k)] = X[(size_t) (N - k)];
                }
                fft (Y, true);
                float* dst = data.data() + (((size_t) t * kFrames + f) * kMipLevels + m) * kFrameSize;
                double peak = 0.0;
                for (int i = 0; i < N; ++i) peak = std::max (peak, std::abs (Y[(size_t) i].real()));
                if (m == 0) {
                    if (peak < 1.0e-9) {   // degenerate frame: fall back to a sine
                        for (int i = 0; i < N; ++i) dst[i] = (float) std::sin (2.0 * 3.14159265358979323846 * i / N);
                        gain0 = 0.0;
                        continue;
                    }
                    gain0 = 1.0 / peak;
                }
                if (gain0 == 0.0) { for (int i = 0; i < N; ++i) dst[i] = (float) std::sin (2.0 * 3.14159265358979323846 * i / N * 1.0); continue; }
                double g = gain0;
                if (peak * g > 1.0) g = 1.0 / peak;
                for (int i = 0; i < N; ++i) dst[i] = (float) (Y[(size_t) i].real() * g);
            }
        }
}

const WavetableBank& WavetableBank::get()
{
    static const WavetableBank bank;
    return bank;
}

int WavetableBank::numTables() const noexcept { return tables; }
const char* WavetableBank::tableName (int index) const noexcept
{
    return kTableNames[clampv (index, 0, 7)];
}
const float* WavetableBank::frame (int table, int fr, int mip) const noexcept
{
    table = clampv (table, 0, tables - 1);
    fr = clampv (fr, 0, kFrames - 1);
    mip = clampv (mip, 0, kMipLevels - 1);
    return data.data() + (((size_t) table * kFrames + fr) * kMipLevels + mip) * kFrameSize;
}

// =================================================================== WavetableOsc
void WavetableOsc::reset (float sampleRate, float startPhase) noexcept
{
    sr = sampleRate > 1000.0f ? sampleRate : 48000.0f;
    phase = wrap01 (startPhase);
}

void WavetableOsc::render (float* out, int n, float hz, int table, float position) noexcept
{
    const WavetableBank& bank = WavetableBank::get();
    constexpr int M = WavetableBank::kFrameSize - 1;
    const float fp = clamp01 (position) * (float) (WavetableBank::kFrames - 1);
    const int f0 = std::min ((int) fp, WavetableBank::kFrames - 2);
    const float fr = fp - (float) f0;
    hz = clampv (hz, 1.0e-3f, 0.49f * sr);
    const float maxHarm = 0.45f * sr / hz;
    int mip = 0;
    while (mip < WavetableBank::kMipLevels - 1 && (float) ((WavetableBank::kFrameSize / 2) >> mip) > maxHarm) ++mip;
    const float* A = bank.frame (table, f0, mip);
    const float* B = bank.frame (table, f0 + 1, mip);
    const float inc = hz / sr;
    for (int i = 0; i < n; ++i) {
        const float idx = phase * (float) WavetableBank::kFrameSize;
        const int i0 = (int) idx & M;
        const int i1 = (i0 + 1) & M;
        const float fx = idx - std::floor (idx);
        const float a = A[i0] + (A[i1] - A[i0]) * fx;
        const float b = B[i0] + (B[i1] - B[i0]) * fx;
        out[i] = a + (b - a) * fr;
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
    }
}

// =================================================================== PluckVoice
namespace {
struct PluckCalc { int L; float eta, g, dc; };

PluckCalc pluckCalc (float sr, float hz, const PluckParams& p, int lineSize) noexcept
{
    hz = clampv (hz, 20.0f, 0.45f * sr);
    const float P = sr / hz;
    const float lpMax = std::max (0.0f, (P - 3.0f) * 0.5f);
    const float dMax = lpMax / (1.0f + lpMax);
    const float dc = std::min (clamp01 (p.damping) * 0.7f, dMax);
    const float D = P - dc / (1.0f - dc);
    int L = (int) std::floor (D - 0.5f);
    L = clampv (L, 1, lineSize - 2);
    const float frac = clampv (D - (float) L, 0.1f, 2.0f);
    PluckCalc c;
    c.L = L; c.dc = dc;
    c.eta = (1.0f - frac) / (1.0f + frac);
    const float T = 0.2f * std::pow (50.0f, clamp01 (p.decay));
    c.g = std::min (std::pow (0.001f, P / (T * sr)), 0.9999f);
    return c;
}
}

void PluckVoice::prepare (float sampleRate)
{
    sr = sampleRate > 1000.0f ? sampleRate : 48000.0f;
    line.assign ((size_t) std::ceil (sr / 20.0f) + 16, 0.0f);
    writePos = 0; lp = ap = apIn = 0.0f;
}

void PluckVoice::start (float hz, float velocity, const PluckParams& p, uint32_t seed) noexcept
{
    const int N = (int) line.size();
    if (N < 32) return;
    const PluckCalc c = pluckCalc (sr, hz, p, N);
    std::fill (line.begin(), line.end(), 0.0f);
    rng.seed (seed);
    const float as = 0.92f * (1.0f - clamp01 (p.noiseColor));
    float y = 0.0f, mean = 0.0f;
    for (int i = 0; i < c.L; ++i) {
        y = (1.0f - as) * rng.bi() + as * y;
        line[(size_t) i] = y;
        mean += y;
    }
    mean /= (float) c.L;
    for (int i = 0; i < c.L; ++i) line[(size_t) i] -= mean;
    const int D = (int) std::lround (clampv (p.pickPos, 0.0f, 0.5f) * (float) c.L);
    if (D >= 1 && D < c.L)
        for (int i = c.L - 1; i >= D; --i) line[(size_t) i] -= line[(size_t) (i - D)];
    float peak = 0.0f;
    for (int i = 0; i < c.L; ++i) peak = std::max (peak, std::fabs (line[(size_t) i]));
    const float sc = peak > 1.0e-9f ? clamp01 (velocity) / peak : 0.0f;
    for (int i = 0; i < c.L; ++i) line[(size_t) i] *= sc;
    writePos = c.L % N;
    lp = ap = apIn = 0.0f;
}

void PluckVoice::render (float* out, int n, float hz, const PluckParams& p) noexcept
{
    const int N = (int) line.size();
    if (N < 32) { for (int i = 0; i < n; ++i) out[i] = 0.0f; return; }
    const PluckCalc c = pluckCalc (sr, hz, p, N);
    float* buf = line.data();
    int rp = writePos - c.L;
    if (rp < 0) rp += N;
    for (int i = 0; i < n; ++i) {
        const float x = buf[rp];
        if (++rp >= N) rp = 0;
        lp = (1.0f - c.dc) * x + c.dc * lp;
        const float ya = c.eta * lp + apIn - c.eta * ap;
        apIn = lp; ap = ya;
        float v = c.g * ya;
        if (std::fabs (v) < 1.0e-20f) v = 0.0f;
        buf[writePos] = v;
        if (++writePos >= N) writePos = 0;
        out[i] = ya;
    }
}

} // namespace el::chimera
