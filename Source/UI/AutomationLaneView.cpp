#include "UI/AutomationLaneView.h"
#include "UI/Theme.h"

namespace el {

juce::Colour AutomationLaneView::theme_accent() { return theme::accent; }

AutomationLaneView::AutomationLaneView (EngineController& c, AutomationOwner o, int idx)
    : controller (c), owner (o), laneIndex (idx)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
}

void AutomationLaneView::setLane (const AutomationLane& l)
{
    if (isInteracting()) return;
    // Keep the point selection across model refreshes unless the point count changed.
    if (l.points.size() != lane.points.size()) selected.clear();
    lane = l;
    invalidateCurve();
}

void AutomationLaneView::setLengthBeats (double b)
{
    b = std::max (1.0, b);
    if (std::abs (b - lengthBeats) > 1.0e-9) { lengthBeats = b; invalidateCurve(); }
}

void AutomationLaneView::setBeatsPerBar (int bpb)
{
    bpb = std::max (1, bpb);
    if (bpb != beatsPerBar) { beatsPerBar = bpb; repaint(); }
}

void AutomationLaneView::setPlayhead (double beat, bool visible)
{
    const int oldX = playheadX();
    const bool oldVisible = playVisible;
    playBeat = beat; playVisible = visible;
    const int newX = playheadX();
    if (oldVisible && (! visible || newX != oldX)) repaintPlayheadAt (oldX);
    if (visible && (! oldVisible || newX != oldX)) repaintPlayheadAt (newX);
}

int AutomationLaneView::playheadX() const noexcept
{
    return (int) beatToX (std::fmod (std::max (0.0, playBeat), std::max (1.0, lengthBeats * 1000)));
}

void AutomationLaneView::repaintPlayheadAt (int x)
{
    repaint (x - 1, 0, 3, getHeight());
}

void AutomationLaneView::resized() { pathDirty = true; }

void AutomationLaneView::rebuildPath()
{
    pathDirty = false;
    curvePath.clear(); curveFill.clear();
    if (lane.points.empty()) return;
    const int w = getWidth(), h = getHeight();
    bool first = true;
    for (int x = 0; x <= w; x += 2)
    {
        float v = 0;
        lane.evaluate (xToBeat ((float) x), v);
        const float y = valueToY (v);
        if (first) { curvePath.startNewSubPath ((float) x, y); first = false; } else curvePath.lineTo ((float) x, y);
    }
    curveFill = curvePath;
    curveFill.lineTo ((float) w, (float) h); curveFill.lineTo (0.0f, (float) h); curveFill.closeSubPath();
}

double AutomationLaneView::xToBeat (float x) const noexcept
{
    return std::clamp ((double) x / std::max (1, getWidth()) * lengthBeats, 0.0, lengthBeats);
}
float AutomationLaneView::beatToX (double b) const noexcept { return (float) (b / lengthBeats * getWidth()); }
float AutomationLaneView::yToValue (float y) const noexcept
{
    const float pad = 6.0f, h = (float) getHeight() - 2 * pad;
    return std::clamp (1.0f - (y - pad) / std::max (1.0f, h), 0.0f, 1.0f);
}
float AutomationLaneView::valueToY (float v) const noexcept
{
    const float pad = 6.0f, h = (float) getHeight() - 2 * pad;
    return pad + (1.0f - v) * h;
}

double AutomationLaneView::snap (double beat, bool fine) const noexcept
{
    const double g = fine ? 1.0 / 128.0 : 0.25;   // 1/16 note = 0.25 beat
    return std::clamp (std::round (beat / g) * g, 0.0, lengthBeats);
}

bool AutomationLaneView::isSelected (int i) const
{
    return std::find (selected.begin(), selected.end(), i) != selected.end();
}

int AutomationLaneView::hitPoint (juce::Point<float> p) const
{
    int best = -1; float bestD = 8.0f;
    for (int i = 0; i < (int) lane.points.size(); ++i)
    {
        const juce::Point<float> q (beatToX (lane.points[(size_t) i].beat), valueToY (lane.points[(size_t) i].value));
        const float d = q.getDistanceFrom (p);
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

int AutomationLaneView::segmentAt (double beat) const
{
    int idx = -1;
    for (int i = 0; i + 1 < (int) lane.points.size(); ++i)
        if (lane.points[(size_t) i].beat <= beat && beat < lane.points[(size_t) i + 1].beat) { idx = i; break; }
    return idx;
}

void AutomationLaneView::commit()
{
    controller.setAutomationLane (owner, laneIndex, lane);
}

void AutomationLaneView::paint (juce::Graphics& g)
{
    const auto w = getWidth(), h = getHeight();
    g.fillAll (theme::panel);

    // grid
    const double ppb = w / lengthBeats;
    const int nBeats = (int) std::ceil (lengthBeats);
    for (int b = 0; b <= nBeats; ++b)
    {
        const bool bar = (b % beatsPerBar) == 0;
        if (! bar && ppb < 10.0) continue;
        if (bar && ppb * beatsPerBar < 6.0 && (b / beatsPerBar) % 4 != 0) continue;
        const float x = (float) (b * ppb);
        g.setColour (bar ? theme::outline.brighter (0.2f) : theme::outline.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, 0.0f, (float) h);
    }
    g.setColour (theme::outline.withAlpha (0.6f));
    for (int q = 1; q < 4; ++q) g.drawHorizontalLine (h * q / 4, 0.0f, (float) w);

    const auto c = lane.enabled ? colour : colour.withSaturation (0.1f).withAlpha (0.6f);

    if (! lane.points.empty())
    {
        if (pathDirty) rebuildPath();
        g.setColour (c.withAlpha (0.12f));
        g.fillPath (curveFill);
        g.setColour (c);
        g.strokePath (curvePath, juce::PathStrokeType (1.6f));

        for (int i = 0; i < (int) lane.points.size(); ++i)
        {
            const auto& pt = lane.points[(size_t) i];
            const float x = beatToX (pt.beat), y = valueToY (pt.value);
            const bool sel = isSelected (i) || i == dragIndex;
            g.setColour (sel ? juce::Colours::white : c);
            g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
            g.setColour (juce::Colours::black);
            g.drawEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f, 1.0f);
        }
    }
    else
    {
        g.setColour (theme::textDim.withAlpha (0.6f));
        g.setFont (theme::font (14.0f));
        g.drawText ("Click to add points", getLocalBounds(), juce::Justification::centred);
    }

    if (mode == Mode::Band)
    {
        const auto r = juce::Rectangle<int> (bandStart, bandNow);
        g.setColour (theme::accent.withAlpha (0.15f)); g.fillRect (r);
        g.setColour (theme::accent); g.drawRect (r);
    }

    if (playVisible)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawVerticalLine (playheadX(), 0.0f, (float) h);
    }
    g.setColour (theme::outline);
    g.drawRect (getLocalBounds());
}

void AutomationLaneView::showPointMenu (int index)
{
    juce::PopupMenu curve;
    curve.addItem (1, "Linear");
    curve.addItem (2, "Ease In");
    curve.addItem (3, "Ease Out");
    juce::PopupMenu m;
    m.addItem (10, "Delete");
    m.addSubMenu ("Curve", curve);
    juce::Component::SafePointer<AutomationLaneView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, index] (int r)
    {
        if (safe == nullptr || r == 0 || index >= (int) safe->lane.points.size()) return;
        auto& pts = safe->lane.points;
        if (r == 10) pts.erase (pts.begin() + index);
        else pts[(size_t) index].curve = r == 1 ? 0.0f : r == 2 ? -0.6f : 0.6f;
        safe->selected.clear();
        safe->commit();
        safe->invalidateCurve();
    });
}

void AutomationLaneView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto pos = e.position;
    const int hit = hitPoint (pos);

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0) showPointMenu (hit);
        return;
    }

    if (hit >= 0 && ! eraser)
    {
        mode = Mode::Point; dragIndex = hit;
        if (! isSelected (hit)) selected.clear();
        invalidateCurve();
        return;
    }

    if (e.mods.isShiftDown())
    {
        mode = Mode::Band; bandStart = bandNow = e.getPosition(); selected.clear();
        repaint();
        return;
    }

    if (e.mods.isAltDown())
    {
        curveIndex = segmentAt (xToBeat (pos.x));
        if (curveIndex >= 0)
        {
            mode = Mode::Curve;
            curveStartValue = yToValue (pos.y);
            curveOrig = lane.points[(size_t) curveIndex].curve;
        }
        return;
    }

    selected.clear();
    if (eraser)
    {
        // Eraser: drag across the lane to remove every point in that span.
        mode = Mode::Erase;
        eraseFrom = std::clamp (xToBeat (pos.x), 0.0, lengthBeats);
        if (hit >= 0) lane.points.erase (lane.points.begin() + hit);
        invalidateCurve();
        return;
    }
    if (pencil)
    {
        mode = Mode::Pencil;
        lane.setPoint (snap (xToBeat (pos.x), false), yToValue (pos.y));
        invalidateCurve();
        return;
    }

    // click on empty space: add a point and drag it
    const double beat = snap (xToBeat (pos.x), e.mods.isAltDown());
    lane.setPoint (beat, yToValue (pos.y), 1.0e-3);
    for (int i = 0; i < (int) lane.points.size(); ++i)
        if (std::abs (lane.points[(size_t) i].beat - beat) <= 1.0e-3) { dragIndex = i; break; }
    mode = Mode::Point;
    invalidateCurve();
}

void AutomationLaneView::mouseDrag (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    switch (mode)
    {
        case Mode::Point:
        {
            if (dragIndex < 0 || dragIndex >= (int) lane.points.size()) return;
            auto& pts = lane.points;
            const double lo = dragIndex > 0 ? pts[(size_t) dragIndex - 1].beat + 1.0e-3 : 0.0;
            const double hi = dragIndex + 1 < (int) pts.size() ? pts[(size_t) dragIndex + 1].beat - 1.0e-3 : lengthBeats;
            pts[(size_t) dragIndex].beat = std::clamp (snap (xToBeat (pos.x), e.mods.isAltDown()), lo, std::max (lo, hi));
            pts[(size_t) dragIndex].value = yToValue (pos.y);
            invalidateCurve();
            break;
        }
        case Mode::Curve:
        {
            if (curveIndex < 0 || curveIndex >= (int) lane.points.size()) return;
            const float d = (yToValue (pos.y) - curveStartValue) * 2.0f;
            lane.points[(size_t) curveIndex].curve = std::clamp (curveOrig + d, -1.0f, 1.0f);
            invalidateCurve();
            break;
        }
        case Mode::Pencil:
            {
                const double b = std::round (xToBeat (pos.x) * 32.0) / 32.0;    // 1/32 beat resolution
                lane.setPoint (std::clamp (b, 0.0, lengthBeats), yToValue (pos.y), 1.0 / 32.0 - 1.0e-4);
            }
            invalidateCurve();
            break;
        case Mode::Band:
            bandNow = e.getPosition();
            repaint();
            break;
        case Mode::Erase:
        {
            const double b = std::clamp (xToBeat (pos.x), 0.0, lengthBeats);
            const double lo = std::min (eraseFrom, b), hi = std::max (eraseFrom, b);
            lane.points.erase (std::remove_if (lane.points.begin(), lane.points.end(),
                                               [lo, hi] (const AutoPoint& p) { return p.beat >= lo && p.beat <= hi; }),
                               lane.points.end());
            eraseFrom = b;
            invalidateCurve();
            break;
        }
        case Mode::None: break;
    }
}

void AutomationLaneView::mouseUp (const juce::MouseEvent& e)
{
    const auto m = mode;
    mode = Mode::None;
    if (m == Mode::Band)
    {
        const auto r = juce::Rectangle<int> (bandStart, bandNow).toFloat();
        selected.clear();
        for (int i = 0; i < (int) lane.points.size(); ++i)
            if (r.contains (beatToX (lane.points[(size_t) i].beat), valueToY (lane.points[(size_t) i].value)))
                selected.push_back (i);
        repaint();
    }
    else if (m == Mode::Point || m == Mode::Curve || m == Mode::Pencil || m == Mode::Erase)
    {
        commit();
        invalidateCurve();
    }
    dragIndex = -1; curveIndex = -1;
    juce::ignoreUnused (e);
}

void AutomationLaneView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int hit = hitPoint (e.position);
    if (hit < 0) return;
    lane.points.erase (lane.points.begin() + hit);
    selected.clear();
    commit();
    invalidateCurve();
}

void AutomationLaneView::deleteSelected()
{
    if (selected.empty()) return;
    std::sort (selected.begin(), selected.end());
    for (auto it = selected.rbegin(); it != selected.rend(); ++it)
        if (*it >= 0 && *it < (int) lane.points.size()) lane.points.erase (lane.points.begin() + *it);
    selected.clear();
    commit();
    invalidateCurve();
}

bool AutomationLaneView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    return false;
}

} // namespace el
