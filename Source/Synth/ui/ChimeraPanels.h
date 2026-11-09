#pragma once
// Chimera editor sections (each one a Panel bound to the current layer through Ctx).
#include "Synth/ui/ChimeraWidgets.h"

namespace el::chimera {

// Things the layer panel needs from the editor (clipboard, remembering the last engine).
struct LayerHooks {
    std::function<void()> copyLayer, pasteLayer, initLayer;
    std::function<void (Engine)> setEngine;
    std::function<bool()> canPaste;
};

std::unique_ptr<Panel> makeVoicePanel (Ctx&, LayerHooks&);
std::unique_ptr<Panel> makeEnginePanel (Ctx&, LayerHooks&);
std::unique_ptr<Panel> makeFilterPanel (Ctx&);
std::unique_ptr<Panel> makeEnvPanel (Ctx&, int which);          // 0 amp, 1 filter, 2 mod
std::unique_ptr<Panel> makeLfoPanel (Ctx&, bool global, int index);
std::unique_ptr<Panel> makeMatrixPanel (Ctx&);
std::unique_ptr<Panel> makeFxPanel (Ctx&, int which);            // 0 chorus, 1 delay, 2 reverb

} // namespace el::chimera
