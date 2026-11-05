#include "UI/ChannelStrip.h"
#include "UI/GraphicEqEditor.h"
#include "UI/Theme.h"
#include "Core/ParamMapping.h"
#include <cmath>

#if __has_include("UI/PluginBrowser.h")
 #include "UI/PluginBrowser.h"
#else
namespace el {
void showPluginMenu (EngineController&, Id trackId, FxChain chain, int insertIndex, juce::Component* target,
                     std::function<void (Id)> onInserted);
}
#endif

namespace el {

namespace {
    juce::String signedDb (float db)
    {
        return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    }
    juce::String volumeText (double n)
    {
        const float db = pm::volumeToDb ((float) n);
        return db <= -100.0f ? juce::String ("-inf dB") : signedDb (db);
    }
    juce::String panText (double n)
    {
        const int p = (int) std::lround (std::abs (pm::panToBipolar ((float) n)) * 50.0f);
        return p == 0 ? juce::String ("C") : (pm::panToBipolar ((float) n) < 0 ? "L" : "R") + juce::String (p);
    }
    constexpr int kMargin = 4;
}

// ---- a slider bound to a normalised track parameter ------------------------------------------
class StripKnob : public juce::Slider {
public:
    StripKnob (EngineController& c, Id track, ParamTarget t, float defaultValue,
               std::function<juce::String (double)> fmt, juce::Slider::SliderStyle style, bool textBelow)
        : controller (c), trackId (track), target (t)
    {
        setRange (0.0, 1.0, 0.0);
        setSliderStyle (style);
        textFromValueFunction = std::move (fmt);
        if (textBelow)
        {
            setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 13);
            setTextBoxIsEditable (false);
            setColour (juce::Slider::textBoxTextColourId, theme::textDim);
            setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        }
        else
        {
            setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            setPopupDisplayEnabled (true, true, nullptr);
        }
        if (style != juce::Slider::LinearVertical)
        {
            setScrollWheelEnabled (false);
            setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        }
        setDoubleClickReturnValue (true, defaultValue);
        setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, theme::outline);
        setColour (juce::Slider::thumbColourId, theme::text);
        setColour (juce::Slider::trackColourId, theme::accent.darker (0.2f));
        setColour (juce::Slider::backgroundColourId, theme::panelRaised);
        onDragStart = [this] { controller.beginParamGesture (trackId, target); };
        onDragEnd = [this] { controller.endParamGesture (trackId, target); };
        onValueChange = [this] { controller.setParam (trackId, target, (float) getValue()); };
        setValue (controller.getParam (trackId, target), juce::dontSendNotification);
    }

    void refresh()
    {
        if (isMouseButtonDown()) return;
        const float v = controller.getParam (trackId, target);
        if (std::abs ((float) getValue() - v) > 1.0e-4f)
            setValue (v, juce::dontSendNotification);
    }

private:
    EngineController& controller;
    Id trackId;
    ParamTarget target;
};

// ---- the scrolling content (draws section titles) --------------------------------------------
class StripContent : public juce::Component {
public:
    struct Title { juce::String text; int y; };
    std::vector<Title> titles;
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (9.0f, true));
        for (auto& t : titles)
            g.drawText (t.text, kMargin + 2, t.y, getWidth() - 2 * kMargin, 11, juce::Justification::centredLeft);
    }
};

// ---- plugin slot row ---------------------------------------------------------------------------
class StripSlotRow : public juce::Component {
public:
    StripSlotRow (ChannelStrip& s, FxChain c, int idx, PluginSlotPtr p)
        : strip (s), chain (c), index (idx), slot (std::move (p))
    {
        lastBypass = slot && slot->bypassed.load();
        setRepaintsOnMouseActivity (true);
    }

    void refresh()
    {
        if (slot && slot->bypassed.load() != lastBypass) { lastBypass = ! lastBypass; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (isMouseOver() ? theme::outline : theme::panelRaised);
        g.fillRoundedRectangle (b, 3.0f);
        const bool hasLed = slot && chain != FxChain::Instrument;
        auto textArea = getLocalBounds().withTrimmedLeft (5).withTrimmedRight (hasLed ? 17 : 5);
        if (! slot)
        {
            g.setColour (theme::textDim);
            g.setFont (theme::font (11.0f));
            g.drawText ("+ Instrument", textArea, juce::Justification::centredLeft);
            return;
        }
        g.setColour (slot->instance == nullptr ? theme::record : (lastBypass ? theme::textDim : theme::text));
        g.setFont (theme::font (11.0f));
        g.drawFittedText (slot->name(), textArea, juce::Justification::centredLeft, 1);
        if (hasLed)
        {
            const auto led = ledBounds().toFloat();
            g.setColour (lastBypass ? theme::outline : theme::play);
            g.fillEllipse (led);
            g.setColour (theme::background);
            g.drawEllipse (led, 1.0f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        auto& c = strip.controller;
        if (e.mods.isPopupMenu())
        {
            if (slot) strip.showSlotMenu (chain, index, slot, this);
            return;
        }
        if (! slot) { strip.addPluginMenu (chain, this); return; }
        if (chain != FxChain::Instrument && ledBounds().expanded (4).contains (e.getPosition()))
            c.setPluginBypass (slot->id, ! slot->bypassed.load());
        else
            c.showPluginEditor (slot->id);
    }

private:
    juce::Rectangle<int> ledBounds() const { return { getWidth() - 14, (getHeight() - 8) / 2, 8, 8 }; }
    ChannelStrip& strip;
    FxChain chain;
    int index;
    PluginSlotPtr slot;
    bool lastBypass = false;
};

// ---- send row ----------------------------------------------------------------------------------
class StripSendRow : public juce::Component {
public:
    StripSendRow (ChannelStrip& s, int idx, const SendSpec& spec, juce::String bus)
        : strip (s), index (idx), pre (spec.preFader), busName (std::move (bus)),
          knob (s.controller, s.trackId, { ParamKind::SendLevel, spec.busId, -1 }, 0.0f,
                [] (double n) { const float db = pm::volumeToDb ((float) n); return db <= -100.0f ? juce::String ("-inf dB") : signedDb (db); },
                juce::Slider::RotaryVerticalDrag, false)
    {
        addAndMakeVisible (knob);
        prePostButton.setClickingTogglesState (false);
        prePostButton.setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        prePostButton.setTooltip ("Pre/post fader send");
        updateButton();
        prePostButton.onClick = [this]
        {
            pre = ! pre;
            strip.controller.setSendPreFader (strip.trackId, index, pre);
            updateButton();
        };
        addAndMakeVisible (prePostButton);
        addMouseListener (this, true);
    }
    ~StripSendRow() override { removeMouseListener (this); }

    void refresh() { knob.refresh(); }

    void resized() override
    {
        auto b = getLocalBounds();
        prePostButton.setBounds (b.removeFromRight (28).reduced (0, 4));
        knob.setBounds (b.removeFromRight (26).reduced (1));
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (10.0f));
        g.drawFittedText (busName, 2, 0, getWidth() - 56, getHeight(), juce::Justification::centredLeft, 1);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && ! e.mouseWasDraggedSinceMouseDown()) strip.showSendMenu (index);
    }

private:
    void updateButton()
    {
        prePostButton.setButtonText (pre ? "PRE" : "POST");
    }
    ChannelStrip& strip;
    int index;
    bool pre;
    juce::String busName;
    StripKnob knob;
    juce::TextButton prePostButton;
};

// ---- ChannelStrip ------------------------------------------------------------------------------
ChannelStrip::ChannelStrip (EngineController& c, Id id)
    : controller (c), trackId (id), isMaster (id == kMasterId)
{
    content = std::make_unique<StripContent>();
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (false, false, true, false);
    addAndMakeVisible (viewport);

    nameLabel.setEditable (false, true, false);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (theme::font (12.0f, true));
    nameLabel.onTextChange = [this]
    {
        const auto n = nameLabel.getText().trim();
        if (n.isNotEmpty()) controller.setTrackName (trackId, n);
    };
    addAndMakeVisible (nameLabel);

    if (! isMaster)
        panKnob = std::make_unique<StripKnob> (controller, trackId, ParamTarget { ParamKind::Pan, kInvalidId, -1 }, 0.5f,
                                               panText, juce::Slider::RotaryVerticalDrag, true);
    fader = std::make_unique<StripKnob> (controller, trackId, ParamTarget { ParamKind::Volume, kInvalidId, -1 }, pm::kUnityVolume,
                                         volumeText, juce::Slider::LinearVertical, false);
    if (panKnob) addAndMakeVisible (*panKnob);
    addAndMakeVisible (*fader);
    dbLabel.setJustificationType (juce::Justification::centred);
    dbLabel.setFont (theme::font (11.0f));
    dbLabel.setColour (juce::Label::textColourId, theme::text);
    dbLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (dbLabel);
    addAndMakeVisible (meter);

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, theme::overdub);
    muteButton.setColour (juce::TextButton::textColourOnId, theme::background);
    muteButton.onClick = [this]
    {
        const ParamTarget tgt { ParamKind::Mute, kInvalidId, -1 };
        controller.beginParamGesture (trackId, tgt);
        controller.setParam (trackId, tgt, muteButton.getToggleState() ? 1.0f : 0.0f);
        controller.endParamGesture (trackId, tgt);
    };
    addAndMakeVisible (muteButton);

    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonOnColourId, theme::armed);
    soloButton.setColour (juce::TextButton::textColourOnId, theme::background);
    soloButton.onClick = [this]
    {
        if (auto t = track(); t && t->rt) t->rt->solo.store (soloButton.getToggleState());
    };
    addChildComponent (soloButton);

    monitorButton.setTooltip ("Auto: hear the input until a loop has been recorded, then mute it so the loop and live input don't double up");
    monitorButton.onClick = [this] { cycleMonitor(); };
    addChildComponent (monitorButton);

    outputButton.onClick = [this] { if (isMaster) showHardwareOutputMenu(); else showOutputMenu(); };
    addAndMakeVisible (outputButton);

    addMouseListener (this, true);
    rebuild();
    refreshValues();
}

ChannelStrip::~ChannelStrip()
{
    removeMouseListener (this);
    clearContent();
    viewport.setViewedComponent (nullptr, false);
}

void ChannelStrip::setSelected (bool s)
{
    if (selected != s) { selected = s; repaint(); }
}

void ChannelStrip::mouseDown (const juce::MouseEvent&)
{
    if (onSelect) onSelect (trackId);
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (selected ? theme::panelRaised : theme::panel);
    g.fillRect (b);
    g.setColour (colour);
    g.fillRect (b.removeFromBottom (5.0f));
    g.setColour (selected ? theme::accent : theme::outline);
    if (selected) g.drawRect (getLocalBounds(), 2);
    else g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void ChannelStrip::resized()
{
    auto b = getLocalBounds().reduced (kMargin, 0);
    b.removeFromBottom (5);
    nameLabel.setBounds (b.removeFromTop (22).reduced (0, 2));
    outputButton.setBounds (b.removeFromBottom (24).reduced (0, 2));
    if (monitorButton.isVisible()) monitorButton.setBounds (b.removeFromBottom (22).reduced (0, 2));
    auto ms = b.removeFromBottom (24).reduced (0, 2);
    muteButton.setBounds (soloButton.isVisible() ? ms.removeFromLeft (ms.getWidth() / 2 - 2) : ms);
    if (soloButton.isVisible()) { ms.removeFromLeft (4); soloButton.setBounds (ms); }

    const int panH = panKnob ? 56 : 0;
    const int fh = juce::jlimit (90, 230, b.getHeight() - 22 - panH - 170);
    faderArea = b.removeFromBottom (fh);
    dbLabel.setBounds (b.removeFromBottom (16));
    if (panKnob) panKnob->setBounds (b.removeFromBottom (panH));
    fader->setBounds (faderArea.removeFromLeft (34));
    faderArea.removeFromLeft (4);
    meter.setBounds (faderArea.removeFromLeft (24).reduced (0, 4));
    viewport.setBounds (getLocalBounds().withTop (b.getY()).withBottom (b.getBottom()));
}

juce::String ChannelStrip::makeSignature (const TrackModel& t) const
{
    juce::String s;
    s << (int) t.kind << '|' << t.name << '|' << juce::String::toHexString ((int) t.colour.getARGB()) << '|' << (int) t.outputBus << '|' << t.hardwareOutput;
    auto slotSig = [&s] (const PluginSlotPtr& p)
    {
        if (p) s << '[' << (int) p->id << p->name() << (p->instance != nullptr ? 1 : 0) << ']';
        else s << "[-]";
    };
    s << "|m"; for (auto& p : t.midiFx) slotSig (p);
    s << "|i"; slotSig (t.instrument);
    s << "|p"; for (auto& p : t.preFx) slotSig (p);
    s << "|o"; for (auto& p : t.postFx) slotSig (p);
    s << "|s";
    for (auto& sp : t.sends)
    {
        auto bus = controller.session().findTrack (sp.busId);
        s << (int) sp.busId << (sp.preFader ? 'p' : 'q') << (bus ? bus->name : juce::String()) << ',';
    }
    if (auto out = controller.session().findTrack (t.outputBus)) s << '|' << out->name;
    return s;
}

void ChannelStrip::updateHeader (const TrackModel& t)
{
    kind = t.kind;
    colour = t.colour;
    if (! nameLabel.isBeingEdited() && nameLabel.getText() != t.name)
        nameLabel.setText (t.name, juce::dontSendNotification);
    nameLabel.setColour (juce::Label::backgroundColourId, t.colour);
    nameLabel.setColour (juce::Label::textColourId, t.colour.contrasting (1.0f));
    nameLabel.setColour (juce::Label::textWhenEditingColourId, theme::text);
    nameLabel.setColour (juce::Label::backgroundWhenEditingColourId, theme::background);

    const bool showSolo = ! isMaster;
    const bool showMonitor = ! isMaster;
    if (soloButton.isVisible() != showSolo) soloButton.setVisible (showSolo);
    if (monitorButton.isVisible() != showMonitor) monitorButton.setVisible (showMonitor);

    if (isMaster)
        outputButton.setButtonText ("Out " + juce::String (t.hardwareOutput + 1) + "-" + juce::String (t.hardwareOutput + 2));
    else if (auto out = controller.session().findTrack (t.outputBus))
        outputButton.setButtonText ("> " + out->name);
    else
        outputButton.setButtonText ("> Master");
    outputButton.setTooltip (isMaster ? "Hardware output pair" : "Output destination");
    resized();
    repaint();
}

void ChannelStrip::clearContent()
{
    slotRows.clear();
    sendRows.clear();
    inputKnob = nullptr;
    eqThumb = nullptr;
    eqToggle = nullptr;
    if (content) { content->removeAllChildren(); content->titles.clear(); }
    owned.clear();
}

void ChannelStrip::rebuild()
{
    auto t = track();
    if (! t) return;
    updateHeader (*t);
    const auto sig = makeSignature (*t);
    if (sig == lastSignature) return;
    lastSignature = sig;
    buildContent (*t);
}

void ChannelStrip::buildContent (const TrackModel& t)
{
    const int scrollPos = viewport.getViewPositionY();
    clearContent();
    const int W = theme::stripWidth;
    const int rw = W - 2 * kMargin;
    int y = 2;

    auto add = [this] (auto comp) { auto* raw = comp.get(); content->addAndMakeVisible (*raw); owned.push_back (std::move (comp)); return raw; };
    auto title = [&] (const juce::String& s) { content->titles.push_back ({ s, y }); y += 12; };
    auto addButton = [&] (const juce::String& text, std::function<void (juce::Component*)> fn)
    {
        auto b = std::make_unique<juce::TextButton> (text);
        auto* raw = b.get();
        b->setColour (juce::TextButton::buttonColourId, theme::background);
        b->setColour (juce::TextButton::textColourOffId, theme::textDim);
        b->onClick = [raw, fn] { fn (raw); };
        add (std::move (b))->setBounds (kMargin, y, rw, 16);
        y += 20;
        return raw;
    };
    auto slotList = [&] (const juce::String& heading, FxChain chain, const std::vector<PluginSlotPtr>& list)
    {
        title (heading);
        for (int i = 0; i < (int) list.size(); ++i)
        {
            auto* row = add (std::make_unique<StripSlotRow> (*this, chain, i, list[(size_t) i]));
            row->setBounds (kMargin, y, rw, 20);
            slotRows.push_back (row);
            y += 22;
        }
        addButton ("+", [this, chain] (juce::Component* c) { addPluginMenu (chain, c); });
    };

    if (t.kind == TrackKind::Audio || t.kind == TrackKind::Bus)
    {
        title ("INPUT");
        inputKnob = add (std::make_unique<StripKnob> (controller, trackId, ParamTarget { ParamKind::InputGain, kInvalidId, -1 }, 0.5f,
                                                      [] (double n) { return "In " + signedDb (pm::inputGainToDb ((float) n)); },
                                                      juce::Slider::RotaryVerticalDrag, true));
        inputKnob->setBounds (kMargin, y, rw, 52);
        y += 54;
    }
    if (t.kind == TrackKind::Instrument)
    {
        title ("INSTRUMENT");
        auto* row = add (std::make_unique<StripSlotRow> (*this, FxChain::Instrument, 0, t.instrument));
        row->setBounds (kMargin, y, rw, 22);
        slotRows.push_back (row);
        y += 26;
        slotList ("MIDI FX", FxChain::MidiFx, t.midiFx);
    }
    slotList ("PRE FADER", FxChain::Pre, t.preFx);

    title ("EQ");
    {
        auto thumb = std::make_unique<GraphicEqEditor> (controller, trackId, true);
        thumb->onClick = [this] { openEqEditor(); };
        eqThumb = add (std::move (thumb));
        eqThumb->setBounds (kMargin, y, rw, 34);
        y += 37;
        auto tb = std::make_unique<juce::TextButton> ("EQ On");
        eqToggle = tb.get();
        tb->setClickingTogglesState (true);
        tb->setColour (juce::TextButton::buttonColourId, theme::background);
        tb->setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.4f));
        tb->onClick = [this]
        {
            if (auto tr = track(); tr && tr->rt) tr->rt->eqEnabled.store (eqToggle->getToggleState());
        };
        add (std::move (tb))->setBounds (kMargin, y, rw, 16);
        y += 20;
    }

    if (! isMaster)
    {
        title ("SENDS");
        for (int i = 0; i < (int) t.sends.size(); ++i)
        {
            auto bus = controller.session().findTrack (t.sends[(size_t) i].busId);
            auto* row = add (std::make_unique<StripSendRow> (*this, i, t.sends[(size_t) i], bus ? bus->name : juce::String ("?")));
            row->setBounds (kMargin, y, rw, 26);
            sendRows.push_back (row);
            y += 28;
        }
        addButton ("+ Send", [this] (juce::Component* c) { showAddSendMenu (c); });
    }

    slotList ("POST FADER", FxChain::Post, t.postFx);

    content->setSize (W, y + 4);
    viewport.setViewPosition (0, scrollPos);
    refreshValues();
}

void ChannelStrip::refreshValues()
{
    auto t = track();
    if (! t || ! t->rt) return;
    if (panKnob) panKnob->refresh();
    fader->refresh();
    dbLabel.setText (volumeText (fader->getValue()), juce::dontSendNotification);
    if (inputKnob) inputKnob->refresh();
    for (auto* r : slotRows) r->refresh();
    for (auto* r : sendRows) r->refresh();
    if (eqThumb) eqThumb->refresh();
    if (eqToggle) eqToggle->setToggleState (t->rt->eqEnabled.load(), juce::dontSendNotification);
    muteButton.setToggleState (controller.getParam (trackId, { ParamKind::Mute, kInvalidId, -1 }) > 0.5f, juce::dontSendNotification);
    soloButton.setToggleState (t->rt->solo.load(), juce::dontSendNotification);
    updateMonitorText (t->rt->monitor.load());
    const auto m = controller.meter (trackId);
    meter.setLevels (m.left, m.right);
}

void ChannelStrip::updateMonitorText (MonitorMode m)
{
    const char* txt = m == MonitorMode::Auto ? "Monitor: Auto" : m == MonitorMode::On ? "Monitor: On" : "Monitor: Off";
    if (monitorButton.getButtonText() != txt) monitorButton.setButtonText (txt);
    monitorButton.setColour (juce::TextButton::buttonColourId, m == MonitorMode::On ? theme::play.darker (0.5f) : theme::panelRaised);
}

void ChannelStrip::cycleMonitor()
{
    auto t = track();
    if (! t || ! t->rt) return;
    const auto cur = t->rt->monitor.load();
    const auto next = cur == MonitorMode::Auto ? MonitorMode::On : cur == MonitorMode::On ? MonitorMode::Off : MonitorMode::Auto;
    controller.setMonitorMode (trackId, next);
    updateMonitorText (next);
}

void ChannelStrip::alertRouting (const juce::String& msg)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Routing", msg);
}

void ChannelStrip::addPluginMenu (FxChain chain, juce::Component* target)
{
    showPluginMenu (controller, trackId, chain, -1, target, {});
}

void ChannelStrip::showSlotMenu (FxChain chain, int index, PluginSlotPtr slot, juce::Component* target)
{
    auto t = track();
    if (! t || ! slot) return;
    const int count = chain == FxChain::MidiFx ? (int) t->midiFx.size()
                    : chain == FxChain::Pre ? (int) t->preFx.size()
                    : chain == FxChain::Post ? (int) t->postFx.size() : 1;
    const bool movable = chain == FxChain::Pre || chain == FxChain::Post;
    juce::PopupMenu m;
    m.addItem (1, "Open editor");
    if (chain == FxChain::Instrument) m.addItem (2, "Replace...");
    if (onPluginPresetMenu) m.addItem (3, "Presets...");
    if (chain != FxChain::Instrument)
    {
        m.addSeparator();
        m.addItem (4, "Move up", index > 0);
        m.addItem (5, "Move down", index < count - 1);
        if (movable) m.addItem (6, chain == FxChain::Pre ? "Move to post-fader" : "Move to pre-fader");
    }
    m.addSeparator();
    m.addItem (7, "Remove");

    juce::Component::SafePointer<ChannelStrip> safe (this);
    juce::Component::SafePointer<juce::Component> tgt (target);
    const Id slotId = slot->id;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe, tgt, slotId, chain, index, t] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& c = safe->controller;
        switch (r)
        {
            case 1: c.showPluginEditor (slotId); break;
            case 2: showPluginMenu (c, safe->trackId, FxChain::Instrument, -1, tgt.getComponent(), {}); break;
            case 3: if (safe->onPluginPresetMenu) safe->onPluginPresetMenu (slotId, tgt.getComponent()); break;
            case 4: c.movePlugin (slotId, chain, index - 1); break;
            case 5: c.movePlugin (slotId, chain, index + 1); break;
            case 6:
            {
                const auto other = chain == FxChain::Pre ? FxChain::Post : FxChain::Pre;
                c.movePlugin (slotId, other, (int) (other == FxChain::Pre ? t->preFx.size() : t->postFx.size()));
                break;
            }
            case 7: c.removePlugin (slotId); break;
            default: break;
        }
    });
}

void ChannelStrip::showAddSendMenu (juce::Component* target)
{
    auto t = track();
    if (! t) return;
    juce::PopupMenu m;
    int n = 0;
    for (auto& bus : controller.session().buses())
    {
        if (bus->id == trackId) continue;
        const bool used = std::any_of (t->sends.begin(), t->sends.end(), [&] (const SendSpec& s) { return s.busId == bus->id; });
        m.addItem ((int) bus->id, bus->name, ! used);
        ++n;
    }
    if (n == 0) m.addItem (-1, "No buses (add a bus first)", false);
    juce::Component::SafePointer<ChannelStrip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (safe->controller.addSend (safe->trackId, (Id) r, false) < 0)
            safe->alertRouting ("That send isn't possible: it would create a feedback loop, or the track has too many sends.");
    });
}

void ChannelStrip::showSendMenu (int sendIndex)
{
    juce::PopupMenu m;
    m.addItem (1, "Remove send");
    juce::Component::SafePointer<ChannelStrip> safe (this);
    m.showMenuAsync ({}, [safe, sendIndex] (int r)
    {
        if (safe != nullptr && r == 1) safe->controller.removeSend (safe->trackId, sendIndex);
    });
}

void ChannelStrip::showOutputMenu()
{
    auto t = track();
    if (! t) return;
    juce::PopupMenu m;
    m.addItem ((int) kMasterId, "Master", true, t->outputBus == kMasterId);
    for (auto& bus : controller.session().buses())
        if (bus->id != trackId)
            m.addItem ((int) bus->id, bus->name, true, t->outputBus == bus->id);
    juce::Component::SafePointer<ChannelStrip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (! safe->controller.setTrackOutput (safe->trackId, (Id) r))
            safe->alertRouting ("That output would create a feedback loop.");
    });
}

void ChannelStrip::showHardwareOutputMenu()
{
    auto t = track();
    if (! t) return;
    juce::StringArray names;
    if (auto* dev = controller.deviceManager().getCurrentAudioDevice())
        names = dev->getOutputChannelNames();
    juce::PopupMenu m;
    const int pairs = std::max (1, names.size() / 2);
    for (int p = 0; p < pairs; ++p)
    {
        const int first = p * 2;
        juce::String label = "Out " + juce::String (first + 1) + "-" + juce::String (first + 2);
        if (first + 1 < names.size()) label << "  (" << names[first] << " / " << names[first + 1] << ")";
        m.addItem (first + 1, label, true, t->hardwareOutput == first);
    }
    juce::Component::SafePointer<ChannelStrip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton), [safe] (int r)
    {
        if (safe != nullptr && r > 0) safe->controller.setMasterHardwareOutput (r - 1);
    });
}

void ChannelStrip::openEqEditor()
{
    if (eqThumb == nullptr) return;
    auto ed = std::make_unique<GraphicEqEditor> (controller, trackId, false);
    ed->setSize (380, 220);
    juce::CallOutBox::launchAsynchronously (std::move (ed), eqThumb->getScreenBounds(), nullptr);
}

} // namespace el
