#pragma once
#include "Types.h"

namespace el {

// 10-band stereo graphic EQ (peaking biquads, ISO octave centres 31 Hz .. 16 kHz).
// Flat bands are skipped entirely, so an untouched EQ costs nothing.
class GraphicEq {
public:
    static constexpr float kFrequencies[kEqBands] = { 31.25f, 62.5f, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    // Gains in dB. Coefficients are only recomputed for bands that changed.
    void setGains (const float* gainsDb) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;
    bool isFlat() const noexcept { return activeMask == 0; }
private:
    struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; float z1[2] {}, z2[2] {}; };
    void design (int band, float gainDb) noexcept;
    Biquad bands[kEqBands];
    float gains[kEqBands] {};
    unsigned activeMask = 0;
    double sr = 48000;
};

} // namespace el
