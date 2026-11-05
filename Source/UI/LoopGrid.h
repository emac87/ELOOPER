#pragma once
// The main black loop screen: one column per track (header + a stack of loop cells + add
// buttons) and a big "+" column to add tracks.
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

    // Opens the "add track" menu (also used by the Track menu); anchored to the big "+" button.
    void showAddTrackMenu();

private:
    class Column;
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
    std::vector<std::unique_ptr<Column>> columns;
    juce::TextButton addTrackButton { "+" };

    bool needsRebuild = true;
    int tick = 0;
};

} // namespace el
