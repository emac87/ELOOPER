#pragma once
// Chimera master effects (stereo, in place). Pure C++17. See Common.h for the real-time rules.
#include "Synth/dsp/Common.h"

namespace el::chimera {

struct ChorusParams { bool on = false; float rate = 0.4f /*Hz 0.05..5*/; float depth = 0.5f /*0..1*/; float mix = 0.35f /*0..1*/; };
struct DelayParams {
    bool on = false;
    bool sync = true;          // time in beats (uses bpm) or in ms
    float beats = 0.75f;       // when sync (0.0625 .. 4)
    float ms = 375.0f;         // when not sync (1 .. 2000)
    float feedback = 0.35f;    // 0..0.95
    float mix = 0.25f;         // 0..1
    float tone = 0.6f;         // 0 = dark repeats, 1 = bright
    bool pingPong = true;
};
struct ReverbParams { bool on = false; float size = 0.6f; float damping = 0.5f; float mix = 0.25f; float predelayMs = 10.0f /*0..200*/; float width = 1.0f; };

class Chorus {
public:
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;
    void process (float* l, float* r, int n, const ChorusParams& p) noexcept;   // no-op when !p.on
private:
    std::vector<float> bufL, bufR; int pos = 0; double sr = 48000; float phase = 0;
    float rateS = 0.4f, depthS = 0.5f, mixS = 0.35f; bool wasOn = false;
};

class Delay {
public:
    void prepare (double sampleRate, int maxBlock);   // up to 4 s of delay
    void reset() noexcept;
    // Delay time changes glide smoothly (no zipper / clicks).
    void process (float* l, float* r, int n, const DelayParams& p, double bpm) noexcept;
private:
    std::vector<float> bufL, bufR; int pos = 0; double sr = 48000; double curDelay = 0; float lpL = 0, lpR = 0, mixS = 0.25f; bool wasOn = false;
};

class Reverb {
public:
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;
    void process (float* l, float* r, int n, const ReverbParams& p) noexcept;   // a smooth FDN / Freeverb-style hall
private:
    static constexpr int N = 8;
    std::vector<float> mem; double sr = 48000;
    int cap[N] = {}, off[N] = {}, wp[N] = {}, pdCap = 0, pdOffL = 0, pdOffR = 0, pdPos = 0;
    float lenCur[N] = {}, lp[N] = {}, phase[N] = {};
    float scaleS = 1.0f, pdS = 0.0f, mixS = 0.25f;
    bool wasOn = false;
};

} // namespace el::chimera
