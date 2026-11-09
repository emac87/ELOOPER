#include "Synth/dsp/PatchJson.h"
#include "Synth/dsp/third_party/json.hpp"
#include <cstring>
#include <functional>

namespace el::chimera {
namespace {

using json = nlohmann::json;

constexpr int kNumWaves = 5, kNumVoiceModes = 3, kNumLoopModes = 3, kNumWavetables = 8, kMaxPolyphony = 32;
constexpr size_t kMaxStr = 256;

// ---- readers: only apply when the key exists with a usable type; clamp ---------------------------
const json* member (const json& j, const char* k)
{
    if (! j.is_object()) return nullptr;
    auto it = j.find (k);
    return it == j.end() ? nullptr : &*it;
}
void rdF (const json& j, const char* k, float& v, float lo, float hi)
{
    if (auto* m = member (j, k); m && m->is_number()) {
        const double d = m->get<double>();
        if (std::isfinite (d)) v = clampv ((float) clampv (d, (double) lo, (double) hi), lo, hi);
    }
}
template <typename T> void rdI (const json& j, const char* k, T& v, int lo, int hi)
{
    if (auto* m = member (j, k); m && m->is_number()) {
        const double d = m->get<double>();
        if (std::isfinite (d)) v = (T) clampv ((int) std::lround (clampv (d, (double) lo, (double) hi)), lo, hi);
    }
}
void rdB (const json& j, const char* k, bool& v)
{
    if (auto* m = member (j, k)) {
        if (m->is_boolean()) v = m->get<bool>();
        else if (m->is_number()) v = m->get<double>() != 0.0;
    }
}
template <typename E> void rdE (const json& j, const char* k, E& v, int count)
{
    int t = (int) v; rdI (j, k, t, 0, count - 1); v = (E) t;
}
void rdS (const json& j, const char* k, std::string& v)
{
    if (auto* m = member (j, k); m && m->is_string()) { v = m->get<std::string>(); if (v.size() > kMaxStr) v.resize (kMaxStr); }
}
template <typename F> void rdArr (const json& j, const char* k, size_t n, F&& f)
{
    if (auto* m = member (j, k); m && m->is_array())
        for (size_t i = 0; i < n && i < m->size(); ++i) if ((*m)[i].is_object() || (*m)[i].is_string() || (*m)[i].is_number()) f (i, (*m)[i]);
}

// ---- small structs -------------------------------------------------------------------------------
json wEnv (const EnvParams& v) { return { { "attack", v.attack }, { "decay", v.decay }, { "sustain", v.sustain }, { "release", v.release } }; }
void rEnv (const json& j, EnvParams& v)
{
    rdF (j, "attack", v.attack, 0, 10); rdF (j, "decay", v.decay, 0, 10);
    rdF (j, "sustain", v.sustain, 0, 1); rdF (j, "release", v.release, 0, 10);
}
json wLfo (const LfoParams& v)
{
    return { { "shape", (int) v.shape }, { "rateHz", v.rateHz }, { "sync", v.sync }, { "beats", v.beats },
             { "retrigger", v.retrigger }, { "fadeIn", v.fadeIn } };
}
void rLfo (const json& j, LfoParams& v)
{
    rdE (j, "shape", v.shape, kNumLfoShapes); rdF (j, "rateHz", v.rateHz, 0.01f, 40); rdB (j, "sync", v.sync);
    rdF (j, "beats", v.beats, 0.0625f, 16); rdB (j, "retrigger", v.retrigger); rdF (j, "fadeIn", v.fadeIn, 0, 10);
}
json wMod (const ModSlot& v) { return { { "src", (int) v.src }, { "dst", (int) v.dst }, { "amount", v.amount } }; }
void rMod (const json& j, ModSlot& v)
{
    rdE (j, "src", v.src, kNumModSrcs); rdE (j, "dst", v.dst, kNumModDsts); rdF (j, "amount", v.amount, -1, 1);
}
json wAnalog (const AnalogParams& v)
{
    return { { "wave1", (int) v.wave1 }, { "wave2", (int) v.wave2 }, { "pw1", v.pw1 }, { "pw2", v.pw2 },
             { "osc2Semi", v.osc2Semi }, { "osc2Fine", v.osc2Fine }, { "mix", v.mix }, { "sub", v.sub },
             { "noise", v.noise }, { "sync", v.sync } };
}
void rAnalog (const json& j, AnalogParams& v)
{
    rdE (j, "wave1", v.wave1, kNumWaves); rdE (j, "wave2", v.wave2, kNumWaves);
    rdF (j, "pw1", v.pw1, 0.05f, 0.95f); rdF (j, "pw2", v.pw2, 0.05f, 0.95f);
    rdF (j, "osc2Semi", v.osc2Semi, -24, 24); rdF (j, "osc2Fine", v.osc2Fine, -100, 100);
    rdF (j, "mix", v.mix, 0, 1); rdF (j, "sub", v.sub, 0, 1); rdF (j, "noise", v.noise, 0, 1); rdB (j, "sync", v.sync);
}
json wSampler (const SamplerParams& v)
{
    return { { "start", v.start }, { "loop", (int) v.loop }, { "loopStart", v.loopStart }, { "loopEnd", v.loopEnd }, { "reverse", v.reverse } };
}
void rSampler (const json& j, SamplerParams& v)
{
    rdF (j, "start", v.start, 0, 1); rdE (j, "loop", v.loop, kNumLoopModes);
    rdF (j, "loopStart", v.loopStart, 0, 1); rdF (j, "loopEnd", v.loopEnd, 0, 1); rdB (j, "reverse", v.reverse);
}
json wPluck (const PluckParams& v)
{
    return { { "decay", v.decay }, { "damping", v.damping }, { "pickPos", v.pickPos }, { "noiseColor", v.noiseColor } };
}
void rPluck (const json& j, PluckParams& v)
{
    rdF (j, "decay", v.decay, 0, 1); rdF (j, "damping", v.damping, 0, 1);
    rdF (j, "pickPos", v.pickPos, 0, 0.5f); rdF (j, "noiseColor", v.noiseColor, 0, 1);
}

// ---- DX7 -----------------------------------------------------------------------------------------
json u8arr (const uint8_t* a, int n) { json r = json::array(); for (int i = 0; i < n; ++i) r.push_back ((int) a[i]); return r; }
void rU8arr (const json& j, const char* k, uint8_t* a, int n, int hi)
{
    if (auto* m = member (j, k); m && m->is_array())
        for (int i = 0; i < n && i < (int) m->size(); ++i)
            if ((*m)[i].is_number()) { json t = { { "x", (*m)[i] } }; rdI (t, "x", a[i], 0, hi); }
}
json wOp (const Dx7Op& o)
{
    return { { "egRate", u8arr (o.egRate, 4) }, { "egLevel", u8arr (o.egLevel, 4) }, { "kbdBreakPoint", (int) o.kbdBreakPoint },
             { "kbdLeftDepth", (int) o.kbdLeftDepth }, { "kbdRightDepth", (int) o.kbdRightDepth },
             { "kbdLeftCurve", (int) o.kbdLeftCurve }, { "kbdRightCurve", (int) o.kbdRightCurve },
             { "kbdRateScaling", (int) o.kbdRateScaling }, { "ampModSens", (int) o.ampModSens }, { "keyVelSens", (int) o.keyVelSens },
             { "outputLevel", (int) o.outputLevel }, { "oscMode", (int) o.oscMode }, { "freqCoarse", (int) o.freqCoarse },
             { "freqFine", (int) o.freqFine }, { "detune", (int) o.detune } };
}
void rOp (const json& j, Dx7Op& o)
{
    rU8arr (j, "egRate", o.egRate, 4, 99); rU8arr (j, "egLevel", o.egLevel, 4, 99);
    rdI (j, "kbdBreakPoint", o.kbdBreakPoint, 0, 99); rdI (j, "kbdLeftDepth", o.kbdLeftDepth, 0, 99);
    rdI (j, "kbdRightDepth", o.kbdRightDepth, 0, 99); rdI (j, "kbdLeftCurve", o.kbdLeftCurve, 0, 3);
    rdI (j, "kbdRightCurve", o.kbdRightCurve, 0, 3); rdI (j, "kbdRateScaling", o.kbdRateScaling, 0, 7);
    rdI (j, "ampModSens", o.ampModSens, 0, 3); rdI (j, "keyVelSens", o.keyVelSens, 0, 7);
    rdI (j, "outputLevel", o.outputLevel, 0, 99); rdI (j, "oscMode", o.oscMode, 0, 1);
    rdI (j, "freqCoarse", o.freqCoarse, 0, 31); rdI (j, "freqFine", o.freqFine, 0, 99); rdI (j, "detune", o.detune, 0, 14);
}
std::string dx7Name (const Dx7Patch& p) { return std::string (p.name, strnlen (p.name, 10)); }
json wDx7 (const Dx7Patch& p)
{
    json ops = json::array();
    for (auto& o : p.op) ops.push_back (wOp (o));
    return { { "op", ops }, { "pitchEgRate", u8arr (p.pitchEgRate, 4) }, { "pitchEgLevel", u8arr (p.pitchEgLevel, 4) },
             { "algorithm", (int) p.algorithm }, { "feedback", (int) p.feedback }, { "oscKeySync", (int) p.oscKeySync },
             { "lfoSpeed", (int) p.lfoSpeed }, { "lfoDelay", (int) p.lfoDelay }, { "lfoPitchModDepth", (int) p.lfoPitchModDepth },
             { "lfoAmpModDepth", (int) p.lfoAmpModDepth }, { "lfoKeySync", (int) p.lfoKeySync }, { "lfoWave", (int) p.lfoWave },
             { "lfoPitchModSens", (int) p.lfoPitchModSens }, { "transpose", (int) p.transpose }, { "name", dx7Name (p) } };
}
void rDx7 (const json& j, Dx7Patch& p)
{
    rdArr (j, "op", 6, [&] (size_t i, const json& o) { if (o.is_object()) rOp (o, p.op[i]); });
    rU8arr (j, "pitchEgRate", p.pitchEgRate, 4, 99); rU8arr (j, "pitchEgLevel", p.pitchEgLevel, 4, 99);
    rdI (j, "algorithm", p.algorithm, 0, 31); rdI (j, "feedback", p.feedback, 0, 7); rdI (j, "oscKeySync", p.oscKeySync, 0, 1);
    rdI (j, "lfoSpeed", p.lfoSpeed, 0, 99); rdI (j, "lfoDelay", p.lfoDelay, 0, 99);
    rdI (j, "lfoPitchModDepth", p.lfoPitchModDepth, 0, 99); rdI (j, "lfoAmpModDepth", p.lfoAmpModDepth, 0, 99);
    rdI (j, "lfoKeySync", p.lfoKeySync, 0, 1); rdI (j, "lfoWave", p.lfoWave, 0, 5);
    rdI (j, "lfoPitchModSens", p.lfoPitchModSens, 0, 7); rdI (j, "transpose", p.transpose, 0, 48);
    if (auto* m = member (j, "name"); m && m->is_string()) {
        const std::string s = m->get<std::string>();
        std::memset (p.name, 0, sizeof p.name);
        int n = 0;
        for (char c : s) { if (n >= 10) break; if (c >= 32 && c < 127) p.name[n++] = c; }
    }
}

// ---- layer ---------------------------------------------------------------------------------------
json wLayer (const LayerParams& l)
{
    json lfo = json::array(), mods = json::array();
    for (auto& x : l.lfo) lfo.push_back (wLfo (x));
    for (auto& x : l.mods) mods.push_back (wMod (x));
    return { { "engine", (int) l.engine }, { "name", l.name }, { "analog", wAnalog (l.analog) }, { "wtTable", l.wtTable },
             { "wtPosition", l.wtPosition }, { "fm", wDx7 (l.fm) }, { "fmAmount", l.fmAmount }, { "sampler", wSampler (l.sampler) },
             { "pluck", wPluck (l.pluck) }, { "filter", (int) l.filter }, { "cutoff", l.cutoff }, { "resonance", l.resonance },
             { "drive", l.drive }, { "keyTrack", l.keyTrack }, { "filterEnvAmount", l.filterEnvAmount },
             { "ampEnv", wEnv (l.ampEnv) }, { "filterEnv", wEnv (l.filterEnv) }, { "modEnv", wEnv (l.modEnv) },
             { "useAmpEnv", l.useAmpEnv }, { "lfo", lfo }, { "level", l.level }, { "pan", l.pan }, { "transpose", l.transpose },
             { "fine", l.fine }, { "keyLo", l.keyLo }, { "keyHi", l.keyHi }, { "velLo", l.velLo }, { "velHi", l.velHi },
             { "velocitySens", l.velocitySens }, { "unison", l.unison }, { "unisonDetune", l.unisonDetune },
             { "unisonSpread", l.unisonSpread }, { "glide", l.glide }, { "mode", (int) l.mode }, { "polyphony", l.polyphony },
             { "output", l.output }, { "mods", mods } };
}
void sub (const json& j, const char* k, const std::function<void (const json&)>& f)
{
    if (auto* m = member (j, k); m && m->is_object()) f (*m);
}
void rLayer (const json& j, LayerParams& l)
{
    rdE (j, "engine", l.engine, kNumEngines); rdS (j, "name", l.name);
    sub (j, "analog", [&] (const json& o) { rAnalog (o, l.analog); });
    rdI (j, "wtTable", l.wtTable, 0, kNumWavetables - 1); rdF (j, "wtPosition", l.wtPosition, 0, 1);
    sub (j, "fm", [&] (const json& o) { rDx7 (o, l.fm); });
    rdF (j, "fmAmount", l.fmAmount, 0, 2);
    sub (j, "sampler", [&] (const json& o) { rSampler (o, l.sampler); });
    sub (j, "pluck", [&] (const json& o) { rPluck (o, l.pluck); });
    rdE (j, "filter", l.filter, kNumFilterTypes); rdF (j, "cutoff", l.cutoff, 20, 20000); rdF (j, "resonance", l.resonance, 0, 1);
    rdF (j, "drive", l.drive, 0, 1); rdF (j, "keyTrack", l.keyTrack, 0, 1); rdF (j, "filterEnvAmount", l.filterEnvAmount, -1, 1);
    sub (j, "ampEnv", [&] (const json& o) { rEnv (o, l.ampEnv); });
    sub (j, "filterEnv", [&] (const json& o) { rEnv (o, l.filterEnv); });
    sub (j, "modEnv", [&] (const json& o) { rEnv (o, l.modEnv); });
    rdB (j, "useAmpEnv", l.useAmpEnv);
    rdArr (j, "lfo", kLayerLfos, [&] (size_t i, const json& o) { if (o.is_object()) rLfo (o, l.lfo[i]); });
    rdF (j, "level", l.level, 0, 1); rdF (j, "pan", l.pan, -1, 1); rdI (j, "transpose", l.transpose, -48, 48);
    rdF (j, "fine", l.fine, -100, 100);
    rdI (j, "keyLo", l.keyLo, 0, 127); rdI (j, "keyHi", l.keyHi, 0, 127); rdI (j, "velLo", l.velLo, 0, 127); rdI (j, "velHi", l.velHi, 0, 127);
    rdF (j, "velocitySens", l.velocitySens, 0, 1); rdI (j, "unison", l.unison, 1, kMaxUnison);
    rdF (j, "unisonDetune", l.unisonDetune, 0, 1); rdF (j, "unisonSpread", l.unisonSpread, 0, 1); rdF (j, "glide", l.glide, 0, 10);
    rdE (j, "mode", l.mode, kNumVoiceModes); rdI (j, "polyphony", l.polyphony, 1, kMaxPolyphony); rdI (j, "output", l.output, 0, kOutputPairs - 1);
    rdArr (j, "mods", kMatrixSlots, [&] (size_t i, const json& o) { if (o.is_object()) rMod (o, l.mods[i]); });
}

// ---- effects -------------------------------------------------------------------------------------
json wChorus (const ChorusParams& v) { return { { "on", v.on }, { "rate", v.rate }, { "depth", v.depth }, { "mix", v.mix } }; }
void rChorus (const json& j, ChorusParams& v)
{
    rdB (j, "on", v.on); rdF (j, "rate", v.rate, 0.05f, 5); rdF (j, "depth", v.depth, 0, 1); rdF (j, "mix", v.mix, 0, 1);
}
json wDelay (const DelayParams& v)
{
    return { { "on", v.on }, { "sync", v.sync }, { "beats", v.beats }, { "ms", v.ms }, { "feedback", v.feedback },
             { "mix", v.mix }, { "tone", v.tone }, { "pingPong", v.pingPong } };
}
void rDelay (const json& j, DelayParams& v)
{
    rdB (j, "on", v.on); rdB (j, "sync", v.sync); rdF (j, "beats", v.beats, 0.0625f, 4); rdF (j, "ms", v.ms, 1, 2000);
    rdF (j, "feedback", v.feedback, 0, 0.95f); rdF (j, "mix", v.mix, 0, 1); rdF (j, "tone", v.tone, 0, 1); rdB (j, "pingPong", v.pingPong);
}
json wReverb (const ReverbParams& v)
{
    return { { "on", v.on }, { "size", v.size }, { "damping", v.damping }, { "mix", v.mix }, { "predelayMs", v.predelayMs }, { "width", v.width } };
}
void rReverb (const json& j, ReverbParams& v)
{
    rdB (j, "on", v.on); rdF (j, "size", v.size, 0, 1); rdF (j, "damping", v.damping, 0, 1); rdF (j, "mix", v.mix, 0, 1);
    rdF (j, "predelayMs", v.predelayMs, 0, 200); rdF (j, "width", v.width, 0, 1);
}

} // namespace

std::string patchToJson (const Patch& p)
{
    try {
        json layers = json::array(), glfo = json::array(), macros = json::array();
        json sNames = json::array(), sRoots = json::array(), cVoices = json::array(), cNames = json::array();
        for (auto& l : p.layers) layers.push_back (wLayer (l));
        for (auto& l : p.globalLfo) glfo.push_back (wLfo (l));
        for (auto& m : p.macroNames) macros.push_back (m);
        for (size_t i = 0; i < (size_t) kMaxLayers; ++i) {
            sNames.push_back (p.samples[i] ? p.samples[i]->name : std::string());
            sRoots.push_back (p.samples[i] ? p.samples[i]->rootNote : 60);
            json v = json::array();
            if (p.carts[i]) for (auto& d : *p.carts[i]) v.push_back (dx7Name (d));
            cVoices.push_back (v);
            cNames.push_back (p.cartNames[i]);
        }
        json r = { { "name", p.name }, { "category", p.category }, { "layers", layers }, { "globalLfo", glfo },
                   { "macroNames", macros }, { "chorus", wChorus (p.chorus) }, { "delay", wDelay (p.delay) },
                   { "reverb", wReverb (p.reverb) }, { "masterVolume", p.masterVolume }, { "bendRange", p.bendRange },
                   { "sampleNames", sNames }, { "sampleRoots", sRoots }, { "cartVoices", cVoices }, { "cartNames", cNames } };
        return r.dump (-1, ' ', false, json::error_handler_t::replace);
    } catch (...) { return "{}"; }
}

bool patchFromJson (const std::string& text, Patch& p, std::string* error)
{
    try {
        json j = json::parse (text);
        if (! j.is_object()) { if (error) *error = "top-level JSON value must be an object"; return false; }
        Patch w = p;   // work on a copy; samples/carts are carried over untouched
        rdS (j, "name", w.name); rdS (j, "category", w.category);
        rdArr (j, "layers", kMaxLayers, [&] (size_t i, const json& o) { if (o.is_object()) rLayer (o, w.layers[i]); });
        rdArr (j, "globalLfo", kGlobalLfos, [&] (size_t i, const json& o) { if (o.is_object()) rLfo (o, w.globalLfo[i]); });
        if (auto* m = member (j, "macroNames"); m && m->is_array())
            for (size_t i = 0; i < (size_t) kMacros && i < m->size(); ++i)
                if ((*m)[i].is_string()) { w.macroNames[i] = (*m)[i].get<std::string>(); if (w.macroNames[i].size() > kMaxStr) w.macroNames[i].resize (kMaxStr); }
        sub (j, "chorus", [&] (const json& o) { rChorus (o, w.chorus); });
        sub (j, "delay", [&] (const json& o) { rDelay (o, w.delay); });
        sub (j, "reverb", [&] (const json& o) { rReverb (o, w.reverb); });
        rdF (j, "masterVolume", w.masterVolume, 0, 1); rdF (j, "bendRange", w.bendRange, 0, 24);
        p = std::move (w);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
    } catch (...) {
        if (error) *error = "unknown error";
    }
    return false;
}

} // namespace el::chimera
