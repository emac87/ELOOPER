// ELooper application entry point.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/MidiMapping.h"
#include "Engine/PresetManager.h"
#include "UI/MainComponent.h"
#include "UI/Theme.h"

class ELooperApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        controller = std::make_unique<el::EngineController>();   // opens the audio device
        controller->newSession();
        midiMapping = std::make_unique<el::MidiMapping> (*controller);
        presets = std::make_unique<el::PresetManager> (*controller);
        window = std::make_unique<MainWindow> (getApplicationName(), *controller, *midiMapping, *presets);
    }

    void shutdown() override
    {
        window.reset();
        presets.reset();
        midiMapping.reset();
        controller.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow (const juce::String& name, el::EngineController& c, el::MidiMapping& mm, el::PresetManager& p)
            : juce::DocumentWindow (name, el::theme::background, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new el::MainComponent (c, mm, p), true);
            setResizable (true, true);
            setResizeLimits (900, 600, 8192, 8192);
            centreWithSize (1280, 800);
            setVisible (true);
            if (auto* content = getContentComponent()) content->grabKeyboardFocus();
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<el::EngineController> controller;
    std::unique_ptr<el::MidiMapping> midiMapping;
    std::unique_ptr<el::PresetManager> presets;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (ELooperApp)
