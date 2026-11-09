#pragma once
// Chimera's editor window (see ChimeraEditor.cpp): top bar (presets, macros, master, meter), layer
// tabs, a voice column, and three pages (sound / modulation / effects) built from ChimeraPanels.
#include <JuceHeader.h>
#include "Synth/plugin/ChimeraProcessor.h"
#include "Synth/ui/ChimeraWidgets.h"
#include "Synth/ui/ChimeraPanels.h"

namespace el::chimera {

class TopBar;
class LayerTabBar;

class ChimeraEditor : public juce::AudioProcessorEditor,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener,
                      private juce::Timer {
public:
    explicit ChimeraEditor (ChimeraProcessor&);
    ~ChimeraEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    enum Page { pageSound = 0, pageMod, pageFx, numPages };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { tick(); }
    void timerCallback() override { tick(); }
    void tick();
    void selectLayer (int layer);
    void selectPage (int page);
    void setEngine (Engine e);
    void toggleLayerPower (int layer);

    struct LayerClip {
        bool valid = false;
        LayerParams params;
        SamplePtr sample;
        std::shared_ptr<const std::vector<Dx7Patch>> cart;
        std::string cartName;
    };

    ChimeraProcessor& proc;
    Ctx ctx;
    LayerHooks hooks;
    LayerClip clip;
    Engine lastEngine[kMaxLayers] {};
    int page = pageSound;

    std::unique_ptr<TopBar> topBar;
    std::unique_ptr<LayerTabBar> layerTabs;
    std::unique_ptr<PillButton> pageButtons[numPages];

    std::unique_ptr<Panel> voice, engine, filter, env[3], lfo[2], globalLfo[2], matrix, fx[3];
    std::vector<Panel*> layerPanels;   // panels tinted with the selected layer's colour
    std::vector<Panel*> allPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChimeraEditor)
};

} // namespace el::chimera
