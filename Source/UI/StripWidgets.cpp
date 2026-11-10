#include "UI/StripWidgets.h"
#include "UI/PluginIcons.h"
#include "UI/Theme.h"
#include "Core/ParamMapping.h"
#include <cmath>

namespace el {

namespace stripfmt {
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
}

void bindValueBar (ValueBar& bar, EngineController& c, Id trackId, ParamTarget tgt)
{
    auto* ctl = &c;
    bar.get = [ctl, trackId, tgt] { return ctl->getParam (trackId, tgt); };
    bar.set = [ctl, trackId, tgt] (float v) { ctl->setParam (trackId, tgt, v); };
    bar.onBegin = [ctl, trackId, tgt] { ctl->beginParamGesture (trackId, tgt); };
    bar.onEnd = [ctl, trackId, tgt] { ctl->endParamGesture (trackId, tgt); };
}

// ---- FxChip -----------------------------------------------------------------------------------
FxChip::FxChip (EngineController& c, Id t, FxChain ch, int idx, PluginSlotPtr s)
    : controller (c), trackId (t), chain (ch), index (idx), slot (std::move (s))
{
    if (slot) setTooltip (slot->name() + (chain == FxChain::Instrument ? "  (click: show / hide, right-click: options)"
                                                                       : "  (click: show / hide, dot: bypass, right-click: options)"));
}

void FxChip::poll()
{
    if (! slot) return;
    const bool b = slot->bypassed.load(), o = controller.isPluginEditorOpen (slot->id);
    if (b != bypassed || o != open) { bypassed = b; open = o; repaint(); }
}

void FxChip::paint (juce::Graphics& g)
{
    if (! slot) return;
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool inst = chain == FxChain::Instrument;
    auto t = controller.session().findTrack (trackId);
    const auto tc = t ? t->colour : theme::accent;
    g.setColour (open ? tc.withAlpha (0.3f) : (isMouseOver() ? theme::outline : theme::panelRaised));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (open ? tc : (inst ? tc.withAlpha (0.6f) : theme::outline));
    g.drawRoundedRectangle (b, 4.0f, open ? 1.5f : 1.0f);
    auto area = getLocalBounds().reduced (6, 0);
    if (hasLed()) {
        g.setColour (bypassed ? theme::outline.brighter (0.3f) : theme::play);
        g.fillEllipse (ledBounds().toFloat());
        area.removeFromLeft (12);
    }
    if (t != nullptr && getWidth() >= 90) {   // generic type icon when there is room
        icons::drawIcon (g, area.removeFromLeft (16).toFloat().withSizeKeepingCentre (14.0f, 14.0f),
                         icons::slotKind (*t, *slot), inst ? tc : theme::textDim);
        area.removeFromLeft (4);
    }
    g.setColour (slot->instance == nullptr ? theme::record : bypassed ? theme::textDim : theme::text);
    g.setFont (theme::font (12.5f, inst));
    g.drawText (slot->name(), area, juce::Justification::centredLeft, true);
}

void FxChip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onMenu) onMenu (*this);
}

void FxChip::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mouseWasDraggedSinceMouseDown() || ! contains (e.getPosition()) || ! slot) return;
    if (hasLed() && ledBounds().expanded (5).contains (e.getPosition())) controller.setPluginBypass (slot->id, ! slot->bypassed.load());
    else controller.togglePluginEditor (slot->id);
    poll();
}

// ---- EmptySlot / MenuSlot ----------------------------------------------------------------------
void EmptySlot::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool hot = isMouseOver();
    g.setColour (hot ? theme::panelRaised : theme::background);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (hot ? theme::accent.withAlpha (0.7f) : theme::outline);
    const float dash[] = { 3.0f, 3.0f };
    juce::Path p; p.addRoundedRectangle (b, 4.0f);
    juce::Path dashed;
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dash, 2);
    g.fillPath (dashed);
    g.setColour (hot ? theme::accent : theme::textDim);
    g.setFont (theme::font (12.0f));
    g.drawText (hint, getLocalBounds().reduced (6, 0), juce::Justification::centred, true);
}

// ---- PluginIconButton -------------------------------------------------------------------------
PluginIconButton::PluginIconButton (EngineController& c, Id id) : controller (c), trackId (id) {}

void PluginIconButton::refresh()
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    if (interactive) {
        auto slot = icons::iconSlot (*t);
        setTooltip (slot != nullptr ? slot->name() + "  (click: show / hide plug-in, right-click: all plug-ins on this track)"
                                    : icons::kindName (icons::channelKind (t->kind)) + " channel  (no plug-in loaded"
                                      + juce::String (onEmptyClick ? ": click to add one)" : ")"));
    }
    repaint();
}

void PluginIconButton::poll()
{
    const bool open = interactive && anyEditorOpen();
    if (open != lit) { lit = open; repaint(); }
}

void PluginIconButton::paint (juce::Graphics& g)
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

void PluginIconButton::mouseDown (const juce::MouseEvent& e)
{
    if (onPressed) onPressed();
    if (interactive && e.mods.isPopupMenu()) showMenu();
}

void PluginIconButton::mouseUp (const juce::MouseEvent& e)
{
    if (! interactive || e.mods.isPopupMenu() || ! contains (e.getPosition())) return;
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    if (auto slot = icons::iconSlot (*t)) controller.togglePluginEditor (slot->id);
    else if (onEmptyClick) { onEmptyClick(); return; }   // may rebuild the page: touch nothing after
    poll();
}

bool PluginIconButton::anyEditorOpen() const
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return false;
    for (auto& p : t->allPlugins())
        if (p != nullptr && controller.isPluginEditorOpen (p->id)) return true;
    return false;
}

void PluginIconButton::showMenu()
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
    if (ids.empty()) m.addItem (-1, "No plug-ins on this track", false);
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

} // namespace el
