// Tests for Chimera's FM cart (Library.cpp) and patch randomiser (Randomize.cpp).
#include "TestFramework.h"
#include "Synth/dsp/Presets.h"
#include "Synth/dsp/Synth.h"
#include <cstring>
#include <set>
#include <string>

using namespace el::chimera;

namespace {
struct Rig {
    Synth s;
    std::vector<float> buf[2];
    float* outs[2] {};
    LiveParams live;
    double sumSq = 0; long count = 0; float peak = 0; bool finite = true;
    explicit Rig (const Patch& p)
    {
        s.prepare (48000.0, 256);
        for (int c = 0; c < 2; ++c) { buf[c].assign (256, 0.0f); outs[c] = buf[c].data(); }
        s.patches().publish (std::make_shared<const Patch> (p));
        live = LiveParams::fromPatch (p);
    }
    void run (int blocks, std::vector<MidiEvent> ev = {})
    {
        for (int b = 0; b < blocks; ++b) {
            s.process (outs, 2, 256, b == 0 && ! ev.empty() ? ev.data() : nullptr, b == 0 ? (int) ev.size() : 0, 120.0, live);
            for (int c = 0; c < 2; ++c)
                for (float x : buf[c]) {
                    if (! std::isfinite (x)) finite = false;
                    peak = std::max (peak, std::fabs (x));
                    if (c == 0) { sumSq += (double) x * x; ++count; }
                }
        }
    }
    double rms() const { return count ? std::sqrt (sumSq / (double) count) : 0.0; }
    void resetStats() { sumSq = 0; count = 0; peak = 0; }
};
MidiEvent on (int note, int vel = 100) { return { 0, 0x90, (uint8_t) note, (uint8_t) vel }; }
MidiEvent offEv (int note) { return { 0, 0x80, (uint8_t) note, 0 }; }
constexpr int kBlocksPerSec = 48000 / 256 + 1;   // ~188
}

EL_TEST (random_cart_has_32_unique_voices)
{
    const auto& c = chimeraCart();
    EL_CHECK (c.size() == 32);
    std::set<std::string> names;
    for (const auto& v : c) {
        const std::string n (v.name);
        EL_CHECK (! n.empty() && n.size() <= 10);
        names.insert (n);
        EL_CHECK (v.algorithm < 32 && v.feedback <= 7);
        // every carrier-capable operator must end at level 0 so the voice releases
        for (const auto& o : v.op) EL_CHECK (o.egLevel[3] == 0);
    }
    EL_CHECK (names.size() == 32);
}

EL_TEST (random_cart_voices_sound_and_release)
{
    for (const auto& v : chimeraCart()) {
        // raw FmVoice
        FmVoice fv;
        fv.prepare (48000.0f);
        fv.noteOn (v, 60, 100);
        std::vector<float> b (4800);
        float pk = 0; bool fin = true;
        for (int i = 0; i < 10; ++i) {
            fv.render (b.data(), 4800, 0.0f, 1.0f);
            for (float x : b) { if (! std::isfinite (x)) fin = false; pk = std::max (pk, std::fabs (x)); }
        }
        EL_CHECK (fin);
        EL_CHECK (pk > 0.05f);
        fv.noteOff();
        for (int i = 0; i < 100 && fv.active(); ++i) fv.render (b.data(), 4800, 0.0f, 1.0f);   // up to 10 s
        EL_CHECK (! fv.active());
        if (fv.active()) std::printf ("    voice %s never released\n", v.name);

        // through the engine
        Patch p;
        p.layers[0].engine = Engine::FM;
        p.layers[0].fm = v;
        p.layers[0].useAmpEnv = false;
        Rig r (p);
        r.run (kBlocksPerSec / 2, { on (60) });
        if (! (r.rms() > 0.002)) std::printf ("    voice %s quiet: rms %g\n", v.name, r.rms());
        EL_CHECK (r.rms() > 0.002);
        EL_CHECK (r.finite && r.peak < 4.0f);
        r.run (1, { offEv (60) });
        r.run (kBlocksPerSec * 10);
        EL_CHECK (r.s.activeVoiceCount() == 0);
    }
}

EL_TEST (random_patch_is_deterministic)
{
    auto same = [] (const Patch& a, const Patch& b) {
        if (a.name != b.name) return false;
        if (a.masterVolume != b.masterVolume || a.reverb.mix != b.reverb.mix || a.delay.on != b.delay.on || a.chorus.on != b.chorus.on) return false;
        for (int i = 0; i < kMaxLayers; ++i) {
            const auto &x = a.layers[(size_t) i], &y = b.layers[(size_t) i];
            if (x.engine != y.engine || x.cutoff != y.cutoff || x.resonance != y.resonance || x.level != y.level
                || x.unison != y.unison || x.ampEnv.attack != y.ampEnv.attack || x.wtPosition != y.wtPosition
                || x.analog.mix != y.analog.mix || x.fm.algorithm != y.fm.algorithm
                || std::memcmp (x.mods, y.mods, sizeof (x.mods)) != 0) return false;
        }
        return true;
    };
    int differing = 0;
    for (uint32_t seed = 1; seed <= 30; ++seed) {
        EL_CHECK (same (randomPatch (seed), randomPatch (seed)));
        if (! same (randomPatch (seed), randomPatch (seed + 1))) ++differing;
    }
    EL_CHECK (differing == 30);
    const Patch p = randomPatch (4821);
    EL_CHECK (p.category == "Random");
    EL_CHECK (p.name == "Random 4821");
}

EL_TEST (random_patches_are_safe_audible_and_release)
{
    int engines[kNumEngines] {}, layerCounts[kMaxLayers + 1] {};
    double worstRms = 1e9, maxRms = 0;
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        const Patch p = randomPatch (seed);
        int active = 0;
        bool brightness = true;
        for (const auto& l : p.layers) {
            if (l.engine == Engine::Off) continue;
            ++active; ++engines[(int) l.engine];
            EL_CHECK (l.engine != Engine::Sampler);
            bool hasMacro = false;
            for (const auto& m : l.mods) if (m.src == ModSrc::Macro1 && m.dst == ModDst::Cutoff) hasMacro = true;
            brightness = brightness && hasMacro;
            EL_CHECK (l.cutoff >= 200.0f && l.cutoff <= 14000.0f);
            EL_CHECK (l.unison >= 1 && l.unison <= 5);
        }
        EL_CHECK (brightness);
        EL_CHECK (active >= 1 && active <= 3);
        ++layerCounts[active];

        Rig r (p);
        r.run (kBlocksPerSec, { on (48, 100) });
        const double rms = r.rms();
        if (! (rms > 0.002)) std::printf ("    seed %u quiet: rms %g\n", seed, rms);
        EL_CHECK (rms > 0.002);
        worstRms = std::min (worstRms, rms); maxRms = std::max (maxRms, rms);
        r.run (1, { offEv (48) });
        r.run (kBlocksPerSec * 15);
        EL_CHECK (r.finite);
        if (! (r.peak < 4.0f)) std::printf ("    seed %u peak %g\n", seed, r.peak);
        EL_CHECK (r.peak < 4.0f);
        if (r.s.activeVoiceCount() != 0) std::printf ("    seed %u still active\n", seed);
        EL_CHECK (r.s.activeVoiceCount() == 0);
    }
    std::printf ("    rms range %.4f .. %.4f; layers 1/2/3 = %d/%d/%d; engines A/W/F/P = %d/%d/%d/%d\n",
                 worstRms, maxRms, layerCounts[1], layerCounts[2], layerCounts[3],
                 engines[(int) Engine::Analog], engines[(int) Engine::Wavetable], engines[(int) Engine::FM], engines[(int) Engine::Pluck]);
    EL_CHECK (engines[(int) Engine::Sampler] == 0);
}

EL_TEST (random_patch_uses_supplied_fm_voices)
{
    const auto& cart = chimeraCart();
    int fmLayers = 0;
    for (uint32_t seed = 1; seed <= 100; ++seed) {
        const Patch p = randomPatch (seed, cart);
        for (const auto& l : p.layers) {
            if (l.engine != Engine::FM) continue;
            ++fmLayers;
            bool found = false;
            for (const auto& v : cart)
                if (dx7ToVced (v) == dx7ToVced (l.fm) && std::strncmp (v.name, l.fm.name, 10) == 0) found = true;
            EL_CHECK (found);
        }
    }
    EL_CHECK (fmLayers > 10);
}
