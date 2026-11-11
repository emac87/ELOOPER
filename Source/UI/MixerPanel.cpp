#include "UI/MixerPanel.h"
#include "UI/Theme.h"
#include "UI/LoopGrid.h"

namespace el {

MixerPanel::MixerPanel (EngineController& c, Selection& s, PresetManager& p) : controller (c), selection (s), presets (p)
{
    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    viewport.onMoved = [this] { if (! settingScroll && onScrolledX) onScrolledX (viewport.getViewPositionX()); };
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
    auto strip = std::make_unique<ChannelStrip> (controller, presets, trackId);
    strip->onSelect = [this] (Id id) { selection.select (id); };
    return strip;
}

void MixerPanel::rebuild()
{
    // Same columns as the loop page (each strip under its track's loops), then the plain buses.
    std::vector<Id> order;
    std::vector<bool> child, onLoopPage;
    for (auto& e : looplayout::loopPageOrder (controller)) { order.push_back (e.trackId); child.push_back (e.child); onLoopPage.push_back (true); }
    auto has = [&order] (Id id) { return std::find (order.begin(), order.end(), id) != order.end(); };
    for (auto& t : controller.session().tracks)
        if (! t->bounced && t->kind == TrackKind::Bus && ! has (t->id)) {
            // A folded instrument's sum folder stays hidden like on the loop page.
            if (t->folderOf != kInvalidId)
                if (auto inst = controller.session().findTrack (t->folderOf); inst && ! inst->folderOpen) continue;
            order.push_back (t->id); child.push_back (false); onLoopPage.push_back (false);
        }
    childFlags = child;

    std::vector<std::unique_ptr<ChannelStrip>> next;
    for (Id id : order)
    {
        auto it = std::find_if (strips.begin(), strips.end(), [id] (auto& s) { return s && s->getTrackId() == id; });
        if (it != strips.end()) { (*it)->rebuild(); next.push_back (std::move (*it)); }
        else next.push_back (makeStrip (id));
        next.back()->setLoopPageTrack (onLoopPage[next.size() - 1]);
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
    // Master sits under the scene buttons; the strips start where the loop columns start.
    auto left = b.removeFromLeft (looplayout::leftGutter);
    if (masterStrip) masterStrip->setBounds (left.withWidth (theme::stripWidth).reduced (0, 2));
    viewport.setBounds (b);
    layoutStrips();
}

void MixerPanel::layoutStrips()
{
    const int h = std::max (50, viewport.getHeight() - viewport.getScrollBarThickness());
    int x = 0;
    for (size_t i = 0; i < strips.size(); ++i) {
        if (i > 0) x += (i < childFlags.size() && childFlags[i]) ? looplayout::groupGap : looplayout::gap;
        strips[i]->setBounds (x, 2, theme::stripWidth, h - 2);
        x += theme::stripWidth;
    }
    // Room past the last strip so the mixer can scroll as far as the loop page does.
    stripHolder.setSize (std::max (x + 200, viewport.getWidth()), h);
}

void MixerPanel::setScrollX (int x)
{
    if (x == viewport.getViewPositionX()) return;
    const juce::ScopedValueSetter<bool> s (settingScroll, true);
    viewport.setViewPosition (x, viewport.getViewPositionY());
}

void MixerPanel::timerCallback()
{
    if (! isShowing()) return;
    for (auto& s : strips) s->refreshValues();
    if (masterStrip) masterStrip->refreshValues();
}

} // namespace el
