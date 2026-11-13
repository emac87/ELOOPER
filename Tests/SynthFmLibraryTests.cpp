// Chimera's built-in FM cart library: categories, cart names and every voice.
#include "TestFramework.h"
#include "Synth/dsp/FmLibrary.h"
#include "Synth/dsp/Presets.h"
#include <cmath>
#include <vector>

using namespace el::chimera;

EL_TEST (fm_library_layout)
{
    EL_CHECK (fmLibCategoryCount() > 10);
    EL_CHECK (std::string (fmLibCategoryName (0)) == "Chimera");
    EL_CHECK (fmLibCartName (0) == kChimeraCartName);
    EL_CHECK (fmLibCart (0).size() == chimeraCart().size());
    int carts = 0, voices = 0;
    for (int c = 0; c < fmLibCategoryCount(); ++c) {
        EL_CHECK (fmLibCategoryCarts (c) >= 1);
        EL_CHECK (fmLibCategoryFirstCart (c) == carts);
        carts += fmLibCategoryCarts (c);
    }
    EL_CHECK (fmLibCartCount() == carts);
    EL_CHECK (carts > 500);
    for (int i = 0; i < carts; ++i) {
        const auto cart = fmLibCart (i);
        EL_CHECK (! cart.empty() && cart.size() <= 32);
        voices += (int) cart.size();
        const auto name = fmLibCartName (i);
        EL_CHECK (fmLibFindCart (name) == i);     // names are unique and round-trip
        const int c = fmLibCartCategory (i);
        EL_CHECK (i >= fmLibCategoryFirstCart (c) && i < fmLibCategoryFirstCart (c) + fmLibCategoryCarts (c));
        for (const auto& v : cart) {
            EL_CHECK (v.algorithm < 32 && v.feedback <= 7 && v.transpose <= 48);
            for (const auto& o : v.op) EL_CHECK (o.outputLevel <= 99 && o.freqCoarse <= 31 && o.detune <= 14);
        }
    }
    EL_CHECK (voices == fmLibVoiceCount());
    EL_CHECK (fmLibFindCart ("Nope 3") == -1 && fmLibFindCart ("Bass 0") == -1 && fmLibFindCart ("My cart") == -1);
    EL_CHECK (fmLibFindCart ("Bass 99999") == -1);
}

// A voice from every cart renders finite audio (the whole library would take too long here).
EL_TEST (fm_library_voices_render)
{
    std::vector<float> b (2400);
    int sounding = 0, tried = 0;
    for (int i = 0; i < fmLibCartCount(); ++i) {
        const auto cart = fmLibCart (i);
        const auto& v = cart[(size_t) (i * 7) % cart.size()];
        FmVoice fv;
        fv.prepare (48000.0f);
        fv.noteOn (v, 60, 100);
        float pk = 0;
        for (int k = 0; k < 4; ++k) {
            fv.render (b.data(), (int) b.size(), 0.0f, 1.0f);
            for (float s : b) { EL_CHECK (std::isfinite (s)); pk = std::max (pk, std::fabs (s)); }
        }
        EL_CHECK (pk < 8.0f);
        ++tried; if (pk > 0.001f) ++sounding;
    }
    EL_CHECK (sounding > tried * 9 / 10);
}
