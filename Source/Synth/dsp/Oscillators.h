#pragma once
// Chimera oscillators: Analog (virtual-analog, PolyBLEP), Wavetable (morphing, band-limited),
// Pluck (Karplus-Strong physical model). Pure C++17. See Common.h for the real-time rules.
#include "Synth/dsp/Common.h"

namespace el::chimera {

// ---- Analog ----------------------------------------------------------------------------
enum class Wave : uint8_t { Sine, Triangle, Saw, Square, Noise };

struct AnalogParams {
    Wave wave1 = Wave::Saw, wave2 = Wave::Saw;
    float pw1 = 0.5f, pw2 = 0.5f;     // pulse width for Square, 0.05..0.95
    float osc2Semi = 0.0f;            // osc 2 pitch offset in semitones (-24..+24)
    float osc2Fine = 0.0f;            // osc 2 fine offset in cents (-100..+100)
    float mix = 0.0f;                 // 0 = osc 1 only, 1 = osc 2 only (equal-power crossfade)
    float sub = 0.0f;                 // square sub-oscillator one octave below osc 1, 0..1
    float noise = 0.0f;               // white noise level, 0..1
    bool sync = false;                // osc 2 hard-synced to osc 1
};

class AnalogOsc {
public:
    // startPhase in [0,1). seed drives the noise.
    void reset (float sampleRate, uint32_t seed, float startPhase) noexcept;
    // Writes (not adds) n mono samples, roughly within -1..1, alias-suppressed (PolyBLEP /
    // PolyBLAMP for triangle). hz = osc 1 frequency (may change every call; no clicks).
    // pwMod (-1..1) is added to both pulse widths (then clamped to 0.05..0.95).
    void render (float* out, int n, float hz, const AnalogParams& p, float pwMod) noexcept;
private:
    float sr = 48000.0f, ph1 = 0, ph2 = 0, phSub = 0, triState1 = 0, triState2 = 0;
    Rng rng;
};

// ---- Wavetable --------------------------------------------------------------------------
// A fixed bank of built-in tables, each a morph through several single-cycle frames.
// Tables (in this order): "Basic Shapes" (sine > triangle > saw > square), "PWM",
// "Harmonics" (additive sweep), "Formant" (vowel-like), "Digital", "Organ", "Bell", "Sync Sweep".
class WavetableBank {
public:
    static constexpr int kFrameSize = 2048;   // samples per single-cycle frame
    static constexpr int kFrames = 16;        // frames per table (position morphs between them)
    static constexpr int kMipLevels = 10;     // band-limited copies, one per octave
    // Built on the first call: call once from a non-real-time thread before audio starts.
    static const WavetableBank& get();
    int numTables() const noexcept;
    const char* tableName (int index) const noexcept;
    // Pointer to kFrameSize samples of (table, frame, mip). mip 0 = full bandwidth.
    const float* frame (int table, int frame, int mip) const noexcept;
private:
    WavetableBank();
    std::vector<float> data;
    int tables = 0;
};

class WavetableOsc {
public:
    void reset (float sampleRate, float startPhase) noexcept;
    // Writes n mono samples. position 0..1 morphs smoothly across the table's frames
    // (linear crossfade between neighbouring frames); picks the mip level from hz so that
    // nothing aliases above Nyquist.
    void render (float* out, int n, float hz, int table, float position) noexcept;
private:
    float sr = 48000.0f, phase = 0;
};

// ---- Pluck (Karplus-Strong) --------------------------------------------------------------
struct PluckParams {
    float decay = 0.5f;       // 0..1 -> about 0.2 s .. 10 s ring time
    float damping = 0.5f;     // 0 = bright, 1 = dark
    float pickPos = 0.2f;     // 0..0.5, position of the pluck along the string (comb on excitation)
    float noiseColor = 0.5f;  // 0 = dark (filtered) excitation, 1 = bright white noise burst
};

class PluckVoice {
public:
    void prepare (float sampleRate);   // allocates the delay line (long enough for 20 Hz)
    void start (float hz, float velocity, const PluckParams& p, uint32_t seed) noexcept;
    // Writes n mono samples. hz may glide/bend: tuning via a fractional (allpass) delay so
    // pitch is accurate within a few cents up to 4 kHz.
    void render (float* out, int n, float hz, const PluckParams& p) noexcept;
private:
    std::vector<float> line;
    float sr = 48000.0f;
    int writePos = 0;
    float lp = 0, ap = 0, apIn = 0;
    Rng rng;
};

} // namespace el::chimera
