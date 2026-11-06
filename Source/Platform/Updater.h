#pragma once
// Automatic self-updater (macOS only). Finds the newest successful GitHub Actions build of the
// update branch, downloads the app bundle, and swaps it in place via a detached shell script
// before relaunching. All network/file work runs on a background thread.
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>

namespace el {

class Updater {
public:
    // canRestartSilently is called on the message thread; true means nothing would be lost by
    // quitting right now (no recorded content, no plugins).
    explicit Updater (std::function<bool()> canRestartSilently);
    ~Updater();

    // Background check + download at launch. Silent on failure. No-op for dev builds, non-mac,
    // scan children, or when the "autoUpdate" setting is off.
    void checkOnLaunch();
    // From a menu item: reports "up to date" and errors to the user.
    void checkInteractive();

    // The "autoUpdate" setting (default true), read fresh from the ELooper settings file.
    // Writers must go through the app's own PropertiesFile (MainComponent does).
    static bool isAutoUpdateEnabled();
    static constexpr const char* autoUpdateKey = "autoUpdate";

private:
    struct Worker;
    struct ProgressWindow;
    struct Result;

    void start (bool interactive);
    void onFinished (std::shared_ptr<Result>);
    void showProgress (const juce::String& text);
    void closeProgress();
    void setProgress (double);
    void promptRestart (juce::File stagedApp, juce::File workDir);
    void install (const juce::File& stagedApp, const juce::File& workDir);

    std::function<bool()> canRestartSilently;
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };
    std::unique_ptr<Worker> worker;
    std::unique_ptr<ProgressWindow> progress;
    bool busy = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Updater)
};

} // namespace el
