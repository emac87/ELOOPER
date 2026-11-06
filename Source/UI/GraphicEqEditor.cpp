#include "UI/GraphicEqEditor.h"
#include "UI/Theme.h"
#include "Core/GraphicEq.h"
#include "Core/ParamMapping.h"
#include <cmath>

namespace el {

namespace {
    const char* const kBandNames[kEqBands] = { "31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k" };
    constexpr float kCurveRangeDb = 15.0f;
    juce::String dbText (double n)
    {
        const float db = pm::eqToDb ((float) n);
        return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    }
}

GraphicEqEditor::GraphicEqEditor (EngineController& c, Id track, bool compactMode)
    : controller (c), trackId (track), compact (compactMode)
{
    for (auto& v : values) v = 0.5f;
    if (compact) setInterceptsMouseClicks (true, false);

    if (! compact)
    {
        for (int i = 0; i < kEqBands; ++i)
        {
            auto& s = sliders[i];
            s.setSliderStyle (juce::Slider::LinearVertical);
            s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            s.setRange (0.0, 1.0, 0.0);
            s.setDoubleClickReturnValue (true, 0.5);
            s.setPopupDisplayEnabled (true, true, nullptr);
            s.textFromValueFunction = dbText;
            s.setColour (juce::Slider::trackColourId, theme::accent);
            s.setColour (juce::Slider::backgroundColourId, theme::panelRaised);
            s.setColour (juce::Slider::thumbColourId, theme::text);
            const ParamTarget tgt { ParamKind::EqBand, kInvalidId, i };
            s.onDragStart = [this, tgt] { controller.beginParamGesture (trackId, tgt); };
            s.onDragEnd = [this, tgt] { controller.endParamGesture (trackId, tgt); };
            s.onValueChange = [this, i, tgt]
            {
                values[i] = (float) sliders[i].getValue();
                controller.setParam (trackId, tgt, values[i]);
                rebuildCurve();
                repaint();
            };
            addAndMakeVisible (s);

            auto& l = labels[i];
            l.setText (kBandNames[i], juce::dontSendNotification);
            l.setJustificationType (juce::Justification::centred);
            l.setFont (theme::font (13.0f));
            l.setColour (juce::Label::textColourId, theme::textDim);
            l.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (l);
        }
        flatButton.onClick = [this] { resetFlat(); };
        flatButton.setTooltip ("Reset all bands to 0 dB");
        addAndMakeVisible (flatButton);
        enableButton.setClickingTogglesState (true);
        enableButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.3f));
        enableButton.onClick = [this] { setEnabledState (enableButton.getToggleState()); };
        addAndMakeVisible (enableButton);
        startTimerHz (15);
    }
    refresh();
}

GraphicEqEditor::~GraphicEqEditor() { stopTimer(); }

void GraphicEqEditor::setEnabledState (bool on)
{
    eqEnabled = on;
    if (auto t = controller.session().findTrack (trackId); t && t->rt)
        t->rt->eqEnabled.store (on);
    repaint();
}

void GraphicEqEditor::resetFlat()
{
    for (int i = 0; i < kEqBands; ++i)
    {
        const ParamTarget tgt { ParamKind::EqBand, kInvalidId, i };
        controller.beginParamGesture (trackId, tgt);
        controller.setParam (trackId, tgt, 0.5f);
        controller.endParamGesture (trackId, tgt);
        values[i] = 0.5f;
        sliders[i].setValue (0.5, juce::dontSendNotification);
    }
    rebuildCurve();
    repaint();
}

void GraphicEqEditor::refresh()
{
    bool changed = false;
    for (int i = 0; i < kEqBands; ++i)
    {
        const float v = controller.getParam (trackId, { ParamKind::EqBand, kInvalidId, i });
        if (std::abs (v - values[i]) > 1.0e-4f)
        {
            values[i] = v;
            changed = true;
        }
        if (! compact && ! sliders[i].isMouseButtonDown() && std::abs ((float) sliders[i].getValue() - v) > 1.0e-4f)
            sliders[i].setValue (v, juce::dontSendNotification);
    }
    bool en = eqEnabled;
    if (auto t = controller.session().findTrack (trackId); t && t->rt)
        en = t->rt->eqEnabled.load();
    if (en != eqEnabled) { eqEnabled = en; changed = true; }
    if (! compact) enableButton.setToggleState (eqEnabled, juce::dontSendNotification);
    if (changed) { rebuildCurve(); repaint(); }
}

float GraphicEqEditor::responseDb (float freq) const noexcept
{
    constexpr float k = 1.0f / (2.0f * 0.55f * 0.55f);
    float sum = 0.0f;
    for (int i = 0; i < kEqBands; ++i)
    {
        const float o = std::log2 (freq / GraphicEq::kFrequencies[i]);
        sum += pm::eqToDb (values[i]) * std::exp (-o * o * k);
    }
    return sum;
}

void GraphicEqEditor::rebuildCurve()
{
    curveLine.clear();
    curveFill.clear();
    if (curveArea.isEmpty()) return;
    const float w = curveArea.getWidth(), h = curveArea.getHeight();
    const float mid = curveArea.getCentreY();
    bool first = true;
    for (float x = 0.0f; x <= w + 0.01f; x += 2.0f)
    {
        const float f = 20.0f * std::pow (1000.0f, x / w);
        const float db = juce::jlimit (-kCurveRangeDb, kCurveRangeDb, responseDb (f));
        const float y = mid - db / kCurveRangeDb * (h * 0.5f);
        if (first) { curveLine.startNewSubPath (curveArea.getX() + x, y); first = false; }
        else curveLine.lineTo (curveArea.getX() + x, y);
    }
    curveFill = curveLine;
    curveFill.lineTo (curveArea.getRight(), mid);
    curveFill.lineTo (curveArea.getX(), mid);
    curveFill.closeSubPath();
}

void GraphicEqEditor::resized()
{
    auto b = getLocalBounds();
    if (compact)
    {
        curveArea = b.toFloat().reduced (1.0f);
    }
    else
    {
        b.reduce (8, 8);
        auto top = b.removeFromTop (32);
        flatButton.setBounds (top.removeFromRight (72));
        top.removeFromRight (6);
        enableButton.setBounds (top.removeFromRight (84));
        b.removeFromTop (6);
        curveArea = b.removeFromTop (100).toFloat();
        b.removeFromTop (6);
        auto labelRow = b.removeFromBottom (20);
        const int cw = b.getWidth() / kEqBands;
        for (int i = 0; i < kEqBands; ++i)
        {
            sliders[i].setBounds (b.getX() + i * cw, b.getY(), cw, b.getHeight());
            labels[i].setBounds (labelRow.getX() + i * cw, labelRow.getY(), cw, labelRow.getHeight());
        }
    }
    rebuildCurve();
}

void GraphicEqEditor::paint (juce::Graphics& g)
{
    if (compact) g.setColour (theme::panelRaised);
    else g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), compact ? 3.0f : theme::cornerRadius);

    g.setColour (theme::background);
    g.fillRect (curveArea);
    g.setColour (theme::outline);
    g.drawHorizontalLine ((int) curveArea.getCentreY(), curveArea.getX(), curveArea.getRight());
    if (! compact)
    {
        for (int i = 0; i < kEqBands; ++i)
        {
            const float x = curveArea.getX() + curveArea.getWidth() * std::log (GraphicEq::kFrequencies[i] / 20.0f) / std::log (1000.0f);
            g.setColour (theme::outline.withAlpha (0.5f));
            g.drawVerticalLine ((int) x, curveArea.getY(), curveArea.getBottom());
        }
    }
    const auto col = eqEnabled ? theme::accent : theme::textDim;
    g.setColour (col.withAlpha (0.18f));
    g.fillPath (curveFill);
    g.setColour (col);
    g.strokePath (curveLine, juce::PathStrokeType (compact ? 1.2f : 1.8f));
    if (compact)
    {
        g.setColour (theme::outline);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
    }
}

void GraphicEqEditor::mouseUp (const juce::MouseEvent& e)
{
    if (compact && onClick && ! e.mouseWasDraggedSinceMouseDown() && getLocalBounds().contains (e.getPosition()))
        onClick();
}

} // namespace el
