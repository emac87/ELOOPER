#include "UI/StepSequencer.h"
#include "UI/PianoRoll.h"
#include "UI/Theme.h"
#include <algorithm>
#include <cmath>

namespace el {

namespace {
struct DrumName { int pitch; const char* name; };
const DrumName kDrums[] = {
    { 35, "Kick 2" }, { 36, "Kick" }, { 37, "Rim" }, { 38, "Snare" }, { 39, "Clap" }, { 40, "Snare 2" },
    { 41, "Low Floor Tom" }, { 42, "Closed HH" }, { 43, "High Floor Tom" }, { 44, "Pedal HH" },
    { 45, "Low Tom" }, { 46, "Open HH" }, { 47, "Mid Tom" }, { 48, "Hi-Mid Tom" }, { 49, "Crash" },
    { 50, "High Tom" }, { 51, "Ride" }, { 52, "China" }, { 53, "Ride Bell" }, { 54, "Tambourine" },
    { 55, "Splash" }, { 56, "Cowbell" }, { 57, "Crash 2" }, { 59, "Ride 2" }
};
const char* drumName (int p)
{
    for (auto& d : kDrums) if (d.pitch == p) return d.name;
    return nullptr;
}
}

StepSequencer::StepSequencer()
{
    setWantsKeyboardFocus (true);
}

juce::String StepSequencer::rowName (int pitch)
{
    if (auto* n = drumName (pitch)) return n;
    return midiNoteName (pitch);
}

int StepSequencer::numSteps() const
{
    if (clip == nullptr) return 0;
    return juce::jmax (1, (int) std::ceil (clip->lengthBeats / stepLen() - 1.0e-6));
}

void StepSequencer::setClip (MidiClip* c, bool resetRows)
{
    clip = c;
    if (resetRows) rows.clear();
    if (clip == nullptr) { repaint(); return; }

    if (resetRows)
    {
        mutedRows.clear();
        std::vector<int> present;
        for (auto& n : clip->notes) present.push_back (n.pitch);
        std::sort (present.begin(), present.end(), std::greater<int>());
        present.erase (std::unique (present.begin(), present.end()), present.end());
        if (present.empty()) present = { 46, 42, 39, 38, 36 };
        rows = present;
        for (int p : rows)
        {
            bool any = false, all = true;
            for (auto& n : clip->notes) if (n.pitch == p) { any = true; all = all && n.muted; }
            if (any && all) mutedRows.push_back (p);
        }
    }
    else refreshRows();
    clampScroll();
    repaint();
}

void StepSequencer::refreshRows()
{
    if (clip == nullptr) return;
    for (auto& n : clip->notes) insertRow (n.pitch);
    repaint();
}

void StepSequencer::stepLengthChanged() { clampScroll(); repaint(); }
void StepSequencer::setLoopColour (juce::Colour c) { colour = c; repaint(); }

void StepSequencer::insertRow (int pitch)
{
    if (std::find (rows.begin(), rows.end(), pitch) != rows.end()) return;
    auto it = std::find_if (rows.begin(), rows.end(), [pitch] (int p) { return p < pitch; });
    rows.insert (it, pitch);
}

bool StepSequencer::isRowMuted (int pitch) const
{
    return std::find (mutedRows.begin(), mutedRows.end(), pitch) != mutedRows.end();
}

int StepSequencer::playStep() const
{
    return playVisible ? (int) std::floor (playBeat / stepLen() + 1.0e-9) : -1;
}

void StepSequencer::repaintColumn (int step)
{
    if (step < 0) return;
    const int x = headerW + (int) (step * cellW - scrollX);
    repaint (x - 1, 0, (int) cellW + 3, getHeight());
}

void StepSequencer::setPlayhead (double beat, bool visible)
{
    const int oldStep = playStep();
    playBeat = beat; playVisible = visible;
    const int newStep = playStep();
    if (oldStep != newStep) { repaintColumn (oldStep); repaintColumn (newStep); }
}

void StepSequencer::zoom (float factor)
{
    cellW = juce::jlimit (10.0f, 80.0f, cellW * factor);
    clampScroll();
    repaint();
}

void StepSequencer::resized() { clampScroll(); }

void StepSequencer::clampScroll()
{
    const auto a = cellsArea();
    scrollX = juce::jlimit (0.0f, juce::jmax (0.0f, numSteps() * cellW - (float) a.getWidth()), scrollX);
    scrollY = juce::jlimit (0.0f, juce::jmax (0.0f, (float) (rows.size() + 1) * rowH - (float) a.getHeight()), scrollY);
}

juce::Rectangle<float> StepSequencer::cellRect (int row, int step) const
{
    return { headerW + step * cellW - scrollX + 1.0f, rulerH + row * (float) rowH - scrollY + 1.0f, cellW - 2.0f, (float) rowH - 2.0f };
}

int StepSequencer::rowAtY (float y) const
{
    if (y < rulerH) return -1;
    return (int) std::floor ((y - rulerH + scrollY) / rowH);
}

bool StepSequencer::cellAt (juce::Point<float> p, int& row, int& step) const
{
    if (clip == nullptr || p.x < headerW || p.y < rulerH) return false;
    row = rowAtY (p.y);
    step = (int) std::floor ((p.x - headerW + scrollX) / cellW);
    return row >= 0 && row < (int) rows.size() && step >= 0 && step < numSteps();
}

//==============================================================================
bool StepSequencer::hasStep (int pitch, int step) const
{
    for (auto& n : clip->notes) if (n.pitch == pitch && stepForStart (n.start) == step) return true;
    return false;
}

int StepSequencer::stepVelocity (int pitch, int step) const
{
    for (auto& n : clip->notes) if (n.pitch == pitch && stepForStart (n.start) == step) return n.velocity;
    return 0;
}

void StepSequencer::addStep (int pitch, int step)
{
    MidiNote n;
    n.start = step * stepLen();
    n.length = stepLen() * 0.9;
    n.pitch = (uint8_t) pitch;
    n.velocity = (uint8_t) newVelocity;
    n.muted = isRowMuted (pitch);
    clip->notes.push_back (n);
}

void StepSequencer::removeStep (int pitch, int step)
{
    clip->notes.erase (std::remove_if (clip->notes.begin(), clip->notes.end(),
                                       [&] (const MidiNote& n) { return n.pitch == pitch && stepForStart (n.start) == step; }),
                       clip->notes.end());
}

void StepSequencer::setStepVelocity (int pitch, int step, int v)
{
    for (auto& n : clip->notes)
        if (n.pitch == pitch && stepForStart (n.start) == step) n.velocity = (uint8_t) juce::jlimit (1, 127, v);
}

void StepSequencer::begin()
{
    if (! edited) { edited = true; if (beginEdit) beginEdit(); }
}

void StepSequencer::commit()
{
    if (commitEdit) commitEdit();
}

void StepSequencer::applyPaint (int row, int step)
{
    const int pitch = rows[(size_t) row];
    const bool has = hasStep (pitch, step);
    if (mode == Mode::Paint && ! has) { begin(); addStep (pitch, step); repaint (cellRect (row, step).getSmallestIntegerContainer()); }
    else if (mode == Mode::Erase && has) { begin(); removeStep (pitch, step); repaint (cellRect (row, step).getSmallestIntegerContainer()); }
}

void StepSequencer::toggleRowMute (int pitch)
{
    const bool mute = ! isRowMuted (pitch);
    if (beginEdit) beginEdit();
    for (auto& n : clip->notes) if (n.pitch == pitch) n.muted = mute;
    if (mute) mutedRows.push_back (pitch);
    else mutedRows.erase (std::remove (mutedRows.begin(), mutedRows.end(), pitch), mutedRows.end());
    commit();
    repaint();
}

void StepSequencer::removeRow (int pitch)
{
    if (beginEdit) beginEdit();
    clip->notes.erase (std::remove_if (clip->notes.begin(), clip->notes.end(), [pitch] (const MidiNote& n) { return n.pitch == pitch; }),
                       clip->notes.end());
    rows.erase (std::remove (rows.begin(), rows.end(), pitch), rows.end());
    mutedRows.erase (std::remove (mutedRows.begin(), mutedRows.end(), pitch), mutedRows.end());
    commit();
    clampScroll();
    repaint();
}

//==============================================================================
void StepSequencer::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    if (clip == nullptr) return;

    const int steps = numSteps();
    const auto area = cellsArea();
    const int ps = playStep();
    const int firstRow = juce::jmax (0, (int) std::floor (scrollY / rowH));
    const int lastRow = juce::jmin ((int) rows.size() - 1, (int) std::floor ((scrollY + area.getHeight()) / rowH));
    const int firstStep = juce::jmax (0, (int) std::floor (scrollX / cellW));
    const int lastStep = juce::jmin (steps - 1, (int) std::floor ((scrollX + area.getWidth()) / cellW));

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (area);
        std::vector<int> vel ((size_t) steps);
        std::vector<char> mutedCell ((size_t) steps);
        for (int r = firstRow; r <= lastRow; ++r)
        {
            const int pitch = rows[(size_t) r];
            std::fill (vel.begin(), vel.end(), 0);
            std::fill (mutedCell.begin(), mutedCell.end(), (char) 0);
            for (auto& n : clip->notes)
            {
                if (n.pitch != pitch) continue;
                const int s = stepForStart (n.start);
                if (s >= 0 && s < steps) { vel[(size_t) s] = n.velocity; mutedCell[(size_t) s] = n.muted ? 1 : 0; }
            }
            const bool rowMute = isRowMuted (pitch);
            for (int s = firstStep; s <= lastStep; ++s)
            {
                const auto rc = cellRect (r, s);
                const bool altGroup = (s / 4) % 2 == 1;
                if (vel[(size_t) s] > 0)
                {
                    auto c = colour.withMultipliedBrightness (0.45f + 0.55f * (float) vel[(size_t) s] / 127.0f);
                    if (rowMute || mutedCell[(size_t) s]) c = c.withMultipliedSaturation (0.2f).withAlpha (0.45f);
                    g.setColour (c);
                    g.fillRoundedRectangle (rc, 3.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.18f));
                    g.drawRoundedRectangle (rc.reduced (0.5f), 3.0f, 1.0f);
                }
                else
                {
                    g.setColour (altGroup ? juce::Colour (0xff1c1c22) : juce::Colour (0xff131317));
                    g.fillRoundedRectangle (rc, 3.0f);
                }
            }
        }
        // beat-group separators
        for (int s = firstStep; s <= lastStep; ++s)
        {
            if (s % 4 != 0) continue;
            g.setColour (theme::outline);
            g.fillRect (headerW + s * cellW - scrollX - 0.5f, (float) rulerH, 1.0f, (float) area.getHeight());
        }
        // loop end
        g.setColour (colour.withAlpha (0.7f));
        g.fillRect (headerW + steps * cellW - scrollX - 1.0f, (float) rulerH, 2.0f, (float) area.getHeight());
        if (ps >= 0 && ps < steps)
        {
            const float x = headerW + ps * cellW - scrollX;
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.fillRect (x, (float) rulerH, cellW, (float) area.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.fillRect (x, (float) rulerH, cellW, 2.0f);
        }
        // velocity drag read-out
        if (mode == Mode::Velocity && velRow >= 0 && velRow < (int) rows.size())
        {
            auto rc = cellRect (velRow, velStep);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillRoundedRectangle (rc.translated (0, -rowH).withHeight (20).withWidth (juce::jmax (34.0f, rc.getWidth())), 3.0f);
            g.setColour (theme::text);
            g.setFont (theme::font (12.0f, true));
            g.drawText (juce::String (velNow), rc.translated (0, -rowH).withHeight (20).withWidth (juce::jmax (34.0f, rc.getWidth())).toNearestInt(),
                        juce::Justification::centred);
        }
    }

    // ruler
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (juce::Rectangle<int> (headerW, 0, area.getWidth(), rulerH));
        g.fillAll (theme::panelRaised);
        g.setFont (theme::font (10.0f));
        for (int s = firstStep; s <= lastStep; ++s)
        {
            if (s % 4 != 0) continue;
            g.setColour (theme::textDim);
            g.drawText (juce::String (s / 4 + 1), (int) (headerW + s * cellW - scrollX) + 3, 0, 30, rulerH, juce::Justification::centredLeft);
        }
    }

    // row headers
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (juce::Rectangle<int> (0, rulerH, headerW, area.getHeight()));
        g.fillAll (theme::panel);
        for (int r = firstRow; r <= (int) rows.size(); ++r)
        {
            const float y = rulerH + r * (float) rowH - scrollY;
            if (y > getHeight()) break;
            if (r == (int) rows.size())
            {
                g.setColour (theme::panelRaised);
                g.fillRoundedRectangle (6.0f, y + 2.0f, headerW - 12.0f, rowH - 4.0f, 4.0f);
                g.setColour (theme::accent);
                g.setFont (theme::font (12.0f, true));
                g.drawText ("+ Add row", 6, (int) y + 2, headerW - 12, rowH - 4, juce::Justification::centred);
                break;
            }
            const int pitch = rows[(size_t) r];
            const bool muted = isRowMuted (pitch);
            g.setColour (theme::panelRaised);
            g.fillRoundedRectangle (4.0f, y + 1.0f, headerW - 8.0f, rowH - 2.0f, 4.0f);
            g.setColour (muted ? theme::textDim : theme::text);
            g.setFont (theme::font (12.0f, true));
            g.drawText (rowName (pitch), 10, (int) y, headerW - 100, rowH, juce::Justification::centredLeft, true);
            g.setColour (theme::textDim);
            g.setFont (theme::font (10.0f));
            g.drawText (midiNoteName (pitch), headerW - 88, (int) y, 34, rowH, juce::Justification::centredRight);
            // mute + remove buttons
            auto mb = juce::Rectangle<float> (headerW - 50.0f, y + 5.0f, 20.0f, rowH - 10.0f);
            g.setColour (muted ? theme::overdub : theme::outline);
            g.fillRoundedRectangle (mb, 3.0f);
            g.setColour (muted ? juce::Colours::black : theme::textDim);
            g.setFont (theme::font (10.0f, true));
            g.drawText ("M", mb.toNearestInt(), juce::Justification::centred);
            auto xb = juce::Rectangle<float> (headerW - 26.0f, y + 5.0f, 20.0f, rowH - 10.0f);
            g.setColour (theme::outline);
            g.fillRoundedRectangle (xb, 3.0f);
            g.setColour (theme::textDim);
            g.drawText ("x", xb.toNearestInt(), juce::Justification::centred);
        }
    }
    g.setColour (theme::outline);
    g.fillRect (headerW - 1, 0, 1, getHeight());
}

//==============================================================================
void StepSequencer::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (clip == nullptr) return;
    edited = false; dragged = false; mode = Mode::None; velRow = -1;
    const auto p = e.position;

    if (p.x < headerW)
    {
        const int r = rowAtY (p.y);
        if (r == (int) rows.size()) { showAddRowMenu(); return; }
        if (r < 0 || r >= (int) rows.size()) return;
        const int pitch = rows[(size_t) r];
        if (p.x >= headerW - 50 && p.x < headerW - 28) toggleRowMute (pitch);
        else if (p.x >= headerW - 26 && p.x < headerW - 4) removeRow (pitch);
        return;
    }

    int row = 0, step = 0;
    if (! cellAt (p, row, step)) return;
    const int pitch = rows[(size_t) row];

    if (e.mods.isPopupMenu())
    {
        if (hasStep (pitch, step))
        {
            mode = Mode::Velocity;
            velRow = row; velStep = step;
            velStart = velNow = stepVelocity (pitch, step);
        }
        return;
    }

    mode = hasStep (pitch, step) ? Mode::Erase : Mode::Paint;
    applyPaint (row, step);
}

void StepSequencer::mouseDrag (const juce::MouseEvent& e)
{
    if (clip == nullptr) return;
    if (e.getDistanceFromDragStart() > 2) dragged = true;
    if (mode == Mode::Velocity)
    {
        if (velRow < 0 || velRow >= (int) rows.size()) return;
        const int v = juce::jlimit (1, 127, velStart - (int) std::lround (e.getDistanceFromDragStartY()));
        if (v != velNow || ! edited)
        {
            if (v == velStart && ! edited) return;
            begin();
            velNow = v;
            setStepVelocity (rows[(size_t) velRow], velStep, v);
            repaint();
        }
        return;
    }
    if (mode == Mode::Paint || mode == Mode::Erase)
    {
        int row = 0, step = 0;
        if (cellAt (e.position, row, step)) applyPaint (row, step);
    }
}

void StepSequencer::mouseUp (const juce::MouseEvent& e)
{
    const auto m = mode;
    mode = Mode::None;
    if (m == Mode::Velocity && ! dragged && ! edited && velRow >= 0)
        showVelocityMenu (velRow, velStep);
    if (edited) { edited = false; commit(); }
    velRow = -1;
    repaint();
}

void StepSequencer::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
        zoom (1.0f + w.deltaY * 0.5f);
    else if (e.mods.isShiftDown())
    {
        scrollX -= (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY) * 500.0f;
        clampScroll(); repaint();
    }
    else
    {
        scrollY -= w.deltaY * 300.0f;
        scrollX -= w.deltaX * 500.0f;
        clampScroll(); repaint();
    }
}

void StepSequencer::showVelocityMenu (int row, int step)
{
    juce::PopupMenu m;
    const int vals[] = { 25, 50, 75, 100, 127 };
    m.addSectionHeader ("Velocity");
    for (int i = 0; i < 5; ++i) m.addItem (1 + i, juce::String (vals[i]));
    const int pitch = rows[(size_t) row];
    juce::Component::SafePointer<StepSequencer> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, pitch, step] (int r)
    {
        if (safe == nullptr || safe->clip == nullptr || r < 1 || r > 5) return;
        const int vals2[] = { 25, 50, 75, 100, 127 };
        if (safe->beginEdit) safe->beginEdit();
        safe->setStepVelocity (pitch, step, vals2[r - 1]);
        safe->commit();
        safe->repaint();
    });
}

void StepSequencer::showAddRowMenu()
{
    juce::PopupMenu drums, notes;
    for (auto& d : kDrums)
        drums.addItem (d.pitch + 1, juce::String (d.name) + "  (" + midiNoteName (d.pitch) + ")", std::find (rows.begin(), rows.end(), d.pitch) == rows.end());
    for (int oct = 6; oct >= 0; --oct)
    {
        juce::PopupMenu sub;
        for (int n = 11; n >= 0; --n)
        {
            const int p = oct * 12 + n;
            if (p > 127) continue;
            sub.addItem (p + 1, midiNoteName (p), std::find (rows.begin(), rows.end(), p) == rows.end());
        }
        notes.addSubMenu ("Octave " + juce::String (oct - 2), sub);
    }
    juce::PopupMenu m;
    m.addSubMenu ("Drums", drums);
    m.addSubMenu ("Notes", notes);
    juce::Component::SafePointer<StepSequencer> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r < 1) return;
        safe->insertRow (r - 1);
        safe->clampScroll();
        safe->repaint();
    });
}

} // namespace el
