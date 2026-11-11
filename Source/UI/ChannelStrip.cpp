#include "UI/ChannelStrip.h"
#include "UI/GraphicEqEditor.h"
#include "UI/PluginBrowser.h"
#include "UI/StripWidgets.h"
#include "UI/Theme.h"
#include "UI/TrackMenu.h"
#include "Core/ParamMapping.h"

namespace el {

namespace {
    constexpr int kM = 6;            // side margin
    constexpr int kHeaderH = 42;
    constexpr int kButtonsY = 46, kButtonsH = 24;
    constexpr int kTopH = 76;        // header + buttons
    constexpr int kSlotH = 24, kSlotGap = 3;
    const char* monitorName (MonitorMode m) { return m == MonitorMode::Auto ? "Auto" : m == MonitorMode::On ? "On" : "Off"; }

    // MIDI device names, cached: enumerating devices (CoreMIDI) on every strip refresh is slow.
    juce::String midiDeviceName (const juce::String& identifier)
    {
        static juce::Array<juce::MidiDeviceInfo> cache;
        static juce::uint32 cachedAt = 0;
        const auto now = juce::Time::getMillisecondCounter();
        if (cache.isEmpty() || now - cachedAt > 3000) { cache = juce::MidiInput::getAvailableDevices(); cachedAt = now; }
        for (auto& d : cache) if (d.identifier == identifier) return d.name;
        return identifier;
    }
}

void ChannelStrip::Content::paint (juce::Graphics& g)
{
    g.setFont (theme::font (11.0f, true));
    g.setColour (theme::textDim);
    for (auto& t : titles)
        g.drawText (t.first, kM + 2, t.second, getWidth() - 2 * kM, 13, juce::Justification::centredLeft);
}

ChannelStrip::ChannelStrip (EngineController& c, PresetManager& p, Id id)
    : controller (c), presets (p), trackId (id)
{
    icon = std::make_unique<PluginIconButton> (controller, trackId);
    icon->onPressed = [this] { if (onSelect) onSelect (trackId); };
    addAndMakeVisible (*icon);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);

    gainBar = std::make_unique<ValueBar>();
    bindValueBar (*gainBar, controller, trackId, { ParamKind::InputGain, kInvalidId, -1 });
    gainBar->label = "In"; gainBar->bipolar = true; gainBar->defaultValue = 0.5f;
    gainBar->format = [] (float v) { return stripfmt::signedDb (pm::inputGainToDb (v)); };
    gainBar->setTooltip ("Input gain (drag; double-click resets to 0 dB)");
    addAndMakeVisible (*gainBar);

    panBar = std::make_unique<ValueBar>();
    bindValueBar (*panBar, controller, trackId, { ParamKind::Pan, kInvalidId, -1 });
    panBar->label = "Pan"; panBar->bipolar = true; panBar->defaultValue = 0.5f;
    panBar->format = [] (float v) { return stripfmt::panText (v); };
    panBar->setTooltip ("Pan (drag; double-click centres)");
    addAndMakeVisible (*panBar);

    fader = std::make_unique<ValueBar>();
    bindValueBar (*fader, controller, trackId, { ParamKind::Volume, kInvalidId, -1 });
    fader->label = "Vol"; fader->vertical = true; fader->defaultValue = pm::kUnityVolume;
    fader->format = [] (float v) { return stripfmt::signedDb (pm::volumeToDb (v)); };
    fader->setTooltip ("Volume (drag up / down; shift = fine; double-click resets to 0 dB)");
    addAndMakeVisible (*fader);

    outputSlot = std::make_unique<MenuSlot>();
    outputSlot->onClick = [this] (juce::Component* c2) {
        if (isMaster) showHardwareOutputMenu (c2);
        else showOutputPicker (controller, trackId, c2);
    };
    addAndMakeVisible (*outputSlot);

    rebuild();
}

ChannelStrip::~ChannelStrip()
{
    if (muteGestureOpen) controller.endParamGesture (trackId, { ParamKind::Mute, kInvalidId, -1 });
    viewport.setViewedComponent (nullptr, false);
}

juce::String ChannelStrip::makeSignature (const TrackModel& t) const
{
    juce::String s;
    s << (int) t.kind << (loopPageTrack ? 'L' : '-') << '|';
    auto slotSig = [&s] (const PluginSlotPtr& p) {
        if (p) s << '[' << (int) p->id << p->name() << (p->instance != nullptr ? 1 : 0) << ']';
        else s << "[-]";
    };
    s << "m"; for (auto& p : t.midiFx) slotSig (p);
    s << "|i"; slotSig (t.instrument);
    s << "|p"; for (auto& p : t.preFx) slotSig (p);
    s << "|o"; for (auto& p : t.postFx) slotSig (p);
    s << "|s";
    for (auto& sp : t.sends) {
        auto bus = controller.session().findTrack (sp.busId);
        s << (int) sp.busId << (sp.preFader ? 'p' : 'q') << (bus ? bus->name + bus->colour.toString() : juce::String()) << ',';
    }
    return s;
}

void ChannelStrip::updateHeader (const TrackModel& t)
{
    kind = t.kind;
    isMaster = t.kind == TrackKind::Master;
    colour = t.colour;
    nameText = t.name;
    line1.clear();
    if (t.kind == TrackKind::Audio)
        line1 = t.inputChannel < 0 ? juce::String ("No input")
              : t.stereoInput ? "In " + juce::String (t.inputChannel + 1) + "/" + juce::String (t.inputChannel + 2)
                              : "In " + juce::String (t.inputChannel + 1);
    else if (t.kind == TrackKind::Instrument) {
        juce::String dev = "All MIDI";
        if (t.midiInputDevice.isNotEmpty()) dev = midiDeviceName (t.midiInputDevice);
        line1 = dev + (t.midiChannel > 0 ? " ch" + juce::String (t.midiChannel) : juce::String());
    }
    else if (t.kind == TrackKind::AuxOut) line1 = "Output " + juce::String (t.auxPair * 2 + 1) + "/" + juce::String (t.auxPair * 2 + 2);
    else if (t.kind == TrackKind::Bus) line1 = t.folderOf != kInvalidId ? "Sum folder" : "Bus";
    else line1 = "Master";

    if (isMaster) outputSlot->setText ("Out " + juce::String (t.hardwareOutput + 1) + "-" + juce::String (t.hardwareOutput + 2));
    else if (auto out = controller.session().findTrack (t.outputBus); out && out->kind != TrackKind::Master) outputSlot->setText (out->name);
    else outputSlot->setText ("Stereo Out");
    outputSlot->setTooltip (isMaster ? "Hardware output pair" : "Output: Stereo Out (Master) or a bus");
    gainBar->setVisible (! isMaster);
    icon->refresh();
    repaint();
}

void ChannelStrip::rebuild()
{
    auto t = track();
    if (! t) return;
    updateHeader (*t);
    const auto sig = makeSignature (*t);
    if (sig != lastSignature) { lastSignature = sig; buildContent (*t); }
    resized();
}

void ChannelStrip::buildContent (const TrackModel& t)
{
    const int scrollPos = viewport.getViewPositionY();
    content.removeAllChildren();
    content.titles.clear();
    owned.clear(); chips.clear(); sendBars.clear();
    eqThumb = nullptr; eqToggle = nullptr;

    const int rw = theme::stripWidth - 2 * kM - 6;   // leave room for the scrollbar
    int y = 2;
    auto add = [this] (auto comp) { auto* raw = comp.get(); content.addAndMakeVisible (*raw); owned.push_back (std::move (comp)); return raw; };
    auto title = [&] (const juce::String& s) { content.titles.push_back ({ s, y }); y += 14; };
    auto chip = [&] (FxChain chain, int idx, const PluginSlotPtr& slot) {
        auto* c = add (std::make_unique<FxChip> (controller, trackId, chain, idx, slot));
        c->onMenu = [this] (FxChip& f) { showSlotMenu (f); };
        c->setBounds (kM, y, rw, kSlotH);
        chips.push_back (c);
        y += kSlotH + kSlotGap;
    };
    auto empty = [&] (const juce::String& hint, const juce::String& tip, std::function<void (juce::Component*)> fn) {
        auto e = std::make_unique<EmptySlot> (hint);
        e->setTooltip (tip);
        e->onClick = std::move (fn);
        add (std::move (e))->setBounds (kM, y, rw, kSlotH);
        y += kSlotH + kSlotGap;
    };
    auto chain = [&] (const juce::String& heading, FxChain ch, const std::vector<PluginSlotPtr>& list, const juce::String& hint) {
        title (heading);
        for (int i = 0; i < (int) list.size(); ++i) chip (ch, i, list[(size_t) i]);
        empty (hint, "Add a plug-in here", [this, ch] (juce::Component* c) { showPluginMenu (controller, trackId, ch, -1, c); });
        y += 3;
    };

    if (t.kind == TrackKind::Instrument) {
        title ("Instrument");
        if (t.instrument != nullptr) chip (FxChain::Instrument, 0, t.instrument);
        else empty ("+ Instrument", "Choose an instrument", [this] (juce::Component* c) { showPluginMenu (controller, trackId, FxChain::Instrument, 0, c); });
        y += 3;
        chain ("MIDI FX", FxChain::MidiFx, t.midiFx, "+ MIDI FX");
    }
    if (! loopPageTrack) chain ("Audio FX", FxChain::Pre, t.preFx, "+ Audio FX");

    title ("EQ");
    {
        auto thumb = std::make_unique<GraphicEqEditor> (controller, trackId, true);
        thumb->onClick = [this] { openEqEditor(); };
        eqThumb = add (std::move (thumb));
        eqThumb->setBounds (kM, y, rw - 40, 30);
        auto tb = std::make_unique<juce::TextButton> ("On");
        eqToggle = tb.get();
        tb->setClickingTogglesState (true);
        tb->setTooltip ("EQ on / off");
        tb->setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        tb->setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.4f));
        tb->onClick = [this] { if (auto tr = track(); tr && tr->rt && eqToggle) tr->rt->eqEnabled.store (eqToggle->getToggleState()); };
        add (std::move (tb))->setBounds (kM + rw - 36, y + 3, 36, 24);
        y += 36;
    }

    if (! isMaster) {
        title ("Sends");
        for (int i = 0; i < (int) t.sends.size(); ++i) {
            const auto& sp = t.sends[(size_t) i];
            auto bus = controller.session().findTrack (sp.busId);
            auto bar = std::make_unique<ValueBar>();
            bindValueBar (*bar, controller, trackId, { ParamKind::SendLevel, sp.busId, -1 });
            bar->label = (bus ? bus->name : juce::String ("?")) + (sp.preFader ? " pre" : "");
            bar->defaultValue = 0.0f;
            bar->fill = bus ? bus->colour : theme::accent;
            bar->format = [] (float v) { return stripfmt::signedDb (pm::volumeToDb (v)); };
            bar->setTooltip ("Send to " + (bus ? bus->name : juce::String ("?")) + " (drag; right-click: pre/post, remove)");
            const int idx = i;
            bar->onRightClick = [this, idx] (juce::Component* c) { showSendMenu (idx, c); };
            auto* raw = add (std::move (bar));
            raw->setBounds (kM, y, rw, kSlotH);
            sendBars.push_back (raw);
            y += kSlotH + kSlotGap;
        }
        empty ("+ Send", "Send to a bus (or make a new one)", [this] (juce::Component* c) { showSendPicker (controller, trackId, c); });
        y += 3;
    }
    if (! loopPageTrack) chain ("Post FX", FxChain::Post, t.postFx, "+ Post FX");

    content.setSize (theme::stripWidth - 6, y + 2);
    viewport.setViewPosition (0, scrollPos);
    refreshValues();
}

void ChannelStrip::resized()
{
    const int W = getWidth(), H = getHeight();
    icon->setBounds (kM, 8, 26, 26);
    const bool full = kind == TrackKind::Audio || kind == TrackKind::Instrument;
    const int bw = W - 2 * kM;
    if (isMaster) {
        muteR = { kM, kButtonsY, bw, kButtonsH };
        soloR = armR = monR = {};
    } else if (loopPageTrack) {
        // M / S / arm and monitor are on the track's loop-page label.
        muteR = soloR = armR = monR = {};
    } else if (full) {
        const int b = 24;
        muteR = { kM, kButtonsY, b, kButtonsH };
        soloR = muteR.translated (b + 3, 0);
        armR = soloR.translated (b + 3, 0);
        monR = { armR.getRight() + 3, kButtonsY, W - kM - armR.getRight() - 3, kButtonsH };
    } else {
        muteR = { kM, kButtonsY, bw / 2 - 2, kButtonsH };
        soloR = { kM + bw / 2 + 2, kButtonsY, bw - bw / 2 - 2, kButtonsH };
        armR = monR = {};
    }

    // Bottom: output, fader + meter, pan, In. The fader grows with the panel.
    int bottom = H - 6;
    outputSlot->setBounds (kM, bottom - kSlotH, bw, kSlotH);
    bottom -= kSlotH + 6;
    const int topH = loopPageTrack ? kHeaderH + 2 : kTopH;
    const int faderH = juce::jlimit (80, 220, (int) ((H - topH) * 0.36f));
    const int meterW = 14;
    fader->setBounds (kM, bottom - faderH, bw - meterW - 4, faderH);
    meterR = { W - kM - meterW, bottom - faderH, meterW, faderH };
    bottom -= faderH + 5;
    panBar->setBounds (kM, bottom - 22, bw, 22);
    bottom -= 22 + 4;
    if (! isMaster) { gainBar->setBounds (kM, bottom - 22, bw, 22); bottom -= 22 + 4; }

    viewport.setBounds (0, topH, W, juce::jmax (0, bottom - topH - 2));
}

ChannelStrip::Button ChannelStrip::buttonAt (juce::Point<int> p) const
{
    if (muteR.contains (p)) return Mute;
    if (soloR.contains (p)) return Solo;
    if (armR.contains (p)) return Arm;
    if (monR.contains (p)) return Monitor;
    return None;
}

void ChannelStrip::setLoopPageTrack (bool b)
{
    if (b == loopPageTrack) return;
    loopPageTrack = b;
    rebuild();
}

void ChannelStrip::setSelected (bool s)
{
    if (s != selected) { selected = s; repaint(); }
}

void ChannelStrip::refreshValues()
{
    auto t = track();
    if (! t || ! t->rt) return;
    const bool m = controller.getParam (trackId, { ParamKind::Mute, kInvalidId, -1 }) > 0.5f;
    const bool s = t->rt->solo.load(), a = t->rt->armed.load();
    const auto mon = t->rt->monitor.load();
    if (m != muted || s != soloed || a != armed || mon != monitor) {
        muted = m; soloed = s; armed = a; monitor = mon;
        repaint (0, kButtonsY - 1, getWidth(), kButtonsH + 2);
    }
    icon->poll();
    gainBar->poll(); panBar->poll(); fader->poll();
    for (auto* c : chips) c->poll();
    for (auto* b : sendBars) b->poll();
    if (eqThumb) eqThumb->refresh();
    if (eqToggle) eqToggle->setToggleState (t->rt->eqEnabled.load(), juce::dontSendNotification);
    const auto ms = controller.meter (trackId);
    const int l = juce::jlimit (0, 60, (int) (ms.left * 60.0f)), r = juce::jlimit (0, 60, (int) (ms.right * 60.0f));
    if (l != meterL || r != meterR2) { meterL = l; meterR2 = r; repaint (meterR.expanded (1)); }
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.5f, 1.5f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (b, 6.0f);
    {
        juce::Path p;
        p.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), 5.0f, 6.0f, 6.0f, true, true, false, false);
        g.setColour (colour);
        g.fillPath (p);
    }
    g.setColour (selected ? juce::Colours::white : theme::outline);
    g.drawRoundedRectangle (b, 6.0f, selected ? 1.5f : 1.0f);

    const int tx = kM + 32;
    g.setColour (theme::text);
    g.setFont (theme::font (14.0f, true));
    g.drawText (nameText, tx, 8, getWidth() - tx - 4, 16, juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f));
    g.drawText (line1, tx, 24, getWidth() - tx - 4, 13, juce::Justification::centredLeft, true);

    auto drawBtn = [&] (juce::Rectangle<int> r, const juce::String& label, bool on, juce::Colour onCol) {
        if (r.isEmpty()) return;
        auto rf = r.toFloat();
        g.setColour (on ? onCol : theme::panelRaised);
        g.fillRoundedRectangle (rf, 4.0f);
        g.setColour (on ? onCol.brighter (0.3f) : theme::outline);
        g.drawRoundedRectangle (rf, 4.0f, 1.0f);
        g.setColour (on ? juce::Colours::black : theme::textDim.brighter (0.3f));
        g.setFont (theme::font (12.5f, true));
        g.drawText (label, r, juce::Justification::centred);
    };
    drawBtn (muteR, "M", muted, theme::overdub);
    drawBtn (soloR, "S", soloed, theme::armed);
    if (! armR.isEmpty()) {
        drawBtn (armR, "", armed, theme::record);
        g.setColour (armed ? juce::Colours::white : theme::record.withAlpha (0.7f));
        const auto c = armR.toFloat().getCentre();
        g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
    }
    drawBtn (monR, monitorName (monitor), monitor == MonitorMode::On, theme::accent);

    // vertical stereo meter beside the fader
    auto mr = meterR.toFloat();
    g.setColour (theme::background);
    g.fillRoundedRectangle (mr, 2.0f);
    const float bw = (mr.getWidth() - 1.0f) * 0.5f;
    auto bar = [&] (float x, int v) {
        if (v <= 0) return;
        const float h = mr.getHeight() * (float) v / 60.0f;
        g.setColour (v > 55 ? theme::meterHigh : v > 42 ? theme::meterMid : theme::meterLow);
        g.fillRect (x, mr.getBottom() - h, bw, h);
    };
    bar (mr.getX(), meterL);
    bar (mr.getX() + bw + 1.0f, meterR2);
}

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    if (onSelect) onSelect (trackId);
    if (e.mods.isPopupMenu()) { showTrackMenu (controller, presets, trackId, this); return; }
    auto t = track();
    if (! t || ! t->rt) return;
    switch (buttonAt (e.getPosition())) {
        case Mute: {
            // The gesture stays open until mouse-up so automation recording sees the change.
            const ParamTarget tgt { ParamKind::Mute, kInvalidId, -1 };
            controller.beginParamGesture (trackId, tgt);
            controller.setParam (trackId, tgt, t->rt->mute.load() ? 0.0f : 1.0f);
            muteGestureOpen = true;
            break;
        }
        case Solo: t->rt->solo.store (! t->rt->solo.load()); break;
        case Arm: controller.setArmed (trackId, ! t->rt->armed.load()); break;
        case Monitor: {
            const auto m = t->rt->monitor.load();
            controller.setMonitorMode (trackId, m == MonitorMode::Auto ? MonitorMode::On : m == MonitorMode::On ? MonitorMode::Off : MonitorMode::Auto);
            break;
        }
        case None: break;
    }
    refreshValues();
}

void ChannelStrip::mouseUp (const juce::MouseEvent&)
{
    if (muteGestureOpen) {
        muteGestureOpen = false;
        controller.endParamGesture (trackId, { ParamKind::Mute, kInvalidId, -1 });
    }
}

void ChannelStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! isMaster && e.y < kHeaderH && e.x > kM + 30 && ! e.mods.isPopupMenu()) renameTrack (controller, trackId);
}

void ChannelStrip::showSlotMenu (FxChip& f)
{
    if (f.getSlot()) showPluginSlotMenu (controller, presets, trackId, f.getChain(), f.getIndex(), &f);
}

void ChannelStrip::showSendMenu (int sendIndex, juce::Component* target)
{
    auto t = track();
    if (! t || sendIndex < 0 || sendIndex >= (int) t->sends.size()) return;
    const bool pre = t->sends[(size_t) sendIndex].preFader;
    juce::PopupMenu m;
    m.addItem (1, "Pre-fader", true, pre);
    m.addItem (2, "Post-fader", true, ! pre);
    m.addSeparator();
    m.addItem (3, "Remove send");
    auto* c = &controller; const Id id = trackId;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [c, id, sendIndex] (int r) {
        if (r == 1) c->setSendPreFader (id, sendIndex, true);
        else if (r == 2) c->setSendPreFader (id, sendIndex, false);
        else if (r == 3) c->removeSend (id, sendIndex);
    });
}

void ChannelStrip::showHardwareOutputMenu (juce::Component* target)
{
    auto t = track();
    if (! t) return;
    juce::StringArray names;
    if (auto* dev = controller.deviceManager().getCurrentAudioDevice()) names = dev->getOutputChannelNames();
    juce::PopupMenu m;
    const int pairs = std::max (1, names.size() / 2);
    for (int p = 0; p < pairs; ++p) {
        const int first = p * 2;
        juce::String label = "Out " + juce::String (first + 1) + "-" + juce::String (first + 2);
        if (first + 1 < names.size()) label << "  (" << names[first] << " / " << names[first + 1] << ")";
        m.addItem (first + 1, label, true, t->hardwareOutput == first);
    }
    auto* c = &controller;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [c] (int r) {
        if (r > 0) c->setMasterHardwareOutput (r - 1);
    });
}

void ChannelStrip::openEqEditor()
{
    if (eqThumb == nullptr) return;
    auto ed = std::make_unique<GraphicEqEditor> (controller, trackId, false);
    ed->setSize (520, 300);
    juce::CallOutBox::launchAsynchronously (std::move (ed), eqThumb->getScreenBounds(), nullptr);
}

} // namespace el
