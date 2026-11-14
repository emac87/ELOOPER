#include "UI/LookAndFeel.h"

namespace el {

ELooperLookAndFeel::ELooperLookAndFeel()
{
    using namespace theme;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::TextButton::buttonColourId, panelRaised);
    setColour (juce::TextButton::buttonOnColourId, accent.darker (0.4f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, textDim);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, panel);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, text);
    setColour (juce::ComboBox::backgroundColourId, panelRaised);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.darker (0.5f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, outline);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, outline);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, panel);
    setColour (juce::Slider::textBoxOutlineColourId, outline);
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff3a3a42));
    setColour (juce::AlertWindow::backgroundColourId, panelRaised);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, outline);
    setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::outlineColourId, outline);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, outline);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::ColourSelector::backgroundColourId, panelRaised);
    setColour (juce::ColourSelector::labelTextColourId, text);
    setColour (juce::BubbleComponent::backgroundColourId, panelRaised);
    setColour (juce::BubbleComponent::outlineColourId, outline);
}

void ELooperLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                                               bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = bg;
    if (b.getToggleState()) c = b.findColour (juce::TextButton::buttonOnColourId);
    if (! b.isEnabled()) c = c.withMultipliedAlpha (0.5f);
    else if (down) c = c.brighter (0.25f);
    else if (over) c = c.brighter (0.12f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId).brighter (0.3f) : theme::outline);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

juce::Font ELooperLookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    return theme::font (juce::jlimit (13.0f, 15.0f, (float) h * 0.45f), b.getToggleState());
}

void ELooperLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const auto tick = b.findColour (juce::ToggleButton::tickColourId);
    g.setColour (on ? tick.withAlpha (0.28f) : theme::panelRaised.brighter (over ? 0.1f : 0.0f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? tick : theme::outline);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (b.isEnabled() ? (on ? juce::Colours::white : theme::textDim.brighter (0.4f)) : theme::textDim);
    g.setFont (theme::font (juce::jlimit (13.0f, 15.0f, r.getHeight() * 0.45f), true));
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (2, 0), juce::Justification::centred, true);
}

void ELooperLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float a0, float a1, juce::Slider& s)
{
    auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto centre = area.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    const float thick = juce::jmax (2.0f, radius * 0.16f);
    const float arcR = radius - thick * 0.5f;

    juce::Path bg, val;
    bg.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, a0, a1, true);
    val.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, a0, angle, true);
    g.setColour (theme::outline);
    g.strokePath (bg, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (s.isEnabled() ? s.findColour (juce::Slider::rotarySliderFillColourId) : theme::textDim);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float ir = radius - thick - 2.0f;
    g.setColour (theme::panelRaised);
    g.fillEllipse (centre.x - ir, centre.y - ir, ir * 2, ir * 2);
    g.setColour (theme::text);
    juce::Path tick;
    tick.addLineSegment ({ 0.0f, -ir * 0.35f, 0.0f, -ir * 0.95f }, 1.5f);
    g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void ELooperLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float, float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    const bool vertical = style == juce::Slider::LinearVertical;
    const bool horizontal = style == juce::Slider::LinearHorizontal;
    if (! vertical && ! horizontal) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }

    const auto fill = s.findColour (juce::Slider::trackColourId);
    if (vertical) {
        const float cx = (float) x + (float) w * 0.5f;
        juce::Rectangle<float> track (cx - 2.0f, (float) y + 4.0f, 4.0f, (float) h - 8.0f);
        g.setColour (theme::outline);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (fill.withAlpha (0.7f));
        g.fillRoundedRectangle (track.withTop (pos), 2.0f);
        juce::Rectangle<float> thumb (cx - 11.0f, pos - 5.0f, 22.0f, 10.0f);
        g.setColour (theme::panelRaised.brighter (0.25f));
        g.fillRoundedRectangle (thumb, 3.0f);
        g.setColour (theme::text);
        g.fillRect (thumb.getX() + 3.0f, thumb.getCentreY() - 0.5f, thumb.getWidth() - 6.0f, 1.0f);
    } else {
        const float cy = (float) y + (float) h * 0.5f;
        juce::Rectangle<float> track ((float) x + 4.0f, cy - 2.0f, (float) w - 8.0f, 4.0f);
        g.setColour (theme::outline);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (fill.withAlpha (0.7f));
        g.fillRoundedRectangle (track.withRight (pos), 2.0f);
        g.setColour (theme::text);
        g.fillEllipse (pos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
    }
}

void ELooperLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawRect (0, 0, width, height, 1);
}

void ELooperLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                       juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) ? theme::accent : theme::outline);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) width - 12.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font ELooperLookAndFeel::getComboBoxFont (juce::ComboBox&) { return theme::font (15.0f); }
juce::Font ELooperLookAndFeel::getPopupMenuFont() { return theme::font (16.0f); }
juce::Font ELooperLookAndFeel::getLabelFont (juce::Label& l) { return l.getFont().withHeight (juce::jmax (14.0f, l.getFont().getHeight())); }
juce::Font ELooperLookAndFeel::getSliderPopupFont (juce::Slider&) { return theme::font (15.0f, true); }
juce::Font ELooperLookAndFeel::getTabButtonFont (juce::TabBarButton&, float h) { return theme::font (juce::jlimit (14.0f, 16.0f, h * 0.55f), true); }
juce::Font ELooperLookAndFeel::getAlertWindowTitleFont() { return theme::font (20.0f, true); }
juce::Font ELooperLookAndFeel::getAlertWindowMessageFont() { return theme::font (16.0f); }
juce::Font ELooperLookAndFeel::getAlertWindowFont() { return theme::font (15.0f); }

juce::Label* ELooperLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (theme::font (14.0f));
    return l;
}

void ELooperLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                                    int& idealWidth, int& idealHeight)
{
    LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardMenuItemHeight, idealWidth, idealHeight);
    if (! isSeparator) idealHeight = juce::jmax (idealHeight, 28);
}

void ELooperLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& sb, int x, int y, int w, int h,
                                        bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    juce::ignoreUnused (sb);
    auto thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize)
                          : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (juce::Colour (0xff3a3a42).brighter (down ? 0.4f : over ? 0.2f : 0.0f));
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}

namespace ui {

namespace {
    juce::Component::SafePointer<juce::Component> alertHost;

    // Shows w modally (it deletes itself). On iOS it goes inside the registered host view.
    void present (juce::AlertWindow* w, std::function<void (int)> done)
    {
       #if JUCE_IOS
        if (auto* h = alertHost.getComponent())
        {
            w->removeFromDesktop();
            h->addAndMakeVisible (w);
            w->setCentrePosition (h->getLocalBounds().getCentre());
        }
       #endif
        w->enterModalState (true, juce::ModalCallbackFunction::create ([done] (int r) { if (done) done (r); }), true);
    }
}

void setAlertHost (juce::Component* host) { alertHost = host; }

juce::Component* hostFor (juce::Component* c)
{
   #if JUCE_IOS
    return c != nullptr ? c->getTopLevelComponent() : nullptr;
   #else
    juce::ignoreUnused (c);
    return nullptr;
   #endif
}

void askText (const juce::String& title, const juce::String& message, const juce::String& initial,
              std::function<void (juce::String)> onOk)
{
    auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("text", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    present (w, [w, onOk] (int r) {
        if (r == 1 && onOk) onOk (w->getTextEditorContents ("text"));
    });
}

void confirm (const juce::String& title, const juce::String& message, const juce::String& okText,
              std::function<void()> onOk)
{
   #if JUCE_IOS
    auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon);
    w->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    present (w, [onOk] (int r) { if (r == 1 && onOk) onOk(); });
   #else
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle (title).withMessage (message)
                                      .withButton (okText).withButton ("Cancel"),
                                  [onOk] (int r) { if (r == 1 && onOk) onOk(); });
   #endif
}

void alert (const juce::String& title, const juce::String& message)
{
   #if JUCE_IOS
    auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::WarningIcon);
    w->addButton ("OK", 0, juce::KeyPress (juce::KeyPress::returnKey));
    present (w, nullptr);
   #else
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title).withMessage (message).withButton ("OK"),
                                  nullptr);
   #endif
}

} // namespace ui
} // namespace el
