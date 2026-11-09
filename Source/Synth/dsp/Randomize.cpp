// Chimera patch randomiser: musical, bounded, deterministic. A patch picks one archetype
// (pluck / pad / keys / lead / bass) that drives envelopes, filter range and layer stacking, then
// fills each layer from the chosen engine with sensible ranges. All output is finite and level-matched.
#include "Synth/dsp/Presets.h"
#include <algorithm>
#include <cstdio>

namespace el::chimera {

namespace {
    enum class Arch { Pluck, Pad, Keys, Lead, Bass };

    struct R {
        Rng g;
        float uni() { return g.uni(); }
        float range (float lo, float hi) { return lo + (hi - lo) * g.uni(); }
        float logRange (float lo, float hi) { return lo * std::pow (hi / lo, g.uni()); }
        bool chance (float p) { return g.uni() < p; }
        int pick (int n) { return (int) (g.nextU() % (uint32_t) n); }
    };

    void mod (LayerParams& l, int slot, ModSrc s, ModDst d, float amount) { l.mods[slot] = { s, d, amount }; }

    EnvParams ampFor (Arch a, R& r)
    {
        switch (a) {
            case Arch::Pluck: return { r.range (0.001f, 0.005f), r.range (0.15f, 0.6f), r.range (0.0f, 0.12f), r.range (0.1f, 0.4f) };
            case Arch::Pad:   return { r.range (0.15f, 0.7f),    r.range (0.5f, 1.5f),  r.range (0.7f, 1.0f),  r.range (0.8f, 2.5f) };
            case Arch::Keys:  return { r.range (0.002f, 0.01f),  r.range (0.4f, 1.5f),  r.range (0.3f, 0.7f),  r.range (0.2f, 0.8f) };
            case Arch::Lead:  return { r.range (0.002f, 0.03f),  r.range (0.2f, 0.6f),  r.range (0.7f, 1.0f),  r.range (0.1f, 0.4f) };
            case Arch::Bass:  return { r.range (0.001f, 0.01f),  r.range (0.15f, 0.5f), r.range (0.5f, 0.9f),  r.range (0.06f, 0.2f) };
        }
        return {};
    }

    // A generated FM voice built from sane operator settings: pick an algorithm, then give
    // carriers full-ish levels with a release and modulators a decaying, velocity-sensitive level.
    Dx7Patch generatedVoice (Arch a, R& r)
    {
        // algorithms (1-based) with a clear carrier / modulator split
        static const int algs[] = { 1, 2, 3, 4, 5, 7, 8, 14, 15, 16, 18, 19, 22, 28 };
        static const uint8_t carrierMask[] = { 0b000101, 0b000101, 0b001001, 0b001001, 0b010101, 0b000101, 0b000101,
                                               0b000101, 0b000101, 0b000001, 0b000001, 0b011001, 0b011101, 0b100101 };
        const int ai = r.pick ((int) (sizeof (algs) / sizeof (algs[0])));
        Dx7Patch v = dx7InitVoice();
        v.algorithm = (uint8_t) (algs[ai] - 1);
        v.feedback = (uint8_t) r.pick (7);
        const char* nm = "RND VOICE";
        std::fill (std::begin (v.name), std::end (v.name), 0);
        for (int i = 0; nm[i]; ++i) v.name[i] = nm[i];

        static const int ratios[] = { 1, 1, 1, 2, 2, 3, 4, 5, 7, 14 };
        const bool sustained = a == Arch::Pad || a == Arch::Lead;
        for (int i = 0; i < 6; ++i) {
            Dx7Op o;
            const bool carrier = (carrierMask[ai] >> i) & 1;
            o.freqCoarse = (uint8_t) (carrier ? (r.chance (0.8f) ? 1 : 2) : ratios[r.pick (10)]);
            if (a == Arch::Bass && carrier) o.freqCoarse = (uint8_t) (r.chance (0.7f) ? 1 : 0);
            o.freqFine = (uint8_t) (r.chance (0.15f) ? r.pick (60) : 0);
            o.detune = (uint8_t) (7 + r.pick (5) - 2);
            o.keyVelSens = (uint8_t) (carrier ? 1 + r.pick (3) : 2 + r.pick (5));
            o.kbdRateScaling = (uint8_t) r.pick (4);
            o.outputLevel = (uint8_t) (carrier ? 82 + r.pick (16) : 40 + r.pick (36));
            const bool fast = ! sustained && (a != Arch::Bass || ! carrier);
            const float e = sustained ? 1.0f : 0.0f;
            o.egRate[0] = (uint8_t) (sustained ? 45 + r.pick (45) : 92 + r.pick (8));
            o.egRate[1] = (uint8_t) (fast ? (carrier ? 20 + r.pick (25) : 40 + r.pick (35)) : 35 + r.pick (30));
            o.egRate[2] = (uint8_t) (fast ? 20 + r.pick (20) : 30 + r.pick (20));
            o.egRate[3] = (uint8_t) (sustained ? 38 + r.pick (22) : 55 + r.pick (30));
            o.egLevel[0] = 99;
            o.egLevel[1] = (uint8_t) (carrier ? (sustained ? 85 + r.pick (14) : 60 + r.pick (30)) : (sustained ? 55 + r.pick (35) : 15 + r.pick (45)));
            o.egLevel[2] = (uint8_t) (sustained ? o.egLevel[1] - (uint8_t) (r.pick (10)) : (a == Arch::Bass && carrier ? 40 + r.pick (30) : (uint8_t) r.pick (12)));
            o.egLevel[3] = 0;
            (void) e;
            v.op[(size_t) i] = o;
        }
        // silence a random trailing modulator or two on busy algorithms for a cleaner tone
        for (int i = 5; i >= 3; --i)
            if (! ((carrierMask[ai] >> i) & 1) && r.chance (0.25f)) v.op[(size_t) i].outputLevel = 0;
        if (sustained && r.chance (0.7f)) {   // gentle vibrato
            v.lfoSpeed = (uint8_t) (30 + r.pick (14)); v.lfoDelay = (uint8_t) (30 + r.pick (30));
            v.lfoPitchModDepth = (uint8_t) (6 + r.pick (10)); v.lfoWave = 4; v.lfoPitchModSens = 3;
        }
        return v;
    }

    void setFilter (LayerParams& l, Arch a, R& r)
    {
        float lo = 800.0f, hi = 12000.0f;
        switch (a) {
            case Arch::Bass:  lo = 200.0f;  hi = 1800.0f;  break;
            case Arch::Pad:   lo = 500.0f;  hi = 6000.0f;  break;
            case Arch::Lead:  lo = 1500.0f; hi = 14000.0f; break;
            case Arch::Keys:  lo = 1000.0f; hi = 10000.0f; break;
            case Arch::Pluck: lo = 800.0f;  hi = 12000.0f; break;
        }
        if (l.engine == Engine::Pluck) lo = std::max (lo, 2500.0f);
        l.cutoff = clampv (r.logRange (lo, hi), 200.0f, 14000.0f);
        const float t = r.uni();
        l.filter = t < 0.55f ? FilterType::LowPass24 : t < 0.78f ? FilterType::LowPass12
                 : t < 0.95f ? FilterType::Ladder : FilterType::LowPass24;
        l.resonance = r.chance (0.8f) ? r.range (0.0f, 0.25f) : r.range (0.25f, 0.55f);
        l.drive = r.chance (0.2f) ? r.range (0.05f, 0.35f) : 0.0f;
        l.keyTrack = r.range (0.2f, 0.8f);
        l.filterEnvAmount = r.chance (0.8f) ? r.range (0.05f, a == Arch::Pad ? 0.3f : 0.5f) : -r.range (0.05f, 0.2f);
        l.filterEnv = { r.range (0.002f, a == Arch::Pad ? 0.8f : 0.02f), r.range (0.2f, 1.2f), r.range (0.0f, 0.4f), r.range (0.15f, 0.8f) };
        l.modEnv = { 0.003f, r.range (0.2f, 1.0f), r.range (0.0f, 0.3f), r.range (0.15f, 0.6f) };
    }

    void setEngine (LayerParams& l, Engine e, Arch a, R& r, const std::vector<Dx7Patch>& fmVoices)
    {
        l.engine = e;
        switch (e) {
            case Engine::Analog: {
                static const Wave waves[] = { Wave::Saw, Wave::Saw, Wave::Square, Wave::Triangle, Wave::Saw, Wave::Sine };
                l.analog.wave1 = waves[r.pick (6)];
                l.analog.wave2 = waves[r.pick (6)];
                l.analog.pw1 = r.range (0.3f, 0.7f); l.analog.pw2 = r.range (0.3f, 0.7f);
                static const float semis[] = { 0, 0, 0, 7, 12, -12, 5, 0 };
                l.analog.osc2Semi = semis[r.pick (8)];
                l.analog.osc2Fine = r.range (-12.0f, 12.0f);
                l.analog.mix = r.chance (0.35f) ? 0.0f : r.range (0.2f, 0.6f);
                l.analog.sub = a == Arch::Bass ? r.range (0.2f, 0.55f) : (r.chance (0.2f) ? r.range (0.1f, 0.35f) : 0.0f);
                l.analog.noise = r.chance (0.12f) ? r.range (0.01f, 0.06f) : 0.0f;
                l.analog.sync = a == Arch::Lead && r.chance (0.1f);
                break; }
            case Engine::Wavetable:
                l.wtTable = r.pick (WavetableBank::get().numTables());
                l.wtPosition = r.uni();
                break;
            case Engine::FM:
                l.fm = fmVoices.empty() ? generatedVoice (a, r)
                                        : fmVoices[(size_t) r.pick ((int) fmVoices.size())];
                l.fmAmount = r.range (0.8f, 1.3f);
                l.useAmpEnv = false;
                l.filter = FilterType::LowPass12;
                l.cutoff = 14000.0f; l.resonance = 0.0f; l.drive = 0.0f; l.filterEnvAmount = 0.0f;
                l.velocitySens = 0.3f;
                break;
            case Engine::Pluck:
                l.pluck.decay = r.range(0.3f, 0.8f); l.pluck.damping = r.range (0.2f, 0.7f);
                l.pluck.pickPos = r.range (0.1f, 0.4f); l.pluck.noiseColor = r.range (0.3f, 0.9f);
                break;
            default: break;
        }
    }

    Engine pickEngine (R& r)
    {
        const float t = r.uni();
        return t < 0.35f ? Engine::Analog : t < 0.65f ? Engine::Wavetable : t < 0.90f ? Engine::FM : Engine::Pluck;
    }

    // Typical loudness of a solo layer at level 0.72 relative to the other engines (measured at
    // C3, velocity 100): Pluck is far quieter than the oscillator engines.
    float engineWeight (Engine e)
    {
        switch (e) {
            case Engine::Analog:    return 0.070f;
            case Engine::Wavetable: return 0.055f;
            case Engine::FM:        return 0.027f;
            case Engine::Pluck:     return 0.0075f;
            default: return 0.05f;
        }
    }
}

Patch randomPatch (uint32_t seed, const std::vector<Dx7Patch>& fmVoices)
{
    R r;
    r.g.seed (seed * 2654435761u + 0x7F4A7C15u);
    for (int i = 0; i < 8; ++i) r.g.nextU();

    Patch p;
    char nm[32];
    std::snprintf (nm, sizeof (nm), "Random %u", (unsigned) (seed % 10000u));
    p.name = nm;
    p.category = "Random";

    // archetype
    const float ta = r.uni();
    const Arch arch = ta < 0.2f ? Arch::Pluck : ta < 0.4f ? Arch::Pad : ta < 0.62f ? Arch::Keys
                    : ta < 0.84f ? Arch::Lead : Arch::Bass;

    const float tl = r.uni();
    const int numLayers = tl < 0.55f ? 1 : tl < 0.90f ? 2 : 3;
    const float layerLevel = numLayers == 1 ? 0.72f : numLayers == 2 ? 0.58f : 0.48f;

    for (int i = 0; i < numLayers; ++i) {
        LayerParams l;
        Arch la = arch;
        if (i > 0 && r.chance (0.3f)) la = Arch::Pluck;   // a transient layer on top
        Engine e = pickEngine (r);
        // keep stacked layers different where possible
        if (i > 0 && e == p.layers[(size_t) i - 1].engine && r.chance (0.6f)) e = pickEngine (r);

        setEngine (l, e, la, r, fmVoices);
        if (e != Engine::FM) setFilter (l, la, r);   // after setEngine so the engine is known
        l.ampEnv = ampFor (la, r);
        if (e == Engine::Pluck) l.ampEnv = { 0.001f, r.range (0.3f, 1.2f), r.range (0.3f, 0.7f), r.range (0.15f, 0.5f) };
        if (e == Engine::FM) l.ampEnv = { 0.001f, 0.01f, 1.0f, r.range (0.1f, 0.4f) };

        l.level = layerLevel;
        l.velocitySens = e == Engine::FM ? 0.3f : r.range (0.3f, 0.8f);
        if (i == 0) { l.transpose = arch == Arch::Bass ? -12 * r.pick (2) : 0; }
        else {
            static const int tr[] = { 0, 12, -12, 0, 7 };
            l.transpose = tr[r.pick (5)];
            l.fine = r.range (-8.0f, 8.0f);
            l.pan = (i == 1 ? -1.0f : 1.0f) * r.range (0.0f, 0.4f);
        }

        // unison: modest, occasional
        if (e != Engine::Pluck && arch != Arch::Bass && r.chance (0.3f)) {
            l.unison = 2 + r.pick (4);
            l.unisonDetune = r.range (0.1f, 0.35f);
            l.unisonSpread = r.range (0.4f, 0.9f);
        }
        if (arch == Arch::Bass && r.chance (0.25f)) { l.mode = VoiceMode::Legato; l.glide = r.range (0.01f, 0.08f); }

        // LFO 1 (vibrato / filter motion), LFO 2 (slow)
        l.lfo[0] = { r.chance (0.7f) ? LfoShape::Sine : LfoShape::Triangle, r.range (3.5f, 6.5f), false, 1.0f, true, r.range (0.2f, 0.8f) };
        l.lfo[1] = { LfoShape::Sine, r.range (0.1f, 1.2f), false, 1.0f, false, 0.0f };

        // 0-3 curated mod routings (slot 7 stays reserved for the brightness macro)
        const int nMods = r.pick (4);
        for (int s = 0, used = 0; used < nMods && s < 6; ++s) {
            switch (r.pick (6)) {
                case 0: if (arch != Arch::Bass) mod (l, used++, ModSrc::Lfo1, ModDst::Pitch, r.range (0.004f, 0.012f)); break;
                case 1: if (e != Engine::FM) mod (l, used++, ModSrc::Lfo2, ModDst::Cutoff, r.range (0.04f, 0.18f)); break;
                case 2: if (e != Engine::FM) mod (l, used++, ModSrc::ModEnv, ModDst::Cutoff, r.range (0.1f, 0.4f)); break;
                case 3: mod (l, used++, ModSrc::Velocity, ModDst::Level, r.range (0.1f, 0.3f)); break;
                case 4: if (e == Engine::Wavetable) mod (l, used++, ModSrc::Lfo2, ModDst::WavePosition, r.range (0.1f, 0.4f)); break;
                case 5: if (e == Engine::FM) mod (l, used++, ModSrc::ModEnv, ModDst::FmAmount, r.range (0.1f, 0.4f)); break;
            }
        }
        mod (l, 7, ModSrc::Macro1, ModDst::Cutoff, 0.5f);
        p.layers[(size_t) i] = l;
    }
    for (int i = numLayers; i < kMaxLayers; ++i) p.layers[(size_t) i].engine = Engine::Off;
    p.macroNames[0] = "Brightness";

    // effects: sometimes, moderate
    if (r.chance (0.4f)) { p.chorus.on = true; p.chorus.rate = r.range (0.2f, 1.2f); p.chorus.depth = r.range (0.3f, 0.7f); p.chorus.mix = r.range (0.2f, 0.45f); }
    if (r.chance (0.35f)) {
        p.delay.on = true; p.delay.sync = true;
        static const float beats[] = { 0.25f, 0.375f, 0.5f, 0.75f, 1.0f };
        p.delay.beats = beats[r.pick (5)];
        p.delay.feedback = r.range (0.2f, 0.5f); p.delay.mix = r.range (0.12f, 0.3f);
        p.delay.tone = r.range (0.35f, 0.7f); p.delay.pingPong = r.chance (0.6f);
    }
    if (r.chance (arch == Arch::Pad ? 0.9f : 0.55f)) {
        p.reverb.on = true; p.reverb.size = r.range (0.35f, 0.8f); p.reverb.damping = r.range (0.3f, 0.7f);
        p.reverb.mix = r.range (0.12f, 0.35f); p.reverb.predelayMs = r.range (0.0f, 30.0f); p.reverb.width = 1.0f;
    }

    // loudness match: estimate the summed layer loudness, then trim master (0.45..1) and, if the
    // patch is still too quiet (plucks), the layer levels (up to 1)
    float est = 0.0f;
    for (int i = 0; i < numLayers; ++i) {
        const float k = p.layers[(size_t) i].level / 0.72f;
        est += engineWeight (p.layers[(size_t) i].engine) * k * k;
    }
    const float target = 0.06f;
    const float f = target / std::max (est, 1.0e-4f);   // gain factor wanted relative to master 0.7 / levels as set
    p.masterVolume = clampv (0.7f * std::sqrt (f), 0.45f, 1.0f);
    const float rem = f / std::max (1.0e-3f, (p.masterVolume / 0.7f) * (p.masterVolume / 0.7f));
    if (rem > 1.0f)
        for (int i = 0; i < numLayers; ++i)
            p.layers[(size_t) i].level = std::min (1.0f, p.layers[(size_t) i].level * std::sqrt (rem));
    return p;
}

} // namespace el::chimera
