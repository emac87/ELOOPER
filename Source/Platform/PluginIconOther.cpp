#include "PluginIconMac.h"
#if ! defined (__APPLE__)
namespace el {
juce::Image loadAudioUnitIcon (const juce::PluginDescription&) { return {}; }
}
#endif
