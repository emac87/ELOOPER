#pragma once
// Step-sequencer lane presets: built-in drum maps for popular drum plug-ins, scales, and the
// user's own saved presets (lanes, optionally with the step pattern) in appDataDir()/StepPresets.
#include <JuceHeader.h>
#include "Core/MidiClip.h"
#include <optional>
#include <vector>

namespace el::steppresets {

struct Lane { int pitch = 36; juce::String name; };

struct Preset {
    juce::String name;
    juce::String note;          // short hint shown in the menu (empty = none)
    std::vector<Lane> lanes;    // top to bottom
};

struct Group { juce::String name; std::vector<Preset> presets; };

// Built-in drum maps, grouped by maker / family.
const std::vector<Group>& drumMaps();

struct Scale { juce::String name; std::vector<int> intervals; };   // semitones above the root
const std::vector<Scale>& scales();

// Lanes for one octave of `scale` from `rootPitch` up to the octave, highest at the top.
Preset makeScale (const Scale& scale, int rootPitch);

// General MIDI drum name for a pitch, or empty.
juce::String gmDrumName (int pitch);

// ---- user presets ---------------------------------------------------------------------------
struct UserPreset {
    juce::String name;
    std::vector<Lane> lanes;
    bool hasPattern = false;
    double stepBeats = 0.25, lengthBeats = 16;
    std::vector<MidiNote> notes;
};

juce::File userDir();                         // created if missing
juce::Array<juce::File> userPresetFiles();    // sorted by name
bool saveUserPreset (const juce::String& name, const std::vector<Lane>& lanes, const MidiClip* patternOrNull);
std::optional<UserPreset> loadUserPreset (const juce::File&);

} // namespace el::steppresets
