#pragma once
// Chimera filters. Pure C++17. See Common.h for the real-time rules.
#include "Synth/dsp/Common.h"

namespace el::chimera {

enum class FilterType : uint8_t {
    Off, LowPass12, LowPass24, HighPass12, HighPass24, BandPass, Notch, Ladder, Comb
};
constexpr int kNumFilterTypes = 9;
const char* filterTypeName (FilterType) noexcept;   // "Off", "LP 12", "LP 24", "HP 12", "HP 24", "BP", "Notch", "Ladder", "Comb"

class Filter {
public:
    void prepare (float sampleRate);   // allocates the comb delay (up to 50 ms)
    void reset() noexcept;
    // Called once per sub-block (every ~32 samples), so it must be cheap. cutoffHz is clamped
    // to 20 .. 0.45 * sampleRate. resonance 0..1 (the SVF / ladder approach self-oscillation
    // near 1 but stay stable). drive 0..1 adds saturation before the filter (tanh-style).
    // For Comb, cutoffHz sets the comb frequency and resonance its feedback (+/-).
    // Changing type or parameters between calls must not click or blow up.
    void set (FilterType t, float cutoffHz, float resonance, float drive) noexcept;
    void process (float* io, int n) noexcept;   // in place, mono
private:
    float sr = 48000.0f;
    FilterType type = FilterType::Off;
    bool snap = true;
    // current (per-sample ramped) values and their targets
    float g = 0.1f, k = 1.4f, k2 = 1.4f, drive = 0, ladG = 0.1f, ladK = 0, combDelay = 2, combFb = 0;
    float gT = 0.1f, kT = 1.4f, k2T = 1.4f, driveT = 0, ladGT = 0.1f, ladKT = 0, combDelayT = 2, combFbT = 0;
    float s1[2] {}, s2[2] {}, lad[4] {}, ladY = 0;
    std::vector<float> comb;
    int combPos = 0;
};

} // namespace el::chimera
