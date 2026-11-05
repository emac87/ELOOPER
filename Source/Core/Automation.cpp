#include "Automation.h"
#include <algorithm>
#include <cmath>

namespace el {

float applyCurve (float t, float curve) noexcept {
    if (std::abs (curve) < 1.0e-3f) return t;
    // Exponential-ish bend; curve > 0 bows upward (fast start), curve < 0 bows downward.
    const float k = curve * 6.0f;
    return (1.0f - std::exp (-k * t)) / (1.0f - std::exp (-k));
}

bool AutomationLane::evaluate (double beat, float& out) const noexcept {
    if (points.empty()) return false;
    if (beat <= points.front().beat) { out = points.front().value; return true; }
    if (beat >= points.back().beat)  { out = points.back().value;  return true; }
    auto it = std::upper_bound (points.begin(), points.end(), beat,
                                [] (double b, const AutoPoint& p) { return b < p.beat; });
    const AutoPoint& b = *it;
    const AutoPoint& a = *(it - 1);
    const double span = b.beat - a.beat;
    const float t = span > 0 ? (float) ((beat - a.beat) / span) : 1.0f;
    out = a.value + (b.value - a.value) * applyCurve (t, a.curve);
    return true;
}

void AutomationLane::setPoint (double beat, float value, double mergeWindow) {
    value = std::clamp (value, 0.0f, 1.0f);
    auto it = std::lower_bound (points.begin(), points.end(), beat - mergeWindow,
                                [] (const AutoPoint& p, double b) { return p.beat < b; });
    if (it != points.end() && std::abs (it->beat - beat) <= mergeWindow) { it->value = value; return; }
    points.insert (it, AutoPoint { beat, value, 0.0f });
}

void AutomationLane::erase (double from, double to) {
    points.erase (std::remove_if (points.begin(), points.end(),
                                  [=] (const AutoPoint& p) { return p.beat >= from && p.beat < to; }),
                  points.end());
}

void AutomationLane::sort() {
    std::stable_sort (points.begin(), points.end(), [] (const AutoPoint& a, const AutoPoint& b) { return a.beat < b.beat; });
}

} // namespace el
