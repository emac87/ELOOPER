#pragma once
#include <JuceHeader.h>

namespace el {
// The icon macOS provides for an Audio Unit (mostly AUv3 app extensions), or an invalid image.
// Message thread. Always returns an invalid image on non-Apple platforms.
juce::Image loadAudioUnitIcon (const juce::PluginDescription&);
}
