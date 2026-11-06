#include "Platform/Updater.h"
#include "UI/Theme.h"
#include "UI/LookAndFeel.h"
#include "Engine/PluginHost.h"

#if ! JUCE_WINDOWS
 #include <unistd.h>
#endif

#ifndef ELOOPER_BUILD_SHA
 #define ELOOPER_BUILD_SHA "dev"
#endif
#ifndef ELOOPER_REPO
 #define ELOOPER_REPO "emac87/ELOOPER"
#endif
#ifndef ELOOPER_UPDATE_BRANCH
 #define ELOOPER_UPDATE_BRANCH "elooper-initial"
#endif

namespace el {

namespace {
    constexpr int apiTimeoutMs = 8000;
    constexpr int downloadTimeoutMs = 120000;

    bool platformSupported()
    {
       #if JUCE_MAC
        return true;
       #else
        return false;
       #endif
    }

    bool isScanChild()
    {
        return juce::JUCEApplicationBase::getCommandLineParameterArray().contains ("--elooper-scan");
    }

    bool isDevBuild() { return juce::String (ELOOPER_BUILD_SHA) == "dev" || juce::String (ELOOPER_BUILD_SHA).isEmpty(); }

    // Update log: ~/Library/Logs/ELooper/update.log
    void ulog (const juce::String& msg)
    {
        auto f = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/ELooper/update.log");
        f.getParentDirectory().createDirectory();
        if (f.getSize() > 512 * 1024) f.deleteFile();
        f.appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + "  " + msg + "\n");
        DBG ("Updater: " << msg);
    }

    // Remembers which build we last tried to install, so a failed install can't loop.
    juce::File attemptFile() { return PluginHost::appDataDir().getChildFile ("update-attempt.txt"); }

    juce::String shQuote (const juce::String& s) { return "'" + s.replace ("'", "'\\''") + "'"; }

    // Runs a process to completion with no piped output. Returns true on exit code 0.
    bool runTool (const juce::StringArray& args, int timeoutMs = 120000)
    {
        juce::ChildProcess p;
        if (! p.start (args, 0)) return false;
        if (! p.waitForProcessToFinish (timeoutMs)) { p.kill(); return false; }
        return p.getExitCode() == 0;
    }

    juce::URL::InputStreamOptions streamOptions (int timeoutMs, int* status, bool json)
    {
        const juce::String headers = json ? "Accept: application/vnd.github+json\r\nUser-Agent: ELooper-Updater\r\n"
                                          : "User-Agent: ELooper-Updater\r\n";
        return juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                   .withConnectionTimeoutMs (timeoutMs)
                   .withNumRedirectsToFollow (5)
                   .withStatusCode (status)
                   .withExtraHeaders (headers);
    }
}

// ---- result handed from the worker to the message thread -------------------------------------
struct Updater::Result {
    enum class Kind { UpToDate, Ready, Error } kind = Kind::Error;
    juce::String message;
    juce::File stagedApp, workDir;
    juce::String sha;
};

// ---- progress window -------------------------------------------------------------------------
struct Updater::ProgressWindow : public juce::DocumentWindow {
    struct Content : public juce::Component {
        Content() : bar (value)
        {
            label.setJustificationType (juce::Justification::centred);
            label.setFont (theme::font (16.0f, true));
            label.setColour (juce::Label::textColourId, theme::text);
            addAndMakeVisible (label);
            bar.setColour (juce::ProgressBar::foregroundColourId, theme::accent);
            bar.setColour (juce::ProgressBar::backgroundColourId, theme::panelRaised);
            addAndMakeVisible (bar);
            setSize (420, 120);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (24, 20);
            label.setBounds (r.removeFromTop (30));
            r.removeFromTop (12);
            bar.setBounds (r.removeFromTop (22));
        }
        void paint (juce::Graphics& g) override { g.fillAll (theme::background); }
        double value = -1.0;   // negative = indeterminate
        juce::Label label;
        juce::ProgressBar bar;
    };

    explicit ProgressWindow (const juce::String& text)
        : juce::DocumentWindow ("ELooper", theme::background, 0)
    {
        auto* c = new Content();
        c->label.setText (text, juce::dontSendNotification);
        content = c;
        setUsingNativeTitleBar (true);
        setContentOwned (c, true);
        setResizable (false, false);
        centreWithSize (420, 120);
        setAlwaysOnTop (true);
        setVisible (true);
    }
    void closeButtonPressed() override {}
    void setText (const juce::String& t) { content->label.setText (t, juce::dontSendNotification); }
    void setValue (double v) { content->value = v; }

    Content* content = nullptr;
};

// ---- worker thread ---------------------------------------------------------------------------
struct Updater::Worker : public juce::Thread {
    Worker (Updater& o, bool interactiveCheck)
        : juce::Thread ("ELooper updater"), owner (o), interactive (interactiveCheck), aliveFlag (o.alive) {}

    ~Worker() override { stopThread (4000); }

    void run() override
    {
        auto result = std::make_shared<Result>();
        try { doWork (*result); }
        catch (...) { result->kind = Result::Kind::Error; result->message = "Unexpected error."; }

        if (threadShouldExit()) { cleanup (*result); return; }
        auto flag = aliveFlag; auto* o = &owner;
        juce::MessageManager::callAsync ([flag, o, result] {
            if (flag->load()) o->onFinished (result);
            else if (result->workDir != juce::File()) result->workDir.deleteRecursively();
        });
    }

    static void cleanup (const Result& r) { if (r.workDir != juce::File()) r.workDir.deleteRecursively(); }

    void post (std::function<void (Updater&)> fn)
    {
        auto flag = aliveFlag; auto* o = &owner;
        juce::MessageManager::callAsync ([flag, o, fn] { if (flag->load()) fn (*o); });
    }

    void fail (Result& r, const juce::String& msg)
    {
        DBG ("Updater: " << msg);
        r.kind = Result::Kind::Error; r.message = msg;
        cleanup (r); r.workDir = juce::File();
    }

    void doWork (Result& r)
    {
        // 1. Latest build.
        const juce::String repo (ELOOPER_REPO), branch (ELOOPER_UPDATE_BRANCH);
        juce::URL api ("https://api.github.com/repos/" + repo + "/actions/workflows/build.yml/runs");
        api = api.withParameter ("branch", branch).withParameter ("status", "success")
                 .withParameter ("event", "push").withParameter ("per_page", "1");
        int status = 0;
        auto in = api.createInputStream (streamOptions (apiTimeoutMs, &status, true));
        if (in == nullptr || status != 200)
            return fail (r, "Couldn't reach GitHub (HTTP " + juce::String (status) + ").");
        const auto json = juce::JSON::parse (in->readEntireStreamAsString());
        auto* runs = json["workflow_runs"].getArray();
        if (runs == nullptr || runs->isEmpty())
            return fail (r, "No successful builds were found.");
        const auto sha = (*runs)[0]["head_sha"].toString();
        const auto runId = (*runs)[0]["id"].toString();
        if (sha.isEmpty() || runId.isEmpty() || runId == "0")
            return fail (r, "Unexpected response from GitHub.");
        if (sha == juce::String (ELOOPER_BUILD_SHA)) { r.kind = Result::Kind::UpToDate; return; }
        r.sha = sha;
        {
            const auto prev = juce::StringArray::fromLines (attemptFile().loadFileAsString());
            if (prev.size() >= 2 && prev[0] == sha
                && juce::Time::currentTimeMillis() - prev[1].getLargeIntValue() < 15 * 60 * 1000)
                return fail (r, "Installing build " + sha.substring (0, 7) + " didn't take effect. See ~/Library/Logs/ELooper/update.log.");
        }
        ulog ("Update available: " + juce::String (ELOOPER_BUILD_SHA).substring (0, 7) + " -> " + sha.substring (0, 7));
        if (threadShouldExit()) return;

        // 2. Download.
        post ([] (Updater& u) { u.showProgress ("Downloading the newest version of ELooper…"); });
        r.workDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getNonexistentChildFile ("ELooperUpdate", "", false);
        if (! r.workDir.createDirectory()) { r.workDir = juce::File(); return fail (r, "Couldn't create a temporary folder."); }
        const auto outer = r.workDir.getChildFile ("outer.zip");

        int dlStatus = 0;
        auto dl = juce::URL ("https://nightly.link/" + repo + "/actions/runs/" + runId + "/ELooper-macos.zip")
                      .createInputStream (streamOptions (downloadTimeoutMs, &dlStatus, false));
        if (dl == nullptr || dlStatus != 200)
            return fail (r, "Couldn't download the update (HTTP " + juce::String (dlStatus) + ").");
        {
            juce::FileOutputStream out (outer);
            if (out.failedToOpen()) return fail (r, "Couldn't write the update to disk.");
            const auto total = dl->getTotalLength();
            juce::HeapBlock<char> buf (64 * 1024);
            juce::int64 done = 0;
            double lastReported = -1;
            while (! dl->isExhausted())
            {
                if (threadShouldExit()) return cleanup (r);
                const int n = dl->read (buf, 64 * 1024);
                if (n < 0) return fail (r, "The download was interrupted.");
                if (n == 0) break;
                if (! out.write (buf, (size_t) n)) return fail (r, "Couldn't write the update to disk.");
                done += n;
                const double p = total > 0 ? (double) done / (double) total : -1.0;
                if (std::abs (p - lastReported) > 0.01) {
                    lastReported = p;
                    post ([p] (Updater& u) { u.setProgress (p); });
                }
            }
            out.flush();
            if (total > 0 && done < total) return fail (r, "The download was incomplete.");
        }
        if (outer.getSize() < 1024) return fail (r, "The downloaded file is empty.");

        // 3. Extract (double zip) and de-quarantine.
        post ([] (Updater& u) { u.setProgress (-1.0); });
        const auto dir1 = r.workDir.getChildFile ("dir1"), dir2 = r.workDir.getChildFile ("dir2");
        dir1.createDirectory(); dir2.createDirectory();
        if (! runTool ({ "/usr/bin/ditto", "-x", "-k", outer.getFullPathName(), dir1.getFullPathName() }))
            return fail (r, "Couldn't unpack the update.");
        const auto inner = dir1.getChildFile ("ELooper-macos.zip");
        if (! inner.existsAsFile()) return fail (r, "The update package has an unexpected layout.");
        if (! runTool ({ "/usr/bin/ditto", "-x", "-k", inner.getFullPathName(), dir2.getFullPathName() }))
            return fail (r, "Couldn't unpack the app bundle.");
        const auto app = dir2.getChildFile ("ELooper.app");
        if (! app.getChildFile ("Contents/MacOS/ELooper").existsAsFile())
            return fail (r, "The update doesn't contain a valid ELooper.app.");
        runTool ({ "/usr/bin/xattr", "-dr", "com.apple.quarantine", app.getFullPathName() }, 60000);
        outer.deleteFile(); inner.deleteFile();

        r.kind = Result::Kind::Ready;
        r.stagedApp = app;
    }

    Updater& owner;
    bool interactive;
    std::shared_ptr<std::atomic<bool>> aliveFlag;
};

// ---- Updater ---------------------------------------------------------------------------------
Updater::Updater (std::function<bool()> f) : canRestartSilently (std::move (f)) {}

Updater::~Updater()
{
    alive->store (false);
    worker.reset();     // stops and joins the thread
    progress.reset();
}

bool Updater::isAutoUpdateEnabled()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "ELooper";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName = "ELooper";
    juce::PropertiesFile pf (o);
    return pf.getBoolValue (autoUpdateKey, true);
}

void Updater::checkOnLaunch()
{
   #if JUCE_MAC
    if (! isScanChild()) cleanupAfterUpdate();
   #endif
    if (! platformSupported() || isDevBuild() || isScanChild() || ! isAutoUpdateEnabled()) return;
    start (false);
}

void Updater::checkInteractive()
{
    if (! platformSupported()) { ui::alert ("Check for Updates", "Updates are only available on macOS."); return; }
    if (isDevBuild()) { ui::alert ("Check for Updates", "This is a development build, which doesn't update itself."); return; }
    if (busy) { ui::alert ("Check for Updates", "An update check is already in progress."); return; }
    start (true);
}

void Updater::start (bool interactive)
{
    if (busy) return;
    busy = true;
    worker = std::make_unique<Worker> (*this, interactive);
    worker->interactive = interactive;
    worker->startThread();
}

void Updater::showProgress (const juce::String& text)
{
    // On launch, only show the window when we could also restart without asking.
    if (worker != nullptr && ! worker->interactive && ! (canRestartSilently && canRestartSilently())) return;
    if (progress == nullptr) progress = std::make_unique<ProgressWindow> (text);
    else progress->setText (text);
}

void Updater::closeProgress() { progress.reset(); }

void Updater::setProgress (double p) { if (progress != nullptr) progress->setValue (p); }

void Updater::onFinished (std::shared_ptr<Result> r)
{
    busy = false;
    const bool interactive = worker != nullptr && worker->interactive;
    closeProgress();
    switch (r->kind) {
        case Result::Kind::UpToDate:
            if (interactive) ui::alert ("Check for Updates", "You're up to date.");
            break;
        case Result::Kind::Error:
            if (interactive) ui::alert ("Check for Updates", "The update check failed.\n\n" + r->message);
            break;
        case Result::Kind::Ready:
            if (canRestartSilently && canRestartSilently()) {
                showProgress ("Updating ELooper to the newest version…");
                install (r->stagedApp, r->workDir, r->sha);
            } else {
                promptRestart (r->stagedApp, r->workDir, r->sha);
            }
            break;
    }
}

void Updater::promptRestart (juce::File stagedApp, juce::File workDir, juce::String sha)
{
    auto flag = alive; auto* self = this;
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Update ready")
                                      .withMessage ("A new version of ELooper is ready. Restart now to update? (Save your project first.)")
                                      .withButton ("Restart").withButton ("Later"),
                                  [flag, self, stagedApp, workDir, sha] (int r) {
        if (r == 1 && flag->load()) self->install (stagedApp, workDir, sha);
        else workDir.deleteRecursively();   // "Later": the next launch will update
    });
}

void Updater::install (const juce::File& stagedApp, const juce::File& workDir, const juce::String& sha)
{
    auto failInstall = [this, workDir] (const juce::String& msg) {
        ulog ("Install failed: " + msg);
        closeProgress();
        ui::alert ("Update failed", msg + "\n\nYou can keep using this version. Details: ~/Library/Logs/ELooper/update.log");
        workDir.deleteRecursively();
    };

    auto current = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    juce::File target = current;
    if (current.getFullPathName().contains ("/AppTranslocation/")
        || ! current.getParentDirectory().hasWriteAccess()
        || ! current.hasFileExtension ("app"))
        target = juce::File ("/Applications/ELooper.app");
    ulog ("Installing " + stagedApp.getFullPathName() + " -> " + target.getFullPathName());

    if (! target.getParentDirectory().hasWriteAccess())
        return failInstall ("ELooper can't write to " + target.getParentDirectory().getFullPathName() + ".");

    // macOS lets us rename the bundle of the running app, so the swap happens here, while we
    // can still report problems, rather than in a script after quitting.
    const auto old = target.getSiblingFile ("ELooper.app.old");
    if (old.exists()) runTool ({ "/bin/rm", "-rf", old.getFullPathName() }, 60000);
    if (target.exists() && ! runTool ({ "/bin/mv", target.getFullPathName(), old.getFullPathName() }, 60000))
        return failInstall ("Couldn't move the current version aside (permissions?).");
    if (! runTool ({ "/bin/mv", stagedApp.getFullPathName(), target.getFullPathName() }, 60000)
        && ! runTool ({ "/usr/bin/ditto", stagedApp.getFullPathName(), target.getFullPathName() }, 120000)) {
        runTool ({ "/bin/mv", old.getFullPathName(), target.getFullPathName() }, 60000);   // put the old one back
        return failInstall ("Couldn't put the new version in place.");
    }
    runTool ({ "/usr/bin/xattr", "-dr", "com.apple.quarantine", target.getFullPathName() }, 60000);
    attemptFile().replaceWithText (sha + "\n" + juce::String (juce::Time::currentTimeMillis()));
    ulog ("Swapped in new version; launching it");

    // Start the new version (allowed to run alongside us for a moment), then quit.
    if (! runTool ({ "/usr/bin/open", "-n", target.getFullPathName(), "--args", "--elooper-updated" }, 20000)) {
        ulog ("open failed");
        closeProgress();
        ui::alert ("Update installed", "The new version is installed, but it couldn't be started automatically. "
                                       "Quit ELooper and open it again from the Dock.");
        workDir.deleteRecursively();
        return;
    }
    workDir.deleteRecursively();
    ulog ("New version started; quitting old version");
    juce::JUCEApplicationBase::quit();
}

void Updater::cleanupAfterUpdate()
{
    // Remove the previous bundle left next to us by an update.
    const auto current = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    const auto old = current.getSiblingFile ("ELooper.app.old");
    if (old.exists()) {
        ulog ("Removing previous version " + old.getFullPathName());
        old.deleteRecursively();
    }
}

} // namespace el
