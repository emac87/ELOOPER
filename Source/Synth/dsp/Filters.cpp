// Chimera filters: TPT state-variable (12/24 dB), Moog-style ladder, feedback comb.
#include "Synth/dsp/Filters.h"
#include <algorithm>

namespace el::chimera {

const char* filterTypeName (FilterType t) noexcept
{
    switch (t) {
        case FilterType::Off:        return "Off";
        case FilterType::LowPass12:  return "LP 12";
        case FilterType::LowPass24:  return "LP 24";
        case FilterType::HighPass12: return "HP 12";
        case FilterType::HighPass24: return "HP 24";
        case FilterType::BandPass:   return "BP";
        case FilterType::Notch:      return "Notch";
        case FilterType::Ladder:     return "Ladder";
        case FilterType::Comb:       return "Comb";
    }
    return "Off";
}

namespace {
inline float fastTanh (float x) noexcept
{
    x = x > 3.0f ? 3.0f : (x < -3.0f ? -3.0f : x);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Output safety: kills NaN/inf and softly limits anything beyond +-16 (never reached normally).
inline float safeOut (float y) noexcept
{
    if (! (std::fabs (y) < 1.0e6f)) return 0.0f;
    const float a = std::fabs (y);
    if (a > 16.0f) y = std::copysign (16.0f + 8.0f * fastTanh ((a - 16.0f) * (1.0f / 8.0f)), y);
    return y;
}

inline float saturate (float x, float drv) noexcept
{
    if (drv <= 0.0f) return x;
    const float gd = 1.0f + 7.0f * drv;
    return x + drv * (fastTanh (gd * x) / std::sqrt (gd) - x);
}

inline bool isSvf (FilterType t) noexcept
{
    return t >= FilterType::LowPass12 && t <= FilterType::Notch;
}

// Zavalishin TPT SVF tick. Returns lp, bp (non-normalised: peak gain 1/k).
inline void svf (float v0, float g, float k, float& ic1, float& ic2, float& lp, float& bp) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1, a3 = g * a2;
    const float v3 = v0 - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    lp = v2; bp = v1;
}

inline float flush (float v) noexcept { return std::fabs (v) < 1.0e-15f ? 0.0f : v; }
}

void Filter::prepare (float sampleRate)
{
    sr = sampleRate > 1000.0f ? sampleRate : 48000.0f;
    comb.assign ((size_t) (sr * 0.05f) + 8, 0.0f);
    reset();
}

void Filter::reset() noexcept
{
    s1[0] = s1[1] = s2[0] = s2[1] = 0.0f;
    for (float& v : lad) v = 0.0f;
    ladY = 0.0f;
    std::fill (comb.begin(), comb.end(), 0.0f);
    combPos = 0;
    snap = true;
}

void Filter::set (FilterType t, float cutoffHz, float resonance, float drv) noexcept
{
    const float cut = clampv (cutoffHz, 20.0f, 0.45f * sr);
    const float res = clamp01 (resonance);
    if (t != type) {
        // Only the structure we are entering starts from silence; shared SVF state carries over.
        if (t == FilterType::Ladder) { for (float& v : lad) v = 0.0f; ladY = 0.0f; }
        else if (t == FilterType::Comb) { std::fill (comb.begin(), comb.end(), 0.0f); combPos = 0; }
        else if (isSvf (t) && ! isSvf (type)) { s1[0] = s1[1] = s2[0] = s2[1] = 0.0f; }
        type = t;
        snap = true;
    }
    gT = std::tan (kPi * cut / sr);
    driveT = clamp01 (drv);
    switch (t) {
        case FilterType::LowPass24:
        case FilterType::HighPass24:
            kT = 1.848f * std::pow (0.6f, res);
            k2T = 0.765f * std::pow (0.055f, res);
            break;
        default:
            kT = 1.4142f * std::pow (0.03f, res);
            k2T = kT;
            break;
    }
    ladGT = gT / (1.0f + gT);
    ladKT = 4.0f * res;
    const float maxD = comb.size() > 8 ? (float) comb.size() - 3.0f : 2.0f;
    combDelayT = clampv (sr / cut, 2.0f, maxD);
    combFbT = 0.95f * res;
    if (snap) {
        g = gT; k = kT; k2 = k2T; drive = driveT; ladG = ladGT; ladK = ladKT;
        combDelay = combDelayT; combFb = combFbT;
        snap = false;
    }
}

void Filter::process (float* io, int n) noexcept
{
    if (n <= 0 || type == FilterType::Off) return;
    const float inv = 1.0f / (float) n;
    const float dg = (gT - g) * inv, dk = (kT - k) * inv, dk2 = (k2T - k2) * inv, dd = (driveT - drive) * inv;
    const float dlg = (ladGT - ladG) * inv, dlk = (ladKT - ladK) * inv;
    const float dcd = (combDelayT - combDelay) * inv, dcf = (combFbT - combFb) * inv;

    switch (type) {
    case FilterType::LowPass12: case FilterType::HighPass12:
    case FilterType::BandPass:  case FilterType::Notch: {
        for (int i = 0; i < n; ++i) {
            g += dg; k += dk; drive += dd;
            const float x = saturate (io[i], drive);
            float lp, bp;
            svf (x, g, k, s1[0], s2[0], lp, bp);
            float y;
            switch (type) {
                case FilterType::LowPass12:  y = lp; break;
                case FilterType::HighPass12: y = x - k * bp - lp; break;
                case FilterType::BandPass:   y = k * bp; break;
                default:                     y = x - k * bp; break;
            }
            io[i] = safeOut (y);
        }
        break; }
    case FilterType::LowPass24: case FilterType::HighPass24: {
        const bool hp = type == FilterType::HighPass24;
        for (int i = 0; i < n; ++i) {
            g += dg; k += dk; k2 += dk2; drive += dd;
            const float x = saturate (io[i], drive);
            float lp, bp;
            svf (x, g, k, s1[0], s2[0], lp, bp);
            const float m = hp ? x - k * bp - lp : lp;
            float lp2, bp2;
            svf (m, g, k2, s1[1], s2[1], lp2, bp2);
            io[i] = safeOut (hp ? m - k2 * bp2 - lp2 : lp2);
        }
        break; }
    case FilterType::Ladder: {
        for (int i = 0; i < n; ++i) {
            ladG += dlg; ladK += dlk; drive += dd;
            const float x = saturate (io[i], drive);
            float in = fastTanh (x - ladK * ladY);
            for (int s = 0; s < 4; ++s) {
                const float v = (in - lad[s]) * ladG;
                const float y = v + lad[s];
                lad[s] = y + v;
                in = y;
            }
            ladY = in;
            io[i] = safeOut (in * (1.0f + 0.25f * ladK * 0.25f));
        }
        break; }
    case FilterType::Comb: {
        const int sz = (int) comb.size();
        if (sz < 16) break;
        for (int i = 0; i < n; ++i) {
            combDelay += dcd; combFb += dcf; drive += dd;
            const float x = saturate (io[i], drive);
            float rp = (float) combPos - combDelay;
            if (rp < 0.0f) rp += (float) sz;
            int i0 = (int) rp;
            const float fr = rp - (float) i0;
            if (i0 >= sz) i0 -= sz;
            const int i1 = i0 + 1 >= sz ? 0 : i0 + 1;
            const float d = comb[(size_t) i0] + (comb[(size_t) i1] - comb[(size_t) i0]) * fr;
            const float y = safeOut (x + combFb * d);
            comb[(size_t) combPos] = y;
            if (++combPos >= sz) combPos = 0;
            io[i] = y * (1.0f - 0.8f * combFb);
        }
        break; }
    default: break;
    }

    g = gT; k = kT; k2 = k2T; drive = driveT; ladG = ladGT; ladK = ladKT;
    combDelay = combDelayT; combFb = combFbT;

    // keep state finite
    bool bad = false;
    for (int i = 0; i < 2; ++i) { if (! std::isfinite (s1[i] + s2[i])) bad = true; s1[i] = flush (s1[i]); s2[i] = flush (s2[i]); }
    for (float& v : lad) { if (! std::isfinite (v)) bad = true; v = flush (v); }
    if (! std::isfinite (ladY)) bad = true;
    if (bad) {
        s1[0] = s1[1] = s2[0] = s2[1] = 0.0f;
        for (float& v : lad) v = 0.0f;
        ladY = 0.0f;
    }
}

} // namespace el::chimera
