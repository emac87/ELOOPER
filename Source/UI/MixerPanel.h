#pragma once
// The show/hide-able mixer: a scrolling row of vertical channel strips (each track, with a
// multi-out instrument's outputs and sum folder beside it, then the buses) plus master pinned right.
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
    juce::Viewport viewport;
    juce::Component stripHolder;
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    std::unique_ptr<ChannelStrip> masterStrip;
};

} // namespace el
