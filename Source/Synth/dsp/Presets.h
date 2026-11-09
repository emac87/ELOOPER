// Chimera's preset and FM-voice library and patch randomiser: plain C++, shared by the
// ELooper built-in, the AU/VST3/standalone plug-in and the web version.
#pragma once
#include "Synth/dsp/Patch.h"
#include <cstdint>
#include <vector>

namespace el::chimera {

// Factory presets, built in code. Every preset has a name and a category
// ("Bass", "Lead", "Pad", "Keys", "FM", "Pluck", "Texture", "Layered", "Init").
std::vector<Patch> makeFactoryPresets();

// Chimera's own 32-voice FM cart (original voices, not Yamaha's), for the cart picker.
const std::vector<Dx7Patch>& chimeraCart();
inline constexpr const char* kChimeraCartName = "Chimera Voices";

// A new random but musical patch. Deterministic for a given seed. When `fmVoices` is
// non-empty, FM layers may take a voice from it rather than a generated one.
Patch randomPatch (uint32_t seed, const std::vector<Dx7Patch>& fmVoices = {});

} // namespace el::chimera
