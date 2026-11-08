#include "UI/LoopCell.h"
#include "UI/ColourPicker.h"
#include "UI/LookAndFeel.h"

namespace el {

namespace {
    constexpr float ringRadius = 27.0f, ringThickness = 5.0f, ringCentreY = 48.0f;

    void buildGlyph (juce::Path& p, LoopType t, juce::Rectangle<float> r)
    {
        p.clear();
        if (t == LoopType::Audio) {
            static const float hs[] = { 0.3f, 0.6f, 1.0f, 0.7f, 0.9f, 0.5f, 0.25f };
            const int n = 7;
            const float bw = r.getWidth() / (float) (n * 2 - 1);
            for (int i = 0; i < n; ++i) {
                const float h = r.getHeight() * hs[i];
                p.addRoundedRectangle (r.getX() + (float) i * bw * 2.0f, r.getCentreY() - h * 0.5f, bw, h, bw * 0.5f);
            }
        } else if (t == LoopType::Midi) {
            const float h = r.getHeight() / 5.0f;
            const float x[] = { 0.0f, 0.25f, 0.5f, 0.15f };
            const float w[] = { 0.4f, 0.3f, 0.5f, 0.35f };
            for (int i = 0; i < 4; ++i)
                p.addRoundedRectangle (r.getX() + x[i] * r.getWidth(), r.getY() + (float) i * h * 1.25f, w[i] * r.getWidth() * 1.4f, h, 1.5f);
        } else {
            juce::Path c;
            c.startNewSubPath (r.getX(), r.getBottom());
            c.cubicTo (r.getX() + r.getWidth() * 0.3f, r.getY() - r.getHeight() * 0.1f,
                       r.getX() + r.getWidth() * 0.45f, r.getBottom() + r.getHeight() * 0.1f,
                       r.getX() + r.getWidth() * 0.65f, r.getCentreY());
            c.cubicTo (r.getX() + r.getWidth() * 0.8f, r.getY(), r.getX() + r.getWidth() * 0.9f, r.getY(), r.getRight(), r.getY());
            juce::PathStrokeType (r.getHeight() * 0.14f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
                .createStrokedPath (p, c);
        }
    }
}

LoopCell::LoopCell (EngineController& c, Selection& s, MidiMapping& mm, Id tid, Id lid)
    : controller (c), selection (s), midiMap (mm), trackId (tid), loopId (lid)
{
    setSize (theme::loopCellSize, theme::loopCellSize);
    setRepaintsOnMouseActivity (false);
    refresh();
    status = controller.loopStatus (loopId);
    updateTexts();
}

void LoopCell::resized()
{
    const auto b = getLocalBounds().toFloat();
    const float cx = b.getCentreX();
    buildGlyph (glyph, type, { cx - 13.0f, ringCentreY - 11.0f, 26.0f, 22.0f });
    buildGlyph (smallGlyph, type, { 6.0f, 6.0f, 13.0f, 10.0f });
}

void LoopCell::refresh()
{
    auto loop = controller.session().findLoop (loopId);
    if (loop == nullptr) return;
    const auto oldType = type;
    type = loop->type;
    colour = controller.loopColour (loopId);
    nameText = loop->name;
    if (nameText.isEmpty()) {
        int idx = 1;
        if (auto t = controller.session().findTrack (trackId))
            for (size_t i = 0; i < t->loops.size(); ++i)
                if (t->loops[i]->id == loopId) idx = (int) i + 1;
        nameText = "Loop " + juce::String (idx);
    }
    if (type != oldType) resized();
    repaint();
}

float LoopCell::ringFraction (const LoopStatus& s) const
{
    if (s.state == LoopState::Empty) return 0.0f;
    if (s.state == LoopState::Recording && s.lengthBeats > 0.0)
        return juce::jlimit (0.0f, 1.0f, (float) (s.recordedBeats / s.lengthBeats));
    return juce::jlimit (0.0f, 1.0f, s.progress);
}

void LoopCell::updateTexts()
{
    takeText = status.numTakes > 1 ? "T" + juce::String (status.activeTake + 1) + "/" + juce::String (status.numTakes)
                                   : juce::String();
    lenText.clear();
    const double len = status.lengthBeats;
    if (len > 0.0 && status.state != LoopState::Empty) {
        const int bpb = juce::jmax (1, controller.session().transport.beatsPerBar);
        const double bars = len / (double) bpb;
        lenText = (std::abs (bars - std::round (bars)) < 0.01 ? juce::String ((int) std::round (bars))
                                                              : juce::String (bars, 1)) + (bars > 1.01 ? " bars" : " bar");
    }
}

void LoopCell::poll (bool blinkOn)
{
    const auto s = controller.loopStatus (loopId);
    const int step = (int) (ringFraction (s) * 160.0f);
    const bool sel = selection.loopId == loopId;
    const bool blinkChanged = s.pending != PendingAction::None && blinkOn != blink;
    const bool textChanged = s.numTakes != status.numTakes || s.activeTake != status.activeTake
                             || std::abs (s.lengthBeats - status.lengthBeats) > 1.0e-6;
    if (s.state != status.state || s.pending != status.pending || sel != selected || blinkChanged || textChanged) {
        status = s;
        ringStep = step;
        selected = sel;
        blink = blinkOn;
        if (textChanged) updateTexts();
        repaint();
    } else if (step != ringStep) {
        // Only the progress ring moved (the common case while playing): redraw just the ring.
        status = s;
        ringStep = step;
        blink = blinkOn;
        const float r = ringRadius + ringThickness;
        repaint (juce::Rectangle<float> (getWidth() * 0.5f - r - 2.0f, ringCentreY - r, (r + 2.0f) * 2.0f, r * 2.0f).getSmallestIntegerContainer());
    } else {
        blink = blinkOn;
    }
}

void LoopCell::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    juce::Colour fill, ring;
    switch (status.state) {
        case LoopState::Empty:       fill = theme::panel; ring = colour.withAlpha (0.45f); break;
        case LoopState::Recording:   fill = theme::record.withMultipliedBrightness (0.35f); ring = theme::record; break;
        case LoopState::Playing:     fill = colour.withMultipliedBrightness (0.5f); ring = colour; break;
        case LoopState::Overdubbing: fill = theme::overdub.withMultipliedBrightness (0.4f); ring = theme::overdub; break;
        case LoopState::Stopped:     fill = colour.withMultipliedBrightness (0.22f).withMultipliedSaturation (0.5f);
                                     ring = colour.withAlpha (0.55f); break;
    }
    g.setColour (fill);
    g.fillRoundedRectangle (b, 9.0f);
    g.setColour (status.state == LoopState::Empty ? colour.withAlpha (0.4f) : colour.withAlpha (0.6f));
    g.drawRoundedRectangle (b, 9.0f, 1.5f);

    const float cx = b.getCentreX();
    const bool dim = status.state == LoopState::Stopped || status.state == LoopState::Empty;
    const auto ink = juce::Colours::white.withAlpha (dim ? 0.55f : 0.92f);

    // ring track + progress
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (cx - ringRadius, ringCentreY - ringRadius, ringRadius * 2, ringRadius * 2, ringThickness);
    const float frac = (float) ringStep / 160.0f;
    if (frac > 0.002f) {
        arcPath.clear();
        const float a0 = 0.0f, a1 = juce::MathConstants<float>::twoPi * juce::jmin (frac, 0.9999f);
        arcPath.addCentredArc (cx, ringCentreY, ringRadius, ringRadius, 0.0f, a0, a1, true);
        g.setColour (ring);
        g.strokePath (arcPath, juce::PathStrokeType (ringThickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    // centre content
    if (status.state == LoopState::Empty) {
        g.setColour (theme::textDim);
        g.setFont (theme::font (15.0f, true));
        g.drawText ("REC", juce::Rectangle<float> (cx - 26.0f, ringCentreY - 10.0f, 52.0f, 20.0f), juce::Justification::centred);
    } else {
        g.setColour (ink);
        g.fillPath (glyph);
    }

    // corner info
    g.setColour (ink.withAlpha (0.8f));
    g.fillPath (smallGlyph);
    g.setFont (theme::font (13.0f));
    if (lenText.isNotEmpty())
        g.drawText (lenText, juce::Rectangle<float> (22.0f, 3.0f, 40.0f, 17.0f), juce::Justification::centredLeft, false);
    if (takeText.isNotEmpty()) {
        g.setColour (theme::armed);
        g.drawText (takeText, juce::Rectangle<float> (b.getRight() - 36.0f, 3.0f, 34.0f, 17.0f), juce::Justification::centredRight, false);
    }
    g.setColour (ink);
    g.setFont (theme::font (14.0f, true));
    g.drawText (nameText, juce::Rectangle<float> (b.getX() + 3.0f, b.getBottom() - 19.0f, b.getWidth() - 6.0f, 17.0f),
                juce::Justification::centred, true);

    if (status.pending != PendingAction::None && blink) {
        g.setColour (theme::armed);
        g.drawRoundedRectangle (b.reduced (1.0f), 8.0f, 3.0f);
    }
    if (selected) {
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (b.expanded (1.0f), 10.0f, 2.0f);
    }
}

void LoopCell::mouseDown (const juce::MouseEvent& e)
{
    selection.select (trackId, loopId);
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    if (e.getNumberOfClicks() > 1) return;      // double-click handled in mouseDoubleClick
    if (e.mods.isShiftDown()) controller.overdubLoop (loopId);
    else controller.triggerLoop (loopId);
}

void LoopCell::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (type == LoopType::Audio) controller.overdubLoop (loopId);
    else if (onEdit) onEdit (trackId, loopId, type == LoopType::Midi);
}

void LoopCell::showMenu()
{
    auto loop = controller.session().findLoop (loopId);
    if (loop == nullptr) return;
    auto* c = &controller; auto* mm = &midiMap;
    const Id id = loopId, tid = trackId;
    const LoopSettings cur = loop->settings;
    const auto lt = loop->type;
    const auto st = status;
    juce::Component::SafePointer<LoopCell> self (this);
    ActionMenu am;
    juce::PopupMenu m;

    // helper: modify settings
    auto edit = [c, id] (std::function<void (LoopSettings&)> f) {
        return [c, id, f] {
            if (auto l = c->session().findLoop (id)) {
                auto s = l->settings;
                f (s);
                c->setLoopSettings (id, s);
            }
        };
    };

    am.add (m, "Record", [c, id] { c->recordLoop (id); });
    am.add (m, "Play", [c, id] { c->playLoop (id); }, false, st.state != LoopState::Empty);
    am.add (m, "Stop", [c, id] { c->stopLoop (id); }, false, st.state != LoopState::Empty);
    am.add (m, "Overdub", [c, id] { c->overdubLoop (id); }, false, st.state != LoopState::Empty);
    am.add (m, "Clear", [c, id] {
        ui::confirm ("Clear Loop", "Clear the recorded material of this loop?", "Clear", [c, id] { c->clearLoop (id); });
    }, false, st.state != LoopState::Empty);
    m.addSeparator();

    struct Q { const char* name; Quantize q; };
    const Q qs[] = { { "None", Quantize::None }, { "Beat", Quantize::Beat }, { "Bar", Quantize::Bar },
                     { "2 Bars", Quantize::TwoBars }, { "4 Bars", Quantize::FourBars }, { "Master", Quantize::Master } };
    juce::PopupMenu startQ, stopQ;
    for (auto& q : qs) {
        const auto v = q.q;
        am.add (startQ, q.name, edit ([v] (LoopSettings& s) { s.startQuantize = v; }), cur.startQuantize == v);
        am.add (stopQ, q.name, edit ([v] (LoopSettings& s) { s.stopQuantize = v; }), cur.stopQuantize == v);
    }
    m.addSubMenu ("Start Quantize", startQ);
    m.addSubMenu ("Stop Quantize", stopQ);

    juce::PopupMenu rl;
    am.add (rl, "Master", edit ([] (LoopSettings& s) { s.lengthMode = RecordLength::Master; }), cur.lengthMode == RecordLength::Master);
    for (int bars : { 1, 2, 4, 8, 16 })
        am.add (rl, juce::String (bars) + (bars == 1 ? " bar" : " bars"),
                edit ([bars] (LoopSettings& s) { s.lengthMode = RecordLength::Bars; s.recordBars = (int16_t) bars; }),
                cur.lengthMode == RecordLength::Bars && cur.recordBars == bars);
    am.add (rl, "Free", edit ([] (LoopSettings& s) { s.lengthMode = RecordLength::Free; }), cur.lengthMode == RecordLength::Free);
    m.addSubMenu ("Record Length", rl);

    juce::PopupMenu ar;
    am.add (ar, "Play", edit ([] (LoopSettings& s) { s.afterRecord = AfterRecord::Play; }), cur.afterRecord == AfterRecord::Play);
    am.add (ar, "Overdub", edit ([] (LoopSettings& s) { s.afterRecord = AfterRecord::Overdub; }), cur.afterRecord == AfterRecord::Overdub);
    am.add (ar, "Stop", edit ([] (LoopSettings& s) { s.afterRecord = AfterRecord::Stop; }), cur.afterRecord == AfterRecord::Stop);
    m.addSubMenu ("After Record", ar);

    juce::PopupMenu pm;
    am.add (pm, "Loop", edit ([] (LoopSettings& s) { s.playMode = PlayMode::Loop; }), cur.playMode == PlayMode::Loop);
    am.add (pm, "One Shot", edit ([] (LoopSettings& s) { s.playMode = PlayMode::OneShot; }), cur.playMode == PlayMode::OneShot);
    m.addSubMenu ("Play Mode", pm);

    juce::PopupMenu tc;
    am.add (tc, "Keep Pitch (time-stretch)", edit ([] (LoopSettings& s) { s.varispeed = false; }), ! cur.varispeed);
    am.add (tc, "Change Pitch (varispeed)", edit ([] (LoopSettings& s) { s.varispeed = true; }), cur.varispeed);
    m.addSubMenu ("Tempo Changes", tc);

    juce::PopupMenu rm;
    am.add (rm, "Replace", edit ([] (LoopSettings& s) { s.recordMode = RecordMode::Replace; s.cycleTakes = false; }),
            cur.recordMode == RecordMode::Replace);
    am.add (rm, "Take Folder", edit ([] (LoopSettings& s) { s.recordMode = RecordMode::TakeFolder; s.cycleTakes = false; }),
            cur.recordMode == RecordMode::TakeFolder && ! cur.cycleTakes);
    am.add (rm, "Take Folder + Cycle", edit ([] (LoopSettings& s) { s.recordMode = RecordMode::TakeFolder; s.cycleTakes = true; }),
            cur.recordMode == RecordMode::TakeFolder && cur.cycleTakes);
    m.addSubMenu ("Record Mode", rm);

    juce::PopupMenu fb;
    for (int pct : { 100, 90, 75, 50 })
        am.add (fb, juce::String (pct) + "%", edit ([pct] (LoopSettings& s) { s.overdubFeedback = (float) pct / 100.0f; }),
                std::abs (cur.overdubFeedback - (float) pct / 100.0f) < 0.005f);
    m.addSubMenu ("Overdub Feedback", fb);

    juce::PopupMenu takes;
    if (st.numTakes > 0) {
        for (int k = 0; k < st.numTakes; ++k)
            am.add (takes, "Take " + juce::String (k + 1), [c, id, k] { c->selectTake (id, k); }, k == st.activeTake);
        takes.addSeparator();
        for (int k = 0; k < st.numTakes; ++k)
            am.add (takes, "Delete Take " + juce::String (k + 1), [c, id, k] { c->deleteTake (id, k); });
    } else am.add (takes, "(no takes)", {}, false, false);
    m.addSubMenu ("Takes", takes);
    m.addSeparator();

    am.addColourMenu (m, "Colour", loop->colour, true, this,
                      [c, id] (std::optional<juce::Colour> col) { c->setLoopColour (id, col); });
    am.add (m, "Rename...", [c, id, n = loop->name] {
        ui::askText ("Rename Loop", "Loop name:", n, [c, id] (juce::String s) { c->setLoopName (id, s.trim()); });
    });
    am.add (m, "Duplicate", [c, id] { c->duplicateLoop (id); }, false, lt != LoopType::Audio);
    am.add (m, "Add to Timeline at Playhead", [c, id] {
        const int bpb = juce::jmax (1, c->session().transport.beatsPerBar);
        const double beat = std::floor (c->transportStatus().beat / bpb) * bpb;
        c->addTimelineClip (id, juce::jmax (0.0, beat));
    });
    if (lt == LoopType::Midi)
        am.add (m, "Edit MIDI", [self, tid, id] { if (self != nullptr && self->onEdit) self->onEdit (tid, id, true); });
    // (Loop automation is hidden for now: it will come back in a different form.)

    juce::PopupMenu learn;
    auto learnItem = [&] (const char* name, MappingTarget::Kind k) {
        am.add (learn, name, [mm, tid, id, k] {
            MappingTarget t; t.kind = k; t.trackId = tid; t.loopId = id;
            mm->startLearn (t);
        });
    };
    learnItem ("Trigger", MappingTarget::Kind::TriggerLoop);
    learnItem ("Record", MappingTarget::Kind::RecordLoop);
    learnItem ("Stop", MappingTarget::Kind::StopLoop);
    m.addSubMenu ("MIDI Learn", learn);

    m.addSeparator();
    am.add (m, "Remove Loop", [c, id] {
        ui::confirm ("Remove Loop", "Remove this loop?", "Remove", [c, id] { c->removeLoop (id); });
    });
    am.show (m, this);
}

} // namespace el
