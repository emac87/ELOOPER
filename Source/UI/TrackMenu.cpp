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

namespace {
    const std::vector<PluginSlotPtr>* chainList (const TrackModel& t, FxChain ch)
    {
        return ch == FxChain::MidiFx ? &t.midiFx : ch == FxChain::Pre ? &t.preFx : ch == FxChain::Post ? &t.postFx : nullptr;
    }

    PluginSlotPtr slotAt (const TrackModel& t, FxChain ch, int index)
    {
        if (ch == FxChain::Instrument) return t.instrument;
        auto* l = chainList (t, ch);
        return l != nullptr && index >= 0 && index < (int) l->size() ? (*l)[(size_t) index] : nullptr;
    }

    // Item ids are idBase + action (1..8).
    void addSlotItems (juce::PopupMenu& m, EngineController& c, const TrackModel& t, FxChain chain, int index,
                       const PluginSlot& slot, int idBase)
    {
        const auto* list = chainList (t, chain);
        const int count = list != nullptr ? (int) list->size() : 1;
        const bool movable = chain == FxChain::Pre || chain == FxChain::Post;
        m.addItem (idBase + 1, c.isPluginEditorOpen (slot.id) ? "Hide window" : "Show window");
        if (chain != FxChain::Instrument) m.addItem (idBase + 8, "Bypass", true, slot.bypassed.load());
        m.addItem (idBase + 2, "Replace...");
        m.addItem (idBase + 3, "Presets...");
        if (chain != FxChain::Instrument) {
            m.addSeparator();
            m.addItem (idBase + 4, "Move up", index > 0);
            m.addItem (idBase + 5, "Move down", index < count - 1);
            if (movable) m.addItem (idBase + 6, chain == FxChain::Pre ? "Move after the fader (Post FX)" : "Move before the fader (Pre FX)");
        }
        m.addSeparator();
        m.addItem (idBase + 7, chain == FxChain::Instrument ? "No Instrument (remove)" : "No Plug-in (remove)");
    }

    void runSlotAction (EngineController* c, PresetManager* pm, juce::Component::SafePointer<juce::Component> tgt,
                        Id tid, Id slotId, FxChain chain, int index, bool wasBypassed, int action)
    {
        switch (action) {
            case 1: c->togglePluginEditor (slotId); break;
            case 2:
                if (chain == FxChain::Instrument) showPluginMenu (*c, tid, FxChain::Instrument, -1, tgt.getComponent());
                else showPluginMenu (*c, tid, chain, index, tgt.getComponent(), [c, slotId] (Id) { c->removePlugin (slotId); });
                break;
            case 3: if (tgt != nullptr) pm->showPluginPresetMenu (slotId, tgt.getComponent()); break;
            case 4: c->movePlugin (slotId, chain, index - 1); break;
            case 5: c->movePlugin (slotId, chain, index + 1); break;
            case 6: {
                auto tr = c->session().findTrack (tid);
                if (tr == nullptr) break;
                const auto other = chain == FxChain::Pre ? FxChain::Post : FxChain::Pre;
                c->movePlugin (slotId, other, (int) (other == FxChain::Pre ? tr->preFx.size() : tr->postFx.size()));
                break;
            }
            case 7: c->removePlugin (slotId); break;
            case 8: c->setPluginBypass (slotId, ! wasBypassed); break;
            default: break;
        }
    }
}

void showPluginSlotMenu (EngineController& controller, PresetManager& presets, Id trackId, FxChain chain, int index,
                         juce::Component* target)
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    auto slot = slotAt (*t, chain, index);
    if (slot == nullptr) return;
    juce::PopupMenu m;
    m.addSectionHeader (slot->name());
    addSlotItems (m, controller, *t, chain, index, *slot, 0);
    juce::Component::SafePointer<juce::Component> tgt (target);
    auto* c = &controller; auto* pm = &presets;
    const Id slotId = slot->id;
    const bool wasBypassed = slot->bypassed.load();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [c, pm, tgt, trackId, slotId, chain, index, wasBypassed] (int r) {
        runSlotAction (c, pm, tgt, trackId, slotId, chain, index, wasBypassed, r);
    });
}

void showChainMenu (EngineController& controller, PresetManager& presets, Id trackId, FxChain chain, juce::Component* target)
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    const auto* list = chainList (*t, chain);
    if (list == nullptr) return;
    if (list->empty()) { showPluginMenu (controller, trackId, chain, -1, target); return; }

    // ids: 1..n toggle plug-in i's window; i * 100 + action for its options; 9000 add; 9001 / 9002 show / hide all.
    struct Entry { Id slotId; bool bypassed; int index; };
    std::vector<Entry> entries;
    juce::PopupMenu m;
    m.addSectionHeader (chain == FxChain::Pre ? "Pre FX (before the fader)" : "Post FX (after the fader)");
    for (int i = 0; i < (int) list->size(); ++i) {
        const auto& p = (*list)[(size_t) i];
        if (p == nullptr) continue;
        entries.push_back ({ p->id, p->bypassed.load(), i });
        const int n = (int) entries.size();
        m.addItem (n, p->name() + (p->bypassed.load() ? "  (bypassed)" : ""), true, controller.isPluginEditorOpen (p->id));
        juce::PopupMenu opts;
        addSlotItems (opts, controller, *t, chain, i, *p, n * 100);
        m.addSubMenu ("    options", opts);
    }
    m.addSeparator();
    m.addItem (9000, "Add plug-in...");
    m.addItem (9001, "Show all");
    m.addItem (9002, "Hide all");

    juce::Component::SafePointer<juce::Component> tgt (target);
    auto* c = &controller; auto* pm = &presets;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [c, pm, tgt, trackId, chain, entries] (int r) {
        if (r <= 0) return;
        if (r == 9000) { showPluginMenu (*c, trackId, chain, -1, tgt.getComponent()); return; }
        if (r == 9001 || r == 9002) {
            for (auto& e : entries)
                if (c->isPluginEditorOpen (e.slotId) != (r == 9001)) c->togglePluginEditor (e.slotId);
            return;
        }
        if (r <= (int) entries.size()) { c->togglePluginEditor (entries[(size_t) r - 1].slotId); return; }
        const int n = r / 100, action = r % 100;
        if (n >= 1 && n <= (int) entries.size())
            { const auto& e = entries[(size_t) n - 1]; runSlotAction (c, pm, tgt, trackId, e.slotId, chain, e.index, e.bypassed, action); }
    });
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
