// Chimera factory presets: names, categories, audibility, level safety and voice release.
#include "TestFramework.h"
#include "Synth/dsp/Presets.h"
#include "Synth/dsp/Synth.h"
#include <algorithm>
#include <cstdlib>
#include <set>
#include <string>

using namespace el::chimera;

namespace {
const char* const kCategories[] = { "Init", "Bass", "Lead", "Pad", "Keys", "FM", "Pluck", "Texture", "Layered" };

int categoryIndex (const std::string& c)
{
    for (int i = 0; i < 9; ++i) if (c == kCategories[i]) return i;
    return -1;
}

bool hasSampler (const Patch& p)
{
    for (auto& l : p.layers) if (l.engine == Engine::Sampler) return true;
    return false;
}

struct Result { double rms = 0, peak = 0; bool finite = true; int voicesAfter = -1; };

// Holds `note` for holdSec, then (optionally) releases it and renders tailSec more.
Result play (const Patch& p, int note, double holdSec, double tailSec, double* rmsFirstWindow = nullptr)
{
    constexpr int kBlock = 256;
    Synth s;
    s.prepare (48000.0, kBlock);
    s.patches().publish (std::make_shared<const Patch> (p));
    std::vector<float> l (kBlock), r (kBlock);
    float* outs[2] { l.data(), r.data() };
    LiveParams live = LiveParams::fromPatch (p);
    Result res;
    double maxWin = 0, winSum = 0; int winCount = 0;
    const int holdBlocks = (int) (holdSec * 48000.0 / kBlock);
    const int tailBlocks = (int) (tailSec * 48000.0 / kBlock);
    for (int b = 0; b < holdBlocks + tailBlocks; ++b) {
        MidiEvent ev { 0, 0, 0, 0 };
        int numEv = 0;
        if (b == 0) { ev = { 0, 0x90, (uint8_t) note, 100 }; numEv = 1; }
        if (b == holdBlocks) { ev = { 0, 0x80, (uint8_t) note, 0 }; numEv = 1; }
        s.process (outs, 2, kBlock, numEv ? &ev : nullptr, numEv, 120.0, live);
        for (int i = 0; i < kBlock; ++i) {
            const double v = l[(size_t) i], w = r[(size_t) i];
            if (! std::isfinite (v) || ! std::isfinite (w)) res.finite = false;
            res.peak = std::max (res.peak, std::max (std::abs (v), std::abs (w)));
            winSum += v * v + w * w;
        }
        if (++winCount == 19) {   // ~100 ms windows
            maxWin = std::max (maxWin, std::sqrt (winSum / (winCount * kBlock * 2)));
            winSum = 0; winCount = 0;
        }
        if (b < holdBlocks) res.rms = maxWin;
    }
    res.voicesAfter = s.activeVoiceCount();
    if (rmsFirstWindow) *rmsFirstWindow = res.rms;
    return res;
}
}

EL_TEST (presets_names_and_categories)
{
    const auto presets = makeFactoryPresets();
    EL_CHECK (presets.size() >= 45);
    std::set<std::string> names;
    int lastCat = 0;
    for (auto& p : presets) {
        EL_CHECK (! p.name.empty());
        EL_CHECK (p.name.size() <= 20);
        EL_CHECK (names.insert (p.name).second);
        const int c = categoryIndex (p.category);
        if (c < 0) std::printf ("  bad category '%s' on '%s'\n", p.category.c_str(), p.name.c_str());
        EL_CHECK (c >= 0);
        if (c < lastCat) std::printf ("  category order broken at '%s'\n", p.name.c_str());
        EL_CHECK (c >= lastCat);
        lastCat = std::max (lastCat, c);
    }
    EL_CHECK (presets.front().category == "Init");
    int inits = 0;
    for (auto& p : presets) inits += p.category == "Init" ? 1 : 0;
    EL_CHECK (inits >= 1 && inits <= 2);
}

EL_TEST (presets_audible_finite_and_bounded)
{
    const bool report = std::getenv ("PRESET_REPORT") != nullptr;
    for (auto& p : makeFactoryPresets()) {
        if (hasSampler (p)) continue;   // no audio ships with the library
        for (int note : { 60, 36 }) {
            if (note == 36 && p.category != "Bass") continue;
            const Result r = play (p, note, 2.0, 0.0);
            if (report) std::printf ("  %-20s %-8s n%d rms %.3f peak %.2f\n", p.name.c_str(), p.category.c_str(), note, r.rms, r.peak);
            if (! (r.rms > 0.005)) std::printf ("  too quiet: %s (note %d) rms %g\n", p.name.c_str(), note, r.rms);
            if (! (r.peak < 4.0)) std::printf ("  too loud: %s (note %d) peak %g\n", p.name.c_str(), note, r.peak);
            EL_CHECK (r.rms > 0.005);
            EL_CHECK (r.peak < 4.0);
            EL_CHECK (r.finite);
        }
    }
}

EL_TEST (presets_voices_end_after_release)
{
    for (auto& p : makeFactoryPresets()) {
        if (hasSampler (p)) continue;
        bool shortRelease = true;
        for (auto& l : p.layers) if (l.engine != Engine::Off && l.ampEnv.release >= 8.0f) shortRelease = false;
        if (! shortRelease) continue;
        const Result r = play (p, p.category == "Bass" ? 36 : 60, 1.5, 12.0);
        if (r.voicesAfter != 0) std::printf ("  voices still active after release: %s (%d)\n", p.name.c_str(), r.voicesAfter);
        EL_CHECK (r.voicesAfter == 0);
        EL_CHECK (r.finite);
    }
}
