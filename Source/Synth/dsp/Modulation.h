#pragma once
// Chimera modulation sources: ADSR envelope and LFO. Pure C++17, real-time safe.
#include "Synth/dsp/Patch.h"

namespace el::chimera {

// Analog-style ADSR: the attack charges towards 1.3 (so it reaches 1 in `attack` seconds with a
// natural curve), decay and release are exponential with the stage time = time to fall ~99%.
class Envelope {
public:
    enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };
    void setSampleRate (float sr) noexcept { sampleRate = sr; }
    void noteOn (bool legato = false) noexcept;   // legato: continue from the current level
    void noteOff() noexcept { if (stage != Stage::Idle) stage = Stage::Release; }
    void kill() noexcept { stage = Stage::Idle; value = 0; }
    // Advance by n samples using params p; returns the value at the end. If out != nullptr the
    // per-sample values are written there.
    float process (const EnvParams& p, int n, float* out = nullptr) noexcept;
    float current() const noexcept { return value; }
    bool idle() const noexcept { return stage == Stage::Idle; }
    Stage getStage() const noexcept { return stage; }
private:
    float sampleRate = 48000.0f, value = 0.0f;
    Stage stage = Stage::Idle;
};

class Lfo {
public:
    void setSampleRate (float sr) noexcept { sampleRate = sr; }
    void reset (float phase01, uint32_t seed) noexcept;
    // Advance by n samples; returns the value (-1..1) for this block. fade 0..1 multiplier is
    // applied by the caller. rateMul scales the rate (modulation).
    float advance (const LfoParams& p, int n, double bpm, float rateMul = 1.0f) noexcept;
    float value() const noexcept { return out; }
private:
    float sampleRate = 48000.0f;
    double phase = 0;
    float out = 0, held = 0, prevHeld = 0;
    Rng rng;
};

} // namespace el::chimera
