#pragma once
#include "Types.h"
#include <vector>

namespace el {

struct AutoPoint {
    double beat = 0;      // position in beats from the lane origin (loop start / timeline start)
    float  value = 0;     // normalised 0..1
    float  curve = 0;     // -1..1 bend of the segment that starts at this point (0 = linear)
};

struct AutomationLane {
    ParamTarget target;
    std::vector<AutoPoint> points;   // sorted by beat
    bool enabled = true;

    // Value at `beat`. Before the first / after the last point the value is held.
    // Returns false if the lane has no points.
    bool evaluate (double beat, float& out) const noexcept;
    // Insert or replace a point (keeps the list sorted). Points within `mergeWindow` beats are replaced.
    void setPoint (double beat, float value, double mergeWindow = 1.0e-4);
    // Remove points in [from, to).
    void erase (double from, double to);
    void sort();
};

// The immutable set of lanes the audio thread reads for one owner (a track or a loop).
struct AutomationSet {
    std::vector<AutomationLane> lanes;
    double lengthBeats = 0;  // loop length for loop layers (wraps); 0 for timeline lanes
};

float applyCurve (float t, float curve) noexcept;

} // namespace el
