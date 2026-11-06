#pragma once
// Piano-roll editor over an el::MidiClip owned by MidiEditor.
#include <JuceHeader.h>
#include "Core/MidiClip.h"
#include <functional>

namespace el {

inline juce::String midiNoteName (int pitch)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[pitch % 12]) + juce::String (pitch / 12 - 2);   // 60 = C3
}

inline bool isBlackKey (int pitch)
{
    switch (pitch % 12) { case 1: case 3: case 6: case 8: case 10: return true; default: return false; }
}

class PianoRoll : public juce::Component
{
public:
    PianoRoll();

    // Host wiring: beginEdit() is called before the first modification of a gesture (undo
    // snapshot); commitEdit() after the gesture ended and the clip should be sent to the engine.
    std::function<void()> beginEdit;
    std::function<void()> commitEdit;

    void setClip (MidiClip* c);
    void clipReplaced();                       // contents changed from outside: drop selection
    void setLoopColour (juce::Colour c);
    void setBeatsPerBar (int b);
    void setGrid (double beats, bool snapEnabled);
    void setNewVelocity (int v) { newVelocity = juce::jlimit (1, 127, v); }
    void setPlayhead (double beat, bool visible);
    void zoom (float factor);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Mode { None, Key, Move, Resize, Rubber, Velocity };

    static constexpr int kbW = 72, rulerH = 26, velH = 84;

    MidiClip* clip = nullptr;
    std::vector<char> sel;
    juce::Colour colour { 0xff40c4ff };
    int beatsPerBar = 4;
    double gridBeats = 0.25;
    bool snap = true;
    int newVelocity = 100;

    float ppb = 60.0f, keyH = 20.0f;
    float scrollX = 0, scrollY = 0;
    bool centred = false;

    double playBeat = 0;
    bool playVisible = false;

    // gesture state
    Mode mode = Mode::None;
    bool edited = false;
    std::vector<MidiNote> orig;
    double downBeat = 0;
    int downPitch = 0;
    int anchorIdx = -1;
    int reselectIdx = -1;
    bool dragged = false;
    juce::Point<float> rubberStart;
    juce::Rectangle<float> rubber;
    std::vector<char> baseSel;
    int heldKey = -1;
    int velIdx = -1;

    juce::Rectangle<int> gridArea() const { return { kbW, rulerH, juce::jmax (0, getWidth() - kbW), juce::jmax (0, getHeight() - rulerH - velH) }; }
    juce::Rectangle<int> velArea() const { return { kbW, juce::jmax (0, getHeight() - velH), juce::jmax (0, getWidth() - kbW), velH }; }
    double xToBeat (float x) const { return (x - kbW + scrollX) / ppb; }
    float beatToX (double b) const { return (float) (kbW + b * ppb - scrollX); }
    int yToPitch (float y) const;
    float pitchToY (int p) const { return rulerH + (127 - p) * keyH - scrollY; }
    juce::Rectangle<float> noteRect (const MidiNote&) const;
    int hitNote (juce::Point<float>) const;
    bool onRightEdge (const juce::Rectangle<float>&, juce::Point<float>) const;
    void clampScroll();
    void ensureSel();
    void selectOnly (int idx);
    int numSelected() const;
    double snapDelta (double d) const;
    double snapDown (double b) const;
    void begin();
    void commit();
    void deleteSelected();
    void duplicateSelectionAfter();
    void moveSelection (double dBeats, int dPitch);
    void quantizeSelection();
    void setVelocitySelected (int v);
    void showNoteMenu();
    void paintGrid (juce::Graphics&, juce::Rectangle<int>);
    void paintNotes (juce::Graphics&, juce::Rectangle<int> clipB);
    void paintKeyboard (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintVelocity (juce::Graphics&);
    void repaintPlayhead (double beat);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};

} // namespace el
