#include "UI/TrackMenu.h"
#include "UI/ColourPicker.h"
#include "UI/LookAndFeel.h"
#include "UI/PluginBrowser.h"
#include "UI/Theme.h"

namespace el {

namespace {
    const char* monitorName (MonitorMode m)
    {
        return m == MonitorMode::Auto ? "Auto" : m == MonitorMode::On ? "On" : "Off";
    }
}

void renameTrack (EngineController& controller, Id trackId)
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    auto* c = &controller; const Id id = trackId;
    ui::askText ("Rename Track", "Track name:", t->name, [c, id] (juce::String n) {
        n = n.trim();
        if (n.isNotEmpty()) c->setTrackName (id, n);
    });
}

void confirmBounceTrack (EngineController& controller, Id trackId)
{
    auto* c = &controller; const Id id = trackId;
    ui::confirm ("Bounce Track",
                 "This bakes the track's full output, including all effects, into a new audio loop on a new track. "
                 "The original track moves to the Bounced page (you can restore it later).",
                 "Bounce", [c, id] { c->bounceTrack (id); });
}

void confirmRemoveTrack (EngineController& controller, Id trackId)
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    auto* c = &controller; const Id id = trackId;
    ui::confirm ("Remove Track", "Remove \"" + t->name + "\" and all of its loops?", "Remove",
                 [c, id] { c->removeTrack (id); });
}

void showTrackMenu (EngineController& controller, PresetManager& presets, Id trackId, juce::Component* target)
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    auto* c = &controller; const Id id = trackId;
    juce::Component::SafePointer<juce::Component> self (target);
    int numOuts = 0;
    for (auto& o : controller.session().tracks) if (o->auxParent == trackId) ++numOuts;
    ActionMenu am;
    juce::PopupMenu m;

    am.add (m, "Rename...", [c, id] { renameTrack (*c, id); });
    am.addColourMenu (m, "Colour", t->colour, false, target,
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
            showPluginMenu (*c, id, FxChain::Instrument, 0, self.getComponent());
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

    am.add (m, "Channel Presets...", [self, pm2 = &presets, id] {
        if (self != nullptr) pm2->showChannelPresetMenu (id, self.getComponent());
    });
    if (t->kind == TrackKind::Instrument && t->instrument != nullptr && t->instrument->numOutputChannels > 2)
        am.add (m, numOuts > 0 ? "Multi-Out: Update Outputs + Sum Folder" : "Multi-Out: Create Outputs + Sum Folder",
                [c, id] { c->createMultiOutTracks (id); });
    else if (t->kind == TrackKind::Instrument)
        am.add (m, "Multi-Out (instrument has a single stereo output)", {}, false, false);

    m.addSeparator();
    if (t->kind != TrackKind::Master) {
        am.add (m, "Bounce Track", [c, id] { confirmBounceTrack (*c, id); });
        am.add (m, "Remove Track", [c, id] { confirmRemoveTrack (*c, id); });
    }
    am.show (m, target);
}

} // namespace el
