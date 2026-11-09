// Chimera factory presets, built in code (no files to ship). Every preset maps Macro 1 to
// brightness so a controller knob is useful straight away; most also use Macro 2.
#include "Synth/plugin/ChimeraProcessor.h"

namespace el::chimera {

namespace {
    LayerParams analogLayer()
    {
        LayerParams l;
        l.engine = Engine::Analog;
        return l;
    }

    void mod (LayerParams& l, int slot, ModSrc s, ModDst d, float amount) { l.mods[slot] = { s, d, amount }; }

    void brightnessMacro (Patch& p, int layer, float amount = 0.5f, int slot = 7)
    {
        mod (p.layers[(size_t) layer], slot, ModSrc::Macro1, ModDst::Cutoff, amount);
        p.macroNames[0] = "Brightness";
    }

    Dx7Op op (int coarse, int fine, int level, std::array<int, 4> rates, std::array<int, 4> levels, int velSens = 2, int detune = 7)
    {
        Dx7Op o;
        o.freqCoarse = (uint8_t) coarse; o.freqFine = (uint8_t) fine; o.outputLevel = (uint8_t) level;
        for (int i = 0; i < 4; ++i) { o.egRate[i] = (uint8_t) rates[(size_t) i]; o.egLevel[i] = (uint8_t) levels[(size_t) i]; }
        o.keyVelSens = (uint8_t) velSens; o.detune = (uint8_t) detune;
        return o;
    }

    Dx7Op silentOp() { Dx7Op o; o.outputLevel = 0; return o; }

    Dx7Patch dxVoice (const char* name, int algorithm, int feedback, std::array<Dx7Op, 6> ops)
    {
        Dx7Patch v = dx7InitVoice();
        v.algorithm = (uint8_t) (algorithm - 1);
        v.feedback = (uint8_t) feedback;
        v.op = ops;
        std::fill (std::begin (v.name), std::end (v.name), 0);
        for (int i = 0; i < 10 && name[i]; ++i) v.name[i] = name[i];
        return v;
    }

    LayerParams fmLayer (const Dx7Patch& v)
    {
        LayerParams l;
        l.engine = Engine::FM;
        l.fm = v;
        l.useAmpEnv = false;
        l.filter = FilterType::LowPass12;
        l.cutoff = 16000.0f;
        l.resonance = 0.0f;
        l.velocitySens = 0.3f;   // the operators already respond to velocity
        return l;
    }

    Dx7Patch fmEPiano()
    {
        return dxVoice ("CHIM EP   ", 5, 0, {
            op (1, 0, 99, { 96, 25, 25, 67 }, { 99, 75, 0, 0 }, 2, 7),        // OP1 carrier
            op (1, 0, 62, { 95, 50, 35, 78 }, { 99, 50, 0, 0 }, 6, 7),        // OP2 -> OP1
            op (1, 0, 93, { 95, 29, 20, 50 }, { 99, 95, 0, 0 }, 2, 9),        // OP3 carrier (detuned)
            op (14, 0, 56, { 99, 70, 30, 80 }, { 99, 0, 0, 0 }, 6, 7),        // OP4 tine -> OP3
            op (1, 0, 80, { 95, 20, 20, 50 }, { 99, 95, 0, 0 }, 1, 5),        // OP5 carrier
            op (1, 0, 50, { 95, 40, 30, 60 }, { 99, 40, 0, 0 }, 4, 7) });     // OP6 -> OP5
    }

    Dx7Patch fmBell()
    {
        return dxVoice ("CHIM BELL ", 5, 2, {
            op (1, 0, 99, { 99, 30, 18, 40 }, { 99, 60, 0, 0 }, 2, 7),
            op (3, 50, 76, { 99, 35, 20, 40 }, { 99, 55, 0, 0 }, 4, 7),
            op (1, 0, 88, { 99, 26, 15, 38 }, { 99, 50, 0, 0 }, 2, 10),
            op (7, 0, 64, { 99, 40, 25, 45 }, { 99, 40, 0, 0 }, 4, 7),
            silentOp(), silentOp() });
    }

    Dx7Patch fmBass()
    {
        return dxVoice ("CHIM BASS ", 1, 5, {
            op (0, 0, 99, { 99, 40, 30, 70 }, { 99, 85, 70, 0 }, 2, 7),
            op (0, 0, 82, { 99, 62, 40, 70 }, { 99, 45, 0, 0 }, 5, 7),
            silentOp(), silentOp(), silentOp(), silentOp() });
    }

    Dx7Patch fmBrass()
    {
        // Algorithm 22: OP2->OP1, and OP6 modulating OP3/OP4/OP5 carriers.
        return dxVoice ("CHIM BRASS", 22, 6, {
            op (1, 0, 99, { 72, 76, 25, 55 }, { 99, 88, 82, 0 }, 1, 7),
            op (1, 0, 78, { 62, 50, 30, 55 }, { 99, 80, 70, 0 }, 3, 7),
            op (1, 0, 94, { 72, 76, 25, 55 }, { 99, 88, 82, 0 }, 1, 8),
            op (1, 0, 94, { 72, 76, 25, 55 }, { 99, 88, 82, 0 }, 1, 6),
            op (1, 0, 90, { 72, 76, 25, 55 }, { 99, 88, 82, 0 }, 1, 9),
            op (1, 0, 74, { 55, 60, 30, 55 }, { 99, 85, 75, 0 }, 3, 7) });
    }

    Patch make (const char* name, const char* category)
    {
        Patch p;
        p.name = name;
        p.category = category;
        return p;
    }
}

std::vector<Patch> makeFactoryPresets()
{
    std::vector<Patch> out;

    { // ---------------------------------------------------------------- Init
        auto p = make ("Init", "Init");
        brightnessMacro (p, 0);
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Warm Pad
        auto p = make ("Warm Pad", "Pad");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave2 = Wave::Saw; l.analog.osc2Fine = 7.0f; l.analog.mix = 0.5f;
        l.unison = 3; l.unisonDetune = 0.25f; l.unisonSpread = 0.8f;
        l.filter = FilterType::LowPass24; l.cutoff = 2200.0f; l.resonance = 0.2f;
        l.ampEnv = { 0.6f, 1.0f, 0.85f, 1.6f };
        l.filterEnv = { 0.8f, 2.0f, 0.4f, 1.5f }; l.filterEnvAmount = 0.15f;
        l.lfo[0] = { LfoShape::Sine, 0.3f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Cutoff, 0.08f);
        mod (l, 1, ModSrc::Macro2, ModDst::Lfo1Rate, 0.6f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Motion";
        p.chorus.on = true;
        p.reverb = { true, 0.8f, 0.5f, 0.3f, 20.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Fat Bass
        auto p = make ("Fat Bass", "Bass");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave1 = Wave::Saw; l.analog.sub = 0.6f;
        l.mode = VoiceMode::Legato; l.glide = 0.04f;
        l.filter = FilterType::LowPass24; l.cutoff = 300.0f; l.resonance = 0.35f; l.drive = 0.3f; l.keyTrack = 0.4f;
        l.filterEnv = { 0.001f, 0.25f, 0.1f, 0.1f }; l.filterEnvAmount = 0.45f;
        l.ampEnv = { 0.001f, 0.3f, 0.9f, 0.08f };
        mod (l, 0, ModSrc::Macro2, ModDst::Drive, 0.6f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Grit";
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Acid Line
        auto p = make ("Acid Line", "Bass");
        auto& l = p.layers[0];
        l = analogLayer();
        l.mode = VoiceMode::Legato; l.glide = 0.06f;
        l.filter = FilterType::Ladder; l.cutoff = 400.0f; l.resonance = 0.75f; l.drive = 0.4f;
        l.filterEnv = { 0.001f, 0.18f, 0.0f, 0.1f }; l.filterEnvAmount = 0.5f;
        l.ampEnv = { 0.001f, 0.2f, 0.85f, 0.05f };
        mod (l, 0, ModSrc::Velocity, ModDst::FilterEnvAmount, 0.3f);
        mod (l, 1, ModSrc::Macro2, ModDst::Resonance, 0.25f);
        brightnessMacro (p, 0, 0.6f);
        p.macroNames[1] = "Squelch";
        p.delay = { true, true, 0.75f, 375.0f, 0.4f, 0.2f, 0.5f, true };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Wobble Bass
        auto p = make ("Wobble Bass", "Bass");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave2 = Wave::Square; l.analog.osc2Semi = -12.0f; l.analog.mix = 0.4f;
        l.mode = VoiceMode::Mono;
        l.filter = FilterType::LowPass24; l.cutoff = 250.0f; l.resonance = 0.45f; l.drive = 0.35f;
        l.lfo[0] = { LfoShape::Sine, 2.0f, true, 0.5f, true, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Cutoff, 0.45f);
        mod (l, 1, ModSrc::Macro2, ModDst::Lfo1Rate, 0.5f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Wobble Rate";
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Super Saw Lead
        auto p = make ("Super Saw Lead", "Lead");
        auto& l = p.layers[0];
        l = analogLayer();
        l.unison = 7; l.unisonDetune = 0.35f; l.unisonSpread = 1.0f;
        l.filter = FilterType::LowPass12; l.cutoff = 6000.0f; l.resonance = 0.1f;
        l.ampEnv = { 0.005f, 0.4f, 0.9f, 0.3f };
        mod (l, 0, ModSrc::Macro2, ModDst::Pitch, 0.0f);
        brightnessMacro (p, 0);
        p.delay = { true, true, 0.75f, 375.0f, 0.3f, 0.2f, 0.6f, true };
        p.reverb = { true, 0.6f, 0.5f, 0.2f, 10.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Square Lead
        auto p = make ("Square Lead", "Lead");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave1 = Wave::Square; l.analog.pw1 = 0.35f;
        l.mode = VoiceMode::Legato; l.glide = 0.03f;
        l.filter = FilterType::LowPass24; l.cutoff = 4000.0f; l.resonance = 0.2f;
        l.lfo[0] = { LfoShape::Triangle, 0.4f, false, 1.0f, false, 0.0f };
        l.lfo[1] = { LfoShape::Sine, 5.5f, false, 1.0f, true, 0.5f };
        mod (l, 0, ModSrc::Lfo1, ModDst::PulseWidth, 0.4f);
        mod (l, 1, ModSrc::Lfo2, ModDst::Pitch, 0.006f);
        mod (l, 2, ModSrc::ModWheel, ModDst::Pitch, 0.0f);
        brightnessMacro (p, 0);
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Sync Lead
        auto p = make ("Sync Lead", "Lead");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.sync = true; l.analog.mix = 1.0f; l.analog.osc2Semi = 7.0f;
        l.modEnv = { 0.001f, 0.6f, 0.2f, 0.3f };
        mod (l, 0, ModSrc::ModEnv, ModDst::Osc2Pitch, 0.5f);
        mod (l, 1, ModSrc::Macro2, ModDst::Osc2Pitch, 0.5f);
        l.filter = FilterType::LowPass12; l.cutoff = 7000.0f;
        brightnessMacro (p, 0);
        p.macroNames[1] = "Sync";
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Glass Keys
        auto p = make ("Glass Keys", "Keys");
        auto& l = p.layers[0];
        l.engine = Engine::Wavetable;
        l.wtTable = 6; l.wtPosition = 0.3f;   // Bell
        l.ampEnv = { 0.002f, 1.8f, 0.0f, 0.6f };
        l.modEnv = { 0.001f, 0.8f, 0.0f, 0.5f };
        mod (l, 0, ModSrc::ModEnv, ModDst::WavePosition, 0.4f);
        mod (l, 1, ModSrc::Macro2, ModDst::WavePosition, 0.6f);
        l.filter = FilterType::LowPass12; l.cutoff = 9000.0f;
        brightnessMacro (p, 0);
        p.macroNames[1] = "Shape";
        p.reverb = { true, 0.5f, 0.4f, 0.22f, 10.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Morph Pad
        auto p = make ("Morph Pad", "Pad");
        auto& l = p.layers[0];
        l.engine = Engine::Wavetable;
        l.wtTable = 2;   // Harmonics
        l.unison = 3; l.unisonDetune = 0.2f;
        l.ampEnv = { 1.2f, 1.0f, 0.9f, 2.5f };
        l.lfo[0] = { LfoShape::SmoothRandom, 0.15f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::WavePosition, 0.5f);
        mod (l, 1, ModSrc::Macro2, ModDst::WavePosition, 0.5f);
        l.filter = FilterType::LowPass24; l.cutoff = 5000.0f;
        brightnessMacro (p, 0);
        p.macroNames[1] = "Morph";
        p.chorus.on = true;
        p.reverb = { true, 0.85f, 0.5f, 0.4f, 30.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Vocal Drift
        auto p = make ("Vocal Drift", "Pad");
        auto& l = p.layers[0];
        l.engine = Engine::Wavetable;
        l.wtTable = 3;   // Formant
        l.ampEnv = { 0.4f, 1.0f, 0.8f, 1.2f };
        l.lfo[0] = { LfoShape::Triangle, 0.2f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::WavePosition, 0.45f);
        mod (l, 1, ModSrc::Macro2, ModDst::WavePosition, 0.5f);
        l.filter = FilterType::BandPass; l.cutoff = 1400.0f; l.resonance = 0.3f;
        brightnessMacro (p, 0, 0.4f);
        p.macroNames[1] = "Vowel";
        p.reverb = { true, 0.7f, 0.5f, 0.35f, 20.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- FM E.Piano
        auto p = make ("FM E.Piano", "Keys");
        p.layers[0] = fmLayer (fmEPiano());
        mod (p.layers[0], 0, ModSrc::Macro2, ModDst::FmAmount, 0.6f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Bark";
        p.chorus = { true, 0.5f, 0.4f, 0.3f };
        p.reverb = { true, 0.45f, 0.5f, 0.15f, 10.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- FM Bell
        auto p = make ("FM Bell", "Keys");
        p.layers[0] = fmLayer (fmBell());
        mod (p.layers[0], 0, ModSrc::Macro2, ModDst::FmAmount, 0.8f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Clang";
        p.reverb = { true, 0.75f, 0.4f, 0.3f, 15.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- FM Bass
        auto p = make ("FM Bass", "Bass");
        p.layers[0] = fmLayer (fmBass());
        p.layers[0].mode = VoiceMode::Mono;
        mod (p.layers[0], 0, ModSrc::Macro2, ModDst::FmAmount, 0.8f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Punch";
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- FM Brass
        auto p = make ("FM Brass", "Brass");
        p.layers[0] = fmLayer (fmBrass());
        p.layers[0].lfo[1] = { LfoShape::Sine, 5.0f, false, 1.0f, true, 0.6f };
        mod (p.layers[0], 0, ModSrc::ModWheel, ModDst::Pitch, 0.0f);
        mod (p.layers[0], 1, ModSrc::Lfo2, ModDst::Pitch, 0.005f);
        mod (p.layers[0], 2, ModSrc::Macro2, ModDst::FmAmount, 0.6f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Brass";
        p.reverb = { true, 0.55f, 0.5f, 0.18f, 10.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Nylon Pluck
        auto p = make ("Nylon Pluck", "Pluck");
        auto& l = p.layers[0];
        l.engine = Engine::Pluck;
        l.pluck = { 0.45f, 0.45f, 0.15f, 0.4f };
        l.filter = FilterType::Off;
        l.ampEnv = { 0.001f, 4.0f, 1.0f, 0.4f };
        mod (l, 0, ModSrc::Macro2, ModDst::PluckDamping, -0.5f);
        mod (l, 1, ModSrc::Velocity, ModDst::PluckDamping, -0.3f);
        p.macroNames[1] = "Brightness";
        p.reverb = { true, 0.4f, 0.5f, 0.15f, 10.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Harp
        auto p = make ("Harp", "Pluck");
        auto& l = p.layers[0];
        l.engine = Engine::Pluck;
        l.pluck = { 0.7f, 0.2f, 0.3f, 0.6f };
        l.filter = FilterType::Off;
        l.ampEnv = { 0.001f, 6.0f, 1.0f, 1.2f };
        mod (l, 0, ModSrc::Macro2, ModDst::PluckDamping, -0.4f);
        p.macroNames[1] = "Brightness";
        p.reverb = { true, 0.7f, 0.4f, 0.35f, 20.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Drone Machine
        auto p = make ("Drone Machine", "FX");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.noise = 0.35f; l.analog.osc2Semi = 7.0f; l.analog.mix = 0.4f;
        l.filter = FilterType::Comb; l.cutoff = 220.0f; l.resonance = 0.85f; l.keyTrack = 1.0f;
        l.ampEnv = { 2.0f, 1.0f, 1.0f, 4.0f };
        l.lfo[0] = { LfoShape::SmoothRandom, 0.08f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Cutoff, 0.04f);
        mod (l, 1, ModSrc::Macro2, ModDst::Noise, 0.5f);
        brightnessMacro (p, 0, 0.1f);
        p.macroNames[0] = "Tune";
        p.macroNames[1] = "Air";
        p.reverb = { true, 0.95f, 0.6f, 0.45f, 50.0f, 1.0f };
        p.delay = { true, true, 1.5f, 375.0f, 0.55f, 0.25f, 0.4f, true };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Keys + Pad (layered)
        auto p = make ("Keys + Pad", "Layered");
        p.layers[0] = fmLayer (fmEPiano());
        auto& pad = p.layers[1];
        pad = analogLayer();
        pad.unison = 3; pad.unisonDetune = 0.2f; pad.level = 0.5f;
        pad.filter = FilterType::LowPass24; pad.cutoff = 1800.0f;
        pad.ampEnv = { 0.9f, 1.0f, 0.8f, 2.0f };
        mod (pad, 0, ModSrc::Macro2, ModDst::Level, 1.0f);
        brightnessMacro (p, 0);
        mod (p.layers[1], 7, ModSrc::Macro1, ModDst::Cutoff, 0.5f);
        p.macroNames[1] = "Pad Level";
        p.chorus.on = true;
        p.reverb = { true, 0.7f, 0.5f, 0.3f, 20.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Bass / Keys split
        auto p = make ("Bass | Keys Split", "Layered");
        auto& bass = p.layers[0];
        bass = analogLayer();
        bass.keyHi = 54;
        bass.analog.sub = 0.5f; bass.mode = VoiceMode::Legato; bass.glide = 0.03f;
        bass.filter = FilterType::LowPass24; bass.cutoff = 500.0f; bass.resonance = 0.3f;
        bass.filterEnv = { 0.001f, 0.25f, 0.2f, 0.1f }; bass.filterEnvAmount = 0.35f;
        bass.output = 0;
        auto& keys = p.layers[1];
        keys = fmLayer (fmEPiano());
        keys.keyLo = 55;
        brightnessMacro (p, 0);
        p.macroNames[0] = "Bass Tone";
        p.reverb = { true, 0.45f, 0.5f, 0.15f, 10.0f, 1.0f };
        out.push_back (p);
    }
    { // ---------------------------------------------------------------- Sampler
        auto p = make ("Sampler", "Init");
        auto& l = p.layers[0];
        l.engine = Engine::Sampler;
        l.filter = FilterType::LowPass24; l.cutoff = 20000.0f; l.resonance = 0.0f;
        l.ampEnv = { 0.002f, 0.5f, 1.0f, 0.3f };
        brightnessMacro (p, 0);
        out.push_back (p);
    }
    return out;
}

} // namespace el::chimera
