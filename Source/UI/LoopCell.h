#pragma once
// One loop on the loop screen: rounded square in the loop colour with a progress ring.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/MidiMapping.h"
#include "UI/Selection.h"
#include <functional>

namespace el {

class LoopCell : public juce::Component {
public:
    LoopCell (EngineController&, Selection&, MidiMapping&, Id trackId, Id loopId);

    Id getLoopId() const noexcept { return loopId; }
    Id getTrackId() const noexcept { return trackId; }

    void refresh();                 // re-read name / colour / type (call on Loop / Track changes)
    void poll (bool blinkOn);       // 30 Hz: repaints only if the status changed

    // (trackId, loopId, midiEditor): open the MIDI editor (true) or the automation panel (false).
    std::function<void (Id, Id, bool)> onEdit;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void showMenu();
    float ringFraction (const LoopStatus&) const;
    void updateTexts();

    EngineController& controller;
    Selection& selection;
    MidiMapping& midiMap;
    const Id trackId, loopId;

    LoopType type = LoopType::Audio;
    juce::Colour colour;
    juce::String nameText, takeText, lenText;
    LoopStatus status;
    int ringStep = 0;
    bool blink = false;
    bool selected = false;

    juce::Path glyph;           // large type glyph (built in resized)
    juce::Path smallGlyph;      // small type glyph (top-left)
    juce::Path arcPath;         // reused for the progress ring
};

} // namespace el
