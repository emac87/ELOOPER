#include "UI/MixerPanel.h"
#include "UI/Theme.h"

namespace el {

MixerPanel::MixerPanel (EngineController& c, Selection& s) : controller (c), selection (s)
{
    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    controller.addListener (this);
    selection.addChangeListener (this);
    rebuild();
    startTimerHz (theme::uiRefreshHz);
}

MixerPanel::~MixerPanel()
{
    stopTimer();
    cancelPendingUpdate();
    selection.removeChangeListener (this);
    controller.removeListener (this);
    viewport.setViewedComponent (nullptr, false);
}

void MixerPanel::sessionChanged (Change what, Id)
{
    if (what == Change::Structure || what == Change::Session || what == Change::Track)
        triggerAsyncUpdate();
}

std::unique_ptr<ChannelStrip> MixerPanel::makeStrip (Id trackId)
{
    auto strip = std::make_unique<ChannelStrip> (controller, trackId);
    strip->onSelect = [this] (Id id) { selection.select (id); };
    strip->onPluginPresetMenu = [this] (Id slotId, juce::Component* c)
    {
        if (onPluginPresetMenu) onPluginPresetMenu (slotId, c);
    };
    return strip;
}

void MixerPanel::rebuild()
{
    std::vector<Id> order;
    const auto& tracks = controller.session().tracks;
    for (auto& t : tracks)
        if (! t->bounced && (t->kind == TrackKind::Audio || t->kind == TrackKind::Instrument || t->kind == TrackKind::AuxOut))
            order.push_back (t->id);
    for (auto& t : tracks)
        if (! t->bounced && t->kind == TrackKind::Bus)
            order.push_back (t->id);

    std::vector<std::unique_ptr<ChannelStrip>> next;
    for (Id id : order)
    {
        auto it = std::find_if (strips.begin(), strips.end(), [id] (auto& s) { return s && s->getTrackId() == id; });
        if (it != strips.end()) { (*it)->rebuild(); next.push_back (std::move (*it)); }
        else next.push_back (makeStrip (id));
    }
    stripHolder.removeAllChildren();
    strips = std::move (next);   // destroys strips of removed tracks
    for (auto& s : strips) stripHolder.addAndMakeVisible (*s);

    if (! masterStrip) { masterStrip = makeStrip (kMasterId); addAndMakeVisible (*masterStrip); }
    else masterStrip->rebuild();

    layoutStrips();
    updateSelection();
}

void MixerPanel::updateSelection()
{
    for (auto& s : strips) s->setSelected (s->getTrackId() == selection.trackId);
    if (masterStrip) masterStrip->setSelected (selection.trackId == kMasterId);
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void MixerPanel::resized()
{
    auto b = getLocalBounds();
    b.removeFromTop (1);
    if (masterStrip) masterStrip->setBounds (b.removeFromRight (theme::stripWidth));
    viewport.setBounds (b);
    layoutStrips();
}

void MixerPanel::layoutStrips()
{
    const int h = std::max (50, viewport.getHeight() - viewport.getScrollBarThickness());
    const int w = std::max ((int) strips.size() * theme::stripWidth, viewport.getWidth());
    stripHolder.setSize (w, h);
    int x = 0;
    for (auto& s : strips) { s->setBounds (x, 0, theme::stripWidth, h); x += theme::stripWidth; }
}

void MixerPanel::timerCallback()
{
    if (! isShowing()) return;
    for (auto& s : strips) s->refreshValues();
    if (masterStrip) masterStrip->refreshValues();
}

} // namespace el
