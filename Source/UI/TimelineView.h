#pragma once
// Simple arrangement view: ruler, one row per track, loop clips placed on the song timeline.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "UI/Selection.h"

namespace el {

class TimelineView : public juce::Component,
                     private EngineController::Listener,
                     private juce::Timer {
public:
    TimelineView (EngineController&, Selection&);
    ~TimelineView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Drag { None, Move, Resize, Locate, LoopRegion };

    EngineController& controller;
    Selection& selection;

    juce::ToggleButton recordButton { "Arrangement record" }, followButton { "Follow" }, loopButton { "Loop region" };
    juce::TextButton zoomOut { "-" }, zoomIn { "+" };

    double pxPerBeat = 12.0;
    double scrollBeat = 0;
    int scrollRow = 0;
    bool follow = true;
    double playBeat = 0;
    bool playing = false;

    Drag drag = Drag::None;
    Id selectedClip = kInvalidId;
    Id dragClip = kInvalidId;
    double dragStart = 0, dragLen = 0, grabOffset = 0;
    Id dragTrack = kInvalidId;
    double regionA = 0, regionB = 0;

    void sessionChanged (Change, Id) override;
    void timerCallback() override;

    // geometry
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> rowsArea() const;
    float beatToX (double beat) const noexcept;
    double xToBeat (float x) const noexcept;
    int visibleRowCount() const;
    std::vector<TrackPtr> visibleTracks() const;
    int rowOfTrack (const std::vector<TrackPtr>&, Id trackId) const;
    juce::Rectangle<float> clipRect (const TimelineClip&, int row, double start, double len) const;
    double snapBeat (double beat, bool fine) const;
    double loopLengthOf (const TimelineClip&) const;
    const TimelineClip* findClip (Id id) const;
    int hitClip (juce::Point<int> p, const std::vector<TrackPtr>& tracks, bool& onRightEdge) const;
    void drawClip (juce::Graphics&, const TimelineClip&, juce::Rectangle<float>, double start, double len, bool selected);
    void zoomBy (double factor, float anchorX);
    void showClipMenu (Id clipId);
    void showAddMenu (Id trackId, double beat);
    void showRulerMenu();
};

} // namespace el
