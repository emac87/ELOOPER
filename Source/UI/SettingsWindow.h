#pragma once
// Settings window: audio device (incl. buffer size), MIDI devices, controller mappings,
// plug-in scanning and default loop settings. Single instance; no modal loops.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/MidiMapping.h"

namespace el {

void showSettingsWindow (EngineController&, MidiMapping&);

} // namespace el
