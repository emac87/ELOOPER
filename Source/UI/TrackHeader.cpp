#include "UI/TrackHeader.h"
#include "UI/ColourPicker.h"
#include "UI/LookAndFeel.h"
#include "UI/PluginBrowser.h"
#include "UI/PluginIcons.h"
#include "UI/Theme.h"

namespace el {

namespace {
    juce::String kindName (TrackKind k)
    {
        switch (k) {
            case TrackKind::Audio:      return "AUDIO";
            case TrackKind::Instrument: return "INST";
            case TrackKind::Bus:        return "BUS";
            case TrackKind::Master:     return "MASTER";
            case TrackKind::AuxOut:     return "AUX OUT";
        }
        return {};
    }
    const char* monitorName (MonitorMode m)
    {
        return m == MonitorMode::Auto ? "Auto" : m == MonitorMode::On ? "On" : "Off";
    }
}


// Icon at the left of the name row: reflects the track's plugin (or the channel type).
// Left click toggles the plugin editor, right click offers icon overrides.
class PluginIconButton : public juce::Component, public juce::SettableTooltipClient {
public:
    PluginIconButton (EngineController& c, Id id) : controller (c), trackId (id) {}

    std::function<void()> onPressed;

    void refresh()
    {
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr) return;
        auto slot = icons::iconSlot (*t);
        setTooltip (slot != nullptr ? slot->name() + "  (click: open editor, right-click: change icon)"
                                    : channelName (*t) + " channel  (right-click: change icon)");
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr) return;
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (hover ? theme::panelRaised.brighter (0.15f) : theme::panelRaised.darker (0.4f));
        g.fillRoundedRectangle (b, 7.0f);
        g.setColour (hover ? t->colour.withAlpha (0.8f) : theme::outline);
        g.drawRoundedRectangle (b, 7.0f, 1.0f);
        icons::IconCache::get().drawIcon (g, b.reduced (8.0f), *t, hover ? t->colour.brighter (0.3f) : t->colour);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onPressed) onPressed();
        if (e.mods.isPopupMenu()) showMenu();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || ! contains (e.getPosition())) return;
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr) return;
        if (auto slot = icons::iconSlot (*t)) controller.togglePluginEditor (slot->id);
    }

private:
    static juce::String channelName (const TrackModel& t) { return icons::kindName (icons::channelKind (t.kind)); }

    void showMenu()
    {
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr) return;
        const auto key = icons::IconCache::overrideKey (*t);
        const auto current = icons::IconCache::get().override (key);
        juce::Component::SafePointer<PluginIconButton> self (this);
        enum { chooseId = 1000, autoId = 1001 };

        juce::PopupMenu m;
        const auto& all = icons::allKinds();
        for (size_t i = 0; i < all.size(); ++i) {
            juce::PopupMenu::Item item (icons::kindName (all[i]));
            item.itemID = 1 + (int) i;
            item.image = icons::makeDrawable (all[i]);
            item.setTicked (current && *current == "builtin:" + icons::kindName (all[i]));
            m.addItem (std::move (item));
            if (all[i] == icons::Kind::Organ || all[i] == icons::Kind::Effect) m.addSeparator();
        }
        m.addSeparator();
        m.addItem (chooseId, "Choose Image...", true, current && ! current->startsWith ("builtin:"));
        m.addItem (autoId, "Automatic", true, ! current.has_value());

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [self, key, all] (int result) {
            if (self == nullptr || result == 0) return;
            auto& cache = icons::IconCache::get();
            if (result == autoId) { cache.setOverride (key, {}); }
            else if (result == chooseId) {
                auto chooser = std::make_shared<juce::FileChooser> ("Choose an icon image", juce::File(),
                                                                    "*.png;*.jpg;*.jpeg;*.tiff;*.icns");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [self, key, chooser] (const juce::FileChooser& fc) {
                    auto f = fc.getResult();
                    if (self == nullptr || ! f.existsAsFile()) return;
                    auto& ic = icons::IconCache::get();
                    ic.setOverride (key, ic.importImage (f).getFullPathName());
                    self->repaint();
                });
                return;
            }
            else if (result >= 1 && result <= (int) all.size())
                cache.setOverride (key, "builtin:" + icons::kindName (all[(size_t) result - 1]));
            self->repaint();
        });
    }

    EngineController& controller;
    const Id trackId;
    bool hover = false;
};

TrackHeader::~TrackHeader() = default;

TrackHeader::TrackHeader (EngineController& c, Selection& s, PresetManager& p, Id id)
    : controller (c), selection (s), presets (p), trackId (id)
{
    iconButton = std::make_unique<PluginIconButton> (controller, trackId);
    iconButton->onPressed = [this] { selection.select (trackId, kInvalidId); };
    addAndMakeVisible (*iconButton);
    setSize (theme::stripWidth, headerHeight);
    refresh();
    poll();
}

void TrackHeader::refresh()
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    colour = t->colour;
    nameText = t->name;
    kind = t->kind;
    kindText = kindName (t->kind);
    line1.clear(); line2.clear();
    if (t->kind == TrackKind::Audio) {
        if (t->inputChannel < 0) line1 = "No input";
        else line1 = t->stereoInput ? "In " + juce::String (t->inputChannel + 1) + "/" + juce::String (t->inputChannel + 2)
                                    : "In " + juce::String (t->inputChannel + 1);
    } else if (t->kind == TrackKind::Instrument) {
        line1 = t->instrument != nullptr ? t->instrument->name() : juce::String ("No instrument");
        juce::String dev = "All MIDI";
        if (t->midiInputDevice.isNotEmpty()) {
            dev = t->midiInputDevice;
            for (auto& d : juce::MidiInput::getAvailableDevices())
                if (d.identifier == t->midiInputDevice) dev = d.name;
        }
        line2 = dev + (t->midiChannel > 0 ? " ch" + juce::String (t->midiChannel) : juce::String());
    } else if (t->kind == TrackKind::AuxOut) {
        line1 = "Pair " + juce::String (t->auxPair + 1);
    } else if (t->kind == TrackKind::Bus) {
        line1 = "Sum";
    }
    if (iconButton != nullptr) iconButton->refresh();
    repaint();
}

void TrackHeader::resized()
{
    if (iconButton != nullptr) iconButton->setBounds (6, 10, 40, 40);
    const int bw = 30, gap = 4, y = 110, h = 28;
    const bool full = kind == TrackKind::Audio || kind == TrackKind::Instrument;
    const int total = full ? bw * 3 + gap * 2 : bw * 2 + gap;
    int x = (getWidth() - total) / 2;
    muteR = { x, y, bw, h }; x += bw + gap;
    soloR = { x, y, bw, h }; x += bw + gap;
    if (full) {
        armR = { x, y, bw, h };
        monR = { 6, y + h + 4, getWidth() - 12, h };
    } else { armR = {}; monR = {}; }
    meterR = { 6, headerHeight - 12, getWidth() - 12, 7 };
}

TrackHeader::Button TrackHeader::buttonAt (juce::Point<int> p) const
{
    if (muteR.contains (p)) return Mute;
    if (soloR.contains (p)) return Solo;
    if (armR.contains (p)) return Arm;
    if (monR.contains (p)) return Monitor;
    return None;
}

void TrackHeader::poll()
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr || t->rt == nullptr) return;
    const bool m = t->rt->mute.load(), s = t->rt->solo.load(), a = t->rt->armed.load();
    const auto mon = t->rt->monitor.load();
    if (m != muted || s != soloed || a != armed || mon != monitor) {
        muted = m; soloed = s; armed = a; monitor = mon;
        repaint();
    }
    const auto ms = controller.meter (trackId);
    const int l = juce::jlimit (0, 40, (int) (ms.left * 40.0f)), r = juce::jlimit (0, 40, (int) (ms.right * 40.0f));
    if (l != meterL || r != meterR2) { meterL = l; meterR2 = r; repaint (meterR.expanded (0, 2)); }
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (b, 7.0f);
    // colour stripe
    {
        juce::Path p;
        p.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), 8.0f, 7.0f, 7.0f, true, true, false, false);
        g.setColour (colour);
        g.fillPath (p);
    }
    const bool sel = selection.trackId == trackId;
    g.setColour (sel ? (selection.loopId == kInvalidId ? juce::Colours::white : colour.withAlpha (0.8f)) : theme::outline);
    g.drawRoundedRectangle (b, 7.0f, sel ? 1.5f : 1.0f);

    // Row 1: icon + track kind; row 2: the name across the full width; rows 3-4: input info.
    const int textX = 52;
    g.setColour (colour.withAlpha (0.9f));
    g.setFont (theme::font (13.0f, true));
    g.drawFittedText (kindText, textX, 10, getWidth() - textX - 5, 40, juce::Justification::centredLeft, 2);
    g.setColour (theme::text);
    g.setFont (theme::font (16.0f, true));
    g.drawText (nameText, 6, 54, getWidth() - 12, 20, juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (theme::font (13.0f));
    g.drawText (line1, 6, 74, getWidth() - 12, 16, juce::Justification::centredLeft, true);
    g.drawText (line2, 6, 90, getWidth() - 12, 16, juce::Justification::centredLeft, true);

    auto drawBtn = [&] (juce::Rectangle<int> r, const juce::String& label, bool on, juce::Colour onCol) {
        if (r.isEmpty()) return;
        auto rf = r.toFloat();
        g.setColour (on ? onCol : theme::panelRaised);
        g.fillRoundedRectangle (rf, 4.0f);
        g.setColour (on ? onCol.brighter (0.3f) : theme::outline);
        g.drawRoundedRectangle (rf, 4.0f, 1.0f);
        g.setColour (on ? juce::Colours::black : theme::textDim.brighter (0.3f));
        g.setFont (theme::font (14.0f, true));
        g.drawText (label, r, juce::Justification::centred);
    };
    drawBtn (muteR, "M", muted, theme::overdub);
    drawBtn (soloR, "S", soloed, theme::armed);
    if (! armR.isEmpty()) {
        drawBtn (armR, "", armed, theme::record);
        g.setColour (armed ? juce::Colours::white : theme::record.withAlpha (0.7f));
        g.fillEllipse (armR.toFloat().getCentre().x - 6.0f, armR.toFloat().getCentre().y - 6.0f, 12.0f, 12.0f);
        drawBtn (monR, monitorName (monitor), monitor == MonitorMode::On, theme::accent);
    }

    // meter
    auto mr = meterR.toFloat();
    g.setColour (theme::panelRaised);
    g.fillRect (mr);
    auto bar = [&] (float y, int v) {
        const float w = mr.getWidth() * (float) v / 40.0f;
        g.setColour (v > 36 ? theme::meterHigh : v > 28 ? theme::meterMid : theme::meterLow);
        g.fillRect (mr.getX(), y, w, 3.0f);
    };
    bar (mr.getY(), meterL);
    bar (mr.getY() + 4.0f, meterR2);
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    downX = e.x;
    selection.select (trackId, kInvalidId);
    if (e.mods.isPopupMenu()) { showMenu(); return; }

    auto t = controller.session().findTrack (trackId);
    if (t == nullptr || t->rt == nullptr) return;
    switch (buttonAt (e.getPosition())) {
        case Mute:  t->rt->mute.store (! t->rt->mute.load()); poll(); break;
        case Solo:  t->rt->solo.store (! t->rt->solo.load()); poll(); break;
        case Arm:   controller.setArmed (trackId, ! t->rt->armed.load()); poll(); break;
        case Monitor: {
            const auto m = t->rt->monitor.load();
            controller.setMonitorMode (trackId, m == MonitorMode::Auto ? MonitorMode::On
                                              : m == MonitorMode::On ? MonitorMode::Off : MonitorMode::Auto);
            poll();
            break;
        }
        case None: break;
    }
}

void TrackHeader::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || buttonAt (e.getMouseDownPosition()) != None) return;
    if (! dragging && std::abs (e.getDistanceFromDragStartX()) > 12) {
        dragging = true;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setAlpha (0.6f);
    }
}

void TrackHeader::mouseUp (const juce::MouseEvent& e)
{
    if (dragging) {
        dragging = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        setAlpha (1.0f);
        if (onReorderDrop) onReorderDrop (e);
    }
}

void TrackHeader::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (buttonAt (e.getPosition()) == None && ! e.mods.isPopupMenu()) rename();
}

void TrackHeader::rename()
{
    auto* c = &controller; const Id id = trackId;
    ui::askText ("Rename Track", "Track name:", nameText, [c, id] (juce::String n) {
        n = n.trim();
        if (n.isNotEmpty()) c->setTrackName (id, n);
    });
}

void TrackHeader::confirmBounce()
{
    auto* c = &controller; const Id id = trackId;
    ui::confirm ("Bounce Track",
                 "This bakes the track's full output, including all effects, into a new audio loop on a new track. "
                 "The original track moves to the Bounced page (you can restore it later).",
                 "Bounce", [c, id] { c->bounceTrack (id); });
}

void TrackHeader::confirmRemove()
{
    auto* c = &controller; const Id id = trackId;
    ui::confirm ("Remove Track", "Remove \"" + nameText + "\" and all of its loops?", "Remove",
                 [c, id] { c->removeTrack (id); });
}

void TrackHeader::showMenu()
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    auto* c = &controller; const Id id = trackId;
    juce::Component::SafePointer<TrackHeader> self (this);
    ActionMenu am;
    juce::PopupMenu m;

    am.add (m, "Rename...", [self] { if (self != nullptr) self->rename(); });
    am.addColourMenu (m, "Colour", t->colour, false, this,
                      [c, id] (std::optional<juce::Colour> col) { if (col) c->setTrackColour (id, *col); });
    m.addSeparator();

    if (t->kind == TrackKind::Audio) {
        juce::PopupMenu in;
        am.add (in, "None", [c, id] { c->setTrackInput (id, -1, false); }, t->inputChannel < 0);
        in.addSeparator();
        int numIn = 0;
        if (auto* dev = controller.deviceManager().getCurrentAudioDevice())
            numIn = dev->getInputChannelNames().size();
        for (int k = 0; k < numIn; ++k)
            am.add (in, "In " + juce::String (k + 1), [c, id, k] { c->setTrackInput (id, k, false); },
                    t->inputChannel == k && ! t->stereoInput);
        if (numIn >= 2) in.addSeparator();
        for (int k = 0; k + 1 < numIn; k += 2)
            am.add (in, "In " + juce::String (k + 1) + "/" + juce::String (k + 2),
                    [c, id, k] { c->setTrackInput (id, k, true); }, t->inputChannel == k && t->stereoInput);
        if (numIn == 0) am.add (in, "(no audio input device)", {}, false, false);
        m.addSubMenu ("Input", in);
    }

    if (t->kind == TrackKind::Instrument) {
        juce::PopupMenu mi;
        const auto dev = t->midiInputDevice;
        const int ch = t->midiChannel;
        am.add (mi, "All Devices", [c, id, ch] { c->setTrackMidiInput (id, {}, ch); }, dev.isEmpty());
        for (auto& d : juce::MidiInput::getAvailableDevices())
            am.add (mi, d.name, [c, id, ch, ident = d.identifier] { c->setTrackMidiInput (id, ident, ch); }, dev == d.identifier);
        mi.addSeparator();
        juce::PopupMenu chm;
        am.add (chm, "Omni", [c, id, dev] { c->setTrackMidiInput (id, dev, 0); }, ch == 0);
        for (int k = 1; k <= 16; ++k)
            am.add (chm, juce::String (k), [c, id, dev, k] { c->setTrackMidiInput (id, dev, k); }, ch == k);
        mi.addSubMenu ("Channel", chm);
        m.addSubMenu ("MIDI Input", mi);

        am.add (m, "Instrument...", [self, c, id] {
            if (self != nullptr) showPluginMenu (*c, id, FxChain::Instrument, 0, self.getComponent());
        });
    }

    if (t->kind == TrackKind::Audio || t->kind == TrackKind::Instrument) {
        juce::PopupMenu mon;
        const MonitorMode modes[] = { MonitorMode::Auto, MonitorMode::On, MonitorMode::Off };
        const auto cur = t->rt != nullptr ? t->rt->monitor.load() : MonitorMode::Auto;
        for (auto mm : modes)
            am.add (mon, monitorName (mm), [c, id, mm] { c->setMonitorMode (id, mm); }, cur == mm);
        m.addSubMenu ("Monitor", mon);
    }

    if (t->kind != TrackKind::Master) {
        juce::PopupMenu out;
        am.add (out, "Master", [id, c] {
            if (! c->setTrackOutput (id, kMasterId)) ui::alert ("Output", "That output is not possible.");
        }, t->outputBus == kMasterId);
        for (auto& b : controller.session().buses()) {
            if (b->id == trackId) continue;
            const Id bid = b->id;
            am.add (out, b->name, [id, c, bid] {
                if (! c->setTrackOutput (id, bid))
                    ui::alert ("Output", "Routing to that bus would create a feedback cycle.");
            }, t->outputBus == bid);
        }
        m.addSubMenu ("Output", out);
    }

    am.add (m, "Channel Presets...", [self, this_ = &presets, id] {
        if (self != nullptr) this_->showChannelPresetMenu (id, self.getComponent());
    });
    if (t->kind == TrackKind::Instrument)
        am.add (m, "Create Multi-Out Tracks", [c, id] { c->createMultiOutTracks (id); });

    m.addSeparator();
    if (t->kind != TrackKind::Master) {
        am.add (m, "Bounce Track", [self] { if (self != nullptr) self->confirmBounce(); });
        am.add (m, "Remove Track", [self] { if (self != nullptr) self->confirmRemove(); });
    }
    am.show (m, this);
}

} // namespace el
