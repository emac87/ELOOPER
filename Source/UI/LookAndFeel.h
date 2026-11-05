#pragma once
// Black flat look & feel plus a few small async dialog helpers (no modal loops).
#include <JuceHeader.h>
#include "UI/Theme.h"
#include <functional>

namespace el {

class ELooperLookAndFeel : public juce::LookAndFeel_V4 {
public:
    ELooperLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int bx, int by,
                       int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
};

namespace ui {
    // Async text prompt (OK / Cancel). onOk receives the entered text.
    void askText (const juce::String& title, const juce::String& message, const juce::String& initial,
                  std::function<void (juce::String)> onOk);
    // Async confirmation. onOk runs only if the user accepts.
    void confirm (const juce::String& title, const juce::String& message, const juce::String& okText,
                  std::function<void()> onOk);
    void alert (const juce::String& title, const juce::String& message);
}

} // namespace el
