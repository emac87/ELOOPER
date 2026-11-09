// Entry point for the stand-alone Chimera builds (AU, VST3 and Standalone app). Inside ELooper,
// Chimera is created directly by PluginHost instead.
#include "Synth/plugin/ChimeraProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new el::chimera::ChimeraProcessor();
}
