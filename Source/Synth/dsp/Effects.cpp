#include "Synth/dsp/Effects.h"
#include <algorithm>

namespace el::chimera {

namespace {
inline float flush (float x) noexcept { return std::fabs (x) < 1e-20f ? 0.0f : x; }
inline float smoothCoef (double sr, double seconds) noexcept { return (float) (1.0 - std::exp (-1.0 / (seconds * sr))); }
inline float readLin (const float* b, int N, int w, double d) noexcept {
    double rp = (double) w - d;
    if (rp < 0.0) rp += (double) N;
    int i0 = (int) rp;
    if (i0 >= N) i0 -= N;
    if (i0 < 0) i0 = 0;
    const float t = (float) (rp - (double) i0);
    const int i1 = i0 + 1 >= N ? 0 : i0 + 1;
    return b[i0] + t * (b[i1] - b[i0]);
}
}

// ------------------------------------------------------------------ Chorus
void Chorus::prepare (double sampleRate, int) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const size_t N = (size_t) (0.035 * sr) + 8;
    bufL.assign (N, 0.0f);
    bufR.assign (N, 0.0f);
    reset();
}

void Chorus::reset() noexcept {
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    pos = 0; phase = 0.0f; wasOn = false;
}

void Chorus::process (float* l, float* r, int n, const ChorusParams& p) noexcept {
    if (! p.on) { wasOn = false; return; }
    if (bufL.empty()) return;
    if (! wasOn) {
        reset(); wasOn = true;
        rateS = clampv (p.rate, 0.05f, 5.0f); depthS = clamp01 (p.depth); mixS = clamp01 (p.mix);
    }
    const int N = (int) bufL.size();
    const float rT = clampv (p.rate, 0.05f, 5.0f), dT = clamp01 (p.depth), mT = clamp01 (p.mix);
    const float c = smoothCoef (sr, 0.02);
    const float msToSamp = (float) (sr * 0.001);
    for (int i = 0; i < n; ++i) {
        rateS += c * (rT - rateS); depthS += c * (dT - depthS); mixS += c * (mT - mixS);
        phase += rateS / (float) sr;
        if (phase >= 1.0f) phase -= 1.0f;
        bufL[(size_t) pos] = l[i];
        bufR[(size_t) pos] = r[i];
        float wl = 0.0f, wr = 0.0f;
        for (int k = 0; k < 3; ++k) {
            float pl = phase + (float) k * (1.0f / 3.0f);
            float pr = pl + 0.25f;
            pl -= std::floor (pl); pr -= std::floor (pr);
            const float centre = 14.0f + 2.0f * (float) k;                 // 14, 16, 18 ms
            const float dl = (centre + depthS * 6.0f * std::sin (kTwoPi * pl)) * msToSamp;   // 8..24 ms
            const float dr = (centre + depthS * 6.0f * std::sin (kTwoPi * pr)) * msToSamp;
            wl += readLin (bufL.data(), N, pos, dl);
            wr += readLin (bufR.data(), N, pos, dr);
        }
        wl *= (1.0f / 3.0f); wr *= (1.0f / 3.0f);
        if (++pos >= N) pos = 0;
        l[i] = l[i] * (1.0f - mixS) + wl * mixS;
        r[i] = r[i] * (1.0f - mixS) + wr * mixS;
    }
}

// ------------------------------------------------------------------ Delay
void Delay::prepare (double sampleRate, int) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const size_t N = (size_t) (4.0 * sr) + 8;
    bufL.assign (N, 0.0f);
    bufR.assign (N, 0.0f);
    reset();
}

void Delay::reset() noexcept {
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    pos = 0; curDelay = 0.0; lpL = lpR = 0.0f; wasOn = false;
}

void Delay::process (float* l, float* r, int n, const DelayParams& p, double bpm) noexcept {
    if (! p.on) { wasOn = false; return; }
    if (bufL.empty()) return;
    const int N = (int) bufL.size();
    const double maxD = 4.0 * sr;
    double target;
    if (p.sync) {
        const double b = std::isfinite (bpm) && bpm > 1.0 ? std::min (bpm, 999.0) : 120.0;
        target = (double) clampv (p.beats, 0.0625f, 4.0f) * (60.0 / b) * sr;
    } else {
        target = (double) clampv (p.ms, 1.0f, 2000.0f) * 0.001 * sr;
    }
    target = clampv (target, 1.0, maxD);
    const float mT = clamp01 (p.mix);
    if (! wasOn) { reset(); wasOn = true; curDelay = target; mixS = mT; }

    const float fb = clampv (p.feedback, 0.0f, 0.95f);
    const double fc = 400.0 * std::pow (40.0, (double) clamp01 (p.tone));   // 400 Hz .. 16 kHz
    const float a = (float) (1.0 - std::exp (-6.283185307179586 * std::min (fc, 0.45 * sr) / sr));
    const double slew = 1.0 - std::exp (-1.0 / (0.04 * sr));
    const float mc = smoothCoef (sr, 0.02);

    for (int i = 0; i < n; ++i) {
        curDelay += slew * (target - curDelay);
        if (std::fabs (target - curDelay) < 1e-3) curDelay = target;
        mixS += mc * (mT - mixS);
        const float dl = readLin (bufL.data(), N, pos, curDelay);
        const float dr = readLin (bufR.data(), N, pos, curDelay);
        lpL = flush (lpL + a * (dl - lpL));
        lpR = flush (lpR + a * (dr - lpR));
        float wl, wr;
        if (p.pingPong) {
            wl = flush (0.5f * (l[i] + r[i]) + fb * lpR);
            wr = flush (fb * lpL);
        } else {
            wl = flush (l[i] + fb * lpL);
            wr = flush (r[i] + fb * lpR);
        }
        bufL[(size_t) pos] = wl;
        bufR[(size_t) pos] = wr;
        if (++pos >= N) pos = 0;
        l[i] += dl * mixS;
        r[i] += dr * mixS;
    }
}

// ------------------------------------------------------------------ Reverb
namespace {
constexpr int kBase48[8] = { 1009, 1297, 1511, 1753, 2003, 2297, 2579, 2851 };   // primes @48k
constexpr float kLfoHz[8] = { 0.11f, 0.17f, 0.23f, 0.29f, 0.13f, 0.19f, 0.31f, 0.37f };
constexpr float kMaxScale = 2.0f;
}

void Reverb::prepare (double sampleRate, int) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const double k = sr / 48000.0;
    int total = 0;
    const int modMax = (int) (0.0006 * sr) + 2;
    for (int i = 0; i < N; ++i) {
        cap[i] = (int) std::ceil (kBase48[i] * k * kMaxScale) + modMax + 8;
        off[i] = total; total += cap[i];
    }
    pdCap = (int) (0.2 * sr) + 8;
    pdOffL = total; total += pdCap;
    pdOffR = total; total += pdCap;
    mem.assign ((size_t) total, 0.0f);
    reset();
}

void Reverb::reset() noexcept {
    std::fill (mem.begin(), mem.end(), 0.0f);
    for (int i = 0; i < N; ++i) {
        wp[i] = 0; lp[i] = 0.0f; phase[i] = (float) i * 0.125f;
        lenCur[i] = (float) (kBase48[i] * (sr / 48000.0));
    }
    pdPos = 0; pdS = 0.0f; wasOn = false;
}

void Reverb::process (float* l, float* r, int n, const ReverbParams& p) noexcept {
    if (! p.on) { wasOn = false; return; }
    if (mem.empty()) return;
    const float sizeT = clamp01 (p.size);
    const float scaleT = 0.5f + sizeT * 1.5f;
    const float mT = clamp01 (p.mix);
    const float pdT = clampv (p.predelayMs, 0.0f, 200.0f) * (float) (sr * 0.001);
    if (! wasOn) { reset(); wasOn = true; scaleS = scaleT; pdS = pdT; mixS = mT;
        for (int i = 0; i < N; ++i) lenCur[i] = (float) (kBase48[i] * (sr / 48000.0)) * scaleS; }

    const float rt60 = 0.25f * std::pow (32.0f, sizeT);
    const float damp = 1.0f - 0.85f * clamp01 (p.damping);
    const float width = clampv (p.width, 0.0f, 2.0f);
    const float mc = smoothCoef (sr, 0.02), pc = smoothCoef (sr, 0.05);
    const float sc = 1.0f - std::exp (-(float) kSubBlock / (float) (0.1 * sr));
    const double k = sr / 48000.0;
    const float modDepth = (float) (0.0004 * sr);
    float* pdL = mem.data() + pdOffL;
    float* pdR = mem.data() + pdOffR;

    float g[N], inc[N], d[N];
    int done = 0;
    while (done < n) {
        const int m = std::min (kSubBlock, n - done);
        scaleS += sc * (scaleT - scaleS);
        for (int i = 0; i < N; ++i) {
            phase[i] += kLfoHz[i] * (float) m / (float) sr;
            phase[i] -= std::floor (phase[i]);
            float tl = (float) (kBase48[i] * k) * scaleS + modDepth * std::sin (kTwoPi * phase[i]);
            tl = clampv (tl, 4.0f, (float) (cap[i] - 4));
            inc[i] = (tl - lenCur[i]) / (float) m;
            g[i] = std::pow (10.0f, -3.0f * tl / ((float) sr * rt60));
        }
        for (int s = 0; s < m; ++s) {
            const int idx = done + s;
            mixS += mc * (mT - mixS);
            pdS += pc * (pdT - pdS);
            pdL[pdPos] = l[idx]; pdR[pdPos] = r[idx];
            const float xl = readLin (pdL, pdCap, pdPos, (double) pdS);
            const float xr = readLin (pdR, pdCap, pdPos, (double) pdS);
            if (++pdPos >= pdCap) pdPos = 0;

            float sum = 0.0f;
            for (int i = 0; i < N; ++i) {
                lenCur[i] += inc[i];
                d[i] = readLin (mem.data() + off[i], cap[i], wp[i], (double) lenCur[i]);
                lp[i] = flush (lp[i] + damp * (d[i] - lp[i]));
                sum += lp[i] * g[i];
            }
            const float hs = 0.25f * sum;
            for (int i = 0; i < N; ++i) {
                const float in = 0.5f * ((i & 1) ? xr : xl);
                float* b = mem.data() + off[i];
                b[wp[i]] = flush (lp[i] * g[i] - hs + in);
                if (++wp[i] >= cap[i]) wp[i] = 0;
            }
            const float n8 = 0.35355339f;
            float wl = 0.0f, wr = 0.0f;
            for (int i = 0; i < N; ++i) {
                wl += ((i & 1) ? -d[i] : d[i]);
                wr += (((i >> 1) & 1) ? -d[i] : d[i]);
            }
            wl *= n8; wr *= n8;
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
            wl = mid + side; wr = mid - side;
            l[idx] = l[idx] * (1.0f - mixS) + wl * mixS;
            r[idx] = r[idx] * (1.0f - mixS) + wr * mixS;
        }
        done += m;
    }
}

} // namespace el::chimera
