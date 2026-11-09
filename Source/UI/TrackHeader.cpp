#include "UI/TrackHeader.h"
#include "UI/StripWidgets.h"
#include "UI/Theme.h"
#include "UI/TrackMenu.h"

namespace el {

TrackHeader::TrackHeader (EngineController& c, Selection& s, PresetManager& p, Id id, int width)
    : controller (c), selection (s), presets (p), trackId (id)
{
    icon = std::make_unique<PluginIconButton> (controller, trackId);
    icon->interactive = false;                 // the loop page has no controls: just the type icon
    icon->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*icon);
    setTooltip ("Click: select. Double-click: rename. Right-click: track menu. Drag sideways: reorder.");
    setSize (width, rowHeight);
    refresh();
}

TrackHeader::~TrackHeader() = default;

void TrackHeader::refresh()
{
    auto t = controller.session().findTrack (trackId);
    if (t == nullptr) return;
    colour = t->colour;
    nameText = t->name;
    folderOpen = t->folderOpen;
    numOuts = 0;
    if (t->kind == TrackKind::Instrument)
        for (auto& o : controller.session().tracks) if (o->auxParent == trackId) ++numOuts;

    line1.clear();
    switch (t->kind) {
        case TrackKind::Audio:
            line1 = t->inputChannel < 0 ? juce::String ("No input")
                  : t->stereoInput ? "In " + juce::String (t->inputChannel + 1) + "/" + juce::String (t->inputChannel + 2)
                                   : "In " + juce::String (t->inputChannel + 1);
            break;
        case TrackKind::Instrument: line1 = t->instrument != nullptr ? t->instrument->name() : juce::String ("No instrument"); break;
        case TrackKind::AuxOut:     line1 = "Output " + juce::String (t->auxPair * 2 + 1) + "/" + juce::String (t->auxPair * 2 + 2); break;
        case TrackKind::Bus:        line1 = t->folderOf != kInvalidId ? "Sum folder" : "Bus"; break;
        case TrackKind::Master:     line1 = "Master"; break;
    }
    icon->refresh();
    resized();
    repaint();
}

void TrackHeader::resized()
{
    icon->setBounds (6, 11, 24, 24);
    folderR = numOuts > 0 ? juce::Rectangle<int> (5, getHeight() - 25, getWidth() - 10, 20) : juce::Rectangle<int>();
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (b, 6.0f);
    {
        juce::Path p;   // colour stripe along the top
        p.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), 5.0f, 6.0f, 6.0f, true, true, false, false);
        g.setColour (colour);
        g.fillPath (p);
    }
    const bool sel = selection.trackId == trackId;
    g.setColour (sel ? (selection.loopId == kInvalidId ? juce::Colours::white : colour.withAlpha (0.8f)) : theme::outline);
    g.drawRoundedRectangle (b, 6.0f, sel ? 1.5f : 1.0f);

    const int w = getWidth();
    g.setColour (theme::text);
    g.setFont (theme::font (13.0f, true));
    g.drawFittedText (nameText, 34, 8, w - 38, 30, juce::Justification::centredLeft, 2, 0.85f);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f));
    const int lineY = folderR.isEmpty() ? 44 : 40;
    g.drawFittedText (line1, 6, lineY, w - 10, folderR.isEmpty() ? 44 : 30, juce::Justification::topLeft, folderR.isEmpty() ? 3 : 2, 0.9f);

    if (! folderR.isEmpty()) {
        auto r = folderR.toFloat();
        g.setColour (theme::panelRaised);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (theme::accent);
        g.setFont (theme::font (11.5f, true));
        g.drawText (juce::String (folderOpen ? "Hide " : "Show ") + juce::String (numOuts) + " outs", folderR, juce::Justification::centred);
    }
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    selection.select (trackId, kInvalidId);
    if (e.mods.isPopupMenu()) { showTrackMenu (controller, presets, trackId, this); return; }
    if (folderR.contains (e.getPosition())) {
        auto* c = &controller; const Id id = trackId; const bool open = ! folderOpen;
        // Deferred: the change rebuilds the page, which deletes this header.
        juce::MessageManager::callAsync ([c, id, open] { c->setFolderOpen (id, open); });
    }
}

void TrackHeader::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || folderR.contains (e.getMouseDownPosition()) || ! onReorderDrop) return;
    if (! dragging && std::abs (e.getDistanceFromDragStartX()) > 10) {
        dragging = true;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setAlpha (0.6f);
    }
}

void TrackHeader::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging) return;
    dragging = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    setAlpha (1.0f);
    if (onReorderDrop) onReorderDrop (e);
}

void TrackHeader::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && ! folderR.contains (e.getPosition())) renameTrack (controller, trackId);
}

} // namespace el
