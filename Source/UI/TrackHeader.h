#pragma once
// The label under each loop-page column (pinned along the bottom of the screen): colour stripe,
// plug-in icon button, name and input. No mixer controls (those live on the mixer). The icon
// shows / hides the track's plug-in window (right-click: every plug-in on the track; on an empty
// instrument track it opens the instrument picker). Click the name to rename; click elsewhere
// selects; right-click anywhere else opens the track menu; drag sideways reorders. A multi-out
// instrument shows an "outs" toggle that folds its outputs and sum folder away.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/PresetManager.h"
#include "UI/Selection.h"
#include <functional>
#include <memory>

namespace el {

class PluginIconButton;

class TrackHeader : public juce::Component, public juce::SettableTooltipClient {
public:
    static constexpr int rowHeight = 96;      // label height
    static constexpr int defaultWidth = 96;   // = loop cell width

    TrackHeader (EngineController&, Selection&, PresetManager&, Id trackId, int width = defaultWidth);
    ~TrackHeader() override;

    Id getTrackId() const noexcept { return trackId; }
    void refresh();   // re-read name / colour / input (cheap)
    void poll();   // lights the icon while a plug-in window is open

    std::function<void (const juce::MouseEvent&)> onReorderDrop;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    EngineController& controller;
    Selection& selection;
    PresetManager& presets;
    const Id trackId;

    juce::Colour colour;
    juce::String nameText, line1;
    int numOuts = 0;
    bool folderOpen = true;
    juce::Rectangle<int> folderR, nameR;
    std::unique_ptr<PluginIconButton> icon;
    bool dragging = false;
};

} // namespace el
