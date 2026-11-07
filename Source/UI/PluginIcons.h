#pragma once
// Plugin / channel icons for track headers: generic single-colour vector icons chosen from the
// plugin's type (instrument, MIDI effect, audio effect) and a guess from its category and name.
#include <JuceHeader.h>
#include "Model/Session.h"
#include <memory>
#include <optional>
#include <vector>

namespace el::icons {

enum class Kind {
    // instruments
    Synth, Piano, Drums, Sampler, Bass, Strings, Organ,
    // effects
    Reverb, Delay, Eq, Compressor, Distortion, Modulation, Effect, MidiFx,
    // channel defaults
    Audio, Midi, Bus, Master, AuxOut
};

const std::vector<Kind>& allKinds();
juce::String kindName (Kind);                       // display name, also used in "builtin:<name>"
std::optional<Kind> kindFromName (const juce::String&);

// White line-art on a 24x24 grid so it can be tinted by the caller.
std::unique_ptr<juce::Drawable> makeDrawable (Kind);

Kind guessKind (const juce::PluginDescription&);
Kind channelKind (TrackKind);
Kind slotKind (const TrackModel&, const PluginSlot&);   // MidiFx for MIDI effects, else guessKind

// Plugin shown for a track: instrument, else first pre insert, else first post insert,
// else first MIDI effect, else null.
PluginSlotPtr iconSlot (const TrackModel&);

// The icon for a track: its iconSlot's generic type icon, or the channel-type icon.
Kind trackKind (const TrackModel&);

void drawIcon (juce::Graphics&, juce::Rectangle<float>, Kind, juce::Colour tint);


} // namespace el::icons
