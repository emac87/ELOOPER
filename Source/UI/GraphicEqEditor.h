#pragma once
// 10-band graphic EQ editor for one track. Compact mode = just the response curve (strip thumbnail).
#include <JuceHeader.h>
#include "Engine/EngineController.h"

namespace el {

class GraphicEqEditor : public juce::Component, private juce::Timer {
public:
    GraphicEqEditor (EngineController&, Id trackId, bool compact = false);
    ~GraphicEqEditor() override;

    void refresh();                         // re-read values from the controller
    std::function<void()> onClick;          // compact mode: thumbnail clicked

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { refresh(); }
    void rebuildCurve();
    float responseDb (float freq) const noexcept;
    void setEnabledState (bool on);
    void resetFlat();

    EngineController& controller;
    Id trackId;
    bool compact;
    juce::Slider sliders[kEqBands];
    juce::Label labels[kEqBands];
    juce::TextButton flatButton { "Flat" }, enableButton { "EQ On" };
    float values[kEqBands];
    bool eqEnabled = true;
    juce::Rectangle<float> curveArea;
    juce::Path curveLine, curveFill;
};

} // namespace el
