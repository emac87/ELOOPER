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

// One plug-in's options (show / hide, bypass, replace, presets, move, remove), shared by the mixer
// chips and the loop page's Pre FX / Post FX buttons.
void showPluginSlotMenu (EngineController&, PresetManager&, Id trackId, FxChain, int index, juce::Component* target);

// A whole chain (Pre FX or Post FX) as one menu: each plug-in (click: show / hide) with its
// options in a submenu, then "Add plug-in...". An empty chain opens the plug-in picker directly.
void showChainMenu (EngineController&, PresetManager&, Id trackId, FxChain, juce::Component* target);

} // namespace el
