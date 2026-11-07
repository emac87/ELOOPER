#include "UI/LoopGrid.h"
#include "UI/LookAndFeel.h"
#include "UI/ColourPicker.h"
#include "UI/PluginBrowser.h"

namespace el {

namespace {
    constexpr int headerW = TrackHeader::defaultWidth;
    constexpr int indent = 22;                              // multi-out outputs / sum folder
    constexpr int rowGap = 6;
    constexpr int rowStep = TrackHeader::rowHeight + rowGap;
    constexpr int cellStep = theme::loopCellSize + 6;
    constexpr int cellsX = headerW + 10;                    // loops start here on every row
    constexpr int plusW = 44;
    constexpr int pad = 12;
    constexpr int addTrackH = 40;
}

// ---- one track row --------------------------------------------------------------------------
class LoopGrid::Row : public juce::Component {
public:
    Row (LoopGrid& g, TrackPtr track, int indentPx, bool topLevel)
        : trackId (track->id), grid (g), isInstrument (track->kind == TrackKind::Instrument),
          isTopLevel (topLevel), indentX (indentPx),
          header (g.controller, g.selection, g.presets, track->id, headerW - indentPx)
    {
        addAndMakeVisible (header);
        if (topLevel) header.onReorderDrop = [this] (const juce::MouseEvent& e) { grid.reorderDrop (trackId, e); };

        for (auto& l : track->loops) {
            auto* c = cells.add (new LoopCell (g.controller, g.selection, g.midiMap, track->id, l->id));
            c->onEdit = [this] (Id t, Id l2, bool midi) { if (grid.onEditLoop) grid.onEditLoop (t, l2, midi); };
            addAndMakeVisible (c);
        }
        plusButton.setButtonText ("+");
        plusButton.setTooltip (isInstrument ? "Add a loop: MIDI or automation" : "Add a loop: audio or automation");
        plusButton.setColour (juce::TextButton::buttonColourId, theme::panel);
        plusButton.setColour (juce::TextButton::textColourOffId, theme::accent);
        plusButton.onClick = [this] { showTypeMenu(); };
        addAndMakeVisible (plusButton);
        setSize (widthFor ((int) track->loops.size()), TrackHeader::rowHeight);
    }

    static int widthFor (int numLoops) { return cellsX + numLoops * cellStep + plusW; }

    void paint (juce::Graphics& g) override
    {
        if (indentX > 0) {   // a thin guide line showing the outputs belong to the instrument above
            g.setColour (theme::outline);
            g.fillRect (indentX / 2 - 1, -rowGap, 2, getHeight() + rowGap);
            g.fillRect (indentX / 2, getHeight() / 2, indentX / 2 - 2, 2);
        }
    }

    void resized() override
    {
        header.setBounds (indentX, 0, headerW - indentX, TrackHeader::rowHeight);
        int x = cellsX;
        const int y = (TrackHeader::rowHeight - theme::loopCellSize) / 2;
        for (auto* c : cells) { c->setTopLeftPosition (x, y); x += cellStep; }
        plusButton.setBounds (x, y, plusW - 6, theme::loopCellSize);
    }

    void refreshAll()
    {
        header.refresh();
        for (auto* c : cells) c->refresh();
    }
    void poll (bool blink)
    {
        header.poll();
        for (auto* c : cells) c->poll (blink);
    }

    const Id trackId;
    TrackHeader& getHeader() { return header; }
    bool topLevel() const noexcept { return isTopLevel; }

private:
    void showTypeMenu()
    {
        auto* c = &grid.controller; auto* sel = &grid.selection;
        const Id tid = trackId;
        auto make = [c, sel, tid] (LoopType t) {
            return [c, sel, tid, t] { const Id id = c->addLoop (tid, t); if (id != kInvalidId) sel->select (tid, id); };
        };
        ActionMenu am;
        juce::PopupMenu m;
        m.addSectionHeader ("Add loop");
        if (isInstrument) am.add (m, "MIDI Loop", make (LoopType::Midi));
        else am.add (m, "Audio Loop", make (LoopType::Audio));
        am.add (m, "Automation Loop", make (LoopType::Automation));
        am.show (m, &plusButton);
    }

    LoopGrid& grid;
    const bool isInstrument, isTopLevel;
    const int indentX;
    TrackHeader header;
    juce::OwnedArray<LoopCell> cells;
    juce::TextButton plusButton;
};

// ---- grid -----------------------------------------------------------------------------------
LoopGrid::LoopGrid (EngineController& c, Selection& s, MidiMapping& mm, PresetManager& p)
    : controller (c), selection (s), midiMap (mm), presets (p)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, true);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    addTrackButton.setTooltip ("Add a track: audio, instrument or bus");
    addTrackButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    addTrackButton.setColour (juce::TextButton::textColourOffId, theme::accent);
    addTrackButton.onClick = [this] { showAddTrackMenu(); };
    content.addAndMakeVisible (addTrackButton);

    controller.addListener (this);
    selection.addChangeListener (this);
    rebuild();
    startTimerHz (theme::uiRefreshHz);
}

LoopGrid::~LoopGrid()
{
    stopTimer();
    selection.removeChangeListener (this);
    controller.removeListener (this);
    viewport.setViewedComponent (nullptr, false);
}

void LoopGrid::paint (juce::Graphics& g) { g.fillAll (theme::background); }

void LoopGrid::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutContent();
}

void LoopGrid::sessionChanged (Change what, Id id)
{
    switch (what) {
        case Change::Structure:
        case Change::Session:
            needsRebuild = true;   // deferred to the timer: never destroy a row inside its own callback
            break;
        case Change::Track:
            for (auto& r : rows)
                if (r->trackId == id) r->refreshAll();
            break;
        case Change::Loop:
            for (auto& r : rows) r->refreshAll();
            break;
        case Change::Clip: case Change::Automation: case Change::Transport: case Change::Timeline: break;
    }
}

void LoopGrid::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto& r : rows) r->getHeader().repaint();
}

void LoopGrid::timerCallback()
{
    if (needsRebuild) rebuild();
    if (! isShowing()) return;   // the timeline / bounced page is up: nothing to poll
    ++tick;
    const bool blink = ((tick / 8) & 1) == 0;
    for (auto& r : rows) r->poll (blink);
}

void LoopGrid::rebuild()
{
    needsRebuild = false;
    const auto viewPos = viewport.getViewPosition();
    for (auto& r : rows) content.removeChildComponent (r.get());
    rows.clear();

    const auto& tracks = controller.session().tracks;
    auto add = [this] (const TrackPtr& t, int ind, bool top) {
        auto row = std::make_unique<Row> (*this, t, ind, top);
        content.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    };

    for (auto& t : tracks) {
        if (t->bounced || t->kind == TrackKind::Master || t->kind == TrackKind::Bus) continue;
        if (t->kind == TrackKind::AuxOut && controller.session().findTrack (t->auxParent) != nullptr) continue;   // shown in its group
        add (t, 0, true);
        if (t->kind != TrackKind::Instrument || ! t->folderOpen) continue;
        for (auto& o : tracks)
            if (! o->bounced && o->kind == TrackKind::AuxOut && o->auxParent == t->id) add (o, indent, false);
        if (auto f = controller.folderBusOf (t->id); f && ! f->bounced) add (f, indent, false);
    }
    // Plain buses are mixer-only; one that holds loops still shows here so they stay reachable.
    for (auto& t : tracks)
        if (! t->bounced && t->kind == TrackKind::Bus && t->folderOf == kInvalidId && ! t->loops.empty()) add (t, 0, true);

    layoutContent();
    viewport.setViewPosition (viewPos);
}

void LoopGrid::layoutContent()
{
    int y = pad, maxW = headerW;
    for (auto& r : rows) {
        r->setTopLeftPosition (pad, y);
        y += rowStep;
        maxW = juce::jmax (maxW, r->getWidth());
    }
    addTrackButton.setBounds (pad, y, headerW, addTrackH);
    y += addTrackH;
    content.setSize (juce::jmax (pad + maxW + pad, viewport.getMaximumVisibleWidth()),
                     juce::jmax (y + pad, viewport.getMaximumVisibleHeight()));
}

void LoopGrid::showAddTrackMenu()
{
    auto* c = &controller;
    juce::Component::SafePointer<juce::TextButton> target (&addTrackButton);
    ActionMenu am;
    juce::PopupMenu m;
    am.add (m, "Add Audio Track", [c] { c->addTrack (TrackKind::Audio); });
    am.add (m, "Add Instrument Track", [c, target] {
        const Id id = c->addTrack (TrackKind::Instrument);
        if (id != kInvalidId) showPluginMenu (*c, id, FxChain::Instrument, 0, target.getComponent());
    });
    am.add (m, "Add Bus (shown on the mixer)", [c] { c->addTrack (TrackKind::Bus); });
    am.show (m, &addTrackButton);
}

void LoopGrid::reorderDrop (Id trackId, const juce::MouseEvent& e)
{
    if (rows.empty()) return;
    const int y = e.getEventRelativeTo (&content).y;
    const int idx = juce::jlimit (0, (int) rows.size() - 1, (y - pad + rowGap / 2) / rowStep);
    // Drop onto a group's output row = drop onto the group's instrument.
    int i = idx;
    while (i > 0 && ! rows[(size_t) i]->topLevel()) --i;
    const Id target = rows[(size_t) i]->trackId;
    if (target == trackId) return;
    const auto& tracks = controller.session().tracks;
    for (size_t k = 0; k < tracks.size(); ++k)
        if (tracks[k]->id == target) { controller.moveTrack (trackId, (int) k); return; }
}

} // namespace el
