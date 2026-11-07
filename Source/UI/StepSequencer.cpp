#include "UI/StepSequencer.h"
#include "UI/LookAndFeel.h"
#include "UI/PianoRoll.h"
#include "UI/Theme.h"
#include <algorithm>
#include <cmath>

namespace el {

namespace {
    using SafeSeq = juce::Component::SafePointer<StepSequencer>;
    const int kVelocities[] = { 25, 50, 64, 80, 100, 110, 127 };
    const int kRatchets[] = { 1, 2, 3, 4, 5, 6, 8 };

    juce::String ratchetLabel (int r) { return r <= 1 ? juce::String ("Off") : "x" + juce::String (r); }

    // Note picker: GM drum names, then one submenu per octave. `onPick` gets the pitch.
    juce::PopupMenu notePicker (int current, const std::vector<int>& used, std::function<void (int)> onPick)
    {
        juce::PopupMenu drums, m;
        for (int p = 35; p <= 81; ++p) {
            const bool taken = p != current && std::find (used.begin(), used.end(), p) != used.end();
            drums.addItem (steppresets::gmDrumName (p) + "  (" + midiNoteName (p) + ")", ! taken, p == current,
                           [onPick, p] { onPick (p); });
        }
        m.addSubMenu ("Drums (GM)", drums);
        for (int oct = 8; oct >= 0; --oct) {
            juce::PopupMenu sub;
            bool any = false;
            for (int n = 11; n >= 0; --n) {
                const int p = oct * 12 + n;
                if (p > 127) continue;
                const bool taken = p != current && std::find (used.begin(), used.end(), p) != used.end();
                sub.addItem (midiNoteName (p), ! taken, p == current, [onPick, p] { onPick (p); });
                any = true;
            }
            if (any) m.addSubMenu ("Octave " + juce::String (oct - 2), sub, true, nullptr, current / 12 == oct);
        }
        return m;
    }
}

StepSequencer::LaneClipboard& StepSequencer::clipboard()
{
    static LaneClipboard c;
    return c;
}

StepSequencer::StepSequencer()
{
    setWantsKeyboardFocus (true);
    for (auto* b : { &lanesButton, &ratchetButton }) {
        b->setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        b->setColour (juce::TextButton::textColourOffId, theme::text);
        addAndMakeVisible (*b);
    }
    lanesButton.setTooltip ("Lane presets: drum maps, scales and your own saved presets");
    lanesButton.onClick = [this] { showLanesMenu(); };
    ratchetButton.setTooltip ("Ratchet for new steps (re-triggers inside one step)");
    ratchetButton.onClick = [this] {
        juce::PopupMenu m;
        m.addSectionHeader ("Ratchet for new steps");
        SafeSeq safe (this);
        for (int r : kRatchets)
            m.addItem (ratchetLabel (r), true, r == newRatchet, [safe, r] { if (safe) { safe->newRatchet = r; safe->updateRatchetButton(); } });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&ratchetButton));
    };
    updateRatchetButton();
}

void StepSequencer::updateRatchetButton()
{
    ratchetButton.setButtonText ("Ratchet " + ratchetLabel (newRatchet));
}

juce::String StepSequencer::rowName (int pitch)
{
    auto n = steppresets::gmDrumName (pitch);
    return n.isNotEmpty() ? n : midiNoteName (pitch);
}

juce::String StepSequencer::laneName (int pitch) const
{
    auto it = laneNames.find (pitch);
    if (it != laneNames.end() && it->second.isNotEmpty()) return it->second;
    return rowName (pitch);
}

int StepSequencer::numSteps() const
{
    if (clip == nullptr) return 0;
    return juce::jmax (1, (int) std::ceil (clip->lengthBeats / stepLen() - 1.0e-6));
}

void StepSequencer::rowsFromLanes()
{
    rows.clear();
    laneNames.clear();
    for (auto& l : clip->stepLanes) {
        if (std::find (rows.begin(), rows.end(), (int) l.pitch) != rows.end()) continue;
        rows.push_back (l.pitch);
        if (! l.name.empty()) laneNames[l.pitch] = juce::String (l.name);
    }
}

void StepSequencer::setClip (MidiClip* c, bool resetRows)
{
    clip = c;
    if (resetRows) { rows.clear(); laneNames.clear(); }
    if (clip == nullptr) { lanesButton.setButtonText ("Lanes"); repaint(); return; }

    auto recomputeMuted = [this] {
        mutedRows.clear();
        for (int p : rows) {
            bool any = false, all = true;
            for (auto& n : clip->notes) if (n.pitch == p) { any = true; all = all && n.muted; }
            if (any && all) mutedRows.push_back (p);
        }
    };

    if (customLanes()) {
        rowsFromLanes();
        for (auto& n : clip->notes) insertRow (n.pitch);
        if (resetRows) recomputeMuted();
    } else if (resetRows) {
        std::vector<int> present;
        for (auto& n : clip->notes) present.push_back (n.pitch);
        std::sort (present.begin(), present.end(), std::greater<int>());
        present.erase (std::unique (present.begin(), present.end()), present.end());
        if (present.empty()) present = { 46, 42, 39, 38, 36 };
        rows = present;
        recomputeMuted();
    } else {
        refreshRows();
    }
    lanesButton.setButtonText (clip->stepMapName.empty() ? juce::String ("Lanes") : "Lanes: " + juce::String (clip->stepMapName));
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
    if (customLanes()) { rows.push_back (pitch); return; }
    auto it = std::find_if (rows.begin(), rows.end(), [pitch] (int p) { return p < pitch; });
    rows.insert (it, pitch);
}

void StepSequencer::syncLanes()
{
    if (clip == nullptr) return;
    clip->stepLanes.clear();
    for (int p : rows) {
        StepLane l;
        l.pitch = (uint8_t) p;
        auto it = laneNames.find (p);
        if (it != laneNames.end()) l.name = it->second.toStdString();
        clip->stepLanes.push_back (l);
    }
}

void StepSequencer::structureEdited()
{
    syncLanes();
    commit();
    lanesButton.setButtonText (clip->stepMapName.empty() ? juce::String ("Lanes") : "Lanes: " + juce::String (clip->stepMapName));
    clampScroll();
    repaint();
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

void StepSequencer::resized()
{
    const int bw = (headerW - 18) / 2;
    lanesButton.setBounds (6, 4, bw + 30, rulerH - 8);
    ratchetButton.setBounds (6 + bw + 36, 4, headerW - (bw + 36) - 12, rulerH - 8);
    clampScroll();
}

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
MidiNote* StepSequencer::findStep (int pitch, int step)
{
    for (auto& n : clip->notes) if (n.pitch == pitch && stepForStart (n.start) == step) return &n;
    return nullptr;
}

const MidiNote* StepSequencer::findStep (int pitch, int step) const
{
    for (auto& n : clip->notes) if (n.pitch == pitch && stepForStart (n.start) == step) return &n;
    return nullptr;
}

int StepSequencer::stepVelocity (int pitch, int step) const
{
    auto* n = findStep (pitch, step);
    return n != nullptr ? n->velocity : 0;
}

void StepSequencer::addStep (int pitch, int step)
{
    MidiNote n;
    n.start = step * stepLen();
    n.length = stepLen() * 0.9;
    n.pitch = (uint8_t) pitch;
    n.velocity = (uint8_t) newVelocity;
    n.ratchet = (uint8_t) newRatchet;
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
    if (! edited) { edited = true; snapshot(); }
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

//==============================================================================
// Lane operations
void StepSequencer::toggleRowMute (int pitch)
{
    const bool mute = ! isRowMuted (pitch);
    snapshot();
    for (auto& n : clip->notes) if (n.pitch == pitch) n.muted = mute;
    if (mute) mutedRows.push_back (pitch);
    else mutedRows.erase (std::remove (mutedRows.begin(), mutedRows.end(), pitch), mutedRows.end());
    commit();
    repaint();
}

void StepSequencer::removeRow (int pitch)
{
    snapshot();
    clip->notes.erase (std::remove_if (clip->notes.begin(), clip->notes.end(), [pitch] (const MidiNote& n) { return n.pitch == pitch; }),
                       clip->notes.end());
    rows.erase (std::remove (rows.begin(), rows.end(), pitch), rows.end());
    mutedRows.erase (std::remove (mutedRows.begin(), mutedRows.end(), pitch), mutedRows.end());
    laneNames.erase (pitch);
    structureEdited();
}

void StepSequencer::changeLaneNote (int row, int newPitch)
{
    if (clip == nullptr || row < 0 || row >= (int) rows.size()) return;
    const int old = rows[(size_t) row];
    if (old == newPitch || std::find (rows.begin(), rows.end(), newPitch) != rows.end()) return;
    snapshot();
    for (auto& n : clip->notes) if (n.pitch == old) n.pitch = (uint8_t) newPitch;
    rows[(size_t) row] = newPitch;
    auto it = laneNames.find (old);
    if (it != laneNames.end()) {
        // A lane named after its note follows the new note; any other name is kept.
        const auto name = it->second == midiNoteName (old) ? midiNoteName (newPitch) : it->second;
        laneNames.erase (it);
        laneNames[newPitch] = name;
    }
    for (auto& m : mutedRows) if (m == old) m = newPitch;
    structureEdited();
}

void StepSequencer::renameLane (int row)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const int pitch = rows[(size_t) row];
    SafeSeq safe (this);
    ui::askText ("Rename lane", "Name for the lane on " + midiNoteName (pitch) + " (leave empty for the default name):",
                 laneName (pitch), [safe, pitch] (juce::String name) {
        if (safe == nullptr || safe->clip == nullptr) return;
        if (std::find (safe->rows.begin(), safe->rows.end(), pitch) == safe->rows.end()) return;
        safe->snapshot();
        name = name.trim();
        if (name.isEmpty() || name == rowName (pitch)) safe->laneNames.erase (pitch);
        else safe->laneNames[pitch] = name;
        safe->structureEdited();
    });
}

void StepSequencer::moveLane (int row, int delta)
{
    const int to = row + delta;
    if (row < 0 || to < 0 || row >= (int) rows.size() || to >= (int) rows.size()) return;
    snapshot();
    std::swap (rows[(size_t) row], rows[(size_t) to]);
    structureEdited();
}

void StepSequencer::setLaneVelocity (int pitch, int how, int value)
{
    std::vector<MidiNote*> lane;
    for (auto& n : clip->notes) if (n.pitch == pitch) lane.push_back (&n);
    if (lane.empty()) return;
    std::sort (lane.begin(), lane.end(), [] (auto* a, auto* b) { return a->start < b->start; });
    snapshot();
    const int stepsPerBeat = juce::jmax (1, (int) std::lround (1.0 / stepLen()));
    auto& rng = juce::Random::getSystemRandom();
    for (size_t i = 0; i < lane.size(); ++i) {
        auto& n = *lane[i];
        const double t = lane.size() > 1 ? (double) i / (double) (lane.size() - 1) : 1.0;
        int v = n.velocity;
        switch (how) {
            case 0: v = value; break;
            case 1: v = (int) std::lround (40 + 87 * t); break;
            case 2: v = (int) std::lround (127 - 87 * t); break;
            case 3: v = stepForStart (n.start) % stepsPerBeat == 0 ? 127 : 90; break;
            case 4: v = n.velocity + rng.nextInt (juce::Range<int> (-10, 11)); break;
            default: break;
        }
        n.velocity = (uint8_t) juce::jlimit (1, 127, v);
    }
    commit();
    repaint();
}

void StepSequencer::setLaneRatchet (int pitch, int ratchet)
{
    snapshot();
    for (auto& n : clip->notes) if (n.pitch == pitch) n.ratchet = (uint8_t) juce::jlimit (1, 8, ratchet);
    commit();
    repaint();
}

void StepSequencer::fillLane (int pitch, int every)
{
    snapshot();
    clip->notes.erase (std::remove_if (clip->notes.begin(), clip->notes.end(), [pitch] (const MidiNote& n) { return n.pitch == pitch; }),
                       clip->notes.end());
    for (int s = 0; s < numSteps(); s += juce::jmax (1, every)) addStep (pitch, s);
    commit();
    repaint();
}

void StepSequencer::shiftLane (int pitch, int delta)
{
    const int steps = numSteps();
    if (steps <= 0) return;
    snapshot();
    for (auto& n : clip->notes) {
        if (n.pitch != pitch) continue;
        const int s = stepForStart (n.start);
        const int to = ((s + delta) % steps + steps) % steps;
        n.start = juce::jmax (0.0, n.start + (to - s) * stepLen());
    }
    clip->sortNotes();
    commit();
    repaint();
}

void StepSequencer::clearLane (int pitch)
{
    snapshot();
    clip->notes.erase (std::remove_if (clip->notes.begin(), clip->notes.end(), [pitch] (const MidiNote& n) { return n.pitch == pitch; }),
                       clip->notes.end());
    commit();
    repaint();
}

void StepSequencer::copyLane (int pitch)
{
    auto& cb = clipboard();
    cb.notes.clear();
    for (auto& n : clip->notes) if (n.pitch == pitch) cb.notes.push_back (n);
    cb.valid = true;
}

void StepSequencer::pasteLane (int pitch)
{
    auto& cb = clipboard();
    if (! cb.valid) return;
    snapshot();
    clip->notes.erase (std::remove_if (clip->notes.begin(), clip->notes.end(), [pitch] (const MidiNote& n) { return n.pitch == pitch; }),
                       clip->notes.end());
    for (auto n : cb.notes) {
        if (n.start >= clip->lengthBeats) continue;
        n.pitch = (uint8_t) pitch;
        n.muted = isRowMuted (pitch);
        clip->notes.push_back (n);
    }
    clip->sortNotes();
    commit();
    repaint();
}

void StepSequencer::transposeAll (int semis)
{
    for (int p : rows) if (p + semis < 0 || p + semis > 127) return;
    for (auto& n : clip->notes) if (n.pitch + semis < 0 || n.pitch + semis > 127) return;
    snapshot();
    for (auto& n : clip->notes) n.pitch = (uint8_t) (n.pitch + semis);
    std::map<int, juce::String> names;
    for (auto& [p, name] : laneNames) names[p + semis] = name == midiNoteName (p) ? midiNoteName (p + semis) : name;
    laneNames = names;
    for (auto& p : rows) p += semis;
    for (auto& p : mutedRows) p += semis;
    structureEdited();
}

void StepSequencer::applyLanes (const std::vector<steppresets::Lane>& lanes, const juce::String& mapName, bool takeSnapshot)
{
    if (clip == nullptr || lanes.empty()) return;
    if (takeSnapshot) snapshot();
    rows.clear();
    laneNames.clear();
    for (auto& l : lanes) {
        if (std::find (rows.begin(), rows.end(), l.pitch) != rows.end()) continue;
        rows.push_back (l.pitch);
        if (l.name.isNotEmpty() && l.name != rowName (l.pitch)) laneNames[l.pitch] = l.name;
    }
    syncLanes();                                        // custom mode on, so extra pitches append
    for (auto& n : clip->notes) insertRow (n.pitch);    // keep lanes for notes the preset lacks
    mutedRows.erase (std::remove_if (mutedRows.begin(), mutedRows.end(),
                                     [this] (int p) { return std::find (rows.begin(), rows.end(), p) == rows.end(); }),
                     mutedRows.end());
    clip->stepMapName = mapName.toStdString();
    scrollY = 0;
    structureEdited();
}

void StepSequencer::resetLanes()
{
    snapshot();
    clip->stepLanes.clear();
    clip->stepMapName.clear();
    commit();
    setClip (clip, true);
}

void StepSequencer::clearAllSteps()
{
    if (clip == nullptr || clip->notes.empty()) return;
    snapshot();
    clip->notes.clear();
    commit();
    repaint();
}

void StepSequencer::saveUserPreset (bool withPattern)
{
    if (clip == nullptr) return;
    SafeSeq safe (this);
    const juce::String suggestion = clip->stepMapName.empty() ? juce::String ("My kit") : juce::String (clip->stepMapName);
    ui::askText (withPattern ? "Save lanes + pattern" : "Save lanes", "Preset name:", suggestion, [safe, withPattern] (juce::String name) {
        if (safe == nullptr || safe->clip == nullptr || name.trim().isEmpty()) return;
        std::vector<steppresets::Lane> lanes;
        for (int p : safe->rows) lanes.push_back ({ p, safe->laneName (p) });
        if (! steppresets::saveUserPreset (name.trim(), lanes, withPattern ? safe->clip : nullptr))
            ui::alert ("Step presets", "Couldn't save the preset to " + steppresets::userDir().getFullPathName());
    });
}

void StepSequencer::loadUserPreset (const juce::File& f, bool withPattern)
{
    auto p = steppresets::loadUserPreset (f);
    if (! p || clip == nullptr) { ui::alert ("Step presets", "Couldn't read " + f.getFileName()); return; }
    snapshot();
    if (withPattern && p->hasPattern) {
        clip->notes.clear();
        for (auto& n : p->notes) if (n.start < clip->lengthBeats) clip->notes.push_back (n);
        clip->stepBeats = p->stepBeats;
        clip->sortNotes();
        mutedRows.clear();
    }
    std::vector<steppresets::Lane> lanes = p->lanes;
    if (lanes.empty()) for (auto& n : p->notes) {
        bool seen = false;
        for (auto& l : lanes) seen = seen || l.pitch == n.pitch;
        if (! seen) lanes.push_back ({ n.pitch, {} });
    }
    applyLanes (lanes, p->name, false);
    stepLengthChanged();
}

//==============================================================================
void StepSequencer::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    if (clip == nullptr) return;

    const int steps = numSteps();
    const auto area = cellsArea();
    const int ps = playStep();
    // Only paint what JUCE asked for: playhead updates repaint a single column, so limiting
    // the loops to the clip region keeps those repaints tiny however big the pattern is.
    const auto clipR = g.getClipBounds();
    const int firstRow = juce::jmax (0, (int) std::floor ((scrollY + juce::jmax (0, clipR.getY() - rulerH)) / rowH));
    const int lastRow = juce::jmin ((int) rows.size() - 1, (int) std::floor ((scrollY + juce::jmin (area.getHeight(), clipR.getBottom() - rulerH)) / rowH));
    const int firstStep = juce::jmax (0, (int) std::floor ((scrollX + juce::jmax (0, clipR.getX() - headerW)) / cellW));
    const int lastStep = juce::jmin (steps - 1, (int) std::floor ((scrollX + juce::jmin (area.getWidth(), clipR.getRight() - headerW)) / cellW));

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (area);
        // One pass over the notes fills a [row][step] table for the visible window (instead of
        // one pass per row); scratch storage is reused between paints.
        const int nr = juce::jmax (0, lastRow - firstRow + 1), ns = juce::jmax (0, lastStep - firstStep + 1);
        const size_t cells = (size_t) nr * (size_t) ns;
        if (paintScratch.size() < cells) paintScratch.resize (cells);
        std::fill (paintScratch.begin(), paintScratch.begin() + (std::ptrdiff_t) cells, CellInfo{});
        int rowOfPitch[128];
        std::fill (std::begin (rowOfPitch), std::end (rowOfPitch), -1);
        for (int r = firstRow; r <= lastRow; ++r) rowOfPitch[rows[(size_t) r] & 127] = r - firstRow;
        if (cells > 0)
            for (auto& n : clip->notes)
            {
                const int ri = rowOfPitch[n.pitch & 127];
                if (ri < 0) continue;
                const int s = stepForStart (n.start) - firstStep;
                if (s < 0 || s >= ns) continue;
                auto& ci = paintScratch[(size_t) ri * (size_t) ns + (size_t) s];
                ci.vel = n.velocity; ci.rat = n.ratchet; ci.muted = n.muted;
            }
        for (int r = firstRow; r <= lastRow; ++r)
        {
            const int pitch = rows[(size_t) r];
            const CellInfo* rowCells = paintScratch.data() + (size_t) (r - firstRow) * (size_t) ns;
            const bool rowMute = isRowMuted (pitch);
            for (int s = firstStep; s <= lastStep; ++s)
            {
                const auto& ci = rowCells[s - firstStep];
                const auto rc = cellRect (r, s);
                const bool altGroup = (s / 4) % 2 == 1;
                if (ci.vel > 0)
                {
                    auto c = colour.withMultipliedBrightness (0.45f + 0.55f * (float) ci.vel / 127.0f);
                    if (rowMute || ci.muted) c = c.withMultipliedSaturation (0.2f).withAlpha (0.45f);
                    g.setColour (c);
                    g.fillRoundedRectangle (rc, 3.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.18f));
                    g.drawRoundedRectangle (rc.reduced (0.5f), 3.0f, 1.0f);
                    // ratchet: split the cell into its re-triggers
                    const int rr = ci.rat;
                    if (rr > 1) {
                        g.setColour (juce::Colours::black.withAlpha (0.55f));
                        for (int k = 1; k < rr; ++k) {
                            const float x = rc.getX() + rc.getWidth() * (float) k / (float) rr;
                            g.fillRect (x - 0.75f, rc.getY() + 4.0f, 1.5f, rc.getHeight() - 8.0f);
                        }
                    }
                    // velocity bar along the bottom
                    g.setColour (juce::Colours::white.withAlpha (0.55f));
                    g.fillRect (rc.getX() + 3.0f, rc.getBottom() - 4.0f, (rc.getWidth() - 6.0f) * (float) ci.vel / 127.0f, 2.0f);
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
            auto box = rc.translated (0, -rowH).withHeight (26).withWidth (juce::jmax (44.0f, rc.getWidth()));
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (theme::text);
            g.setFont (theme::font (15.0f, true));
            g.drawText (juce::String (velNow), box.toNearestInt(), juce::Justification::centred);
        }
    }

    // top-left corner behind the Lanes / Ratchet buttons
    g.setColour (theme::panel);
    g.fillRect (0, 0, headerW, rulerH);

    // ruler
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (juce::Rectangle<int> (headerW, 0, area.getWidth(), rulerH));
        g.fillAll (theme::panelRaised);
        g.setFont (theme::font (13.0f));
        for (int s = firstStep; s <= lastStep; ++s)
        {
            if (s % 4 != 0) continue;
            g.setColour (theme::textDim);
            g.drawText (juce::String (s / 4 + 1), (int) (headerW + s * cellW - scrollX) + 3, 0, 40, rulerH, juce::Justification::centredLeft);
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
                g.setFont (theme::font (15.0f, true));
                g.drawText ("+ Add lane", 6, (int) y + 2, headerW - 12, rowH - 4, juce::Justification::centred);
                break;
            }
            const int pitch = rows[(size_t) r];
            const bool muted = isRowMuted (pitch);
            g.setColour (theme::panelRaised);
            g.fillRoundedRectangle (4.0f, y + 1.0f, headerW - 8.0f, rowH - 2.0f, 4.0f);
            g.setColour (muted ? theme::textDim : theme::text);
            g.setFont (theme::font (15.0f, true));
            g.drawText (laneName (pitch), 10, (int) y, headerW - 142, rowH, juce::Justification::centredLeft, true);
            g.setColour (theme::textDim);
            g.setFont (theme::font (13.0f));
            g.drawText (midiNoteName (pitch), headerW - 128, (int) y, 48, rowH, juce::Justification::centredRight);
            // mute + remove buttons
            auto mb = juce::Rectangle<float> (headerW - 76.0f, y + 4.0f, 32.0f, rowH - 8.0f);
            g.setColour (muted ? theme::overdub : theme::outline);
            g.fillRoundedRectangle (mb, 3.0f);
            g.setColour (muted ? juce::Colours::black : theme::textDim);
            g.setFont (theme::font (14.0f, true));
            g.drawText ("M", mb.toNearestInt(), juce::Justification::centred);
            auto xb = juce::Rectangle<float> (headerW - 40.0f, y + 4.0f, 32.0f, rowH - 8.0f);
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
        if (e.mods.isPopupMenu()) { showLaneMenu (r); return; }
        if (p.x >= headerW - 76 && p.x < headerW - 42) toggleRowMute (pitch);
        else if (p.x >= headerW - 40 && p.x < headerW - 6) removeRow (pitch);
        else if (e.getNumberOfClicks() >= 2) renameLane (r);
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
            const auto rc = cellRect (velRow, velStep);
            repaint (rc.getUnion (rc.translated (0, -rowH).withHeight (26).withWidth (juce::jmax (44.0f, rc.getWidth())))
                       .getSmallestIntegerContainer().expanded (2));
        }
        return;
    }
    if (mode == Mode::Paint || mode == Mode::Erase)
    {
        int row = 0, step = 0;
        if (cellAt (e.position, row, step)) applyPaint (row, step);
    }
}

void StepSequencer::mouseUp (const juce::MouseEvent&)
{
    const auto m = mode;
    mode = Mode::None;
    if (m == Mode::Velocity && ! dragged && ! edited && velRow >= 0)
        showStepMenu (velRow, velStep);
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

//==============================================================================
// Menus
void StepSequencer::showStepMenu (int row, int step)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const int pitch = rows[(size_t) row];
    auto* n = findStep (pitch, step);
    if (n == nullptr) return;
    const int curVel = n->velocity, curRat = n->ratchet;
    SafeSeq safe (this);

    juce::PopupMenu vel, rat, m;
    for (int v : kVelocities)
        vel.addItem (juce::String (v), true, v == curVel, [safe, pitch, step, v] {
            if (safe == nullptr || safe->clip == nullptr) return;
            safe->snapshot(); safe->setStepVelocity (pitch, step, v); safe->commit(); safe->repaint();
        });
    for (int r : kRatchets)
        rat.addItem (ratchetLabel (r), true, r == curRat, [safe, pitch, step, r] {
            if (safe == nullptr || safe->clip == nullptr) return;
            safe->snapshot();
            if (auto* x = safe->findStep (pitch, step)) x->ratchet = (uint8_t) r;
            safe->commit(); safe->repaint();
        });
    m.addSectionHeader (laneName (pitch) + " - step " + juce::String (step + 1));
    m.addSubMenu ("Velocity (" + juce::String (curVel) + ")", vel);
    m.addSubMenu ("Ratchet (" + ratchetLabel (curRat) + ")", rat);
    m.addSeparator();
    m.addItem ("Remove step", [safe, pitch, step] {
        if (safe == nullptr || safe->clip == nullptr) return;
        safe->snapshot(); safe->removeStep (pitch, step); safe->commit(); safe->repaint();
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                         .withTargetScreenArea (localAreaToGlobal (cellRect (row, step).getSmallestIntegerContainer())));
}

void StepSequencer::showLaneMenu (int row)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const int pitch = rows[(size_t) row];
    SafeSeq safe (this);
    auto run = [safe] (std::function<void (StepSequencer&)> f) {
        return [safe, f] { if (safe != nullptr && safe->clip != nullptr) f (*safe); };
    };

    juce::PopupMenu m;
    m.addSectionHeader (laneName (pitch) + "  (" + midiNoteName (pitch) + ")");
    m.addSubMenu ("Change note", notePicker (pitch, rows, [safe, row] (int p) { if (safe) safe->changeLaneNote (row, p); }));
    m.addItem ("Rename lane...", run ([row] (StepSequencer& s) { s.renameLane (row); }));
    m.addSeparator();

    juce::PopupMenu vel;
    for (int v : kVelocities) vel.addItem ("All " + juce::String (v), run ([pitch, v] (StepSequencer& s) { s.setLaneVelocity (pitch, 0, v); }));
    vel.addSeparator();
    vel.addItem ("Ramp up", run ([pitch] (StepSequencer& s) { s.setLaneVelocity (pitch, 1, 0); }));
    vel.addItem ("Ramp down", run ([pitch] (StepSequencer& s) { s.setLaneVelocity (pitch, 2, 0); }));
    vel.addItem ("Accent on the beat", run ([pitch] (StepSequencer& s) { s.setLaneVelocity (pitch, 3, 0); }));
    vel.addItem ("Humanize (+/- 10)", run ([pitch] (StepSequencer& s) { s.setLaneVelocity (pitch, 4, 0); }));
    m.addSubMenu ("Velocity (all steps)", vel);

    juce::PopupMenu rat;
    for (int r : kRatchets) rat.addItem (ratchetLabel (r), run ([pitch, r] (StepSequencer& s) { s.setLaneRatchet (pitch, r); }));
    m.addSubMenu ("Ratchet (all steps)", rat);

    juce::PopupMenu fill;
    fill.addItem ("Every step", run ([pitch] (StepSequencer& s) { s.fillLane (pitch, 1); }));
    fill.addItem ("Every 2nd step", run ([pitch] (StepSequencer& s) { s.fillLane (pitch, 2); }));
    fill.addItem ("Every 4th step", run ([pitch] (StepSequencer& s) { s.fillLane (pitch, 4); }));
    fill.addItem ("Every 8th step", run ([pitch] (StepSequencer& s) { s.fillLane (pitch, 8); }));
    m.addSubMenu ("Fill", fill);

    juce::PopupMenu shift;
    shift.addItem ("Left one step", run ([pitch] (StepSequencer& s) { s.shiftLane (pitch, -1); }));
    shift.addItem ("Right one step", run ([pitch] (StepSequencer& s) { s.shiftLane (pitch, 1); }));
    m.addSubMenu ("Shift", shift);
    m.addSeparator();
    m.addItem ("Copy lane", run ([pitch] (StepSequencer& s) { s.copyLane (pitch); }));
    m.addItem ("Paste lane", clipboard().valid, false, run ([pitch] (StepSequencer& s) { s.pasteLane (pitch); }));
    m.addItem ("Clear lane", run ([pitch] (StepSequencer& s) { s.clearLane (pitch); }));
    m.addSeparator();
    m.addItem ("Move up", row > 0, false, run ([row] (StepSequencer& s) { s.moveLane (row, -1); }));
    m.addItem ("Move down", row + 1 < (int) rows.size(), false, run ([row] (StepSequencer& s) { s.moveLane (row, 1); }));
    m.addItem (isRowMuted (pitch) ? "Unmute lane" : "Mute lane", run ([pitch] (StepSequencer& s) { s.toggleRowMute (pitch); }));
    m.addItem ("Remove lane", run ([pitch] (StepSequencer& s) { s.removeRow (pitch); }));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void StepSequencer::showLanesMenu()
{
    if (clip == nullptr) return;
    SafeSeq safe (this);
    const juce::String current (clip->stepMapName);
    juce::PopupMenu m;

    m.addSectionHeader ("Drum maps");
    for (auto& group : steppresets::drumMaps()) {
        juce::PopupMenu sub;
        bool anyTicked = false;
        for (auto& p : group.presets) {
            const bool ticked = p.name == current;
            anyTicked = anyTicked || ticked;
            auto lanes = p.lanes;
            auto name = p.name;
            sub.addItem (p.name, true, ticked, [safe, lanes, name] { if (safe) safe->applyLanes (lanes, name); });
        }
        m.addSubMenu (group.name, sub, true, nullptr, anyTicked);
    }

    m.addSectionHeader ("Scales");
    juce::PopupMenu scaleMenu;
    static const char* roots[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (auto& sc : steppresets::scales()) {
        juce::PopupMenu rootMenu;
        for (int oct = 1; oct <= 4; ++oct) {
            juce::PopupMenu octMenu;
            for (int i = 0; i < 12; ++i) {
                const int root = (oct + 2) * 12 + i;   // C1 = 36, C3 = 60
                auto preset = steppresets::makeScale (sc, root);
                octMenu.addItem (juce::String (roots[i]) + juce::String (oct), true, preset.name + " " + juce::String (oct) == current,
                                 [safe, preset, oct] { if (safe) safe->applyLanes (preset.lanes, preset.name + " " + juce::String (oct)); });
            }
            rootMenu.addSubMenu ("Root in octave " + juce::String (oct), octMenu);
        }
        scaleMenu.addSubMenu (sc.name, rootMenu);
    }
    m.addSubMenu ("Scales", scaleMenu);

    m.addSectionHeader ("My presets");
    juce::PopupMenu mine;
    auto files = steppresets::userPresetFiles();
    for (auto& f : files) {
        juce::PopupMenu one;
        one.addItem ("Load lanes", [safe, f] { if (safe) safe->loadUserPreset (f, false); });
        one.addItem ("Load lanes + pattern", [safe, f] { if (safe) safe->loadUserPreset (f, true); });
        mine.addSubMenu (f.getFileNameWithoutExtension(), one);
    }
    if (files.isEmpty()) mine.addItem ("(none saved yet)", false, false, [] {});
    mine.addSeparator();
    mine.addItem ("Save lanes as preset...", [safe] { if (safe) safe->saveUserPreset (false); });
    mine.addItem ("Save lanes + pattern as preset...", [safe] { if (safe) safe->saveUserPreset (true); });
    mine.addItem ("Show presets folder", [] { steppresets::userDir().revealToUser(); });
    m.addSubMenu ("My presets", mine);

    m.addSeparator();
    juce::PopupMenu tr;
    tr.addItem ("Up an octave", [safe] { if (safe && safe->clip) safe->transposeAll (12); });
    tr.addItem ("Down an octave", [safe] { if (safe && safe->clip) safe->transposeAll (-12); });
    tr.addItem ("Up a semitone", [safe] { if (safe && safe->clip) safe->transposeAll (1); });
    tr.addItem ("Down a semitone", [safe] { if (safe && safe->clip) safe->transposeAll (-1); });
    m.addSubMenu ("Transpose all lanes", tr);
    m.addItem ("Add lane...", [safe] { if (safe) safe->showAddRowMenu(); });
    m.addItem ("Reset lanes to automatic", customLanes(), false, [safe] { if (safe && safe->clip) safe->resetLanes(); });
    m.addItem ("Clear all steps", ! clip->notes.empty(), false, [safe] { if (safe && safe->clip) safe->clearAllSteps(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lanesButton));
}

void StepSequencer::showAddRowMenu()
{
    SafeSeq safe (this);
    auto m = notePicker (-1, rows, [safe] (int p) {
        if (safe == nullptr || safe->clip == nullptr) return;
        if (std::find (safe->rows.begin(), safe->rows.end(), p) != safe->rows.end()) return;
        safe->snapshot();
        safe->insertRow (p);          // sorted by pitch unless the lanes have a custom order
        safe->structureEdited();
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

} // namespace el
