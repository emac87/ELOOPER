#include "UI/LoopGrid.h"
#include "UI/LookAndFeel.h"
#include "UI/ColourPicker.h"
#include "UI/PluginBrowser.h"
#include "Engine/MidiMapping.h"

namespace el {

namespace {
    constexpr int cell = theme::loopCellSize;              // loop cells are square
    constexpr int colW = cell;                             // a track column
    constexpr int colGap = 10;
    constexpr int groupGap = 4;                            // between a multi-out instrument and its outputs
    constexpr int rowStep = cell + 6;                      // loop rows (= scene rows)
    constexpr int plusH = 34;
    constexpr int labelH = TrackHeader::rowHeight;
    constexpr int sceneW = 112;                            // scene column on the left
    constexpr int pad = 10;
    constexpr int addTrackW = 72;
}

// ---- a viewport that tells the grid when it scrolled (labels / scenes follow the loops) ------------
class LoopGrid::SyncViewport : public juce::Viewport {
public:
    std::function<void()> onMoved;
    void visibleAreaChanged (const juce::Rectangle<int>&) override { if (onMoved) onMoved(); }
};

// ---- one track column: its loops (top-down) and "+" ------------------------------------------------
class LoopGrid::Column : public juce::Component {
public:
    Column (LoopGrid& g, const TrackPtr& track) : trackId (track->id), grid (g), isInstrument (track->kind == TrackKind::Instrument)
    {
        for (auto& l : track->loops) {
            if (! EngineController::isSceneLoop (*l)) continue;   // automation loops are hidden for now
            auto* c = cells.add (new LoopCell (g.controller, g.selection, g.midiMap, track->id, l->id));
            c->onEdit = [this] (Id t, Id l2, bool midi) { if (grid.onEditLoop) grid.onEditLoop (t, l2, midi); };
            addAndMakeVisible (c);
        }
        plus.setButtonText ("+");
        plus.setTooltip (isInstrument ? "Add a MIDI loop" : "Add an audio loop");
        plus.setColour (juce::TextButton::buttonColourId, theme::panel);
        plus.setColour (juce::TextButton::textColourOffId, theme::accent);
        plus.onClick = [this] {
            const Id id = grid.controller.addLoop (trackId, isInstrument ? LoopType::Midi : LoopType::Audio);
            if (id != kInvalidId) grid.selection.select (trackId, id);
        };
        addAndMakeVisible (plus);
        setSize (colW, heightFor (cells.size()));
    }

    static int heightFor (int n) { return n * rowStep + plusH; }
    int numCells() const { return cells.size(); }

    void resized() override
    {
        int y = 0;
        for (auto* c : cells) { c->setTopLeftPosition (0, y); y += rowStep; }
        plus.setBounds (0, y, colW, plusH);
    }
    void refreshAll() { for (auto* c : cells) c->refresh(); }
    void poll (bool blink) { for (auto* c : cells) c->poll (blink); }

    const Id trackId;

private:
    LoopGrid& grid;
    const bool isInstrument;
    juce::OwnedArray<LoopCell> cells;
    juce::TextButton plus;
};

// ---- the scene buttons down the left side, one per loop row --------------------------------------
class LoopGrid::SceneButtons : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit SceneButtons (LoopGrid& g) : grid (g) {}
    void setCount (int n) { if (n != count) { count = n; setSize (sceneW, juce::jmax (1, n * rowStep)); repaint(); } }
    int getCount() const noexcept { return count; }
    void setLit (int scene) { if (scene != lit) { lit = scene; repaint(); } }

    juce::Rectangle<int> sceneRect (int i) const { return { 0, i * rowStep, sceneW - 8, cell }; }
    int sceneAt (juce::Point<int> p) const
    {
        for (int i = 0; i < count; ++i) if (sceneRect (i).contains (p)) return i;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < count; ++i) {
            const auto r = sceneRect (i);
            const bool on = lit == i, hot = hover == i;
            auto rf = r.toFloat().reduced (0.5f);
            g.setColour (on ? theme::play.withAlpha (0.3f) : hot ? theme::panelRaised : theme::panel);
            g.fillRoundedRectangle (rf, 6.0f);
            g.setColour (on ? theme::play : hot ? theme::play.withAlpha (0.7f) : theme::outline);
            g.drawRoundedRectangle (rf, 6.0f, on ? 1.5f : 1.0f);
            // big play triangle + "Scene N"
            const float cx = rf.getCentreX(), cy = rf.getCentreY() - 10.0f;
            juce::Path p; p.addTriangle (cx - 8.0f, cy - 10.0f, cx - 8.0f, cy + 10.0f, cx + 10.0f, cy);
            g.setColour (on ? juce::Colours::white : theme::play);
            g.fillPath (p);
            g.setColour (on ? juce::Colours::white : theme::text);
            g.setFont (theme::font (13.5f, true));
            g.drawText ("Scene " + juce::String (i + 1), r.withTrimmedTop (r.getHeight() / 2 + 8).withHeight (20), juce::Justification::centred);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = sceneAt (e.getPosition());
        if (h != hover) { hover = h; repaint(); }
        setTooltip (h >= 0 ? "Scene " + juce::String (h + 1) + ": play row " + juce::String (h + 1)
                             + " on every track and stop all other loops (right-click: MIDI learn)" : juce::String());
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = sceneAt (e.getPosition());
        if (i < 0) return;
        if (e.mods.isPopupMenu()) { grid.learnMenu (MappingTarget::Kind::Scene, i, this); return; }
        grid.controller.launchScene (i);
        setLit (i);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        grid.gridView->mouseWheelMove (e.getEventRelativeTo (grid.gridView.get()), w);   // scroll the loops
    }

private:
    LoopGrid& grid;
    int count = 0, lit = -1, hover = -1;
};

// ---- grid -----------------------------------------------------------------------------------------
LoopGrid::LoopGrid (EngineController& c, Selection& s, MidiMapping& mm, PresetManager& p)
    : controller (c), selection (s), midiMap (mm), presets (p)
{
    gridView = std::make_unique<SyncViewport>();
    labelView = std::make_unique<SyncViewport>();
    sceneView = std::make_unique<SyncViewport>();
    gridView->setViewedComponent (&gridContent, false);
    gridView->setScrollBarsShown (true, true);
    gridView->setScrollBarThickness (10);
    labelView->setViewedComponent (&labelContent, false);
    labelView->setScrollBarsShown (false, false, false, true);   // wheel scrolls it sideways
    sceneView->setViewedComponent (&sceneContent, false);
    sceneView->setScrollBarsShown (false, false, true, false);
    gridView->onMoved = [this] { syncFrom (gridView.get()); };
    labelView->onMoved = [this] { syncFrom (labelView.get()); };
    sceneView->onMoved = [this] { syncFrom (sceneView.get()); };
    addAndMakeVisible (*gridView);
    addAndMakeVisible (*labelView);
    addAndMakeVisible (*sceneView);

    scenes = std::make_unique<SceneButtons> (*this);
    sceneContent.addAndMakeVisible (*scenes);

    addTrackButton.setTooltip ("Add a track: audio, instrument or bus");
    addTrackButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    addTrackButton.setColour (juce::TextButton::textColourOffId, theme::accent);
    addTrackButton.onClick = [this] { showAddTrackMenu(); };
    labelContent.addAndMakeVisible (addTrackButton);

    stopAllButton.setTooltip ("Stop every loop (right-click: MIDI learn)");
    stopAllButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    stopAllButton.setColour (juce::TextButton::textColourOffId, theme::record);
    stopAllButton.onAnyClick = [this] (bool popup) {
        if (popup) learnMenu (MappingTarget::Kind::StopAll, 0, &stopAllButton);
        else controller.stopAllLoops();
    };
    addAndMakeVisible (stopAllButton);

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
    gridView->setViewedComponent (nullptr, false);
    labelView->setViewedComponent (nullptr, false);
    sceneView->setViewedComponent (nullptr, false);
}

void LoopGrid::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::outline);   // divider above the labels
    g.fillRect (0, getHeight() - labelH - pad - 5, getWidth(), 1);
}

void LoopGrid::resized()
{
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom (labelH + pad + 4);
    stopAllButton.setBounds (bottom.getX() + pad, bottom.getY() + 4, sceneW - 8, labelH);
    labelView->setBounds (bottom.withTrimmedLeft (pad + sceneW).withTrimmedTop (4).withHeight (labelH));
    sceneView->setBounds (r.getX() + pad, r.getY() + pad, sceneW, r.getHeight() - pad);
    gridView->setBounds (r.withTrimmedLeft (pad + sceneW).withTrimmedTop (pad));
    layoutContent();
}

void LoopGrid::syncFrom (juce::Viewport* src)
{
    if (syncing) return;
    const juce::ScopedValueSetter<bool> s (syncing, true);
    const auto gp = gridView->getViewPosition();
    if (src == gridView.get()) {
        labelView->setViewPosition (gp.x, 0);
        sceneView->setViewPosition (0, gp.y);
    } else if (src == labelView.get()) {
        gridView->setViewPosition (labelView->getViewPositionX(), gp.y);
    } else if (src == sceneView.get()) {
        gridView->setViewPosition (gp.x, sceneView->getViewPositionY());
    }
}

void LoopGrid::sessionChanged (Change what, Id id)
{
    switch (what) {
        case Change::Structure:
        case Change::Session:
            needsRebuild = true;   // deferred to the timer: never destroy a component inside its own callback
            break;
        case Change::Track:
            for (auto& l : labels) if (l->getTrackId() == id) l->refresh();
            for (auto& c : columns) if (c->trackId == id) c->refreshAll();
            break;
        case Change::Loop:
            for (auto& c : columns) c->refreshAll();
            break;
        case Change::Clip: case Change::Automation: case Change::Transport: case Change::Timeline: break;
    }
}

void LoopGrid::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto& l : labels) l->repaint();
}

void LoopGrid::timerCallback()
{
    if (needsRebuild) rebuild();
    if (! isShowing()) return;
    ++tick;
    const bool blink = ((tick / 8) & 1) == 0;
    for (auto& c : columns) c->poll (blink);
    if ((tick & 3) == 0) updateSceneLight();
}

void LoopGrid::rebuild()
{
    needsRebuild = false;
    const auto viewPos = gridView->getViewPosition();
    for (auto& c : columns) gridContent.removeChildComponent (c.get());
    for (auto& l : labels) labelContent.removeChildComponent (l.get());
    columns.clear();
    labels.clear();

    const auto& tracks = controller.session().tracks;
    auto add = [this] (const TrackPtr& t, bool top) {
        auto col = std::make_unique<Column> (*this, t);
        gridContent.addAndMakeVisible (*col);
        columns.push_back (std::move (col));
        auto lab = std::make_unique<TrackHeader> (controller, selection, presets, t->id, colW);
        if (top) { const Id id = t->id; lab->onReorderDrop = [this, id] (const juce::MouseEvent& e) { reorderDrop (id, e); }; }
        lab->getProperties().set ("child", ! top);
        labelContent.addAndMakeVisible (*lab);
        labels.push_back (std::move (lab));
    };

    for (auto& t : tracks) {
        if (t->bounced || t->kind == TrackKind::Master || t->kind == TrackKind::Bus) continue;
        if (t->kind == TrackKind::AuxOut && controller.session().findTrack (t->auxParent) != nullptr) continue;   // shown in its group
        add (t, true);
        if (t->kind != TrackKind::Instrument || ! t->folderOpen) continue;
        for (auto& o : tracks)
            if (! o->bounced && o->kind == TrackKind::AuxOut && o->auxParent == t->id) add (o, false);
        if (auto f = controller.folderBusOf (t->id); f && ! f->bounced) add (f, false);
    }
    // Plain buses are mixer-only; one that holds loops still shows so they stay reachable.
    for (auto& t : tracks)
        if (! t->bounced && t->kind == TrackKind::Bus && t->folderOf == kInvalidId
            && std::any_of (t->loops.begin(), t->loops.end(), [] (const LoopPtr& l) { return EngineController::isSceneLoop (*l); }))
            add (t, true);

    layoutContent();
    gridView->setViewPosition (viewPos);
    syncFrom (gridView.get());
}

void LoopGrid::layoutContent()
{
    int x = 0, maxCells = 0, maxH = 0;
    for (size_t i = 0; i < columns.size(); ++i) {
        const bool child = (bool) labels[i]->getProperties()["child"];
        if (i > 0) x += child ? groupGap : colGap;
        columns[i]->setTopLeftPosition (x, 0);
        labels[i]->setBounds (x, 0, colW, labelH);
        x += colW;
        maxCells = juce::jmax (maxCells, columns[i]->numCells());
        maxH = juce::jmax (maxH, columns[i]->getHeight());
    }
    const int contentW = x + colGap + addTrackW + pad;
    addTrackButton.setBounds (x + (columns.empty() ? 0 : colGap), 0, addTrackW, labelH);
    gridContent.setSize (juce::jmax (contentW, gridView->getMaximumVisibleWidth()),
                         juce::jmax (maxH + pad, gridView->getMaximumVisibleHeight()));
    labelContent.setSize (gridContent.getWidth() + 20, labelH);   // +20: room past the grid's scrollbar
    scenes->setCount (maxCells);
    sceneContent.setSize (sceneW, gridContent.getHeight() + 20);
    scenes->setTopLeftPosition (0, 0);
}

void LoopGrid::learnMenu (MappingTarget::Kind k, int scene, juce::Component* target)
{
    auto* mm = &midiMap;
    ActionMenu am;
    juce::PopupMenu m;
    am.add (m, "MIDI Learn", [mm, k, scene] {
        MappingTarget t; t.kind = k; t.loopId = (Id) scene;
        mm->startLearn (t);
    });
    am.show (m, target);
}

void LoopGrid::updateSceneLight()
{
    // A scene is lit when every non-empty loop in its row is playing (or about to) and no loop
    // outside it is.
    const int n = scenes->getCount();
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
    scenes->setLit (lit);
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
    if (labels.empty()) return;
    const int x = e.getEventRelativeTo (&labelContent).x;
    int idx = 0;
    for (size_t i = 0; i < labels.size(); ++i)
        if (x >= labels[i]->getX() - colGap / 2) idx = (int) i;
    while (idx > 0 && (bool) labels[(size_t) idx]->getProperties()["child"]) --idx;   // onto a group = onto its instrument
    const Id target = labels[(size_t) idx]->getTrackId();
    if (target == trackId) return;
    const auto& tracks = controller.session().tracks;
    for (size_t k = 0; k < tracks.size(); ++k)
        if (tracks[k]->id == target) { controller.moveTrack (trackId, (int) k); return; }
}

} // namespace el
