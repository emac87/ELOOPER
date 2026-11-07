#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace el {

struct MidiNote {
    double  start = 0;     // beats from clip start
    double  length = 0.25; // beats
    uint8_t pitch = 60;
    uint8_t velocity = 100;
    uint8_t channel = 1;   // 1..16
    bool    muted = false;
    uint8_t ratchet = 1;   // 1..8: number of equal re-triggers spread over the note's length
};

// One step-sequencer lane: the note it plays and the name shown for it.
struct StepLane {
    uint8_t     pitch = 36;
    std::string name;      // empty = default name for the pitch
};

// Raw short MIDI message at a beat position.
struct MidiEvent {
    double  beat = 0;
    uint8_t data[3] = { 0, 0, 0 };
    uint8_t size = 3;
    bool isNoteOn()  const noexcept { return (data[0] & 0xF0) == 0x90 && data[2] > 0; }
    bool isNoteOff() const noexcept { return (data[0] & 0xF0) == 0x80 || ((data[0] & 0xF0) == 0x90 && data[2] == 0); }
};

// Editable clip: notes plus pass-through controller / pitch-bend events.
struct MidiClip {
    double lengthBeats = 16;
    std::vector<MidiNote>  notes;
    std::vector<MidiEvent> controllers;
    // Step-sequencer view settings (the steps are just notes).
    double stepBeats = 0.25;         // 1/16 notes
    std::vector<StepLane> stepLanes; // lane order + names, top to bottom (empty = automatic)
    std::string stepMapName;         // name of the lane preset in use (display only)

    void sortNotes();
    // Quantise note starts (and optionally ends) to `grid` beats with `strength` 0..1.
    void quantize (double grid, float strength = 1.0f, bool ends = false);
    // Build from raw events recorded relative to the loop start. Notes still held at the end
    // are closed at lengthBeats; events past the end wrap around.
    static MidiClip fromEvents (const std::vector<MidiEvent>& events, double lengthBeats);
    // Merge newly recorded raw events into this clip (overdub).
    void mergeEvents (const std::vector<MidiEvent>& events);
};

// Immutable, sorted playback form handed to the audio thread.
struct MidiSequence {
    double lengthBeats = 16;
    std::vector<MidiEvent> events;  // sorted; note-offs before note-ons at equal beats
    static MidiSequence fromClip (const MidiClip& clip);
};

} // namespace el
