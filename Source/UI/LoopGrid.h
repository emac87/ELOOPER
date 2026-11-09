#pragma once
// The main black loop screen. Each track is a column: its label is pinned along the bottom of
// the screen and its loops are stacked above it (loop 1 at the top), with "+" under the last
// loop. Scenes run down the left side: Scene N plays row N (the N-th loop of every track) and
// stops the rest; "Stop all" sits in the bottom-left corner beside the labels. A multi-out
// instrument's outputs and sum folder are columns right after it (foldable). Plain buses are on
// the mixer only. The labels and scene buttons scroll together with the loops.
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

    // (trackId, loopId, midiEditor): double-click on a MIDI loop, or the "Edit" menu items.
    std::function<void (Id, Id, bool)> onEditLoop;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showAddTrackMenu();

private:
    class Column;
    class SceneButtons;
    class SyncViewport;
    struct Content : public juce::Component { void paint (juce::Graphics&) override {} };

    void timerCallback() override;
    void sessionChanged (Change what, Id id) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuild();
    void layoutContent();
    void syncFrom (juce::Viewport* source);
    void reorderDrop (Id trackId, const juce::MouseEvent&);
    void updateSceneLight();
    void learnMenu (MappingTarget::Kind, int scene, juce::Component* target);

    EngineController& controller;
    Selection& selection;
    MidiMapping& midiMap;
    PresetManager& presets;

    std::unique_ptr<SyncViewport> gridView, labelView, sceneView;
    Content gridContent, labelContent, sceneContent;
    std::vector<std::unique_ptr<Column>> columns;
    std::vector<std::unique_ptr<TrackHeader>> labels;
    std::unique_ptr<SceneButtons> scenes;
    juce::TextButton addTrackButton { "+ Track" };
    struct ClickButton : public juce::TextButton {
        using juce::TextButton::TextButton;
        std::function<void (bool popup)> onAnyClick;
        void clicked (const juce::ModifierKeys& m) override { if (onAnyClick) onAnyClick (m.isPopupMenu()); }
    };
    ClickButton stopAllButton { "Stop all" };

    bool needsRebuild = true, syncing = false;
    int tick = 0;
};

} // namespace el
