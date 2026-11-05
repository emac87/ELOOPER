#include "UI/LevelMeter.h"
#include "UI/Theme.h"
#include <cmath>

namespace el {

namespace {
    constexpr float kFallDbPerTick = 1.4f;   // ~42 dB/s at 30 Hz
    constexpr int   kHoldTicks = 45;         // 1.5 s
    constexpr float kHoldFallDbPerTick = 0.8f;
}

LevelMeter::LevelMeter() { setOpaque (false); setInterceptsMouseClicks (true, false); }

void LevelMeter::setLevels (float left, float right)
{
    const float in[2] = { left, right };
    bool changed = false;
    for (int i = 0; i < 2; ++i)
    {
        auto& c = ch[i];
        const float db = in[i] > 1.0e-4f ? juce::jlimit (-90.0f, 24.0f, 20.0f * std::log10 (in[i])) : -90.0f;
        const float oldDb = c.db, oldHold = c.hold;
        c.db = db >= c.db ? db : std::max (db, c.db - kFallDbPerTick);
        if (c.db >= c.hold) { c.hold = c.db; c.holdAge = 0; }
        else if (++c.holdAge > kHoldTicks) c.hold = std::max (c.db, c.hold - kHoldFallDbPerTick);
        if (std::abs (c.db - oldDb) > 0.05f || std::abs (c.hold - oldHold) > 0.05f) changed = true;
    }
    if (changed) repaint();
}

void LevelMeter::resetPeaks()
{
    for (auto& c : ch) { c.hold = c.db; c.holdAge = 0; }
    repaint();
}

void LevelMeter::resized()
{
    const float h = (float) getHeight();
    gradient = juce::ColourGradient (theme::meterHigh, 0, 0, theme::meterLow, 0, h, false);
    auto pos = [] (float db) { return juce::jlimit (0.0f, 1.0f, (kMaxDb - db) / (kMaxDb - kMinDb)); };
    gradient.addColour (pos (0.0f), theme::meterHigh);
    gradient.addColour (pos (-3.0f), theme::meterMid);
    gradient.addColour (pos (-12.0f), theme::meterMid.interpolatedWith (theme::meterLow, 0.5f));
    gradient.addColour (pos (-24.0f), theme::meterLow);
}

float LevelMeter::yFor (float db, float top, float height) const noexcept
{
    const float n = juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
    return top + height * (1.0f - n);
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float gap = 1.0f;
    const float w = (b.getWidth() - gap) * 0.5f;
    for (int i = 0; i < 2; ++i)
    {
        const float x = b.getX() + (float) i * (w + gap);
        g.setColour (theme::panelRaised);
        g.fillRect (x, b.getY(), w, b.getHeight());
        const float yTop = yFor (ch[i].db, b.getY(), b.getHeight());
        g.setGradientFill (gradient);
        g.fillRect (x, yTop, w, b.getBottom() - yTop);
        if (ch[i].hold > kMinDb)
        {
            const float yh = yFor (ch[i].hold, b.getY(), b.getHeight());
            g.setColour (ch[i].hold > 0.0f ? theme::meterHigh : theme::text);
            g.fillRect (x, yh, w, 1.5f);
        }
    }
    const float y0 = yFor (0.0f, b.getY(), b.getHeight());
    g.setColour (theme::textDim.withAlpha (0.6f));
    g.fillRect (b.getX(), y0, b.getWidth(), 1.0f);
}

} // namespace el
