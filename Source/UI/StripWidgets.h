#pragma once
// Small controls shared by the mixer strips (and anything else that shows a track's chain):
// a drag value bar (horizontal or vertical), a plug-in chip, an empty "add" slot, and the
// track's plug-in icon button. All of them are cheap to poll at the UI refresh rate.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
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
    // Binds get/set/gesture to a track parameter.
    void bind (EngineController&, Id trackId, ParamTarget);
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

// One plug-in: click = show / hide its window; the dot = bypass (effects); right-click = onMenu.
class FxChip : public juce::Component, public juce::SettableTooltipClient {
public:
    FxChip (EngineController&, Id trackId, FxChain, int index, PluginSlotPtr);

    std::function<void (FxChip&)> onMenu;

    void poll();
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    const PluginSlotPtr& getSlot() const noexcept { return slot; }
    FxChain getChain() const noexcept { return chain; }
    int getIndex() const noexcept { return index; }

private:
    bool hasLed() const { return chain != FxChain::Instrument; }
    juce::Rectangle<int> ledBounds() const { return { 7, (getHeight() - 8) / 2, 8, 8 }; }
    EngineController& controller;
    const Id trackId;
    FxChain chain;
    int index;
    PluginSlotPtr slot;
    bool bypassed = false, open = false;
};

// An empty slot (Logic style): a dim box that shows its hint and opens a menu when clicked.
class EmptySlot : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit EmptySlot (juce::String hintText) : hint (std::move (hintText)) {}
    std::function<void (juce::Component*)> onClick;
    juce::String hint;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (contains (e.getPosition()) && onClick) onClick (this); }
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

// The track's plug-in icon: click shows / hides its main plug-in, right-click lists all of them.
class PluginIconButton : public juce::Component, public juce::SettableTooltipClient {
public:
    PluginIconButton (EngineController&, Id trackId);
    std::function<void()> onPressed;
    bool interactive = true;
    void refresh();
    void poll();
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = interactive; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool anyEditorOpen() const;
    void showMenu();
    EngineController& controller;
    const Id trackId;
    bool hover = false, lit = false;
};

namespace stripfmt {
    juce::String signedDb (float db);
    juce::String panText (float normalised);
}

} // namespace el
