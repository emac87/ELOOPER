#pragma once
// Colour choosing helpers: a small palette popup (12 palette colours + "Custom..."), and
// ActionMenu, a tiny helper that lets popup menus be built from lambdas.
#include <JuceHeader.h>
#include "Model/Session.h"
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace el {

class ColourPicker : public juce::Component {
public:
    static constexpr int numSwatches = 12;
    ColourPicker (juce::Colour current, std::function<void (juce::Colour)> onPick, std::function<void()> onCustom);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    // Shows the palette in a call-out box pointing at `target`.
    static void show (juce::Component* target, juce::Colour current, std::function<void (juce::Colour)> onPick);
    // Shows a full colour selector in a call-out box.
    static void showCustom (juce::Component* target, juce::Colour current, std::function<void (juce::Colour)> onPick);

private:
    juce::Rectangle<int> swatchBounds (int i) const;
    juce::Rectangle<int> customBounds() const;
    int hit (juce::Point<int>) const;   // 0..11 swatch, 12 custom, -1 none

    juce::Colour currentColour;
    std::function<void (juce::Colour)> pick;
    std::function<void()> custom;
    int hover = -1;
};

// Builds popup menus out of lambdas. Item ids are indexes into `actions`; the shared vector
// keeps the lambdas alive until the (async) menu closes.
class ActionMenu {
public:
    using Fn = std::function<void()>;
    ActionMenu() : actions (std::make_shared<std::vector<Fn>>()) {}

    void add (juce::PopupMenu& m, const juce::String& text, Fn fn, bool ticked = false, bool enabled = true,
              std::optional<juce::Colour> tint = std::nullopt);

    // Adds a "Colour" submenu: palette swatches, "Custom..." and optionally "Use Track Colour"
    // (reported as nullopt). `target` anchors the custom colour call-out.
    void addColourMenu (juce::PopupMenu& parent, const juce::String& title, std::optional<juce::Colour> current,
                        bool offerDefault, juce::Component* target,
                        std::function<void (std::optional<juce::Colour>)> onPick);

    void show (juce::PopupMenu m, juce::Component* target);

private:
    std::shared_ptr<std::vector<Fn>> actions;
};

} // namespace el
