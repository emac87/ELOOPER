#pragma once
#include <JuceHeader.h>

namespace el {

// "ELooper EQ": the 10-band graphic EQ as a built-in effect plug-in, added from the Pre FX / Post FX menus
// like any Audio Unit. It needs no scan and is saved with the project like any other plug-in.
namespace builtin {
    juce::PluginDescription eqDescription();
    bool isBuiltIn (const juce::PluginDescription&);
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&);
    juce::MemoryBlock eqState (const float* gainsDb);   // plug-in state for these band gains (dB)
}

} // namespace el
