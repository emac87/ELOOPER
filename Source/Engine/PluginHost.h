#pragma once
// Plugin discovery and instantiation (AU on macOS, VST3 everywhere). Message thread only.
#include <JuceHeader.h>
#include "Model/Session.h"
#include <functional>
#include <map>

namespace el {

class PluginHost {
public:
    PluginHost();
    ~PluginHost();

    juce::AudioPluginFormatManager& formats() noexcept { return formatManager; }
    juce::KnownPluginList& knownPlugins() noexcept { return knownList; }

    // Scans all default AU/VST3 locations on a background thread. The list is saved to
    // appDataDir()/plugins.xml after each plugin, and crashing plugins are blacklisted via a
    // dead-man's-pedal file. Callbacks are delivered on the message thread.
    void startScan (std::function<void (float progress, const juce::String& currentPlugin)> onProgress,
                    std::function<void()> onFinished);
    void cancelScan();
    bool isScanning() const noexcept;

    void loadKnownList();
    void saveKnownList();

    // Sorted, filtered views for menus.
    std::vector<juce::PluginDescription> instruments() const;
    std::vector<juce::PluginDescription> effects() const;      // audio effects
    std::vector<juce::PluginDescription> midiEffects() const;

    using CreateCallback = std::function<void (std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error)>;
    // Asynchronous instantiation (AU v3 requires async). Callback on the message thread.
    void createInstance (const juce::PluginDescription& desc, double sampleRate, int blockSize, CreateCallback cb);

    // Tries to enable every output bus as stereo (multi-out instruments). Returns total output channels.
    static int enableMultiOut (juce::AudioPluginInstance& p);

    // Plugin editor windows (one per slot, floating, always-on-top optional).
    void showEditor (const PluginSlotPtr& slot);
    void closeEditor (Id slotId);
    void closeAllEditors();

    static juce::File appDataDir();

    // Entry point for the out-of-process scanner child (see Main.cpp). Returns the exit code.
    // Must run on a background thread of the child process while its message loop runs.
    static int runScanChild (const juce::StringArray& commandLineArgs);   // ~/Library/Application Support/ELooper (created if missing)

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownList;
};

} // namespace el
