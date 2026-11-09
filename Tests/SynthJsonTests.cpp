// Chimera patch JSON codec: full round-trip, partial updates, error handling, clamping, read-only keys.
#include "TestFramework.h"
#include "Synth/dsp/PatchJson.h"
#include "Synth/dsp/third_party/json.hpp"
#include <cstring>

using namespace el::chimera;

namespace {
int g_diffs = 0;
#define EQ(a, b) do { if (!((a) == (b))) { std::printf ("  diff at %s:%d: %s\n", __FILE__, __LINE__, #a); ++g_diffs; } } while (0)

bool same (const EnvParams& a, const EnvParams& b) { return a.attack == b.attack && a.decay == b.decay && a.sustain == b.sustain && a.release == b.release; }
bool same (const LfoParams& a, const LfoParams& b)
{ return a.shape == b.shape && a.rateHz == b.rateHz && a.sync == b.sync && a.beats == b.beats && a.retrigger == b.retrigger && a.fadeIn == b.fadeIn; }
bool same (const Dx7Op& a, const Dx7Op& b)
{
    for (int i = 0; i < 4; ++i) if (a.egRate[i] != b.egRate[i] || a.egLevel[i] != b.egLevel[i]) return false;
    return a.kbdBreakPoint == b.kbdBreakPoint && a.kbdLeftDepth == b.kbdLeftDepth && a.kbdRightDepth == b.kbdRightDepth
        && a.kbdLeftCurve == b.kbdLeftCurve && a.kbdRightCurve == b.kbdRightCurve && a.kbdRateScaling == b.kbdRateScaling
        && a.ampModSens == b.ampModSens && a.keyVelSens == b.keyVelSens && a.outputLevel == b.outputLevel && a.oscMode == b.oscMode
        && a.freqCoarse == b.freqCoarse && a.freqFine == b.freqFine && a.detune == b.detune;
}
bool same (const Dx7Patch& a, const Dx7Patch& b)
{
    for (int i = 0; i < 6; ++i) if (! same (a.op[(size_t) i], b.op[(size_t) i])) return false;
    for (int i = 0; i < 4; ++i) if (a.pitchEgRate[i] != b.pitchEgRate[i] || a.pitchEgLevel[i] != b.pitchEgLevel[i]) return false;
    return a.algorithm == b.algorithm && a.feedback == b.feedback && a.oscKeySync == b.oscKeySync && a.lfoSpeed == b.lfoSpeed
        && a.lfoDelay == b.lfoDelay && a.lfoPitchModDepth == b.lfoPitchModDepth && a.lfoAmpModDepth == b.lfoAmpModDepth
        && a.lfoKeySync == b.lfoKeySync && a.lfoWave == b.lfoWave && a.lfoPitchModSens == b.lfoPitchModSens
        && a.transpose == b.transpose && std::memcmp (a.name, b.name, sizeof a.name) == 0;
}
bool same (const LayerParams& a, const LayerParams& b)
{
    const auto &x = a.analog, &y = b.analog;
    bool ok = a.engine == b.engine && a.name == b.name && x.wave1 == y.wave1 && x.wave2 == y.wave2 && x.pw1 == y.pw1 && x.pw2 == y.pw2
        && x.osc2Semi == y.osc2Semi && x.osc2Fine == y.osc2Fine && x.mix == y.mix && x.sub == y.sub && x.noise == y.noise && x.sync == y.sync
        && a.wtTable == b.wtTable && a.wtPosition == b.wtPosition && same (a.fm, b.fm) && a.fmAmount == b.fmAmount
        && a.sampler.start == b.sampler.start && a.sampler.loop == b.sampler.loop && a.sampler.loopStart == b.sampler.loopStart
        && a.sampler.loopEnd == b.sampler.loopEnd && a.sampler.reverse == b.sampler.reverse
        && a.pluck.decay == b.pluck.decay && a.pluck.damping == b.pluck.damping && a.pluck.pickPos == b.pluck.pickPos && a.pluck.noiseColor == b.pluck.noiseColor
        && a.filter == b.filter && a.cutoff == b.cutoff && a.resonance == b.resonance && a.drive == b.drive && a.keyTrack == b.keyTrack
        && a.filterEnvAmount == b.filterEnvAmount && same (a.ampEnv, b.ampEnv) && same (a.filterEnv, b.filterEnv) && same (a.modEnv, b.modEnv)
        && a.useAmpEnv == b.useAmpEnv && a.level == b.level && a.pan == b.pan && a.transpose == b.transpose && a.fine == b.fine
        && a.keyLo == b.keyLo && a.keyHi == b.keyHi && a.velLo == b.velLo && a.velHi == b.velHi && a.velocitySens == b.velocitySens
        && a.unison == b.unison && a.unisonDetune == b.unisonDetune && a.unisonSpread == b.unisonSpread && a.glide == b.glide
        && a.mode == b.mode && a.polyphony == b.polyphony && a.output == b.output;
    for (int i = 0; i < kLayerLfos; ++i) ok = ok && same (a.lfo[i], b.lfo[i]);
    for (int i = 0; i < kMatrixSlots; ++i) ok = ok && a.mods[i].src == b.mods[i].src && a.mods[i].dst == b.mods[i].dst && a.mods[i].amount == b.mods[i].amount;
    return ok;
}
bool same (const Patch& a, const Patch& b)   // everything except samples/carts/cartNames
{
    bool ok = a.name == b.name && a.category == b.category && a.macroNames == b.macroNames && a.masterVolume == b.masterVolume
        && a.bendRange == b.bendRange
        && a.chorus.on == b.chorus.on && a.chorus.rate == b.chorus.rate && a.chorus.depth == b.chorus.depth && a.chorus.mix == b.chorus.mix
        && a.delay.on == b.delay.on && a.delay.sync == b.delay.sync && a.delay.beats == b.delay.beats && a.delay.ms == b.delay.ms
        && a.delay.feedback == b.delay.feedback && a.delay.mix == b.delay.mix && a.delay.tone == b.delay.tone && a.delay.pingPong == b.delay.pingPong
        && a.reverb.on == b.reverb.on && a.reverb.size == b.reverb.size && a.reverb.damping == b.reverb.damping && a.reverb.mix == b.reverb.mix
        && a.reverb.predelayMs == b.reverb.predelayMs && a.reverb.width == b.reverb.width;
    for (int i = 0; i < kMaxLayers; ++i) ok = ok && same (a.layers[(size_t) i], b.layers[(size_t) i]);
    for (int i = 0; i < kGlobalLfos; ++i) ok = ok && same (a.globalLfo[i], b.globalLfo[i]);
    return ok;
}

// Every field set to a non-default, awkward value (seeded pseudo-random so floats are not round).
Patch heavyPatch()
{
    uint32_t s = 12345;
    auto rnd = [&] { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; };
    auto f = [&] (float lo, float hi) { return lo + rnd() * (hi - lo); };
    auto i = [&] (int lo, int hi) { return lo + (int) (rnd() * (hi - lo + 1)) % (hi - lo + 1); };
    Patch p;
    p.name = "H\"eavy \\ patch\n\xC3\xA9"; p.category = "Test/Cat";
    p.macroNames = { "A", "Bb", "C c", "\xE2\x82\xAC" };
    p.masterVolume = f (0, 1); p.bendRange = f (0, 24);
    p.chorus = { true, f (0.05f, 5), f (0, 1), f (0, 1) };
    p.delay = { true, false, f (0.0625f, 4), f (1, 2000), f (0, 0.95f), f (0, 1), f (0, 1), false };
    p.reverb = { true, f (0, 1), f (0, 1), f (0, 1), f (0, 200), f (0, 1) };
    for (auto& l : p.globalLfo) l = { (LfoShape) i (0, kNumLfoShapes - 1), f (0.01f, 40), true, f (0.0625f, 16), true, f (0, 10) };
    for (auto& L : p.layers) {
        L.engine = (Engine) i (0, kNumEngines - 1); L.name = "Lyr";
        L.analog = { (Wave) i (0, 4), (Wave) i (0, 4), f (0.05f, 0.95f), f (0.05f, 0.95f), f (-24, 24), f (-100, 100), f (0, 1), f (0, 1), f (0, 1), true };
        L.wtTable = i (1, 7); L.wtPosition = f (0, 1); L.fmAmount = f (0, 2);
        auto& d = L.fm;
        for (auto& o : d.op) {
            for (int k = 0; k < 4; ++k) { o.egRate[k] = (uint8_t) i (0, 99); o.egLevel[k] = (uint8_t) i (0, 99); }
            o.kbdBreakPoint = (uint8_t) i (0, 99); o.kbdLeftDepth = (uint8_t) i (0, 99); o.kbdRightDepth = (uint8_t) i (0, 99);
            o.kbdLeftCurve = (uint8_t) i (0, 3); o.kbdRightCurve = (uint8_t) i (0, 3); o.kbdRateScaling = (uint8_t) i (0, 7);
            o.ampModSens = (uint8_t) i (0, 3); o.keyVelSens = (uint8_t) i (0, 7); o.outputLevel = (uint8_t) i (0, 99);
            o.oscMode = (uint8_t) i (0, 1); o.freqCoarse = (uint8_t) i (0, 31); o.freqFine = (uint8_t) i (0, 99); o.detune = (uint8_t) i (0, 14);
        }
        for (int k = 0; k < 4; ++k) { d.pitchEgRate[k] = (uint8_t) i (0, 99); d.pitchEgLevel[k] = (uint8_t) i (0, 99); }
        d.algorithm = (uint8_t) i (0, 31); d.feedback = (uint8_t) i (0, 7); d.oscKeySync = 0; d.lfoSpeed = (uint8_t) i (0, 99);
        d.lfoDelay = (uint8_t) i (0, 99); d.lfoPitchModDepth = (uint8_t) i (0, 99); d.lfoAmpModDepth = (uint8_t) i (0, 99);
        d.lfoKeySync = 0; d.lfoWave = (uint8_t) i (0, 5); d.lfoPitchModSens = (uint8_t) i (0, 7); d.transpose = (uint8_t) i (0, 48);
        std::memset (d.name, 0, sizeof d.name); std::memcpy (d.name, "BRASS 1", 7);
        L.sampler = { f (0, 1), (LoopMode) i (1, 2), f (0, 0.5f), f (0.5f, 1), true };
        L.pluck = { f (0, 1), f (0, 1), f (0, 0.5f), f (0, 1) };
        L.filter = (FilterType) i (1, kNumFilterTypes - 1); L.cutoff = f (20, 20000); L.resonance = f (0, 1); L.drive = f (0, 1);
        L.keyTrack = f (0, 1); L.filterEnvAmount = f (-1, 1);
        L.ampEnv = { f (0, 10), f (0, 10), f (0, 1), f (0, 10) }; L.filterEnv = { f (0, 10), f (0, 10), f (0, 1), f (0, 10) };
        L.modEnv = { f (0, 10), f (0, 10), f (0, 1), f (0, 10) };
        L.useAmpEnv = false;
        for (auto& l : L.lfo) l = { (LfoShape) i (0, kNumLfoShapes - 1), f (0.01f, 40), true, f (0.0625f, 16), false, f (0, 10) };
        L.level = f (0, 1); L.pan = f (-1, 1); L.transpose = i (-48, 48); L.fine = f (-100, 100);
        L.keyLo = i (1, 40); L.keyHi = i (60, 126); L.velLo = i (2, 30); L.velHi = i (60, 126); L.velocitySens = f (0, 1);
        L.unison = i (2, 7); L.unisonDetune = f (0, 1); L.unisonSpread = f (0, 1); L.glide = f (0, 10);
        L.mode = (VoiceMode) i (1, 2); L.polyphony = i (1, 32); L.output = i (1, 3);
        for (auto& m : L.mods) m = { (ModSrc) i (1, kNumModSrcs - 1), (ModDst) i (1, kNumModDsts - 1), f (-1, 1) };
    }
    return p;
}
}

EL_TEST (json_roundtrip_heavy_patch)
{
    const Patch a = heavyPatch();
    Patch b;
    std::string err;
    EL_CHECK (patchFromJson (patchToJson (a), b, &err));
    EL_CHECK (same (a, b));
    EL_CHECK (patchToJson (a) == patchToJson (b));
    // sanity: the heavy patch really differs from the default
    EL_CHECK (! same (a, Patch()));
    // default patch round-trips too
    Patch d, e; e.name = "other";
    EL_CHECK (patchFromJson (patchToJson (d), e));
    EL_CHECK (same (d, e));
}

EL_TEST (json_partial_update_only_touches_given_keys)
{
    Patch a = heavyPatch();
    const Patch before = a;
    EL_CHECK (patchFromJson (R"({"masterVolume":0.25,"layers":[{"cutoff":1234.5},null,{"fm":{"op":[{"outputLevel":7},{"detune":3}],"algorithm":9}}],"reverb":{"on":false}})", a));
    Patch want = before;
    want.masterVolume = 0.25f; want.layers[0].cutoff = 1234.5f;
    want.layers[2].fm.op[0].outputLevel = 7; want.layers[2].fm.op[1].detune = 3; want.layers[2].fm.algorithm = 9;
    want.reverb.on = false;
    EL_CHECK (same (a, want));
    Patch c = before;
    EL_CHECK (patchFromJson ("{}", c));
    EL_CHECK (same (c, before));
}

EL_TEST (json_garbage_returns_false_and_leaves_patch)
{
    const Patch orig = heavyPatch();
    for (const char* bad : { "", "not json", "{\"name\":", "[1,2,3]", "42", "null", "{\"masterVolume\":0.1,", "\x01\x02\xff" }) {
        Patch p = orig; std::string err;
        EL_CHECK (! patchFromJson (bad, p, &err));
        EL_CHECK (! err.empty());
        EL_CHECK (same (p, orig));
    }
    // wrong-typed values are ignored, not fatal
    Patch p = orig;
    EL_CHECK (patchFromJson (R"({"masterVolume":"loud","layers":"x","chorus":5,"name":3})", p));
    EL_CHECK (same (p, orig));
}

EL_TEST (json_out_of_range_is_clamped)
{
    Patch p;
    EL_CHECK (patchFromJson (R"({"masterVolume":5,"bendRange":-3,"layers":[{"cutoff":1e9,"resonance":-2,"unison":99,"polyphony":0,
        "transpose":-500,"engine":99,"filter":-4,"keyLo":300,"output":9,"mods":[{"amount":7,"src":99,"dst":-1}],
        "fm":{"algorithm":200,"feedback":-1,"name":"WAY TOO LONG NAME","op":[{"egRate":[500,-5,50],"outputLevel":1000,"kbdLeftCurve":9}]},
        "lfo":[{"rateHz":1000,"shape":50}]}],"delay":{"feedback":2,"ms":0},"chorus":{"rate":99}})", p));
    EL_CHECK (p.masterVolume == 1.0f && p.bendRange == 0.0f);
    const auto& l = p.layers[0];
    EL_CHECK (l.cutoff == 20000.0f && l.resonance == 0.0f && l.unison == kMaxUnison && l.polyphony == 1 && l.transpose == -48);
    EL_CHECK ((int) l.engine == kNumEngines - 1 && l.filter == FilterType::Off && l.keyLo == 127 && l.output == kOutputPairs - 1);
    EL_CHECK (l.mods[0].amount == 1.0f && (int) l.mods[0].src == kNumModSrcs - 1 && l.mods[0].dst == ModDst::None);
    EL_CHECK (l.fm.algorithm == 31 && l.fm.feedback == 0 && std::strcmp (l.fm.name, "WAY TOO LO") == 0);
    EL_CHECK (l.fm.op[0].egRate[0] == 99 && l.fm.op[0].egRate[1] == 0 && l.fm.op[0].egRate[2] == 50 && l.fm.op[0].egRate[3] == 99);
    EL_CHECK (l.fm.op[0].outputLevel == 99 && l.fm.op[0].kbdLeftCurve == 3);
    EL_CHECK (l.lfo[0].rateHz == 40.0f && (int) l.lfo[0].shape == kNumLfoShapes - 1);
    EL_CHECK (p.delay.feedback == 0.95f && p.delay.ms == 1.0f && p.chorus.rate == 5.0f);
}

EL_TEST (json_samples_and_carts_untouched)
{
    Patch p;
    auto s = std::make_shared<SampleData>(); s->name = "kick.wav"; s->rootNote = 36; s->left.assign (10, 0.5f);
    SamplePtr sp = s;
    auto cart = std::make_shared<std::vector<Dx7Patch>>(2, dx7InitVoice());
    std::memcpy ((*cart)[1].name, "SECOND    ", 10);
    p.samples[1] = sp; p.carts[2] = cart; p.cartNames[2] = "mycart";
    EL_CHECK (patchFromJson (R"({"sampleNames":["a","b","c","d"],"sampleRoots":[1,2,3,4],"cartVoices":[[],[],["x"],[]],"cartNames":["q","q","q","q"],"masterVolume":0.1})", p));
    EL_CHECK (p.samples[1] == sp && ! p.samples[0] && p.carts[2] == cart && ! p.carts[0] && p.cartNames[2] == "mycart");
    EL_CHECK (patchFromJson (patchToJson (Patch()), p));   // a patch with no samples must not clear ours
    EL_CHECK (p.samples[1] == sp && p.carts[2] == cart);
}

EL_TEST (json_readonly_info_keys_present)
{
    Patch p;
    auto s = std::make_shared<SampleData>(); s->name = "kick.wav"; s->rootNote = 36;
    p.samples[1] = s;
    p.carts[2] = std::make_shared<std::vector<Dx7Patch>>(2, dx7InitVoice());
    p.cartNames[2] = "mycart";
    const auto j = nlohmann::json::parse (patchToJson (p));
    EL_CHECK (j["sampleNames"].size() == 4 && j["sampleNames"][1] == "kick.wav" && j["sampleNames"][0] == "");
    EL_CHECK (j["sampleRoots"].size() == 4 && j["sampleRoots"][1] == 36);
    EL_CHECK (j["cartVoices"].size() == 4 && j["cartVoices"][2].size() == 2 && j["cartVoices"][2][0] == "INIT VOICE" && j["cartVoices"][0].empty());
    EL_CHECK (j["cartNames"][2] == "mycart");
    EL_CHECK (j["layers"].size() == 4 && j["layers"][0]["fm"]["op"].size() == 6 && j["layers"][0]["lfo"].size() == 2 && j["layers"][0]["mods"].size() == 8);
    EL_CHECK (j["layers"][0]["fm"]["name"] == "INIT VOICE" && ! j.contains ("samples") && ! j.contains ("carts"));
    EL_CHECK (j["globalLfo"].size() == 2 && j["macroNames"].size() == 4);
    EL_CHECK (g_diffs == 0);
}
