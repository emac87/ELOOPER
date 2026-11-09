#pragma once
// One automation lane body: grid, breakpoint curve, editable points.
#include <JuceHeader.h>
#include "Engine/EngineController.h"

namespace el {

class AutomationLaneView : public juce::Component {
public:
    AutomationLaneView (EngineController& c, AutomationOwner owner, int laneIndex);

    // Replace the displayed lane (ignored while the user is dragging).
    void setLane (const AutomationLane& l);
    const AutomationLane& getLane() const noexcept { return lane; }
    void setLengthBeats (double b);
    void setBeatsPerBar (int bpb);
    void setColour (juce::Colour c) { colour = c; repaint(); }
    void setPencil (bool on) { pencil = on; }
    void setEraser (bool on) { eraser = on; }
    void setPlayhead (double beat, bool visible);
    bool isInteracting() const noexcept { return mode != Mode::None; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Mode { None, Point, Curve, Pencil, Band, Erase };

    EngineController& controller;
    AutomationOwner owner;
    int laneIndex;
    AutomationLane lane;
    double lengthBeats = 16;
    int beatsPerBar = 4;
    juce::Colour colour { theme_accent() };
    bool pencil = false;
    bool eraser = false;
    double eraseFrom = 0;
    double playBeat = 0;
    bool playVisible = false;
    juce::Path curvePath, curveFill;   // cached; rebuilt only when pathDirty
    bool pathDirty = true;
    void rebuildPath();
    void invalidateCurve() { pathDirty = true; repaint(); }
    int playheadX() const noexcept;
    void repaintPlayheadAt (int x);

    Mode mode = Mode::None;
    int dragIndex = -1;
    float curveStartValue = 0;
    float curveOrig = 0;
    int curveIndex = -1;
    juce::Point<int> bandStart, bandNow;
    std::vector<int> selected;

    static juce::Colour theme_accent();
    double xToBeat (float x) const noexcept;
    float beatToX (double b) const noexcept;
    float yToValue (float y) const noexcept;
    float valueToY (float v) const noexcept;
    double snap (double beat, bool fine) const noexcept;
    int hitPoint (juce::Point<float> p) const;
    int segmentAt (double beat) const;
    void commit();
    void showPointMenu (int index);
    void deleteSelected();
    bool isSelected (int i) const;
};

} // namespace el
