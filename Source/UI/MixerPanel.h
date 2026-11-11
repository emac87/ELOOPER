#pragma once
// The show/hide-able mixer: a scrolling row of vertical channel strips laid out exactly like the
// loop page's columns (same width, order and gaps, scrolling with them), so each strip sits right
// under its track's loops; then the plain buses. Master is pinned on the left, under the scene
// buttons.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "UI/Selection.h"
#include "UI/ChannelStrip.h"
#include "Engine/PresetManager.h"

namespace el {

class MixerPanel : public juce::Component,
                   private EngineController::Listener,
                   private juce::ChangeListener,
                   private juce::Timer,
                   private juce::AsyncUpdater {
public:
    MixerPanel (EngineController&, Selection&, PresetManager&);
    ~MixerPanel() override;


    void paint (juce::Graphics&) override;
    void resized() override;

    // Horizontal scrolling, kept in step with the loop page.
    std::function<void (int x)> onScrolledX;
    void setScrollX (int x);

private:
    void sessionChanged (Change what, Id id) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateSelection(); }
    void timerCallback() override;
    void handleAsyncUpdate() override { rebuild(); }

    void rebuild();
    void layoutStrips();
    void updateSelection();
    std::unique_ptr<ChannelStrip> makeStrip (Id trackId);

    EngineController& controller;
    Selection& selection;
    PresetManager& presets;
    struct ScrollViewport : public juce::Viewport {
        std::function<void()> onMoved;
        void visibleAreaChanged (const juce::Rectangle<int>&) override { if (onMoved) onMoved(); }
    };
    ScrollViewport viewport;
    std::vector<bool> childFlags;   // per strip: an output / sum folder of the instrument before it
    bool settingScroll = false;
    juce::Component stripHolder;
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    std::unique_ptr<ChannelStrip> masterStrip;
};

} // namespace el
