#pragma once
// Chimera sampler voice. Pure C++17. See Common.h for the real-time rules.
#include "Synth/dsp/Common.h"

namespace el::chimera {

struct SampleData {
    std::vector<float> left, right;   // right empty = mono
    double sampleRate = 44100.0;
    int rootNote = 60;                // MIDI note that plays the sample at its original pitch
    std::string name;                 // file name, for display
    std::string path;                 // full path it was loaded from (for saving the patch)
    bool stereo() const noexcept { return ! right.empty(); }
    int length() const noexcept { return (int) left.size(); }
};
using SamplePtr = std::shared_ptr<const SampleData>;

enum class LoopMode : uint8_t { Off, Forward, PingPong };

struct SamplerParams {
    float start = 0.0f;                     // 0..1 of the length
    LoopMode loop = LoopMode::Off;
    float loopStart = 0.0f, loopEnd = 1.0f; // 0..1 of the length (loopEnd > loopStart)
    bool reverse = false;                   // play backwards from the end (start applies from the end)
};

class SamplerVoice {
public:
    // `sample` must stay alive while the voice plays (the synth holds a SamplePtr).
    void start (const SampleData* sample, float note, float velocity, const SamplerParams& p,
                double outputSampleRate) noexcept;
    // Writes n samples to l and r (mono samples go to both). note may bend/glide; the pitch
    // ratio is 2^((note - rootNote)/12) * sample.sampleRate / outputSampleRate. Cubic (Hermite)
    // interpolation. Loop points are read from p every call (can be modulated). Short fades
    // at loop seams are not required. Returns false once a one-shot / non-looping sample has
    // ended (it then writes zeros for the rest).
    bool render (float* l, float* r, int n, float note, const SamplerParams& p) noexcept;
    bool active() const noexcept { return playing; }
private:
    const SampleData* s = nullptr;
    double pos = 0, outRate = 48000.0;
    int dir = 1;
    bool playing = false;
    float vel = 1.0f;
};

} // namespace el::chimera
