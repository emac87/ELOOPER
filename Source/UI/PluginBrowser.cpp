#include "UI/PluginBrowser.h"
#include "Engine/BuiltInEq.h"
#include "UI/Theme.h"
#include "UI/PluginIcons.h"

namespace el {

namespace {

// Content of the scan window: progress bar, current plug-in name and a Cancel button.
class ScanContent : public juce::Component {
public:
    explicit ScanContent (std::function<void()> onCancel)
        : bar (progress), cancelCallback (std::move (onCancel))
    {
        label.setFont (theme::font (16.0f));
        label.setColour (juce::Label::textColourId, theme::text);
        label.setText ("Preparing...", juce::dontSendNotification);
        label.setMinimumHorizontalScale (1.0f);
        bar.setColour (juce::ProgressBar::foregroundColourId, theme::accent);
        bar.setColour (juce::ProgressBar::backgroundColourId, theme::panelRaised);
        cancel.setButtonText ("Cancel");
        cancel.onClick = [this] { if (cancelCallback) cancelCallback(); };
        addAndMakeVisible (label);
        addAndMakeVisible (bar);
        addAndMakeVisible (cancel);
        setSize (540, 170);
    }

    void update (float p, const juce::String& current)
    {
        progress = (double) p;
        label.setText (juce::File (current).getFileName().isNotEmpty() ? juce::File (current).getFileName() : current,
                       juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::panel); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        label.setBounds (r.removeFromTop (30));
        r.removeFromTop (8);
        bar.setBounds (r.removeFromTop (28));
        r.removeFromTop (12);
        cancel.setBounds (r.removeFromTop (36).withSizeKeepingCentre (120, 36));
    }

private:
    double progress = 0;
    juce::ProgressBar bar;
    juce::Label label;
    juce::TextButton cancel;
    std::function<void()> cancelCallback;
};

class ScanWindow : public juce::DocumentWindow {
public:
    ScanWindow (std::function<void()> onCancel)
        : juce::DocumentWindow ("Scanning Plug-ins", theme::panel, juce::DocumentWindow::closeButton),
          cancelCallback (std::move (onCancel))
    {
        setUsingNativeTitleBar (true);
        content = new ScanContent ([this] { closeButtonPressed(); });
        setContentOwned (content, true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        setAlwaysOnTop (true);
    }

    void update (float p, const juce::String& current) { content->update (p, current); }
    void closeButtonPressed() override { if (cancelCallback) cancelCallback(); }

private:
    ScanContent* content = nullptr;
    std::function<void()> cancelCallback;
};

void showError (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plug-in", message);
}

} // namespace

void showPluginScanWindow (EngineController& c, std::function<void()> onFinished)
{
    auto& host = c.pluginHost();
    if (host.isScanning()) return;

    // The window is owned by this shared holder and torn down when the scan finishes or is cancelled.
    auto holder = std::make_shared<std::unique_ptr<ScanWindow>>();
    *holder = std::make_unique<ScanWindow> ([&host, holder]
    {
        // Cancel: stop the scan; the finished callback (still delivered) removes the window.
        host.cancelScan();
        juce::MessageManager::callAsync ([holder] { holder->reset(); });
    });

    host.startScan (
        [holder] (float p, const juce::String& current) { if (*holder) (*holder)->update (p, current); },
        [holder, finishedCb = std::move (onFinished)]
        {
            holder->reset();
            if (finishedCb) finishedCb();
        });
}

namespace {
    // ---- recently used plug-ins (per menu type), kept in Application Support/ELooper -------------
    juce::File recentFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("ELooper").getChildFile ("RecentPlugins.txt");
    }
    juce::String recentKey (FxChain chain)
    {
        return chain == FxChain::Instrument ? "inst" : chain == FxChain::MidiFx ? "midi" : "fx";
    }
    juce::StringArray loadRecent (FxChain chain)
    {
        juce::StringArray lines, out;
        lines.addLines (recentFile().loadFileAsString());
        const auto key = recentKey (chain) + "\t";
        for (auto& l : lines) if (l.startsWith (key)) out.add (l.fromFirstOccurrenceOf ("\t", false, false));
        return out;
    }
    void rememberRecent (FxChain chain, const juce::String& identifier)
    {
        juce::StringArray lines, kept;
        lines.addLines (recentFile().loadFileAsString());
        const auto key = recentKey (chain) + "\t";
        kept.add (key + identifier);
        int n = 1;
        for (auto& l : lines) {
            if (l.trim().isEmpty() || l == key + identifier) continue;
            if (l.startsWith (key) && ++n > 6) continue;   // keep the newest 6 per type
            kept.add (l);
        }
        recentFile().getParentDirectory().createDirectory();
        recentFile().replaceWithText (kept.joinIntoString ("\n"));
    }

    juce::String categoryName (icons::Kind k)
    {
        switch (k) {
            case icons::Kind::Synth:      return "Synthesizers";
            case icons::Kind::Piano:      return "Keyboards";
            case icons::Kind::Drums:      return "Drum Machines & Kits";
            case icons::Kind::Sampler:    return "Samplers";
            case icons::Kind::Bass:       return "Bass";
            case icons::Kind::Strings:    return "Strings, Pads & Orchestral";
            case icons::Kind::Organ:      return "Organs";
            case icons::Kind::Reverb:     return "Reverb";
            case icons::Kind::Delay:      return "Delay";
            case icons::Kind::Eq:         return "EQ & Filter";
            case icons::Kind::Compressor: return "Dynamics";
            case icons::Kind::Distortion: return "Distortion & Amps";
            case icons::Kind::Modulation: return "Modulation";
            case icons::Kind::MidiFx:     return "MIDI Effects";
            case icons::Kind::Effect: case icons::Kind::Audio: case icons::Kind::Midi:
            case icons::Kind::Bus: case icons::Kind::Master: case icons::Kind::AuxOut: break;
        }
        return "Other";
    }
}

void showPluginMenu (EngineController& c, Id trackId, FxChain chain, int insertIndex, juce::Component* target,
                     std::function<void (Id)> onInserted)
{
    auto& host = c.pluginHost();
    std::vector<juce::PluginDescription> list;
    switch (chain)
    {
        case FxChain::Instrument: list = host.instruments(); break;
        case FxChain::MidiFx:     list = host.midiEffects(); break;
        case FxChain::Pre:
        case FxChain::Post:       list = host.effects(); break;
    }

    // Logic-style: recently used first, then a submenu per type, then every plug-in by maker.
    constexpr int scanId = 1, randomId = 2, firstPluginId = 100;
    juce::PopupMenu menu;
    auto label = [] (const juce::PluginDescription& d, bool withMaker) {
        return d.name + (withMaker && d.manufacturerName.isNotEmpty() ? "  (" + d.manufacturerName + ")" : juce::String());
    };
    auto itemFor = [&] (size_t i, bool withMaker) {
        juce::PopupMenu::Item it (label (list[i], withMaker));
        it.itemID = firstPluginId + (int) i;
        it.image = icons::makeDrawable (chain == FxChain::MidiFx ? icons::Kind::MidiFx : icons::guessKind (list[i]));
        return it;
    };

    if (list.empty()) {
        if (chain == FxChain::Instrument)
            menu.addItem (randomId, "Random instrument", false);
        menu.addItem (-1, juce::String (juce::CharPointer_UTF8 ("No plug-ins found \xe2\x80\x94 scan first")), false);
    } else {
        // Random pick across every scanned instrument; handled in the callback below.
        if (chain == FxChain::Instrument) {
            menu.addItem (randomId, "Random instrument");
            menu.addSeparator();
        }

        // Built-in effects (ELooper EQ) straight at the top of the Pre / Post FX menus.
        if (chain == FxChain::Pre || chain == FxChain::Post) {
            bool any = false;
            for (size_t i = 0; i < list.size(); ++i)
                if (builtin::isBuiltIn (list[i])) { menu.addItem (itemFor (i, false)); any = true; }
            if (any) menu.addSeparator();
        }

        // Recently used
        const auto recent = loadRecent (chain);
        bool anyRecent = false;
        for (auto& id : recent)
            for (size_t i = 0; i < list.size(); ++i)
                if (list[i].createIdentifierString() == id) {
                    if (builtin::isBuiltIn (list[i])) break;   // already at the top of the menu
                    if (! anyRecent) { menu.addSectionHeader ("Recent"); anyRecent = true; }
                    menu.addItem (itemFor (i, true));
                    break;
                }
        if (anyRecent) menu.addSeparator();

        // By type
        if (chain != FxChain::MidiFx) {
            std::vector<icons::Kind> order = chain == FxChain::Instrument
                ? std::vector<icons::Kind> { icons::Kind::Synth, icons::Kind::Drums, icons::Kind::Sampler, icons::Kind::Piano,
                                             icons::Kind::Bass, icons::Kind::Strings, icons::Kind::Organ }
                : std::vector<icons::Kind> { icons::Kind::Eq, icons::Kind::Compressor, icons::Kind::Reverb, icons::Kind::Delay,
                                             icons::Kind::Modulation, icons::Kind::Distortion, icons::Kind::MidiFx, icons::Kind::Effect };
            for (auto k : order) {
                juce::PopupMenu sub;
                std::vector<size_t> idx;
                for (size_t i = 0; i < list.size(); ++i) if (icons::guessKind (list[i]) == k) idx.push_back (i);
                std::sort (idx.begin(), idx.end(), [&] (size_t a, size_t b) { return list[a].name.compareIgnoreCase (list[b].name) < 0; });
                for (auto i : idx) sub.addItem (itemFor (i, true));
                if (sub.getNumItems() > 0)
                    menu.addSubMenu (categoryName (k), sub, true, icons::makeDrawable (k));
            }
            menu.addSeparator();
        }

        // Every plug-in by manufacturer (the host list is sorted by manufacturer, then name).
        juce::PopupMenu byMaker, sub;
        juce::String currentMaker;
        auto flush = [&] {
            if (sub.getNumItems() > 0)
                byMaker.addSubMenu (currentMaker.isNotEmpty() ? currentMaker : juce::String ("Other"), sub);
            sub = juce::PopupMenu();
        };
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0 && list[i].manufacturerName != currentMaker) flush();
            currentMaker = list[i].manufacturerName;
            sub.addItem (itemFor (i, false));
        }
        flush();
        menu.addSubMenu (chain == FxChain::Instrument ? "AU Instruments" : chain == FxChain::MidiFx ? "AU MIDI Effects" : "Audio Units", byMaker);
    }

    menu.addSeparator();
    menu.addItem (scanId, "Scan for Plug-ins...", ! host.isScanning());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
        [&c, trackId, chain, insertIndex, list, insertedCb = std::move (onInserted)] (int result)
        {
            // "Random instrument" resolves to one uniformly chosen entry, then takes the normal insert path below.
            if (result == randomId && ! list.empty())
                result = firstPluginId + juce::Random::getSystemRandom().nextInt ((int) list.size());

            if (result == scanId)
            {
                showPluginScanWindow (c);
            }
            else if (result >= firstPluginId && (size_t) (result - firstPluginId) < list.size())
            {
                const auto& d = list[(size_t) (result - firstPluginId)];
                rememberRecent (chain, d.createIdentifierString());
                c.insertPlugin (trackId, chain, insertIndex, d,
                    [insertedCb] (Id slotId, juce::String error)
                    {
                        if (slotId == kInvalidId || error.isNotEmpty())
                            showError (error.isNotEmpty() ? error : juce::String ("The plug-in could not be loaded."));
                        else if (insertedCb)
                            insertedCb (slotId);
                    });
            }
        });
}

// ---- routing pickers ----------------------------------------------------------------------------
namespace {
    void routingAlert (const juce::String& msg)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Routing", msg);
    }
}

void showOutputPicker (EngineController& c, Id trackId, juce::Component* target)
{
    auto t = c.session().findTrack (trackId);
    if (! t) return;
    juce::PopupMenu m, buses;
    m.addItem ((int) kMasterId, "Stereo Out (Master)", true, t->outputBus == kMasterId);
    for (auto& bus : c.session().buses())
        if (bus->id != trackId)
            buses.addItem ((int) bus->id, bus->name, true, t->outputBus == bus->id);
    buses.addSeparator();
    buses.addItem (-2, "New Bus");
    m.addSubMenu ("Bus", buses);
    auto* ctl = &c;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [ctl, trackId] (int r) {
        if (r == -2) {
            const Id bus = ctl->addTrack (TrackKind::Bus);
            if (bus != kInvalidId && ! ctl->setTrackOutput (trackId, bus)) routingAlert ("That output would create a feedback loop.");
            return;
        }
        if (r <= 0) return;
        if (! ctl->setTrackOutput (trackId, (Id) r)) routingAlert ("That output would create a feedback loop.");
    });
}

void showSendPicker (EngineController& c, Id trackId, juce::Component* target)
{
    auto t = c.session().findTrack (trackId);
    if (! t) return;
    juce::PopupMenu m;
    m.addSectionHeader ("Send to");
    for (auto& bus : c.session().buses()) {
        if (bus->id == trackId) continue;
        const bool used = std::any_of (t->sends.begin(), t->sends.end(), [&] (const SendSpec& s) { return s.busId == bus->id; });
        m.addItem ((int) bus->id, bus->name, ! used);
    }
    m.addSeparator();
    m.addItem (-2, "New Bus");
    auto* ctl = &c;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [ctl, trackId] (int r) {
        Id bus = kInvalidId;
        if (r == -2) bus = ctl->addTrack (TrackKind::Bus);
        else if (r > 0) bus = (Id) r;
        if (bus == kInvalidId) return;
        if (ctl->addSend (trackId, bus, false) < 0)
            routingAlert ("That send isn't possible: it would create a feedback loop, or the track has too many sends.");
    });
}

} // namespace el
