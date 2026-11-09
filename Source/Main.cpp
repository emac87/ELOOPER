// ELooper application entry point.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/MidiMapping.h"
#include "Engine/PresetManager.h"
#include "UI/SettingsWindow.h"
#include "UI/MainComponent.h"
#include "UI/Theme.h"
#include "Platform/Permissions.h"
#include "Platform/Updater.h"

class ELooperApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // Plugin-scan helper processes are extra instances of this app.
    // Plugin-scan helpers and a just-updated copy (which starts while the old one quits)
    // are allowed to run alongside another instance.
    bool moreThanOneInstanceAllowed() override
    {
        return isScanChild() || getCommandLineParameterArray().contains ("--elooper-updated");
    }

    void initialise (const juce::String&) override
    {
        if (isScanChild()) { runScanChild(); return; }

        // Ask for microphone access once, up front; only then open audio devices.
        el::requestMicrophoneAccess ([this] (bool granted) {
            controller = std::make_unique<el::EngineController>();   // opens the audio device
            midiMapping = std::make_unique<el::MidiMapping> (*controller);
            presets = std::make_unique<el::PresetManager> (*controller);
            window = std::make_unique<MainWindow> (getApplicationName(), *controller, *midiMapping, *presets);
            startUpdater();
            if (! granted)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Microphone access is off",
                    "ELooper can play back, but it can't record from your audio interface.\n\n"
                    "Turn it on in System Settings > Privacy & Security > Microphone > ELooper, then restart ELooper.");
        });
    }

    void shutdown() override
    {
        if (scanThread != nullptr) { scanThread->stopThread (2000); return; }
        el::closeSettingsWindow();
        updater.reset();
        window.reset();
        presets.reset();
        midiMapping.reset();
        controller.reset();
    }

    // Cmd-Q, the window's close button and OS shutdown all land here: ask about unsaved work first.
    void systemRequestedQuit() override
    {
        if (window != nullptr)
            if (auto* mc = dynamic_cast<el::MainComponent*> (window->getContentComponent()))
            {
                mc->runIfSafeToDiscard ([] { juce::JUCEApplication::quit(); });
                return;
            }
        quit();
    }

private:
    static bool isScanChild() { return getCommandLineParameterArray().contains ("--elooper-scan"); }

    // Scans one plugin on a background thread (AUv3 needs the message loop free), then quits
    // with the scan's exit code. If the plugin crashes, only this process dies.
    void runScanChild()
    {
       #if JUCE_MAC
        juce::Process::setDockIconVisible (false);
       #endif
        struct ScanThread : juce::Thread {
            ScanThread() : juce::Thread ("scan") {}
            void run() override {
                const int code = el::PluginHost::runScanChild (getCommandLineParameterArray());
                juce::MessageManager::callAsync ([code] {
                    juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (code);
                    juce::JUCEApplicationBase::quit();
                });
            }
        };
        scanThread = std::make_unique<ScanThread>();
        scanThread->startThread();
    }

    std::unique_ptr<juce::Thread> scanThread;

    // True when quitting would lose nothing: no recorded loop content and no plugins loaded.
    bool nothingToLose() const
    {
        if (controller == nullptr) return true;
        if (window != nullptr)
            if (auto* mc = dynamic_cast<el::MainComponent*> (window->getContentComponent()))
                if (mc->isDirty()) return false;   // unsaved changes: never restart for an update silently
        for (auto& t : controller->session().tracks) {
            if (! t->allPlugins().empty()) return false;
            for (auto& l : t->loops)
                if (l->rt != nullptr && l->rt->takeCount() > 0) return false;
        }
        return true;
    }

    void startUpdater()
    {
        updater = std::make_unique<el::Updater> ([this] { return nothingToLose(); });
        if (auto* mc = dynamic_cast<el::MainComponent*> (window->getContentComponent()))
            mc->onCheckForUpdates = [this] { if (updater != nullptr) updater->checkInteractive(); };
        updater->checkOnLaunch();
    }

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
    std::unique_ptr<el::Updater> updater;
};

START_JUCE_APPLICATION (ELooperApp)
