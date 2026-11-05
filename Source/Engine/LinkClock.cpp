#include "LinkClock.h"
#include <ableton/Link.hpp>
#include <ableton/link/HostTimeFilter.hpp>
#include <atomic>
#include <cmath>

namespace el {

struct LinkClock::Impl {
    explicit Impl (double bpm) : link (bpm) {}
    ableton::Link link;
    ableton::link::HostTimeFilter<ableton::link::platform::Clock> hostTime;
    std::atomic<double> quantum { 4.0 };
    std::atomic<bool> startRequest { false }, stopRequest { false };
    std::atomic<double> locateRequest { -1.0 };
    // Audio thread only.
    double origin = 0;       // Link beat that corresponds to musical beat 0
    double startOffset = 0;  // musical beat at which the next start begins (timeline locate)
    bool wasPlaying = false;
    double lastSampleRate = 0;
    std::atomic<bool> offline { false };
    std::chrono::microseconds offlineBase { 0 };
};

LinkClock::LinkClock (double tempo) : impl (std::make_unique<Impl> (tempo)) {}
LinkClock::~LinkClock() { impl->link.enable (false); }

void LinkClock::setEnabled (bool on) { impl->link.enable (on); }
bool LinkClock::isEnabled() const { return impl->link.isEnabled(); }
void LinkClock::setStartStopSync (bool on) { impl->link.enableStartStopSync (on); }
bool LinkClock::isStartStopSyncEnabled() const { return impl->link.isStartStopSyncEnabled(); }
int LinkClock::numPeers() const { return (int) impl->link.numPeers(); }
void LinkClock::setOffline (bool on) { impl->offlineBase = impl->link.clock().micros(); impl->offline = on; }
void LinkClock::setQuantum (double q) { impl->quantum = std::max (1.0, q); }

void LinkClock::setTempo (double bpm) {
    auto s = impl->link.captureAppSessionState();
    s.setTempo (std::clamp (bpm, 20.0, 999.0), impl->link.clock().micros());
    impl->link.commitAppSessionState (s);
}

double LinkClock::tempo() const { return impl->link.captureAppSessionState().tempo(); }

void LinkClock::requestPlay (bool shouldPlay) {
    if (shouldPlay) impl->startRequest = true; else impl->stopRequest = true;
}

void LinkClock::startFromAudio() noexcept { impl->startRequest.store (true, std::memory_order_relaxed); }
void LinkClock::stopFromAudio() noexcept  { impl->stopRequest.store (true, std::memory_order_relaxed); }
void LinkClock::locateFromAudio (double beat) noexcept { impl->locateRequest.store (std::max (0.0, beat), std::memory_order_relaxed); }

BlockTime LinkClock::beginBlock (int64_t sampleTime, int numSamples, double sampleRate, int outputLatencySamples,
                                 double beatsPerBar, double masterBeats) noexcept {
    auto& d = *impl;
    if (sampleRate != d.lastSampleRate) { d.hostTime.reset(); d.lastSampleRate = sampleRate; }
    const double q = d.quantum.load (std::memory_order_relaxed);
    // Host time at which the first sample of this block will be HEARD.
    const auto bufferTime = d.offline.load (std::memory_order_relaxed)
        ? d.offlineBase + std::chrono::microseconds (llround (1.0e6 * (double) sampleTime / sampleRate))
        : d.hostTime.sampleTimeToHostTime ((double) sampleTime);
    const auto latency = std::chrono::microseconds (llround (1.0e6 * outputLatencySamples / sampleRate));
    const auto t = bufferTime + latency;

    auto state = d.link.captureAudioSessionState();
    bool commit = false;
    if (d.startRequest.exchange (false, std::memory_order_relaxed) && ! state.isPlaying()) {
        // Alone: beat 0 lands now. With peers: beat 0 lands on the next shared downbeat.
        state.setIsPlayingAndRequestBeatAtTime (true, t, 0.0, q);
        commit = true;
    }
    if (d.stopRequest.exchange (false, std::memory_order_relaxed) && state.isPlaying()) {
        state.setIsPlaying (false, t);
        commit = true;
    }
    if (commit) d.link.commitAudioSessionState (state);

    BlockTime bt;
    bt.sampleRate = sampleRate;
    bt.numSamples = numSamples;
    bt.tempo = state.tempo();
    bt.beatsPerBar = beatsPerBar;
    bt.masterBeats = masterBeats;

    const bool linkPlaying = state.isPlaying();
    if (linkPlaying && ! d.wasPlaying) {
        // Musical beat 0 = the first quantum boundary at/after the start time.
        const double b = state.beatAtTime (state.timeForIsPlaying(), q);
        d.origin = std::ceil (b / q - 1.0e-6) * q - d.startOffset;
    }
    d.wasPlaying = linkPlaying;

    const double loc = d.locateRequest.exchange (-1.0, std::memory_order_relaxed);
    if (loc >= 0 && ! linkPlaying) d.startOffset = loc;
    if (loc >= 0 && linkPlaying) {
        double newOrigin = state.beatAtTime (t, q) - loc;
        if (d.link.isEnabled() && d.link.numPeers() > 0)
            newOrigin = std::round (newOrigin / q) * q;   // keep bar phase shared with peers
        d.origin = newOrigin;
    }

    const double beat = state.beatAtTime (t, q) - d.origin;
    bt.playing = linkPlaying && beat >= -1.0e-9;          // before the shared downbeat: count-in silence
    bt.beatStart = bt.playing ? std::max (0.0, beat) : 0.0;
    bt.beatsPerSample = bt.playing ? bt.tempo / 60.0 / sampleRate : 0.0;
    return bt;
}

} // namespace el
