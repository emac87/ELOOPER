#include "Synth/dsp/Sampler.h"
#include <algorithm>

namespace el::chimera {

namespace {
inline float hermite (float x0, float x1, float x2, float x3, float t) noexcept {
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
}
inline float fetch (const float* d, int len, int i) noexcept { return d[i < 0 ? 0 : (i >= len ? len - 1 : i)]; }
inline float interp (const float* d, int len, double pos) noexcept {
    const double fl = std::floor (pos);
    const int i = (int) fl;
    const float t = (float) (pos - fl);
    return hermite (fetch (d, len, i - 1), fetch (d, len, i), fetch (d, len, i + 1), fetch (d, len, i + 2), t);
}
}

void SamplerVoice::start (const SampleData* sample, float, float velocity, const SamplerParams& p,
                          double outputSampleRate) noexcept {
    s = sample;
    vel = velocity;
    outRate = outputSampleRate > 1.0 ? outputSampleRate : 48000.0;
    playing = false;
    if (s == nullptr || s->length() < 1) return;
    const double len = (double) s->length();
    const double st = (double) clamp01 (p.start);
    if (p.reverse) { pos = std::max (0.0, len - 1.0 - st * len); dir = -1; }
    else           { pos = std::min (len - 1.0, st * len);        dir = 1; }
    playing = true;
}

bool SamplerVoice::render (float* l, float* r, int n, float note, const SamplerParams& p) noexcept {
    if (! playing || s == nullptr || s->length() < 1) {
        playing = false;
        for (int i = 0; i < n; ++i) { l[i] = 0.0f; r[i] = 0.0f; }
        return false;
    }
    const int len = s->length();
    const double lenD = (double) len;
    const float* dl = s->left.data();
    const bool st = s->stereo() && s->right.size() >= s->left.size();
    const float* dr = st ? s->right.data() : dl;

    if (! std::isfinite (note)) note = (float) s->rootNote;
    note = clampv (note, -60.0f, 200.0f);
    double ratio = std::exp2 (((double) note - (double) s->rootNote) / 12.0) * s->sampleRate / outRate;
    if (! std::isfinite (ratio) || ratio < 0.0) ratio = 0.0;
    ratio = std::min (ratio, 256.0);

    // loop points (samples), clamped to a sane minimum length
    const double minLoop = std::min (4.0, lenD);
    double ls = (double) clamp01 (p.loopStart) * lenD;
    double le = (double) clamp01 (p.loopEnd) * lenD;
    if (le < ls + minLoop) le = ls + minLoop;
    if (le > lenD) { le = lenD; ls = le - minLoop; }
    if (ls < 0.0) ls = 0.0;
    const double ll = le - ls;

    int i = 0;
    for (; i < n; ++i) {
        if (! std::isfinite (pos)) { playing = false; break; }
        if (p.loop == LoopMode::Forward) {
            if (dir > 0 && pos >= le)      pos = ls + std::fmod (pos - ls, ll);
            else if (dir < 0 && pos < ls)  pos = le - std::fmod (ls - pos, ll);
        } else if (p.loop == LoopMode::PingPong) {
            if (dir > 0 && pos >= le) {
                const double m = std::fmod (pos - le, 2.0 * ll);
                if (m < ll) { pos = le - m; dir = -1; } else { pos = ls + (m - ll); dir = 1; }
                pos = clampv (pos, ls, le);
            } else if (dir < 0 && pos < ls) {
                const double m = std::fmod (ls - pos, 2.0 * ll);
                if (m < ll) { pos = ls + m; dir = 1; } else { pos = le - (m - ll); dir = -1; }
                pos = clampv (pos, ls, le);
            }
        } else if (pos < 0.0 || pos >= lenD) {
            playing = false;
            break;
        }
        l[i] = interp (dl, len, pos);
        r[i] = st ? interp (dr, len, pos) : l[i];
        pos += (double) dir * ratio;
    }
    for (; i < n; ++i) { l[i] = 0.0f; r[i] = 0.0f; }
    return playing;
}

} // namespace el::chimera
