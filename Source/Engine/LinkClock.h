#pragma once
// Ableton Link wrapper. The Link session state is ALWAYS the clock (Link just isn't
// networked while disabled), so tempo, start/stop and bar phase behave identically.
#include <JuceHeader.h>
#include "Core/TimeMath.h"
#include <memory>

namespace el {

class LinkClock {
public:
    LinkClock (double initialTempo = 120.0);
    ~LinkClock();

    // ---- message thread ----------------------------------------------------------------
    void setEnabled (bool on);
    bool isEnabled() const;
    void setStartStopSync (bool on);
    bool isStartStopSyncEnabled() const;
    void setTempo (double bpm);
    double tempo() const;
    void requestPlay (bool shouldPlay);
    int numPeers() const;
    void setQuantum (double beatsPerBar);   // bar length in beats, used for phase alignment

    // ---- audio thread ------------------------------------------------------------------
    // Computes musical time for the block about to be rendered. `sampleTime` is a running
    // sample counter; `outputLatencySamples` lets the block line up with what is heard.
    // Also applies transport requests made from the audio thread (startFromAudio).
    BlockTime beginBlock (int64_t sampleTime, int numSamples, double sampleRate, int outputLatencySamples,
                          double beatsPerBar, double masterBeats) noexcept;
    void startFromAudio() noexcept;          // e.g. a loop was triggered while stopped
    void stopFromAudio() noexcept;
    void locateFromAudio (double beat) noexcept;   // timeline mode jump

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace el
