#include "Synth/ui/ChimeraPanels.h"
#include "UI/LookAndFeel.h"

namespace el::chimera {

// Accessor for a LayerParams member path; works on const and non-const layers.
#define LP(expr) [] (auto& l) -> auto& { return l.expr; }

namespace {

using Rect = juce::Rectangle<int>;
using Range = juce::NormalisableRange<float>;

Range lin (float lo, float hi, float step = 0.0f) { return Range (lo, hi, step); }
Range skewed (float lo, float hi, float centre, float step = 0.0f)
{
    Range r (lo, hi, step);
    r.setSkewForCentre (centre);
    return r;
}

Rect takeRow (Rect& r, int h, int gap = 4)
{
    auto x = r.removeFromTop (h);
    r.removeFromTop (gap);
    return x;
}

void splitH (Rect r, std::initializer_list<juce::Component*> comps, int gap = 4)
{
    const int n = (int) comps.size();
    if (n == 0) return;
    const int w = (r.getWidth() - gap * (n - 1)) / n;
    int i = 0;
    for (auto* c : comps) {
        c->setBounds (r.getX() + i * (w + gap), r.getY(), i == n - 1 ? r.getRight() - (r.getX() + i * (w + gap)) : w, r.getHeight());
        ++i;
    }
}

int rowHeightFor (int avail, int rows, int gap, int lo = 18, int hi = 24)
{
    return juce::jlimit (lo, hi, (avail - gap * (rows - 1)) / juce::jmax (1, rows));
}

juce::String stName (float v) { return fmtSigned (v, 0) + " st"; }
juce::String pct (float v) { return fmtPercent (v); }
juce::String intStr (float v) { return juce::String (juce::roundToInt (v)); }

juce::String lastErrorShown;
void showError (const juce::String& msg)
{
    if (msg.isNotEmpty()) el::ui::alert ("Chimera", msg);
}

} // namespace

// =====================================================================================================
// Layer / voice column
// =====================================================================================================
namespace {
class VoicePanel : public Panel {
public:
    VoicePanel (Ctx& c, LayerHooks& h) : Panel (c, "Layer 1"), hooks (h)
    {
        engine = &enumMenu ("Engine: ", kNumEngines, [] (int i) { return juce::String (engineName ((Engine) i)); },
                            [this] { return (int) ctx.L().engine; }, [this] (int i) { hooks.setEngine ((Engine) i); });
        level = &paramBar ("Level", [this] { return ctx.proc.levelParam (ctx.layer); });
        pan = &paramBar ("Pan", [this] { return ctx.proc.panParam (ctx.layer); }, true);
        transpose = &layerBar ("Transpose", LP(transpose), lin (-48, 48, 1), 0, stName, true);
        fine = &layerBar ("Fine", LP(fine), lin (-100, 100, 1), 0, [] (float v) { return fmtSigned (v, 0) + " ct"; }, true);
        mode = &layerEnum ("Mode: ", 3, [] (int i) { static const char* n[] = { "Poly", "Mono", "Legato" }; return juce::String (n[i]); }, LP(mode));
        poly = &layerBar ("Polyphony", LP(polyphony), lin (1, 32, 1), 16, intStr);
        glide = &layerBar ("Glide", LP(glide), skewed (0, 5, 0.4f, 0.001f), 0, fmtSeconds);
        unison = &layerBar ("Unison", LP(unison), lin (1, kMaxUnison, 1), 1, intStr);
        detune = &layerBar ("Detune", LP(unisonDetune), lin (0, 1), 0.2f, pct);
        spread = &layerBar ("Spread", LP(unisonSpread), lin (0, 1), 0.6f, pct);
        velSens = &layerBar ("Vel sens", LP(velocitySens), lin (0, 1), 0.6f, pct);
        keyLo = &bar ("Key lo", [this] { return (float) ctx.L().keyLo; },
                      [this] (float v) { ctx.editLayer ([v] (LayerParams& l) { l.keyLo = juce::roundToInt (v); l.keyHi = juce::jmax (l.keyHi, l.keyLo); }); },
                      lin (0, 127, 1), 0, [] (float v) { return noteName (juce::roundToInt (v)); });
        keyHi = &bar ("Key hi", [this] { return (float) ctx.L().keyHi; },
                      [this] (float v) { ctx.editLayer ([v] (LayerParams& l) { l.keyHi = juce::roundToInt (v); l.keyLo = juce::jmin (l.keyLo, l.keyHi); }); },
                      lin (0, 127, 1), 127, [] (float v) { return noteName (juce::roundToInt (v)); });
        velLo = &bar ("Vel lo", [this] { return (float) ctx.L().velLo; },
                      [this] (float v) { ctx.editLayer ([v] (LayerParams& l) { l.velLo = juce::roundToInt (v); l.velHi = juce::jmax (l.velHi, l.velLo); }); },
                      lin (1, 127, 1), 1, intStr);
        velHi = &bar ("Vel hi", [this] { return (float) ctx.L().velHi; },
                      [this] (float v) { ctx.editLayer ([v] (LayerParams& l) { l.velHi = juce::roundToInt (v); l.velLo = juce::jmin (l.velLo, l.velHi); }); },
                      lin (1, 127, 1), 127, intStr);
        output = &layerEnum ("Output: ", kOutputPairs, [] (int i) { static const char* n[] = { "Main", "Out 3-4", "Out 5-6", "Out 7-8" }; return juce::String (n[i]); }, LP(output));
        copyBtn = &button ("Copy", [this] { if (hooks.copyLayer) hooks.copyLayer(); });
        pasteBtn = &button ("Paste", [this] { if (hooks.pasteLayer) hooks.pasteLayer(); });
        initBtn = &button ("Init", [this] { if (hooks.initLayer) hooks.initLayer(); });
        copyBtn->setTooltip ("Copy this layer (engine, filter, envelopes, sample / cart)");
        pasteBtn->setTooltip ("Paste the copied layer here");
        initBtn->setTooltip ("Reset this layer to a plain analog patch");
        level->setTooltip ("Layer level (automatable)");
        pan->setTooltip ("Layer pan (automatable)");
        glide->setTooltip ("Portamento time");
        detune->setTooltip ("Unison detune spread (up to 50 cents)");
        output->setTooltip ("Stereo output pair. Only the main pair goes through the Chimera effects.");
        addPoller ([this] { setTitle ("Layer " + juce::String (ctx.layer + 1)); });
    }

    void resized() override
    {
        auto r = body();
        const int gap = 4, rows = 15;
        const int h = rowHeightFor (r.getHeight(), rows, gap);
        for (auto* c : std::initializer_list<juce::Component*> { engine, level, pan, transpose, fine, mode, poly, glide, unison, detune, spread, velSens })
            c->setBounds (takeRow (r, h, gap));
        splitH (takeRow (r, h, gap), { keyLo, keyHi });
        splitH (takeRow (r, h, gap), { velLo, velHi });
        output->setBounds (takeRow (r, h, gap));
        splitH (takeRow (r, h, gap), { copyBtn, pasteBtn, initBtn });
    }

private:
    LayerHooks& hooks;
    MenuButton *engine {}, *mode {}, *output {};
    ValueBar *level {}, *pan {}, *transpose {}, *fine {}, *poly {}, *glide {}, *unison {}, *detune {}, *spread {}, *velSens {},
             *keyLo {}, *keyHi {}, *velLo {}, *velHi {};
    PillButton *copyBtn {}, *pasteBtn {}, *initBtn {};
};

// =====================================================================================================
// Engine sub-panels
// =====================================================================================================
class AnalogSub : public Panel {
public:
    explicit AnalogSub (Ctx& c) : Panel (c, {})
    {
        framed = false;
        static const char* waves[] = { "Sine", "Triangle", "Saw", "Square", "Noise" };
        auto waveName = [] (int i) { return juce::String (waves[i]); };
        head1 = &caption ([] { return juce::String ("OSC 1"); });
        head2 = &caption ([] { return juce::String ("OSC 2"); });
        head3 = &caption ([] { return juce::String ("MIX"); });
        for (auto* h : { head1, head2, head3 }) { h->bold = true; h->size = 11.5f; }
        wave1 = &layerEnum ("Wave: ", 5, waveName, LP(analog.wave1));
        wave2 = &layerEnum ("Wave: ", 5, waveName, LP(analog.wave2));
        pw1 = &layerBar ("Pulse width", LP(analog.pw1), lin (0.05f, 0.95f), 0.5f, pct);
        pw2 = &layerBar ("Pulse width", LP(analog.pw2), lin (0.05f, 0.95f), 0.5f, pct);
        semi = &layerBar ("Semitones", LP(analog.osc2Semi), lin (-24, 24, 1), 0, stName, true);
        fineB = &layerBar ("Fine", LP(analog.osc2Fine), lin (-100, 100, 1), 0, [] (float v) { return fmtSigned (v, 0) + " ct"; }, true);
        sync = &layerToggle ("Hard sync to osc 1", LP(analog.sync));
        mix = &layerBar ("Osc 1 / 2", LP(analog.mix), lin (0, 1), 0, pct);
        subB = &layerBar ("Sub", LP(analog.sub), lin (0, 1), 0, pct);
        noise = &layerBar ("Noise", LP(analog.noise), lin (0, 1), 0, pct);
        view1 = &make<FuncView>();
        view2 = &make<FuncView>();
        view1->key = [this] { return (int) ctx.L().analog.wave1 * 1000 + juce::roundToInt (ctx.L().analog.pw1 * 100.0f); };
        view2->key = [this] { return (int) ctx.L().analog.wave2 * 1000 + juce::roundToInt (ctx.L().analog.pw2 * 100.0f); };
        view1->fn = [this] (float t) { return waveShape (ctx.L().analog.wave1, t, ctx.L().analog.pw1); };
        view2->fn = [this] (float t) { return waveShape (ctx.L().analog.wave2, t, ctx.L().analog.pw2); };
        view3 = &make<FuncView>();
        view3->key = [this] {
            const auto& a = ctx.L().analog;
            return view1->key() * 7 + view2->key() * 13 + juce::roundToInt (a.mix * 100.0f) * 31 + juce::roundToInt (a.sub * 100.0f) * 53 + juce::roundToInt (a.osc2Semi) * 97;
        };
        view3->fn = [this] (float t) {
            const auto& a = ctx.L().analog;
            const float o1 = waveShape (a.wave1, t, a.pw1), o2 = waveShape (a.wave2, t * semisToRatio (a.osc2Semi), a.pw2);
            const float sub = waveShape (Wave::Square, t * 0.5f, 0.5f) * a.sub * 0.5f;
            return juce::jlimit (-1.0f, 1.0f, 0.8f * ((1.0f - a.mix) * o1 + a.mix * o2) + sub);
        };
        addPoller ([this] { view1->poll(); view2->poll(); view3->poll(); });
        mix->setTooltip ("Crossfade between oscillator 1 (left) and oscillator 2 (right)");
        subB->setTooltip ("Square sub-oscillator one octave below oscillator 1");
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int gap = 14;
        const int cw = (r.getWidth() - 2 * gap) / 3;
        auto c1 = r.removeFromLeft (cw); r.removeFromLeft (gap);
        auto c2 = r.removeFromLeft (cw); r.removeFromLeft (gap);
        auto c3 = r;
        const int h = 24, g = 4;
        head1->setBounds (takeRow (c1, 16, 2)); wave1->setBounds (takeRow (c1, h, g)); pw1->setBounds (takeRow (c1, h, g));
        head2->setBounds (takeRow (c2, 16, 2)); wave2->setBounds (takeRow (c2, h, g)); pw2->setBounds (takeRow (c2, h, g));
        semi->setBounds (takeRow (c2, h, g)); fineB->setBounds (takeRow (c2, h, g)); sync->setBounds (takeRow (c2, h, g));
        head3->setBounds (takeRow (c3, 16, 2)); mix->setBounds (takeRow (c3, h, g)); subB->setBounds (takeRow (c3, h, g)); noise->setBounds (takeRow (c3, h, g));
        // the three pictures share one row below the tallest column
        const int top = c2.getY() + 4, vh = juce::jlimit (40, 210, r.getBottom() - top - 2);
        view1->setBounds (0, top, cw, vh);
        view2->setBounds (c2.getX(), top, cw, vh);
        view3->setBounds (c3.getX(), top, c3.getWidth(), vh);
    }

private:
    Caption *head1 {}, *head2 {}, *head3 {};
    MenuButton *wave1 {}, *wave2 {};
    ValueBar *pw1 {}, *pw2 {}, *semi {}, *fineB {}, *mix {}, *subB {}, *noise {};
    PillButton* sync {};
    FuncView *view1 {}, *view2 {}, *view3 {};
};

class WavetableSub : public Panel {
public:
    explicit WavetableSub (Ctx& c) : Panel (c, {})
    {
        framed = false;
        const int n = WavetableBank::get().numTables();
        table = &layerEnum ("Table: ", n, [] (int i) { return juce::String (WavetableBank::get().tableName (i)); }, LP(wtTable));
        prev = &button ("<", [this] { step (-1); });
        next = &button (">", [this] { step (1); });
        pos = &layerBar ("Position", LP(wtPosition), lin (0, 1), 0, pct);
        view = &make<WavetableView>();
        view->getTable = [this] { return ctx.L().wtTable; };
        view->getPosition = [this] { return ctx.L().wtPosition; };
        view->setPosition = [this] (float v) { ctx.editLayer ([v] (LayerParams& l) { l.wtPosition = v; }); };
        addPoller ([this] { view->poll(); });
        view->setTooltip ("Current wavetable frame (drag to scan the position)");
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = takeRow (r, 24, 8);
        auto tabRow = top.removeFromLeft (juce::jmin (top.getWidth() / 2, 300));
        prev->setBounds (tabRow.removeFromLeft (26)); tabRow.removeFromLeft (3);
        next->setBounds (tabRow.removeFromRight (26)); tabRow.removeFromRight (3);
        table->setBounds (tabRow);
        top.removeFromLeft (10);
        pos->setBounds (top);
        view->setBounds (r.withTrimmedBottom (2));
    }
private:
    void step (int d)
    {
        const int n = WavetableBank::get().numTables();
        const int t = (ctx.L().wtTable + d + n) % n;
        ctx.editLayer ([t] (LayerParams& l) { l.wtTable = t; });
    }
    MenuButton* table {};
    PillButton *prev {}, *next {};
    ValueBar* pos {};
    WavetableView* view {};
};

class PluckSub : public Panel {
public:
    explicit PluckSub (Ctx& c) : Panel (c, {})
    {
        framed = false;
        decay = &layerBar ("Decay", LP(pluck.decay), lin (0, 1), 0.5f, pct);
        damping = &layerBar ("Damping", LP(pluck.damping), lin (0, 1), 0.5f, pct);
        pick = &layerBar ("Pick position", LP(pluck.pickPos), lin (0, 0.5f), 0.2f, [] (float v) { return fmtPercent (v * 2.0f); });
        colour = &layerBar ("Noise colour", LP(pluck.noiseColor), lin (0, 1), 0.5f, pct);
        decay->setTooltip ("Ring time, about 0.2 s to 10 s");
        damping->setTooltip ("0 = bright string, 1 = dark and quickly muted");
        pick->setTooltip ("Where along the string it is plucked");
        colour->setTooltip ("Tone of the excitation burst: dark to bright noise");
        view = &make<FuncView>();
        view->cycles = 1;
        view->key = [this] { return juce::roundToInt (ctx.L().pluck.decay * 100.0f) * 1000 + juce::roundToInt (ctx.L().pluck.damping * 100.0f); };
        view->fn = [this] (float t) {
            const auto& p = ctx.L().pluck;
            const float d = std::exp (-t * (7.0f - 6.0f * p.decay));
            return d * std::sin (kTwoPi * (22.0f + 20.0f * (1.0f - p.damping)) * t * (1.0f + t)) * (0.6f + 0.4f * p.noiseColor);
        };
        addPoller ([this] { view->poll(); });
    }
    void resized() override
    {
        auto r = getLocalBounds();
        splitH (takeRow (r, 24, 6), { decay, damping });
        splitH (takeRow (r, 24, 10), { pick, colour });
        view->setBounds (r.withTrimmedBottom (2));
    }
private:
    ValueBar *decay {}, *damping {}, *pick {}, *colour {};
    FuncView* view {};
};

class SamplerSub : public Panel {
public:
    explicit SamplerSub (Ctx& c) : Panel (c, {})
    {
        framed = false;
        load = &button ("Load sample", [this] { chooseFile(); });
        load->setTooltip ("Load an audio file (WAV, AIFF, FLAC, MP3, Ogg). You can also drop files on the window.");
        name = &caption ([this] {
            const auto s = ctx.P().samples[(size_t) ctx.layer];
            return s ? juce::String (s->name) + "   " + juce::String ((double) s->length() / s->sampleRate, 2) + " s"
                       + (s->stereo() ? "  stereo" : "  mono")
                     : juce::String ("No sample loaded");
        });
        name->dim = false; name->bold = true;
        root = &make<ValueBar>();
        root->label = "Root note";
        root->fill = accent;
        bars.push_back (root);
        root->defaultValue = 60.0f / 127.0f;
        root->get = [this] { return (pendingRoot >= 0 ? (float) pendingRoot : (float) currentRoot()) / 127.0f; };
        root->set = [this] (float n) { pendingRoot = juce::jlimit (0, 127, juce::roundToInt (n * 127.0f)); };
        root->format = [] (float n) { return noteName (juce::roundToInt (n * 127.0f)); };
        root->onEnd = [this] { commitRoot(); };
        root->setTooltip ("MIDI note that plays the sample at its original pitch");
        addPoller ([this] { root->poll(); });
        loop = &layerEnum ("Loop: ", 3, [] (int i) { static const char* n[] = { "Off", "Forward", "Ping-pong" }; return juce::String (n[i]); }, LP(sampler.loop));
        reverse = &layerToggle ("Reverse", LP(sampler.reverse));
        start = &layerBar ("Start", LP(sampler.start), lin (0, 1), 0, [] (float v) { return juce::String (v * 100.0f, 1) + "%"; });
        view = &make<SampleView>();
        view->getSample = [this] { return ctx.P().samples[(size_t) ctx.layer]; };
        view->getParams = [this] { return ctx.L().sampler; };
        view->setMarker = [this] (int which, float v) {
            ctx.editLayer ([which, v] (LayerParams& l) {
                if (which == 0) l.sampler.start = v;
                else if (which == 1) l.sampler.loopStart = v;
                else l.sampler.loopEnd = v;
            });
        };
        view->setTooltip ("Drag the START marker and, with looping on, the LOOP / END markers");
        addPoller ([this] { view->poll(); });
        loopStartB = &layerBar ("Loop start", LP(sampler.loopStart), lin (0, 1), 0, [] (float v) { return juce::String (v * 100.0f, 1) + "%"; });
        loopEndB = &layerBar ("Loop end", LP(sampler.loopEnd), lin (0, 1), 1, [] (float v) { return juce::String (v * 100.0f, 1) + "%"; });
        addPoller ([this] {   // loop points only matter while looping
            const float a = ctx.L().sampler.loop == LoopMode::Off ? 0.45f : 1.0f;
            if (std::abs (loopStartB->getAlpha() - a) > 0.01f) { loopStartB->setAlpha (a); loopEndB->setAlpha (a); }
        });
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = takeRow (r, 24, 6);
        load->setBounds (top.removeFromLeft (110)); top.removeFromLeft (8);
        root->setBounds (top.removeFromRight (juce::jmin (170, top.getWidth() / 2))); top.removeFromRight (8);
        name->setBounds (top);
        auto row2 = takeRow (r, 24, 6);
        splitH (row2, { loop, reverse, start, loopStartB, loopEndB }, 6);
        view->setBounds (r.withTrimmedBottom (2));
    }
private:
    int currentRoot() const { auto s = ctx.P().samples[(size_t) ctx.layer]; return s ? s->rootNote : 60; }
    void commitRoot()
    {
        const int v = pendingRoot;
        pendingRoot = -1;
        const auto l = (size_t) ctx.layer;
        const auto s = ctx.P().samples[l];
        if (v < 0 || ! s || s->rootNote == v) return;
        ctx.edit ([l, v, s] (Patch& p) {
            auto copy = std::make_shared<SampleData> (*s);   // samples are immutable and shared with the audio thread
            copy->rootNote = v;
            p.samples[l] = copy;
        });
    }
    void chooseFile()
    {
        const int layer = ctx.layer;
        chooser = std::make_unique<juce::FileChooser> ("Load sample", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        juce::Component::SafePointer<SamplerSub> sp (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [sp, layer] (const juce::FileChooser& fc) {
                                  const auto f = fc.getResult();
                                  if (sp == nullptr || ! f.existsAsFile()) return;
                                  showError (sp->ctx.proc.loadSample (layer, f));
                              });
    }
    PillButton *load {}, *reverse {};
    Caption* name {};
    ValueBar *root {}, *start {}, *loopStartB {}, *loopEndB {};
    MenuButton* loop {};
    SampleView* view {};
    int pendingRoot = -1;
    std::unique_ptr<juce::FileChooser> chooser;
};

class FmSub : public Panel {
public:
    explicit FmSub (Ctx& c) : Panel (c, {})
    {
        framed = false;
        algoView = &make<AlgoView>();
        algoView->getAlgorithm = [this] { return (int) ctx.L().fm.algorithm; };
        algoView->getSelectedOp = [this] { return selOp; };
        algoView->selectOp = [this] (int i) { selOp = i; };
        algoView->setTooltip ("Algorithm: filled boxes are carriers (heard), outlined boxes are modulators. Click an operator to edit it.");
        addPoller ([this] { algoView->poll(); });
        algo = &layerBar ("Algorithm", LP(fm.algorithm), lin (0, 31, 1), 0, [] (float v) { return juce::String (juce::roundToInt (v) + 1); });
        feedback = &layerBar ("Feedback", LP(fm.feedback), lin (0, 7, 1), 0, intStr);
        amount = &layerBar ("FM amount", LP(fmAmount), lin (0, 2), 1.0f, [] (float v) { return juce::String (v, 2); });
        ampEnv = &layerToggle ("Use amp envelope", LP(useAmpEnv));
        ampEnv->setTooltip ("On: the amp envelope shapes the sound. Off: only the operator envelopes do (like a real DX7).");
        loadBtn = &button ("Load cart (.syx)", [this] { chooseCart(); });
        saveBtn = &button ("Save cart", [this] { saveCart(); });
        voiceName = &caption ([this] {
            const auto& f = ctx.L().fm;
            return juce::String (f.name).trimEnd();
        });
        voiceName->dim = false; voiceName->bold = true;
        cartName = &caption ([this] {
            const auto cart = ctx.P().carts[(size_t) ctx.layer];
            return cart ? juce::String (ctx.P().cartNames[(size_t) ctx.layer]) + "  (" + juce::String ((int) cart->size()) + ")" : juce::String ("No cart loaded");
        });

        for (int i = 0; i < 6; ++i) {
            auto& b = toggle (juce::String (i + 1), [this, i] { return selOp == i; }, [this, i] (bool) { selOp = i; });
            opBtn[i] = &b;
        }
        opMode = &layerEnum ("Mode: ", 2, [] (int i) { return juce::String (i == 0 ? "Ratio" : "Fixed"); },
                             [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].oscMode; });
        coarse = &layerBar ("Coarse", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].freqCoarse; }, lin (0, 31, 1), 1, intStr);
        fineB = &layerBar ("Fine", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].freqFine; }, lin (0, 99, 1), 0, intStr);
        detune = &layerBar ("Detune", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].detune; }, lin (0, 14, 1), 7,
                            [] (float v) { return fmtSigned (v - 7.0f, 0); }, true);
        opLevel = &layerBar ("Level", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].outputLevel; }, lin (0, 99, 1), 0, intStr);
        velSens = &layerBar ("Vel sens", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].keyVelSens; }, lin (0, 7, 1), 0, intStr);
        egView = &make<EgView>();
        egView->get = [this] (uint8_t* r, uint8_t* l) {
            const auto& o = ctx.L().fm.op[(size_t) selOp];
            std::memcpy (r, o.egRate, 4); std::memcpy (l, o.egLevel, 4);
        };
        addPoller ([this] { egView->poll(); });
        rate[0] = &layerBar ("R1", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].egRate[0]; }, lin (0, 99, 1), 99, intStr);
        rate[1] = &layerBar ("R2", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].egRate[1]; }, lin (0, 99, 1), 99, intStr);
        rate[2] = &layerBar ("R3", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].egRate[2]; }, lin (0, 99, 1), 99, intStr);
        rate[3] = &layerBar ("R4", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].egRate[3]; }, lin (0, 99, 1), 99, intStr);
        lev[0] = &layerBar ("L1", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].egLevel[0]; }, lin (0, 99, 1), 99, intStr);
        lev[1] = &layerBar ("L2", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].egLevel[1]; }, lin (0, 99, 1), 99, intStr);
        lev[2] = &layerBar ("L3", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].egLevel[2]; }, lin (0, 99, 1), 99, intStr);
        lev[3] = &layerBar ("L4", [this] (auto& l) -> auto& { return l.fm.op[(size_t) selOp].egLevel[3]; }, lin (0, 99, 1), 0, intStr);

        for (int i = 0; i < 32; ++i) {
            auto& cell = toggle ({}, [this, i] { return cellLit (i); }, [this, i] (bool) { pickVoice (i); });
            cell.fontSize = 11.5f;
            cells[i] = &cell;
        }
        voiceHead = &caption ([this] { return juce::String (ctx.P().carts[(size_t) ctx.layer] ? "CART VOICES" : "CART VOICES  -  load or drop a .syx cartridge"); });
        voiceHead->bold = true; voiceHead->size = 11.5f;
        addPoller ([this] { refreshCells(); });
        // fonts of captions
        opHead = &caption ([this] { return "OPERATOR " + juce::String (selOp + 1) + (AlgoView::isCarrier ((int) ctx.L().fm.algorithm, selOp) ? "  (carrier)" : "  (modulator)"); });
        opHead->bold = true; opHead->size = 11.5f;
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int gap = 12;
        const int leftW = juce::jlimit (170, 230, r.getWidth() * 28 / 100);
        const int rightW = juce::jlimit (170, 300, r.getWidth() * 34 / 100);
        auto left = r.removeFromLeft (leftW); r.removeFromLeft (gap);
        auto right = r.removeFromRight (rightW); r.removeFromRight (gap);
        auto mid = r;

        const int h = 22, g = 4;
        algoView->setBounds (takeRow (left, juce::jmax (70, left.getHeight() - (3 * (h + g) + h + 8 + h + g + 18 + 16) - 6), 6));
        algo->setBounds (takeRow (left, h, g)); feedback->setBounds (takeRow (left, h, g)); amount->setBounds (takeRow (left, h, g));
        ampEnv->setBounds (takeRow (left, h, 8));
        splitH (takeRow (left, h, g), { loadBtn, saveBtn });
        cartName->setBounds (takeRow (left, 16, 2));
        voiceName->setBounds (left.removeFromTop (16));

        auto tabs = takeRow (mid, 24, 6);
        splitH (tabs, { opBtn[0], opBtn[1], opBtn[2], opBtn[3], opBtn[4], opBtn[5] }, 3);
        opHead->setBounds (takeRow (mid, 16, 4));
        splitH (takeRow (mid, h, g), { opMode, coarse });
        splitH (takeRow (mid, h, g), { fineB, detune });
        splitH (takeRow (mid, h, g), { opLevel, velSens });
        const int egH = juce::jmax (24, mid.getHeight() - 4 * (h + g) - 6);
        egView->setBounds (takeRow (mid, egH, 6));
        for (int i = 0; i < 4; ++i) splitH (takeRow (mid, h, g), { rate[i], lev[i] });

        voiceHead->setBounds (takeRow (right, 16, 2));
        const int cellH = juce::jlimit (14, 24, (right.getHeight() - 15 * 2) / 16);
        const int cw = (right.getWidth() - 4) / 2;
        for (int i = 0; i < 32; ++i) {
            const int col = i / 16, rw = i % 16;
            cells[i]->setBounds (right.getX() + col * (cw + 4), right.getY() + rw * (cellH + 2), cw, cellH);
        }
    }

private:
    bool cellLit (int i) const
    {
        const auto c = ctx.P().carts[(size_t) ctx.layer];
        if (! c || i >= (int) c->size() || i != pickedVoice) return false;
        return std::memcmp ((*c)[(size_t) i].name, ctx.L().fm.name, 10) == 0;
    }
    void refreshCells()
    {
        const auto c = ctx.P().carts[(size_t) ctx.layer];
        const int n = c ? (int) c->size() : 0;
        for (int i = 0; i < 32; ++i) {
            const bool vis = i < n;
            if (cells[i]->isVisible() != vis) cells[i]->setVisible (vis);
            if (vis) {
                char nm[11] {};
                std::memcpy (nm, (*c)[(size_t) i].name, 10);
                const auto t = juce::String (i + 1).paddedLeft ('0', 2) + " " + juce::String (nm).trimEnd();
                if (cells[i]->text != t) { cells[i]->text = t; cells[i]->repaint(); }
            }
        }
    }
    void pickVoice (int i)
    {
        pickedVoice = i;
        ctx.proc.selectCartVoice (ctx.layer, i);
    }
    void chooseCart()
    {
        const int layer = ctx.layer;
        chooser = std::make_unique<juce::FileChooser> ("Load DX7 cartridge", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.syx");
        juce::Component::SafePointer<FmSub> sp (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [sp, layer] (const juce::FileChooser& fc) {
                                  const auto f = fc.getResult();
                                  if (sp == nullptr || ! f.existsAsFile()) return;
                                  sp->pickedVoice = 0;
                                  showError (sp->ctx.proc.loadCart (layer, f));
                              });
    }
    void saveCart()
    {
        const int layer = ctx.layer;
        auto def = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        const auto nm = ctx.P().cartNames[(size_t) layer];
        def = def.getChildFile ((nm.empty() ? juce::String ("Chimera Cart") : juce::String (nm)) + ".syx");
        chooser = std::make_unique<juce::FileChooser> ("Save DX7 cartridge", def, "*.syx");
        juce::Component::SafePointer<FmSub> sp (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [sp, layer] (const juce::FileChooser& fc) {
                                  auto f = fc.getResult();
                                  if (sp == nullptr || f == juce::File()) return;
                                  if (f.getFileExtension().isEmpty()) f = f.withFileExtension ("syx");
                                  showError (sp->ctx.proc.saveCart (layer, f));
                              });
    }

    int selOp = 0, pickedVoice = -1;
    AlgoView* algoView {};
    ValueBar *algo {}, *feedback {}, *amount {}, *coarse {}, *fineB {}, *detune {}, *opLevel {}, *velSens {};
    ValueBar *rate[4] {}, *lev[4] {};
    PillButton *ampEnv {}, *loadBtn {}, *saveBtn {}, *opBtn[6] {}, *cells[32] {};
    MenuButton* opMode {};
    Caption *voiceName {}, *cartName {}, *voiceHead {}, *opHead {};
    EgView* egView {};
    std::unique_ptr<juce::FileChooser> chooser;
};

class OffSub : public Panel {
public:
    OffSub (Ctx& c, LayerHooks& h) : Panel (c, {}), hooks (h)
    {
        framed = false;
        msg = &caption ([] { return juce::String ("This layer is off. Choose an engine:"); });
        msg->just = juce::Justification::centred;
        msg->size = 14.0f;
        static const Engine engines[] = { Engine::Analog, Engine::Wavetable, Engine::FM, Engine::Sampler, Engine::Pluck };
        for (int i = 0; i < 5; ++i) {
            auto& b = button (engineName (engines[i]), [this, i] { if (hooks.setEngine) hooks.setEngine (engines[i]); });
            btn[i] = &b;
        }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        const int h = 30, w = juce::jmin (110, (r.getWidth() - 5 * 8) / 5);
        const int total = 5 * w + 4 * 8;
        const int y = r.getCentreY() - 10;
        msg->setBounds (r.getX(), y - 34, r.getWidth(), 24);
        for (int i = 0; i < 5; ++i) btn[i]->setBounds (r.getCentreX() - total / 2 + i * (w + 8), y, w, h);
    }
private:
    LayerHooks& hooks;
    Caption* msg {};
    PillButton* btn[5] {};
};

class EnginePanel : public Panel {
public:
    EnginePanel (Ctx& c, LayerHooks& h) : Panel (c, "Engine")
    {
        analog = &sub<AnalogSub> (c);
        wavetable = &sub<WavetableSub> (c);
        fm = &sub<FmSub> (c);
        sampler = &sub<SamplerSub> (c);
        pluck = &sub<PluckSub> (c);
        off = &sub<OffSub> (c, h);
        addPoller ([this] { updateVisibility(); });
        updateVisibility();
    }
    void resized() override
    {
        for (auto* s : subs) s->setBounds (body());
    }
private:
    void updateVisibility()
    {
        const auto e = ctx.L().engine;
        if (e == shown) return;
        shown = e;
        analog->setVisible (e == Engine::Analog);
        wavetable->setVisible (e == Engine::Wavetable);
        fm->setVisible (e == Engine::FM);
        sampler->setVisible (e == Engine::Sampler);
        pluck->setVisible (e == Engine::Pluck);
        off->setVisible (e == Engine::Off);
        setTitle (e == Engine::Off ? juce::String ("Engine") : "Engine - " + juce::String (engineName (e)));
    }
    Engine shown = (Engine) 255;
    Panel *analog {}, *wavetable {}, *fm {}, *sampler {}, *pluck {}, *off {};
};

// =====================================================================================================
// Filter, envelopes
// =====================================================================================================
class FilterPanel : public Panel {
public:
    explicit FilterPanel (Ctx& c) : Panel (c, "Filter")
    {
        type = &layerEnum ("Type: ", kNumFilterTypes, [] (int i) { return juce::String (filterTypeName ((FilterType) i)); }, LP(filter));
        cutoff = &paramBar ("Cutoff", [this] { return ctx.proc.cutoffParam (ctx.layer); });
        reso = &paramBar ("Resonance", [this] { return ctx.proc.resonanceParam (ctx.layer); });
        drive = &layerBar ("Drive", LP(drive), lin (0, 1), 0, pct);
        keyTrack = &layerBar ("Key track", LP(keyTrack), lin (0, 1), 0, pct);
        envAmt = &layerBar ("Env amount", LP(filterEnvAmount), lin (-1, 1), 0, [] (float v) { return fmtSigned (v * 60.0f, 0) + " st"; }, true);
        envAmt->setTooltip ("How far the filter envelope moves the cutoff (up to 60 semitones either way)");
        keyTrack->setTooltip ("How much the cutoff follows the played note (100% = one octave per octave)");
    }
    void resized() override
    {
        auto r = body();
        const int h = rowHeightFor (r.getHeight(), 6, 4, 18, 24);
        for (auto* c : std::initializer_list<juce::Component*> { type, cutoff, reso, drive, keyTrack, envAmt }) c->setBounds (takeRow (r, h, 4));
    }
private:
    MenuButton* type {};
    ValueBar *cutoff {}, *reso {}, *drive {}, *keyTrack {}, *envAmt {};
};

class EnvPanel : public Panel {
public:
    EnvPanel (Ctx& c, int which) : Panel (c, which == 0 ? "Amp envelope" : which == 1 ? "Filter envelope" : "Mod envelope")
    {
        EnvParams LayerParams::*m = which == 0 ? &LayerParams::ampEnv : which == 1 ? &LayerParams::filterEnv : &LayerParams::modEnv;
        view = &make<EnvView>();
        view->get = [this, m] { return ctx.L().*m; };
        addPoller ([this] { view->poll(); });
        const auto tr = skewed (0.001f, 12.0f, 0.35f);
        a = &layerBar ("Attack", [m] (auto& l) -> auto& { return (l.*m).attack; }, tr, 0.003f, fmtSeconds);
        d = &layerBar ("Decay", [m] (auto& l) -> auto& { return (l.*m).decay; }, tr, 0.4f, fmtSeconds);
        s = &layerBar ("Sustain", [m] (auto& l) -> auto& { return (l.*m).sustain; }, lin (0, 1), 0.8f, pct);
        r = &layerBar ("Release", [m] (auto& l) -> auto& { return (l.*m).release; }, tr, 0.25f, fmtSeconds);
    }
    void resized() override
    {
        auto area = body();
        const int h = rowHeightFor (area.getHeight() - 30, 4, 3, 17, 24);
        auto bars4 = area.removeFromBottom (4 * h + 3 * 3);
        area.removeFromBottom (5);
        view->setBounds (area);
        a->setBounds (takeRow (bars4, h, 3)); d->setBounds (takeRow (bars4, h, 3)); s->setBounds (takeRow (bars4, h, 3)); r->setBounds (takeRow (bars4, h, 3));
    }
private:
    EnvView* view {};
    ValueBar *a {}, *d {}, *s {}, *r {};
};

// =====================================================================================================
// LFOs, mod matrix
// =====================================================================================================
std::vector<float> beatList (float maxBeats)
{
    std::vector<float> v;
    for (auto b : beatChoices()) if (b <= maxBeats + 1.0e-4f) v.push_back (b);
    return v;
}
int nearestIndex (const std::vector<float>& v, float x)
{
    int best = 0;
    for (int i = 1; i < (int) v.size(); ++i) if (std::abs (v[(size_t) i] - x) < std::abs (v[(size_t) best] - x)) best = i;
    return best;
}

class LfoPanel : public Panel {
public:
    LfoPanel (Ctx& c, bool glob, int idx)
        : Panel (c, (glob ? "Global LFO " : "LFO ") + juce::String (idx + 1)), global (glob), index (idx)
    {
        view = &make<FuncView>();
        view->cycles = 2;
        view->key = [this] { return (int) get().shape; };
        view->fn = [this] (float t) { return lfoShape (get().shape, t); };
        addPoller ([this] { view->poll(); });
        shape = &enumMenu ("Shape: ", kNumLfoShapes, [] (int i) { return juce::String (lfoShapeName ((LfoShape) i)); },
                           [this] { return (int) get().shape; }, [this] (int i) { edit ([i] (LfoParams& p) { p.shape = (LfoShape) i; }); });
        sync = &toggle ("Sync", [this] { return get().sync; }, [this] (bool v) { edit ([v] (LfoParams& p) { p.sync = v; }); });
        sync->setTooltip ("Lock the rate to the song tempo");
        rate = &bar ("Rate", [this] { return get().rateHz; }, [this] (float v) { edit ([v] (LfoParams& p) { p.rateHz = v; }); },
                     skewed (0.01f, 40.0f, 2.0f), 2.0f, [] (float v) { return juce::String (v, v < 10.0f ? 2 : 1) + " Hz"; });
        beatsChoices = beatList (16.0f);
        beats = &enumMenu ("Rate: ", (int) beatsChoices.size(), [this] (int i) { return fmtBeats (beatsChoices[(size_t) i]); },
                           [this] { return nearestIndex (beatsChoices, get().beats); },
                           [this] (int i) { const float b = beatsChoices[(size_t) i]; edit ([b] (LfoParams& p) { p.beats = b; }); });
        retrig = &toggle ("Retrigger", [this] { return get().retrigger; }, [this] (bool v) { edit ([v] (LfoParams& p) { p.retrigger = v; }); });
        retrig->setTooltip ("Restart the LFO on every note (off = free-running)");
        fade = &bar ("Fade in", [this] { return get().fadeIn; }, [this] (float v) { edit ([v] (LfoParams& p) { p.fadeIn = v; }); },
                     skewed (0, 5, 0.5f, 0.001f), 0, fmtSeconds);
        retrig->setVisible (! global); fade->setVisible (! global);
        addPoller ([this] {
            const bool s = get().sync;
            if (rate->isVisible() == s) { rate->setVisible (! s); beats->setVisible (s); }
        });
        beats->setVisible (false);
    }
    void resized() override
    {
        auto r = body();
        const int h = rowHeightFor (r.getHeight() - 36, global ? 3 : 4, 4, 18, 24);
        view->setBounds (takeRow (r, juce::jmax (28, r.getHeight() - (global ? 3 : 4) * (h + 4) + 4), 6));
        shape->setBounds (takeRow (r, h, 4));
        if (global) {
            splitH (takeRow (r, h, 4), { sync });
            rate->setBounds (r.removeFromTop (h)); beats->setBounds (rate->getBounds());
        } else {
            splitH (takeRow (r, h, 4), { sync, retrig });
            auto rr = takeRow (r, h, 4);
            rate->setBounds (rr); beats->setBounds (rr);
            fade->setBounds (takeRow (r, h, 4));
        }
    }
private:
    static float lfoShape (LfoShape s, float t)
    {
        const float ph = t - std::floor (t);
        auto rnd = [] (int k) { uint32_t h = (uint32_t) k * 2654435761u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return (float) (h & 1023u) / 511.5f - 1.0f; };
        switch (s) {
            case LfoShape::Sine: return std::sin (kTwoPi * ph);
            case LfoShape::Triangle: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
            case LfoShape::SawUp: return 2.0f * ph - 1.0f;
            case LfoShape::SawDown: return 1.0f - 2.0f * ph;
            case LfoShape::Square: return ph < 0.5f ? 1.0f : -1.0f;
            case LfoShape::SampleHold: return rnd ((int) std::floor (t * 6.0f) + 3);
            case LfoShape::SmoothRandom: {
                const float x = t * 4.0f; const int k = (int) std::floor (x); const float f = x - (float) k;
                return rnd (k + 7) + (rnd (k + 8) - rnd (k + 7)) * (f * f * (3.0f - 2.0f * f));
            }
        }
        return 0.0f;
    }
    const LfoParams& get() const { return global ? ctx.P().globalLfo[index] : ctx.L().lfo[index]; }
    void edit (std::function<void (LfoParams&)> f)
    {
        const auto i = (size_t) index;
        if (global) ctx.edit ([i, f] (Patch& p) { f (p.globalLfo[i]); });
        else ctx.editLayer ([i, f] (LayerParams& l) { f (l.lfo[i]); });
    }
    bool global;
    int index;
    std::vector<float> beatsChoices;
    FuncView* view {};
    MenuButton *shape {}, *beats {};
    PillButton *sync {}, *retrig {};
    ValueBar *rate {}, *fade {};
};

class MatrixPanel : public Panel {
public:
    explicit MatrixPanel (Ctx& c) : Panel (c, "Modulation matrix")
    {
        for (int i = 0; i < kMatrixSlots; ++i) {
            num[i] = &caption ([i] { return juce::String (i + 1); });
            num[i]->just = juce::Justification::centred;
            src[i] = &layerEnum ({}, kNumModSrcs, [this] (int k) {
                                     const int m = k - (int) ModSrc::Macro1;   // macros show their patch names
                                     return m >= 0 && m < kMacros ? juce::String (ctx.P().macroNames[(size_t) m]) : juce::String (modSrcName ((ModSrc) k));
                                 },
                                 [i] (auto& l) -> auto& { return l.mods[i].src; });
            dst[i] = &layerEnum ({}, kNumModDsts, [] (int k) { return juce::String (modDstName ((ModDst) k)); },
                                 [i] (auto& l) -> auto& { return l.mods[i].dst; });
            amt[i] = &layerBar ("Amount", [i] (auto& l) -> auto& { return l.mods[i].amount; }, lin (-1, 1), 0, [] (float v) { return fmtSigned (v, 2); }, true);
        }
        headSrc = &caption ([] { return juce::String ("SOURCE"); });
        headDst = &caption ([] { return juce::String ("DESTINATION"); });
        headAmt = &caption ([] { return juce::String ("AMOUNT"); });
        for (auto* h : { headSrc, headDst, headAmt }) { h->size = 10.5f; h->bold = true; }
        bend = &bar ("Pitch bend range", [this] { return ctx.P().bendRange; },
                     [this] (float v) { ctx.edit ([v] (Patch& p) { p.bendRange = v; }); }, lin (0, 24, 1), 2, [] (float v) { return "+/-" + juce::String (juce::roundToInt (v)) + " st"; });
        bend->setTooltip ("Pitch bend range for all layers");
    }
    void resized() override
    {
        auto hdr = Rect (getWidth() - 8 - 190, 3, 190, 20);
        bend->setBounds (hdr);
        auto b = body();
        const int numW = 22, gap = 6;
        const int amtW = juce::jmax (110, b.getWidth() * 34 / 100);
        const int menuW = (b.getWidth() - numW - amtW - 3 * gap) / 2;
        auto head = takeRow (b, 14, 2);
        head.removeFromLeft (numW + gap);
        headSrc->setBounds (head.removeFromLeft (menuW)); head.removeFromLeft (gap);
        headDst->setBounds (head.removeFromLeft (menuW)); head.removeFromLeft (gap);
        headAmt->setBounds (head);
        const int h = rowHeightFor (b.getHeight(), kMatrixSlots, 4, 20, 28);
        for (int i = 0; i < kMatrixSlots; ++i) {
            auto row = takeRow (b, h, 4);
            num[i]->setBounds (row.removeFromLeft (numW)); row.removeFromLeft (gap);
            src[i]->setBounds (row.removeFromLeft (menuW)); row.removeFromLeft (gap);
            dst[i]->setBounds (row.removeFromLeft (menuW)); row.removeFromLeft (gap);
            amt[i]->setBounds (row);
        }
    }
private:
    Caption *num[kMatrixSlots] {}, *headSrc {}, *headDst {}, *headAmt {};
    MenuButton *src[kMatrixSlots] {}, *dst[kMatrixSlots] {};
    ValueBar *amt[kMatrixSlots] {}, *bend {};
};

// =====================================================================================================
// Effects
// =====================================================================================================
class FxPanel : public Panel {
public:
    FxPanel (Ctx& c, int which) : Panel (c, which == 0 ? "Chorus" : which == 1 ? "Delay" : "Reverb"), kind (which)
    {
        accent = theme::accent;
        if (kind == 0) {
            on = &pToggle ("On", [] (auto& p) -> auto& { return p.chorus.on; });
            add ("Rate", [] (auto& p) -> auto& { return p.chorus.rate; }, skewed (0.05f, 5.0f, 0.6f), 0.4f, [] (float v) { return juce::String (v, 2) + " Hz"; });
            add ("Depth", [] (auto& p) -> auto& { return p.chorus.depth; }, lin (0, 1), 0.5f, pct);
            add ("Mix", [] (auto& p) -> auto& { return p.chorus.mix; }, lin (0, 1), 0.35f, pct);
        } else if (kind == 1) {
            on = &pToggle ("On", [] (auto& p) -> auto& { return p.delay.on; });
            sync = &pToggle ("Tempo sync", [] (auto& p) -> auto& { return p.delay.sync; });
            beatsChoices = beatList (4.0f);
            beats = &enumMenu ("Time: ", (int) beatsChoices.size(), [this] (int i) { return fmtBeats (beatsChoices[(size_t) i]); },
                               [this] { return nearestIndex (beatsChoices, ctx.P().delay.beats); },
                               [this] (int i) { const float b = beatsChoices[(size_t) i]; ctx.edit ([b] (Patch& p) { p.delay.beats = b; }); });
            ms = &pBar ("Time", [] (auto& p) -> auto& { return p.delay.ms; }, skewed (1, 2000, 300), 375, [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; });
            add ("Feedback", [] (auto& p) -> auto& { return p.delay.feedback; }, lin (0, 0.95f), 0.35f, pct);
            add ("Tone", [] (auto& p) -> auto& { return p.delay.tone; }, lin (0, 1), 0.6f, pct);
            add ("Mix", [] (auto& p) -> auto& { return p.delay.mix; }, lin (0, 1), 0.25f, pct);
            pingPong = &pToggle ("Ping-pong", [] (auto& p) -> auto& { return p.delay.pingPong; });
            addPoller ([this] {
                const bool s = ctx.P().delay.sync;
                if (ms->isVisible() == s) { ms->setVisible (! s); beats->setVisible (s); }
            });
            beats->setVisible (false);
        } else {
            on = &pToggle ("On", [] (auto& p) -> auto& { return p.reverb.on; });
            add ("Size", [] (auto& p) -> auto& { return p.reverb.size; }, lin (0, 1), 0.6f, pct);
            add ("Damping", [] (auto& p) -> auto& { return p.reverb.damping; }, lin (0, 1), 0.5f, pct);
            add ("Pre-delay", [] (auto& p) -> auto& { return p.reverb.predelayMs; }, lin (0, 200), 10, [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; });
            add ("Width", [] (auto& p) -> auto& { return p.reverb.width; }, lin (0, 1), 1.0f, pct);
            add ("Mix", [] (auto& p) -> auto& { return p.reverb.mix; }, lin (0, 1), 0.25f, pct);
        }
        addPoller ([this] { dim(); });
    }
    void resized() override
    {
        auto r = body();
        const int count = (int) items.size() + 1 + (kind == 1 ? 2 : 0);
        const int h = rowHeightFor (r.getHeight(), count, 6, 22, 30);
        if (kind == 1) {
            splitH (takeRow (r, h, 6), { on, sync });
            auto t = takeRow (r, h, 6);
            ms->setBounds (t); beats->setBounds (t);
            for (auto* c : items) c->setBounds (takeRow (r, h, 6));
            pingPong->setBounds (takeRow (r, h, 6));
        } else {
            on->setBounds (takeRow (r, h, 6));
            for (auto* c : items) c->setBounds (takeRow (r, h, 6));
        }
    }
private:
    template <class Acc> PillButton& pToggle (const juce::String& label, Acc acc)
    {
        return toggle (label, [this, acc] { return (bool) acc (ctx.P()); },
                       [this, acc] (bool v) { ctx.edit ([acc, v] (Patch& p) { acc (p) = v; }); });
    }
    template <class Acc> ValueBar& pBar (const juce::String& label, Acc acc, Range r, float def, std::function<juce::String (float)> fmt)
    {
        return bar (label, [this, acc] { return (float) acc (ctx.P()); },
                    [this, acc] (float v) { ctx.edit ([acc, v] (Patch& p) { acc (p) = v; }); }, r, def, std::move (fmt));
    }
    template <class Acc> void add (const juce::String& label, Acc acc, Range r, float def, std::function<juce::String (float)> fmt)
    {
        items.push_back (&pBar (label, acc, r, def, std::move (fmt)));
    }
    void dim()   // controls of a switched-off effect look dimmed
    {
        const bool isOn = kind == 0 ? ctx.P().chorus.on : kind == 1 ? ctx.P().delay.on : ctx.P().reverb.on;
        if (isOn != shownOn) {
            shownOn = isOn;
            for (auto* c : items) c->setAlpha (isOn ? 1.0f : 0.5f);
            if (kind == 1) { ms->setAlpha (isOn ? 1.0f : 0.5f); beats->setAlpha (isOn ? 1.0f : 0.5f); }
        }
    }
    int kind;
    bool shownOn = true;
    std::vector<float> beatsChoices;
    PillButton *on {}, *sync {}, *pingPong {};
    MenuButton* beats {};
    ValueBar* ms {};
    std::vector<ValueBar*> items;
};

} // namespace

std::unique_ptr<Panel> makeVoicePanel (Ctx& c, LayerHooks& h) { return std::make_unique<VoicePanel> (c, h); }
std::unique_ptr<Panel> makeEnginePanel (Ctx& c, LayerHooks& h) { return std::make_unique<EnginePanel> (c, h); }
std::unique_ptr<Panel> makeFilterPanel (Ctx& c) { return std::make_unique<FilterPanel> (c); }
std::unique_ptr<Panel> makeEnvPanel (Ctx& c, int which) { return std::make_unique<EnvPanel> (c, which); }
std::unique_ptr<Panel> makeLfoPanel (Ctx& c, bool global, int index) { return std::make_unique<LfoPanel> (c, global, index); }
std::unique_ptr<Panel> makeMatrixPanel (Ctx& c) { return std::make_unique<MatrixPanel> (c); }
std::unique_ptr<Panel> makeFxPanel (Ctx& c, int which) { return std::make_unique<FxPanel> (c, which); }

} // namespace el::chimera
