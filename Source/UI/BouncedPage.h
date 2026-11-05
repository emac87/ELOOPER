#pragma once
// "Bounced" page: tracks that were bounced away, with Restore / Delete.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include <memory>

namespace el {

class BouncedPage : public juce::Component,
                    private EngineController::Listener,
                    private juce::AsyncUpdater {
public:
    explicit BouncedPage (EngineController&);
    ~BouncedPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    struct Content : public juce::Component {};
    void sessionChanged (Change what, Id id) override;
    void handleAsyncUpdate() override;
    void rebuild();

    EngineController& controller;
    juce::Viewport viewport;
    Content content;
    juce::OwnedArray<Row> rows;
};

} // namespace el
