#pragma once
// The main black loop screen: one row per track (a compact header strip on the left, its loops
// to the right, then a "+" to add a loop of the chosen type), and "+ Add Track" at the bottom.
// Multi-out instruments show their outputs and sum folder indented under them (collapsible).
// Plain buses live on the mixer only (they appear here only if they hold loops).
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/MidiMapping.h"
#include "Engine/PresetManager.h"
#include "UI/Selection.h"
#include "UI/TrackHeader.h"
#include "UI/LoopCell.h"
#include <functional>
#include <memory>
#include <vector>

namespace el {

class LoopGrid : public juce::Component,
                 private juce::Timer,
                 private EngineController::Listener,
                 private juce::ChangeListener {
public:
    LoopGrid (EngineController&, Selection&, MidiMapping&, PresetManager&);
    ~LoopGrid() override;

    // (trackId, loopId, midiEditor): double-click on a MIDI / automation loop, or the "Edit" menu items.
    std::function<void (Id, Id, bool)> onEditLoop;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Opens the "add track" menu (also used by the Track menu); anchored to the "+ Add Track" button.
    void showAddTrackMenu();

private:
    class Row;
    struct Content : public juce::Component { void paint (juce::Graphics&) override {} };

    void timerCallback() override;
    void sessionChanged (Change what, Id id) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuild();
    void layoutContent();
    void reorderDrop (Id trackId, const juce::MouseEvent&);

    EngineController& controller;
    Selection& selection;
    MidiMapping& midiMap;
    PresetManager& presets;

    juce::Viewport viewport;
    Content content;
    std::vector<std::unique_ptr<Row>> rows;
    juce::TextButton addTrackButton { "+ Add Track" };

    bool needsRebuild = true;
    int tick = 0;
};

} // namespace el
