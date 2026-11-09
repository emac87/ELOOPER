#pragma once
// Chimera FM: a 6-operator DX7-compatible engine (32 algorithms, operator feedback, rate /
// level envelopes, keyboard scaling, velocity, pitch EG, LFO) and the Yamaha DX7 SysEx
// cartridge format. Pure C++17. See Common.h for the real-time rules.
#include "Synth/dsp/Common.h"

namespace el::chimera {

struct Dx7Op {
    uint8_t egRate[4] { 99, 99, 99, 99 }, egLevel[4] { 99, 99, 99, 0 };
    uint8_t kbdBreakPoint = 39;            // 0..99 (39 = C3)
    uint8_t kbdLeftDepth = 0, kbdRightDepth = 0;   // 0..99
    uint8_t kbdLeftCurve = 0, kbdRightCurve = 0;   // 0..3: -LIN, -EXP, +EXP, +LIN
    uint8_t kbdRateScaling = 0;            // 0..7
    uint8_t ampModSens = 0;                // 0..3
    uint8_t keyVelSens = 0;                // 0..7
    uint8_t outputLevel = 0;               // 0..99
    uint8_t oscMode = 0;                   // 0 = ratio, 1 = fixed
    uint8_t freqCoarse = 1;                // 0..31
    uint8_t freqFine = 0;                  // 0..99
    uint8_t detune = 7;                    // 0..14, 7 = centre
};

// One voice, unpacked (VCED order of fields). op[0] is OP1 ... op[5] is OP6 (the SysEx data
// stores OP6 first; the parser converts).
struct Dx7Patch {
    std::array<Dx7Op, 6> op {};
    uint8_t pitchEgRate[4] { 99, 99, 99, 99 }, pitchEgLevel[4] { 50, 50, 50, 50 };
    uint8_t algorithm = 0;        // 0..31 (algorithm 1..32)
    uint8_t feedback = 0;         // 0..7
    uint8_t oscKeySync = 1;
    uint8_t lfoSpeed = 35, lfoDelay = 0, lfoPitchModDepth = 0, lfoAmpModDepth = 0;
    uint8_t lfoKeySync = 1, lfoWave = 0 /*0 tri,1 saw down,2 saw up,3 square,4 sine,5 S&H*/;
    uint8_t lfoPitchModSens = 3;  // 0..7
    uint8_t transpose = 24;       // 0..48, 24 = no transpose
    char name[11] { 'I','N','I','T',' ','V','O','I','C','E', 0 };
};

// The DX7 "INIT VOICE": OP1 at full level, algorithm 1, everything else neutral.
Dx7Patch dx7InitVoice() noexcept;

// Parses a 32-voice bulk cartridge (4104 bytes: F0 43 0n 09 20 00 + 4096 packed + checksum + F7,
// or the raw 4096 packed bytes) or a single-voice dump (163 bytes: F0 43 0n 00 01 1B + 155 + cs +
// F7). Out-of-range bytes are masked/clamped (many carts in the wild are sloppy); a bad checksum
// is tolerated. Returns false with a reason if the data is not DX7 voice data.
bool parseDx7SysEx (const uint8_t* data, size_t size, std::vector<Dx7Patch>& out, std::string& error);
// 155-byte VCED (unpacked) form, used to save a voice inside a Chimera patch.
std::array<uint8_t, 155> dx7ToVced (const Dx7Patch&) noexcept;
Dx7Patch dx7FromVced (const uint8_t* vced155) noexcept;
// A 4104-byte bulk dump of up to 32 voices (missing voices = INIT VOICE), for "Save cart".
std::vector<uint8_t> dx7MakeCart (const std::vector<Dx7Patch>& voices);

class FmVoice {
public:
    void prepare (float sampleRate) noexcept;
    void noteOn (const Dx7Patch& p, int note, int velocity /*1..127*/) noexcept;
    void noteOff() noexcept;   // operator EGs go to their release stage
    // Writes n mono samples (about -1..1 for a single carrier at level 99; several carriers are
    // scaled like the DX7 does). pitchSemis = extra pitch on top of the note (bend + mods), may
    // change every call. fmAmount scales every modulator's output (0 = only carriers sound,
    // 1 = as programmed, up to 2). The patch passed to noteOn must stay alive while it plays.
    void render (float* out, int n, float pitchSemis, float fmAmount) noexcept;
    bool active() const noexcept;    // false once released and every carrier EG has decayed
private:
    const Dx7Patch* patch = nullptr;
    float sr = 48000.0f;
    // implementation state (per-op phase, EG state, feedback memory, pitch EG, LFO)
    struct OpState {
        uint32_t phase = 0, inc = 0;
        float amp = 0.0f, ampStep = 0.0f;
        float level = 0.0f;            // EG level, 0..127 in 0.75 dB units
        int stage = 5;                 // 0..2 running, 3 sustain, 4 release, 5 finished
        float opLevel = 0.0f;          // static level 0..127 (output level + scaling + velocity)
        float ratio = 1.0f, fixedHz = 0.0f, detuneRatio = 1.0f, amSens = 0.0f;
        bool fixed = false;
        float speed[4] {}, target[4] {};
    };
    std::array<OpState, 6> ops {};
    int algo = 0, note = 60, blockLeft = 0;
    bool released = true, silent = true;
    float fbScale = 0.0f, fbH0 = 0.0f, fbH1 = 0.0f, carrierScale = 1.0f, curPitch = 0.0f;
    float invSr = 1.0f / 48000.0f;
    // pitch EG
    bool pegOn = false;
    int pegStage = 5;
    float pegVal = 0.0f, pegSpeed[4] {}, pegTarget[4] {};
    // LFO
    double lfoPhase = 0.0;
    float lfoT = 0.0f, lfoHz = 1.0f, lfoHold = 0.0f, lfoFade = 0.0f, lfoSH = 0.0f;
    float lfoSemis = 0.0f, lfoAmU = 0.0f;
    Rng rng;
    void updateControl() noexcept;
    void computeIncs (float pitchSemis) noexcept;
};

} // namespace el::chimera
