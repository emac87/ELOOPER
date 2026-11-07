#pragma once
// FL-Studio style step sequencer ("channel rack") over an el::MidiClip owned by MidiEditor.
// Lanes (rows) can come from presets (drum maps for popular plug-ins, scales, the user's own
// saved presets), be renamed and re-pitched; each step has a velocity and a ratchet count.
#include <JuceHeader.h>
#include "Core/MidiClip.h"
#include "UI/StepPresets.h"
#include <functional>
#include <map>
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

    static juce::String rowName (int pitch);   // default lane name: GM drum name or note name

private:
    enum class Mode { None, Paint, Erase, Velocity };
    static constexpr int headerW = 260, rulerH = 34, rowH = 34;

    MidiClip* clip = nullptr;
    std::vector<int> rows;               // pitches, top to bottom
    std::vector<int> mutedRows;
    std::map<int, juce::String> laneNames;
    juce::Colour colour { 0xff40c4ff };
    int newVelocity = 100, newRatchet = 1;
    float cellW = 28.0f;
    float scrollX = 0, scrollY = 0;
    double playBeat = 0;
    bool playVisible = false;

    Mode mode = Mode::None;
    bool edited = false, dragged = false;
    int velRow = -1, velStep = -1, velStart = 0, velNow = 0;

    juce::TextButton lanesButton { "Lanes" }, ratchetButton;

    struct LaneClipboard { bool valid = false; std::vector<MidiNote> notes; };
    static LaneClipboard& clipboard();

    double stepLen() const { return clip ? juce::jmax (1.0e-3, clip->stepBeats) : 0.25; }
    int numSteps() const;
    int stepForStart (double start) const { return (int) std::floor (start / stepLen() + 0.5); }
    int playStep() const;
    juce::Rectangle<int> cellsArea() const { return { headerW, rulerH, juce::jmax (0, getWidth() - headerW), juce::jmax (0, getHeight() - rulerH) }; }
    juce::Rectangle<float> cellRect (int row, int step) const;
    bool cellAt (juce::Point<float> p, int& row, int& step) const;
    int rowAtY (float y) const;
    MidiNote* findStep (int pitch, int step);
    const MidiNote* findStep (int pitch, int step) const;
    bool hasStep (int pitch, int step) const { return findStep (pitch, step) != nullptr; }
    void addStep (int pitch, int step);
    void removeStep (int pitch, int step);
    void setStepVelocity (int pitch, int step, int v);
    int stepVelocity (int pitch, int step) const;
    bool isRowMuted (int pitch) const;
    bool customLanes() const { return clip != nullptr && ! clip->stepLanes.empty(); }
    juce::String laneName (int pitch) const;
    void insertRow (int pitch);
    void rowsFromLanes();
    void syncLanes();                    // rows + names -> clip->stepLanes
    void structureEdited();              // undo snapshot already taken: sync lanes, commit, repaint
    void clampScroll();
    void begin();
    void snapshot() { if (beginEdit) beginEdit(); }
    void commit();
    void applyPaint (int row, int step);
    void updateRatchetButton();

    // menus
    void showAddRowMenu();
    void showStepMenu (int row, int step);
    void showLaneMenu (int row);
    void showLanesMenu();

    // lane operations
    void toggleRowMute (int pitch);
    void removeRow (int pitch);
    void changeLaneNote (int row, int newPitch);
    void renameLane (int row);
    void moveLane (int row, int delta);
    void setLaneVelocity (int pitch, int mode, int value);   // mode 0 set, 1 ramp up, 2 ramp down, 3 accent beats, 4 humanize
    void setLaneRatchet (int pitch, int ratchet);
    void fillLane (int pitch, int every);
    void shiftLane (int pitch, int delta);
    void clearLane (int pitch);
    void copyLane (int pitch);
    void pasteLane (int pitch);
    void transposeAll (int semitones);
    void applyLanes (const std::vector<steppresets::Lane>& lanes, const juce::String& mapName, bool snapshot = true);
    void resetLanes();
    void clearAllSteps();
    void saveUserPreset (bool withPattern);
    void loadUserPreset (const juce::File& f, bool withPattern);
    void repaintColumn (int step);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepSequencer)
};

} // namespace el
