#include "UI/PianoRoll.h"
#include "UI/Theme.h"
#include <algorithm>
#include <cmath>

namespace el {

PianoRoll::PianoRoll()
{
    setWantsKeyboardFocus (true);
}

void PianoRoll::setClip (MidiClip* c) { clip = c; clipReplaced(); }

void PianoRoll::clipReplaced()
{
    sel.assign (clip ? clip->notes.size() : 0, 0);
    mode = Mode::None;
    clampScroll();
    repaint();
}

void PianoRoll::setLoopColour (juce::Colour c) { colour = c; repaint(); }
void PianoRoll::setBeatsPerBar (int b) { beatsPerBar = juce::jmax (1, b); repaint(); }
void PianoRoll::setGrid (double beats, bool snapEnabled) { gridBeats = juce::jmax (1.0e-3, beats); snap = snapEnabled; repaint(); }

void PianoRoll::repaintPlayhead (double beat)
{
    const int x = (int) beatToX (beat);
    repaint (x - 2, rulerH, 5, getHeight() - rulerH);
}

void PianoRoll::setPlayhead (double beat, bool visible)
{
    if (visible == playVisible && (! visible || std::abs (beat - playBeat) < 1.0e-4)) return;
    if (playVisible) repaintPlayhead (playBeat);
    playBeat = beat; playVisible = visible;
    if (playVisible) repaintPlayhead (playBeat);
}

void PianoRoll::zoom (float factor)
{
    const auto g = gridArea();
    const double centreBeat = xToBeat ((float) g.getCentreX());
    ppb = juce::jlimit (8.0f, 600.0f, ppb * factor);
    scrollX = (float) (centreBeat * ppb - (g.getCentreX() - kbW));
    clampScroll();
    repaint();
}

void PianoRoll::resized()
{
    if (! centred && gridArea().getHeight() > 0)
    {
        scrollY = (float) ((127 - 72) * keyH - gridArea().getHeight() * 0.5);
        centred = true;
    }
    clampScroll();
}

void PianoRoll::clampScroll()
{
    const auto g = gridArea();
    const float maxY = juce::jmax (0.0f, 128 * keyH - (float) g.getHeight());
    const double len = clip ? clip->lengthBeats : 16.0;
    const float maxX = juce::jmax (0.0f, (float) ((len + 2.0) * ppb - g.getWidth()));
    scrollY = juce::jlimit (0.0f, maxY, scrollY);
    scrollX = juce::jlimit (0.0f, maxX, scrollX);
}

void PianoRoll::ensureSel()
{
    if (clip != nullptr && sel.size() != clip->notes.size()) sel.resize (clip->notes.size(), 0);
}

int PianoRoll::yToPitch (float y) const
{
    return juce::jlimit (0, 127, 127 - (int) std::floor ((y - rulerH + scrollY) / keyH));
}

juce::Rectangle<float> PianoRoll::noteRect (const MidiNote& n) const
{
    const float x = beatToX (n.start);
    const float w = juce::jmax (4.0f, (float) (n.length * ppb));
    return { x, pitchToY (n.pitch), w, keyH };
}

int PianoRoll::hitNote (juce::Point<float> p) const
{
    if (clip == nullptr) return -1;
    for (int i = (int) clip->notes.size(); --i >= 0;)
        if (noteRect (clip->notes[(size_t) i]).contains (p)) return i;
    return -1;
}

bool PianoRoll::onRightEdge (const juce::Rectangle<float>& r, juce::Point<float> p) const
{
    return r.getWidth() > 10.0f && p.x > r.getRight() - 6.0f;
}

void PianoRoll::selectOnly (int idx)
{
    ensureSel();
    std::fill (sel.begin(), sel.end(), (char) 0);
    if (idx >= 0 && idx < (int) sel.size()) sel[(size_t) idx] = 1;
}

int PianoRoll::numSelected() const
{
    return (int) std::count (sel.begin(), sel.end(), (char) 1);
}

double PianoRoll::snapDelta (double d) const
{
    return snap ? std::round (d / gridBeats) * gridBeats : d;
}

double PianoRoll::snapDown (double b) const
{
    return snap ? std::floor (b / gridBeats + 1.0e-9) * gridBeats : b;
}

void PianoRoll::begin()
{
    if (! edited) { edited = true; if (beginEdit) beginEdit(); }
}

void PianoRoll::commit()
{
    if (commitEdit) commitEdit();
}

//==============================================================================
void PianoRoll::paintGrid (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int pHi = yToPitch ((float) area.getY());
    const int pLo = yToPitch ((float) area.getBottom());
    for (int p = pLo; p <= pHi; ++p)
    {
        const float y = pitchToY (p);
        g.setColour (isBlackKey (p) ? juce::Colour (0xff08080a) : juce::Colour (0xff121216));
        g.fillRect ((float) area.getX(), y, (float) area.getWidth(), keyH);
        if (p % 12 == 0)
        {
            g.setColour (theme::outline);
            g.fillRect ((float) area.getX(), y + keyH - 1.0f, (float) area.getWidth(), 1.0f);
        }
    }

    const double b0 = xToBeat ((float) area.getX()), b1 = xToBeat ((float) area.getRight());
    double step = gridBeats;
    while (step * ppb < 5.0 && step < 64.0) step *= 2.0;
    const int i0 = (int) std::floor (b0 / step), i1 = (int) std::ceil (b1 / step);
    for (int i = i0; i <= i1; ++i)
    {
        const double b = i * step;
        if (b < 0) continue;
        const bool isBeat = std::abs (b - std::round (b)) < 1.0e-6;
        const bool isBar = isBeat && ((long) std::llround (b)) % beatsPerBar == 0;
        g.setColour (isBar ? juce::Colour (0xff4a4a54) : isBeat ? juce::Colour (0xff2e2e36) : juce::Colour (0xff1c1c22));
        g.fillRect (beatToX (b), (float) area.getY(), 1.0f, (float) area.getHeight());
    }

    const float xe = beatToX (clip ? clip->lengthBeats : 16.0);
    if (xe < area.getRight())
    {
        const float x0 = juce::jmax (xe, (float) area.getX());
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (x0, (float) area.getY(), (float) area.getRight() - x0, (float) area.getHeight());
    }
    g.setColour (colour.withAlpha (0.8f));
    g.fillRect (xe - 1.0f, (float) area.getY(), 2.0f, (float) area.getHeight());
}

void PianoRoll::paintNotes (juce::Graphics& g, juce::Rectangle<int> clipB)
{
    ensureSel();
    const auto cb = clipB.toFloat();
    for (size_t i = 0; i < clip->notes.size(); ++i)
    {
        const auto& n = clip->notes[i];
        auto r = noteRect (n);
        if (! r.intersects (cb)) continue;
        r = r.reduced (0.0f, 1.0f);
        const auto c = colour.withMultipliedBrightness (0.4f + 0.6f * (float) n.velocity / 127.0f);
        if (n.muted)
        {
            g.setColour (c.withAlpha (0.85f));
            g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
        }
        else
        {
            g.setColour (c);
            g.fillRoundedRectangle (r, 2.0f);
        }
        if (sel[i])
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (r, 2.0f, 1.5f);
        }
    }
}

void PianoRoll::paintKeyboard (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState ss (g);
    const auto area = gridArea().withX (0).withWidth (kbW);
    g.reduceClipRegion (area);
    g.fillAll (theme::panel);
    const int pHi = yToPitch ((float) area.getY()), pLo = yToPitch ((float) area.getBottom());
    for (int p = pLo; p <= pHi; ++p)
    {
        const float y = pitchToY (p);
        const bool black = isBlackKey (p);
        g.setColour (p == heldKey ? colour : juce::Colour (0xffd8d8dc));
        g.fillRect (0.0f, y, (float) kbW - 1.0f, keyH - 0.5f);
        if (black)
        {
            g.setColour (p == heldKey ? colour.darker (0.3f) : juce::Colour (0xff1a1a1e));
            g.fillRect (0.0f, y, kbW * 0.62f, keyH - 0.5f);
        }
        g.setColour (juce::Colour (0xff3a3a40));
        g.fillRect (0.0f, y + keyH - 1.0f, (float) kbW, 1.0f);
        if (p % 12 == 0 && keyH >= 9.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.setFont (theme::font (juce::jmin (11.0f, keyH - 2.0f)));
            g.drawText (midiNoteName (p), 0, (int) y, kbW - 4, (int) keyH, juce::Justification::centredRight);
        }
    }
}

void PianoRoll::paintRuler (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (juce::Rectangle<int> (kbW, 0, juce::jmax (0, getWidth() - kbW), rulerH));
    g.fillAll (theme::panelRaised);
    const double b0 = xToBeat ((float) kbW), b1 = xToBeat ((float) getWidth());
    g.setFont (theme::font (10.0f));
    for (int b = juce::jmax (0, (int) std::floor (b0)); b <= (int) std::ceil (b1); ++b)
    {
        const bool bar = b % beatsPerBar == 0;
        const float x = beatToX (b);
        g.setColour (bar ? theme::textDim : theme::outline);
        g.fillRect (x, bar ? 2.0f : (float) rulerH - 6.0f, 1.0f, bar ? (float) rulerH - 2.0f : 6.0f);
        if (bar && ppb * beatsPerBar >= 24.0f)
        {
            g.setColour (theme::text);
            g.drawText (juce::String (b / beatsPerBar + 1), (int) x + 3, 0, 40, rulerH - 4, juce::Justification::centredLeft);
        }
    }
}

void PianoRoll::paintVelocity (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState ss (g);
    const auto area = velArea();
    g.reduceClipRegion (area);
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
    ensureSel();
    const float usable = (float) velH - 8.0f;
    const auto cb = g.getClipBounds();
    for (size_t i = 0; i < clip->notes.size(); ++i)
    {
        const auto& n = clip->notes[i];
        const float x = beatToX (n.start);
        if (x < cb.getX() - 6 || x > cb.getRight() + 6) continue;
        const float h = usable * (float) n.velocity / 127.0f;
        auto bar = juce::Rectangle<float> (x, (float) area.getBottom() - 3.0f - h, 5.0f, h);
        g.setColour (sel[i] ? juce::Colours::white : colour.withMultipliedBrightness (0.5f + 0.5f * (float) n.velocity / 127.0f));
        g.fillRect (bar);
    }
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    if (clip == nullptr) return;
    const auto grid = gridArea();
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (grid);
        paintGrid (g, grid);
        paintNotes (g, g.getClipBounds());
        if (mode == Mode::Rubber)
        {
            g.setColour (theme::accent.withAlpha (0.18f));
            g.fillRect (rubber);
            g.setColour (theme::accent);
            g.drawRect (rubber, 1.0f);
        }
        if (playVisible)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (beatToX (playBeat), (float) grid.getY(), 1.5f, (float) grid.getHeight());
        }
    }
    paintVelocity (g);
    paintKeyboard (g);
    paintRuler (g);
}

//==============================================================================
void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    if (clip != nullptr && gridArea().contains (e.getPosition()))
    {
        const int i = hitNote (e.position);
        edge = i >= 0 && onRightEdge (noteRect (clip->notes[(size_t) i]), e.position);
    }
    setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (clip == nullptr) return;
    ensureSel();
    edited = false; dragged = false; reselectIdx = -1; anchorIdx = -1; mode = Mode::None;
    const auto pos = e.position;
    const auto grid = gridArea();

    if (pos.x < kbW)
    {
        if (grid.contains (e.getPosition())) { heldKey = yToPitch (pos.y); mode = Mode::Key; repaint (0, grid.getY(), kbW, grid.getHeight()); }
        return;
    }

    if (velArea().contains (e.getPosition()))
    {
        for (int i = (int) clip->notes.size(); --i >= 0;)
        {
            if (std::abs (beatToX (clip->notes[(size_t) i].start) + 2.5f - pos.x) <= 4.0f)
            {
                velIdx = i;
                if (! sel[(size_t) i]) selectOnly (i);
                orig = clip->notes;
                mode = Mode::Velocity;
                mouseDrag (e);
                return;
            }
        }
        return;
    }

    if (! grid.contains (e.getPosition())) return;

    const int idx = hitNote (pos);
    if (e.mods.isPopupMenu())
    {
        if (idx >= 0)
        {
            if (! sel[(size_t) idx]) selectOnly (idx);
            repaint();
            showNoteMenu();
        }
        return;
    }

    downBeat = xToBeat (pos.x);
    downPitch = yToPitch (pos.y);

    if (idx >= 0)
    {
        anchorIdx = idx;
        const bool resize = onRightEdge (noteRect (clip->notes[(size_t) idx]), pos);
        if (e.mods.isShiftDown()) sel[(size_t) idx] = sel[(size_t) idx] ? 0 : 1;
        else if (! sel[(size_t) idx]) selectOnly (idx);
        else reselectIdx = idx;

        if (! sel[(size_t) idx]) { repaint(); return; }

        if (resize) { mode = Mode::Resize; reselectIdx = -1; }
        else
        {
            mode = Mode::Move;
            if (e.mods.isAltDown())
            {
                begin();
                const size_t n = clip->notes.size();
                std::vector<MidiNote> copies;
                for (size_t i = 0; i < n; ++i) if (sel[i]) copies.push_back (clip->notes[i]);
                std::fill (sel.begin(), sel.end(), (char) 0);
                for (auto& c : copies) { clip->notes.push_back (c); sel.push_back (1); }
                anchorIdx = (int) clip->notes.size() - 1;   // not exact identity, only used for reference
                reselectIdx = -1;
            }
        }
        orig = clip->notes;
        repaint();
        return;
    }

    // empty space
    if (e.mods.isShiftDown())
    {
        mode = Mode::Rubber;
        rubberStart = pos;
        rubber = {};
        baseSel = sel;
        return;
    }

    begin();
    const double start = juce::jlimit (0.0, juce::jmax (0.0, clip->lengthBeats - 0.01), snapDown (downBeat));
    MidiNote n;
    n.start = start;
    n.length = gridBeats;
    n.pitch = (uint8_t) downPitch;
    n.velocity = (uint8_t) newVelocity;
    clip->notes.push_back (n);
    sel.assign (clip->notes.size(), 0);
    sel.back() = 1;
    anchorIdx = (int) clip->notes.size() - 1;
    orig = clip->notes;
    mode = Mode::Resize;
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (clip == nullptr) return;
    ensureSel();
    const auto pos = e.position;

    switch (mode)
    {
        case Mode::Move:
        case Mode::Resize:
        {
            if (orig.size() != clip->notes.size()) return;
            if (e.getDistanceFromDragStart() > 2) dragged = true;
            double d = snapDelta (xToBeat (pos.x) - downBeat);
            int dP = mode == Mode::Move ? yToPitch (pos.y) - downPitch : 0;
            if (mode == Mode::Move)
            {
                double minS = 1.0e9, maxS = -1.0e9; int minP = 127, maxP = 0;
                for (size_t i = 0; i < orig.size(); ++i) if (sel[i])
                {
                    minS = std::min (minS, orig[i].start); maxS = std::max (maxS, orig[i].start);
                    minP = std::min (minP, (int) orig[i].pitch); maxP = std::max (maxP, (int) orig[i].pitch);
                }
                if (minS > 1.0e8) return;
                d = juce::jlimit (-minS, std::max (-minS, clip->lengthBeats - 0.01 - maxS), d);
                dP = juce::jlimit (-minP, 127 - maxP, dP);
            }
            if (! edited && d == 0.0 && dP == 0) return;
            begin();
            for (size_t i = 0; i < orig.size(); ++i)
            {
                if (! sel[i]) continue;
                auto& n = clip->notes[i];
                if (mode == Mode::Move) { n.start = orig[i].start + d; n.pitch = (uint8_t) (orig[i].pitch + dP); }
                else n.length = std::max (0.03, orig[i].length + d);
            }
            repaint (gridArea());
            repaint (velArea());
            break;
        }
        case Mode::Rubber:
        {
            rubber = juce::Rectangle<float> (rubberStart, pos).getIntersection (gridArea().toFloat());
            for (size_t i = 0; i < clip->notes.size(); ++i)
                sel[i] = (i < baseSel.size() && baseSel[i]) || noteRect (clip->notes[i]).intersects (rubber) ? 1 : 0;
            repaint();
            break;
        }
        case Mode::Velocity:
        {
            if (velIdx < 0 || orig.size() != clip->notes.size()) return;
            const float usable = (float) velH - 8.0f;
            const float yb = (float) velArea().getBottom() - 3.0f;
            const int v = juce::jlimit (1, 127, (int) std::lround ((yb - pos.y) / usable * 127.0f));
            const int delta = v - (int) orig[(size_t) velIdx].velocity;
            begin();
            for (size_t i = 0; i < orig.size(); ++i)
                if (sel[i] || (int) i == velIdx)
                    clip->notes[i].velocity = (uint8_t) juce::jlimit (1, 127, (int) orig[i].velocity + delta);
            repaint (velArea());
            repaint (gridArea());
            break;
        }
        default: break;
    }
}

void PianoRoll::mouseUp (const juce::MouseEvent& e)
{
    const auto m = mode;
    mode = Mode::None;
    if (m == Mode::Key) { heldKey = -1; repaint(); return; }
    if (m == Mode::Move && ! dragged && reselectIdx >= 0 && ! edited) selectOnly (reselectIdx);
    if (m == Mode::Rubber) rubber = {};
    if (edited) { edited = false; commit(); }
    velIdx = -1;
    repaint();
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (clip == nullptr || ! gridArea().contains (e.getPosition())) return;
    const int idx = hitNote (e.position);
    if (idx < 0) return;
    // The first click of the double-click may have added a note on empty space only; here a note was hit.
    ensureSel();
    begin();
    clip->notes.erase (clip->notes.begin() + idx);
    sel.assign (clip->notes.size(), 0);
    edited = false;
    commit();
    repaint();
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        const double beatAt = xToBeat (e.position.x);
        ppb = juce::jlimit (8.0f, 600.0f, ppb * (1.0f + w.deltaY * 0.6f));
        scrollX = (float) (beatAt * ppb - (e.position.x - kbW));
    }
    else if (e.mods.isAltDown())
    {
        const float pitchAt = (float) (127.0 - (e.position.y - rulerH + scrollY) / keyH);
        keyH = juce::jlimit (6.0f, 40.0f, keyH * (1.0f + w.deltaY * 0.6f));
        scrollY = (127.0f - pitchAt) * keyH - (e.position.y - rulerH);
    }
    else if (e.mods.isShiftDown())
        scrollX -= (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY) * 500.0f;
    else
    {
        scrollY -= w.deltaY * 350.0f;
        scrollX -= w.deltaX * 500.0f;
    }
    clampScroll();
    repaint();
}

//==============================================================================
void PianoRoll::deleteSelected()
{
    if (clip == nullptr) return;
    ensureSel();
    if (numSelected() == 0) return;
    begin();
    std::vector<MidiNote> keep;
    for (size_t i = 0; i < clip->notes.size(); ++i) if (! sel[i]) keep.push_back (clip->notes[i]);
    clip->notes = std::move (keep);
    sel.assign (clip->notes.size(), 0);
    edited = false;
    commit();
    repaint();
}

void PianoRoll::duplicateSelectionAfter()
{
    ensureSel();
    double minS = 1.0e9, maxE = -1.0e9;
    for (size_t i = 0; i < clip->notes.size(); ++i) if (sel[i])
    {
        minS = std::min (minS, clip->notes[i].start);
        maxE = std::max (maxE, clip->notes[i].start + clip->notes[i].length);
    }
    if (minS > 1.0e8) return;
    const double offset = std::max (gridBeats, std::ceil ((maxE - minS) / gridBeats - 1.0e-9) * gridBeats);
    std::vector<MidiNote> copies;
    for (size_t i = 0; i < clip->notes.size(); ++i) if (sel[i])
    {
        auto n = clip->notes[i];
        n.start += offset;
        if (n.start < clip->lengthBeats) copies.push_back (n);
    }
    if (copies.empty()) return;
    begin();
    std::fill (sel.begin(), sel.end(), (char) 0);
    for (auto& c : copies) { clip->notes.push_back (c); sel.push_back (1); }
    edited = false;
    commit();
    repaint();
}

void PianoRoll::moveSelection (double dBeats, int dPitch)
{
    ensureSel();
    double minS = 1.0e9, maxS = -1.0e9; int minP = 127, maxP = 0;
    for (size_t i = 0; i < clip->notes.size(); ++i) if (sel[i])
    {
        minS = std::min (minS, clip->notes[i].start); maxS = std::max (maxS, clip->notes[i].start);
        minP = std::min (minP, (int) clip->notes[i].pitch); maxP = std::max (maxP, (int) clip->notes[i].pitch);
    }
    if (minS > 1.0e8) return;
    dBeats = juce::jlimit (-minS, std::max (-minS, clip->lengthBeats - 0.01 - maxS), dBeats);
    dPitch = juce::jlimit (-minP, 127 - maxP, dPitch);
    if (dBeats == 0.0 && dPitch == 0) return;
    begin();
    for (size_t i = 0; i < clip->notes.size(); ++i) if (sel[i])
    {
        clip->notes[i].start += dBeats;
        clip->notes[i].pitch = (uint8_t) (clip->notes[i].pitch + dPitch);
    }
    edited = false;
    commit();
    repaint();
}

void PianoRoll::quantizeSelection()
{
    ensureSel();
    if (numSelected() == 0) return;
    begin();
    for (size_t i = 0; i < clip->notes.size(); ++i) if (sel[i])
    {
        auto& n = clip->notes[i];
        n.start = std::round (n.start / gridBeats) * gridBeats;
        if (n.start >= clip->lengthBeats) n.start = 0;
    }
    edited = false;
    commit();
    repaint();
}

void PianoRoll::setVelocitySelected (int v)
{
    ensureSel();
    begin();
    for (size_t i = 0; i < clip->notes.size(); ++i) if (sel[i]) clip->notes[i].velocity = (uint8_t) juce::jlimit (1, 127, v);
    edited = false;
    commit();
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if (clip == nullptr) return false;
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    const bool cmd = mods.isCommandDown();

    if (cmd && (code == 'A' || code == 'a'))
    {
        ensureSel();
        std::fill (sel.begin(), sel.end(), (char) 1);
        repaint();
        return true;
    }
    if (cmd && (code == 'D' || code == 'd')) { duplicateSelectionAfter(); return true; }
    if (cmd) return false;   // leave Cmd+Z etc. to MidiEditor
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (code == 'Q' || code == 'q') { quantizeSelection(); return true; }
    const int octave = mods.isShiftDown() ? 12 : 1;
    if (code == juce::KeyPress::leftKey)  { moveSelection (-gridBeats, 0); return true; }
    if (code == juce::KeyPress::rightKey) { moveSelection (gridBeats, 0); return true; }
    if (code == juce::KeyPress::upKey)    { moveSelection (0, octave); return true; }
    if (code == juce::KeyPress::downKey)  { moveSelection (0, -octave); return true; }
    return false;
}

void PianoRoll::showNoteMenu()
{
    juce::PopupMenu vel;
    const int vals[] = { 25, 50, 75, 100, 127 };
    for (int i = 0; i < 5; ++i) vel.addItem (100 + i, juce::String (vals[i]));

    juce::PopupMenu m;
    m.addItem (1, "Delete");
    m.addItem (2, "Mute / Unmute");
    m.addSubMenu ("Velocity", vel);

    juce::Component::SafePointer<PianoRoll> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || safe->clip == nullptr || r == 0) return;
        auto& self = *safe;
        self.ensureSel();
        if (r == 1) self.deleteSelected();
        else if (r == 2)
        {
            bool anyUnmuted = false;
            for (size_t i = 0; i < self.clip->notes.size(); ++i) if (self.sel[i] && ! self.clip->notes[i].muted) anyUnmuted = true;
            self.begin();
            for (size_t i = 0; i < self.clip->notes.size(); ++i) if (self.sel[i]) self.clip->notes[i].muted = anyUnmuted;
            self.edited = false;
            self.commit();
            self.repaint();
        }
        else if (r >= 100 && r < 105)
        {
            const int vals2[] = { 25, 50, 75, 100, 127 };
            self.setVelocitySelected (vals2[r - 100]);
        }
    });
}

} // namespace el
