// Chimera for the web: a C API over the real synth engine, compiled to WebAssembly and driven
// from an AudioWorklet (web/src/worklet.js). Everything here runs on the worklet's audio thread,
// one call at a time, so there is no locking; patch changes are published between blocks.
#include "Synth/dsp/Synth.h"
#include "Synth/dsp/Presets.h"
#include "Synth/dsp/FmLibrary.h"
#include "Synth/dsp/PatchJson.h"
#include <emscripten/emscripten.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace el::chimera;

namespace {
constexpr int kBlock = 128;          // Web Audio render quantum

struct State {
    Synth synth;
    Patch working;
    std::vector<Patch> presets;
    LiveParams live;
    float macros[kMacros] {};
    std::vector<MidiEvent> pending;
    float outL[kBlock] {}, outR[kBlock] {};
    std::string json, text;
    std::vector<uint8_t> bytes;
    int currentPreset = 0;
    uint32_t rng = 0x9e3779b9u;
};
State* st = nullptr;

uint32_t nextRand()
{
    auto& x = st->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

void publish()
{
    st->live = LiveParams::fromPatch (st->working);
    for (int i = 0; i < kMacros; ++i) st->live.macros[i] = st->macros[i];
    st->synth.patches().publish (std::make_shared<const Patch> (st->working));
}

void setPatch (const Patch& p)
{
    st->working = p;
    for (auto& m : st->macros) m = 0.0f;   // presets start from neutral macros, like the plug-in
    publish();
}

bool validLayer (int layer) { return layer >= 0 && layer < kMaxLayers; }
} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int chimera_init (double sampleRate)
{
    if (st == nullptr) st = new State();
    st->synth.prepare (sampleRate, kBlock);
    st->presets = makeFactoryPresets();
    st->pending.reserve (1024);
    st->currentPreset = 0;
    for (int i = 0; i < (int) st->presets.size(); ++i)
        if (st->presets[(size_t) i].category != "Init") { st->currentPreset = i; break; }   // open on a real sound
    setPatch (st->presets.empty() ? Patch() : st->presets[(size_t) st->currentPreset]);
    return (int) st->presets.size();
}

EMSCRIPTEN_KEEPALIVE void chimera_seed (uint32_t seed) { if (st) st->rng = seed ? seed : 0x9e3779b9u; }

// ---- audio --------------------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE void chimera_midi (int status, int d1, int d2)
{
    if (! st || st->pending.size() >= 1024) return;
    st->pending.push_back ({ 0, (uint8_t) status, (uint8_t) (d1 & 127), (uint8_t) (d2 & 127) });
}

// Renders n (<= 128) frames; returns the left buffer, right follows at +128 floats.
EMSCRIPTEN_KEEPALIVE float* chimera_render (int n)
{
    if (! st) return nullptr;
    n = std::clamp (n, 0, kBlock);
    float* outs[2] = { st->outL, st->outR };
    std::fill (st->outL, st->outL + kBlock, 0.0f);
    std::fill (st->outR, st->outR + kBlock, 0.0f);
    st->synth.process (outs, 2, n, st->pending.data(), (int) st->pending.size(), 120.0, st->live);
    st->pending.clear();
    return st->outL;
}

EMSCRIPTEN_KEEPALIVE int chimera_voices() { return st ? st->synth.activeVoiceCount() : 0; }
EMSCRIPTEN_KEEPALIVE float chimera_peak() { return st ? st->synth.peakLevel() : 0.0f; }
EMSCRIPTEN_KEEPALIVE void chimera_panic() { if (st) st->synth.panic(); }

EMSCRIPTEN_KEEPALIVE void chimera_macro (int i, float v)
{
    if (! st || i < 0 || i >= kMacros) return;
    st->macros[i] = std::clamp (v, 0.0f, 1.0f);
    st->live.macros[i] = st->macros[i];
}

// ---- patch --------------------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE const char* chimera_patch_json()
{
    if (! st) return "";
    st->json = patchToJson (st->working);
    return st->json.c_str();
}

// Returns 1 on success; on failure the patch is unchanged and chimera_last_error() says why.
EMSCRIPTEN_KEEPALIVE int chimera_set_patch_json (const char* json)
{
    if (! st || ! json) return 0;
    Patch p = st->working;
    std::string err;
    if (! patchFromJson (json, p, &err)) { st->text = err; return 0; }
    st->working = p;
    publish();
    return 1;
}

EMSCRIPTEN_KEEPALIVE const char* chimera_last_error() { return st ? st->text.c_str() : ""; }

// ---- presets ------------------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int chimera_preset_count() { return st ? (int) st->presets.size() : 0; }
EMSCRIPTEN_KEEPALIVE const char* chimera_preset_name (int i)
{
    return st && i >= 0 && i < (int) st->presets.size() ? st->presets[(size_t) i].name.c_str() : "";
}
EMSCRIPTEN_KEEPALIVE const char* chimera_preset_category (int i)
{
    return st && i >= 0 && i < (int) st->presets.size() ? st->presets[(size_t) i].category.c_str() : "";
}
EMSCRIPTEN_KEEPALIVE int chimera_current_preset() { return st ? st->currentPreset : -1; }

EMSCRIPTEN_KEEPALIVE int chimera_load_preset (int i)
{
    if (! st || i < 0 || i >= (int) st->presets.size()) return 0;
    st->currentPreset = i;
    setPatch (st->presets[(size_t) i]);
    return 1;
}

// Dice: a different factory preset at random; returns its index.
EMSCRIPTEN_KEEPALIVE int chimera_random_preset()
{
    if (! st || st->presets.empty()) return -1;
    const int n = (int) st->presets.size();
    int pick = (int) (nextRand() % (uint32_t) n);
    if (pick == st->currentPreset && n > 1) pick = (pick + 1 + (int) (nextRand() % (uint32_t) (n - 1))) % n;
    chimera_load_preset (pick);
    return pick;
}

// Shift-dice: a brand-new random patch.
EMSCRIPTEN_KEEPALIVE void chimera_random_patch()
{
    if (! st) return;
    st->currentPreset = -1;
    setPatch (randomPatch (nextRand() & 0x3fffffffu, chimeraCart()));
}

// ---- carts --------------------------------------------------------------------------------------
namespace {
void useCart (int layer, std::shared_ptr<const std::vector<Dx7Patch>> voices, const std::string& name)
{
    st->working.carts[(size_t) layer] = voices;
    st->working.cartNames[(size_t) layer] = name;
    auto& lp = st->working.layers[(size_t) layer];
    lp.engine = Engine::FM;
    lp.fm = voices->front();
    lp.useAmpEnv = false;
    publish();
}
}

// Loads a .syx (32-voice bulk or single voice) into a layer. Returns the voice count, 0 on error.
EMSCRIPTEN_KEEPALIVE int chimera_load_cart (int layer, const uint8_t* data, int size, const char* name)
{
    if (! st || ! validLayer (layer) || ! data || size <= 0) return 0;
    auto voices = std::make_shared<std::vector<Dx7Patch>>();
    std::string err;
    if (! parseDx7SysEx (data, (size_t) size, *voices, err) || voices->empty()) {
        st->text = err.empty() ? "Not a DX7 voice or cartridge file." : err;
        return 0;
    }
    useCart (layer, voices, name ? name : "Cart");
    return (int) voices->size();
}

EMSCRIPTEN_KEEPALIVE void chimera_builtin_cart (int layer)
{
    if (! st || ! validLayer (layer)) return;
    useCart (layer, std::make_shared<const std::vector<Dx7Patch>> (chimeraCart()), kChimeraCartName);
}

EMSCRIPTEN_KEEPALIVE void chimera_cart_voice (int layer, int index)
{
    if (! st || ! validLayer (layer)) return;
    const auto cart = st->working.carts[(size_t) layer];
    if (! cart || index < 0 || index >= (int) cart->size()) return;
    st->working.layers[(size_t) layer].fm = (*cart)[(size_t) index];
    st->working.layers[(size_t) layer].engine = Engine::FM;
    publish();
}

// ---- built-in cart library (Synth/dsp/FmLibrary.h) ---------------------------------------------
// The categories as JSON: [{"name":"Chimera","carts":1,"first":0}, ...].
EMSCRIPTEN_KEEPALIVE const char* chimera_library_info()
{
    if (! st) return "[]";
    std::string s = "[";
    for (int c = 0; c < fmLibCategoryCount(); ++c) {
        if (c) s += ",";
        s += std::string ("{\"name\":\"") + fmLibCategoryName (c) + "\",\"carts\":" + std::to_string (fmLibCategoryCarts (c))
           + ",\"first\":" + std::to_string (fmLibCategoryFirstCart (c)) + "}";
    }
    st->json = s + "]";
    return st->json.c_str();
}

// Loads library cart `cart` into a layer and selects `voice`. Returns the cart's voice count.
EMSCRIPTEN_KEEPALIVE int chimera_library_cart (int layer, int cart, int voice)
{
    if (! st || ! validLayer (layer)) return 0;
    cart = std::clamp (cart, 0, fmLibCartCount() - 1);
    auto voices = std::make_shared<const std::vector<Dx7Patch>> (fmLibCart (cart));
    if (voices->empty()) return 0;
    useCart (layer, voices, fmLibCartName (cart));
    voice = std::clamp (voice, 0, (int) voices->size() - 1);
    if (voice > 0) { st->working.layers[(size_t) layer].fm = (*voices)[(size_t) voice]; publish(); }
    return (int) voices->size();
}

// Dice: a random voice from the whole library, or (sameCategory) from the category of the
// layer's cart. Returns the voice's index in its cart.
EMSCRIPTEN_KEEPALIVE int chimera_random_library_voice (int layer, int sameCategory)
{
    if (! st || ! validLayer (layer)) return -1;
    const int cur = st->working.carts[(size_t) layer] ? fmLibFindCart (st->working.cartNames[(size_t) layer]) : -1;
    int cart = 0;
    if (sameCategory && cur >= 0) {
        const int c = fmLibCartCategory (cur);
        cart = fmLibCategoryFirstCart (c) + (int) (nextRand() % (uint32_t) fmLibCategoryCarts (c));
    } else {
        int v = (int) (nextRand() % (uint32_t) fmLibVoiceCount());
        for (cart = 0; cart < fmLibCartCount() - 1; ++cart) {
            const int size = (int) fmLibCart (cart).size();
            if (v < size) break;
            v -= size;
        }
    }
    const int voice = (int) (nextRand() % (uint32_t) fmLibCart (cart).size());
    chimera_library_cart (layer, cart, voice);
    return voice;
}

// Dice on the FM page: a random voice from the layer's cart (Chimera's own if none). Returns it.
EMSCRIPTEN_KEEPALIVE int chimera_random_cart_voice (int layer)
{
    if (! st || ! validLayer (layer)) return -1;
    if (! st->working.carts[(size_t) layer] || st->working.carts[(size_t) layer]->empty()) chimera_builtin_cart (layer);
    const auto cart = st->working.carts[(size_t) layer];
    if (! cart || cart->empty()) return -1;
    const int i = (int) (nextRand() % (uint32_t) cart->size());
    chimera_cart_voice (layer, i);
    return i;
}

// Builds the layer's cart (or just its voice) as a 4104-byte .syx; returns the size, bytes via chimera_bytes().
EMSCRIPTEN_KEEPALIVE int chimera_save_cart (int layer)
{
    if (! st || ! validLayer (layer)) return 0;
    std::vector<Dx7Patch> voices;
    if (auto c = st->working.carts[(size_t) layer]) voices = *c;
    if (voices.empty()) voices.push_back (st->working.layers[(size_t) layer].fm);
    st->bytes = dx7MakeCart (voices);
    return (int) st->bytes.size();
}
EMSCRIPTEN_KEEPALIVE const uint8_t* chimera_bytes() { return st ? st->bytes.data() : nullptr; }

// ---- samples ------------------------------------------------------------------------------------
// left/right are copied; right may be null (mono). The layer switches to the Sampler engine.
EMSCRIPTEN_KEEPALIVE int chimera_load_sample (int layer, const float* left, const float* right, int length,
                                              double sampleRate, const char* name)
{
    if (! st || ! validLayer (layer) || ! left || length < 4 || sampleRate <= 0) return 0;
    auto s = std::make_shared<SampleData>();
    s->left.assign (left, left + length);
    if (right) s->right.assign (right, right + length);
    s->sampleRate = sampleRate;
    s->rootNote = 60;
    s->name = name ? name : "Sample";
    st->working.samples[(size_t) layer] = s;
    st->working.layers[(size_t) layer].engine = Engine::Sampler;
    publish();
    return 1;
}

// Scratch memory for JS to pass arrays in (malloc/free exported too).
EMSCRIPTEN_KEEPALIVE void* chimera_alloc (int bytes) { return std::malloc ((size_t) std::max (1, bytes)); }
EMSCRIPTEN_KEEPALIVE void chimera_free (void* p) { std::free (p); }

} // extern "C"
