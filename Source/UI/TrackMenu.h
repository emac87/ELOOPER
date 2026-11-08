#pragma once
// The track right-click menu (rename, colour, input, MIDI input, instrument, monitor, output,
// channel presets, multi-out, bounce, remove), shared by the loop page and the mixer.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/PresetManager.h"

namespace el {

void showTrackMenu (EngineController&, PresetManager&, Id trackId, juce::Component* target);
void renameTrack (EngineController&, Id trackId);
void confirmBounceTrack (EngineController&, Id trackId);
void confirmRemoveTrack (EngineController&, Id trackId);

} // namespace el
