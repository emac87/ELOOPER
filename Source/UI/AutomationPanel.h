#pragma once
// The automation layer: a see-through overlay shown on top of the loops, piano roll or mixer
// (toggled by the Automation button). Lanes for the selected loop's automation layer, or the
// selected track's timeline lanes, with + Lane, Pencil, Eraser, Write (record arm) and Close.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "UI/Selection.h"
#include "UI/Theme.h"

namespace el {

class AutomationPanel : public juce::Component,
                        private EngineController::Listener,
                        private juce::ChangeListener,
                        private juce::Timer {
public:
    AutomationPanel (EngineController&, Selection&);
    ~AutomationPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    // Only the top bar and the lane rows (or the empty-state card) take clicks; everything else
    // passes through to the page underneath (loops, mixer, editor).
    bool hitTest (int x, int y) override;
    void visibilityChanged() override;
    std::function<void()> onClose;   // the overlay's Close button

private:
    // Compact custom-painted button in the strip's M / S style (toggle chip, plain chip, or remove icon).
    class Chip : public juce::Button {
    public:
        enum class Kind { Plain, Toggle, Remove };
        Chip (const juce::String& label, Kind k = Kind::Plain, juce::Colour on = theme::accent)
            : juce::Button (label), kind (k), onColour (on)
        {
            setClickingTogglesState (k == Kind::Toggle);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const float dim = isEnabled() ? 1.0f : 0.4f;
            if (kind == Kind::Remove)
            {
                const auto col = (over || down) ? theme::record : theme::textDim;
                if (over || down) { g.setColour (theme::record.withAlpha (0.18f)); g.fillRoundedRectangle (r, 4.0f); }
                g.setColour ((down ? col.brighter (0.3f) : col).withMultipliedAlpha (dim));
                const auto c = r.getCentre();
                const float d = juce::jmin (r.getWidth(), r.getHeight()) * 0.2f;
                g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d, 2.0f);
                g.drawLine (c.x - d, c.y + d, c.x + d, c.y - d, 2.0f);
                return;
            }
            const bool on = kind == Kind::Toggle && getToggleState();
            const auto base = on ? onColour : theme::panelRaised;
            g.setColour ((down ? base.brighter (0.25f) : over ? base.brighter (0.12f) : base).withMultipliedAlpha (dim));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour ((on ? onColour.brighter (0.3f) : (over ? theme::textDim : theme::outline)).withMultipliedAlpha (dim));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
            const auto onText = onColour.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white;
            g.setColour ((on ? onText : theme::textDim.brighter (0.3f)).withMultipliedAlpha (dim));
            g.setFont (theme::font (12.5f, true));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    private:
        Kind kind;
        juce::Colour onColour;
    };

    class LaneRow;
    class Container;

    EngineController& controller;
    Selection& selection;

    Chip loopLayerButton { "Loop layer", Chip::Kind::Toggle }, trackLanesButton { "Track lanes", Chip::Kind::Toggle };
    Chip pencilButton { "Pencil", Chip::Kind::Toggle }, eraserButton { "Eraser", Chip::Kind::Toggle };
    Chip writeButton { "Write", Chip::Kind::Toggle, theme::record };
    Chip addLaneButton { "+ Lane" };
    Chip closeButton { "Close" };
    juce::Viewport viewport;
    std::unique_ptr<Container> container;

    AutomationOwner owner;
    bool ownerValid = false;
    bool useTrackLanes = false;
    Id lastLoopSel = kInvalidId, lastTrackSel = kInvalidId;
    bool dirty = true;
    int ticksSinceRefresh = 100;
    juce::String title;
    juce::Colour titleColour { 0xffffffff };

    void sessionChanged (Change, Id) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void resolveOwner();
    void refresh();
    void rebuildRows();
    void updateRows();
    void layoutRows();
    int contentHeight() const;   // height of the clickable lane area under the top bar
    void showAddMenu();
    void removeLane (int index);
    double laneLength() const;
};

} // namespace el
