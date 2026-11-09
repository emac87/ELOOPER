#include "Synth/dsp/Modulation.h"

namespace el::chimera {

namespace {
    // Per-sample coefficient so an exponential approach covers ~99% of the distance in `seconds`.
    inline float coefFor (float seconds, float sr) noexcept
    {
        const float s = std::max (seconds, 0.0005f) * sr;
        return 1.0f - std::exp (-4.6f / s);
    }
}

void Envelope::noteOn (bool legato) noexcept
{
    if (! legato) value = std::min (value, 1.0f);   // retrigger from where it is (no click)
    stage = legato && stage != Stage::Idle && stage != Stage::Release ? stage : Stage::Attack;
}

float Envelope::process (const EnvParams& p, int n, float* out) noexcept
{
    if (stage == Stage::Idle) {
        if (out) for (int i = 0; i < n; ++i) out[i] = 0.0f;
        value = 0;
        return 0;
    }
    const float sus = clamp01 (p.sustain);
    // Attack: approach 1.3 so the curve reaches 1.0 at about `attack` seconds.
    const float aCoef = 1.0f - std::exp (-1.47f / (std::max (p.attack, 0.0005f) * sampleRate));
    const float dCoef = coefFor (p.decay, sampleRate);
    const float rCoef = coefFor (p.release, sampleRate);
    for (int i = 0; i < n; ++i) {
        switch (stage) {
            case Stage::Attack:
                value += (1.3f - value) * aCoef;
                if (value >= 1.0f) { value = 1.0f; stage = Stage::Decay; }
                break;
            case Stage::Decay:
                value += (sus - value) * dCoef;
                if (std::abs (value - sus) < 1.0e-4f) { value = sus; stage = Stage::Sustain; }
                break;
            case Stage::Sustain:
                value += (sus - value) * dCoef;   // follows sustain changes smoothly
                break;
            case Stage::Release:
                value -= value * rCoef;
                if (value < 1.0e-4f) { value = 0; stage = Stage::Idle; }
                break;
            case Stage::Idle:
                value = 0;
                break;
        }
        if (out) out[i] = value;
    }
    return value;
}

void Lfo::reset (float phase01, uint32_t seed) noexcept
{
    phase = phase01;
    rng.seed (seed);
    held = rng.bi();
    prevHeld = held;
    out = 0;
}

float Lfo::advance (const LfoParams& p, int n, double bpm, float rateMul) noexcept
{
    double hz = p.sync ? (bpm / 60.0) / std::max (1.0f / 64.0f, p.beats) : (double) clampv (p.rateHz, 0.01f, 40.0f);
    hz *= std::max (0.0f, rateMul);
    const double prev = phase;
    phase += hz * n / sampleRate;
    const bool wrapped = phase >= 1.0;
    phase -= std::floor (phase);
    if (wrapped || prev > phase) { prevHeld = held; held = rng.bi(); }
    const float ph = (float) phase;
    switch (p.shape) {
        case LfoShape::Sine:        out = std::sin (kTwoPi * ph); break;
        case LfoShape::Triangle:    out = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
        case LfoShape::SawUp:       out = 2.0f * ph - 1.0f; break;
        case LfoShape::SawDown:     out = 1.0f - 2.0f * ph; break;
        case LfoShape::Square:      out = ph < 0.5f ? 1.0f : -1.0f; break;
        case LfoShape::SampleHold:  out = held; break;
        case LfoShape::SmoothRandom: {
            const float t = ph * ph * (3.0f - 2.0f * ph);   // smoothstep between random points
            out = prevHeld + (held - prevHeld) * t;
            break;
        }
    }
    return out;
}

} // namespace el::chimera
