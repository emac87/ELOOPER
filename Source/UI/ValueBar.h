#pragma once
// A drag value bar (horizontal or vertical fader) and a menu slot, used by the mixer strips and by Chimera's
// editor. No engine dependencies, so the Chimera plug-in can build it on its own.
#include <JuceHeader.h>
#include <functional>

namespace el {

// Drag to change (relative: it never jumps to where you click). Shift = fine, double-click =
// default. The mouse wheel is left to the parent so scrolling never changes a level by accident.
class ValueBar : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<float()> get;
    std::function<void (float)> set;
    std::function<void()> onBegin, onEnd;
    std::function<juce::String (float)> format;
    std::function<void (juce::Component*)> onRightClick;
    juce::String label;
    float defaultValue = 0.5f;
    bool bipolar = false;
    bool vertical = false;       // fader: fills from the bottom, label on top, value below it
    juce::Colour fill;

    ValueBar();
    ~ValueBar() override;   // ends a drag gesture that's still open (the bar was rebuilt mid-drag)
    void poll();

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    bool dragging = false;
    float startValue = 0, shown = -1.0f;
};


// A slot that shows a value and opens a menu (output, input).
class MenuSlot : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<void (juce::Component*)> onClick;
    juce::String text;
    juce::Colour accent;
    MenuSlot();
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (contains (e.getPosition()) && onClick) onClick (this); }
};

} // namespace el
