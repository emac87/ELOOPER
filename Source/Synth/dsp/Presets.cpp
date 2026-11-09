// Chimera factory presets, built in code (no files to ship). Every preset maps Macro 1 to
// brightness so a controller knob is useful straight away; most also use Macro 2.
#include "Synth/dsp/Presets.h"
#include <algorithm>
#include <cstring>

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

    // ---- FM voices for the new presets (original, built from ratios and envelopes)
    Dx7Patch fmSubPulse()
    {
        // Algorithm 1: OP2->OP1 plus OP3 as a clean octave-down carrier (OP4-6 silent).
        return dxVoice ("CHIM SUBFM", 1, 4, {
            op (1, 0, 99, { 99, 45, 30, 75 }, { 99, 88, 80, 0 }, 2, 7),
            op (1, 0, 70, { 99, 70, 45, 75 }, { 99, 30, 10, 0 }, 5, 7),
            op (0, 0, 80, { 99, 40, 30, 75 }, { 99, 90, 85, 0 }, 1, 7),
            silentOp(), silentOp(), silentOp() });
    }

    Dx7Patch fmGlassLead()
    {
        return dxVoice ("CHIM GLASS ", 1, 3, {
            op (1, 0, 99, { 85, 40, 30, 55 }, { 99, 94, 92, 0 }, 2, 7),
            op (2, 0, 66, { 80, 40, 30, 55 }, { 99, 80, 75, 0 }, 4, 7),
            op (1, 0, 86, { 85, 40, 30, 55 }, { 99, 94, 92, 0 }, 2, 10),
            silentOp(), silentOp(), silentOp() });
    }

    Dx7Patch fmDreamTine()
    {
        return dxVoice ("CHIM TINE  ", 5, 0, {
            op (1, 0, 99, { 97, 28, 18, 55 }, { 99, 70, 0, 0 }, 2, 7),
            op (1, 0, 54, { 96, 40, 25, 60 }, { 99, 45, 0, 0 }, 5, 7),
            op (1, 0, 88, { 97, 24, 16, 55 }, { 99, 72, 0, 0 }, 2, 9),
            op (14, 0, 44, { 99, 85, 30, 80 }, { 99, 0, 0, 0 }, 6, 7),
            silentOp(), silentOp() });
    }

    Dx7Patch fmVibes()
    {
        return dxVoice ("CHIM VIBES ", 5, 0, {
            op (1, 0, 99, { 98, 22, 14, 45 }, { 99, 80, 0, 0 }, 2, 7),
            op (4, 0, 50, { 98, 55, 35, 60 }, { 99, 25, 0, 0 }, 5, 7),
            op (1, 0, 70, { 98, 26, 16, 45 }, { 99, 70, 0, 0 }, 2, 6),
            op (10, 0, 36, { 99, 80, 40, 70 }, { 99, 0, 0, 0 }, 6, 7),
            silentOp(), silentOp() });
    }

    Dx7Patch fmMarimba()
    {
        return dxVoice ("CHIM MARIM ", 5, 0, {
            op (1, 0, 99, { 99, 48, 36, 70 }, { 99, 55, 0, 0 }, 2, 7),
            op (4, 0, 62, { 99, 68, 45, 80 }, { 99, 20, 0, 0 }, 5, 7),
            op (0, 0, 70, { 99, 55, 40, 75 }, { 99, 40, 0, 0 }, 2, 7),
            op (10, 0, 40, { 99, 85, 50, 90 }, { 99, 0, 0, 0 }, 5, 7),
            silentOp(), silentOp() });
    }

    Dx7Patch fmTubular()
    {
        return dxVoice ("CHIM TUBE  ", 5, 1, {
            op (1, 0, 99, { 99, 24, 14, 38 }, { 99, 72, 0, 0 }, 2, 7),
            op (3, 50, 66, { 99, 30, 16, 38 }, { 99, 50, 0, 0 }, 4, 7),
            op (2, 76, 82, { 99, 22, 12, 36 }, { 99, 65, 0, 0 }, 2, 10),
            op (5, 40, 58, { 99, 32, 18, 40 }, { 99, 35, 0, 0 }, 4, 7),
            op (1, 0, 55, { 99, 28, 15, 38 }, { 99, 55, 0, 0 }, 2, 4),
            op (6, 12, 30, { 99, 40, 20, 45 }, { 99, 20, 0, 0 }, 4, 7) });
    }

    Dx7Patch fmAnvil()
    {
        return dxVoice ("CHIM ANVIL ", 5, 7, {
            op (1, 41, 99, { 99, 50, 32, 60 }, { 99, 60, 0, 0 }, 3, 7),
            op (2, 77, 84, { 99, 55, 35, 60 }, { 99, 40, 0, 0 }, 5, 7),
            op (1, 0, 80, { 99, 45, 30, 60 }, { 99, 55, 0, 0 }, 3, 3),
            op (6, 11, 78, { 99, 62, 40, 70 }, { 99, 30, 0, 0 }, 5, 7),
            op (3, 3, 60, { 99, 55, 35, 60 }, { 99, 45, 0, 0 }, 3, 11),
            op (7, 5, 58, { 99, 70, 45, 75 }, { 99, 20, 0, 0 }, 5, 7) });
    }

    Dx7Patch fmStab()
    {
        return dxVoice ("CHIM STAB ", 22, 7, {
            op (1, 0, 99, { 95, 55, 30, 60 }, { 99, 80, 45, 0 }, 1, 7),
            op (1, 0, 82, { 92, 50, 28, 60 }, { 99, 70, 35, 0 }, 3, 7),
            op (1, 0, 94, { 95, 55, 30, 60 }, { 99, 80, 45, 0 }, 1, 8),
            op (1, 0, 94, { 95, 55, 30, 60 }, { 99, 80, 45, 0 }, 1, 6),
            op (1, 0, 90, { 95, 55, 30, 60 }, { 99, 80, 45, 0 }, 1, 9),
            op (1, 0, 78, { 90, 50, 28, 60 }, { 99, 75, 40, 0 }, 3, 7) });
    }

    Dx7Patch fmKalimba()
    {
        return dxVoice ("CHIM KALIM ", 5, 0, {
            op (1, 0, 99, { 99, 40, 24, 55 }, { 99, 45, 0, 0 }, 2, 7),
            op (5, 40, 58, { 99, 82, 60, 90 }, { 99, 0, 0, 0 }, 5, 7),
            silentOp(), silentOp(), silentOp(), silentOp() });
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
        auto p = make ("FM Bell", "FM");
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
        auto p = make ("FM Brass", "FM");
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
        auto p = make ("Drone Machine", "Texture");
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
    // ================================================================ Bass
    { // Deep Sub
        auto p = make ("Deep Sub", "Bass");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave1 = Wave::Sine; l.analog.wave2 = Wave::Saw; l.analog.mix = 0.0f; l.analog.sub = 0.3f;
        l.mode = VoiceMode::Mono;
        l.filter = FilterType::LowPass24; l.cutoff = 500.0f; l.resonance = 0.1f; l.drive = 0.1f; l.keyTrack = 0.3f;
        l.ampEnv = { 0.004f, 0.4f, 0.95f, 0.15f };
        mod (l, 0, ModSrc::Macro2, ModDst::OscMix, 0.35f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Bite";
        out.push_back (p);
    }
    { // Reese Rumble
        auto p = make ("Reese Rumble", "Bass");
        auto& l = p.layers[0];
        l = analogLayer();
        l.level = 0.6f;
        l.analog.wave2 = Wave::Saw; l.analog.osc2Fine = -11.0f; l.analog.mix = 0.5f; l.analog.sub = 0.25f;
        l.unison = 3; l.unisonDetune = 0.3f; l.unisonSpread = 0.5f;
        l.mode = VoiceMode::Legato; l.glide = 0.05f;
        l.filter = FilterType::LowPass24; l.cutoff = 650.0f; l.resonance = 0.2f; l.drive = 0.25f; l.keyTrack = 0.3f;
        l.ampEnv = { 0.01f, 0.5f, 0.9f, 0.2f };
        l.lfo[0] = { LfoShape::Sine, 0.35f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Cutoff, 0.1f);
        mod (l, 1, ModSrc::Macro2, ModDst::Drive, 0.5f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Grind";
        out.push_back (p);
    }
    { // Sub Pulse FM
        auto p = make ("Sub Pulse FM", "Bass");
        p.layers[0] = fmLayer (fmSubPulse());
        p.layers[0].mode = VoiceMode::Mono;
        p.masterVolume = 0.85f;
        mod (p.layers[0], 0, ModSrc::Macro2, ModDst::FmAmount, 0.7f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Growl";
        out.push_back (p);
    }
    { // Rubber Pluck
        auto p = make ("Rubber Pluck", "Bass");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave2 = Wave::Square; l.analog.mix = 0.35f; l.analog.osc2Semi = -12.0f;
        l.mode = VoiceMode::Mono;
        l.filter = FilterType::LowPass24; l.cutoff = 220.0f; l.resonance = 0.3f; l.drive = 0.2f; l.keyTrack = 0.5f;
        l.filterEnv = { 0.001f, 0.16f, 0.0f, 0.1f }; l.filterEnvAmount = 0.5f;
        l.ampEnv = { 0.001f, 0.45f, 0.35f, 0.12f };
        mod (l, 0, ModSrc::Velocity, ModDst::FilterEnvAmount, 0.25f);
        mod (l, 1, ModSrc::Macro2, ModDst::FilterEnvAmount, 0.4f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Snap";
        out.push_back (p);
    }
    { // Squelch Box
        auto p = make ("Squelch Box", "Bass");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave1 = Wave::Square; l.analog.pw1 = 0.5f;
        l.mode = VoiceMode::Legato; l.glide = 0.05f;
        l.filter = FilterType::Ladder; l.cutoff = 300.0f; l.resonance = 0.7f; l.drive = 0.5f;
        l.filterEnv = { 0.001f, 0.22f, 0.05f, 0.1f }; l.filterEnvAmount = 0.6f;
        l.ampEnv = { 0.001f, 0.3f, 0.8f, 0.06f };
        mod (l, 0, ModSrc::Velocity, ModDst::FilterEnvAmount, 0.35f);
        mod (l, 1, ModSrc::Macro2, ModDst::Resonance, 0.25f);
        mod (l, 2, ModSrc::Macro3, ModDst::FilterEnvAmount, 0.3f);
        brightnessMacro (p, 0, 0.5f);
        p.macroNames[1] = "Squeal";
        p.macroNames[2] = "Accent";
        p.delay = { true, true, 0.75f, 375.0f, 0.3f, 0.15f, 0.5f, true };
        out.push_back (p);
    }

    // ================================================================ Lead
    { // Glide Lead
        auto p = make ("Glide Lead", "Lead");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave2 = Wave::Square; l.analog.osc2Fine = 6.0f; l.analog.mix = 0.4f;
        l.mode = VoiceMode::Legato; l.glide = 0.09f;
        l.filter = FilterType::LowPass24; l.cutoff = 3200.0f; l.resonance = 0.25f; l.keyTrack = 0.4f;
        l.ampEnv = { 0.005f, 0.3f, 0.85f, 0.2f };
        l.lfo[0] = { LfoShape::Sine, 5.5f, false, 1.0f, true, 0.5f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Pitch, 0.005f);
        mod (l, 1, ModSrc::ModWheel, ModDst::Cutoff, 0.2f);
        brightnessMacro (p, 0);
        p.delay = { true, true, 0.75f, 375.0f, 0.3f, 0.15f, 0.55f, true };
        p.reverb = { true, 0.45f, 0.5f, 0.15f, 10.0f, 1.0f };
        out.push_back (p);
    }
    { // Stadium Saw
        auto p = make ("Stadium Saw", "Lead");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave2 = Wave::Square; l.analog.osc2Semi = 12.0f; l.analog.mix = 0.2f;
        l.unison = 5; l.unisonDetune = 0.28f; l.unisonSpread = 0.9f;
        l.filter = FilterType::LowPass12; l.cutoff = 5000.0f; l.resonance = 0.12f; l.keyTrack = 0.3f;
        l.ampEnv = { 0.004f, 0.5f, 0.85f, 0.35f };
        mod (l, 0, ModSrc::Velocity, ModDst::Cutoff, 0.15f);
        brightnessMacro (p, 0);
        p.chorus = { true, 0.3f, 0.4f, 0.2f };
        p.reverb = { true, 0.6f, 0.5f, 0.22f, 15.0f, 1.0f };
        out.push_back (p);
    }
    { // PWM Cry
        auto p = make ("PWM Cry", "Lead");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave1 = Wave::Square; l.analog.pw1 = 0.3f;
        l.analog.wave2 = Wave::Square; l.analog.pw2 = 0.7f; l.analog.osc2Fine = 5.0f; l.analog.mix = 0.5f;
        l.unison = 2; l.unisonDetune = 0.15f; l.unisonSpread = 0.7f;
        l.mode = VoiceMode::Legato; l.glide = 0.02f;
        l.filter = FilterType::LowPass12; l.cutoff = 3800.0f; l.resonance = 0.15f;
        l.ampEnv = { 0.008f, 0.4f, 0.85f, 0.25f };
        l.lfo[0] = { LfoShape::Triangle, 0.7f, false, 1.0f, false, 0.0f };
        l.lfo[1] = { LfoShape::Sine, 5.0f, false, 1.0f, true, 0.6f };
        mod (l, 0, ModSrc::Lfo1, ModDst::PulseWidth, 0.5f);
        mod (l, 1, ModSrc::Lfo2, ModDst::Pitch, 0.004f);
        mod (l, 2, ModSrc::Macro2, ModDst::PulseWidth, 0.3f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Width";
        p.chorus.on = true;
        p.reverb = { true, 0.5f, 0.5f, 0.15f, 10.0f, 1.0f };
        out.push_back (p);
    }
    { // Digital Edge
        auto p = make ("Digital Edge", "Lead");
        auto& l = p.layers[0];
        l.engine = Engine::Wavetable;
        l.wtTable = 4; l.wtPosition = 0.35f;   // Digital
        l.unison = 3; l.unisonDetune = 0.15f; l.unisonSpread = 0.8f;
        l.filter = FilterType::LowPass12; l.cutoff = 7500.0f; l.resonance = 0.15f;
        l.ampEnv = { 0.004f, 0.4f, 0.85f, 0.25f };
        l.modEnv = { 0.001f, 0.5f, 0.0f, 0.3f };
        mod (l, 0, ModSrc::ModEnv, ModDst::WavePosition, 0.3f);
        mod (l, 1, ModSrc::Macro2, ModDst::WavePosition, 0.5f);
        mod (l, 2, ModSrc::Velocity, ModDst::WavePosition, 0.15f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Morph";
        p.delay = { true, true, 0.5f, 250.0f, 0.3f, 0.18f, 0.6f, true };
        p.reverb = { true, 0.5f, 0.5f, 0.15f, 10.0f, 1.0f };
        out.push_back (p);
    }
    { // Crystal Lead (FM)
        auto p = make ("Crystal Lead", "Lead");
        auto& l = p.layers[0];
        l = fmLayer (fmGlassLead());
        l.mode = VoiceMode::Legato; l.glide = 0.06f;
        l.lfo[0] = { LfoShape::Sine, 5.2f, false, 1.0f, true, 0.6f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Pitch, 0.004f);
        mod (l, 1, ModSrc::Macro2, ModDst::FmAmount, 0.7f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Shine";
        p.delay = { true, true, 0.75f, 375.0f, 0.3f, 0.15f, 0.6f, true };
        p.reverb = { true, 0.5f, 0.5f, 0.15f, 10.0f, 1.0f };
        out.push_back (p);
    }

    // ================================================================ Pad
    { // Silk Strings
        auto p = make ("Silk Strings", "Pad");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave2 = Wave::Saw; l.analog.osc2Fine = -6.0f; l.analog.mix = 0.5f;
        l.unison = 5; l.unisonDetune = 0.2f; l.unisonSpread = 0.9f;
        l.filter = FilterType::LowPass12; l.cutoff = 3000.0f; l.resonance = 0.1f; l.keyTrack = 0.5f;
        l.ampEnv = { 0.45f, 0.8f, 0.9f, 1.0f };
        l.lfo[0] = { LfoShape::Sine, 5.0f, false, 1.0f, true, 1.5f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Pitch, 0.0015f);
        mod (l, 1, ModSrc::Aftertouch, ModDst::Cutoff, 0.2f);
        mod (l, 2, ModSrc::Macro2, ModDst::Pitch, 0.0f);
        brightnessMacro (p, 0);
        p.chorus = { true, 0.35f, 0.5f, 0.3f };
        p.reverb = { true, 0.75f, 0.5f, 0.3f, 20.0f, 1.0f };
        out.push_back (p);
    }
    { // Tidal Table
        auto p = make ("Tidal Table", "Pad");
        auto& l = p.layers[0];
        l.engine = Engine::Wavetable;
        l.wtTable = 7; l.wtPosition = 0.2f;   // Sync Sweep
        l.unison = 3; l.unisonDetune = 0.18f; l.unisonSpread = 1.0f;
        l.filter = FilterType::LowPass24; l.cutoff = 4500.0f; l.resonance = 0.1f;
        l.ampEnv = { 0.9f, 1.0f, 0.9f, 2.5f };
        l.lfo[0] = { LfoShape::SmoothRandom, 0.1f, false, 1.0f, false, 0.0f };
        l.lfo[1] = { LfoShape::Triangle, 0.07f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::WavePosition, 0.6f);
        mod (l, 1, ModSrc::Lfo2, ModDst::Cutoff, 0.1f);
        mod (l, 2, ModSrc::Macro2, ModDst::WavePosition, 0.4f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Tide";
        p.chorus.on = true;
        p.reverb = { true, 0.85f, 0.5f, 0.35f, 30.0f, 1.0f };
        out.push_back (p);
    }
    { // Choir Loft
        auto p = make ("Choir Loft", "Pad");
        auto& l = p.layers[0];
        l.engine = Engine::Wavetable;
        l.level = 0.95f; p.masterVolume = 0.9f;
        l.wtTable = 3; l.wtPosition = 0.5f;   // Formant
        l.unison = 3; l.unisonDetune = 0.15f; l.unisonSpread = 1.0f;
        l.filter = FilterType::LowPass12; l.cutoff = 4500.0f; l.resonance = 0.1f;
        l.ampEnv = { 0.7f, 1.0f, 0.85f, 1.4f };
        l.lfo[0] = { LfoShape::Sine, 0.25f, false, 1.0f, false, 0.0f };
        l.lfo[1] = { LfoShape::Sine, 4.8f, false, 1.0f, true, 1.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::WavePosition, 0.2f);
        mod (l, 1, ModSrc::Macro2, ModDst::WavePosition, 0.5f);
        mod (l, 2, ModSrc::Lfo2, ModDst::Pitch, 0.0015f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Vowel";
        p.chorus.on = true;
        p.reverb = { true, 0.85f, 0.4f, 0.4f, 25.0f, 1.0f };
        out.push_back (p);
    }
    { // Abyss
        auto p = make ("Abyss", "Pad");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave2 = Wave::Saw; l.analog.osc2Semi = -12.0f; l.analog.mix = 0.5f; l.analog.noise = 0.04f;
        l.unison = 3; l.unisonDetune = 0.4f; l.unisonSpread = 1.0f;
        l.filter = FilterType::LowPass24; l.cutoff = 550.0f; l.resonance = 0.25f; l.keyTrack = 0.4f;
        l.ampEnv = { 0.9f, 1.0f, 1.0f, 3.0f };
        l.lfo[0] = { LfoShape::Sine, 0.1f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Cutoff, 0.15f);
        mod (l, 1, ModSrc::Macro2, ModDst::Noise, 0.3f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Dust";
        p.reverb = { true, 0.92f, 0.6f, 0.4f, 40.0f, 1.0f };
        out.push_back (p);
    }
    { // Shimmer Veil
        auto p = make ("Shimmer Veil", "Pad");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave1 = Wave::Sine; l.analog.wave2 = Wave::Triangle; l.analog.osc2Semi = 12.0f; l.analog.mix = 0.4f;
        l.unison = 5; l.unisonDetune = 0.12f; l.unisonSpread = 1.0f;
        l.filter = FilterType::LowPass12; l.cutoff = 7000.0f; l.resonance = 0.05f;
        l.ampEnv = { 0.8f, 1.0f, 0.9f, 3.0f };
        l.lfo[0] = { LfoShape::Triangle, 0.12f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::OscMix, 0.25f);
        mod (l, 1, ModSrc::Macro2, ModDst::OscMix, 0.4f);
        brightnessMacro (p, 0, 0.4f);
        p.macroNames[1] = "Shimmer";
        p.chorus = { true, 0.3f, 0.6f, 0.3f };
        p.delay = { true, true, 1.5f, 375.0f, 0.5f, 0.2f, 0.45f, true };
        p.reverb = { true, 0.97f, 0.3f, 0.55f, 30.0f, 1.0f };
        out.push_back (p);
    }

    // ================================================================ Keys
    { // Dream Tine
        auto p = make ("Dream Tine", "Keys");
        p.layers[0] = fmLayer (fmDreamTine());
        mod (p.layers[0], 0, ModSrc::Macro2, ModDst::FmAmount, 0.6f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Tine";
        p.chorus = { true, 0.4f, 0.5f, 0.35f };
        p.reverb = { true, 0.55f, 0.5f, 0.2f, 12.0f, 1.0f };
        out.push_back (p);
    }
    { // Drawbar Organ
        auto p = make ("Drawbar Organ", "Keys");
        auto& l = p.layers[0];
        l.engine = Engine::Wavetable;
        l.wtTable = 5; l.wtPosition = 0.5f;   // Organ
        l.filter = FilterType::LowPass12; l.cutoff = 9000.0f; l.keyTrack = 0.3f;
        l.ampEnv = { 0.004f, 0.1f, 1.0f, 0.08f };
        l.lfo[0] = { LfoShape::Sine, 6.0f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Pitch, 0.0012f);
        mod (l, 1, ModSrc::Lfo1, ModDst::Level, 0.08f);
        mod (l, 2, ModSrc::Macro2, ModDst::WavePosition, 0.5f);
        brightnessMacro (p, 0, 0.3f);
        p.macroNames[1] = "Drawbars";
        p.chorus = { true, 0.8f, 0.4f, 0.3f };
        p.reverb = { true, 0.4f, 0.5f, 0.12f, 8.0f, 1.0f };
        out.push_back (p);
    }
    { // Clav Funk
        auto p = make ("Clav Funk", "Keys");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave1 = Wave::Square; l.analog.pw1 = 0.2f; l.analog.wave2 = Wave::Saw; l.analog.mix = 0.25f;
        l.filter = FilterType::LowPass24; l.cutoff = 900.0f; l.resonance = 0.2f; l.keyTrack = 0.8f;
        l.filterEnv = { 0.001f, 0.14f, 0.0f, 0.08f }; l.filterEnvAmount = 0.45f;
        l.ampEnv = { 0.001f, 0.3f, 0.3f, 0.07f };
        mod (l, 0, ModSrc::Velocity, ModDst::Cutoff, 0.15f);
        mod (l, 1, ModSrc::Macro2, ModDst::FilterEnvAmount, 0.4f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Pick";
        p.reverb = { true, 0.3f, 0.5f, 0.1f, 5.0f, 1.0f };
        out.push_back (p);
    }
    { // Vibe Mallets
        auto p = make ("Vibe Mallets", "Keys");
        auto& l = p.layers[0];
        l = fmLayer (fmVibes());
        l.lfo[0] = { LfoShape::Sine, 4.5f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Level, 0.25f);
        mod (l, 1, ModSrc::Macro2, ModDst::FmAmount, 0.6f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Mallet";
        p.chorus = { true, 0.3f, 0.3f, 0.2f };
        p.reverb = { true, 0.6f, 0.4f, 0.25f, 12.0f, 1.0f };
        out.push_back (p);
    }

    // ================================================================ FM
    { // Wood Marimba
        auto p = make ("Wood Marimba", "FM");
        p.layers[0] = fmLayer (fmMarimba());
        mod (p.layers[0], 0, ModSrc::Macro2, ModDst::FmAmount, 0.7f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Mallet";
        p.reverb = { true, 0.45f, 0.5f, 0.2f, 8.0f, 1.0f };
        out.push_back (p);
    }
    { // Tubular Hall
        auto p = make ("Tubular Hall", "FM");
        p.layers[0] = fmLayer (fmTubular());
        mod (p.layers[0], 0, ModSrc::Macro2, ModDst::FmAmount, 0.7f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Strike";
        p.reverb = { true, 0.85f, 0.35f, 0.35f, 20.0f, 1.0f };
        out.push_back (p);
    }
    { // Metal Anvil
        auto p = make ("Metal Anvil", "FM");
        p.layers[0] = fmLayer (fmAnvil());
        p.layers[0].velocitySens = 0.4f;
        p.masterVolume = 0.95f;
        mod (p.layers[0], 0, ModSrc::Macro2, ModDst::FmAmount, 0.8f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Clang";
        p.reverb = { true, 0.5f, 0.4f, 0.2f, 8.0f, 1.0f };
        out.push_back (p);
    }
    { // Brass Stab
        auto p = make ("Brass Stab", "FM");
        p.layers[0] = fmLayer (fmStab());
        mod (p.layers[0], 0, ModSrc::Macro2, ModDst::FmAmount, 0.6f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Blare";
        p.reverb = { true, 0.45f, 0.5f, 0.15f, 8.0f, 1.0f };
        out.push_back (p);
    }

    // ================================================================ Pluck
    { // Koto Strum
        auto p = make ("Koto Strum", "Pluck");
        auto& l = p.layers[0];
        l.engine = Engine::Pluck;
        l.pluck = { 0.6f, 0.3f, 0.12f, 0.8f };
        l.filter = FilterType::Off;
        l.ampEnv = { 0.001f, 5.0f, 1.0f, 0.5f };
        l.modEnv = { 0.001f, 0.07f, 0.0f, 0.1f };
        mod (l, 0, ModSrc::ModEnv, ModDst::Pitch, 0.03f);
        mod (l, 1, ModSrc::Macro2, ModDst::PluckDamping, -0.3f);
        mod (l, 2, ModSrc::Velocity, ModDst::PluckDamping, -0.25f);
        p.macroNames[1] = "Brightness";
        p.reverb = { true, 0.6f, 0.5f, 0.25f, 12.0f, 1.0f };
        out.push_back (p);
    }
    { // Kalimba Dust
        auto p = make ("Kalimba Dust", "Pluck");
        auto& l = p.layers[0];
        l = fmLayer (fmKalimba());
        mod (l, 0, ModSrc::Macro2, ModDst::FmAmount, 0.7f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Pluck";
        p.delay = { true, true, 0.75f, 375.0f, 0.35f, 0.15f, 0.5f, true };
        p.reverb = { true, 0.55f, 0.5f, 0.25f, 10.0f, 1.0f };
        out.push_back (p);
    }
    { // Neon Pluck
        auto p = make ("Neon Pluck", "Pluck");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave2 = Wave::Square; l.analog.mix = 0.3f; l.analog.osc2Fine = 8.0f;
        l.unison = 2; l.unisonDetune = 0.15f; l.unisonSpread = 0.7f;
        l.filter = FilterType::LowPass24; l.cutoff = 500.0f; l.resonance = 0.3f; l.keyTrack = 0.6f;
        l.filterEnv = { 0.001f, 0.2f, 0.0f, 0.1f }; l.filterEnvAmount = 0.55f;
        l.ampEnv = { 0.001f, 0.45f, 0.0f, 0.2f };
        mod (l, 0, ModSrc::Velocity, ModDst::FilterEnvAmount, 0.25f);
        mod (l, 1, ModSrc::Macro2, ModDst::FilterEnvAmount, 0.35f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Snap";
        p.delay = { true, true, 0.75f, 375.0f, 0.4f, 0.22f, 0.55f, true };
        p.reverb = { true, 0.5f, 0.5f, 0.18f, 10.0f, 1.0f };
        out.push_back (p);
    }

    // ================================================================ Texture
    { // Wind Sweep
        auto p = make ("Wind Sweep", "Texture");
        auto& l = p.layers[0];
        l = analogLayer();
        l.level = 1.0f; p.masterVolume = 1.0f;
        l.analog.wave1 = Wave::Noise; l.analog.mix = 0.0f;
        l.unison = 3; l.unisonSpread = 1.0f;
        l.filter = FilterType::BandPass; l.cutoff = 900.0f; l.resonance = 0.55f;
        l.ampEnv = { 0.9f, 1.0f, 1.0f, 2.0f };
        l.lfo[0] = { LfoShape::SmoothRandom, 0.18f, false, 1.0f, false, 0.0f };
        l.lfo[1] = { LfoShape::Triangle, 0.09f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Cutoff, 0.25f);
        mod (l, 1, ModSrc::Lfo2, ModDst::Cutoff, 0.15f);
        mod (l, 2, ModSrc::Macro2, ModDst::Resonance, 0.35f);
        brightnessMacro (p, 0, 0.4f);
        p.macroNames[1] = "Whistle";
        p.reverb = { true, 0.85f, 0.4f, 0.4f, 30.0f, 1.0f };
        out.push_back (p);
    }
    { // Bleep Garden
        auto p = make ("Bleep Garden", "Texture");
        auto& l = p.layers[0];
        l = analogLayer();
        l.level = 0.5f;
        l.analog.wave1 = Wave::Square; l.analog.wave2 = Wave::Triangle; l.analog.osc2Semi = 12.0f; l.analog.mix = 0.3f;
        l.filter = FilterType::LowPass12; l.cutoff = 3000.0f; l.resonance = 0.3f;
        l.ampEnv = { 0.002f, 0.3f, 1.0f, 0.2f };
        l.lfo[0] = { LfoShape::SampleHold, 1.0f, true, 0.25f, true, 0.0f };
        l.lfo[1] = { LfoShape::SawDown, 1.0f, true, 0.25f, true, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Pitch, 0.25f);
        mod (l, 1, ModSrc::Lfo1, ModDst::Cutoff, 0.15f);
        mod (l, 2, ModSrc::Lfo2, ModDst::Level, 0.5f);
        mod (l, 3, ModSrc::Macro2, ModDst::Lfo1Rate, 0.3f);
        brightnessMacro (p, 0);
        p.macroNames[1] = "Chatter";
        p.delay = { true, true, 0.75f, 375.0f, 0.5f, 0.3f, 0.5f, true };
        p.reverb = { true, 0.6f, 0.5f, 0.25f, 15.0f, 1.0f };
        out.push_back (p);
    }
    { // Comb Cathedral
        auto p = make ("Comb Cathedral", "Texture");
        auto& l = p.layers[0];
        l = analogLayer();
        l.analog.wave2 = Wave::Saw; l.analog.osc2Semi = 7.0f; l.analog.mix = 0.35f; l.analog.noise = 0.15f;
        l.unison = 2; l.unisonDetune = 0.3f; l.unisonSpread = 1.0f;
        l.filter = FilterType::Comb; l.cutoff = 330.0f; l.resonance = 0.8f; l.keyTrack = 1.0f;
        l.ampEnv = { 0.8f, 1.0f, 1.0f, 3.0f };
        l.lfo[0] = { LfoShape::SmoothRandom, 0.1f, false, 1.0f, false, 0.0f };
        l.lfo[1] = { LfoShape::Triangle, 0.05f, false, 1.0f, false, 0.0f };
        mod (l, 0, ModSrc::Lfo1, ModDst::Cutoff, 0.05f);
        mod (l, 1, ModSrc::Lfo2, ModDst::OscMix, 0.3f);
        mod (l, 2, ModSrc::Macro2, ModDst::Noise, 0.4f);
        brightnessMacro (p, 0, 0.15f);
        p.macroNames[0] = "Tune";
        p.macroNames[1] = "Breath";
        p.reverb = { true, 0.92f, 0.45f, 0.45f, 40.0f, 1.0f };
        out.push_back (p);
    }

    // ================================================================ Layered
    { // Pluck Over Pad
        auto p = make ("Pluck Over Pad", "Layered");
        auto& pl = p.layers[0];
        pl.engine = Engine::Pluck;
        pl.pluck = { 0.55f, 0.4f, 0.15f, 0.6f };
        pl.filter = FilterType::Off;
        pl.ampEnv = { 0.001f, 4.0f, 1.0f, 0.5f };
        mod (pl, 0, ModSrc::Macro2, ModDst::PluckDamping, -0.4f);
        auto& pad = p.layers[1];
        pad = analogLayer();
        pad.analog.wave2 = Wave::Saw; pad.analog.osc2Fine = 7.0f; pad.analog.mix = 0.5f;
        pad.unison = 3; pad.unisonDetune = 0.22f; pad.unisonSpread = 0.9f; pad.level = 0.5f;
        pad.filter = FilterType::LowPass24; pad.cutoff = 2000.0f; pad.resonance = 0.15f;
        pad.ampEnv = { 0.7f, 1.0f, 0.85f, 1.8f };
        mod (pad, 0, ModSrc::Macro3, ModDst::Level, 1.0f);
        mod (pad, 7, ModSrc::Macro1, ModDst::Cutoff, 0.5f);
        p.macroNames[0] = "Brightness";
        p.macroNames[1] = "Pluck Tone";
        p.macroNames[2] = "Pad Level";
        p.chorus.on = true;
        p.reverb = { true, 0.8f, 0.5f, 0.3f, 20.0f, 1.0f };
        out.push_back (p);
    }
    { // Bass + Lead Split
        auto p = make ("Bass Lead Split", "Layered");
        auto& bass = p.layers[0];
        bass = analogLayer();
        bass.keyHi = 53;
        bass.analog.wave2 = Wave::Square; bass.analog.osc2Semi = -12.0f; bass.analog.mix = 0.3f; bass.analog.sub = 0.3f;
        bass.mode = VoiceMode::Legato; bass.glide = 0.03f;
        bass.filter = FilterType::LowPass24; bass.cutoff = 450.0f; bass.resonance = 0.3f; bass.drive = 0.15f;
        bass.filterEnv = { 0.001f, 0.25f, 0.15f, 0.1f }; bass.filterEnvAmount = 0.4f;
        bass.ampEnv = { 0.002f, 0.3f, 0.9f, 0.1f };
        auto& lead = p.layers[1];
        lead = analogLayer();
        lead.keyLo = 54;
        lead.analog.wave2 = Wave::Square; lead.analog.osc2Fine = 6.0f; lead.analog.mix = 0.4f;
        lead.unison = 3; lead.unisonDetune = 0.2f; lead.unisonSpread = 0.8f; lead.level = 0.65f;
        lead.mode = VoiceMode::Legato; lead.glide = 0.06f;
        lead.filter = FilterType::LowPass24; lead.cutoff = 3500.0f; lead.resonance = 0.2f; lead.keyTrack = 0.4f;
        lead.ampEnv = { 0.005f, 0.3f, 0.85f, 0.25f };
        lead.lfo[0] = { LfoShape::Sine, 5.5f, false, 1.0f, true, 0.5f };
        mod (lead, 0, ModSrc::Lfo1, ModDst::Pitch, 0.004f);
        mod (lead, 7, ModSrc::Macro1, ModDst::Cutoff, 0.5f);
        mod (bass, 7, ModSrc::Macro1, ModDst::Cutoff, 0.4f);
        p.macroNames[0] = "Brightness";
        p.delay = { true, true, 0.75f, 375.0f, 0.3f, 0.12f, 0.55f, true };
        p.reverb = { true, 0.45f, 0.5f, 0.15f, 10.0f, 1.0f };
        out.push_back (p);
    }

    {   // Group by category in browser order (stable, so each group keeps its authored order)
        static const char* const order[] = { "Init", "Bass", "Lead", "Pad", "Keys", "FM", "Pluck", "Texture", "Layered" };
        auto rank = [] (const std::string& c) {
            for (int i = 0; i < 9; ++i) if (c == order[i]) return i;
            return 9;
        };
        std::stable_sort (out.begin(), out.end(), [&] (const Patch& a, const Patch& b) { return rank (a.category) < rank (b.category); });
    }

    return out;
}

} // namespace el::chimera
