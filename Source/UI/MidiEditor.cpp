#include "UI/MidiEditor.h"
#include "UI/Theme.h"
#include <cmath>

namespace el {

MidiEditor::MidiEditor (EngineController& c, Selection& s) : controller (c), selection (s)
{
    setWantsKeyboardFocus (true);

    for (auto* b : { &pianoTab, &stepTab })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.35f));
        b->setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        b->setColour (juce::TextButton::textColourOnId, theme::text);
        b->setColour (juce::TextButton::textColourOffId, theme::textDim);
        addAndMakeVisible (*b);
    }
    pianoTab.setToggleState (true, juce::dontSendNotification);
    pianoTab.onClick = [this] { setTab (0); };
    stepTab.onClick = [this] { setTab (1); };

    gridBox.addItem ("1/4", 1);
    gridBox.addItem ("1/8", 2);
    gridBox.addItem ("1/16", 3);
    gridBox.addItem ("1/32", 4);
    gridBox.setSelectedId (3, juce::dontSendNotification);
    gridBox.onChange = [this] { userChangedGrid(); };
    addAndMakeVisible (gridBox);

    tripletToggle.onClick = [this] { userChangedGrid(); };
    addAndMakeVisible (tripletToggle);
    snapToggle.setToggleState (true, juce::dontSendNotification);
    snapToggle.onClick = [this] { snapOn = snapToggle.getToggleState(); piano.setGrid (currentGridBeats(), snapOn); };
    addAndMakeVisible (snapToggle);

    quantizeButton.onClick = [this] { quantizeClip(); };
    addAndMakeVisible (quantizeButton);

    lenMinus.onClick = [this] { changeLength (-1); };
    lenPlus.onClick = [this] { changeLength (1); };
    addAndMakeVisible (lenMinus);
    addAndMakeVisible (lenPlus);
    lenLabel.setJustificationType (juce::Justification::centred);
    lenLabel.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (lenLabel);

    velLabel.setText ("Vel", juce::dontSendNotification);
    velLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (velLabel);
    velSlider.setSliderStyle (juce::Slider::LinearBar);
    velSlider.setRange (1, 127, 1);
    velSlider.setValue (100, juce::dontSendNotification);
    velSlider.onValueChange = [this]
    {
        piano.setNewVelocity ((int) velSlider.getValue());
        steps.setNewVelocity ((int) velSlider.getValue());
    };
    addAndMakeVisible (velSlider);

    zoomOut.onClick = [this] { if (tab == 0) piano.zoom (0.8f); else steps.zoom (0.8f); };
    zoomIn.onClick = [this] { if (tab == 0) piano.zoom (1.25f); else steps.zoom (1.25f); };
    addAndMakeVisible (zoomOut);
    addAndMakeVisible (zoomIn);

    for (auto* e : { &piano })
    {
        e->beginEdit = [this] { pushUndo(); };
        e->commitEdit = [this] { commitClip(); };
    }
    steps.beginEdit = [this] { pushUndo(); };
    steps.commitEdit = [this] { commitClip(); };
    addAndMakeVisible (piano);
    addChildComponent (steps);
    piano.setClip (&clip);
    steps.setClip (&clip, true);

    controller.addListener (this);
    selection.addChangeListener (this);
    selectionChanged();
    startTimerHz (theme::uiRefreshHz);
}

MidiEditor::~MidiEditor()
{
    stopTimer();
    selection.removeChangeListener (this);
    controller.removeListener (this);
}

//==============================================================================
void MidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    if (loopId == kInvalidId)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (15.0f));
        g.drawText ("Select a MIDI loop", getLocalBounds(), juce::Justification::centred);
        return;
    }
    g.setColour (theme::panel);
    g.fillRect (0, 0, getWidth(), headerH);
    g.setColour (theme::outline);
    g.fillRect (0, headerH - 1, getWidth(), 1);
    if (auto lp = controller.session().findLoop (loopId))
    {
        g.setColour (controller.loopColour (loopId));
        g.setFont (theme::font (14.0f, true));
        g.drawText (lp->name.isNotEmpty() ? lp->name : "MIDI loop", 10, 0, 130, headerH, juce::Justification::centredLeft, true);
    }
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f));
    g.drawText ("Length", lenMinus.getX() - 46, 0, 44, headerH, juce::Justification::centredRight);
}

void MidiEditor::resized()
{
    auto r = getLocalBounds();
    auto h = r.removeFromTop (headerH).reduced (4, 4);
    h.removeFromLeft (140);
    pianoTab.setBounds (h.removeFromLeft (84));
    stepTab.setBounds (h.removeFromLeft (84));
    h.removeFromLeft (10);
    gridBox.setBounds (h.removeFromLeft (64));
    h.removeFromLeft (4);
    tripletToggle.setBounds (h.removeFromLeft (70));
    snapToggle.setBounds (h.removeFromLeft (60));
    quantizeButton.setBounds (h.removeFromLeft (76));
    h.removeFromLeft (52);
    lenMinus.setBounds (h.removeFromLeft (24));
    lenLabel.setBounds (h.removeFromLeft (64));
    lenPlus.setBounds (h.removeFromLeft (24));
    h.removeFromLeft (10);
    velLabel.setBounds (h.removeFromLeft (28));
    velSlider.setBounds (h.removeFromLeft (110));
    h.removeFromLeft (10);
    zoomOut.setBounds (h.removeFromLeft (24));
    zoomIn.setBounds (h.removeFromLeft (24));
    piano.setBounds (r);
    steps.setBounds (r);
}

void MidiEditor::updateVisibility()
{
    const bool has = loopId != kInvalidId;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &pianoTab, &stepTab, &gridBox, &tripletToggle, &snapToggle, &quantizeButton,
                                                                           &lenMinus, &lenPlus, &lenLabel, &velSlider, &velLabel, &zoomOut, &zoomIn })
        c->setVisible (has);
    piano.setVisible (has && tab == 0);
    steps.setVisible (has && tab == 1);
    repaint();
}

//==============================================================================
void MidiEditor::changeListenerCallback (juce::ChangeBroadcaster*) { selectionChanged(); }

void MidiEditor::selectionChanged()
{
    Id id = kInvalidId;
    if (selection.loopId != kInvalidId)
        if (auto lp = controller.session().findLoop (selection.loopId))
            if (lp->type == LoopType::Midi) id = selection.loopId;

    if (id != loopId)
    {
        loopId = id;
        undoStack.clear();
        redoStack.clear();
        if (loopId != kInvalidId) reloadClip (true, true);
    }
    updateVisibility();
    if (loopId != kInvalidId)
    {
        const auto col = controller.loopColour (loopId);
        piano.setLoopColour (col);
        steps.setLoopColour (col);
        piano.setBeatsPerBar (controller.session().transport.beatsPerBar);
        syncHeader();
    }
}

void MidiEditor::sessionChanged (Change what, Id id)
{
    if (committing) return;
    if (what == Change::Session || what == Change::Structure)
    {
        selectionChanged();
        if (loopId != kInvalidId) reloadClip (false, false);
        return;
    }
    if (loopId == kInvalidId || id != loopId) return;
    if (what == Change::Clip || what == Change::Loop)
    {
        reloadClip (false, false);
        const auto col = controller.loopColour (loopId);
        piano.setLoopColour (col);
        steps.setLoopColour (col);
        repaint (0, 0, getWidth(), headerH);
    }
}

bool MidiEditor::sameClip (const MidiClip& a, const MidiClip& b)
{
    if (a.lengthBeats != b.lengthBeats || a.stepBeats != b.stepBeats || a.notes.size() != b.notes.size()
        || a.controllers.size() != b.controllers.size())
        return false;
    for (size_t i = 0; i < a.notes.size(); ++i)
    {
        const auto &x = a.notes[i], &y = b.notes[i];
        if (x.start != y.start || x.length != y.length || x.pitch != y.pitch || x.velocity != y.velocity
            || x.channel != y.channel || x.muted != y.muted)
            return false;
    }
    return true;
}

void MidiEditor::reloadClip (bool force, bool resetRows)
{
    if (loopId == kInvalidId) return;
    MidiClip fresh;
    if (auto* c = controller.midiClip (loopId)) fresh = *c;
    else
    {
        const auto& ts = controller.session().transport;
        double len = controller.loopStatus (loopId).lengthBeats;
        if (len <= 0)
        {
            if (auto lp = controller.session().findLoop (loopId))
                len = (lp->settings.lengthMode == RecordLength::Bars ? lp->settings.recordBars : ts.masterBars) * ts.beatsPerBar;
        }
        fresh.lengthBeats = len > 0 ? len : 16.0;
    }
    if (! force && sameClip (fresh, clip)) return;
    clip = fresh;

    // Derive step grid from the clip.
    const int ds[] = { 4, 8, 16, 32 };
    divs[1] = 16; trips[1] = false;
    for (int d : ds)
    {
        if (std::abs (clip.stepBeats - 4.0 / d) < 1.0e-6) { divs[1] = d; trips[1] = false; }
        else if (std::abs (clip.stepBeats - 4.0 / d * 2.0 / 3.0) < 1.0e-6) { divs[1] = d; trips[1] = true; }
    }

    piano.clipReplaced();
    steps.setClip (&clip, resetRows);
    syncHeader();
}

//==============================================================================
void MidiEditor::pushUndo()
{
    undoStack.push_back (clip);
    if (undoStack.size() > 50) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void MidiEditor::commitClip()
{
    if (loopId == kInvalidId) return;
    committing = true;
    controller.setMidiClip (loopId, clip);
    committing = false;
    steps.refreshRows();
    syncHeader();
}

void MidiEditor::clipEditedExternally()
{
    commitClip();
    piano.clipReplaced();
    steps.setClip (&clip, false);
    syncHeader();
}

void MidiEditor::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (clip);
    clip = undoStack.back();
    undoStack.pop_back();
    clipEditedExternally();
}

void MidiEditor::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (clip);
    clip = redoStack.back();
    redoStack.pop_back();
    clipEditedExternally();
}

bool MidiEditor::keyPressed (const juce::KeyPress& key)
{
    const auto m = key.getModifiers();
    if (m.isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z'))
    {
        if (m.isShiftDown()) redo(); else undo();
        return true;
    }
    if (m.isCommandDown() && (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y')) { redo(); return true; }
    return false;
}

//==============================================================================
void MidiEditor::setTab (int t)
{
    tab = t;
    pianoTab.setToggleState (t == 0, juce::dontSendNotification);
    stepTab.setToggleState (t == 1, juce::dontSendNotification);
    syncHeader();
    updateVisibility();
}

double MidiEditor::currentGridBeats() const
{
    return 4.0 / divs[tab] * (trips[tab] ? 2.0 / 3.0 : 1.0);
}

void MidiEditor::syncHeader()
{
    const int ds[] = { 4, 8, 16, 32 };
    int id = 3;
    for (int i = 0; i < 4; ++i) if (ds[i] == divs[tab]) id = i + 1;
    gridBox.setSelectedId (id, juce::dontSendNotification);
    tripletToggle.setToggleState (trips[tab], juce::dontSendNotification);
    snapToggle.setToggleState (snapOn, juce::dontSendNotification);
    piano.setGrid (divs[0] > 0 ? 4.0 / divs[0] * (trips[0] ? 2.0 / 3.0 : 1.0) : 0.25, snapOn);

    const int bpb = juce::jmax (1, controller.session().transport.beatsPerBar);
    const double bars = clip.lengthBeats / bpb;
    juce::String txt;
    if (std::abs (bars - std::round (bars)) < 1.0e-6)
        txt = juce::String ((int) std::round (bars)) + (std::round (bars) == 1 ? " bar" : " bars");
    else
        txt = juce::String (clip.lengthBeats, 2) + " beats";
    lenLabel.setText (txt, juce::dontSendNotification);
}

void MidiEditor::userChangedGrid()
{
    const int ds[] = { 4, 8, 16, 32 };
    const int id = gridBox.getSelectedId();
    if (id >= 1 && id <= 4) divs[tab] = ds[id - 1];
    trips[tab] = tripletToggle.getToggleState();
    if (tab == 1)
    {
        const double beats = currentGridBeats();
        if (std::abs (clip.stepBeats - beats) > 1.0e-9)
        {
            pushUndo();
            clip.stepBeats = beats;
            steps.stepLengthChanged();
            commitClip();
        }
    }
    syncHeader();
}

void MidiEditor::changeLength (int direction)
{
    if (loopId == kInvalidId) return;
    const int bpb = juce::jmax (1, controller.session().transport.beatsPerBar);
    const double nl = juce::jmax (1.0, clip.lengthBeats + direction * bpb);
    if (nl == clip.lengthBeats) return;
    pushUndo();
    clip.lengthBeats = nl;
    if (direction < 0)   // drop notes that would start beyond the new end
        clip.notes.erase (std::remove_if (clip.notes.begin(), clip.notes.end(), [nl] (const MidiNote& n) { return n.start >= nl; }), clip.notes.end());
    commitClip();
    piano.clipReplaced();
    steps.setClip (&clip, false);
}

void MidiEditor::quantizeClip()
{
    if (loopId == kInvalidId || clip.notes.empty()) return;
    pushUndo();
    clip.quantize (currentGridBeats());
    commitClip();
    piano.clipReplaced();
    steps.setClip (&clip, false);
}

void MidiEditor::timerCallback()
{
    if (loopId == kInvalidId) return;
    const auto st = controller.loopStatus (loopId);
    const bool vis = st.state == LoopState::Playing || st.state == LoopState::Overdubbing || st.state == LoopState::Recording;
    piano.setPlayhead (st.phaseBeats, vis);
    steps.setPlayhead (st.phaseBeats, vis);
}

} // namespace el
