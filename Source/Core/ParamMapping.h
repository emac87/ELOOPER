#pragma once
// Normalised (0..1) <-> native value mapping, shared by UI, automation and engine.
#include <cmath>
#include <algorithm>

namespace el::pm {

// Fader law: gain = 2 * n^3  ->  n = 0.7937 is unity, n = 1 is +6 dB.
constexpr float kUnityVolume = 0.79370052f;
inline float volumeToGain (float n) noexcept { n = std::clamp (n, 0.0f, 1.0f); return 2.0f * n * n * n; }
inline float gainToVolume (float g) noexcept { return std::cbrt (std::max (0.0f, g) * 0.5f); }
inline float gainToDb (float g) noexcept { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }
inline float volumeToDb (float n) noexcept { return gainToDb (volumeToGain (n)); }

// Input gain: -24 .. +24 dB, 0.5 = 0 dB.
inline float inputGainToDb (float n) noexcept { return (std::clamp (n, 0.0f, 1.0f) - 0.5f) * 48.0f; }
inline float inputGainToGain (float n) noexcept { return std::pow (10.0f, inputGainToDb (n) / 20.0f); }

// Pan: 0 = hard left, 0.5 = centre, 1 = hard right. Constant-power (-3 dB centre).
inline float panToBipolar (float n) noexcept { return std::clamp (n, 0.0f, 1.0f) * 2.0f - 1.0f; }
inline void panGains (float n, float& l, float& r) noexcept {
    const float a = std::clamp (n, 0.0f, 1.0f) * 1.5707963f;
    l = std::cos (a) * 1.41421356f; r = std::sin (a) * 1.41421356f; // unity at centre
    l = std::min (l, 1.0f); r = std::min (r, 1.0f);
}

// Graphic-EQ band gain: -12 .. +12 dB, 0.5 = flat.
constexpr float kEqRangeDb = 12.0f;
inline float eqToDb (float n) noexcept { return (std::clamp (n, 0.0f, 1.0f) - 0.5f) * 2.0f * kEqRangeDb; }
inline float dbToEq (float db) noexcept { return std::clamp (db / (2.0f * kEqRangeDb) + 0.5f, 0.0f, 1.0f); }

} // namespace el::pm
