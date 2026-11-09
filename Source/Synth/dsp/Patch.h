#pragma once
// Chimera patch model: up to 4 layers, each with its own synthesis engine (Analog, Wavetable,
// FM, Sampler, Pluck), filter, 3 envelopes, 2 LFOs, unison, glide, key / velocity zones, output
// pair and an 8-slot modulation matrix; plus 2 global LFOs, 4 macros, master FX and volume.
// Plain data, copied freely on the message thread; the audio thread only reads it.
#include "Synth/dsp/Common.h"
#include "Synth/dsp/Effects.h"
#include "Synth/dsp/Filters.h"
#include "Synth/dsp/Fm.h"
#include "Synth/dsp/Oscillators.h"
#include "Synth/dsp/Sampler.h"

namespace el::chimera {

constexpr int kMaxLayers = 4;
constexpr int kLayerLfos = 2;
constexpr int kGlobalLfos = 2;
constexpr int kMatrixSlots = 8;
constexpr int kMacros = 4;
constexpr int kMaxVoices = 64;      // shared by all layers
constexpr int kMaxUnison = 7;
constexpr int kOutputPairs = 4;     // main + 3 extra stereo outputs (multi-out)

enum class Engine : uint8_t { Off, Analog, Wavetable, FM, Sampler, Pluck };
constexpr int kNumEngines = 6;
const char* engineName (Engine) noexcept;

struct EnvParams {
    float attack = 0.003f, decay = 0.4f, sustain = 0.8f, release = 0.25f;   // seconds; sustain 0..1
};

enum class LfoShape : uint8_t { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom };
constexpr int kNumLfoShapes = 7;
const char* lfoShapeName (LfoShape) noexcept;

struct LfoParams {
    LfoShape shape = LfoShape::Sine;
    float rateHz = 2.0f;        // 0.01..40 when not synced
    bool sync = false;          // tempo-synced: one cycle every `beats`
    float beats = 1.0f;         // 1/16 .. 16 beats
    bool retrigger = true;      // layer LFOs: restart on each note (else free-running)
    float fadeIn = 0.0f;        // seconds to fade in after note on (layer LFOs)
};

enum class ModSrc : uint8_t {
    None, Lfo1, Lfo2, GlobalLfo1, GlobalLfo2, AmpEnv, FilterEnv, ModEnv, Velocity, KeyTrack,
    ModWheel, Aftertouch, PitchBend, Random, Macro1, Macro2, Macro3, Macro4
};
constexpr int kNumModSrcs = 18;
const char* modSrcName (ModSrc) noexcept;

enum class ModDst : uint8_t {
    None, Pitch, Cutoff, Resonance, Drive, Level, Pan, PulseWidth, OscMix, Osc2Pitch, Noise,
    WavePosition, FmAmount, SampleStart, PluckDamping, Lfo1Rate, Lfo2Rate, FilterEnvAmount
};
constexpr int kNumModDsts = 18;
const char* modDstName (ModDst) noexcept;

struct ModSlot { ModSrc src = ModSrc::None; ModDst dst = ModDst::None; float amount = 0.0f; /* -1..1 */ };

enum class VoiceMode : uint8_t { Poly, Mono, Legato };

struct LayerParams {
    Engine engine = Engine::Off;
    std::string name;                 // optional label (defaults to the engine name)

    // engines
    AnalogParams analog;
    int wtTable = 0;
    float wtPosition = 0.0f;          // 0..1
    Dx7Patch fm = dx7InitVoice();
    float fmAmount = 1.0f;            // 0..2, scales modulator depth
    SamplerParams sampler;
    PluckParams pluck;

    // filter
    FilterType filter = FilterType::LowPass24;
    float cutoff = 12000.0f;          // Hz
    float resonance = 0.15f;          // 0..1
    float drive = 0.0f;               // 0..1
    float keyTrack = 0.0f;            // 0..1: how much cutoff follows the note (1 = 100%)
    float filterEnvAmount = 0.0f;     // -1..1 = -60..+60 semitones of cutoff

    // envelopes: amp, filter, mod
    EnvParams ampEnv, filterEnv { 0.003f, 0.5f, 0.0f, 0.3f }, modEnv { 0.003f, 0.5f, 0.0f, 0.3f };
    bool useAmpEnv = true;            // FM layers usually let the operator envelopes shape the sound

    LfoParams lfo[kLayerLfos];

    // voice
    float level = 0.75f;              // 0..1 (squared for gain)
    float pan = 0.0f;                 // -1..1
    int transpose = 0;                // semitones
    float fine = 0.0f;                // cents
    int keyLo = 0, keyHi = 127, velLo = 1, velHi = 127;
    float velocitySens = 0.6f;        // 0..1
    int unison = 1;                   // 1..7
    float unisonDetune = 0.2f;        // 0..1 = up to 50 cents spread
    float unisonSpread = 0.6f;        // stereo width of the unison voices 0..1
    float glide = 0.0f;               // seconds
    VoiceMode mode = VoiceMode::Poly;
    int polyphony = 16;               // 1..32 voices for this layer
    int output = 0;                   // 0..3 stereo output pair (only pair 0 goes through the FX)

    ModSlot mods[kMatrixSlots];
};

struct Patch {
    std::string name = "Init";
    std::string category;             // e.g. "Bass", "Pad", "Keys"
    std::array<LayerParams, kMaxLayers> layers {};
    std::array<SamplePtr, kMaxLayers> samples {};                                   // Sampler layers
    std::array<std::shared_ptr<const std::vector<Dx7Patch>>, kMaxLayers> carts {};  // loaded DX7 carts (browsing)
    std::array<std::string, kMaxLayers> cartNames {};
    LfoParams globalLfo[kGlobalLfos] { {}, { LfoShape::Triangle, 0.25f, false, 1.0f, false, 0.0f } };
    std::array<std::string, kMacros> macroNames { "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    ChorusParams chorus;
    DelayParams delay;
    ReverbParams reverb;
    float masterVolume = 0.7f;        // 0..1 (squared for gain)
    float bendRange = 2.0f;           // semitones

    Patch() { layers[0].engine = Engine::Analog; }
};

// Values the host can automate, refreshed every block; they override the patch's own fields.
struct LiveParams {
    float macros[kMacros] { 0, 0, 0, 0 };
    float master = 0.7f;
    float level[kMaxLayers] { 0.75f, 0.75f, 0.75f, 0.75f };
    float cutoff[kMaxLayers] { 12000, 12000, 12000, 12000 };
    float resonance[kMaxLayers] { 0.15f, 0.15f, 0.15f, 0.15f };
    float pan[kMaxLayers] { 0, 0, 0, 0 };
    static LiveParams fromPatch (const Patch&) noexcept;
};

} // namespace el::chimera
