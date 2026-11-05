#pragma once
// Plugin presets (state of one plugin) and channel presets (a whole channel strip: mixer + plugin chain).
// Stored as XML files under PluginHost::appDataDir()/Presets. Message thread only.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include <functional>

namespace el {

class PresetManager {
public:
    explicit PresetManager (EngineController& c) : controller (c) {}

    // ---- plugin presets -------------------------------------------------------------------
    bool savePluginPreset (Id slotId, const juce::String& presetName);
    bool loadPluginPreset (Id slotId, const juce::File& file);
    juce::Array<juce::File> pluginPresets (const juce::PluginDescription& desc) const;

    // ---- channel presets ------------------------------------------------------------------
    bool saveChannelPreset (Id trackId, const juce::String& name);
    void loadChannelPreset (Id trackId, const juce::File& file, std::function<void (juce::String errors)> done = {});
    juce::Array<juce::File> channelPresets() const;

    // ---- UI helpers (async popup menus, no modal loops) -------------------------------------
    void showPluginPresetMenu (Id slotId, juce::Component* target);
    void showChannelPresetMenu (Id trackId, juce::Component* target);

    // ---- (de)serialisation, shared with SessionSerializer ---------------------------------
    static juce::ValueTree pluginPresetDataToTree (const PluginPresetData& d);
    static PluginPresetData pluginPresetDataFromTree (const juce::ValueTree& t);
    static juce::ValueTree channelPresetToTree (const ChannelPreset& p);
    static ChannelPreset channelPresetFromTree (const juce::ValueTree& t);

    // Plugin description <-> XML (shared with SessionSerializer).
    static std::unique_ptr<juce::XmlElement> descriptionToXml (const juce::PluginDescription& d);
    static juce::PluginDescription descriptionFromXml (const juce::XmlElement& e);

    static juce::File pluginPresetDir (const juce::PluginDescription& desc);
    static juce::File channelPresetDir();
    static juce::String sanitise (const juce::String& name);

private:
    EngineController& controller;
    JUCE_DECLARE_NON_COPYABLE (PresetManager)
};

} // namespace el
