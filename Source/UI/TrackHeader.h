#pragma once
// The label at the left of each loop-page row: colour stripe, track icon, name and input. No mixer
// controls (those live on the mixer). Click selects, double-click renames, right-click opens the
// track menu, drag up / down reorders. A multi-out instrument shows an "outs" toggle that folds
// its outputs and sum folder away.
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
    static constexpr int rowHeight = 96;
    static constexpr int defaultWidth = 176;

    TrackHeader (EngineController&, Selection&, PresetManager&, Id trackId, int width = defaultWidth);
    ~TrackHeader() override;

    Id getTrackId() const noexcept { return trackId; }
    void refresh();   // re-read name / colour / input (cheap)
    void poll() {}

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
    std::unique_ptr<PluginIconButton> icon;
    bool dragging = false;
};

} // namespace el
