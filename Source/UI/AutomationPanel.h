#pragma once
// The automation layer: a see-through overlay shown on top of the loops, piano roll or mixer
// (toggled by the Automation button). Lanes for the selected loop's automation layer, or the
// selected track's timeline lanes, with + Lane, Pencil, Eraser, Write (record arm) and Close.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "UI/Selection.h"

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
    std::function<void()> onClose;   // the overlay's Close button

private:
    class LaneRow;
    class Container;

    EngineController& controller;
    Selection& selection;

    juce::TextButton loopLayerButton { "Loop layer" }, trackLanesButton { "Track lanes" };
    juce::ToggleButton pencilButton { "Pencil" }, eraserButton { "Eraser" };
    juce::TextButton writeButton { "Write" };
    juce::TextButton addLaneButton { "+ Lane" };
    juce::TextButton closeButton { "Close" };
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
    void showAddMenu();
    void removeLane (int index);
    double laneLength() const;
};

} // namespace el
