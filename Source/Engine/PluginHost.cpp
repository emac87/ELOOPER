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
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
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
    // Background scanner. Callbacks are marshalled to the message thread and guarded by `alive`.
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
            const auto dataDir = PluginHost::appDataDir();
            const auto pedal = dataDir.getChildFile ("scan-deadmanspedal.txt");
            const auto listFile = dataDir.getChildFile ("plugins.xml");
            const int numFormats = formats.getNumFormats();

            for (int f = 0; f < numFormats && ! threadShouldExit(); ++f)
            {
                auto* format = formats.getFormat (f);
                juce::PluginDirectoryScanner scanner (known, *format, format->getDefaultLocationsToSearch(),
                                                      true, pedal, false);
                juce::String name;
                for (;;)
                {
                    if (threadShouldExit()) break;
                    const auto next = scanner.getNextPluginFileThatWillBeScanned();
                    post (((float) f + scanner.getProgress()) / (float) juce::jmax (1, numFormats), next);
                    if (! scanner.scanNextFile (true, name))
                        break;
                    if (auto xml = known.createXml())
                        xml->writeTo (listFile);   // persist after each plugin so a crash keeps progress
                }
            }

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
};

// ---------------------------------------------------------------------------------------------
PluginHost::PluginHost() : impl (std::make_unique<Impl>())
{
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    formatManager.addFormat (new juce::AudioUnitPluginFormat());
#endif
#if JUCE_PLUGINHOST_VST3
    formatManager.addFormat (new juce::VST3PluginFormat());
#endif
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
        it->second->toFront (true);
        return;
    }
    auto alive = impl->alive;
    auto* self = this;
    impl->windows[slot->id] = std::make_unique<PluginWindow> (slot, [alive, self] (Id id)
    {
        // Delete the window after the close-button handler returns.
        juce::MessageManager::callAsync ([alive, self, id]
        {
            if (alive->load()) self->closeEditor (id);
        });
    });
}

void PluginHost::closeEditor (Id slotId)
{
    impl->windows.erase (slotId);
}

void PluginHost::closeAllEditors()
{
    impl->windows.clear();
}

} // namespace el
