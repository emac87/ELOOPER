#include "UI/TrackControls.h"
#include "UI/StripWidgets.h"
#include "UI/PluginBrowser.h"
#include "UI/TrackMenu.h"
#include "UI/Theme.h"

namespace el {

// ---- Mute / Solo / Record arm ----------------------------------------------------------------------
class TrackControls::Buttons : public juce::Component, public juce::SettableTooltipClient {
public:
    Buttons (EngineController& c, Selection& s, Id id) : controller (c), selection (s), trackId (id)
    {
        auto t = controller.session().findTrack (trackId);
        hasArm = t != nullptr && (t->kind == TrackKind::Audio || t->kind == TrackKind::Instrument);
        setTooltip (hasArm ? "Mute / Solo / Record arm" : "Mute / Solo");
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
        draw (1, "S", soloed, theme::armed);
        if (hasArm) {
            draw (2, {}, armed, theme::record);
            const auto c = rect (2).toFloat().getCentre();
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
        } else if (i == 1) {
            t->rt->solo.store (! t->rt->solo.load());
        } else if (i == 2) {
            controller.setArmed (trackId, ! t->rt->armed.load());
        }
        poll();
    }

    void mouseUp (const juce::MouseEvent&) override { endMute(); }

private:
    int count() const { return hasArm ? 3 : 2; }
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
    bool hasArm = false, muted = false, soloed = false, armed = false, muteOpen = false;
};

// ---- Pre FX / Post FX ------------------------------------------------------------------------------
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
        const auto& list = chain == FxChain::Pre ? t->preFx : t->postFx;
        count = (int) list.size();
        colour = t->colour;
        juce::String tip = chain == FxChain::Pre ? "Pre FX (before the fader)" : "Post FX (after the fader)";
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
        for (auto& p : chain == FxChain::Pre ? t->preFx : t->postFx)
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
        juce::String text = chain == FxChain::Pre ? "Pre FX" : "Post FX";
        if (count > 1) text << " " << count;
        g.drawFittedText (text, getLocalBounds().reduced (2, 0), juce::Justification::centred, 1, 0.8f);
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
};

// ---- the set -----------------------------------------------------------------------------------------
TrackControls::TrackControls (EngineController& c, Selection& s, PresetManager& p, Id trackId)
{
    icon = std::make_unique<PluginIconButton> (c, trackId);
    auto* sel = &s;
    icon->onPressed = [sel, trackId] { sel->select (trackId); };
    if (auto t = c.session().findTrack (trackId); t != nullptr && t->kind == TrackKind::Instrument) {
        auto* ctl = &c;
        juce::Component::SafePointer<PluginIconButton> btn (icon.get());
        icon->onEmptyClick = [ctl, trackId, btn] { if (btn != nullptr) showPluginMenu (*ctl, trackId, FxChain::Instrument, -1, btn.getComponent()); };
    }
    buttons = std::make_unique<Buttons> (c, s, trackId);
    pre = std::make_unique<ChainButton> (c, s, p, trackId, FxChain::Pre);
    post = std::make_unique<ChainButton> (c, s, p, trackId, FxChain::Post);
    icon->refresh();
}

TrackControls::~TrackControls() = default;

void TrackControls::addTo (juce::Component& parent)
{
    parent.addAndMakeVisible (*icon);
    parent.addAndMakeVisible (*buttons);
    parent.addAndMakeVisible (*pre);
    parent.addAndMakeVisible (*post);
}

void TrackControls::setBox (juce::Rectangle<int> b)
{
    const int m = 6;
    icon->setBounds (b.getX() + m, b.getY() + m, 22, 22);
    buttons->setBounds (b.getX() + m + 25, b.getY() + m, b.getWidth() - 2 * m - 25, 22);
    const int w = (b.getWidth() - 2 * m - 3) / 2;
    pre->setBounds (b.getX() + m, b.getY() + fxBandY, w, fxBandH);
    post->setBounds (b.getX() + m + w + 3, b.getY() + fxBandY, b.getWidth() - 2 * m - w - 3, fxBandH);
}

void TrackControls::refresh()
{
    icon->refresh();
    pre->refresh();
    post->refresh();
}

void TrackControls::poll()
{
    icon->poll();
    buttons->poll();
    pre->poll();
    post->poll();
}

} // namespace el
