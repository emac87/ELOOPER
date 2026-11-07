#include "UI/TrackHeader.h"
#include "UI/ColourPicker.h"
#include "UI/LookAndFeel.h"
#include "UI/PluginBrowser.h"
#include "UI/PluginIcons.h"
#include "UI/Theme.h"
#include "Core/ParamMapping.h"
#include <cmath>

namespace el {

namespace {
    juce::String kindName (TrackKind k)
    {
        switch (k) {
            case TrackKind::Audio:      return "Audio";
            case TrackKind::Instrument: return "Instrument";
            case TrackKind::Bus:        return "Bus";
            case TrackKind::Master:     return "Master";
            case TrackKind::AuxOut:     return "Output";
        }
        return {};
    }
    const char* monitorName (MonitorMode m)
    {
        return m == MonitorMode::Auto ? "Auto" : m == MonitorMode::On ? "On" : "Off";
    }
    juce::String signedDb (float db)
    {
        if (db <= -100.0f) return "-inf";
        return (db > 0.05f ? "+" : "") + juce::String (db, 1);
    }
    juce::String panText (float n)
    {
        const float b = pm::panToBipolar (n);
        const int p = (int) std::lround (std::abs (b) * 50.0f);
        return p == 0 ? juce::String ("C") : (b < 0 ? "L" : "R") + juce::String (p);
    }

    // Layout metrics (row height 96)
    constexpr int x0 = 12;
    constexpr int rowAy = 5, rowAh = 26;
    constexpr int rowBy = 36, rowBh = 22;
    constexpr int rowCy = 64, rowCh = 24;
    constexpr int btnW = 26, btnGap = 3, monW = 44, folderW = 66;
    constexpr int chipGap = 3;
}

// ---- a compact horizontal value bar: drag left/right (or up/down) to change ----------------------
// Relative drag (it never jumps to where you click), shift for fine, double-click for default.
// The mouse wheel is left to the page so scrolling never changes a level by accident.
class ValueBar : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<float()> get;
    std::function<void (float)> set;
    std::function<void()> onBegin, onEnd;
    std::function<juce::String (float)> format;
    std::function<void (juce::Component*)> onRightClick;
    juce::String label;
    float defaultValue = 0.5f;
    bool bipolar = false;
    juce::Colour fill = theme::accent;

    void poll()
    {
        if (! get || dragging) return;
        const float v = get();
        if (std::abs (v - shown) > 1.0e-4f) { shown = v; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::background);
        g.fillRoundedRectangle (b, 4.0f);
        const float v = juce::jlimit (0.0f, 1.0f, shown);
        auto inner = b.reduced (1.5f);
        juce::Rectangle<float> f;
        if (bipolar) {
            const float cx = inner.getCentreX(), x = inner.getX() + inner.getWidth() * v;
            f = juce::Rectangle<float> (juce::jmin (cx, x), inner.getY(), std::abs (x - cx), inner.getHeight());
            g.setColour (theme::outline);
            g.fillRect (cx - 0.5f, inner.getY(), 1.0f, inner.getHeight());
        } else {
            f = inner.withWidth (inner.getWidth() * v);
        }
        g.setColour (fill.withAlpha (isMouseOverOrDragging() ? 0.55f : 0.4f));
        g.fillRoundedRectangle (f, 3.0f);
        g.setColour (isMouseOverOrDragging() ? fill.withAlpha (0.9f) : theme::outline);
        g.drawRoundedRectangle (b, 4.0f, 1.0f);
        g.setFont (theme::font (12.5f, true));
        g.setColour (theme::textDim.brighter (0.25f));
        auto t = getLocalBounds().reduced (6, 0);
        g.drawText (label, t, juce::Justification::centredLeft, true);
        g.setColour (theme::text);
        g.setFont (theme::font (12.5f));
        g.drawText (format ? format (v) : juce::String (v, 2), t, juce::Justification::centredRight, true);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (this); return; }
        if (! get) return;
        dragging = true;
        startValue = shown = get();
        if (onBegin) onBegin();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float span = (float) juce::jmax (80, getWidth()) * (e.mods.isShiftDown() ? 6.0f : 1.5f);
        const float d = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / span;
        const float v = juce::jlimit (0.0f, 1.0f, startValue + d);
        if (std::abs (v - shown) > 1.0e-5f) { shown = v; if (set) set (v); repaint(); }
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        if (onEnd) onEnd();
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || ! set) return;
        if (onBegin) onBegin();
        shown = defaultValue; set (defaultValue);
        if (onEnd) onEnd();
        repaint();
    }

private:
    bool dragging = false;
    float startValue = 0, shown = -1.0f;
};

// ---- one plug-in on the chip row ------------------------------------------------------------------
class FxChip : public juce::Component, public juce::SettableTooltipClient {
public:
    FxChip (TrackHeader& h, FxChain c, int idx, PluginSlotPtr s) : owner (h), chain (c), index (idx), slot (std::move (s))
    {
        if (slot) setTooltip (slot->name() + (chain == FxChain::Instrument ? "  (click: show / hide, right-click: options)"
                                                                           : "  (click: show / hide, dot: bypass, right-click: options)"));
        else setTooltip ("Choose an instrument");
    }

    int preferredWidth() const
    {
        const auto txt = slot ? slot->name() : juce::String ("+ Instrument");
        const int w = juce::GlyphArrangement::getStringWidthInt (theme::font (12.5f, chain == FxChain::Instrument), txt) + (hasLed() ? 24 : 14);
        return juce::jlimit (44, chain == FxChain::Instrument ? 120 : 100, w);
    }

    void poll()
    {
        if (! slot) return;
        const bool b = slot->bypassed.load(), o = owner.getController().isPluginEditorOpen (slot->id);
        if (b != bypassed || o != open) { bypassed = b; open = o; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        const bool inst = chain == FxChain::Instrument;
        auto t = owner.getController().session().findTrack (owner.getTrackId());
        const auto tc = t ? t->colour : theme::accent;
        g.setColour (open ? tc.withAlpha (0.3f) : (isMouseOver() ? theme::outline : theme::panelRaised));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (open ? tc : (inst ? tc.withAlpha (0.6f) : theme::outline));
        g.drawRoundedRectangle (b, 4.0f, open ? 1.5f : 1.0f);
        auto area = getLocalBounds().reduced (6, 0);
        if (hasLed()) {
            const auto led = ledBounds().toFloat();
            g.setColour (bypassed ? theme::outline.brighter (0.3f) : theme::play);
            g.fillEllipse (led);
            area.removeFromLeft (12);
        }
        if (! slot) { g.setColour (theme::accent); g.setFont (theme::font (12.5f, true)); g.drawText ("+ Instrument", area, juce::Justification::centredLeft, true); return; }
        g.setColour (slot->instance == nullptr ? theme::record : bypassed ? theme::textDim : theme::text);
        g.setFont (theme::font (12.5f, inst));
        g.drawText (slot->name(), area, juce::Justification::centredLeft, true);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) {
            if (slot) owner.showSlotMenu (chain, index, slot->id, this);
            else showPluginMenu (owner.getController(), owner.getTrackId(), FxChain::Instrument, 0, this);
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || e.mouseWasDraggedSinceMouseDown() || ! contains (e.getPosition())) return;
        auto& c = owner.getController();
        if (! slot) { showPluginMenu (c, owner.getTrackId(), FxChain::Instrument, 0, this); return; }
        if (hasLed() && ledBounds().expanded (5).contains (e.getPosition())) c.setPluginBypass (slot->id, ! slot->bypassed.load());
        else c.togglePluginEditor (slot->id);
        poll();
    }

    const PluginSlotPtr& getSlot() const noexcept { return slot; }
    FxChain getChain() const noexcept { return chain; }

private:
    bool hasLed() const { return slot != nullptr && chain != FxChain::Instrument; }
    juce::Rectangle<int> ledBounds() const { return { 7, (getHeight() - 8) / 2, 8, 8 }; }
    TrackHeader& owner;
    FxChain chain;
    int index;
    PluginSlotPtr slot;
    bool bypassed = false, open = false;
};

// ---- icon at the left of the name: click shows / hides the track's main plug-in ---------------------
class PluginIconButton : public juce::Component, public juce::SettableTooltipClient {
public:
    PluginIconButton (EngineController& c, Id id) : controller (c), trackId (id) {}

    std::function<void()> onPressed;

    void refresh()
    {
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr) return;
        auto slot = icons::iconSlot (*t);
        setTooltip (slot != nullptr ? slot->name() + "  (click: show / hide plug-in, right-click: all plug-ins on this track)"
                                    : icons::kindName (icons::channelKind (t->kind)) + " channel  (no plug-in loaded)");
        repaint();
    }

    void poll()
    {
        const bool open = anyEditorOpen();
        if (open != lit) { lit = open; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr) return;
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (lit ? t->colour.withAlpha (0.35f) : (hover ? theme::panelRaised.brighter (0.15f) : theme::panelRaised.darker (0.4f)));
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (lit ? t->colour : (hover ? t->colour.withAlpha (0.8f) : theme::outline));
        g.drawRoundedRectangle (b, 5.0f, lit ? 1.5f : 1.0f);
        const auto tint = lit ? juce::Colours::white : (hover ? t->colour.brighter (0.3f) : t->colour);
        icons::drawIcon (g, b.reduced (4.5f), icons::trackKind (*t), tint);
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
        poll();
    }

private:
    bool anyEditorOpen() const
    {
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr) return false;
        for (auto& p : t->allPlugins())
            if (p != nullptr && controller.isPluginEditorOpen (p->id)) return true;
        return false;
    }

    void showMenu()
    {
        auto t = controller.session().findTrack (trackId);
        if (t == nullptr) return;
        std::vector<Id> ids;
        juce::PopupMenu m;
        m.addSectionHeader ("Show / hide plug-in");
        for (auto& p : t->allPlugins()) {
            if (p == nullptr) continue;
            ids.push_back (p->id);
            juce::PopupMenu::Item item (p->name());
            item.itemID = (int) ids.size();
            item.image = icons::makeDrawable (icons::slotKind (*t, *p));
            item.setTicked (controller.isPluginEditorOpen (p->id));
            m.addItem (std::move (item));
        }
        if (ids.empty()) { m.addItem (-1, "No plug-ins on this track", false); }
        else {
            m.addSeparator();
            m.addItem (1000, "Show all");
            m.addItem (1001, "Hide all");
        }
        juce::Component::SafePointer<PluginIconButton> self (this);
        auto* c = &controller;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self, c, ids] (int r) {
            if (r <= 0) return;
            if (r == 1000) { for (auto id : ids) c->showPluginEditor (id); }
            else if (r == 1001) { for (auto id : ids) if (c->isPluginEditorOpen (id)) c->togglePluginEditor (id); }
            else if (r <= (int) ids.size()) c->togglePluginEditor (ids[(size_t) r - 1]);
            if (self != nullptr) self->poll();
        });
    }

    EngineController& controller;
    const Id trackId;
    bool hover = false, lit = false;
};

// ---- TrackHeader ------------------------------------------------------------------------------------
TrackHeader::~TrackHeader() = default;

TrackHeader::TrackHeader (EngineController& c, Selection& s, PresetManager& p, Id id, int width)
    : controller (c), selection (s), presets (p), trackId (id)
{
    iconButton = std::make_unique<PluginIconButton> (controller, trackId);
    iconButton->onPressed = [this] { selection.select (trackId, kInvalidId); };
    addAndMakeVisible (*iconButton);

    auto makeBar = [this] (ParamKind k, const juce::String& label, float def, std::function<juce::String (float)> fmt, bool bipolar) {
        auto b = std::make_unique<ValueBar>();
        const ParamTarget tgt { k, kInvalidId, -1 };
        auto* ctl = &controller; const Id tid = trackId;
        b->get = [ctl, tid, tgt] { return ctl->getParam (tid, tgt); };
        b->set = [ctl, tid, tgt] (float v) { ctl->setParam (tid, tgt, v); };
        b->onBegin = [ctl, tid, tgt] { ctl->beginParamGesture (tid, tgt); };
        b->onEnd = [ctl, tid, tgt] { ctl->endParamGesture (tid, tgt); };
        b->label = label; b->defaultValue = def; b->format = std::move (fmt); b->bipolar = bipolar;
        addAndMakeVisible (*b);
        return b;
    };
    gainBar = makeBar (ParamKind::InputGain, "In", 0.5f, [] (float v) { return signedDb (pm::inputGainToDb (v)) + " dB"; }, true);
    gainBar->setTooltip ("Input gain (drag; double-click resets to 0 dB)");
    volBar = makeBar (ParamKind::Volume, "Vol", pm::kUnityVolume, [] (float v) { return signedDb (pm::volumeToDb (v)) + " dB"; }, false);
    volBar->setTooltip ("Volume (drag; shift = fine; double-click resets to 0 dB)");
    panBar = makeBar (ParamKind::Pan, "Pan", 0.5f, [] (float v) { return panText (v); }, true);
    panBar->setTooltip ("Pan (drag; double-click centres)");

    overflowButton = std::make_unique<juce::TextButton> ("...");
    overflowButton->setTooltip ("Everything on this track's chain");
    overflowButton->setColour (juce::TextButton::buttonColourId, theme::panelRaised);
    overflowButton->onClick = [this] { showOverflowMenu (overflowButton.get()); };
    addChildComponent (*overflowButton);

    setSize (width, rowHeight);
    refresh();
    poll();
}

juce::String TrackHeader::chainSignature (const TrackModel& t) const
{
    juce::String s;
    s << (int) t.kind << '|';
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
        s << (int) sp.busId << (sp.preFader ? 'p' : 'q') << (bus ? bus->name : juce::String()) << ',';
    }
    return s;
}

void TrackHeader::rebuildChips (const TrackModel& t)
{
    for (auto& c : chips) removeChildComponent (c.get());
    chips.clear(); fxChips.clear(); sendBars.clear();

    auto addFx = [this] (FxChain chain, int idx, PluginSlotPtr slot) {
        auto chip = std::make_unique<FxChip> (*this, chain, idx, std::move (slot));
        fxChips.push_back (chip.get());
        addAndMakeVisible (*chip);
        chips.push_back (std::move (chip));
    };
    if (t.kind == TrackKind::Instrument) {
        addFx (FxChain::Instrument, 0, t.instrument);
        for (int i = 0; i < (int) t.midiFx.size(); ++i) addFx (FxChain::MidiFx, i, t.midiFx[(size_t) i]);
    }
    for (int i = 0; i < (int) t.preFx.size(); ++i) addFx (FxChain::Pre, i, t.preFx[(size_t) i]);
    for (int i = 0; i < (int) t.postFx.size(); ++i) addFx (FxChain::Post, i, t.postFx[(size_t) i]);

    {
        auto b = std::make_unique<juce::TextButton> ("+FX");
        b->setTooltip ("Add an effect");
        b->setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        b->setColour (juce::TextButton::textColourOffId, theme::accent);
        auto* raw = b.get();
        b->onClick = [this, raw] { showAddFxMenu (raw); };
        b->getProperties().set ("role", "addFx");
        addAndMakeVisible (*b);
        chips.push_back (std::move (b));
    }

    for (int i = 0; i < (int) t.sends.size(); ++i) {
        const auto& sp = t.sends[(size_t) i];
        auto bus = controller.session().findTrack (sp.busId);
        auto bar = std::make_unique<ValueBar>();
        const ParamTarget tgt { ParamKind::SendLevel, sp.busId, -1 };
        auto* ctl = &controller; const Id tid = trackId;
        bar->get = [ctl, tid, tgt] { return ctl->getParam (tid, tgt); };
        bar->set = [ctl, tid, tgt] (float v) { ctl->setParam (tid, tgt, v); };
        bar->onBegin = [ctl, tid, tgt] { ctl->beginParamGesture (tid, tgt); };
        bar->onEnd = [ctl, tid, tgt] { ctl->endParamGesture (tid, tgt); };
        bar->label = (bus ? bus->name : juce::String ("?")) + (sp.preFader ? " (pre)" : "");
        bar->defaultValue = 0.0f;
        bar->fill = bus ? bus->colour : theme::accent;
        bar->format = [] (float v) { return signedDb (pm::volumeToDb (v)); };
        bar->setTooltip ("Send to " + (bus ? bus->name : juce::String ("?")) + " (drag; right-click: pre/post, remove)");
        const int idx = i;
        bar->onRightClick = [this, idx] (juce::Component* c) { showSendMenu (idx, c); };
        sendBars.push_back (bar.get());
        addAndMakeVisible (*bar);
        chips.push_back (std::move (bar));
    }
    if (t.kind != TrackKind::Master) {
        auto b = std::make_unique<juce::TextButton> ("+Send");
        b->setTooltip ("Send this track to a bus");
        b->setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        b->setColour (juce::TextButton::textColourOffId, theme::accent);
        auto* raw = b.get();
        b->onClick = [this, raw] { showAddSendMenu (raw); };
        b->getProperties().set ("role", "addSend");
        addAndMakeVisible (*b);
        chips.push_back (std::move (b));
    }
    layoutChips();
}

void TrackHeader::layoutChips()
{
    // FX on the left part, sends on the right part. Whatever doesn't fit is reachable via "...".
    const int right = getWidth() - 6;
    const int total = right - x0;
    const int fxRight = x0 + (int) (total * 0.58f);
    bool overflow = false;

    auto place = [&] (int from, int to, int x, int limit) {
        // [from, to) are chips of one group; the last one is its "+" button and always shows.
        if (from >= to) return;
        const int lastIdx = to - 1;
        const int addW = chips[(size_t) lastIdx]->getProperties()["role"].toString() == "addSend" ? 48 : 38;
        int cx = x;
        for (int i = from; i < lastIdx; ++i) {
            auto* c = chips[(size_t) i].get();
            int w = 92;
            if (auto* fx = dynamic_cast<FxChip*> (c)) w = fx->preferredWidth();
            if (cx + w + chipGap + addW > limit) { c->setVisible (false); overflow = true; continue; }
            c->setVisible (true);
            c->setBounds (cx, rowCy, w, rowCh);
            cx += w + chipGap;
        }
        chips[(size_t) lastIdx]->setVisible (true);
        chips[(size_t) lastIdx]->setBounds (cx, rowCy, addW, rowCh);
    };

    int split = 0;
    while (split < (int) chips.size() && chips[(size_t) split]->getProperties()["role"].toString() != "addFx") ++split;
    const int fxEnd = juce::jmin ((int) chips.size(), split + 1);
    const int sendLimit = right - 30;   // room for "..." if needed
    place (0, fxEnd, x0, fxRight);
    place (fxEnd, (int) chips.size(), fxRight + 6, sendLimit);
    overflowButton->setVisible (overflow);
    if (overflow) overflowButton->setBounds (right - 26, rowCy, 26, rowCh);
}

void TrackHeader::refresh()
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    colour = t->colour;
    nameText = t->name;
    kind = t->kind;
    isFolderSum = t->kind == TrackKind::Bus && t->folderOf != kInvalidId;
    folderOpen = t->folderOpen;
    numOuts = 0;
    if (t->kind == TrackKind::Instrument)
        for (auto& o : controller.session().tracks) if (o->auxParent == trackId) ++numOuts;

    line1.clear();
    if (t->kind == TrackKind::Audio) {
        if (t->inputChannel < 0) line1 = "No input";
        else line1 = t->stereoInput ? "In " + juce::String (t->inputChannel + 1) + "/" + juce::String (t->inputChannel + 2)
                                    : "In " + juce::String (t->inputChannel + 1);
    } else if (t->kind == TrackKind::Instrument) {
        juce::String dev = "All MIDI";
        if (t->midiInputDevice.isNotEmpty()) {
            dev = t->midiInputDevice;
            for (auto& d : juce::MidiInput::getAvailableDevices())
                if (d.identifier == t->midiInputDevice) dev = d.name;
        }
        line1 = dev + (t->midiChannel > 0 ? " ch" + juce::String (t->midiChannel) : juce::String());
    } else if (t->kind == TrackKind::AuxOut) {
        line1 = "Output " + juce::String (t->auxPair * 2 + 1) + "/" + juce::String (t->auxPair * 2 + 2);
    } else if (isFolderSum) {
        line1 = "Sum of all outputs";
    } else {
        line1 = kindName (t->kind);
    }
    if (auto out = controller.session().findTrack (t->outputBus); out && t->kind != TrackKind::Master)
        line1 << "  >  " << out->name;

    if (iconButton != nullptr) iconButton->refresh();
    const auto sig = chainSignature (*t);
    if (sig != lastChainSig) { lastChainSig = sig; rebuildChips (*t); }
    resized();
    repaint();
}

void TrackHeader::resized()
{
    const int W = getWidth();
    if (iconButton != nullptr) iconButton->setBounds (x0, rowAy, rowAh, rowAh);
    const bool full = kind == TrackKind::Audio || kind == TrackKind::Instrument;
    int x = W - 6;
    auto take = [&x] (int w) { x -= w; juce::Rectangle<int> r (x, rowAy + 1, w, rowAh - 2); x -= btnGap; return r; };
    if (full) { monR = take (monW); armR = take (btnW); } else { monR = {}; armR = {}; }
    soloR = take (btnW);
    muteR = take (btnW);
    folderR = numOuts > 0 ? take (folderW) : juce::Rectangle<int>();

    // Row B: input gain, volume, pan, meter
    const int avail = W - 6 - x0;
    int bx = x0;
    const int gw = (int) (avail * 0.27f), vw = (int) (avail * 0.33f), pw = (int) (avail * 0.17f);
    gainBar->setBounds (bx, rowBy, gw, rowBh); bx += gw + 4;
    volBar->setBounds (bx, rowBy, vw, rowBh); bx += vw + 4;
    panBar->setBounds (bx, rowBy, pw, rowBh); bx += pw + 6;
    meterR = { bx, rowBy + 4, W - 6 - bx, rowBh - 8 };

    layoutChips();
}

TrackHeader::Button TrackHeader::buttonAt (juce::Point<int> p) const
{
    if (muteR.contains (p)) return Mute;
    if (soloR.contains (p)) return Solo;
    if (armR.contains (p)) return Arm;
    if (monR.contains (p)) return Monitor;
    if (folderR.contains (p)) return Folder;
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
        repaint (getLocalBounds().withHeight (rowAy + rowAh + 2));
    }
    if (iconButton != nullptr) iconButton->poll();
    gainBar->poll(); volBar->poll(); panBar->poll();
    for (auto* f : fxChips) f->poll();
    for (auto* b : sendBars) b->poll();
    const auto ms = controller.meter (trackId);
    const int l = juce::jlimit (0, 48, (int) (ms.left * 48.0f)), r = juce::jlimit (0, 48, (int) (ms.right * 48.0f));
    if (l != meterL || r != meterR2) { meterL = l; meterR2 = r; repaint (meterR.expanded (1)); }
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (b, 6.0f);
    {   // colour stripe down the left edge
        juce::Path p;
        p.addRoundedRectangle (b.getX(), b.getY(), 6.0f, b.getHeight(), 6.0f, 6.0f, true, false, true, false);
        g.setColour (colour);
        g.fillPath (p);
    }
    const bool sel = selection.trackId == trackId;
    g.setColour (sel ? (selection.loopId == kInvalidId ? juce::Colours::white : colour.withAlpha (0.8f)) : theme::outline);
    g.drawRoundedRectangle (b, 6.0f, sel ? 1.5f : 1.0f);

    // name + one line of details
    const int tx = x0 + rowAh + 6;
    const int nameRight = (folderR.isEmpty() ? muteR.getX() : folderR.getX()) - 4;
    g.setColour (theme::text);
    g.setFont (theme::font (14.5f, true));
    g.drawText (nameText, tx, rowAy - 2, nameRight - tx, 17, juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.5f));
    g.drawText (line1, tx, rowAy + 14, nameRight - tx, 13, juce::Justification::centredLeft, true);

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
        drawBtn (monR, monitorName (monitor), monitor == MonitorMode::On, theme::accent);
    }
    if (! folderR.isEmpty())
        drawBtn (folderR, juce::String (folderOpen ? "v " : "> ") + juce::String (numOuts) + " outs", false, theme::accent);

    // horizontal stereo meter
    auto mr = meterR.toFloat();
    g.setColour (theme::background);
    g.fillRoundedRectangle (mr, 2.0f);
    const float bh = (mr.getHeight() - 1.0f) * 0.5f;
    auto bar = [&] (float y, int v) {
        if (v <= 0) return;
        const float w = mr.getWidth() * (float) v / 48.0f;
        g.setColour (v > 44 ? theme::meterHigh : v > 34 ? theme::meterMid : theme::meterLow);
        g.fillRect (mr.getX(), y, w, bh);
    };
    bar (mr.getY(), meterL);
    bar (mr.getY() + bh + 1.0f, meterR2);
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    selection.select (trackId, kInvalidId);
    if (e.mods.isPopupMenu()) { showMenu(); return; }

    auto t = controller.session().findTrack (trackId);
    if (t == nullptr || t->rt == nullptr) return;
    switch (buttonAt (e.getPosition())) {
        case Mute: {
            const ParamTarget tgt { ParamKind::Mute, kInvalidId, -1 };
            controller.beginParamGesture (trackId, tgt);
            controller.setParam (trackId, tgt, t->rt->mute.load() ? 0.0f : 1.0f);
            controller.endParamGesture (trackId, tgt);
            poll(); break;
        }
        case Solo:  t->rt->solo.store (! t->rt->solo.load()); poll(); break;
        case Arm:   controller.setArmed (trackId, ! t->rt->armed.load()); poll(); break;
        case Monitor: {
            const auto m = t->rt->monitor.load();
            controller.setMonitorMode (trackId, m == MonitorMode::Auto ? MonitorMode::On
                                              : m == MonitorMode::On ? MonitorMode::Off : MonitorMode::Auto);
            poll();
            break;
        }
        case Folder: {
            auto* c = &controller; const Id id = trackId; const bool open = ! folderOpen;
            // Deferred: the change rebuilds the page, which deletes this header.
            juce::MessageManager::callAsync ([c, id, open] { c->setFolderOpen (id, open); });
            break;
        }
        case None: break;
    }
}

void TrackHeader::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || buttonAt (e.getMouseDownPosition()) != None || ! onReorderDrop) return;
    if (! dragging && std::abs (e.getDistanceFromDragStartY()) > 10) {
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
    if (buttonAt (e.getPosition()) == None && ! e.mods.isPopupMenu() && e.y < rowBy) rename();
}

// ---- chip menus ---------------------------------------------------------------------------------------
void TrackHeader::showAddFxMenu (juce::Component* target)
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    if (t->kind != TrackKind::Instrument) { showPluginMenu (controller, trackId, FxChain::Pre, -1, target); return; }
    auto* c = &controller; const Id id = trackId;
    juce::Component::SafePointer<juce::Component> tgt (target);
    juce::PopupMenu m;
    m.addItem (1, "Audio effect...");
    m.addItem (2, "MIDI effect...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [c, id, tgt] (int r) {
        if (r == 1) showPluginMenu (*c, id, FxChain::Pre, -1, tgt.getComponent());
        else if (r == 2) showPluginMenu (*c, id, FxChain::MidiFx, -1, tgt.getComponent());
    });
}

void TrackHeader::showAddSendMenu (juce::Component* target)
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    juce::PopupMenu m;
    int n = 0;
    for (auto& bus : controller.session().buses()) {
        if (bus->id == trackId) continue;
        const bool used = std::any_of (t->sends.begin(), t->sends.end(), [&] (const SendSpec& s) { return s.busId == bus->id; });
        m.addItem ((int) bus->id, bus->name, ! used);
        ++n;
    }
    m.addSeparator();
    m.addItem (-2, "New bus + send");
    if (n == 0) m.addItem (-1, "(no buses yet)", false);
    auto* c = &controller; const Id id = trackId;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [c, id] (int r) {
        if (r == -2) {
            const Id bus = c->addTrack (TrackKind::Bus);
            if (bus != kInvalidId && c->addSend (id, bus, false) < 0) ui::alert ("Send", "That send isn't possible.");
            return;
        }
        if (r <= 0) return;
        if (c->addSend (id, (Id) r, false) < 0)
            ui::alert ("Send", "That send isn't possible: it would create a feedback loop, or the track has too many sends.");
    });
}

void TrackHeader::showSendMenu (int sendIndex, juce::Component* target)
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr || sendIndex < 0 || sendIndex >= (int) t->sends.size()) return;
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

void TrackHeader::showSlotMenu (FxChain chain, int index, Id slotId, juce::Component* target)
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    auto slot = t->findPlugin (slotId);
    if (slot == nullptr) return;
    const int count = chain == FxChain::MidiFx ? (int) t->midiFx.size()
                    : chain == FxChain::Pre ? (int) t->preFx.size()
                    : chain == FxChain::Post ? (int) t->postFx.size() : 1;
    const bool movable = chain == FxChain::Pre || chain == FxChain::Post;
    juce::PopupMenu m;
    m.addSectionHeader (slot->name());
    m.addItem (1, controller.isPluginEditorOpen (slotId) ? "Hide window" : "Show window");
    if (chain != FxChain::Instrument) m.addItem (8, "Bypass", true, slot->bypassed.load());
    if (chain == FxChain::Instrument) m.addItem (2, "Replace...");
    m.addItem (3, "Presets...");
    if (chain != FxChain::Instrument) {
        m.addSeparator();
        m.addItem (4, "Move left", index > 0);
        m.addItem (5, "Move right", index < count - 1);
        if (movable) m.addItem (6, chain == FxChain::Pre ? "Move after the fader" : "Move before the fader");
    }
    m.addSeparator();
    m.addItem (7, "Remove");

    juce::Component::SafePointer<juce::Component> tgt (target);
    auto* c = &controller; auto* pm2 = &presets; const Id tid = trackId;
    const bool wasBypassed = slot->bypassed.load();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [c, pm2, tgt, tid, slotId, chain, index, wasBypassed] (int r) {
        switch (r) {
            case 1: c->togglePluginEditor (slotId); break;
            case 2: showPluginMenu (*c, tid, FxChain::Instrument, -1, tgt.getComponent()); break;
            case 3: if (tgt != nullptr) pm2->showPluginPresetMenu (slotId, tgt.getComponent()); break;
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
    });
}

void TrackHeader::showOverflowMenu (juce::Component* target)
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    juce::PopupMenu m;
    std::vector<Id> ids;
    m.addSectionHeader ("Plug-ins (show / hide)");
    for (auto& p : t->allPlugins()) {
        if (p == nullptr) continue;
        ids.push_back (p->id);
        m.addItem ((int) ids.size(), p->name() + (p->bypassed.load() ? "  (bypassed)" : ""), true, controller.isPluginEditorOpen (p->id));
    }
    if (! t->sends.empty()) {
        m.addSectionHeader ("Sends (remove)");
        for (int i = 0; i < (int) t->sends.size(); ++i) {
            auto bus = controller.session().findTrack (t->sends[(size_t) i].busId);
            m.addItem (500 + i, "Remove send to " + (bus ? bus->name : juce::String ("?")));
        }
    }
    auto* c = &controller; const Id tid = trackId;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [c, ids, tid] (int r) {
        if (r >= 500) c->removeSend (tid, r - 500);
        else if (r > 0 && r <= (int) ids.size()) c->togglePluginEditor (ids[(size_t) r - 1]);
    });
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
    if (t->kind == TrackKind::Instrument && t->instrument != nullptr && t->instrument->numOutputChannels > 2)
        am.add (m, numOuts > 0 ? "Multi-Out: Update Outputs + Sum Folder" : "Multi-Out: Create Outputs + Sum Folder",
                [c, id] { c->createMultiOutTracks (id); });
    else if (t->kind == TrackKind::Instrument)
        am.add (m, "Multi-Out (instrument has a single stereo output)", {}, false, false);

    m.addSeparator();
    if (t->kind != TrackKind::Master) {
        am.add (m, "Bounce Track", [self] { if (self != nullptr) self->confirmBounce(); });
        am.add (m, "Remove Track", [self] { if (self != nullptr) self->confirmRemove(); });
    }
    am.show (m, this);
}

} // namespace el
