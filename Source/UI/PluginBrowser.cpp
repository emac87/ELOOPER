#include "UI/PluginBrowser.h"
#include "UI/Theme.h"

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

    constexpr int scanId = 1, firstPluginId = 100;
    juce::PopupMenu menu;

    if (list.empty())
        menu.addItem (-1, juce::String (juce::CharPointer_UTF8 ("No plug-ins found \xe2\x80\x94 scan first")), false);

    // Group by manufacturer (the list is already sorted by manufacturer, then name).
    juce::String currentMaker;
    juce::PopupMenu sub;
    auto flush = [&]
    {
        if (currentMaker.isNotEmpty() || sub.getNumItems() > 0)
            menu.addSubMenu (currentMaker.isNotEmpty() ? currentMaker : juce::String ("Other"), sub);
        sub = juce::PopupMenu();
    };
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto maker = list[i].manufacturerName;
        if (i > 0 && maker != currentMaker) flush();
        currentMaker = maker;
        sub.addItem (firstPluginId + (int) i, list[i].name + (list[i].pluginFormatName.isNotEmpty() ? "  (" + list[i].pluginFormatName + ")" : juce::String()));
    }
    if (! list.empty()) flush();

    menu.addSeparator();
    menu.addItem (scanId, "Scan for Plug-ins...", ! host.isScanning());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
        [&c, trackId, chain, insertIndex, list, insertedCb = std::move (onInserted)] (int result)
        {
            if (result == scanId)
            {
                showPluginScanWindow (c);
            }
            else if (result >= firstPluginId && (size_t) (result - firstPluginId) < list.size())
            {
                c.insertPlugin (trackId, chain, insertIndex, list[(size_t) (result - firstPluginId)],
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

} // namespace el
