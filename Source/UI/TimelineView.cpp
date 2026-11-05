#include "UI/TimelineView.h"
#include "UI/Theme.h"

namespace el {

namespace {
constexpr int kBar = 28, kRuler = 22, kHeader = 120, kRow = 56;
}

TimelineView::TimelineView (EngineController& c, Selection& s) : controller (c), selection (s)
{
    setWantsKeyboardFocus (true);

    recordButton.setTooltip ("Record your loop launches from the Loops page into the timeline");
    recordButton.onClick = [this] { controller.setArrangementRecord (recordButton.getToggleState()); };
    followButton.setToggleState (true, juce::dontSendNotification);
    followButton.onClick = [this] { follow = followButton.getToggleState(); };
    loopButton.onClick = [this]
    {
        const auto& t = controller.session().transport;
        controller.setTimelineLoop (loopButton.getToggleState(), t.timelineLoopStart, t.timelineLoopEnd);
    };
    zoomOut.onClick = [this] { zoomBy (1.0 / 1.5, (float) (kHeader + (getWidth() - kHeader) / 2)); };
    zoomIn.onClick  = [this] { zoomBy (1.5, (float) (kHeader + (getWidth() - kHeader) / 2)); };
    for (auto* b : { (juce::Component*) &recordButton, (juce::Component*) &followButton, (juce::Component*) &loopButton,
                     (juce::Component*) &zoomOut, (juce::Component*) &zoomIn })
        addAndMakeVisible (b);

    controller.addListener (this);
    startTimerHz (theme::uiRefreshHz);
}

TimelineView::~TimelineView()
{
    stopTimer();
    controller.removeListener (this);
}

void TimelineView::sessionChanged (Change what, Id)
{
    if (what == Change::Timeline || what == Change::Structure || what == Change::Session
        || what == Change::Transport || what == Change::Track || what == Change::Loop)
        repaint();
}

void TimelineView::resized()
{
    auto b = getLocalBounds().removeFromTop (kBar).reduced (4, 3);
    recordButton.setBounds (b.removeFromLeft (150));
    followButton.setBounds (b.removeFromLeft (70));
    loopButton.setBounds (b.removeFromLeft (100));
    b.removeFromLeft (8);
    zoomOut.setBounds (b.removeFromLeft (28));
    b.removeFromLeft (2);
    zoomIn.setBounds (b.removeFromLeft (28));
}

// ---- geometry ----------------------------------------------------------------------------
juce::Rectangle<int> TimelineView::rulerArea() const { return { kHeader, kBar, std::max (0, getWidth() - kHeader), kRuler }; }
juce::Rectangle<int> TimelineView::rowsArea() const { return { 0, kBar + kRuler, getWidth(), std::max (0, getHeight() - kBar - kRuler) }; }
float TimelineView::beatToX (double beat) const noexcept { return (float) (kHeader + (beat - scrollBeat) * pxPerBeat); }
double TimelineView::xToBeat (float x) const noexcept { return scrollBeat + (x - kHeader) / pxPerBeat; }
int TimelineView::visibleRowCount() const { return std::max (1, rowsArea().getHeight() / kRow + 1); }

std::vector<TrackPtr> TimelineView::visibleTracks() const
{
    std::vector<TrackPtr> out;
    for (auto& t : controller.session().tracks)
        if (t != nullptr && ! t->bounced && t->kind != TrackKind::Master) out.push_back (t);
    return out;
}

int TimelineView::rowOfTrack (const std::vector<TrackPtr>& tr, Id id) const
{
    for (size_t i = 0; i < tr.size(); ++i) if (tr[i]->id == id) return (int) i;
    return -1;
}

juce::Rectangle<float> TimelineView::clipRect (const TimelineClip&, int row, double start, double len) const
{
    const float x = beatToX (start), w = (float) (len * pxPerBeat);
    const float y = (float) (rowsArea().getY() + (row - scrollRow) * kRow + 3);
    return { x, y, std::max (3.0f, w), (float) kRow - 6.0f };
}

double TimelineView::snapBeat (double beat, bool fine) const
{
    const double g = fine ? 1.0 : (double) std::max (1, controller.session().transport.beatsPerBar);
    return std::max (0.0, std::round (beat / g) * g);
}

const TimelineClip* TimelineView::findClip (Id id) const
{
    for (auto& c : controller.session().timeline) if (c.id == id) return &c;
    return nullptr;
}

double TimelineView::loopLengthOf (const TimelineClip& c) const
{
    auto loop = controller.session().findLoop (c.loopId);
    if (loop == nullptr) return 0;
    if (loop->type == LoopType::Midi)
    {
        if (auto* m = controller.midiClip (c.loopId)) return m->lengthBeats;
    }
    else if (loop->type == LoopType::Audio)
        return controller.loopStatus (c.loopId).lengthBeats;
    return controller.automationLengthBeats ({ c.trackId, c.loopId });
}

int TimelineView::hitClip (juce::Point<int> p, const std::vector<TrackPtr>& tracks, bool& edge) const
{
    edge = false;
    const auto& tl = controller.session().timeline;
    for (int i = (int) tl.size() - 1; i >= 0; --i)
    {
        const auto& c = tl[(size_t) i];
        const int row = rowOfTrack (tracks, c.trackId);
        if (row < 0) continue;
        const auto r = clipRect (c, row, c.startBeat, c.lengthBeats);
        if (r.contains (p.toFloat()))
        {
            edge = p.x >= r.getRight() - 7;
            return i;
        }
    }
    return -1;
}

// ---- painting ----------------------------------------------------------------------------
void TimelineView::drawClip (juce::Graphics& g, const TimelineClip& c, juce::Rectangle<float> r,
                             double start, double len, bool selected)
{
    auto loop = controller.session().findLoop (c.loopId);
    const auto col = loop != nullptr ? controller.loopColour (c.loopId) : theme::textDim;
    g.saveState();
    g.reduceClipRegion (r.getSmallestIntegerContainer());
    g.setColour (col.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 4.0f);

    const double ll = loopLengthOf (c);
    juce::ignoreUnused (start);
    if (loop != nullptr && loop->type == LoopType::Midi)
    {
        if (auto* clip = controller.midiClip (c.loopId))
        {
            if (! clip->notes.empty() && clip->lengthBeats > 0)
            {
                int lo = 127, hi = 0;
                for (auto& n : clip->notes) { lo = std::min (lo, (int) n.pitch); hi = std::max (hi, (int) n.pitch); }
                const float span = (float) std::max (8, hi - lo + 1);
                const float nh = std::max (1.5f, (r.getHeight() - 16.0f) / span);
                g.setColour (col.brighter (0.4f));
                for (double off = 0; off < len; off += clip->lengthBeats)
                {
                    if (beatToX (start + off + clip->lengthBeats) < 0 || beatToX (start + off) > getWidth()) continue;
                    for (auto& n : clip->notes)
                    {
                        const double s = off + n.start;
                        if (s >= len) continue;
                        const float x = r.getX() + (float) (s * pxPerBeat);
                        const float w = std::max (1.5f, (float) (std::min (n.length, len - s) * pxPerBeat));
                        const float y = r.getBottom() - 4.0f - (float) (n.pitch - lo + 1) * (r.getHeight() - 16.0f) / span;
                        g.fillRect (x, y, w, nh);
                    }
                }
            }
        }
    }
    else if (loop != nullptr && loop->type == LoopType::Audio)
    {
        g.setColour (col.withAlpha (0.55f));
        g.fillRoundedRectangle (r.withTrimmedTop (r.getHeight() * 0.35f).withTrimmedBottom (r.getHeight() * 0.15f).reduced (2, 0), 3.0f);
    }

    if (ll > 0.01 && len > ll + 0.01)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        for (double off = ll; off < len - 0.01; off += ll)
            g.drawVerticalLine ((int) (r.getX() + off * pxPerBeat), r.getY(), r.getBottom());
    }
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.setFont (theme::font (11.0f, true));
    g.drawText (loop != nullptr ? loop->name : "?", r.toNearestInt().reduced (5, 2).withHeight (14),
                juce::Justification::centredLeft, true);
    g.restoreState();

    g.setColour (selected ? juce::Colours::white : col);
    g.drawRoundedRectangle (r, 4.0f, selected ? 2.0f : 1.0f);
}

void TimelineView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    const auto& sess = controller.session();
    const auto& tp = sess.transport;
    const int bpb = std::max (1, tp.beatsPerBar);
    const auto tracks = visibleTracks();
    const auto rows = rowsArea();
    const auto ruler = rulerArea();
    const double endBeat = xToBeat ((float) getWidth());

    // ruler
    g.setColour (theme::panel);
    g.fillRect (0, kBar, getWidth(), kRuler);
    {
        g.saveState();
        g.reduceClipRegion (ruler);
        const double barPx = pxPerBeat * bpb;
        int step = 1;
        while (barPx * step < 44.0) step *= 2;
        const int firstBar = std::max (0, (int) std::floor (scrollBeat / bpb));
        g.setFont (theme::font (11.0f));
        for (int b = firstBar; b * (double) bpb <= endBeat; ++b)
        {
            const float x = beatToX (b * (double) bpb);
            const bool lab = (b % step) == 0;
            g.setColour (lab ? theme::textDim : theme::outline);
            g.drawVerticalLine ((int) x, (float) (lab ? ruler.getY() + 4 : ruler.getY() + 14), (float) ruler.getBottom());
            if (lab) { g.setColour (theme::text); g.drawText (juce::String (b + 1), (int) x + 3, ruler.getY(), 40, kRuler, juce::Justification::centredLeft); }
        }
        g.restoreState();
    }

    // loop region (live drag preview or saved)
    double la = tp.timelineLoopStart, lb = tp.timelineLoopEnd;
    bool showLoop = tp.timelineLoop;
    if (drag == Drag::LoopRegion) { la = std::min (regionA, regionB); lb = std::max (regionA, regionB); showLoop = true; }

    // rows
    {
        g.saveState();
        g.reduceClipRegion (rows);
        const int nRows = std::max ((int) tracks.size(), visibleRowCount());
        for (int row = scrollRow; row < nRows && rows.getY() + (row - scrollRow) * kRow < getHeight(); ++row)
        {
            const int y = rows.getY() + (row - scrollRow) * kRow;
            g.setColour (row % 2 ? theme::background : theme::panel.withAlpha (0.6f));
            g.fillRect (kHeader, y, getWidth() - kHeader, kRow);
            g.setColour (theme::outline);
            g.drawHorizontalLine (y + kRow - 1, 0.0f, (float) getWidth());
        }
        // grid
        const double barPx = pxPerBeat * bpb;
        for (int b = std::max (0, (int) std::floor (scrollBeat / bpb)); b * (double) bpb <= endBeat; ++b)
        {
            if (barPx < 6.0 && b % 4 != 0) continue;
            g.setColour (theme::outline.withAlpha (0.8f));
            g.drawVerticalLine ((int) beatToX (b * (double) bpb), (float) rows.getY(), (float) rows.getBottom());
            if (pxPerBeat >= 20.0)
            {
                g.setColour (theme::outline.withAlpha (0.35f));
                for (int k = 1; k < bpb; ++k)
                    g.drawVerticalLine ((int) beatToX (b * (double) bpb + k), (float) rows.getY(), (float) rows.getBottom());
            }
        }
        // clips
        for (auto& c : sess.timeline)
        {
            const int row = rowOfTrack (tracks, c.trackId);
            if (row < scrollRow) continue;
            double st = c.startBeat, ln = c.lengthBeats; Id tid = c.trackId;
            const bool dragging = (drag == Drag::Move || drag == Drag::Resize) && c.id == dragClip;
            if (dragging) { st = dragStart; ln = dragLen; tid = dragTrack; }
            const int r2 = rowOfTrack (tracks, tid);
            if (r2 < scrollRow) continue;
            const auto rect = clipRect (c, r2, st, ln);
            if (rect.getRight() < kHeader || rect.getX() > getWidth() || rect.getY() > getHeight()) continue;
            g.saveState();
            g.reduceClipRegion (juce::Rectangle<int> (kHeader, rows.getY(), getWidth() - kHeader, rows.getHeight()));
            drawClip (g, c, rect, st, ln, c.id == selectedClip);
            g.restoreState();
        }
        g.restoreState();
    }

    if (showLoop && lb > la)
    {
        const float x1 = std::max ((float) kHeader, beatToX (la)), x2 = beatToX (lb);
        if (x2 > x1)
        {
            g.setColour (theme::accent.withAlpha (0.25f));
            g.fillRect (x1, (float) ruler.getY(), x2 - x1, (float) ruler.getHeight());
            g.setColour (theme::accent.withAlpha (0.08f));
            g.fillRect (x1, (float) rows.getY(), x2 - x1, (float) rows.getHeight());
            g.setColour (theme::accent);
            g.drawVerticalLine ((int) x1, (float) ruler.getY(), (float) rows.getBottom());
            g.drawVerticalLine ((int) x2, (float) ruler.getY(), (float) rows.getBottom());
        }
    }

    // track headers (over scrolled clips)
    g.setColour (theme::panel);
    g.fillRect (0, rows.getY(), kHeader, rows.getHeight());
    g.fillRect (0, kBar, kHeader, kRuler);
    for (int row = scrollRow; row < (int) tracks.size(); ++row)
    {
        const int y = rows.getY() + (row - scrollRow) * kRow;
        if (y >= getHeight()) break;
        const auto& t = *tracks[(size_t) row];
        g.setColour (t.colour);
        g.fillRect (0, y + 2, 4, kRow - 4);
        g.setColour (t.id == selection.trackId ? theme::panelRaised : theme::panel);
        g.fillRect (4, y, kHeader - 4, kRow - 1);
        g.setColour (theme::text);
        g.setFont (theme::font (12.0f, true));
        g.drawText (t.name, 10, y, kHeader - 14, kRow, juce::Justification::centredLeft, true);
        g.setColour (theme::outline);
        g.drawHorizontalLine (y + kRow - 1, 0.0f, (float) kHeader);
    }
    g.setColour (theme::outline);
    g.drawVerticalLine (kHeader - 1, (float) kBar, (float) getHeight());

    // playhead over everything
    const float px = beatToX (playBeat);
    if (px >= kHeader && px <= getWidth())
    {
        g.setColour (playing ? theme::play : theme::text);
        g.drawVerticalLine ((int) px, (float) kBar, (float) getHeight());
        juce::Path tri;
        tri.addTriangle (px - 5, (float) kBar, px + 5, (float) kBar, px, (float) kBar + 8);
        g.fillPath (tri);
    }
}

// ---- timer / follow ----------------------------------------------------------------------
void TimelineView::timerCallback()
{
    const auto ts = controller.transportStatus();
    bool needRepaint = false;
    if (std::abs (beatToX (ts.beat) - beatToX (playBeat)) >= 1.0f || ts.playing != playing) needRepaint = true;
    playBeat = ts.beat; playing = ts.playing;

    if (follow && playing && drag == Drag::None)
    {
        const float x = beatToX (playBeat);
        if (x > getWidth() - 10 || x < kHeader)
        {
            const int bpb = std::max (1, controller.session().transport.beatsPerBar);
            scrollBeat = std::max (0.0, std::floor (playBeat / bpb) * bpb);
            needRepaint = true;
        }
    }
    const auto& t = controller.session().transport;
    if (recordButton.getToggleState() != controller.isArrangementRecording())
        recordButton.setToggleState (controller.isArrangementRecording(), juce::dontSendNotification);
    if (loopButton.getToggleState() != t.timelineLoop)
        loopButton.setToggleState (t.timelineLoop, juce::dontSendNotification);
    if (needRepaint) repaint();
}

void TimelineView::zoomBy (double factor, float anchorX)
{
    const double anchorBeat = xToBeat (anchorX);
    pxPerBeat = std::clamp (pxPerBeat * factor, 1.5, 120.0);
    scrollBeat = std::max (0.0, anchorBeat - (anchorX - kHeader) / pxPerBeat);
    repaint();
}

void TimelineView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomBy (std::pow (1.5, w.deltaY * 3.0), e.position.x);
        return;
    }
    double dx = w.deltaX;
    if (e.mods.isShiftDown() || std::abs (dx) < 1.0e-6) if (e.mods.isShiftDown()) dx = w.deltaY;
    if (std::abs (dx) > 1.0e-6)
    {
        scrollBeat = std::max (0.0, scrollBeat - dx * 600.0 / pxPerBeat);
        repaint();
    }
    else if (std::abs (w.deltaY) > 1.0e-6)
    {
        const int n = (int) visibleTracks().size();
        scrollRow = std::clamp (scrollRow + (w.deltaY < 0 ? 1 : -1), 0, std::max (0, n - 1));
        repaint();
    }
}

// ---- mouse -------------------------------------------------------------------------------
void TimelineView::showRulerMenu()
{
    juce::PopupMenu m;
    const auto& t = controller.session().transport;
    m.addItem (1, t.timelineLoop ? "Loop Region Off" : "Loop Region On");
    juce::Component::SafePointer<TimelineView> safe (this);
    m.showMenuAsync ({}, [safe] (int r)
    {
        if (safe == nullptr || r != 1) return;
        const auto& tp = safe->controller.session().transport;
        double a = tp.timelineLoopStart, b = tp.timelineLoopEnd;
        if (b <= a) { a = 0; b = 4.0 * tp.beatsPerBar; }
        safe->controller.setTimelineLoop (! tp.timelineLoop, a, b);
    });
}

void TimelineView::showClipMenu (Id clipId)
{
    juce::PopupMenu m;
    m.addItem (1, "Duplicate");
    m.addItem (2, "Delete");
    juce::Component::SafePointer<TimelineView> safe (this);
    m.showMenuAsync ({}, [safe, clipId] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto* c = safe->findClip (clipId);
        if (c == nullptr) return;
        if (r == 1)
        {
            const Id loopId = c->loopId; const double st = c->startBeat + c->lengthBeats, len = c->lengthBeats;
            const Id nid = safe->controller.addTimelineClip (loopId, st, len);
            juce::ignoreUnused (nid);
        }
        else
        {
            if (safe->selectedClip == clipId) safe->selectedClip = kInvalidId;
            safe->controller.removeTimelineClip (clipId);
        }
        safe->repaint();
    });
}

void TimelineView::showAddMenu (Id trackId, double beat)
{
    auto tr = controller.session().findTrack (trackId);
    if (tr == nullptr) return;
    juce::PopupMenu m;
    std::vector<Id> ids;
    for (auto& l : tr->loops)
    {
        ids.push_back (l->id);
        m.addItem ((int) ids.size(), l->name.isEmpty() ? juce::String ("Loop ") + juce::String ((int) ids.size()) : l->name);
    }
    if (ids.empty()) m.addItem (-1, "No loops on this track", false, false);
    juce::Component::SafePointer<TimelineView> safe (this);
    m.showMenuAsync ({}, [safe, ids, beat] (int r)
    {
        if (safe == nullptr || r <= 0 || r > (int) ids.size()) return;
        safe->controller.addTimelineClip (ids[(size_t) r - 1], beat, 0);
        safe->repaint();
    });
}

void TimelineView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.getPosition();
    const auto tracks = visibleTracks();

    if (rulerArea().contains (p))
    {
        if (e.mods.isPopupMenu()) { showRulerMenu(); return; }
        if (e.mods.isShiftDown())
        {
            drag = Drag::LoopRegion;
            regionA = regionB = snapBeat (xToBeat ((float) p.x), e.mods.isAltDown());
        }
        else
        {
            drag = Drag::Locate;
            controller.locate (std::max (0.0, xToBeat ((float) p.x)));
        }
        repaint();
        return;
    }
    if (! rowsArea().contains (p)) return;

    if (p.x < kHeader)
    {
        const int row = scrollRow + (p.y - rowsArea().getY()) / kRow;
        if (row >= 0 && row < (int) tracks.size()) { selection.select (tracks[(size_t) row]->id); repaint(); }
        return;
    }

    bool edge = false;
    const int idx = hitClip (p, tracks, edge);
    if (idx < 0) { selectedClip = kInvalidId; repaint(); return; }

    const auto c = controller.session().timeline[(size_t) idx];
    selectedClip = c.id;
    selection.select (c.trackId, c.loopId);
    if (e.mods.isPopupMenu()) { showClipMenu (c.id); repaint(); return; }

    dragClip = c.id; dragStart = c.startBeat; dragLen = c.lengthBeats; dragTrack = c.trackId;
    grabOffset = xToBeat ((float) p.x) - c.startBeat;
    drag = edge ? Drag::Resize : Drag::Move;
    repaint();
}

void TimelineView::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (drag == Drag::Locate)
    {
        controller.locate (std::max (0.0, xToBeat ((float) p.x)));
        repaint();
    }
    else if (drag == Drag::LoopRegion)
    {
        regionB = snapBeat (xToBeat ((float) p.x), e.mods.isAltDown());
        repaint();
    }
    else if (drag == Drag::Move)
    {
        const auto* c = findClip (dragClip);
        if (c == nullptr) { drag = Drag::None; return; }
        dragStart = snapBeat (xToBeat ((float) p.x) - grabOffset, e.mods.isAltDown());
        const auto tracks = visibleTracks();
        const int row = scrollRow + (p.y - rowsArea().getY()) / kRow;
        dragTrack = c->trackId;
        if (row >= 0 && row < (int) tracks.size())
        {
            auto loop = controller.session().findLoop (c->loopId);
            if (loop != nullptr && loop->trackId == tracks[(size_t) row]->id) dragTrack = tracks[(size_t) row]->id;
        }
        repaint();
    }
    else if (drag == Drag::Resize)
    {
        const double end = snapBeat (xToBeat ((float) p.x), e.mods.isAltDown());
        const double minLen = e.mods.isAltDown() ? 0.25 : (double) std::max (1, controller.session().transport.beatsPerBar);
        dragLen = std::max (minLen, end - dragStart);
        repaint();
    }
}

void TimelineView::mouseUp (const juce::MouseEvent&)
{
    const auto d = drag;
    drag = Drag::None;
    if (d == Drag::LoopRegion)
    {
        const double a = std::min (regionA, regionB), b = std::max (regionA, regionB);
        if (b - a >= 1.0) controller.setTimelineLoop (true, a, b);
    }
    else if (d == Drag::Move)
    {
        if (auto* c = findClip (dragClip))
            if (std::abs (c->startBeat - dragStart) > 1.0e-9 || c->trackId != dragTrack)
                controller.moveTimelineClip (dragClip, dragStart, dragTrack);
    }
    else if (d == Drag::Resize)
    {
        if (auto* c = findClip (dragClip))
            if (std::abs (c->lengthBeats - dragLen) > 1.0e-9)
                controller.resizeTimelineClip (dragClip, dragLen);
    }
    dragClip = kInvalidId;
    repaint();
}

void TimelineView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (! rowsArea().contains (p) || p.x < kHeader) return;
    const auto tracks = visibleTracks();
    bool edge = false;
    if (hitClip (p, tracks, edge) >= 0) return;
    const int row = scrollRow + (p.y - rowsArea().getY()) / kRow;
    if (row < 0 || row >= (int) tracks.size()) return;
    const double bar = (double) std::max (1, controller.session().transport.beatsPerBar);
    const double beat = std::max (0.0, std::floor (xToBeat ((float) p.x) / bar) * bar);
    showAddMenu (tracks[(size_t) row]->id, beat);
}

bool TimelineView::keyPressed (const juce::KeyPress& k)
{
    if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && selectedClip != kInvalidId)
    {
        const Id id = selectedClip;
        selectedClip = kInvalidId;
        controller.removeTimelineClip (id);
        repaint();
        return true;
    }
    if (k.getTextCharacter() == '+' || k.getTextCharacter() == '=') { zoomBy (1.5, (float) (kHeader + (getWidth() - kHeader) / 2)); return true; }
    if (k.getTextCharacter() == '-') { zoomBy (1.0 / 1.5, (float) (kHeader + (getWidth() - kHeader) / 2)); return true; }
    return false;
}

} // namespace el
