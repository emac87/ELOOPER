#pragma once
#include "Types.h"
#include <cmath>

namespace el {

// Musical time for one audio block. Beats are quarter notes from transport start.
struct BlockTime {
    double beatStart = 0;        // musical beat at the first sample of the block
    double beatsPerSample = 0;   // 0 when the transport is stopped
    double sampleRate = 48000;
    double tempo = 120;
    double beatsPerBar = 4;
    double masterBeats = 16;     // master loop length in beats
    bool   playing = false;
    int    numSamples = 0;
    double beatAt (int sample) const noexcept { return beatStart + beatsPerSample * sample; }
    double beatEnd() const noexcept { return beatAt (numSamples); }
};

inline double quantizeBeats (Quantize q, double beatsPerBar, double masterBeats) noexcept {
    switch (q) {
        case Quantize::None:     return 0;
        case Quantize::Beat:     return 1;
        case Quantize::Bar:      return beatsPerBar;
        case Quantize::TwoBars:  return beatsPerBar * 2;
        case Quantize::FourBars: return beatsPerBar * 4;
        case Quantize::Master:   return masterBeats;
    }
    return 0;
}

// Next grid line at or after `beat` (grid starts at `origin`). Tolerance avoids skipping a
// boundary we are sitting on because of rounding.
inline double nextBoundary (double beat, double grid, double origin = 0, double tolerance = 1e-6) noexcept {
    if (grid <= 0) return beat;
    const double k = std::ceil ((beat - origin) / grid - tolerance);
    return origin + k * grid;
}

// Sample offset inside a block at which `beat` falls, clamped to [0, numSamples].
inline int sampleOffsetForBeat (const BlockTime& t, double beat) noexcept {
    if (t.beatsPerSample <= 0) return beat <= t.beatStart ? 0 : t.numSamples;
    const double s = std::ceil ((beat - t.beatStart) / t.beatsPerSample - 1e-9);
    if (s <= 0) return 0;
    if (s >= t.numSamples) return t.numSamples;
    return (int) s;
}

inline double positiveMod (double x, double m) noexcept {
    if (m <= 0) return 0;
    double r = std::fmod (x, m);
    return r < 0 ? r + m : r;
}

} // namespace el
