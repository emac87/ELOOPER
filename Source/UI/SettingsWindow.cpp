#include "UI/SettingsWindow.h"
#include "UI/Theme.h"

namespace el {
namespace {

void styleLabel (juce::Label& l, const juce::String& text, bool dim = false, float h = 14.0f)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (theme::font (h));
    l.setColour (juce::Label::textColourId, dim ? theme::textDim : theme::text);
    l.setMinimumHorizontalScale (1.0f);
}

void styleSlider (juce::Slider& s)
{
    s.setColour (juce::Slider::trackColourId, theme::accent);
    s.setColour (juce::Slider::textBoxTextColourId, theme::text);
    s.setColour (juce::Slider::textBoxBackgroundColourId, theme::panelRaised);
    s.setColour (juce::Slider::textBoxOutlineColourId, theme::outline);
}

void styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, theme::panelRaised);
    c.setColour (juce::ComboBox::textColourId, theme::text);
    c.setColour (juce::ComboBox::outlineColourId, theme::outline);
    c.setColour (juce::ComboBox::arrowColourId, theme::textDim);
}

void styleButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, theme::panelRaised);
    b.setColour (juce::TextButton::textColourOffId, theme::text);
}

void styleToggle (juce::ToggleButton& t)
{
    t.setColour (juce::ToggleButton::textColourId, theme::text);
    t.setColour (juce::ToggleButton::tickColourId, theme::accent);
    t.setColour (juce::ToggleButton::tickDisabledColourId, theme::textDim);
}

juce::String msString (int samples, double sr)
{
    return sr > 0 ? juce::String ((double) samples * 1000.0 / sr, 1) + " ms" : juce::String ("n/a");
}

// ============================================================================================
// Audio
// ============================================================================================
class AudioTab : public juce::Component, private juce::Timer {
public:
    explicit AudioTab (EngineController& c)
        : controller (c),
          selector (c.deviceManager(), 0, 256, 0, 256, false, false, true, false)
    {
        addAndMakeVisible (selector);
        styleLabel (latencyLabel, {});
        addAndMakeVisible (latencyLabel);
        styleLabel (offsetLabel, "Recording latency offset (samples)");
        addAndMakeVisible (offsetLabel);

        offsetSlider.setRange (-2048, 2048, 1);
        offsetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        offsetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        offsetSlider.setValue (controller.session().transport.recordLatencyOffset, juce::dontSendNotification);
        offsetSlider.setDoubleClickReturnValue (true, 0);
        styleSlider (offsetSlider);
        offsetSlider.onValueChange = [this] { controller.setRecordLatencyOffset ((int) offsetSlider.getValue()); };
        addAndMakeVisible (offsetSlider);

        styleLabel (help, "Recorded loops are shifted earlier by the measured round-trip latency so they line up "
                          "with what you heard. Use the offset to fine-tune.", true, 13.0f);
        help.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (help);

        updateLatency();
        startTimerHz (4);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto bottom = r.removeFromBottom (130);
        selector.setBounds (r);
        latencyLabel.setBounds (bottom.removeFromTop (24));
        bottom.removeFromTop (6);
        offsetLabel.setBounds (bottom.removeFromTop (22));
        offsetSlider.setBounds (bottom.removeFromTop (28));
        bottom.removeFromTop (6);
        help.setBounds (bottom);
    }

private:
    void timerCallback() override { updateLatency(); }

    void updateLatency()
    {
        auto t = controller.transportStatus();
        latencyLabel.setText ("Latency: input " + msString (t.latencyIn, t.sampleRate) + "  |  output "
                                  + msString (t.latencyOut, t.sampleRate) + "  |  round trip "
                                  + msString (t.latencyIn + t.latencyOut, t.sampleRate)
                                  + "  (" + juce::String (t.latencyIn + t.latencyOut) + " samples, buffer "
                                  + juce::String (t.blockSize) + ")",
                              juce::dontSendNotification);
    }

    EngineController& controller;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label latencyLabel, offsetLabel, help;
    juce::Slider offsetSlider;
};

// ============================================================================================
// MIDI devices
// ============================================================================================
class MidiTab : public juce::Component, private juce::Timer {
public:
    explicit MidiTab (EngineController& c) : controller (c)
    {
        styleLabel (title, "MIDI input devices", false, 16.0f);
        addAndMakeVisible (title);
        styleLabel (empty, "No MIDI input devices found.", true);
        addAndMakeVisible (empty);
        rebuild();
        startTimer (1000);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        title.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
        empty.setBounds (r.removeFromTop (24));
        for (auto& t : toggles)
            t->setBounds (r.removeFromTop (30));
    }

private:
    void timerCallback() override
    {
        auto devs = juce::MidiInput::getAvailableDevices();
        bool same = devs.size() == (int) ids.size();
        for (int i = 0; same && i < devs.size(); ++i) same = ids[(size_t) i] == devs[i].identifier;
        if (! same) { rebuild(); return; }
        for (size_t i = 0; i < toggles.size(); ++i)
        {
            const bool en = controller.deviceManager().isMidiInputDeviceEnabled (ids[i]);
            if (toggles[i]->getToggleState() != en) toggles[i]->setToggleState (en, juce::dontSendNotification);
        }
    }

    void rebuild()
    {
        toggles.clear();
        ids.clear();
        for (auto& d : juce::MidiInput::getAvailableDevices())
        {
            auto t = std::make_unique<juce::ToggleButton> (d.name);
            styleToggle (*t);
            auto id = d.identifier;
            t->setToggleState (controller.deviceManager().isMidiInputDeviceEnabled (id), juce::dontSendNotification);
            auto* raw = t.get();
            t->onClick = [this, id, raw] { controller.deviceManager().setMidiInputDeviceEnabled (id, raw->getToggleState()); };
            addAndMakeVisible (*t);
            toggles.push_back (std::move (t));
            ids.push_back (id);
        }
        empty.setVisible (toggles.empty());
        resized();
    }

    EngineController& controller;
    juce::Label title, empty;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
    std::vector<juce::String> ids;
};

// ============================================================================================
// Controller mappings
// ============================================================================================
class ControllersTab : public juce::Component, private juce::ListBoxModel, private juce::Timer {
public:
    ControllersTab (EngineController& c, MidiMapping& m) : controller (c), mapping (m)
    {
        list.setModel (this);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, theme::panel);
        list.setColour (juce::ListBox::outlineColourId, theme::outline);
        list.setOutlineThickness (1);
        addAndMakeVisible (list);

        styleLabel (header, "Mappings", false, 16.0f);
        addAndMakeVisible (header);

        styleButton (clearBtn);
        clearBtn.setButtonText ("Clear All");
        clearBtn.onClick = [this] { mapping.clearAll(); refresh(); };
        addAndMakeVisible (clearBtn);

        struct Item { const char* label; MappingTarget::Kind kind; };
        const Item items[] = { { "Learn Play/Stop", MappingTarget::Kind::PlayStop },
                               { "Learn Record selected loop", MappingTarget::Kind::Record },
                               { "Learn Stop All", MappingTarget::Kind::StopAll },
                               { "Learn Tap Tempo", MappingTarget::Kind::TapTempo } };
        for (int i = 0; i < 4; ++i)
        {
            auto& b = learnButtons[(size_t) i];
            styleButton (b);
            b.setButtonText (items[i].label);
            auto kind = items[i].kind;
            b.onClick = [this, i, kind]
            {
                if (mapping.isLearning() && activeLearn == i) { mapping.cancelLearn(); activeLearn = -1; }
                else
                {
                    MappingTarget t; t.kind = kind;
                    mapping.startLearn (t);
                    activeLearn = i;
                }
                updateButtons();
            };
            addAndMakeVisible (b);
        }

        styleLabel (status, {}, false);
        status.setColour (juce::Label::textColourId, theme::armed);
        addAndMakeVisible (status);

        styleLabel (tip, "Global actions can be learned here; per-loop triggers can be learned from a loop's "
                         "right-click menu.", true, 13.0f);
        tip.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (tip);

        refresh();
        startTimerHz (5);
    }

    ~ControllersTab() override
    {
        if (activeLearn >= 0) mapping.cancelLearn();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        auto top = r.removeFromTop (28);
        clearBtn.setBounds (top.removeFromRight (90));
        header.setBounds (top);
        r.removeFromTop (6);
        auto bottom = r.removeFromBottom (110);
        list.setBounds (r);
        bottom.removeFromTop (8);
        auto row = bottom.removeFromTop (30);
        const int w = row.getWidth() / 4;
        for (auto& b : learnButtons) b.setBounds (row.removeFromLeft (w).reduced (2, 0));
        bottom.removeFromTop (6);
        status.setBounds (bottom.removeFromTop (22));
        tip.setBounds (bottom);
    }

private:
    // ListBoxModel
    int getNumRows() override { return (int) mapping.mappings().size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= getNumRows()) return;
        g.setColour (row % 2 ? theme::panel : theme::panelRaised);
        g.fillRect (0, 0, w, h);
        auto& m = mapping.mappings()[(size_t) row];
        auto dev = m.deviceId.isEmpty() ? juce::String ("Any") : deviceName (m.deviceId);
        g.setFont (theme::font (13.0f));
        g.setColour (theme::text);
        g.drawText (m.description, 8, 0, w - 420, h, juce::Justification::centredLeft, true);
        g.setColour (theme::textDim);
        g.drawText (dev, w - 412, 0, 110, h, juce::Justification::centredLeft, true);
        g.drawText ("Ch " + juce::String (m.channel), w - 298, 0, 44, h, juce::Justification::centredLeft);
        g.drawText ((m.isCC ? "CC " : "Note ") + juce::String (m.number), w - 250, 0, 70, h, juce::Justification::centredLeft);
    }

    juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override
    {
        if (row < 0 || row >= getNumRows()) { delete existing; return nullptr; }
        auto* rc = dynamic_cast<RowButton*> (existing);
        if (rc == nullptr)
        {
            delete existing;
            rc = new RowButton();
            styleButton (rc->button);
        }
        rc->button.onClick = [this, row] { mapping.removeMapping (row); refresh(); };
        return rc;
    }

    // Transparent row overlay: only the Remove button at the right edge is interactive.
    struct RowButton : juce::Component {
        RowButton() { addAndMakeVisible (button); button.setButtonText ("Remove"); setInterceptsMouseClicks (false, true); }
        void resized() override { button.setBounds (getLocalBounds().removeFromRight (80).reduced (2, 3)); }
        juce::TextButton button;
    };

    // Row components fill the entire row by default; keep the button small via a wrapper layout.
    void listBoxItemClicked (int, const juce::MouseEvent&) override {}

    static juce::String deviceName (const juce::String& id)
    {
        for (auto& d : juce::MidiInput::getAvailableDevices())
            if (d.identifier == id) return d.name;
        return "(disconnected)";
    }

    void timerCallback() override
    {
        juce::String sig = juce::String (mapping.mappings().size()) + (mapping.isLearning() ? "L" : "");
        for (auto& m : mapping.mappings()) sig << m.description << m.number << m.channel;
        if (sig != lastSig) { lastSig = sig; refresh(); }
        if (! mapping.isLearning() && activeLearn >= 0) { activeLearn = -1; updateButtons(); }
    }

    void refresh()
    {
        list.updateContent();
        list.repaint();
        updateButtons();
    }

    void updateButtons()
    {
        const bool learning = mapping.isLearning();
        status.setText (learning ? "Waiting for MIDI... (move a knob or press a key; click the button again to cancel)" : juce::String(),
                        juce::dontSendNotification);
        const char* labels[] = { "Learn Play/Stop", "Learn Record selected loop", "Learn Stop All", "Learn Tap Tempo" };
        for (int i = 0; i < 4; ++i)
        {
            const bool active = learning && activeLearn == i;
            learnButtons[(size_t) i].setButtonText (active ? "Waiting for MIDI..." : labels[i]);
            learnButtons[(size_t) i].setColour (juce::TextButton::buttonColourId, active ? theme::armed.darker (0.6f) : theme::panelRaised);
        }
        clearBtn.setEnabled (! mapping.mappings().empty());
    }

    EngineController& controller;
    MidiMapping& mapping;
    juce::ListBox list;
    juce::Label header, status, tip;
    juce::TextButton clearBtn;
    std::array<juce::TextButton, 4> learnButtons;
    int activeLearn = -1;
    juce::String lastSig;
};

// ============================================================================================
// Plug-ins
// ============================================================================================
class PluginsTab : public juce::Component {
public:
    explicit PluginsTab (EngineController& c)
        : controller (c),
          progressBar (progress),
          pluginList (c.pluginHost().formats(), c.pluginHost().knownPlugins(),
                      PluginHost::appDataDir().getChildFile ("scan-deadmanspedal.txt"), nullptr)
    {
        styleLabel (countLabel, {}, false, 15.0f);
        addAndMakeVisible (countLabel);
        styleButton (scanBtn);
        scanBtn.setButtonText ("Scan for Plug-ins");
        scanBtn.onClick = [this] { startScan(); };
        addAndMakeVisible (scanBtn);
        styleButton (cancelBtn);
        cancelBtn.setButtonText ("Cancel");
        cancelBtn.onClick = [this] { controller.pluginHost().cancelScan(); };
        addChildComponent (cancelBtn);
        styleLabel (progressText, {}, true, 13.0f);
        addChildComponent (progressText);
        addChildComponent (progressBar);
        addAndMakeVisible (pluginList);
        controller.pluginHost().knownPlugins().addChangeListener (&changeRelay);
        changeRelay.owner = this;
        updateCount();
        if (controller.pluginHost().isScanning()) setScanning (true);
    }

    ~PluginsTab() override
    {
        controller.pluginHost().knownPlugins().removeChangeListener (&changeRelay);
        controller.pluginHost().saveKnownList();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        auto top = r.removeFromTop (30);
        cancelBtn.setBounds (top.removeFromRight (80));
        top.removeFromRight (6);
        scanBtn.setBounds (top.removeFromRight (140));
        countLabel.setBounds (top);
        r.removeFromTop (6);
        auto prog = r.removeFromTop (22);
        progressBar.setBounds (prog.removeFromRight (180));
        progressText.setBounds (prog);
        r.removeFromTop (8);
        pluginList.setBounds (r);
    }

private:
    struct Relay : juce::ChangeListener {
        PluginsTab* owner = nullptr;
        void changeListenerCallback (juce::ChangeBroadcaster*) override { if (owner) owner->updateCount(); }
    };

    void updateCount()
    {
        countLabel.setText (juce::String (controller.pluginHost().knownPlugins().getNumTypes()) + " plug-ins known",
                            juce::dontSendNotification);
    }

    void setScanning (bool s)
    {
        scanBtn.setEnabled (! s);
        cancelBtn.setVisible (s);
        progressBar.setVisible (s);
        progressText.setVisible (s);
    }

    void startScan()
    {
        progress = 0;
        progressText.setText ("Scanning...", juce::dontSendNotification);
        setScanning (true);
        juce::Component::SafePointer<PluginsTab> safe (this);
        controller.pluginHost().startScan (
            [safe] (float p, const juce::String& current)
            {
                if (safe == nullptr) return;
                safe->progress = (double) p;
                safe->progressText.setText ("Scanning: " + current, juce::dontSendNotification);
                safe->updateCount();
            },
            [safe]
            {
                if (safe == nullptr) return;
                safe->setScanning (false);
                safe->updateCount();
                safe->controller.pluginHost().saveKnownList();
            });
    }

    EngineController& controller;
    juce::Label countLabel, progressText;
    juce::TextButton scanBtn, cancelBtn;
    double progress = 0;
    juce::ProgressBar progressBar;
    juce::PluginListComponent pluginList;
    Relay changeRelay;
};

// ============================================================================================
// Default loop settings
// ============================================================================================
class LoopsTab : public juce::Component {
public:
    explicit LoopsTab (EngineController& c) : controller (c)
    {
        auto& tr = controller.session().transport;
        const auto s = tr.defaultLoopSettings;

        addRow (startQ, "Start quantise");
        addRow (stopQ, "Stop quantise");
        for (auto* cb : { &startQ, &stopQ })
        {
            cb->addItem ("None", 1); cb->addItem ("Beat", 2); cb->addItem ("Bar", 3);
            cb->addItem ("2 Bars", 4); cb->addItem ("4 Bars", 5); cb->addItem ("Master loop", 6);
        }
        startQ.setSelectedId ((int) s.startQuantize + 1, juce::dontSendNotification);
        stopQ.setSelectedId ((int) s.stopQuantize + 1, juce::dontSendNotification);

        addRow (lengthMode, "Record length");
        lengthMode.addItem ("Master", 1); lengthMode.addItem ("Bars", 2); lengthMode.addItem ("Free", 3);
        lengthMode.setSelectedId ((int) s.lengthMode + 1, juce::dontSendNotification);

        addRow (bars, "Record bars");
        bars.setRange (1, 64, 1);
        bars.setValue (s.recordBars, juce::dontSendNotification);

        addRow (afterRec, "After record");
        afterRec.addItem ("Play", 1); afterRec.addItem ("Overdub", 2); afterRec.addItem ("Stop", 3);
        afterRec.setSelectedId ((int) s.afterRecord + 1, juce::dontSendNotification);

        addRow (recMode, "Record mode");
        recMode.addItem ("Replace", 1); recMode.addItem ("Take Folder", 2); recMode.addItem ("Take Folder + Cycle", 3);
        recMode.setSelectedId (s.recordMode == RecordMode::Replace ? 1 : (s.cycleTakes ? 3 : 2), juce::dontSendNotification);

        addRow (feedback, "Overdub feedback");
        feedback.setRange (0.0, 1.0, 0.01);
        feedback.setValue (s.overdubFeedback, juce::dontSendNotification);

        addRow (masterBars, "Master bars");
        masterBars.setRange (1, 64, 1);
        masterBars.setValue (tr.masterBars, juce::dontSendNotification);

        addRow (metroVol, "Metronome volume");
        metroVol.setRange (0.0, 1.0, 0.01);
        metroVol.setValue (tr.metronomeVolume, juce::dontSendNotification);

        for (auto* cb : { &startQ, &stopQ, &lengthMode, &afterRec, &recMode })
            cb->onChange = [this] { pushSettings(); };
        bars.onValueChange = feedback.onValueChange = [this] { pushSettings(); };
        masterBars.onValueChange = [this] { controller.setMasterBars ((int) masterBars.getValue()); };
        metroVol.onValueChange = [this]
        {
            controller.setMetronome (controller.session().transport.metronome, (float) metroVol.getValue());
        };
        updateEnablement();

        styleLabel (note, "These settings apply to newly created loops.", true, 13.0f);
        addAndMakeVisible (note);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        for (auto& row : rows)
        {
            auto line = r.removeFromTop (34);
            row.label->setBounds (line.removeFromLeft (170));
            row.comp->setBounds (line.reduced (0, 3));
        }
        r.removeFromTop (10);
        note.setBounds (r.removeFromTop (22));
    }

private:
    struct Row { std::unique_ptr<juce::Label> label; juce::Component* comp; };

    void addRow (juce::ComboBox& cb, const juce::String& name)
    {
        styleCombo (cb);
        addRowImpl (cb, name);
    }

    void addRow (juce::Slider& s, const juce::String& name)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
        styleSlider (s);
        addRowImpl (s, name);
    }

    void addRowImpl (juce::Component& c, const juce::String& name)
    {
        auto l = std::make_unique<juce::Label>();
        styleLabel (*l, name);
        addAndMakeVisible (*l);
        addAndMakeVisible (c);
        rows.push_back ({ std::move (l), &c });
    }

    void updateEnablement() { bars.setEnabled (lengthMode.getSelectedId() == 2); }

    void pushSettings()
    {
        LoopSettings s = controller.session().transport.defaultLoopSettings;
        s.startQuantize = (Quantize) (startQ.getSelectedId() - 1);
        s.stopQuantize = (Quantize) (stopQ.getSelectedId() - 1);
        s.lengthMode = (RecordLength) (lengthMode.getSelectedId() - 1);
        s.recordBars = (int16_t) bars.getValue();
        s.afterRecord = (AfterRecord) (afterRec.getSelectedId() - 1);
        s.recordMode = recMode.getSelectedId() == 1 ? RecordMode::Replace : RecordMode::TakeFolder;
        s.cycleTakes = recMode.getSelectedId() == 3;
        s.overdubFeedback = (float) feedback.getValue();
        controller.setDefaultLoopSettings (s);
        updateEnablement();
    }

    EngineController& controller;
    juce::ComboBox startQ, stopQ, lengthMode, afterRec, recMode;
    juce::Slider bars, feedback, masterBars, metroVol;
    juce::Label note;
    std::vector<Row> rows;
};

// ============================================================================================
// Window
// ============================================================================================
class SettingsTabs : public juce::TabbedComponent {
public:
    SettingsTabs (EngineController& c) : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop), controller (c) {}
    void currentTabChanged (int, const juce::String&) override { controller.pluginHost().saveKnownList(); }
private:
    EngineController& controller;
};

class SettingsWindow : public juce::DocumentWindow, private juce::DeletedAtShutdown {
public:
    SettingsWindow (EngineController& c, MidiMapping& m)
        : juce::DocumentWindow ("Settings", theme::background, juce::DocumentWindow::closeButton), controller (c)
    {
        setUsingNativeTitleBar (true);
        setBackgroundColour (theme::background);
        auto tabs = std::make_unique<SettingsTabs> (c);
        tabs->setTabBarDepth (32);
        tabs->setOutline (0);
        const auto bg = theme::background;
        tabs->addTab ("Audio", bg, new AudioTab (c), true);
        tabs->addTab ("MIDI", bg, new MidiTab (c), true);
        tabs->addTab ("Controllers", bg, new ControllersTab (c, m), true);
        tabs->addTab ("Plug-ins", bg, new PluginsTab (c), true);
        tabs->addTab ("Loops", bg, new LoopsTab (c), true);
        tabs->setSize (700, 720);
        setContentOwned (tabs.release(), true);
        setResizable (true, false);
        setResizeLimits (560, 520, 1200, 1100);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~SettingsWindow() override { controller.pluginHost().saveKnownList(); }

    void closeButtonPressed() override
    {
        controller.pluginHost().saveKnownList();
        setVisible (false);
        juce::Component::SafePointer<SettingsWindow> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) delete safe.getComponent(); });
    }

private:
    EngineController& controller;
};

juce::Component::SafePointer<SettingsWindow> instance;

} // namespace

void showSettingsWindow (EngineController& c, MidiMapping& m)
{
    if (instance != nullptr)
    {
        instance->setVisible (true);
        instance->toFront (true);
        return;
    }
    instance = new SettingsWindow (c, m);
}

} // namespace el
