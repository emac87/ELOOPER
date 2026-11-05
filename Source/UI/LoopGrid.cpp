#include "UI/LoopGrid.h"
#include "UI/LookAndFeel.h"
#include "UI/ColourPicker.h"
#include "UI/PluginBrowser.h"

namespace el {

namespace {
    constexpr int colWidth = theme::stripWidth;
    constexpr int colGap = 10;
    constexpr int cellStep = theme::loopCellSize + 4;
    constexpr int addAreaH = 52;
    constexpr int pad = 14;
    constexpr int addColWidth = 96;
}

// ---- one track column -----------------------------------------------------------------------
class LoopGrid::Column : public juce::Component {
public:
    Column (LoopGrid& g, TrackPtr track)
        : trackId (track->id), grid (g), isInstrument (track->kind == TrackKind::Instrument),
          header (g.controller, g.selection, g.presets, track->id)
    {
        addAndMakeVisible (header);
        header.onReorderDrop = [this] (const juce::MouseEvent& e) { grid.reorderDrop (trackId, e); };

        for (auto& l : track->loops) {
            auto* c = cells.add (new LoopCell (g.controller, g.selection, g.midiMap, track->id, l->id));
            c->onEdit = [this] (Id t, Id l2, bool midi) { if (grid.onEditLoop) grid.onEditLoop (t, l2, midi); };
            addAndMakeVisible (c);
        }
        setupButton (plusButton, "+", "Add a loop (choose the type)");
        setupButton (addAudio, "+A", "Add an audio loop");
        setupButton (addMidi, "+M", "Add a MIDI loop");
        setupButton (addAuto, "+Auto", "Add an automation loop");
        addAudio.setEnabled (! isInstrument);
        addMidi.setEnabled (isInstrument);
        plusButton.onClick = [this] { showTypeMenu(); };
        addAudio.onClick = [this] { add (LoopType::Audio); };
        addMidi.onClick = [this] { add (LoopType::Midi); };
        addAuto.onClick = [this] { add (LoopType::Automation); };
        setSize (colWidth, heightFor ((int) track->loops.size()));
    }

    static int heightFor (int numLoops)
    {
        return TrackHeader::headerHeight + 8 + numLoops * cellStep + addAreaH;
    }

    void resized() override
    {
        header.setBounds (0, 0, colWidth, TrackHeader::headerHeight);
        int y = TrackHeader::headerHeight + 8;
        for (auto* c : cells) { c->setTopLeftPosition ((colWidth - theme::loopCellSize) / 2, y); y += cellStep; }
        plusButton.setBounds (4, y, colWidth - 8, 26);
        const int w = (colWidth - 8 - 6) / 3;
        addAudio.setBounds (4, y + 30, w, 20);
        addMidi.setBounds (4 + w + 3, y + 30, w, 20);
        addAuto.setBounds (4 + (w + 3) * 2, y + 30, w, 20);
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

private:
    void setupButton (juce::TextButton& b, const juce::String& text, const juce::String& tip)
    {
        b.setButtonText (text);
        b.setTooltip (tip);
        b.setColour (juce::TextButton::buttonColourId, theme::panel);
        addAndMakeVisible (b);
    }

    void add (LoopType type)
    {
        const Id id = grid.controller.addLoop (trackId, type);
        if (id != kInvalidId) grid.selection.select (trackId, id);
    }

    void showTypeMenu()
    {
        auto* c = &grid.controller; auto* sel = &grid.selection;
        const Id tid = trackId;
        ActionMenu am;
        juce::PopupMenu m;
        auto make = [c, sel, tid] (LoopType t) {
            return [c, sel, tid, t] { const Id id = c->addLoop (tid, t); if (id != kInvalidId) sel->select (tid, id); };
        };
        am.add (m, "Audio Loop", make (LoopType::Audio), false, ! isInstrument);
        am.add (m, "MIDI Loop", make (LoopType::Midi), false, isInstrument);
        am.add (m, "Automation Loop", make (LoopType::Automation));
        am.show (m, &plusButton);
    }

    LoopGrid& grid;
    const bool isInstrument;
    TrackHeader header;
    juce::OwnedArray<LoopCell> cells;
    juce::TextButton plusButton, addAudio, addMidi, addAuto;
};

// ---- grid -----------------------------------------------------------------------------------
LoopGrid::LoopGrid (EngineController& c, Selection& s, MidiMapping& mm, PresetManager& p)
    : controller (c), selection (s), midiMap (mm), presets (p)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    addTrackButton.setTooltip ("Add a track: audio, instrument or bus");
    addTrackButton.setColour (juce::TextButton::buttonColourId, theme::panel);
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
            needsRebuild = true;   // deferred to the timer: never destroy a column inside its own callback
            break;
        case Change::Track:
            for (auto& col : columns)
                if (col->trackId == id) col->refreshAll();
            break;
        case Change::Loop:
            for (auto& col : columns) col->refreshAll();
            break;
        case Change::Clip: case Change::Automation: case Change::Transport: case Change::Timeline: break;
    }
}

void LoopGrid::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto& col : columns) col->getHeader().repaint();
}

void LoopGrid::timerCallback()
{
    if (needsRebuild) rebuild();
    ++tick;
    const bool blink = ((tick / 8) & 1) == 0;
    for (auto& col : columns) col->poll (blink);
}

void LoopGrid::rebuild()
{
    needsRebuild = false;
    columns.clear();
    const auto& tracks = controller.session().tracks;
    std::vector<TrackPtr> ordered;
    for (auto& t : tracks)
        if (! t->bounced && t->kind != TrackKind::Master && t->kind != TrackKind::Bus) ordered.push_back (t);
    for (auto& t : tracks)
        if (! t->bounced && t->kind == TrackKind::Bus) ordered.push_back (t);

    for (auto& t : ordered) {
        auto col = std::make_unique<Column> (*this, t);
        content.addAndMakeVisible (*col);
        columns.push_back (std::move (col));
    }
    layoutContent();
}

void LoopGrid::layoutContent()
{
    int x = pad, maxH = 0;
    for (auto& col : columns) {
        col->setTopLeftPosition (x, pad);
        x += colWidth + colGap;
        maxH = juce::jmax (maxH, col->getHeight());
    }
    addTrackButton.setBounds (x, pad, addColWidth, TrackHeader::headerHeight);
    maxH = juce::jmax (maxH, TrackHeader::headerHeight);
    content.setSize (juce::jmax (x + addColWidth + pad, viewport.getMaximumVisibleWidth()),
                     juce::jmax (maxH + pad * 2, viewport.getMaximumVisibleHeight()));
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
    am.add (m, "Add Bus", [c] { c->addTrack (TrackKind::Bus); });
    am.show (m, &addTrackButton);
}

void LoopGrid::reorderDrop (Id trackId, const juce::MouseEvent& e)
{
    if (columns.empty()) return;
    const int x = e.getEventRelativeTo (&content).x;
    const int idx = juce::jlimit (0, (int) columns.size() - 1, (x - pad + colGap / 2) / (colWidth + colGap));
    const Id target = columns[(size_t) idx]->trackId;
    if (target == trackId) return;
    const auto& tracks = controller.session().tracks;
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i]->id == target) { controller.moveTrack (trackId, (int) i); return; }
}

} // namespace el
