#pragma once
// MIDI editing panel: header bar + piano roll / step sequencer over the selected MIDI loop.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "UI/Selection.h"
#include "UI/PianoRoll.h"
#include "UI/StepSequencer.h"

namespace el {

class MidiEditor : public juce::Component,
                   private EngineController::Listener,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    MidiEditor (EngineController&, Selection&);
    ~MidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    static constexpr int headerH = 44;

    void sessionChanged (Change what, Id id) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void selectionChanged();
    void reloadClip (bool force, bool resetRows);
    static bool sameClip (const MidiClip&, const MidiClip&);
    void pushUndo();
    void commitClip();
    void undo();
    void redo();
    void clipEditedExternally();   // after undo/redo: refresh both editors
    void setTab (int t);
    void syncHeader();
    void userChangedGrid();
    double currentGridBeats() const;
    void changeLength (int direction);
    void quantizeClip();
    void updateVisibility();

    EngineController& controller;
    Selection& selection;
    Id loopId = kInvalidId;
    MidiClip clip;
    std::vector<MidiClip> undoStack, redoStack;
    bool committing = false;

    int tab = 0;                 // 0 piano roll, 1 step sequencer
    int divs[2] { 16, 16 };
    bool trips[2] { false, false };
    bool snapOn = true;

    juce::TextButton pianoTab { "Piano Roll" }, stepTab { "Step Seq" };
    juce::ComboBox gridBox;
    juce::ToggleButton tripletToggle { "Triplet" }, snapToggle { "Snap" };
    juce::TextButton quantizeButton { "Quantize" };
    juce::TextButton lenMinus { "-" }, lenPlus { "+" };
    juce::Label lenLabel;
    juce::Slider velSlider;
    juce::Label velLabel;
    juce::TextButton zoomOut { "-" }, zoomIn { "+" };

    PianoRoll piano;
    StepSequencer steps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiEditor)
};

} // namespace el
