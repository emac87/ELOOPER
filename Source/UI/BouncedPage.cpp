#include "UI/BouncedPage.h"
#include "UI/LookAndFeel.h"

namespace el {

class BouncedPage::Row : public juce::Component {
public:
    Row (EngineController& c, const TrackPtr& t) : controller (c), trackId (t->id)
    {
        colour = t->colour;
        name = t->name;
        loops = (int) t->loops.size();
        if (auto into = c.session().findTrack (t->bouncedInto)) info = "bounced into " + into->name;
        else info = "bounced";
        info += "  -  " + juce::String (loops) + (loops == 1 ? " loop" : " loops");

        restore.setButtonText ("Restore");
        restore.setTooltip ("Bring this track back to the main page");
        restore.onClick = [this] { controller.restoreBouncedTrack (trackId); };
        remove.setButtonText ("Delete");
        remove.onClick = [this] {
            auto* ctrl = &controller; const Id id = trackId;
            ui::confirm ("Delete Track", "Permanently delete \"" + name + "\" and its loops?", "Delete",
                         [ctrl, id] { ctrl->removeTrack (id); });
        };
        addAndMakeVisible (restore);
        addAndMakeVisible (remove);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (b, 7.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (b, 7.0f, 1.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (b.getX() + 8.0f, b.getY() + 8.0f, 6.0f, b.getHeight() - 16.0f, 3.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (19.0f, true));
        g.drawText (name, juce::Rectangle<int> (30, 8, getWidth() - 270, 26), juce::Justification::centredLeft, true);
        g.setColour (theme::textDim);
        g.setFont (theme::font (15.0f));
        g.drawText (info, juce::Rectangle<int> (30, 38, getWidth() - 270, 22), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        remove.setBounds (getWidth() - 96, 14, 84, 36);
        restore.setBounds (getWidth() - 190, 14, 88, 36);
    }

private:
    EngineController& controller;
    const Id trackId;
    juce::Colour colour;
    juce::String name, info;
    int loops = 0;
    juce::TextButton restore, remove;
};

BouncedPage::BouncedPage (EngineController& c) : controller (c)
{
    viewport.setViewedComponent (&content, false);
    addAndMakeVisible (viewport);
    controller.addListener (this);
    rebuild();
}

BouncedPage::~BouncedPage()
{
    cancelPendingUpdate();
    controller.removeListener (this);
    viewport.setViewedComponent (nullptr, false);
}

void BouncedPage::sessionChanged (Change what, Id)
{
    if (what == Change::Structure || what == Change::Session || what == Change::Track) triggerAsyncUpdate();
}

void BouncedPage::handleAsyncUpdate() { rebuild(); }

void BouncedPage::rebuild()
{
    rows.clear();
    for (auto& t : controller.session().tracks)
        if (t->bounced && t->kind != TrackKind::Master)
            content.addAndMakeVisible (rows.add (new Row (controller, t)));
    resized();
    repaint();
}

void BouncedPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    if (rows.isEmpty()) {
        g.setColour (theme::textDim);
        g.setFont (theme::font (18.0f));
        g.drawText ("Bounced tracks appear here.", getLocalBounds(), juce::Justification::centred);
    }
}

void BouncedPage::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = juce::jmax (300, viewport.getMaximumVisibleWidth() - 24);
    int y = 12;
    for (auto* r : rows) { r->setBounds (12, y, w, 64); y += 70; }
    content.setSize (w + 24, juce::jmax (y + 12, viewport.getMaximumVisibleHeight()));
}

} // namespace el
