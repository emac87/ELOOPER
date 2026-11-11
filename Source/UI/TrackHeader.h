#pragma once
// The label under each loop-page column (pinned along the bottom of the screen): colour stripe,
// name and input. The plug-in icon and the M / S / arm / FX buttons live in the track's first
// loop box (see TrackControls). Click selects; double-click renames; right-click opens the track
// menu (which also has Rename); drag sideways reorders. A multi-out instrument shows an "outs"
// toggle that folds its outputs and sum folder away.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/PresetManager.h"
#include "UI/Selection.h"
#include "UI/Theme.h"
#include <functional>
#include <memory>

namespace el {

class TrackHeader : public juce::Component, public juce::SettableTooltipClient {
public:
    static constexpr int rowHeight = 96;      // label height
    static constexpr int defaultWidth = theme::columnWidth;   // = loop column width

    TrackHeader (EngineController&, Selection&, PresetManager&, Id trackId, int width = defaultWidth);
    ~TrackHeader() override;

    Id getTrackId() const noexcept { return trackId; }
    void refresh();   // re-read name / colour / input (cheap)

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
    juce::Rectangle<int> folderR;
    bool dragging = false;
};

} // namespace el
