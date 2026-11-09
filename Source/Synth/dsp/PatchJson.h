#pragma once
// JSON codec for Chimera patches (used by the web build's JS UI). Schema: web/PATCH_SCHEMA.md.
#include "Synth/dsp/Patch.h"

namespace el::chimera {

// Compact JSON of every Patch field except samples/carts, plus read-only info keys
// (sampleNames, sampleRoots, cartVoices, cartNames).
std::string patchToJson (const Patch& p);

// Updates p in place from the keys present (missing keys keep p's values); every value is clamped
// to its valid range. Never throws. Returns false (p untouched) if the text is not a JSON object.
// Never changes p.samples / p.carts; read-only info keys are ignored.
bool patchFromJson (const std::string& json, Patch& p, std::string* error = nullptr);

} // namespace el::chimera
