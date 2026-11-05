#include "UI/AutomationPanel.h"
#include "UI/AutomationLaneView.h"
#include "UI/Theme.h"
#include "Core/ParamMapping.h"

namespace el {

namespace {
constexpr int kRowHeight = 70, kHeaderWidth = 140, kTopBar = 30;

juce::String formatValue (const ParamTarget& t, float v)
{
    switch (t.kind)
    {
        case ParamKind::Volume:
        {
            const float db = pm::volumeToDb (v);
            return db <= -100.0f ? "-inf dB" : juce::String (db, 1) + " dB";
        }
        case ParamKind::Pan:
        {
            const float b = pm::panToBipolar (v);
            if (std::abs (b) < 0.01f) return "C";
            return juce::String (juce::roundToInt (std::abs (b) * 100.0f)) + (b < 0 ? " L" : " R");
        }
        case ParamKind::InputGain: return juce::String (pm::inputGainToDb (v), 1) + " dB";
        case ParamKind::EqBand:    return juce::String (pm::eqToDb (v), 1) + " dB";
        case ParamKind::Mute:
        case ParamKind::PluginBypass: return v >= 0.5f ? "On" : "Off";
        case ParamKind::LoopVolume:
        case ParamKind::SendLevel:
        case ParamKind::PluginParam: break;
    }
    return juce::String (juce::roundToInt (v * 100.0f)) + " %";
}
}

// ---- one lane = header + body --------------------------------------------------------------
class AutomationPanel::LaneRow : public juce::Component {
public:
    LaneRow (AutomationPanel& p, EngineController& c, AutomationOwner o, int idx, const juce::String& name)
        : panel (p), controller (c), owner (o), index (idx), view (c, o, idx)
    {
        nameLabel.setText (name, juce::dontSendNotification);
        nameLabel.setFont (theme::font (12.0f, true));
        nameLabel.setColour (juce::Label::textColourId, theme::text);
        nameLabel.setMinimumHorizontalScale (0.7f);
        valueLabel.setFont (theme::font (11.0f));
        valueLabel.setColour (juce::Label::textColourId, theme::textDim);
        enable.setButtonText ("On");
        enable.onClick = [this] { modify ([this] (AutomationLane& l) { l.enabled = enable.getToggleState(); }); };
        clear.setButtonText ("Clear");
        clear.onClick = [this] { modify ([] (AutomationLane& l) { l.points.clear(); }); };
        remove.setButtonText ("X");
        remove.setTooltip ("Remove lane");
        remove.onClick = [this] { panel.removeLane (index); };
        for (auto* c2 : { (juce::Component*) &nameLabel, (juce::Component*) &valueLabel, (juce::Component*) &enable,
                          (juce::Component*) &clear, (juce::Component*) &remove, (juce::Component*) &view })
            addAndMakeVisible (c2);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panelRaised);
        g.fillRect (0, 0, kHeaderWidth, getHeight());
        g.setColour (theme::outline);
        g.drawRect (getLocalBounds());
    }

    void resized() override
    {
        auto h = getLocalBounds().removeFromLeft (kHeaderWidth).reduced (4, 3);
        nameLabel.setBounds (h.removeFromTop (18));
        valueLabel.setBounds (h.removeFromTop (16));
        h.removeFromTop (2);
        auto row = h.removeFromTop (22);
        enable.setBounds (row.removeFromLeft (44));
        row.removeFromLeft (2);
        remove.setBounds (row.removeFromRight (24));
        row.removeFromRight (2);
        clear.setBounds (row);
        view.setBounds (getLocalBounds().withTrimmedLeft (kHeaderWidth));
    }

    void update (const AutomationLane& l, double len, int bpb, juce::Colour col, bool pencil)
    {
        view.setLane (l);
        view.setLengthBeats (len);
        view.setBeatsPerBar (bpb);
        view.setColour (col);
        view.setPencil (pencil);
        enable.setToggleState (l.enabled, juce::dontSendNotification);
        target = l.target;
        updateValue();
    }

    void updateValue()
    {
        valueLabel.setText (formatValue (target, controller.getParam (owner.trackId, target)), juce::dontSendNotification);
    }
    ParamTarget target;
    AutomationLaneView& getView() { return view; }

private:
    AutomationPanel& panel;
    EngineController& controller;
    AutomationOwner owner;
    int index;
    juce::Label nameLabel, valueLabel;
    juce::ToggleButton enable;
    juce::TextButton clear, remove;
    AutomationLaneView view;

    template <typename Fn> void modify (Fn fn)
    {
        const auto* lanes = controller.automationLanes (owner);
        if (lanes == nullptr || index >= (int) lanes->size()) return;
        auto l = (*lanes)[(size_t) index];
        fn (l);
        controller.setAutomationLane (owner, index, l);
    }
};

class AutomationPanel::Container : public juce::Component {
public:
    std::vector<std::unique_ptr<LaneRow>> rows;
    void layoutRows (int width)
    {
        for (size_t i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, (int) i * kRowHeight, width, kRowHeight);
        setSize (width, std::max (1, (int) rows.size() * kRowHeight));
    }
};

AutomationPanel::AutomationPanel (EngineController& c, Selection& s) : controller (c), selection (s)
{
    container = std::make_unique<Container>();
    viewport.setViewedComponent (container.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    for (auto* b : { &loopLayerButton, &trackLanesButton })
    {
        b->setClickingTogglesState (false);
        addChildComponent (*b);
    }
    loopLayerButton.onClick = [this] { useTrackLanes = false; dirty = true; ticksSinceRefresh = 100; refresh(); };
    trackLanesButton.onClick = [this] { useTrackLanes = true; dirty = true; ticksSinceRefresh = 100; refresh(); };
    pencilButton.setTooltip ("Pencil: drag to draw points freehand");
    pencilButton.onClick = [this] { dirty = true; };
    addAndMakeVisible (pencilButton);
    addLaneButton.onClick = [this] { showAddMenu(); };
    addAndMakeVisible (addLaneButton);

    controller.addListener (this);
    selection.addChangeListener (this);
    refresh();
    startTimerHz (theme::uiRefreshHz);
}

AutomationPanel::~AutomationPanel()
{
    stopTimer();
    selection.removeChangeListener (this);
    controller.removeListener (this);
    viewport.setViewedComponent (nullptr, false);
}

void AutomationPanel::sessionChanged (Change what, Id)
{
    if (what == Change::Automation || what == Change::Structure || what == Change::Session
        || what == Change::Transport || what == Change::Track || what == Change::Loop)
        dirty = true;
}

void AutomationPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    dirty = true;
    ticksSinceRefresh = 100;
    refresh();
}

void AutomationPanel::timerCallback()
{
    ++ticksSinceRefresh;
    if (dirty && ticksSinceRefresh >= 3) refresh();     // at most ~10 refreshes per second

    if (! ownerValid) return;
    bool vis = false; double beat = 0;
    if (owner.loopId != kInvalidId)
    {
        const auto st = controller.loopStatus (owner.loopId);
        vis = st.state == LoopState::Playing || st.state == LoopState::Recording || st.state == LoopState::Overdubbing;
        beat = st.phaseBeats;
    }
    else
    {
        const auto ts = controller.transportStatus();
        vis = ts.playing; beat = ts.beat;
    }
    for (auto& r : container->rows)
    {
        r->getView().setPlayhead (beat, vis);
        r->updateValue();
    }
}

void AutomationPanel::resolveOwner()
{
    auto& s = controller.session();
    if (selection.loopId != lastLoopSel || selection.trackId != lastTrackSel)
    {
        useTrackLanes = false;
        lastLoopSel = selection.loopId; lastTrackSel = selection.trackId;
    }
    ownerValid = false;
    title = "Automation";
    const auto tr = s.findTrack (selection.trackId);
    if (tr == nullptr) { owner = {}; return; }
    const auto loop = selection.loopId != kInvalidId ? s.findLoop (selection.loopId) : nullptr;
    owner.trackId = tr->id;
    owner.loopId = (loop != nullptr && ! useTrackLanes) ? loop->id : kInvalidId;
    ownerValid = true;
    titleColour = owner.loopId != kInvalidId ? controller.loopColour (owner.loopId) : tr->colour;
    title = juce::String (juce::CharPointer_UTF8 ("Automation \xe2\x80\x94 ")) + tr->name + " / "
            + (owner.loopId != kInvalidId ? loop->name : juce::String ("Track (timeline)"));
    const bool showToggle = loop != nullptr;
    loopLayerButton.setVisible (showToggle);
    trackLanesButton.setVisible (showToggle);
    loopLayerButton.setColour (juce::TextButton::buttonColourId, ! useTrackLanes ? theme::accent.darker (0.4f) : theme::panelRaised);
    trackLanesButton.setColour (juce::TextButton::buttonColourId, useTrackLanes ? theme::accent.darker (0.4f) : theme::panelRaised);
}

double AutomationPanel::laneLength() const
{
    double len = controller.automationLengthBeats (owner);
    if (owner.loopId == kInvalidId)
        len = std::max (len, 32.0 * controller.session().transport.beatsPerBar);
    return len > 0 ? len : 16.0;
}

void AutomationPanel::refresh()
{
    dirty = false; ticksSinceRefresh = 0;
    resolveOwner();
    resized();      // re-evaluates toggle visibility layout
    const auto* lanes = ownerValid ? controller.automationLanes (owner) : nullptr;
    bool same = lanes != nullptr && lanes->size() == container->rows.size();
    if (same)
        for (size_t i = 0; i < lanes->size(); ++i)
            if ((*lanes)[i].target != container->rows[i]->target) { same = false; break; }
    if (lanes == nullptr && ! container->rows.empty()) same = false;
    if (same) updateRows(); else rebuildRows();
    repaint();
}

void AutomationPanel::rebuildRows()
{
    container->rows.clear();
    container->removeAllChildren();
    const auto* lanes = ownerValid ? controller.automationLanes (owner) : nullptr;
    if (lanes != nullptr)
    {
        const auto infos = controller.listParams (owner.trackId);
        for (size_t i = 0; i < lanes->size(); ++i)
        {
            juce::String name = "Parameter";
            for (auto& pi : infos) if (pi.target == (*lanes)[i].target) { name = pi.name; break; }
            auto row = std::make_unique<LaneRow> (*this, controller, owner, (int) i, name);
            container->addAndMakeVisible (*row);
            container->rows.push_back (std::move (row));
        }
    }
    updateRows();
    layoutRows();
}

void AutomationPanel::updateRows()
{
    const auto* lanes = ownerValid ? controller.automationLanes (owner) : nullptr;
    if (lanes == nullptr) return;
    const double len = laneLength();
    const int bpb = controller.session().transport.beatsPerBar;
    for (size_t i = 0; i < lanes->size() && i < container->rows.size(); ++i)
        container->rows[i]->update ((*lanes)[i], len, bpb, titleColour, pencilButton.getToggleState());
}

void AutomationPanel::layoutRows()
{
    container->layoutRows (std::max (100, viewport.getMaximumVisibleWidth()));
}

void AutomationPanel::resized()
{
    auto b = getLocalBounds();
    auto top = b.removeFromTop (kTopBar).reduced (4, 3);
    addLaneButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    pencilButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    if (trackLanesButton.isVisible())
    {
        trackLanesButton.setBounds (top.removeFromRight (84));
        loopLayerButton.setBounds (top.removeFromRight (84));
    }
    viewport.setBounds (b);
    layoutRows();
}

void AutomationPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (titleColour);
    g.fillRect (0, 0, 3, kTopBar);
    g.setFont (theme::font (13.0f, true));
    g.setColour (ownerValid ? titleColour : theme::textDim);
    int right = addLaneButton.getX();
    if (loopLayerButton.isVisible()) right = loopLayerButton.getX();
    else right = pencilButton.getX();
    g.drawText (ownerValid ? title : juce::String (juce::CharPointer_UTF8 ("Automation \xe2\x80\x94 select a track or loop")), 10, 0, std::max (10, right - 14), kTopBar, juce::Justification::centredLeft, true);

    if (container->rows.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawFittedText (ownerValid
            ? "Automation is recorded into a loop's layer whenever you move a control on its channel while that loop is recording or overdubbing."
            : "Select a track or a loop to edit its automation.",
            getLocalBounds().withTrimmedTop (kTopBar).reduced (24, 8), juce::Justification::centred, 3);
    }
}

void AutomationPanel::removeLane (int index)
{
    const auto o = owner;
    juce::Component::SafePointer<AutomationPanel> safe (this);
    juce::MessageManager::callAsync ([safe, o, index]
    {
        if (safe == nullptr) return;
        safe->controller.removeAutomationLane (o, index);
        safe->dirty = true; safe->ticksSinceRefresh = 100;
    });
}

void AutomationPanel::showAddMenu()
{
    if (! ownerValid) return;
    const auto infos = controller.listParams (owner.trackId);
    const auto* lanes = controller.automationLanes (owner);
    std::vector<juce::String> groups;
    std::vector<juce::PopupMenu> menus;
    for (size_t i = 0; i < infos.size(); ++i)
    {
        size_t gi = 0;
        for (; gi < groups.size(); ++gi) if (groups[gi] == infos[i].group) break;
        if (gi == groups.size()) { groups.push_back (infos[i].group); menus.emplace_back(); }
        bool has = false;
        if (lanes != nullptr) for (auto& l : *lanes) if (l.target == infos[i].target) { has = true; break; }
        juce::PopupMenu::Item item (infos[i].name);
        item.itemID = (int) i + 1;
        item.isTicked = has;
        item.isEnabled = ! has;
        menus[gi].addItem (item);
    }
    juce::PopupMenu m;
    for (size_t gi = 0; gi < groups.size(); ++gi) m.addSubMenu (groups[gi].isEmpty() ? "Other" : groups[gi], menus[gi]);
    if (groups.empty()) m.addItem (-1, "No parameters", false, false);

    const auto o = owner;
    juce::Component::SafePointer<AutomationPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addLaneButton),
                     [safe, infos, o] (int r)
    {
        if (safe == nullptr || r <= 0 || r > (int) infos.size()) return;
        safe->controller.addAutomationLane (o, infos[(size_t) r - 1].target);
        safe->dirty = true; safe->ticksSinceRefresh = 100;
    });
}

} // namespace el
