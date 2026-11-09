#include "Synth/ui/ChimeraWidgets.h"

namespace el::chimera {

namespace { inline bool differs (float a, float b) { return std::abs (a - b) > 1.0e-6f; } }

// ---- helpers ---------------------------------------------------------------------------------
juce::Colour layerColour (int l)
{
    static const juce::Colour c[4] { juce::Colour (0xff40c4ff), juce::Colour (0xffff9f0a), juce::Colour (0xffbf5af2), juce::Colour (0xff34c759) };
    return c[juce::jlimit (0, 3, l)];
}

juce::String noteName (int n)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    n = juce::jlimit (0, 127, n);
    return juce::String (names[n % 12]) + juce::String (n / 12 - 2);
}

juce::String fmtSeconds (float s)
{
    if (s < 0.0995f) return juce::String (juce::roundToInt (s * 1000.0f)) + " ms";
    if (s < 1.0f) return juce::String (juce::roundToInt (s * 1000.0f)) + " ms";
    return juce::String (s, s < 10.0f ? 2 : 1) + " s";
}
juce::String fmtPercent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
juce::String fmtSigned (float v, int d) { return (v > 0.0005f ? "+" : "") + juce::String (v, d); }

const std::vector<float>& beatChoices()
{
    static const std::vector<float> c { 0.0625f, 0.125f, 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 16.0f };
    return c;
}

juce::String fmtBeats (float b)
{
    struct E { float v; const char* t; };
    static const E tbl[] = { { 0.0625f, "1/16 beat" }, { 0.125f, "1/8 beat" }, { 0.25f, "1/4 beat" }, { 0.375f, "3/8 beat" }, { 0.5f, "1/2 beat" },
                             { 0.75f, "3/4 beat" }, { 1.0f, "1 beat" }, { 1.5f, "1.5 beats" }, { 2.0f, "2 beats" }, { 3.0f, "3 beats" },
                             { 4.0f, "4 beats" }, { 6.0f, "6 beats" }, { 8.0f, "8 beats" }, { 16.0f, "16 beats" } };
    for (auto& e : tbl) if (std::abs (e.v - b) < 1.0e-3f) return e.t;
    return juce::String (b, 3) + " beats";
}

float waveShape (Wave w, float ph, float pw)
{
    ph -= std::floor (ph);
    switch (w) {
        case Wave::Sine: return std::sin (kTwoPi * ph);
        case Wave::Triangle: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
        case Wave::Saw: return 2.0f * ph - 1.0f;
        case Wave::Square: return ph < pw ? 1.0f : -1.0f;
        case Wave::Noise: { uint32_t h = (uint32_t) (ph * 64.0f) * 2654435761u; h ^= h >> 15; return (float) (h & 1023u) / 511.5f - 1.0f; }
    }
    return 0.0f;
}

// ---- PillButton / MenuButton / Caption / PeakMeter ----------------------------------------------
void PillButton::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool hot = isMouseOver();
    const bool lit = get ? on : false;
    g.setColour (lit ? colour.withAlpha (hot ? 0.95f : 0.8f) : (hot ? theme::outline : theme::panelRaised));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (lit ? colour : (hot ? colour : theme::outline));
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
    g.setColour (lit ? juce::Colours::black.withAlpha (0.85f) : theme::text);
    g.setFont (theme::font (fontSize, lit));
    g.drawText (text, getLocalBounds().reduced (4, 0), juce::Justification::centred, true);
}

void PillButton::mouseUp (const juce::MouseEvent& e)
{
    if (! contains (e.getPosition()) || e.mods.isPopupMenu()) return;
    if (get) { on = ! on; if (set) set (on); repaint(); }
    else if (onClick) onClick();
}

void MenuButton::show()
{
    juce::PopupMenu m;
    if (buildMenu) buildMenu (m);
    juce::Component::SafePointer<MenuButton> sp (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                     [sp] (int r) { if (sp != nullptr && r > 0 && sp->onChoose) sp->onChoose (r); });
}

void Caption::paint (juce::Graphics& g)
{
    g.setColour (dim ? theme::textDim : theme::text);
    g.setFont (theme::font (size, bold));
    g.drawText (text, getLocalBounds(), just, true);
}

void PeakMeter::setLevel (float lin)
{
    const float nv = lin;
    shown = nv > shown ? nv : shown * 0.82f;
    hold = nv > hold ? nv : hold * 0.975f;
    repaint();
}

void PeakMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::background);
    g.fillRoundedRectangle (b, 3.0f);
    auto toFrac = [] (float lin) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (lin, -60.0f) + 60.0f) / 60.0f); };
    auto in = b.reduced (1.5f);
    const float f = toFrac (shown);
    const float w = in.getWidth() * f;
    const float wGreen = in.getWidth() * 0.78f, wYellow = in.getWidth() * 0.92f;
    auto seg = [&] (float x0, float x1, juce::Colour c) {
        const float a = juce::jmin (w, x1);
        if (a > x0) { g.setColour (c); g.fillRect (in.getX() + x0, in.getY(), a - x0, in.getHeight()); }
    };
    seg (0, wGreen, theme::meterLow); seg (wGreen, wYellow, theme::meterMid); seg (wYellow, in.getWidth(), theme::meterHigh);
    g.setColour (theme::text.withAlpha (0.8f));
    g.fillRect (in.getX() + in.getWidth() * toFrac (hold) - 1.0f, in.getY(), 1.5f, in.getHeight());
    g.setColour (theme::outline);
    g.drawRoundedRectangle (b, 3.0f, 1.0f);
}

// ---- Panel -----------------------------------------------------------------------------------
void Panel::paint (juce::Graphics& g)
{
    if (! framed) return;
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (b, theme::cornerRadius);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (b, theme::cornerRadius, 1.0f);
    if (title.isNotEmpty()) {
        g.setColour (accent);
        g.fillRoundedRectangle (9.0f, 8.0f, 3.0f, 11.0f, 1.5f);
        g.setColour (theme::text.withAlpha (0.85f));
        g.setFont (theme::font (11.5f, true));
        g.drawText (title.toUpperCase(), 17, 4, getWidth() - 24, 19, juce::Justification::centredLeft, true);
    }
}

void Panel::setAccent (juce::Colour c)
{
    accent = c;
    for (auto* b : bars) b->fill = c;
    for (auto* p : pills) p->colour = c;
    for (auto* s : subs) s->setAccent (c);
    // views that take the accent colour
    for (auto& o : owned) {
        if (auto* v = dynamic_cast<FuncView*> (o.get())) v->colour = c;
        else if (auto* e = dynamic_cast<EnvView*> (o.get())) e->colour = c;
        else if (auto* e2 = dynamic_cast<EgView*> (o.get())) e2->colour = c;
        else if (auto* w = dynamic_cast<WavetableView*> (o.get())) w->colour = c;
        else if (auto* s2 = dynamic_cast<SampleView*> (o.get())) s2->colour = c;
        else if (auto* a = dynamic_cast<AlgoView*> (o.get())) a->colour = c;
        else if (auto* m = dynamic_cast<MenuButton*> (o.get())) m->accent = c;
    }
    repaint();
}

void Panel::pollAll()
{
    if (! isShowing()) return;
    for (auto& f : pollers) f();
    for (auto* s : subs) s->pollAll();
}

ValueBar& Panel::bar (const juce::String& label, std::function<float()> get, std::function<void (float)> set,
                      juce::NormalisableRange<float> r, float def, std::function<juce::String (float)> fmt, bool bipolar)
{
    auto& b = make<ValueBar>();
    b.label = label;
    b.bipolar = bipolar;
    b.fill = accent;
    b.defaultValue = r.convertTo0to1 (def);
    b.get = [get, r] { return r.convertTo0to1 (get()); };
    b.set = [set, r] (float n) { set (r.snapToLegalValue (r.convertFrom0to1 (n))); };
    b.format = [r, fmt] (float n) {
        const float v = r.snapToLegalValue (r.convertFrom0to1 (n));
        return fmt ? fmt (v) : juce::String (v, 2);
    };
    bars.push_back (&b);
    pollers.push_back ([&b] { b.poll(); });
    return b;
}

ValueBar& Panel::paramBar (const juce::String& label, std::function<juce::AudioParameterFloat*()> pf, bool bipolar)
{
    auto& b = make<ValueBar>();
    b.label = label;
    b.bipolar = bipolar;
    b.fill = accent;
    if (auto* p = pf()) b.defaultValue = static_cast<juce::AudioProcessorParameter*> (p)->getDefaultValue();
    b.get = [pf] { auto* p = pf(); return p != nullptr ? static_cast<juce::AudioProcessorParameter*> (p)->getValue() : 0.0f; };
    b.set = [pf] (float v) { if (auto* p = pf()) p->setValueNotifyingHost (v); };
    b.onBegin = [pf] { if (auto* p = pf()) p->beginChangeGesture(); };
    b.onEnd = [pf] { if (auto* p = pf()) p->endChangeGesture(); };
    b.format = [pf] (float n) { auto* p = pf(); return p != nullptr ? static_cast<const juce::AudioProcessorParameter*> (p)->getText (n, 16) : juce::String(); };
    bars.push_back (&b);
    pollers.push_back ([&b] { b.poll(); });
    return b;
}

MenuButton& Panel::enumMenu (const juce::String& prefix, int count, std::function<juce::String (int)> nameOf,
                             std::function<int()> getIdx, std::function<void (int)> setIdx)
{
    auto& m = make<MenuButton>();
    m.accent = accent;
    m.getText = [prefix, nameOf, getIdx] { return prefix + nameOf (getIdx()); };
    m.buildMenu = [count, nameOf, getIdx] (juce::PopupMenu& pm) {
        const int cur = getIdx();
        for (int i = 0; i < count; ++i) pm.addItem (i + 1, nameOf (i), true, i == cur);
    };
    m.onChoose = [setIdx] (int id) { setIdx (id - 1); };
    pollers.push_back ([&m] { m.poll(); });
    return m;
}

PillButton& Panel::toggle (const juce::String& label, std::function<bool()> get, std::function<void (bool)> set)
{
    auto& p = make<PillButton>();
    p.text = label;
    p.colour = accent;
    p.get = std::move (get);
    p.set = std::move (set);
    pills.push_back (&p);
    pollers.push_back ([&p] { p.poll(); });
    return p;
}

PillButton& Panel::button (const juce::String& label, std::function<void()> onClick)
{
    auto& p = make<PillButton>();
    p.text = label;
    p.colour = accent;
    p.onClick = std::move (onClick);
    return p;
}

Caption& Panel::caption (std::function<juce::String()> get)
{
    auto& c = make<Caption>();
    c.get = std::move (get);
    pollers.push_back ([&c] { c.poll(); });
    return c;
}

// ---- FuncView ---------------------------------------------------------------------------------
void FuncView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::background);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
    if (! fn) return;
    auto in = b.reduced (4.0f, 5.0f);
    g.setColour (theme::outline.withAlpha (0.7f));
    g.drawHorizontalLine ((int) in.getCentreY(), in.getX(), in.getRight());
    juce::Path p;
    const int n = juce::jmax (8, (int) in.getWidth());
    for (int i = 0; i <= n; ++i) {
        const float t = (float) i / (float) n;
        const float x = in.getX() + in.getWidth() * t;
        const float y = in.getCentreY() - fn (t * (float) cycles) * in.getHeight() * 0.5f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

// ---- EnvView ----------------------------------------------------------------------------------
void EnvView::poll()
{
    if (! get) return;
    const auto e = get();
    if (differs (e.attack, shown.attack) || differs (e.decay, shown.decay) || differs (e.sustain, shown.sustain) || differs (e.release, shown.release)) { shown = e; repaint(); }
}

void EnvView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::background);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
    auto in = b.reduced (5.0f, 5.0f);
    auto n = [] (float t) { return juce::jlimit (0.0f, 1.0f, std::log10 (juce::jmax (t, 0.001f) * 1000.0f) / std::log10 (12000.0f)); };
    float wa = 0.08f + 0.22f * n (shown.attack), wd = 0.08f + 0.22f * n (shown.decay), ws = 0.2f, wr = 0.08f + 0.22f * n (shown.release);
    const float tot = wa + wd + ws + wr;
    wa /= tot; wd /= tot; ws /= tot; wr /= tot;
    const float W = in.getWidth(), H = in.getHeight(), x0 = in.getX(), yb = in.getBottom();
    const float sus = juce::jlimit (0.0f, 1.0f, shown.sustain);
    juce::Path p;
    p.startNewSubPath (x0, yb);
    const int seg = 16;
    for (int i = 1; i <= seg; ++i) {   // attack: fast rise, concave
        const float s = (float) i / seg;
        p.lineTo (x0 + W * wa * s, yb - H * (1.0f - (1.0f - s) * (1.0f - s)));
    }
    const float xd = x0 + W * wa, e4 = std::exp (-4.0f);
    for (int i = 1; i <= seg; ++i) {   // decay: exponential to the sustain level
        const float s = (float) i / seg;
        const float k = (std::exp (-4.0f * s) - e4) / (1.0f - e4);
        p.lineTo (xd + W * wd * s, yb - H * (sus + (1.0f - sus) * k));
    }
    const float xs = xd + W * wd;
    p.lineTo (xs + W * ws, yb - H * sus);
    const float xr = xs + W * ws;
    for (int i = 1; i <= seg; ++i) {
        const float s = (float) i / seg;
        const float k = (std::exp (-4.0f * s) - e4) / (1.0f - e4);
        p.lineTo (xr + W * wr * s, yb - H * sus * k);
    }
    juce::Path fillP (p);
    fillP.lineTo (x0 + W, yb);
    fillP.closeSubPath();
    g.setColour (colour.withAlpha (0.16f));
    g.fillPath (fillP);
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.6f));
}

// ---- EgView (DX7) -------------------------------------------------------------------------------
void EgView::poll()
{
    if (! get) return;
    uint8_t r[4], l[4];
    get (r, l);
    if (std::memcmp (r, sr, 4) != 0 || std::memcmp (l, sl, 4) != 0) { std::memcpy (sr, r, 4); std::memcpy (sl, l, 4); repaint(); }
}

void EgView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::background);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
    auto in = b.reduced (5.0f, 5.0f);
    float w[4], tot = 0.0f;
    for (int i = 0; i < 4; ++i) { w[i] = 0.12f + 0.88f * (float) (99 - juce::jmin<int> (sr[i], 99)) / 99.0f; tot += w[i]; }
    const float hold = 0.25f * (tot + 0.25f);
    tot += hold;
    auto Y = [&] (int lvl) { return in.getBottom() - in.getHeight() * (float) juce::jmin<int> (lvl, 99) / 99.0f; };
    juce::Path p;
    float x = in.getX();
    p.startNewSubPath (x, Y (sl[3]));
    for (int i = 0; i < 3; ++i) { x += in.getWidth() * w[i] / tot; p.lineTo (x, Y (sl[i])); }
    x += in.getWidth() * hold / tot;
    p.lineTo (x, Y (sl[2]));
    x += in.getWidth() * w[3] / tot;
    p.lineTo (x, Y (sl[3]));
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

// ---- WavetableView ------------------------------------------------------------------------------
void WavetableView::poll()
{
    if (! getTable || ! getPosition) return;
    const int t = getTable();
    const float pos = getPosition();
    if (t != shownTable || std::abs (pos - shownPos) > 1.0e-4f) { shownTable = t; shownPos = pos; repaint(); }
}

void WavetableView::drag (const juce::MouseEvent& e)
{
    if (setPosition && getWidth() > 4) setPosition (juce::jlimit (0.0f, 1.0f, (float) e.x / (float) getWidth()));
}

void WavetableView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::background);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
    const auto& bank = WavetableBank::get();
    if (bank.numTables() <= 0) return;
    const int table = juce::jlimit (0, bank.numTables() - 1, shownTable < 0 ? 0 : shownTable);
    auto in = b.reduced (6.0f, 8.0f);
    const int cols = juce::jlimit (32, 512, (int) in.getWidth() / 2);
    const int N = WavetableBank::kFrameSize, F = WavetableBank::kFrames;
    auto makePath = [&] (const float* a, const float* c, float mixc) {
        juce::Path p;
        for (int i = 0; i <= cols; ++i) {
            const int idx = juce::jmin (N - 1, i * N / cols);
            const float v = a[idx] + (c != nullptr ? (c[idx] - a[idx]) * mixc : 0.0f);
            const float x = in.getX() + in.getWidth() * (float) i / (float) cols;
            const float y = in.getCentreY() - juce::jlimit (-1.2f, 1.2f, v) * in.getHeight() * 0.46f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };
    g.setColour (theme::outline.withAlpha (0.7f));
    g.drawHorizontalLine ((int) in.getCentreY(), in.getX(), in.getRight());
    g.setColour (colour.withAlpha (0.10f));
    for (int f = 0; f < F; ++f) g.strokePath (makePath (bank.frame (table, f, 0), nullptr, 0.0f), juce::PathStrokeType (1.0f));
    const float pos = juce::jlimit (0.0f, 1.0f, shownPos < 0 ? 0.0f : shownPos) * (float) (F - 1);
    const int f0 = juce::jmin (F - 2, (int) pos);
    auto cur = makePath (bank.frame (table, f0, 0), bank.frame (table, f0 + 1, 0), pos - (float) f0);
    juce::Path fillP (cur);
    fillP.lineTo (in.getRight(), in.getCentreY());
    fillP.lineTo (in.getX(), in.getCentreY());
    fillP.closeSubPath();
    g.setColour (colour.withAlpha (0.14f));
    g.fillPath (fillP);
    g.setColour (colour);
    g.strokePath (cur, juce::PathStrokeType (2.0f));
}

// ---- SampleView ---------------------------------------------------------------------------------
void SampleView::rebuildPeaks (const SampleData* s)
{
    constexpr int bins = 2048;
    peakMin.assign (bins, 0.0f);
    peakMax.assign (bins, 0.0f);
    if (s == nullptr || s->length() <= 0) return;
    const int len = s->length();
    for (int i = 0; i < bins; ++i) {
        const int a = (int) ((int64_t) i * len / bins), e = juce::jmax (a + 1, (int) ((int64_t) (i + 1) * len / bins));
        float lo = 0.0f, hi = 0.0f;
        for (int k = a; k < e && k < len; ++k) {
            float v = s->left[(size_t) k];
            if (s->stereo()) v = 0.5f * (v + s->right[(size_t) k]);
            lo = juce::jmin (lo, v); hi = juce::jmax (hi, v);
        }
        peakMin[(size_t) i] = lo; peakMax[(size_t) i] = hi;
    }
}

void SampleView::poll()
{
    if (! getSample || ! getParams) return;
    auto s = getSample();
    const auto p = getParams();
    bool changed = first;
    if (s.get() != cached) { keep = s; cached = s.get(); rebuildPeaks (cached); changed = true; }
    if (differs (p.start, shownParams.start) || p.loop != shownParams.loop || differs (p.loopStart, shownParams.loopStart)
        || differs (p.loopEnd, shownParams.loopEnd) || p.reverse != shownParams.reverse) changed = true;
    if (changed) { shownParams = p; first = false; repaint(); }
}

float SampleView::markerX (int which, const SamplerParams& p) const
{
    const float w = (float) getWidth();
    if (which == 0) return w * (p.reverse ? 1.0f - p.start : p.start);
    return w * (which == 1 ? p.loopStart : p.loopEnd);
}

void SampleView::mouseDown (const juce::MouseEvent& e)
{
    dragging = -1;
    if (! getParams || cached == nullptr) return;
    const auto p = getParams();
    float best = 10.0f;
    for (int k = 0; k < 3; ++k) {
        const int i = (k + 1) % 3;   // loop markers first, so one sitting on START can still be grabbed
        if (i > 0 && p.loop == LoopMode::Off) continue;
        const float d = std::abs ((float) e.x - markerX (i, p));
        if (d < best) { best = d; dragging = i; }
    }
    if (dragging >= 0) mouseDrag (e);
}

void SampleView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || ! setMarker || ! getParams || getWidth() < 4) return;
    const auto p = getParams();
    const float f = juce::jlimit (0.0f, 1.0f, (float) e.x / (float) getWidth());
    if (dragging == 0) setMarker (0, p.reverse ? 1.0f - f : f);
    else if (dragging == 1) setMarker (1, juce::jmin (f, p.loopEnd - 0.002f));
    else setMarker (2, juce::jmax (f, p.loopStart + 0.002f));
}

void SampleView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::background);
    g.fillRoundedRectangle (b, 4.0f);
    const int W = getWidth(), H = getHeight();
    if (cached == nullptr) {
        g.setColour (theme::textDim);
        g.setFont (theme::font (13.0f));
        g.drawText ("Drop an audio file here, or click Load sample", getLocalBounds(), juce::Justification::centred);
    } else {
        const float mid = (float) H * 0.5f, amp = (float) H * 0.46f;
        const auto& p = shownParams;
        if (p.loop != LoopMode::Off) {
            g.setColour (juce::Colour (0xffff9f0a).withAlpha (0.10f));
            g.fillRect (markerX (1, p), 1.0f, markerX (2, p) - markerX (1, p), (float) H - 2.0f);
        }
        g.setColour (colour.withAlpha (0.85f));
        const int bins = (int) peakMax.size();
        for (int x = 0; x < W; ++x) {
            const int a = x * bins / juce::jmax (1, W), e = juce::jmax (a + 1, (x + 1) * bins / juce::jmax (1, W));
            float lo = 0.0f, hi = 0.0f;
            for (int k = a; k < e && k < bins; ++k) { lo = juce::jmin (lo, peakMin[(size_t) k]); hi = juce::jmax (hi, peakMax[(size_t) k]); }
            const float y0 = mid - hi * amp, y1 = mid - lo * amp;
            g.fillRect ((float) x, y0, 1.0f, juce::jmax (1.0f, y1 - y0));
        }
        auto marker = [&] (int which, juce::Colour c, const char* t) {
            const float x = juce::jlimit (0.0f, (float) W - 1.0f, markerX (which, p));
            g.setColour (c);
            g.fillRect (x - 0.5f, 0.0f, 1.5f, (float) H);
            g.setFont (theme::font (10.5f, true));
            g.drawText (t, (int) x + 3, 2, 40, 12, juce::Justification::centredLeft, false);
        };
        marker (0, theme::play, "START");
        if (p.loop != LoopMode::Off) { marker (1, theme::overdub, "LOOP"); marker (2, theme::overdub, "END"); }
    }
    g.setColour (theme::outline);
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
}

// ---- AlgoView -----------------------------------------------------------------------------------
namespace {
    struct AlgDef { uint8_t mods[6]; uint8_t carriers; uint8_t fbDst, fbSrc; };   // same table as the FM engine
#define B(n) (1u << ((n) - 1))
    const AlgDef kAlgs[32] = {
        { { B(2), 0, B(4), B(5), B(6), 0 }, B(1)|B(3), 5, 5 }, { { B(2), 0, B(4), B(5), B(6), 0 }, B(1)|B(3), 1, 1 },
        { { B(2), B(3), 0, B(5), B(6), 0 }, B(1)|B(4), 5, 5 }, { { B(2), B(3), 0, B(5), B(6), 0 }, B(1)|B(4), 5, 3 },
        { { B(2), 0, B(4), 0, B(6), 0 }, B(1)|B(3)|B(5), 5, 5 }, { { B(2), 0, B(4), 0, B(6), 0 }, B(1)|B(3)|B(5), 5, 4 },
        { { B(2), 0, B(4), B(5)|B(6), 0, 0 }, B(1)|B(3), 5, 5 }, { { B(2), 0, B(4), B(5)|B(6), 0, 0 }, B(1)|B(3), 3, 3 },
        { { B(2), 0, B(4), B(5)|B(6), 0, 0 }, B(1)|B(3), 1, 1 }, { { B(2), B(3), 0, B(5)|B(6), 0, 0 }, B(1)|B(4), 2, 2 },
        { { B(2), B(3), 0, B(5)|B(6), 0, 0 }, B(1)|B(4), 5, 5 }, { { B(2), 0, B(4)|B(5)|B(6), 0, 0, 0 }, B(1)|B(3), 1, 1 },
        { { B(2), 0, B(4)|B(5)|B(6), 0, 0, 0 }, B(1)|B(3), 5, 5 }, { { B(2), 0, B(4)|B(5), 0, B(6), 0 }, B(1)|B(3), 5, 5 },
        { { B(2), 0, B(4)|B(5), 0, B(6), 0 }, B(1)|B(3), 1, 1 }, { { B(2)|B(3)|B(5), 0, B(4), 0, B(6), 0 }, B(1), 5, 5 },
        { { B(2)|B(3)|B(5), 0, B(4), 0, B(6), 0 }, B(1), 1, 1 }, { { B(2)|B(3)|B(4), 0, 0, B(5), B(6), 0 }, B(1), 2, 2 },
        { { B(2), B(3), 0, B(6), B(6), 0 }, B(1)|B(4)|B(5), 5, 5 }, { { B(3), B(3), 0, B(5)|B(6), 0, 0 }, B(1)|B(2)|B(4), 2, 2 },
        { { B(3), B(3), 0, B(6), B(6), 0 }, B(1)|B(2)|B(4)|B(5), 2, 2 }, { { B(2), 0, B(6), B(6), B(6), 0 }, B(1)|B(3)|B(4)|B(5), 5, 5 },
        { { 0, B(3), 0, B(6), B(6), 0 }, B(1)|B(2)|B(4)|B(5), 5, 5 }, { { 0, 0, B(6), B(6), B(6), 0 }, B(1)|B(2)|B(3)|B(4)|B(5), 5, 5 },
        { { 0, 0, 0, B(6), B(6), 0 }, B(1)|B(2)|B(3)|B(4)|B(5), 5, 5 }, { { 0, B(3), 0, B(5)|B(6), 0, 0 }, B(1)|B(2)|B(4), 5, 5 },
        { { 0, B(3), 0, B(5)|B(6), 0, 0 }, B(1)|B(2)|B(4), 2, 2 }, { { B(2), 0, B(4), B(5), 0, 0 }, B(1)|B(3)|B(6), 4, 4 },
        { { 0, 0, B(4), 0, B(6), 0 }, B(1)|B(2)|B(3)|B(5), 5, 5 }, { { 0, 0, B(4), B(5), 0, 0 }, B(1)|B(2)|B(3)|B(6), 4, 4 },
        { { 0, 0, 0, 0, B(6), 0 }, B(1)|B(2)|B(3)|B(4)|B(5), 5, 5 }, { { 0, 0, 0, 0, 0, 0 }, B(1)|B(2)|B(3)|B(4)|B(5)|B(6), 5, 5 },
    };
#undef B
    // depth of each operator: 0 = outputs to the mix (nothing above it modulates... i.e. it feeds no one)
    void opLevels (const AlgDef& a, int level[6])
    {
        for (int i = 0; i < 6; ++i) level[i] = 0;
        for (int pass = 0; pass < 6; ++pass)
            for (int i = 0; i < 6; ++i)
                for (int j = 0; j < 6; ++j)
                    if (a.mods[j] & (1u << i)) level[i] = juce::jmax (level[i], level[j] + 1);
    }
}

int AlgoView::numCarriers (int a)
{
    int n = 0;
    for (int i = 0; i < 6; ++i) if (kAlgs[juce::jlimit (0, 31, a)].carriers & (1u << i)) ++n;
    return n;
}
bool AlgoView::isCarrier (int a, int op) { return (kAlgs[juce::jlimit (0, 31, a)].carriers & (1u << op)) != 0; }

juce::Point<float> AlgoView::opCentre (int algo, int op) const
{
    const auto& a = kAlgs[juce::jlimit (0, 31, algo)];
    int level[6];
    opLevels (a, level);
    int maxL = 0;
    for (int i = 0; i < 6; ++i) maxL = juce::jmax (maxL, level[i]);
    int count = 0, rank = 0;
    for (int i = 0; i < 6; ++i) if (level[i] == level[op]) { ++count; if (i < op) ++rank; }
    auto in = getLocalBounds().toFloat().reduced (10.0f, 9.0f);
    const float rows = (float) (maxL + 1);
    const float y = in.getY() + in.getHeight() * ((float) (maxL - level[op]) + 0.5f) / rows;
    const float x = in.getX() + in.getWidth() * ((float) rank + 0.5f) / (float) count;
    return { x, y };
}

void AlgoView::poll()
{
    if (! getAlgorithm) return;
    const int a = getAlgorithm(), o = getSelectedOp ? getSelectedOp() : 0;
    if (a != shownAlgo || o != shownOp) { shownAlgo = a; shownOp = o; repaint(); }
}

void AlgoView::mouseDown (const juce::MouseEvent& e)
{
    if (! selectOp || shownAlgo < 0) return;
    for (int i = 0; i < 6; ++i)
        if (opCentre (shownAlgo, i).getDistanceFrom (e.position) < 13.0f) { selectOp (i); return; }
}

void AlgoView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::background);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
    if (shownAlgo < 0) return;
    const auto& a = kAlgs[juce::jlimit (0, 31, shownAlgo)];
    g.setColour (theme::textDim);
    for (int j = 0; j < 6; ++j)
        for (int i = 0; i < 6; ++i)
            if (a.mods[j] & (1u << i)) g.drawLine (juce::Line<float> (opCentre (shownAlgo, i), opCentre (shownAlgo, j)), 1.3f);
    const float r = 10.5f;
    for (int i = 0; i < 6; ++i) {
        const auto c = opCentre (shownAlgo, i);
        const bool car = (a.carriers & (1u << i)) != 0, sel = i == shownOp;
        g.setColour (car ? colour.withAlpha (0.85f) : theme::panelRaised);
        g.fillRoundedRectangle (c.x - r, c.y - r, 2 * r, 2 * r, 4.0f);
        g.setColour (sel ? juce::Colours::white : (car ? colour : theme::textDim));
        g.drawRoundedRectangle (c.x - r, c.y - r, 2 * r, 2 * r, 4.0f, sel ? 2.0f : 1.0f);
        g.setColour (car ? juce::Colours::black.withAlpha (0.85f) : theme::text);
        g.setFont (theme::font (12.0f, true));
        g.drawText (juce::String (i + 1), (int) (c.x - r), (int) (c.y - r), (int) (2 * r), (int) (2 * r), juce::Justification::centred);
        if (a.fbSrc == i) {   // operator feedback: a small loop on the source, a dashed link if it feeds another operator
            juce::Path loop;
            loop.addArc (c.x + r - 3.0f, c.y - r - 8.0f, 12.0f, 12.0f, 0.0f, juce::MathConstants<float>::pi * 1.5f, true);
            g.setColour (juce::Colour (0xffff9f0a));
            g.strokePath (loop, juce::PathStrokeType (1.3f));
            if (a.fbDst != i) {
                const float dash[] = { 3.0f, 3.0f };
                g.drawDashedLine (juce::Line<float> (c, opCentre (shownAlgo, a.fbDst)), dash, 2, 1.2f);
            }
        }
    }
}

} // namespace el::chimera
