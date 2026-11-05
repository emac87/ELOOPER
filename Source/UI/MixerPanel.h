#pragma once
// The show/hide-able mixer: a scrolling row of channel strips plus the master strip pinned right.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "UI/Selection.h"
#include "UI/ChannelStrip.h"

namespace el {

class MixerPanel : public juce::Component,
                   private EngineController::Listener,
                   private juce::ChangeListener,
                   private juce::Timer,
                   private juce::AsyncUpdater {
public:
    MixerPanel (EngineController&, Selection&);
    ~MixerPanel() override;

    // Set by MainComponent: show the plugin-preset menu for a plugin slot anchored at a component.
    std::function<void (Id slotId, juce::Component*)> onPluginPresetMenu;

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
    juce::Viewport viewport;
    juce::Component stripHolder;
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    std::unique_ptr<ChannelStrip> masterStrip;
};

} // namespace el
