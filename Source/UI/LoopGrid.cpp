#include "UI/LoopGrid.h"
#include "UI/LookAndFeel.h"
#include "UI/ColourPicker.h"
#include "UI/PluginBrowser.h"
#include "Engine/MidiMapping.h"

namespace el {

namespace {
    constexpr int headerW = TrackHeader::defaultWidth;
    constexpr int indent = 16;                              // multi-out outputs / sum folder
    constexpr int rowGap = 6;
    constexpr int rowStep = TrackHeader::rowHeight + rowGap;
    constexpr int cellStep = theme::loopCellSize + 6;
    constexpr int cellsX = headerW + 10;                    // loops start here on every row
    constexpr int plusW = 44;
    constexpr int pad = 12;
    constexpr int addTrackH = 40;
    constexpr int sceneH = 30;                              // scene bar above the rows
}

// ---- scene bar: Stop all + one launch button above each loop column -----------------------------
class LoopGrid::SceneBar : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit SceneBar (LoopGrid& g) : grid (g) {}
    void setCount (int n) { if (n != count) { count = n; repaint(); } }
    int getCount() const noexcept { return count; }
    void setLit (int scene) { if (scene != lit) { lit = scene; repaint(); } }

    juce::Rectangle<int> stopRect() const { return { 0, 0, headerW, sceneH }; }
    juce::Rectangle<int> sceneRect (int i) const { return { cellsX + i * cellStep, 0, theme::loopCellSize, sceneH }; }
    int sceneAt (juce::Point<int> p) const
    {
        for (int i = 0; i < count; ++i) if (sceneRect (i).contains (p)) return i;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        auto drawBtn = [&] (juce::Rectangle<int> r, const juce::String& text, bool on, bool hot, juce::Colour accent, bool stopShape) {
            auto rf = r.toFloat().reduced (0.5f);
            g.setColour (on ? accent.withAlpha (0.35f) : hot ? theme::panelRaised : theme::panel);
            g.fillRoundedRectangle (rf, 5.0f);
            g.setColour (on ? accent : hot ? accent.withAlpha (0.7f) : theme::outline);
            g.drawRoundedRectangle (rf, 5.0f, on ? 1.5f : 1.0f);
            g.setFont (theme::font (13.0f, true));
            const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text);
            const float x0 = rf.getCentreX() - (float) (tw + 16) * 0.5f, cy = rf.getCentreY();
            g.setColour (on ? juce::Colours::white : accent);
            if (stopShape) g.fillRect (x0, cy - 4.5f, 9.0f, 9.0f);
            else { juce::Path p; p.addTriangle (x0, cy - 5.5f, x0, cy + 5.5f, x0 + 9.5f, cy); g.fillPath (p); }
            g.setColour (on ? juce::Colours::white : theme::text);
            g.drawText (text, juce::Rectangle<float> (x0 + 16.0f, rf.getY(), (float) tw + 4.0f, rf.getHeight()), juce::Justification::centredLeft);
        };
        drawBtn (stopRect(), "Stop all", false, hover == -2, theme::record, true);
        for (int i = 0; i < count; ++i)
            drawBtn (sceneRect (i), "Scene " + juce::String (i + 1), lit == i, hover == i, theme::play, false);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = stopRect().contains (e.getPosition()) ? -2 : sceneAt (e.getPosition());
        if (h != hover) { hover = h; repaint(); }
        setTooltip (h == -2 ? "Stop every loop" : h >= 0 ? "Scene " + juce::String (h + 1) + ": play loop " + juce::String (h + 1)
                    + " of every track and stop all the others (right-click: MIDI learn)" : juce::String());
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        if (stopRect().contains (p)) {
            if (e.mods.isPopupMenu()) { learnMenu (MappingTarget::Kind::StopAll, 0); return; }
            grid.controller.stopAllLoops();
            return;
        }
        const int i = sceneAt (p);
        if (i < 0) return;
        if (e.mods.isPopupMenu()) { learnMenu (MappingTarget::Kind::Scene, i); return; }
        grid.controller.launchScene (i);
        setLit (i);
    }

private:
    void learnMenu (MappingTarget::Kind k, int scene)
    {
        auto* mm = &grid.midiMap;
        ActionMenu am;
        juce::PopupMenu m;
        am.add (m, "MIDI Learn", [mm, k, scene] {
            MappingTarget t; t.kind = k; t.loopId = (Id) scene;
            mm->startLearn (t);
        });
        am.show (m, this);
    }

    LoopGrid& grid;
    int count = 0, lit = -1, hover = -1;
};

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
            if (! EngineController::isSceneLoop (*l)) continue;   // automation loops are hidden for now
            auto* c = cells.add (new LoopCell (g.controller, g.selection, g.midiMap, track->id, l->id));
            c->onEdit = [this] (Id t, Id l2, bool midi) { if (grid.onEditLoop) grid.onEditLoop (t, l2, midi); };
            addAndMakeVisible (c);
        }
        plusButton.setButtonText ("+");
        plusButton.setTooltip (isInstrument ? "Add a MIDI loop" : "Add an audio loop");
        plusButton.setColour (juce::TextButton::buttonColourId, theme::panel);
        plusButton.setColour (juce::TextButton::textColourOffId, theme::accent);
        plusButton.onClick = [this] { addLoop(); };
        addAndMakeVisible (plusButton);
        setSize (widthFor (cells.size()), TrackHeader::rowHeight);
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

    int numCells() const { return cells.size(); }

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
    void addLoop()
    {
        const Id id = grid.controller.addLoop (trackId, isInstrument ? LoopType::Midi : LoopType::Audio);
        if (id != kInvalidId) grid.selection.select (trackId, id);
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
    sceneBar = std::make_unique<SceneBar> (*this);
    content.addAndMakeVisible (*sceneBar);

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
    if ((tick & 3) == 0) updateSceneLight();
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

void LoopGrid::updateSceneLight()
{
    // A scene is lit when every non-empty loop in its column is playing (or about to) and no
    // loop outside it is.
    const int n = sceneBar->getCount();
    int lit = -1;
    for (int i = 0; i < n && lit < 0; ++i) {
        bool any = false, ok = true;
        for (auto& t : controller.session().tracks) {
            if (t->bounced) continue;
            const auto loops = controller.sceneLoops (*t);
            for (int k = 0; k < (int) loops.size() && ok; ++k) {
                const auto st = controller.loopStatus (loops[(size_t) k]->id);
                const bool on = (st.state == LoopState::Playing || st.state == LoopState::Overdubbing) && st.pending != PendingAction::Stop;
                const bool starting = st.state == LoopState::Stopped && st.pending == PendingAction::Play;
                if (k == i) { if (on || starting) any = true; else if (st.state == LoopState::Stopped) ok = false; }
                else if (on || starting) ok = false;
            }
        }
        if (any && ok) lit = i;
    }
    sceneBar->setLit (lit);
}

void LoopGrid::layoutContent()
{
    int maxCells = 0;
    for (auto& r : rows) maxCells = juce::jmax (maxCells, r->numCells());
    sceneBar->setCount (maxCells);
    sceneBar->setBounds (pad, pad, cellsX + juce::jmax (1, maxCells) * cellStep, sceneH);
    int y = pad + sceneH + 8, maxW = headerW;
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
