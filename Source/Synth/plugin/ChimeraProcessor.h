#pragma once
// Chimera as a hosted instrument: a juce::AudioPluginInstance that ELooper creates directly (no
// scanning), with 4 stereo outputs (multi-out), 21 automatable parameters (4 macros, master, and
// level / cutoff / resonance / pan per layer), patch state, sample and DX7 cart loading, factory
// and user presets.
//
// Threading: everything here is message-thread API except processBlock. Edits go through
// editPatch(), which copies the working patch and hands the copy to the audio thread.
#include <JuceHeader.h>
#include "Synth/dsp/Synth.h"

namespace el::chimera {

class ChimeraProcessor : public juce::AudioPluginInstance, public juce::ChangeBroadcaster {
public:
    static constexpr const char* kFormatName = "ELooper";
    static constexpr const char* kIdentifier = "builtin:chimera";
    static juce::PluginDescription makeDescription();

    ChimeraProcessor();
    ~ChimeraProcessor() override;

    // ---- patch editing (message thread) -------------------------------------------------------
    const Patch& patch() const noexcept { return working; }
    // Apply a change to the working patch and publish it to the audio thread. Also syncs the
    // automatable parameters from the patch (level / cutoff / resonance / pan / master), so set
    // those fields in `fn` as usual. Sends a change message (editors refresh).
    void editPatch (const std::function<void (Patch&)>& fn);
    void setPatch (const Patch& p);                        // replace everything (preset load)

    juce::String loadSample (int layer, const juce::File& file);        // "" = ok, else the error
    juce::String loadCart (int layer, const juce::File& file);          // DX7 .syx (32 voices or 1)
    void selectCartVoice (int layer, int index);                         // copies it into the layer's FM voice
    juce::String saveCart (int layer, const juce::File& file);           // writes the layer's cart (with its current voice)

    // Factory and user presets (user presets are .chimera files in presetFolder()).
    static int numFactoryPresets();
    static juce::String factoryPresetName (int i);
    static juce::String factoryPresetCategory (int i);
    void loadFactoryPreset (int i);
    static juce::File presetFolder();
    juce::String savePreset (const juce::File& file);
    juce::String loadPreset (const juce::File& file);

    // Parameters the host can automate (index order: macro1..4, master, then per layer
    // level, cutoff, resonance, pan).
    juce::AudioParameterFloat* macroParam (int i) const noexcept { return macros[(size_t) i]; }
    juce::AudioParameterFloat* masterParam() const noexcept { return master; }
    juce::AudioParameterFloat* levelParam (int layer) const noexcept { return level[(size_t) layer]; }
    juce::AudioParameterFloat* cutoffParam (int layer) const noexcept { return cutoff[(size_t) layer]; }
    juce::AudioParameterFloat* resonanceParam (int layer) const noexcept { return resonance[(size_t) layer]; }
    juce::AudioParameterFloat* panParam (int layer) const noexcept { return pan[(size_t) layer]; }

    int activeVoices() const noexcept { return synth.activeVoiceCount(); }
    float outputPeak() const noexcept { return synth.peakLevel(); }

    // ---- AudioProcessor ---------------------------------------------------------------------
    const juce::String getName() const override { return "Chimera"; }
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioPluginInstance::processBlock;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    double getTailLengthSeconds() const override { return 4.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return numFactoryPresets(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override { loadFactoryPreset (index); }
    const juce::String getProgramName (int index) override { return factoryPresetName (index); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int size) override;
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = makeDescription(); }

private:
    void publish();
    void syncParamsFromPatch();
    void syncPatchFromParams();   // host automation moved a parameter: reflect it in `working`

    Synth synth;
    Patch working;
    int currentProgram = 0;
    std::array<juce::AudioParameterFloat*, kMacros> macros {};
    juce::AudioParameterFloat* master = nullptr;
    std::array<juce::AudioParameterFloat*, kMaxLayers> level {}, cutoff {}, resonance {}, pan {};
    std::vector<MidiEvent> midiScratch;
    std::vector<std::vector<Dx7Patch>> cartStore;   // keeps loaded carts' storage stable (not required, tidy)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChimeraProcessor)
};

// Patch <-> XML (used for plug-in state and .chimera preset files). Samples are stored by path
// (reloaded on load; a missing file leaves the layer's sample empty), DX7 voices as VCED hex, and
// loaded carts embedded (4 KB each).
std::unique_ptr<juce::XmlElement> patchToXml (const Patch&);
Patch patchFromXml (const juce::XmlElement&, juce::StringArray* warnings = nullptr);
SamplePtr loadSampleFile (const juce::File&, juce::String& error);   // decodes with JUCE's formats
std::vector<Patch> makeFactoryPresets();

} // namespace el::chimera
