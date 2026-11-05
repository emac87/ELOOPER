#include "UI/TransportBar.h"
#include "UI/LookAndFeel.h"

namespace el {

namespace {
    struct Sig { int n, d; };
    const Sig signatures[] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 4 }, { 7, 4 },
                               { 3, 8 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } };
    constexpr int numSigs = (int) (sizeof (signatures) / sizeof (signatures[0]));
    constexpr int customMasterId = 100;
}

void TransportBar::PlayButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    g.setColour (on ? theme::play.withAlpha (0.25f) : theme::panelRaised.brighter (down ? 0.2f : over ? 0.1f : 0.0f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? theme::play : theme::outline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    auto c = r.getCentre();
    g.setColour (on ? theme::play : theme::text);
    if (on) g.fillRoundedRectangle (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f, 2.0f);
    else {
        juce::Path p;
        p.addTriangle (c.x - 6.0f, c.y - 8.0f, c.x - 6.0f, c.y + 8.0f, c.x + 8.0f, c.y);
        g.fillPath (p);
    }
}

void TransportBar::TempoLabel::mouseDown (const juce::MouseEvent& e)
{
    dragStart = current;
    juce::Label::mouseDown (e);
}

void TransportBar::TempoLabel::mouseDrag (const juce::MouseEvent& e)
{
    if (isBeingEdited() || e.getDistanceFromDragStart() < 2) return;
    const double delta = -e.getDistanceFromDragStartY() * (e.mods.isShiftDown() ? 0.05 : 0.5);
    if (onDragTempo) onDragTempo (juce::jlimit (20.0, 400.0, std::round ((dragStart + delta) * 100.0) / 100.0));
}

TransportBar::TransportBar (EngineController& c) : controller (c)
{
    addAndMakeVisible (playButton);
    playButton.setTooltip ("Play / Stop (Space)");
    playButton.onClick = [this] { controller.togglePlay(); };

    tempoLabel.setJustificationType (juce::Justification::centred);
    tempoLabel.setFont (theme::font (20.0f, true));
    tempoLabel.setEditable (false, true, false);
    tempoLabel.setColour (juce::Label::backgroundColourId, theme::panelRaised);
    tempoLabel.setColour (juce::Label::outlineColourId, theme::outline);
    tempoLabel.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    tempoLabel.setTooltip ("Tempo in BPM: drag up/down to change (Shift = fine), double-click to type");
    tempoLabel.onTextChange = [this] { commitTempo(); };
    tempoLabel.onDragTempo = [this] (double bpm) { controller.setTempo (bpm); tempoChanged (bpm); };
    addAndMakeVisible (tempoLabel);

    tapButton.setTooltip ("Tap tempo");
    tapButton.onClick = [this] { controller.tapTempo(); };
    addAndMakeVisible (tapButton);

    for (int i = 0; i < numSigs; ++i)
        sigBox.addItem (juce::String (signatures[i].n) + "/" + juce::String (signatures[i].d), i + 1);
    sigBox.setTooltip ("Time signature");
    sigBox.onChange = [this] {
        const int i = sigBox.getSelectedId() - 1;
        if (i >= 0 && i < numSigs) controller.setTimeSignature (signatures[i].n, signatures[i].d);
    };
    addAndMakeVisible (sigBox);

    for (int b : { 1, 2, 4, 8, 16, 32 }) masterBox.addItem (juce::String (b), b);
    masterBox.addSeparator();
    masterBox.addItem ("Custom...", customMasterId);
    masterBox.setTooltip ("Bars to record before a new loop closes and starts looping");
    masterBox.onChange = [this] {
        const int id = masterBox.getSelectedId();
        if (id == customMasterId) {
            const int cur = controller.session().transport.masterBars;
            lastMasterBars = -1;   // force the box to be re-synced
            ui::askText ("Master Bars", "Bars to record before a new loop closes (1 - 256):", juce::String (cur),
                         [this_ = juce::Component::SafePointer<TransportBar> (this), ctrl = &controller] (juce::String s) {
                             const int v = juce::jlimit (1, 256, s.getIntValue());
                             ctrl->setMasterBars (v);
                             if (this_ != nullptr) this_->lastMasterBars = -1;
                         });
        } else if (id > 0) controller.setMasterBars (id);
    };
    addAndMakeVisible (masterBox);

    metroButton.setTooltip ("Metronome");
    metroButton.onClick = [this] { controller.setMetronome (metroButton.getToggleState()); };
    linkButton.setTooltip ("Ableton Link: sync tempo and phase with other apps on the network");
    linkButton.onClick = [this] { controller.setLinkEnabled (linkButton.getToggleState()); };
    syncButton.setTooltip ("Link start/stop sync");
    syncButton.onClick = [this] { controller.setLinkStartStopSync (syncButton.getToggleState()); };
    arrButton.setTooltip ("Arrangement record: write loop launches to the timeline");
    arrButton.setColour (juce::ToggleButton::tickColourId, theme::record);
    arrButton.onClick = [this] { controller.setArrangementRecord (arrButton.getToggleState()); };
    for (auto* b : { &metroButton, &linkButton, &syncButton, &arrButton }) addAndMakeVisible (*b);

    const char* pageNames[] = { "Loops", "Timeline", "Bounced" };
    for (int i = 0; i < 3; ++i) {
        auto& b = pageButtons[i];
        b.setButtonText (pageNames[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (4711);
        b.onClick = [this, i] { setPage ((Page) i); if (onPage) onPage ((Page) i); };
        addAndMakeVisible (b);
    }
    pageButtons[0].setToggleState (true, juce::dontSendNotification);

    const char* dockNames[] = { "Mixer", "Auto", "Editor" };
    const char* dockTips[] = { "Show / hide mixer (M)", "Show / hide automation (A)", "Show / hide MIDI editor (E)" };
    for (int i = 0; i < 3; ++i) {
        auto& b = dockButtons[i];
        b.setButtonText (dockNames[i]);
        b.setTooltip (dockTips[i]);
        b.onClick = [this, i] { if (onDock) onDock ((Dock) i, dockButtons[i].getToggleState()); };
        addAndMakeVisible (b);
    }

    settingsButton.setTooltip ("Settings (Cmd+,)");
    settingsButton.onClick = [this] { if (onSettings) onSettings(); };
    addAndMakeVisible (settingsButton);

    refresh (true);
    startTimerHz (theme::uiRefreshHz);
}

TransportBar::~TransportBar() { stopTimer(); }

void TransportBar::setPage (Page p)
{
    // Set every button explicitly: radio groups don't untoggle siblings without a notification.
    for (int i = 0; i < (int) std::size (pageButtons); ++i)
        pageButtons[i].setToggleState (i == (int) p, juce::dontSendNotification);
}

void TransportBar::setDockOn (Dock d, bool on)
{
    dockButtons[(int) d].setToggleState (on, juce::dontSendNotification);
}

void TransportBar::resized()
{
    const int h = getHeight(), m = 8, gap = 4;
    const int bh = 30, by = (h - bh) / 2;
    int right = getWidth() - m;

    // right cluster (right-aligned): settings, cpu (optional), docks, pages
    settingsButton.setBounds (right - 28, by, 28, bh); right -= 28 + 8;
    const int cpuW = 52;
    const int pagesW = 3 * 52 + 2 * 2, docksW = 3 * 42 + 2 * 2;
    int leftLimitNeeded = m + 40 + gap + 66 + gap + 62 + gap + 38 + gap + 52 + gap + 36 + 52 + gap + 44 + gap + 62 + gap + 46;
    const bool showCpu = right - (pagesW + docksW + 8 + cpuW + 8) > leftLimitNeeded;
    if (showCpu) { cpuR = { right - cpuW, by, cpuW, bh }; right -= cpuW + 8; } else cpuR = {};
    for (int i = 2; i >= 0; --i) { dockButtons[i].setBounds (right - 42, by, 42, bh); right -= 42 + (i > 0 ? 2 : 0); }
    right -= 8;
    for (int i = 2; i >= 0; --i) { pageButtons[i].setBounds (right - 52, by, 52, bh); right -= 52 + (i > 0 ? 2 : 0); }
    right -= 12;

    // left cluster
    int x = m;
    auto place = [&] (juce::Component& c, int w) { c.setBounds (x, by, w, bh); x += w + gap; };
    place (playButton, 40);
    positionR = { x, by, 66, bh }; x += 66 + gap;
    const int phaseW = 70, syncW = 40;
    const int restW = 62 + gap + 38 + gap + 52 + gap + 36 + 52 + gap + 44 + gap + 62 + gap + 46;
    const bool showPhase = x + phaseW + gap + restW <= right;
    if (showPhase) { phaseR = { x, by + 6, phaseW, bh - 12 }; x += phaseW + gap; } else phaseR = {};
    tempoLabelR = { x, by, 62, bh };
    place (tempoLabel, 62);
    place (tapButton, 38);
    place (sigBox, 52);
    masterLabelR = { x, by, 36, bh }; x += 36;
    place (masterBox, 52);
    place (metroButton, 44);
    place (linkButton, 62);
    const bool showSync = x + syncW + gap + 46 <= right;
    syncButton.setVisible (showSync);
    if (showSync) place (syncButton, syncW);
    place (arrButton, 46);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // position read-out
    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (positionR.toFloat(), 6.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (positionR.toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (17.0f, true));
    g.drawText (positionText, positionR, juce::Justification::centred);

    // phase indicator
    if (! phaseR.isEmpty()) {
        auto r = phaseR.toFloat();
        g.setColour (theme::panelRaised);
        g.fillRoundedRectangle (r, 3.0f);
        const int bpb = juce::jmax (1, controller.session().transport.beatsPerBar);
        const int beats = juce::jmax (1, controller.session().transport.masterBars * bpb);
        g.setColour (theme::outline);
        for (int i = 1; i < beats && beats <= 64; ++i)
            g.fillRect (r.getX() + r.getWidth() * (float) i / (float) beats, r.getY(), 1.0f, r.getHeight());
        g.setColour (playing ? theme::accent : theme::textDim);
        g.fillRoundedRectangle (r.getX(), r.getY(), juce::jmax (2.0f, r.getWidth() * (float) shownPhase / 400.0f), r.getHeight(), 3.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }

    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f, true));
    g.drawText ("Master", masterLabelR, juce::Justification::centredLeft);

    if (! cpuR.isEmpty()) {
        auto r = cpuR.toFloat();
        const float load = juce::jlimit (0.0f, 1.0f, (float) shownCpu / 100.0f);
        g.setColour (theme::panelRaised);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (load > 0.85f ? theme::meterHigh : load > 0.6f ? theme::meterMid : theme::meterLow);
        g.fillRoundedRectangle (r.withWidth (juce::jmax (2.0f, r.getWidth() * load)).reduced (0.0f, 0.0f), 5.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (juce::Colours::white);
        g.setFont (theme::font (11.0f, true));
        g.drawText ("CPU " + juce::String (shownCpu) + "%", cpuR, juce::Justification::centred);
    }
}

void TransportBar::tempoChanged (double bpm)
{
    lastTempo = bpm;
    tempoLabel.current = bpm;
    tempoLabel.setText (juce::String (bpm, 1), juce::dontSendNotification);
}

void TransportBar::commitTempo()
{
    const double bpm = juce::jlimit (20.0, 400.0, tempoLabel.getText().getDoubleValue());
    if (bpm > 0.0) controller.setTempo (bpm);
    tempoChanged (controller.session().transport.tempo);
}

void TransportBar::timerCallback() { refresh (false); }

void TransportBar::refresh (bool force)
{
    const auto s = controller.transportStatus();
    const auto& tr = controller.session().transport;

    if (s.playing != playing || force) {
        playing = s.playing;
        playButton.setToggleState (playing, juce::dontSendNotification);
        playButton.repaint();
    }

    const int bar = s.bar + 1, beat = (int) std::floor (s.beatInBar) + 1;
    const int bpb = juce::jmax (1, tr.beatsPerBar);
    const double masterLen = (double) juce::jmax (1, tr.masterBars) * bpb;
    const double ph = std::fmod (juce::jmax (0.0, s.beat), masterLen) / masterLen;
    const int phase = (int) (ph * 400.0);
    if (bar != shownBar || beat != shownBeat || force) {
        shownBar = bar; shownBeat = beat;
        positionText = juce::String (bar).paddedLeft ('0', 3) + "." + juce::String (beat);
        repaint (positionR);
    }
    if (phase != shownPhase || force) { shownPhase = phase; repaint (phaseR); }
    const int cpu = (int) std::round (s.cpuLoad * 100.0);
    if (cpu != shownCpu || force) { shownCpu = cpu; repaint (cpuR); }

    if (std::abs (tr.tempo - lastTempo) > 1.0e-6 && ! tempoLabel.isBeingEdited()) tempoChanged (tr.tempo);

    if (tr.beatsPerBar != lastBpb || tr.beatUnit != lastUnit || force) {
        lastBpb = tr.beatsPerBar; lastUnit = tr.beatUnit;
        int id = 0;
        for (int i = 0; i < numSigs; ++i)
            if (signatures[i].n == lastBpb && signatures[i].d == lastUnit) id = i + 1;
        sigBox.setSelectedId (id, juce::dontSendNotification);
        if (id == 0) sigBox.setText (juce::String (lastBpb) + "/" + juce::String (lastUnit), juce::dontSendNotification);
    }
    if (tr.masterBars != lastMasterBars || force) {
        lastMasterBars = tr.masterBars;
        masterBox.setText (juce::String (lastMasterBars), juce::dontSendNotification);
    }

    metroButton.setToggleState (tr.metronome, juce::dontSendNotification);
    syncButton.setToggleState (tr.linkStartStopSync, juce::dontSendNotification);
    arrButton.setToggleState (controller.isArrangementRecording(), juce::dontSendNotification);
    linkButton.setToggleState (s.linkEnabled, juce::dontSendNotification);
    if (s.linkPeers != shownPeers || s.linkEnabled != shownLinkEnabled || force) {
        shownPeers = s.linkPeers; shownLinkEnabled = s.linkEnabled;
        linkButton.setButtonText (s.linkEnabled && s.linkPeers > 0 ? "Link " + juce::String (s.linkPeers) : "Link");
    }
}

} // namespace el
