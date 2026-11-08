#pragma once
// Plug-in picker: a Logic-style async popup menu (recent, by type, by maker) that inserts the chosen plug-in
// into a track's chain, plus a small progress window for plug-in scanning.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include <functional>

namespace el {

// Shows the plug-in menu for `chain` (Instrument -> instruments, MidiFx -> MIDI effects, Pre/Post -> effects).
// Choosing an entry inserts it at `insertIndex` (-1 = append) and calls onInserted(slotId) on success;
// failures are reported with an async alert. "Scan for Plug-ins..." opens a progress window.
void showPluginMenu (EngineController& c, Id trackId, FxChain chain, int insertIndex, juce::Component* target,
                     std::function<void (Id slotId)> onInserted = {});

// Logic-style routing pickers: Output (Stereo Out / Bus > ... / New Bus) and Send (bus list / New Bus).
void showOutputPicker (EngineController& c, Id trackId, juce::Component* target);
void showSendPicker (EngineController& c, Id trackId, juce::Component* target);

// Starts a scan with a progress window (Cancel stops it). Used by the menu; callable on its own.
void showPluginScanWindow (EngineController& c, std::function<void()> onFinished = {});

} // namespace el
