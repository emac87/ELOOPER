#include "UI/ColourPicker.h"
#include "UI/Theme.h"

namespace el {

namespace {
    constexpr int swatchSize = 36, swatchGap = 8, cols = 6, pad = 12, customH = 34;

    class CustomContent : public juce::Component, private juce::ChangeListener {
    public:
        CustomContent (juce::Colour c, std::function<void (juce::Colour)> cb)
            : selector (juce::ColourSelector::showColourspace | juce::ColourSelector::editableColour, 4, 4),
              onPick (std::move (cb))
        {
            selector.setCurrentColour (c.withAlpha (1.0f));
            selector.addChangeListener (this);
            addAndMakeVisible (selector);
            setSize (340, 380);
        }
        ~CustomContent() override { selector.removeChangeListener (this); }
        void resized() override { selector.setBounds (getLocalBounds()); }
    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            if (onPick) onPick (selector.getCurrentColour().withAlpha (1.0f));
        }
        juce::ColourSelector selector;
        std::function<void (juce::Colour)> onPick;
    };
}

ColourPicker::ColourPicker (juce::Colour c, std::function<void (juce::Colour)> p, std::function<void()> cu)
    : currentColour (c), pick (std::move (p)), custom (std::move (cu))
{
    setSize (pad * 2 + cols * swatchSize + (cols - 1) * swatchGap,
             pad * 2 + 2 * swatchSize + swatchGap + 8 + customH);
}

juce::Rectangle<int> ColourPicker::swatchBounds (int i) const
{
    return { pad + (i % cols) * (swatchSize + swatchGap), pad + (i / cols) * (swatchSize + swatchGap), swatchSize, swatchSize };
}

juce::Rectangle<int> ColourPicker::customBounds() const
{
    return { pad, getHeight() - pad - customH, getWidth() - pad * 2, customH };
}

int ColourPicker::hit (juce::Point<int> p) const
{
    for (int i = 0; i < numSwatches; ++i)
        if (swatchBounds (i).contains (p)) return i;
    return customBounds().contains (p) ? numSwatches : -1;
}

void ColourPicker::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelRaised);
    for (int i = 0; i < numSwatches; ++i) {
        auto r = swatchBounds (i).toFloat();
        const auto c = paletteColour (i);
        g.setColour (c);
        g.fillRoundedRectangle (r, 5.0f);
        if (hover == i) { g.setColour (juce::Colours::white.withAlpha (0.5f)); g.drawRoundedRectangle (r, 5.0f, 1.5f); }
        if (c == currentColour) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 2.0f); }
    }
    auto cr = customBounds().toFloat();
    g.setColour (hover == numSwatches ? theme::outline.brighter (0.3f) : theme::outline);
    g.fillRoundedRectangle (cr, 5.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (15.0f));
    g.drawText ("Custom...", customBounds(), juce::Justification::centred);
}

void ColourPicker::resized() {}

void ColourPicker::mouseMove (const juce::MouseEvent& e)
{
    const int h = hit (e.getPosition());
    if (h != hover) { hover = h; repaint(); }
}

void ColourPicker::mouseUp (const juce::MouseEvent& e)
{
    const int h = hit (e.getPosition());
    if (h < 0) return;
    if (h == numSwatches) { if (custom) custom(); }
    else if (pick) pick (paletteColour (h));
}

void ColourPicker::show (juce::Component* target, juce::Colour current, std::function<void (juce::Colour)> onPick)
{
    if (target == nullptr) return;
    juce::Component::SafePointer<juce::Component> t (target);
    auto area = target->getScreenBounds();
    auto content = std::make_unique<ColourPicker> (current, nullptr, nullptr);
    auto* raw = content.get();
    auto* callOut = &juce::CallOutBox::launchAsynchronously (std::move (content), area, nullptr);
    juce::Component::SafePointer<juce::CallOutBox> co (callOut);
    raw->pick = [co, onPick] (juce::Colour c) { if (onPick) onPick (c); if (co != nullptr) co->dismiss(); };
    raw->custom = [co, t, current, onPick] {
        if (co != nullptr) co->dismiss();
        if (t != nullptr) showCustom (t.getComponent(), current, onPick);
    };
}

void ColourPicker::showCustom (juce::Component* target, juce::Colour current, std::function<void (juce::Colour)> onPick)
{
    if (target == nullptr) return;
    juce::CallOutBox::launchAsynchronously (std::make_unique<CustomContent> (current, std::move (onPick)),
                                            target->getScreenBounds(), nullptr);
}

// ---- ActionMenu ---------------------------------------------------------------------------
void ActionMenu::add (juce::PopupMenu& m, const juce::String& text, Fn fn, bool ticked, bool enabled,
                      std::optional<juce::Colour> tint)
{
    actions->push_back (std::move (fn));
    juce::PopupMenu::Item item (text);
    item.itemID = (int) actions->size();
    item.isTicked = ticked;
    item.isEnabled = enabled;
    if (tint) item.colour = *tint;
    m.addItem (item);
}

void ActionMenu::addColourMenu (juce::PopupMenu& parent, const juce::String& title,
                                std::optional<juce::Colour> current, bool offerDefault, juce::Component* target,
                                std::function<void (std::optional<juce::Colour>)> onPick)
{
    juce::PopupMenu sub;
    juce::Component::SafePointer<juce::Component> t (target);
    if (offerDefault)
        add (sub, "Use Track Colour", [onPick] { onPick (std::nullopt); }, ! current.has_value());
    for (int i = 0; i < ColourPicker::numSwatches; ++i) {
        const auto c = paletteColour (i);
        add (sub, juce::String::fromUTF8 ("\xe2\x96\xa0 ") + "Colour " + juce::String (i + 1),
             [onPick, c] { onPick (c); }, current && *current == c, true, c);
    }
    sub.addSeparator();
    add (sub, "Custom...", [t, current, onPick] {
        if (t != nullptr)
            ColourPicker::showCustom (t.getComponent(), current.value_or (paletteColour (4)),
                                      [onPick] (juce::Colour c) { onPick (c); });
    });
    parent.addSubMenu (title, sub);
}

void ActionMenu::show (juce::PopupMenu m, juce::Component* target)
{
    auto acts = actions;
    auto opts = juce::PopupMenu::Options();
    if (target != nullptr) opts = opts.withTargetComponent (target);
    m.showMenuAsync (opts, [acts] (int r) {
        if (r > 0 && (size_t) r <= acts->size()) {
            auto fn = (*acts)[(size_t) r - 1];
            if (fn) fn();
        }
    });
}

} // namespace el
