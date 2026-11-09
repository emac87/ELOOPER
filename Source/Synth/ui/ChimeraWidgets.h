#pragma once
// Chimera editor building blocks: Panel (titled rounded box that owns its controls and polls them),
// small controls (pill button, menu button, caption) and the little drawn views (envelopes, wave
// shapes, wavetable frame, sample waveform, FM algorithm). Controls read the processor's patch
// through getters and are refreshed by a ~20 Hz poll, so they never fight a drag in progress.
#include <JuceHeader.h>
#include "UI/StripWidgets.h"
#include "UI/Theme.h"
#include "Synth/plugin/ChimeraProcessor.h"
#include <functional>

namespace el::chimera {

juce::Colour layerColour (int layer);
juce::String noteName (int note);                 // 60 = C3
juce::String fmtSeconds (float s);
juce::String fmtPercent (float v01);
juce::String fmtSigned (float v, int decimals = 2);
juce::String fmtBeats (float beats);
const std::vector<float>& beatChoices();           // 1/16 .. 16 beats
float waveShape (Wave w, float phase01, float pw);  // -1..1, for drawing

// What the panels edit: the processor plus the layer that is currently selected.
struct Ctx {
    ChimeraProcessor& proc;
    int layer = 0;
    const Patch& P() const { return proc.patch(); }
    const LayerParams& L() const { return proc.patch().layers[(size_t) layer]; }
    void edit (const std::function<void (Patch&)>& fn) { proc.editPatch (fn); }
    template <class F> void editLayer (F f)
    {
        const auto l = (size_t) layer;
        proc.editPatch ([l, f] (Patch& p) { f (p.layers[l]); });
    }
};

// ---- small controls --------------------------------------------------------------------------
class PillButton : public juce::Component, public juce::SettableTooltipClient {
public:
    juce::String text;
    juce::Colour colour = theme::accent;
    float fontSize = 12.5f;
    std::function<bool()> get;                 // set = toggle button, bound to get / set
    std::function<void (bool)> set;
    std::function<void()> onClick;             // plain button
    void poll() { if (get) { const bool v = get(); if (v != on) { on = v; repaint(); } } }
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
private:
    bool on = false;
};

class MenuButton : public MenuSlot {
public:
    std::function<juce::String()> getText;
    std::function<void (juce::PopupMenu&)> buildMenu;   // item ids must be > 0
    std::function<void (int)> onChoose;
    MenuButton() { onClick = [this] (juce::Component*) { show(); }; }
    void poll() { if (getText) setText (getText()); }
    void show();
};

class Caption : public juce::Component {
public:
    std::function<juce::String()> get;
    juce::Justification just = juce::Justification::centredLeft;
    float size = 12.5f;
    bool bold = false, dim = true;
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void poll() { if (get) setText (get()); }
    void paint (juce::Graphics&) override;
private:
    juce::String text;
};

class PeakMeter : public juce::Component, public juce::SettableTooltipClient {
public:
    void setLevel (float linear);
    void paint (juce::Graphics&) override;
private:
    float shown = 0.0f, hold = 0.0f;
};

// ---- Panel -----------------------------------------------------------------------------------
class Panel : public juce::Component {
public:
    Panel (Ctx& c, juce::String t) : ctx (c), title (std::move (t)) {}
    void setTitle (const juce::String& t) { if (t != title) { title = t; repaint(); } }
    void setAccent (juce::Colour c);
    void pollAll();
    int headerHeight() const { return title.isEmpty() ? 6 : 26; }
    juce::Rectangle<int> body() const { return getLocalBounds().reduced (8, 0).withTrimmedTop (headerHeight()).withTrimmedBottom (7); }
    void paint (juce::Graphics&) override;

    Ctx& ctx;
    juce::Colour accent { theme::accent };
    bool framed = true;   // false for sub-panels drawn inside another panel's frame

protected:
    template <class T, class... Args> T& make (Args&&... a)
    {
        auto c = std::make_unique<T> (std::forward<Args> (a)...);
        auto& r = *c;
        addAndMakeVisible (r);
        owned.push_back (std::move (c));
        return r;
    }
    template <class T, class... Args> T& sub (Args&&... a)
    {
        auto& r = make<T> (std::forward<Args> (a)...);
        subs.push_back (&r);
        return r;
    }

    // value bar over a real-valued range (patch edits)
    ValueBar& bar (const juce::String& label, std::function<float()> get, std::function<void (float)> set,
                   juce::NormalisableRange<float> range, float def, std::function<juce::String (float)> fmt = {},
                   bool bipolar = false);
    // value bar bound to an automatable parameter (begin / end gestures)
    ValueBar& paramBar (const juce::String& label, std::function<juce::AudioParameterFloat*()> param, bool bipolar = false);
    MenuButton& enumMenu (const juce::String& prefix, int count, std::function<juce::String (int)> nameOf,
                          std::function<int()> getIdx, std::function<void (int)> setIdx);
    PillButton& toggle (const juce::String& label, std::function<bool()> get, std::function<void (bool)> set);
    PillButton& button (const juce::String& label, std::function<void()> onClick);
    Caption& caption (std::function<juce::String()> get);
    void addPoller (std::function<void()> f) { pollers.push_back (std::move (f)); }

    // layer-field helpers: acc is a generic lambda returning a reference into LayerParams
    template <class Acc>
    ValueBar& layerBar (const juce::String& label, Acc acc, juce::NormalisableRange<float> range, float def,
                        std::function<juce::String (float)> fmt = {}, bool bipolar = false)
    {
        return bar (label, [this, acc] { return (float) acc (ctx.L()); },
                    [this, acc] (float v) {
                        ctx.editLayer ([acc, v] (LayerParams& l) {
                            using T = std::decay_t<decltype (acc (l))>;
                            acc (l) = fromFloat<T> (v);
                        });
                    }, range, def, std::move (fmt), bipolar);
    }
    template <class Acc>
    PillButton& layerToggle (const juce::String& label, Acc acc)
    {
        return toggle (label, [this, acc] { return (bool) acc (ctx.L()); },
                       [this, acc] (bool v) { ctx.editLayer ([acc, v] (LayerParams& l) { acc (l) = v; }); });
    }
    template <class Acc>
    MenuButton& layerEnum (const juce::String& prefix, int count, std::function<juce::String (int)> nameOf, Acc acc)
    {
        using T = std::decay_t<decltype (acc (std::declval<LayerParams&>()))>;
        return enumMenu (prefix, count, std::move (nameOf), [this, acc] { return (int) acc (ctx.L()); },
                         [this, acc] (int i) { ctx.editLayer ([acc, i] (LayerParams& l) { acc (l) = (T) i; }); });
    }

    template <class T> static T fromFloat (float v)
    {
        if constexpr (std::is_same_v<T, bool>) return v > 0.5f;
        else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) return (T) juce::roundToInt (v);
        else return (T) v;
    }

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::function<void()>> pollers;
    std::vector<ValueBar*> bars;
    std::vector<PillButton*> pills;
    std::vector<Panel*> subs;
    juce::String title;
};

// ---- drawn views -----------------------------------------------------------------------------
// A function of phase drawn over `cycles` periods (wave shapes, LFO shapes). `key` changes whenever
// the picture should be redrawn.
class FuncView : public juce::Component {
public:
    std::function<int()> key;
    std::function<float (float)> fn;
    int cycles = 2;
    juce::Colour colour { theme::accent };
    void poll() { if (key) { const int k = key(); if (k != shownKey) { shownKey = k; repaint(); } } }
    void paint (juce::Graphics&) override;
private:
    int shownKey = -1;
};

class EnvView : public juce::Component {
public:
    std::function<EnvParams()> get;
    juce::Colour colour { theme::accent };
    void poll();
    void paint (juce::Graphics&) override;
private:
    EnvParams shown { -1, -1, -1, -1 };
};

// DX7 operator EG: 4 rates and 4 levels (0..99)
class EgView : public juce::Component {
public:
    std::function<void (uint8_t* rates, uint8_t* levels)> get;
    juce::Colour colour { theme::accent };
    void poll();
    void paint (juce::Graphics&) override;
private:
    uint8_t sr[4] { 255, 0, 0, 0 }, sl[4] {};
};

class WavetableView : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<int()> getTable;
    std::function<float()> getPosition;
    std::function<void (float)> setPosition;   // drag horizontally
    juce::Colour colour { theme::accent };
    void poll();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
private:
    void drag (const juce::MouseEvent&);
    int shownTable = -1; float shownPos = -1.0f;
};

class SampleView : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<SamplePtr()> getSample;
    std::function<SamplerParams()> getParams;
    std::function<void (int which, float v)> setMarker;   // 0 start, 1 loop start, 2 loop end
    juce::Colour colour { theme::accent };
    void poll();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }
private:
    float markerX (int which, const SamplerParams&) const;
    void rebuildPeaks (const SampleData*);
    const SampleData* cached = nullptr;
    SamplePtr keep;
    std::vector<float> peakMin, peakMax;
    SamplerParams shownParams {};
    bool first = true;
    int dragging = -1;
};

class AlgoView : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<int()> getAlgorithm;
    std::function<int()> getSelectedOp;
    std::function<void (int)> selectOp;
    juce::Colour colour { theme::accent };
    void poll();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    static int numCarriers (int algorithm);
    static bool isCarrier (int algorithm, int op);
private:
    juce::Point<float> opCentre (int algorithm, int op) const;
    int shownAlgo = -1, shownOp = -1;
};

} // namespace el::chimera
