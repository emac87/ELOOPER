#pragma once
// Chimera (ELooper's built-in synth): shared DSP basics. Pure C++17, no JUCE.
// Real-time rules for everything under Source/Synth/dsp: no allocation, locks, I/O or
// exceptions in render/process functions; allocate only in prepare()/constructors.
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace el::chimera {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr int kSubBlock = 32;   // modulation is evaluated every 32 samples (control rate)

inline float noteToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
inline float semisToRatio (float s) noexcept { return std::exp2 (s / 12.0f); }
inline float dbToGain (float db) noexcept { return db <= -96.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
inline float clamp01 (float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
template <typename T> inline T clampv (T x, T lo, T hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }

// Small, fast, deterministic random generator (xorshift32). Never seed with 0.
struct Rng {
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) noexcept { s = v ? v : 0x9E3779B9u; }
    uint32_t nextU() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() noexcept { return (float) (nextU() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
    float bi() noexcept { return uni() * 2.0f - 1.0f; }                                // [-1, 1)
};

} // namespace el::chimera
