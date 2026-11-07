#include "Engine/PluginHost.h"

namespace el {

namespace {

// Floating window that hosts a plugin's editor (or a generic editor if it has none).
class PluginWindow : public juce::DocumentWindow {
public:
    PluginWindow (PluginSlotPtr s, std::function<void (Id)> onCloseRequested)
        : juce::DocumentWindow (s->name(), juce::Colours::black, juce::DocumentWindow::closeButton),
          slot (std::move (s)), closeRequested (std::move (onCloseRequested))
    {
        setUsingNativeTitleBar (true);
        auto* inst = slot->instance.get();
        juce::AudioProcessorEditor* ed = nullptr;
        if (inst != nullptr)
        {
            if (inst->hasEditor())
                ed = inst->createEditorIfNeeded();
            if (ed == nullptr)
                ed = new juce::GenericAudioProcessorEditor (*inst);
        }
        if (ed != nullptr)
            setContentOwned (ed, true);   // editor deleted with the window (before the instance)
        setResizable (ed != nullptr && ed->isResizable(), false);
        centreWithSize (juce::jmax (120, getWidth()), juce::jmax (60, getHeight()));
        keepOnScreen();
        setVisible (true);
        toFront (true);
    }

    // Plug-in editors are native views (NSView) that JUCE does not scale. If this window used
    // the app's UI zoom (Desktop global scale), JUCE would size the native view by the zoom,
    // the view would report the bigger size back, and the window would grow without end (the
    // "glitching" window). Plug-in windows therefore always run at 100%.
    float getDesktopScaleFactor() const override { return 1.0f; }

    // Keep the title bar reachable even if a plug-in asks for a huge or off-screen window.
    void keepOnScreen()
    {
        auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
        if (display == nullptr) display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        if (display == nullptr) return;
        const auto area = display->userArea;
        auto b = getBounds();
        b.setWidth (juce::jmin (b.getWidth(), area.getWidth()));
        b.setHeight (juce::jmin (b.getHeight(), area.getHeight()));
        b = b.constrainedWithin (area);
        if (b != getBounds()) setBounds (b);
    }

    void childBoundsChanged (juce::Component* c) override
    {
        juce::DocumentWindow::childBoundsChanged (c);
        if (! inResize) { const juce::ScopedValueSetter<bool> s (inResize, true); keepOnScreen(); }
    }

    ~PluginWindow() override
    {
        // The editor must go before the processor does; the processor outlives us by contract.
        clearContentComponent();
    }

    void closeButtonPressed() override { if (closeRequested) closeRequested (slot->id); }

private:
    PluginSlotPtr slot;
    std::function<void (Id)> closeRequested;
    bool inResize = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

bool isMidiEffect (const juce::PluginDescription& d)
{
    if (d.isInstrument) return false;
    if (d.numInputChannels == 0 && d.numOutputChannels == 0) return true;
    return d.category.containsIgnoreCase ("midi");
}

} // namespace

// ---------------------------------------------------------------------------------------------
struct PluginHost::Impl {
    // Background scanner. Every plugin is probed in a separate child process (this app
    // relaunched with --elooper-scan), so a plugin that crashes or hangs while being scanned
    // is blacklisted instead of taking ELooper down. Callbacks are marshalled to the message
    // thread and guarded by `alive`.
    class ScanThread : public juce::Thread {
    public:
        ScanThread (juce::AudioPluginFormatManager& fm, juce::KnownPluginList& kl,
                    std::function<void (float, const juce::String&)> prog, std::function<void()> fin,
                    std::shared_ptr<std::atomic<bool>> aliveFlag)
            : juce::Thread ("ELooper plugin scan"), formats (fm), known (kl),
              onProgress (std::move (prog)), onFinished (std::move (fin)), alive (std::move (aliveFlag)) {}

        ~ScanThread() override { stopThread (20000); }

        std::atomic<bool> finishedFlag { false };

        void run() override
        {
            const auto listFile = PluginHost::appDataDir().getChildFile ("plugins.xml");
            const auto tmpDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ELooperScan");
            tmpDir.createDirectory();
            const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

            struct Job { juce::String format, id; };
            std::vector<Job> jobs;
            for (int f = 0; f < formats.getNumFormats(); ++f) {
                auto* format = formats.getFormat (f);
                for (auto& id : format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false))
                    if (! known.getBlacklistedFiles().contains (id) && ! known.isListingUpToDate (id, *format))
                        jobs.push_back ({ format->getName(), id });
            }

            struct Running { std::unique_ptr<juce::ChildProcess> proc; Job job; juce::File out; juce::uint32 started; };
            std::vector<Running> running;
            const int maxParallel = juce::jlimit (1, 4, juce::SystemStats::getNumCpus() / 2);
            constexpr juce::uint32 timeoutMs = 60000;
            size_t next = 0, done = 0;

            auto finish = [&] (Running& r, bool ok) {
                bool added = false;
                if (ok) if (auto xml = juce::parseXML (r.out)) {
                    for (auto* e : xml->getChildIterator()) {
                        juce::PluginDescription d;
                        if (d.loadFromXml (*e)) { known.addType (d); added = true; }
                    }
                }
                if (! added) known.addToBlacklist (r.job.id);   // crashed, hung or not a plugin
                r.out.deleteFile();
                ++done;
                if (auto xml = known.createXml()) xml->writeTo (listFile);
            };

            while (! threadShouldExit() && (next < jobs.size() || ! running.empty())) {
                while (running.size() < (size_t) maxParallel && next < jobs.size()) {
                    Running r; r.job = jobs[next]; r.started = juce::Time::getMillisecondCounter();
                    r.out = tmpDir.getChildFile ("scan-" + juce::String ((int) next) + ".xml");
                    r.out.deleteFile();
                    r.proc = std::make_unique<juce::ChildProcess>();
                    const juce::StringArray args { exe.getFullPathName(), "--elooper-scan", r.job.format, r.job.id, r.out.getFullPathName() };
                    post ((float) done / (float) std::max<size_t> (1, jobs.size()), nameOf (r.job.id));
                    ++next;
                    if (! r.proc->start (args, 0)) { finish (r, false); continue; }
                    running.push_back (std::move (r));
                }
                for (size_t i = 0; i < running.size();) {
                    auto& r = running[i];
                    const bool timedOut = juce::Time::getMillisecondCounter() - r.started > timeoutMs;
                    if (! r.proc->isRunning() || timedOut) {
                        if (timedOut) r.proc->kill();
                        finish (r, ! timedOut && r.proc->getExitCode() == 0);
                        running.erase (running.begin() + (long) i);
                    } else ++i;
                }
                wait (30);
            }
            for (auto& r : running) { r.proc->kill(); r.out.deleteFile(); }   // cancelled

            if (auto xml = known.createXml())
                xml->writeTo (listFile);

            finishedFlag = true;
            auto flag = alive;
            auto fin = onFinished;
            juce::MessageManager::callAsync ([flag, fin]
            {
                if (flag->load() && fin) fin();
            });
        }

    private:
        static juce::String nameOf (const juce::String& id)
        {
            // AU identifiers look like "AudioUnit:Synths/aumu,abcd,Manu" — show something readable.
            return id.fromLastOccurrenceOf ("/", false, false).upToFirstOccurrenceOf (",", false, false).isNotEmpty()
                       ? id.fromLastOccurrenceOf ("/", false, false) : id;
        }

        void post (float progress, const juce::String& current)
        {
            auto flag = alive;
            auto cb = onProgress;
            juce::MessageManager::callAsync ([flag, cb, progress, current]
            {
                if (flag->load() && cb) cb (progress, current);
            });
        }

        juce::AudioPluginFormatManager& formats;
        juce::KnownPluginList& known;
        std::function<void (float, const juce::String&)> onProgress;
        std::function<void()> onFinished;
        std::shared_ptr<std::atomic<bool>> alive;
    };

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::unique_ptr<ScanThread> scanThread;
    std::map<Id, std::unique_ptr<PluginWindow>> windows;

    // Keeps plugin windows floating above the main window while ELooper is in front, and lets
    // them drop behind other apps when it is not (like the plug-in windows of other DAWs).
    struct FloatTimer : juce::Timer {
        explicit FloatTimer (Impl& i) : impl (i) {}
        void timerCallback() override
        {
            const bool front = juce::Process::isForegroundProcess();
            if (front == wasFront) return;
            wasFront = front;
            for (auto& [id, w] : impl.windows)
                if (w != nullptr) w->setAlwaysOnTop (front);
        }
        Impl& impl;
        bool wasFront = false;
    };
    FloatTimer floatTimer { *this };
};

// ---------------------------------------------------------------------------------------------
PluginHost::PluginHost() : impl (std::make_unique<Impl>())
{
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    formatManager.addFormat (new juce::AudioUnitPluginFormat());
#endif
    // Audio Units only (VST3 support was removed at the user's request).
    loadKnownList();
}

PluginHost::~PluginHost()
{
    impl->alive->store (false);
    impl->scanThread.reset();
    closeAllEditors();
}

juce::File PluginHost::appDataDir()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    auto dir = base.getChildFile ("Application Support").getChildFile ("ELooper");   // base is ~/Library
#else
    auto dir = base.getChildFile ("ELooper");
#endif
    dir.createDirectory();
    return dir;
}

void PluginHost::startScan (std::function<void (float, const juce::String&)> onProgress,
                            std::function<void()> onFinished)
{
    if (isScanning()) return;
    impl->scanThread.reset();   // joins a finished previous scan
    impl->scanThread = std::make_unique<Impl::ScanThread> (formatManager, knownList, std::move (onProgress),
                                                           std::move (onFinished), impl->alive);
    impl->scanThread->startThread();
}

void PluginHost::cancelScan()
{
    if (impl->scanThread != nullptr)
    {
        impl->scanThread->signalThreadShouldExit();
        impl->scanThread->stopThread (30000);
    }
}

bool PluginHost::isScanning() const noexcept
{
    return impl->scanThread != nullptr && impl->scanThread->isThreadRunning() && ! impl->scanThread->finishedFlag.load();
}

void PluginHost::loadKnownList()
{
    auto f = appDataDir().getChildFile ("plugins.xml");
    if (! f.existsAsFile()) return;
    if (auto xml = juce::parseXML (f))
        knownList.recreateFromXml (*xml);
    // Drop entries for formats this build no longer hosts (e.g. VST3 from older versions).
    juce::StringArray hosted;
    for (int i = 0; i < formatManager.getNumFormats(); ++i) hosted.add (formatManager.getFormat (i)->getName());
    for (auto& t : knownList.getTypes())
        if (! hosted.contains (t.pluginFormatName))
            knownList.removeType (t);
}

void PluginHost::saveKnownList()
{
    if (auto xml = knownList.createXml())
        xml->writeTo (appDataDir().getChildFile ("plugins.xml"));
}

static std::vector<juce::PluginDescription> filtered (const juce::KnownPluginList& list,
                                                      const std::function<bool (const juce::PluginDescription&)>& keep)
{
    std::vector<juce::PluginDescription> out;
    for (const auto& d : list.getTypes())
        if (keep (d)) out.push_back (d);
    std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b)
    {
        const int m = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
        return m != 0 ? m < 0 : a.name.compareIgnoreCase (b.name) < 0;
    });
    return out;
}

std::vector<juce::PluginDescription> PluginHost::instruments() const
{
    return filtered (knownList, [] (const auto& d) { return d.isInstrument; });
}

std::vector<juce::PluginDescription> PluginHost::effects() const
{
    return filtered (knownList, [] (const auto& d) { return ! d.isInstrument && ! isMidiEffect (d); });
}

std::vector<juce::PluginDescription> PluginHost::midiEffects() const
{
    return filtered (knownList, [] (const auto& d) { return isMidiEffect (d); });
}

void PluginHost::createInstance (const juce::PluginDescription& desc, double sampleRate, int blockSize, CreateCallback cb)
{
    formatManager.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [callback = std::move (cb)] (std::unique_ptr<juce::AudioPluginInstance> p, const juce::String& err)
        {
            if (callback) callback (std::move (p), err);
        });
}

int PluginHost::enableMultiOut (juce::AudioPluginInstance& p)
{
    const int numOut = p.getBusCount (false);
    if (numOut > 1)
    {
        auto layout = p.getBusesLayout();
        for (int i = 1; i < numOut; ++i)
            layout.outputBuses.getReference (i) = juce::AudioChannelSet::stereo();

        if (! p.checkBusesLayoutSupported (layout) || ! p.setBusesLayout (layout))
        {
            // Fall back to the main bus only.
            auto mainOnly = p.getBusesLayout();
            for (int i = 1; i < numOut; ++i)
                mainOnly.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();
            if (p.checkBusesLayoutSupported (mainOnly))
                p.setBusesLayout (mainOnly);
        }
    }
    return p.getTotalNumOutputChannels();
}

void PluginHost::showEditor (const PluginSlotPtr& slot)
{
    if (slot == nullptr || slot->instance == nullptr) return;
    auto it = impl->windows.find (slot->id);
    if (it != impl->windows.end())
    {
        it->second->setVisible (true);
        it->second->setAlwaysOnTop (juce::Process::isForegroundProcess());
        it->second->toFront (true);
        return;
    }
    auto alive = impl->alive;
    auto* self = this;
    auto w = std::make_unique<PluginWindow> (slot, [alive, self] (Id id)
    {
        // Hide (not delete) after the close-button handler returns, so it reopens in place.
        juce::MessageManager::callAsync ([alive, self, id]
        {
            if (alive->load()) self->hideEditor (id);
        });
    });
    w->setAlwaysOnTop (juce::Process::isForegroundProcess());
    impl->windows[slot->id] = std::move (w);
    if (! impl->floatTimer.isTimerRunning()) impl->floatTimer.startTimer (300);
}

void PluginHost::hideEditor (Id slotId)
{
    auto it = impl->windows.find (slotId);
    if (it != impl->windows.end() && it->second != nullptr) it->second->setVisible (false);
}

bool PluginHost::isEditorOpen (Id slotId) const
{
    auto it = impl->windows.find (slotId);
    return it != impl->windows.end() && it->second != nullptr && it->second->isVisible();
}

void PluginHost::closeEditor (Id slotId)
{
    impl->windows.erase (slotId);
}

void PluginHost::closeAllEditors()
{
    impl->floatTimer.stopTimer();
    impl->windows.clear();
}

} // namespace el

// ---------------------------------------------------------------------------------------------
// Child-process side of the scanner: ELooper --elooper-scan <format> <identifier> <outFile>
int el::PluginHost::runScanChild (const juce::StringArray& args)
{
    const int i = args.indexOf ("--elooper-scan");
    if (i < 0 || i + 3 >= args.size()) return 3;
    const auto formatName = args[i + 1], id = args[i + 2];
    const juce::File out (args[i + 3]);
    juce::AudioPluginFormatManager fm;
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    fm.addFormat (new juce::AudioUnitPluginFormat());
#endif
    for (int f = 0; f < fm.getNumFormats(); ++f) {
        auto* format = fm.getFormat (f);
        if (format->getName() != formatName) continue;
        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile (found, id);
        if (found.isEmpty()) return 2;
        juce::XmlElement root ("PLUGINS");
        for (auto* d : found) root.addChildElement (d->createXml().release());
        return root.writeTo (out) ? 0 : 4;
    }
    return 1;
}
