#include "UI/TrackControls.h"
#include "UI/StripWidgets.h"
#include "UI/PluginBrowser.h"
#include "UI/TrackMenu.h"
#include "UI/ColourPicker.h"
#include "UI/Theme.h"
#include "UI/ValueBar.h"
#include "Core/ParamMapping.h"

namespace el {

// ---- Mute / Solo / Record arm ----------------------------------------------------------------------
class TrackControls::Buttons : public juce::Component, public juce::SettableTooltipClient {
public:
    Buttons (EngineController& c, Selection& s, Id id) : controller (c), selection (s), trackId (id)
    {
        auto t = controller.session().findTrack (trackId);
        hasArm = t != nullptr && (t->kind == TrackKind::Audio || t->isMidiLike());
        hasSolo = t == nullptr || t->kind != TrackKind::Midi;   // MIDI tracks: just Mute and Record arm
        setTooltip (! hasSolo ? "Mute / Record arm" : hasArm ? "Mute / Solo / Record arm" : "Mute / Solo");
    }

    ~Buttons() override { endMute(); }

    void poll()
    {
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr || ! t->rt) return;
        const bool m = controller.getParam (trackId, { ParamKind::Mute, kInvalidId, -1 }) > 0.5f;
        const bool s = t->rt->solo.load(), a = t->rt->armed.load();
        if (m != muted || s != soloed || a != armed) { muted = m; soloed = s; armed = a; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto draw = [&] (int i, const juce::String& label, bool on, juce::Colour onCol) {
            auto r = rect (i).toFloat();
            g.setColour (on ? onCol : theme::panelRaised);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? onCol.brighter (0.3f) : theme::outline);
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            if (label.isNotEmpty()) {
                g.setColour (on ? juce::Colours::black : theme::textDim.brighter (0.3f));
                g.setFont (theme::font (12.5f, true));
                g.drawText (label, rect (i), juce::Justification::centred);
            }
        };
        draw (0, "M", muted, theme::overdub);
        if (hasSolo) draw (1, "S", soloed, theme::armed);
        if (hasArm) {
            const int ai = armIndex();
            draw (ai, {}, armed, theme::record);
            const auto c = rect (ai).toFloat().getCentre();
            g.setColour (armed ? juce::Colours::white : theme::record.withAlpha (0.75f));
            g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        selection.select (trackId);
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr || ! t->rt || e.mods.isPopupMenu()) return;
        const int i = indexAt (e.getPosition());
        if (i == 0) {
            // The gesture stays open until mouse-up so automation recording sees the change.
            const ParamTarget tgt { ParamKind::Mute, kInvalidId, -1 };
            controller.beginParamGesture (trackId, tgt);
            controller.setParam (trackId, tgt, t->rt->mute.load() ? 0.0f : 1.0f);
            muteOpen = true;
        } else if (hasSolo && i == 1) {
            t->rt->solo.store (! t->rt->solo.load());
        } else if (hasArm && i == armIndex()) {
            controller.setArmed (trackId, ! t->rt->armed.load());
        }
        poll();
    }

    void mouseUp (const juce::MouseEvent&) override { endMute(); }

private:
    int count() const { return 1 + (hasSolo ? 1 : 0) + (hasArm ? 1 : 0); }
    int armIndex() const { return hasSolo ? 2 : 1; }
    juce::Rectangle<int> rect (int i) const
    {
        const int gap = 3, n = count();
        const int w = (getWidth() - gap * (n - 1)) / n;
        return { i * (w + gap), 0, i == n - 1 ? getWidth() - i * (w + gap) : w, getHeight() };
    }
    int indexAt (juce::Point<int> p) const
    {
        for (int i = 0; i < count(); ++i) if (rect (i).contains (p)) return i;
        return -1;
    }
    void endMute()
    {
        if (! muteOpen) return;
        muteOpen = false;
        controller.endParamGesture (trackId, { ParamKind::Mute, kInvalidId, -1 });
    }

    EngineController& controller;
    Selection& selection;
    const Id trackId;
    bool hasArm = false, hasSolo = true, muted = false, soloed = false, armed = false, muteOpen = false;
};

// ---- Pre FX / Post FX / MIDI FX ----------------------------------------------------------------------
namespace {
    const std::vector<PluginSlotPtr>& chainOf (const TrackModel& t, FxChain ch)
    {
        return ch == FxChain::Pre ? t.preFx : ch == FxChain::Post ? t.postFx : t.midiFx;
    }
    juce::String chainName (FxChain ch)
    {
        return ch == FxChain::Pre ? "Pre FX" : ch == FxChain::Post ? "Post FX" : "MIDI FX";
    }
}

class TrackControls::ChainButton : public juce::Component, public juce::SettableTooltipClient {
public:
    ChainButton (EngineController& c, Selection& s, PresetManager& p, Id id, FxChain ch)
        : controller (c), selection (s), presets (p), trackId (id), chain (ch)
    {
        setRepaintsOnMouseActivity (true);
        refresh();
    }

    void refresh()
    {
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr) return;
        const auto& list = chainOf (*t, chain);
        count = (int) list.size();
        colour = t->colour;
        // Say which plug-in is on the chain: its name, plus "+N" when there are more.
        firstName.clear();
        for (auto& p : list) if (p) { firstName = p->name(); break; }
        juce::String tip = chain == FxChain::Pre ? "Pre FX (before the fader)" : chain == FxChain::Post ? "Post FX (after the fader)"
                                                 : t->kind == TrackKind::Midi ? "MIDI FX (their MIDI goes to the MIDI To track)" : "MIDI FX (before the instrument)";
        if (count == 0) tip << ": empty, click to add a plug-in";
        else {
            tip << ":";
            for (auto& p : list) if (p) tip << "\n  " << p->name() << (p->bypassed.load() ? " (bypassed)" : "");
            tip << "\nClick: show / hide, options, add";
        }
        setTooltip (tip);
        repaint();
    }

    void poll()
    {
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr) return;
        bool o = false;
        for (auto& p : chainOf (*t, chain))
            if (p && controller.isPluginEditorOpen (p->id)) { o = true; break; }
        if (o != open) { open = o; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        const bool hot = isMouseOver();
        g.setColour (open ? colour.withAlpha (0.35f) : hot ? theme::outline : theme::panelRaised);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (open ? colour : count > 0 ? colour.withAlpha (0.6f) : theme::outline);
        g.drawRoundedRectangle (b, 4.0f, 1.0f);
        g.setColour (open ? juce::Colours::white : count > 0 ? theme::text : theme::textDim);
        g.setFont (theme::font (11.0f, count > 0));
        if (count > 0 && firstName.isNotEmpty()) {
            // The plug-in's name instead of "Pre FX" (the tooltip still says which chain and lists them all).
            auto r = getLocalBounds().reduced (3, 0);
            if (count > 1) {
                const juce::String more = "+" + juce::String (count - 1);
                const int mw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), more) + 3;
                g.drawText (more, r.removeFromRight (mw), juce::Justification::centredRight, false);
            }
            g.drawText (firstName, r, juce::Justification::centred, true);   // "..." when it doesn't fit
        } else {
            g.drawFittedText (chainName (chain), getLocalBounds().reduced (2, 0), juce::Justification::centred, 1, 0.8f);
        }
    }

    void mouseDown (const juce::MouseEvent&) override { selection.select (trackId); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! contains (e.getPosition())) return;
        showChainMenu (controller, presets, trackId, chain, this);   // async; may rebuild the page
    }

private:
    EngineController& controller;
    Selection& selection;
    PresetManager& presets;
    const Id trackId;
    const FxChain chain;
    int count = 0;
    bool open = false;
    juce::Colour colour;
    juce::String firstName;   // first plug-in on the chain (shown on the button)
};

// ---- monitor: Auto / On / Off -------------------------------------------------------------------------
class TrackControls::MonitorButton : public juce::Component, public juce::SettableTooltipClient {
public:
    MonitorButton (EngineController& c, Selection& s, Id id) : controller (c), selection (s), trackId (id)
    {
        setTooltip ("Input monitoring (I): Auto (hear the input until a loop is recorded, letter lit), On (filled) or Off (dim). Click to cycle.");
    }
    void poll()
    {
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr || ! t->rt) return;
        const auto m = t->rt->monitor.load();
        if (m != mode || ! known) { mode = m; known = true; repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        const bool on = mode == MonitorMode::On;
        auto r = getLocalBounds().toFloat();
        g.setColour (on ? theme::accent : theme::panelRaised);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? theme::accent.brighter (0.3f) : theme::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (on ? juce::Colours::black : theme::textDim.brighter (0.3f));
        g.setFont (theme::font (12.0f, true));
        // "I" = input monitoring. On: filled; Auto: accent letter; Off: dim letter.
        if (mode == MonitorMode::Auto) g.setColour (theme::accent);
        else if (! on) g.setColour (theme::textDim.withAlpha (0.6f));
        g.setFont (theme::font (12.5f, true));
        g.drawText ("I", getLocalBounds(), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        selection.select (trackId);
        if (e.mods.isPopupMenu()) return;
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr || ! t->rt) return;
        const auto m = t->rt->monitor.load();
        controller.setMonitorMode (trackId, m == MonitorMode::Auto ? MonitorMode::On : m == MonitorMode::On ? MonitorMode::Off : MonitorMode::Auto);
        poll();
    }
private:
    EngineController& controller;
    Selection& selection;
    const Id trackId;
    MonitorMode mode = MonitorMode::Auto;
    bool known = false;
};

// ---- MIDI To (MIDI tracks): the track whose instrument plays this track's MIDI ---------------------
class TrackControls::MidiToButton : public juce::Component, public juce::SettableTooltipClient {
public:
    MidiToButton (EngineController& c, Selection& s, Id id) : controller (c), selection (s), trackId (id)
    {
        setRepaintsOnMouseActivity (true);
        setTooltip ("MIDI To: the tracks that play this track's MIDI (click to tick them on / off)");
        refresh();
    }
    void refresh()
    {
        auto t = controller.session().findTrack (trackId);
        text = t != nullptr ? midiToText (controller.session(), *t) : juce::String();
        auto d = t != nullptr && ! t->midiTargets.empty() ? controller.session().findTrack (t->midiTargets.front()) : nullptr;
        targetName = d != nullptr ? d->name : juce::String();
        targetColour = d != nullptr ? d->colour : theme::outline;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (isMouseOver() ? theme::outline : theme::panelRaised);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (targetName.isNotEmpty() ? targetColour.withAlpha (0.8f) : theme::outline);
        g.drawRoundedRectangle (b, 4.0f, 1.0f);
        g.setColour (targetName.isNotEmpty() ? theme::text : theme::textDim);
        g.setFont (theme::font (11.0f, targetName.isNotEmpty()));
        g.drawFittedText (text, getLocalBounds().reduced (3, 0), juce::Justification::centred, 1, 0.6f);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        selection.select (trackId);
        showMidiToMenu (controller, trackId, this);
    }
private:
    EngineController& controller;
    Selection& selection;
    const Id trackId;
    juce::String targetName, text;
    juce::Colour targetColour;
};

// ---- the set -----------------------------------------------------------------------------------------
TrackControls::TrackControls (EngineController& c, Selection& s, PresetManager& p, Id trackId)
{
    icon = std::make_unique<PluginIconButton> (c, trackId);
    auto* sel = &s;
    icon->onPressed = [sel, trackId] { sel->select (trackId); };
    if (auto t = c.session().findTrack (trackId); t != nullptr && t->isMidiLike()) {
        auto* ctl = &c;
        juce::Component::SafePointer<PluginIconButton> btn (icon.get());
        const auto chain = t->kind == TrackKind::Midi ? FxChain::MidiFx : FxChain::Instrument;   // MIDI track: DrumBridge, arps...
        icon->onEmptyClick = [ctl, trackId, btn, chain] { if (btn != nullptr) showPluginMenu (*ctl, trackId, chain, -1, btn.getComponent()); };
    }
    buttons = std::make_unique<Buttons> (c, s, trackId);
    pre = std::make_unique<ChainButton> (c, s, p, trackId, FxChain::Pre);
    post = std::make_unique<ChainButton> (c, s, p, trackId, FxChain::Post);
    monitor = std::make_unique<MonitorButton> (c, s, trackId);
    if (auto t = c.session().findTrack (trackId)) {
        hasMonitor = t->kind == TrackKind::Audio || t->kind == TrackKind::Instrument;   // MIDI tracks always pass live MIDI
        if (t->isMidiLike()) midi = std::make_unique<ChainButton> (c, s, p, trackId, FxChain::MidiFx);
        if (t->kind == TrackKind::Midi) midiTo = std::make_unique<MidiToButton> (c, s, trackId);
    }

    gainBar = std::make_unique<ValueBar>();
    bindValueBar (*gainBar, c, trackId, { ParamKind::InputGain, kInvalidId, -1 });
    gainBar->label = "In"; gainBar->bipolar = true; gainBar->defaultValue = 0.5f;
    gainBar->format = [] (float v) { return stripfmt::signedDb (pm::inputGainToDb (v)); };
    gainBar->setTooltip ("Input gain (drag; double-click resets to 0 dB)");

    panBar = std::make_unique<ValueBar>();
    bindValueBar (*panBar, c, trackId, { ParamKind::Pan, kInvalidId, -1 });
    panBar->label = "Pan"; panBar->bipolar = true; panBar->defaultValue = 0.5f;
    panBar->format = [] (float v) { return stripfmt::panText (v); };
    panBar->setTooltip ("Pan (drag; double-click centres)");
    icon->refresh();
}

TrackControls::~TrackControls() = default;

void TrackControls::addTo (juce::Component& parent)
{
    parent.addAndMakeVisible (*icon);
    parent.addAndMakeVisible (*buttons);
    if (hasMonitor) parent.addAndMakeVisible (*monitor);
    if (midi) parent.addAndMakeVisible (*midi);
    if (midiTo) { parent.addAndMakeVisible (*midiTo); return; }   // no audio: no FX, In or Pan
    parent.addAndMakeVisible (*pre);
    parent.addAndMakeVisible (*post);
    parent.addAndMakeVisible (*gainBar);
    parent.addAndMakeVisible (*panBar);
}

void TrackControls::layout (juce::Rectangle<int> iconR, juce::Rectangle<int> msr, juce::Rectangle<int> monR, juce::Rectangle<int> fx,
                            juce::Rectangle<int> inR, juce::Rectangle<int> panR, juce::Rectangle<int> midiR)
{
    if (midiTo) {   // MIDI track: MIDI To where Pre / Post FX would be, MIDI FX under it
        midiTo->setBounds (fx);
        if (midi) midi->setBounds (inR);
        // Just M and record arm: two slim buttons, the same size as on the other tracks' four-button row.
        const int gap = 3, cw = (msr.getWidth() - gap * 3) / 4;
        msr = msr.withWidth (cw * 2 + gap);
    } else if (midi) midi->setBounds (midiR);
    gainBar->setBounds (inR);
    panBar->setBounds (panR);
    icon->setBounds (iconR);
    if (monR.isEmpty() && hasMonitor) {
        // The monitor button shares the M / S / R row as a fourth, equal-width "I" button.
        const int gap = 3, n = 4;   // M, S, R (tracks with a monitor always have record arm) + I
        const int cw = (msr.getWidth() - gap * (n - 1)) / n;
        monR = msr.removeFromRight (msr.getWidth() - (n - 1) * (cw + gap));
        msr.removeFromRight (gap);
    }
    buttons->setBounds (msr);
    monitor->setBounds (monR);
    const int w = (fx.getWidth() - 3) / 2;
    pre->setBounds (fx.withWidth (w));
    post->setBounds (fx.withTrimmedLeft (w + 3));
}

void TrackControls::refresh()
{
    icon->refresh();
    pre->refresh();
    post->refresh();
    if (midi) midi->refresh();
    if (midiTo) midiTo->refresh();
}

void TrackControls::poll()
{
    icon->poll();
    buttons->poll();
    pre->poll();
    post->poll();
    if (midi) midi->poll();
    if (hasMonitor) monitor->poll();
}

void TrackControls::pollValues()
{
    gainBar->poll();
    panBar->poll();
}

} // namespace el
