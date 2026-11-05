#pragma once
// FL-Studio style step sequencer ("channel rack") over an el::MidiClip owned by MidiEditor.
#include <JuceHeader.h>
#include "Core/MidiClip.h"
#include <functional>
#include <vector>

namespace el {

class StepSequencer : public juce::Component
{
public:
    StepSequencer();

    std::function<void()> beginEdit;    // before first modification of a gesture (undo snapshot)
    std::function<void()> commitEdit;   // gesture finished: send clip to the engine

    // resetRows: rebuild the row list from the clip (new clip loaded); otherwise keep the rows
    // and only append pitches that appeared in the clip.
    void setClip (MidiClip* c, bool resetRows);
    void refreshRows();
    void stepLengthChanged();
    void setLoopColour (juce::Colour c);
    void setNewVelocity (int v) { newVelocity = juce::jlimit (1, 127, v); }
    void setPlayhead (double beat, bool visible);
    void zoom (float factor);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override;

    static juce::String rowName (int pitch);

private:
    enum class Mode { None, Paint, Erase, Velocity };
    static constexpr int headerW = 170, rulerH = 18, rowH = 28;

    MidiClip* clip = nullptr;
    std::vector<int> rows;               // pitches, high to low
    std::vector<int> mutedRows;
    juce::Colour colour { 0xff40c4ff };
    int newVelocity = 100;
    float cellW = 28.0f;
    float scrollX = 0, scrollY = 0;
    double playBeat = 0;
    bool playVisible = false;

    Mode mode = Mode::None;
    bool edited = false, dragged = false;
    int velRow = -1, velStep = -1, velStart = 0, velNow = 0;

    double stepLen() const { return clip ? juce::jmax (1.0e-3, clip->stepBeats) : 0.25; }
    int numSteps() const;
    int stepForStart (double start) const { return (int) std::floor (start / stepLen() + 0.5); }
    int playStep() const;
    juce::Rectangle<int> cellsArea() const { return { headerW, rulerH, juce::jmax (0, getWidth() - headerW), juce::jmax (0, getHeight() - rulerH) }; }
    juce::Rectangle<float> cellRect (int row, int step) const;
    bool cellAt (juce::Point<float> p, int& row, int& step) const;
    int rowAtY (float y) const;
    bool hasStep (int pitch, int step) const;
    void addStep (int pitch, int step);
    void removeStep (int pitch, int step);
    void setStepVelocity (int pitch, int step, int v);
    int stepVelocity (int pitch, int step) const;
    bool isRowMuted (int pitch) const;
    void insertRow (int pitch);
    void clampScroll();
    void begin();
    void commit();
    void applyPaint (int row, int step);
    void showAddRowMenu();
    void showVelocityMenu (int row, int step);
    void toggleRowMute (int pitch);
    void removeRow (int pitch);
    void repaintColumn (int step);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepSequencer)
};

} // namespace el
