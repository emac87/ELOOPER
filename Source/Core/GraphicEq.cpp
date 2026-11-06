#include "GraphicEq.h"
#include <algorithm>
#include <cmath>

namespace el {

constexpr float GraphicEq::kFrequencies[kEqBands];

void GraphicEq::prepare (double sampleRate) noexcept {
    sr = sampleRate;
    for (int b = 0; b < kEqBands; ++b) design (b, gains[b]);
    reset();
}

void GraphicEq::reset() noexcept {
    for (auto& b : bands) { b.z1[0] = b.z1[1] = b.z2[0] = b.z2[1] = 0; }
}

void GraphicEq::setGains (const float* g) noexcept {
    for (int b = 0; b < kEqBands; ++b)
        if (std::abs (g[b] - gains[b]) > 1.0e-3f) design (b, g[b]);
}

void GraphicEq::design (int band, float gainDb) noexcept {
    gains[band] = gainDb;
    const bool wasActive = (activeMask >> band) & 1u;
    if (std::abs (gainDb) < 0.05f) { activeMask &= ~(1u << band); return; }
    activeMask |= 1u << band;
    // RBJ peaking EQ, constant octave bandwidth (Q ~ 1.41).
    const double f = std::min ((double) kFrequencies[band], sr * 0.45);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * 3.14159265358979 * f / sr;
    const double alpha = std::sin (w) / (2.0 * 1.41);
    const double c = std::cos (w);
    const double a0 = 1.0 + alpha / A;
    auto& q = bands[band];
    q.b0 = (float) ((1.0 + alpha * A) / a0);
    q.b1 = (float) ((-2.0 * c) / a0);
    q.b2 = (float) ((1.0 - alpha * A) / a0);
    q.a1 = (float) ((-2.0 * c) / a0);
    q.a2 = (float) ((1.0 - alpha / A) / a0);
    if (! wasActive) { q.z1[0] = q.z1[1] = q.z2[0] = q.z2[1] = 0; }
}

void GraphicEq::process (float* left, float* right, int n) noexcept {
    if (activeMask == 0) return;
    float* ch[2] = { left, right };
    for (int b = 0; b < kEqBands; ++b) {
        if (! ((activeMask >> b) & 1u)) continue;
        auto& q = bands[b];
        for (int c = 0; c < 2; ++c) {
            if (ch[c] == nullptr) continue;
            float z1 = q.z1[c], z2 = q.z2[c];
            float* x = ch[c];
            for (int i = 0; i < n; ++i) {   // transposed direct form II
                const float in = x[i];
                const float out = q.b0 * in + z1;
                z1 = q.b1 * in - q.a1 * out + z2;
                z2 = q.b2 * in - q.a2 * out;
                x[i] = out;
            }
            // flush denormals
            q.z1[c] = std::abs (z1) < 1.0e-15f ? 0.0f : z1;
            q.z2[c] = std::abs (z2) < 1.0e-15f ? 0.0f : z2;
        }
    }
}

} // namespace el
