#pragma once
// The track controls on a loop-page track label (under its column): the plug-in icon (click:
// show / hide the plug-in, right-click: all plug-ins; on an empty instrument track it opens the
// instrument picker), Mute / Solo / Record arm, the monitor button (Auto / On / Off) and
// Pre FX / Post FX buttons (each opens that chain's plug-ins; empty: the plug-in picker), and the
// In (input gain) and Pan bars under them.
// The rest of the channel (instrument, MIDI FX, EQ, sends, Vol, output) is on the mixer.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/PresetManager.h"
#include "UI/Selection.h"
#include <memory>

namespace el {

class PluginIconButton;
class ValueBar;

class TrackControls {
public:
    TrackControls (EngineController&, Selection&, PresetManager&, Id trackId);
    ~TrackControls();

    void addTo (juce::Component& parent);
    // Places the parts (parent coordinates). Monitor / arm only show on audio and instrument tracks.
    void layout (juce::Rectangle<int> icon, juce::Rectangle<int> msr, juce::Rectangle<int> monitor, juce::Rectangle<int> fx,
                 juce::Rectangle<int> inBar, juce::Rectangle<int> panBar);
    void refresh();                               // names / plug-in counts changed
    void poll();                                  // states (cheap; call a few times a second)
    void pollValues();                            // In / Pan bars (every UI tick, so automation moves smoothly)

    class Buttons;
    class ChainButton;
    class MonitorButton;

private:
    std::unique_ptr<PluginIconButton> icon;
    std::unique_ptr<Buttons> buttons;
    std::unique_ptr<ChainButton> pre, post;
    std::unique_ptr<MonitorButton> monitor;
    std::unique_ptr<ValueBar> gainBar, panBar;
    bool hasMonitor = false;
};

} // namespace el
