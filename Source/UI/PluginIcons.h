#pragma once
// Plugin / channel icons for track headers: real AU icons when macOS provides one, otherwise
// built-in single-colour vector icons chosen by guessing from the plugin's category and name.
// User overrides are remembered per plugin (or per track kind when the track has no plugin).
#include <JuceHeader.h>
#include "Model/Session.h"
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace el::icons {

enum class Kind {
    // instruments
    Synth, Piano, Drums, Sampler, Bass, Strings, Organ,
    // effects
    Reverb, Delay, Eq, Compressor, Distortion, Modulation, Effect,
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

// Plugin shown for a track: instrument, else first pre insert, else first post insert, else null.
PluginSlotPtr iconSlot (const TrackModel&);

class IconCache {
public:
    static IconCache& get();   // message thread only

    juce::Image realIcon (const juce::PluginDescription&);   // cached; invalid if none

    // Override value: "builtin:<kindName>" or an absolute image path. Absent = automatic.
    std::optional<juce::String> override (const juce::String& key);
    void setOverride (const juce::String& key, const juce::String& value);   // empty value = automatic

    static juce::String overrideKey (const TrackModel&);     // plugin identifier, or "track:<kind>"
    juce::File importImage (const juce::File& source) const; // copies into appDataDir()/Icons

    void drawIcon (juce::Graphics&, juce::Rectangle<float>, const TrackModel&, juce::Colour tint);

private:
    IconCache() = default;
    void loadOverrides();
    void saveOverrides() const;
    juce::Image fileImage (const juce::String& path);

    bool loaded = false;
    std::map<juce::String, juce::String> overrides;
    std::map<juce::String, juce::Image> real, files;
};

} // namespace el::icons
