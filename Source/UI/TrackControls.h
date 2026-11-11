#pragma once
// The track controls that sit in a track's first loop box on the loop page: the plug-in icon
// (click: show / hide the plug-in, right-click: all plug-ins; on an empty instrument track it
// opens the instrument picker), Mute / Solo / Record arm along the top, and Pre FX / Post FX
// buttons along the bottom (each opens that chain's plug-ins; empty: the plug-in picker).
// The rest of the channel (instrument, MIDI FX, EQ, sends, In, Pan, Vol, output) is on the mixer.
//
// These are separate child components laid over the loop box's top and bottom bands, so the
// middle of the box still triggers the loop.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/PresetManager.h"
#include "UI/Selection.h"
#include <memory>

namespace el {

class PluginIconButton;

class TrackControls {
public:
    TrackControls (EngineController&, Selection&, PresetManager&, Id trackId);
    ~TrackControls();

    void addTo (juce::Component& parent);
    void setBox (juce::Rectangle<int> loopBox);   // the first loop box, in the parent's coordinates
    void refresh();                               // names / plug-in counts changed
    void poll();                                  // states (cheap; call a few times a second)

    // Where a loop box with controls keeps its own drawing (relative to the box).
    static constexpr int topBandH = 30;           // icon + M / S / R
    static constexpr int fxBandY = 89, fxBandH = 17;

    class Buttons;
    class ChainButton;

private:
    std::unique_ptr<PluginIconButton> icon;
    std::unique_ptr<Buttons> buttons;
    std::unique_ptr<ChainButton> pre, post;
};

} // namespace el
