#include "UI/ValueBar.h"
#include "UI/Theme.h"
#include <cmath>

namespace el {

// ---- ValueBar ---------------------------------------------------------------------------------
ValueBar::ValueBar() : fill (theme::accent) {}

ValueBar::~ValueBar()
{
    // If the strip rebuilds while this bar is being dragged, close the gesture so the parameter
    // isn't left "touched" (automation would ignore it and keep recording it forever).
    if (dragging && onEnd) onEnd();
}

void ValueBar::poll()
{
    if (! get || dragging) return;
    const float v = get();
    if (std::abs (v - shown) > 1.0e-4f) { shown = v; repaint(); }
}

void ValueBar::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::background);
    g.fillRoundedRectangle (b, 4.0f);
    const float v = juce::jlimit (0.0f, 1.0f, shown);
    auto inner = b.reduced (1.5f);
    juce::Rectangle<float> f;
    if (vertical) {
        f = inner.withTop (inner.getBottom() - inner.getHeight() * v);
    } else if (bipolar) {
        const float cx = inner.getCentreX(), x = inner.getX() + inner.getWidth() * v;
        f = juce::Rectangle<float> (juce::jmin (cx, x), inner.getY(), std::abs (x - cx), inner.getHeight());
        g.setColour (theme::outline);
        g.fillRect (cx - 0.5f, inner.getY(), 1.0f, inner.getHeight());
    } else {
        f = inner.withWidth (inner.getWidth() * v);
    }
    const bool hot = isMouseOverOrDragging();
    g.setColour (fill.withAlpha (hot ? 0.55f : 0.4f));
    g.fillRoundedRectangle (f, 3.0f);
    if (vertical) {   // a bright line at the level, like a fader cap
        g.setColour (fill.brighter (0.4f));
        g.fillRect (inner.getX(), f.getY(), inner.getWidth(), 2.0f);
    }
    g.setColour (hot ? fill.withAlpha (0.9f) : theme::outline);
    g.drawRoundedRectangle (b, 4.0f, 1.0f);

    const auto valueText = format ? format (v) : juce::String (v, 2);
    if (vertical) {
        // Text sits at the bottom, inside the fill, so the level line never crosses it.
        auto t = getLocalBounds().reduced (3, 5);
        g.setColour (theme::text);
        g.setFont (theme::font (12.5f, true));
        g.drawText (valueText, t.removeFromBottom (15), juce::Justification::centred, true);
        g.setColour (theme::textDim.brighter (0.4f));
        g.setFont (theme::font (11.5f));
        g.drawText (label, t.removeFromBottom (13), juce::Justification::centred, true);
        return;
    }
    auto t = getLocalBounds().reduced (6, 0);
    g.setFont (theme::font (12.5f, true));
    g.setColour (theme::textDim.brighter (0.25f));
    g.drawText (label, t, juce::Justification::centredLeft, true);
    g.setColour (theme::text);
    g.setFont (theme::font (12.5f));
    g.drawText (valueText, t, juce::Justification::centredRight, true);
}

void ValueBar::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (this); return; }
    if (! get) return;
    dragging = true;
    startValue = shown = get();
    if (onBegin) onBegin();
}

void ValueBar::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float len = (float) juce::jmax (80, vertical ? getHeight() : getWidth());
    const float span = len * (e.mods.isShiftDown() ? 6.0f : (vertical ? 1.0f : 1.5f));
    const float d = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / span;
    const float v = juce::jlimit (0.0f, 1.0f, startValue + d);
    if (std::abs (v - shown) > 1.0e-5f) { shown = v; if (set) set (v); repaint(); }
}

void ValueBar::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    if (onEnd) onEnd();
    repaint();
}

void ValueBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! set) return;
    if (onBegin) onBegin();
    shown = defaultValue; set (defaultValue);
    if (onEnd) onEnd();
    repaint();
}


MenuSlot::MenuSlot() : accent (theme::accent) {}

void MenuSlot::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool hot = isMouseOver();
    g.setColour (hot ? theme::outline : theme::panelRaised);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (hot ? accent : theme::outline);
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
    auto r = getLocalBounds().reduced (6, 0);
    g.setColour (theme::textDim);
    juce::Path tri;   // small "menu" arrow
    const float cx = (float) r.getRight() - 4.0f, cy = (float) getHeight() * 0.5f;
    tri.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.fillPath (tri);
    r.removeFromRight (12);
    g.setColour (theme::text);
    g.setFont (theme::font (12.5f));
    g.drawText (text, r, juce::Justification::centredLeft, true);
}

} // namespace el
