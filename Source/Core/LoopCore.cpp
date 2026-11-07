#include "LoopCore.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace el {

namespace {
    constexpr float kEnvStep = 1.0f / 96.0f;   // ~2 ms fade at 48 kHz
    constexpr double kMinLoopBeats = 1.0 / 16.0;
    constexpr double kInf = std::numeric_limits<double>::infinity();
}

// ------------------------------------------------------------------------------------------
bool TakeAudio::ensure (int64_t frame, ChunkPool& pool) noexcept {
    if (frame < 0) return false;
    const int64_t idx = frame >> kChunkShift;
    if (idx >= kMaxChunksPerTake) return false;
    if (chunks[idx] != nullptr) return true;
    // Chunks are filled in order; allocate any gap too.
    for (int64_t i = numChunks; i <= idx; ++i) {
        if (chunks[i] != nullptr) continue;
        auto* c = pool.pop();
        if (c == nullptr) return false;
        chunks[i] = c;
        numChunks = (int) i + 1;
    }
    return true;
}

void TakeAudio::release (ChunkPool& pool) noexcept {
    for (int i = 0; i < numChunks; ++i) { pool.push (chunks[i]); chunks[i] = nullptr; }
    numChunks = 0; lengthFrames = 0; lengthBeats = 0; hasContent = false;
}

// ------------------------------------------------------------------------------------------
LoopRt::LoopRt (Id loopId, Id track, LoopType t) : id (loopId), trackId (track), type (t) {}

double LoopRt::lengthBeats() const noexcept {
    if (type == LoopType::Audio) return numTakes > 0 ? takes[activeTake].lengthBeats : 0.0;
    return numTakes > 0 ? takes[activeTake].lengthBeats : 0.0; // MIDI / automation store beats only
}

double LoopRt::phaseBeatsAt (double beat) const noexcept {
    if (state == LoopState::Recording) return std::max (0.0, beat - anchorBeat);
    const double L = lengthBeats();
    return L > 0 ? positiveMod (beat - anchorBeat, L) : 0.0;
}

double LoopRt::quantizeGrid (Quantize q, const BlockTime& t) const noexcept {
    return quantizeBeats (q, t.beatsPerBar, t.masterBeats);
}

void LoopRt::post (const LoopContext& ctx, EngineEventType ty, int takeIdx, double beat) noexcept {
    if (ctx.events == nullptr) return;
    EngineEvent e; e.type = ty; e.loopId = id; e.trackId = trackId; e.take = takeIdx; e.beat = beat;
    ctx.events->push (e);
}

// ------------------------------------------------------------------------------------------
void LoopRt::command (const LoopCommand& c, const BlockTime& t, const LoopContext& ctx) noexcept {
    const double now = t.beatStart;
    const double startGrid = quantizeGrid (settings.startQuantize, t);
    const double stopGrid  = quantizeGrid (settings.stopQuantize, t);
    auto startAt = [&] { return nextBoundary (now, startGrid); };
    // Stopping a playing loop at "Master" means at the end of its own cycle.
    auto stopAt = [&] {
        if (settings.stopQuantize == Quantize::Master && lengthBeats() > 0) return nextBoundary (now, lengthBeats(), anchorBeat);
        return nextBoundary (now, stopGrid);
    };
    auto closeAt = [&] { return stopGrid > 0 ? nextBoundary (std::max (now, anchorBeat + kMinLoopBeats), stopGrid, anchorBeat) : now; };

    switch (c.type) {
        case LoopCommandType::Trigger:
            if (pending != PendingAction::None) { pending = PendingAction::None; break; }  // cancel armed action
            switch (state) {
                case LoopState::Empty:       schedule (PendingAction::Record, startAt()); break;
                case LoopState::Recording:   schedule (PendingAction::CloseRecord, closeAt()); break;
                case LoopState::Playing:     schedule (PendingAction::Stop, stopAt()); break;
                case LoopState::Overdubbing: schedule (PendingAction::EndOverdub, now); break;
                case LoopState::Stopped:     schedule (PendingAction::Play, startAt()); break;
            }
            break;
        case LoopCommandType::Record:
            if (state == LoopState::Recording) {
                if (settings.cycleTakes && settings.recordMode == RecordMode::TakeFolder) closeAtCycleEnd = true;
                else schedule (PendingAction::CloseRecord, closeAt());
            } else {
                schedule (PendingAction::Record, startAt());
                pendingLength = 0;
            }
            break;
        case LoopCommandType::RecordAt:
            schedule (PendingAction::Record, std::max (c.beat, now));
            pendingLength = c.lengthBeats;
            break;
        case LoopCommandType::Play:
            if (state == LoopState::Overdubbing) schedule (PendingAction::EndOverdub, now);
            else if (state == LoopState::Stopped) schedule (PendingAction::Play, startAt());
            else if (state == LoopState::Recording) schedule (PendingAction::CloseRecord, closeAt());
            break;
        case LoopCommandType::Stop:
            if (state == LoopState::Recording) { abortRecording (ctx); break; }
            if (state == LoopState::Playing || state == LoopState::Overdubbing) schedule (PendingAction::Stop, stopAt());
            else pending = PendingAction::None;
            break;
        case LoopCommandType::Overdub:
            if (state == LoopState::Playing) schedule (PendingAction::Overdub, now);
            else if (state == LoopState::Overdubbing) schedule (PendingAction::EndOverdub, now);
            else if (state == LoopState::Stopped) schedule (PendingAction::Overdub, startAt());
            break;
        case LoopCommandType::Clear:
            if (state == LoopState::Recording) abortRecording (ctx);
            for (int i = 0; i < numTakes; ++i) takes[i].release (*ctx.pool);
            numTakes = 0; activeTake = 0; tailTake = -1; tailRemaining = 0;
            state = LoopState::Empty; pending = PendingAction::None; env = 0;
            flushNotes = true;
            break;
        case LoopCommandType::SelectTake:
            if (c.take >= 0 && c.take < numTakes && state != LoopState::Recording) activeTake = c.take;
            break;
        case LoopCommandType::DeleteTake:
            deleteTake (c.take, ctx);
            break;
        case LoopCommandType::SetSettings:
            settings = c.settings;
            break;
        case LoopCommandType::DefineTake:
            if (type == LoopType::Audio || c.take < 0 || c.take >= kMaxTakes || c.lengthBeats <= 0) break;
            if (state == LoopState::Recording && c.take == recordTake) break;
            takes[c.take].lengthBeats = c.lengthBeats;
            takes[c.take].hasContent = true;
            numTakes = std::max (numTakes, c.take + 1);
            if (state == LoopState::Empty) state = LoopState::Stopped;
            break;
        case LoopCommandType::PlayAt:
            if (numTakes == 0 || state == LoopState::Recording) break;
            if (state != LoopState::Playing && state != LoopState::Overdubbing) env = 0;
            if (state != LoopState::Overdubbing) state = LoopState::Playing;
            anchorBeat = c.beat;
            pending = PendingAction::None;
            break;
        case LoopCommandType::StopNow:
            if (state == LoopState::Recording) { abortRecording (ctx); break; }
            if (state == LoopState::Playing || state == LoopState::Overdubbing) state = LoopState::Stopped;
            pending = PendingAction::None;
            flushNotes = true;
            break;
    }
    publish (now);
}

void LoopRt::deleteTake (int index, const LoopContext& ctx) noexcept {
    if (index < 0 || index >= numTakes || state == LoopState::Recording) return;
    takes[index].release (*ctx.pool);
    for (int i = index; i < numTakes - 1; ++i) {
        std::memcpy (&takes[i], &takes[i + 1], sizeof (TakeAudio));
    }
    std::memset (&takes[numTakes - 1], 0, sizeof (TakeAudio));
    --numTakes;
    if (tailTake == index) { tailTake = -1; tailRemaining = 0; }
    else if (tailTake > index) --tailTake;
    if (activeTake >= numTakes) activeTake = std::max (0, numTakes - 1);
    else if (activeTake > index) --activeTake;
    if (numTakes == 0) { state = LoopState::Empty; pending = PendingAction::None; }
    post (ctx, EngineEventType::TakeDeleted, index);
}

// ------------------------------------------------------------------------------------------
void LoopRt::startRecording (double beat, double targetBeats, const BlockTime& t, const LoopContext& ctx) noexcept {
    int take = activeTake;
    if (settings.recordMode == RecordMode::TakeFolder && numTakes > 0) {
        take = numTakes < kMaxTakes ? numTakes : kMaxTakes - 1;
    }
    if (numTakes == 0) take = 0;
    if (tailTake == take) { tailTake = -1; tailRemaining = 0; }
    takes[take].release (*ctx.pool);
    if (take >= numTakes) numTakes = take + 1;
    activeTake = take;
    recordTake = take;
    anchorBeat = beat;
    recFrames = 0;
    recordLatency = ctx.recordLatency;
    takes[take].sampleRate = t.sampleRate;
    if (targetBeats > 0) recordTargetBeats = targetBeats;
    else if (settings.lengthMode == RecordLength::Master) recordTargetBeats = t.masterBeats;
    else if (settings.lengthMode == RecordLength::Bars) recordTargetBeats = std::max<int> (1, settings.recordBars) * t.beatsPerBar;
    else recordTargetBeats = 0;
    state = LoopState::Recording;
    env = 0;
    post (ctx, EngineEventType::RecordStarted, take);
}

void LoopRt::abortRecording (const LoopContext& ctx) noexcept {
    takes[recordTake].release (*ctx.pool);
    post (ctx, EngineEventType::RecordAborted, recordTake);
    // Drop the empty take slot if it was the last one.
    if (recordTake == numTakes - 1) --numTakes;
    activeTake = std::max (0, std::min (activeTake, numTakes - 1));
    if (recordTake == activeTake && numTakes > 0) activeTake = numTakes - 1;
    state = numTakes > 0 ? LoopState::Stopped : LoopState::Empty;
    pending = PendingAction::None;
    closeAtCycleEnd = false;
}

void LoopRt::finishRecording (double endBeat, bool userClosed, const BlockTime& t, const LoopContext& ctx) noexcept {
    const double lenBeats = endBeat - anchorBeat;
    if (lenBeats < kMinLoopBeats) { abortRecording (ctx); return; }
    auto& tk = takes[recordTake];
    tk.lengthBeats = lenBeats;
    tk.lengthFrames = std::max<int64_t> (1, recFrames);
    tk.hasContent = true;
    if (type == LoopType::Audio) {
        // The last `recordLatency` frames of the loop arrive after the boundary.
        tk.ensure (tk.lengthFrames - 1, *ctx.pool);
        tailTake = recordTake;
        tailIndex = std::max<int64_t> (0, tk.lengthFrames - recordLatency);
        tailRemaining = (int) std::min<int64_t> (recordLatency, tk.lengthFrames);
    }
    post (ctx, EngineEventType::RecordFinished, recordTake, lenBeats);

    const bool cycling = settings.recordMode == RecordMode::TakeFolder && settings.cycleTakes && ! userClosed && ! closeAtCycleEnd;
    if (cycling && numTakes < kMaxTakes) {
        // Seamlessly start the next take; the previous take keeps receiving its tail.
        const int keepTail = tailTake, keepRemaining = tailRemaining; const int64_t keepIndex = tailIndex;
        startRecording (endBeat, lenBeats, t, ctx);
        tailTake = keepTail; tailRemaining = keepRemaining; tailIndex = keepIndex;
        return;
    }
    closeAtCycleEnd = false;
    activeTake = recordTake;
    anchorBeat = endBeat;  // phase 0 at the moment recording ended
    switch (settings.afterRecord) {
        case AfterRecord::Play:    state = LoopState::Playing; env = 1; lastGain = muted.load() ? 0.0f : volume.load(); break;
        case AfterRecord::Overdub: state = LoopState::Overdubbing; env = 1; lastGain = muted.load() ? 0.0f : volume.load(); break;
        case AfterRecord::Stop:    state = LoopState::Stopped; break;
    }
}

// ------------------------------------------------------------------------------------------
void LoopRt::applyPending (double beat, int sampleInBlock, const BlockTime& t, const LoopContext& ctx, LoopIo& io) noexcept {
    const auto a = pending;
    pending = PendingAction::None;
    switch (a) {
        case PendingAction::None: break;
        case PendingAction::Record:
            if (state == LoopState::Playing || state == LoopState::Overdubbing) allNotesOff (io.midiOut, sampleInBlock);
            startRecording (beat, pendingLength, t, ctx);
            pendingLength = 0;
            break;
        case PendingAction::CloseRecord:
            if (state == LoopState::Recording) finishRecording (beat, true, t, ctx);
            break;
        case PendingAction::Play:
            if (numTakes > 0) { state = LoopState::Playing; anchorBeat = beat; env = 0; }
            break;
        case PendingAction::Stop:
            if (state == LoopState::Playing || state == LoopState::Overdubbing) state = LoopState::Stopped;
            allNotesOff (io.midiOut, sampleInBlock);
            break;
        case PendingAction::Overdub:
            if (numTakes == 0) break;
            if (state == LoopState::Stopped) { anchorBeat = beat; env = 0; }
            state = LoopState::Overdubbing;
            break;
        case PendingAction::EndOverdub:
            if (state == LoopState::Overdubbing) state = LoopState::Playing;
            break;
    }
}

void LoopRt::process (const BlockTime& t, LoopIo& io, const LoopContext& ctx) noexcept {
    if (flushNotes && io.midiOut != nullptr) { allNotesOff (io.midiOut, 0); flushNotes = false; }
    if (! t.playing || t.numSamples <= 0) { publish (t.beatStart); return; }

    int pos = 0;
    for (int guard = 0; guard < 16 && pos <= t.numSamples; ++guard) {
        // Earliest event in this block.
        double evBeat = kInf; int which = 0;
        if (pending != PendingAction::None) { evBeat = pendingBeat; which = 1; }
        if (state == LoopState::Recording && recordTargetBeats > 0 && anchorBeat + recordTargetBeats < evBeat) {
            evBeat = anchorBeat + recordTargetBeats; which = 2;
        }
        if (state == LoopState::Playing && settings.playMode == PlayMode::OneShot && lengthBeats() > 0) {
            const double end = anchorBeat + lengthBeats();
            if (end < evBeat) { evBeat = end; which = 3; }
        }
        int end = t.numSamples;
        if (which != 0) end = std::max (pos, sampleOffsetForBeat (t, evBeat));
        if (end > pos) renderSegment (pos, end, t, io, ctx);
        pos = end;
        if (which == 0 || end >= t.numSamples) break;
        if (which == 1) applyPending (evBeat, end, t, ctx, io);
        else if (which == 2) finishRecording (anchorBeat + recordTargetBeats, false, t, ctx);
        else if (which == 3) { state = LoopState::Stopped; allNotesOff (io.midiOut, end); }
    }
    publish (t.beatEnd());
}

void LoopRt::renderSegment (int from, int to, const BlockTime& t, LoopIo& io, const LoopContext& ctx) noexcept {
    if (type == LoopType::Audio && tailRemaining > 0) writeTail (from, to, io);
    switch (state) {
        case LoopState::Recording:
            if (type == LoopType::Audio) {
                const int stopAt = recordAudio (from, to, io, ctx);
                if (stopAt >= 0) {   // out of memory / max length: close with what we have
                    recFrames += (stopAt - from);
                    recordLatency = 0;
                    finishRecording (t.beatAt (stopAt), true, t, ctx);
                    break;
                }
            } else if (type == LoopType::Midi) recordMidi (from, to, t, io, ctx, anchorBeat);
            recFrames += (to - from);
            break;
        case LoopState::Playing:
        case LoopState::Overdubbing: {
            const bool od = state == LoopState::Overdubbing;
            if (type == LoopType::Audio) playAudio (from, to, t, io, od, ctx);
            else if (type == LoopType::Midi) {
                if (! muted.load (std::memory_order_relaxed)) playMidi (from, to, t, io);
                if (od) recordMidi (from, to, t, io, ctx, anchorBeat);
            }
            break;
        }
        case LoopState::Stopped:
            if (type == LoopType::Audio && env > 0) playAudio (from, to, t, io, false, ctx); // fade out
            break;
        case LoopState::Empty: break;
    }
}

int LoopRt::recordAudio (int from, int to, LoopIo& io, const LoopContext& ctx) noexcept {
    auto& tk = takes[recordTake];
    for (int i = from; i < to; ++i) {
        const int64_t dst = recFrames + (i - from) - recordLatency;
        if (dst < 0) continue;
        if (tk.chunks[dst >> kChunkShift] == nullptr && ! tk.ensure (dst, *ctx.pool)) {
            post (ctx, EngineEventType::OutOfMemory, recordTake);
            return i;   // caller closes the loop here
        }
        float* l = tk.writePtr (0, dst); float* r = tk.writePtr (1, dst);
        *l = io.in[0] != nullptr ? io.in[0][i] : 0.0f;
        *r = io.in[1] != nullptr ? io.in[1][i] : *l;
    }
    return -1;
}

void LoopRt::writeTail (int from, int to, LoopIo& io) noexcept {
    if (tailTake < 0 || tailRemaining <= 0) return;
    auto& tk = takes[tailTake];
    const int n = std::min (to - from, tailRemaining);
    for (int i = 0; i < n; ++i) {
        const int64_t dst = tailIndex + i;
        if (dst >= tk.lengthFrames) break;
        float* l = tk.writePtr (0, dst); float* r = tk.writePtr (1, dst);
        if (l == nullptr || r == nullptr) break;
        *l = io.in[0] != nullptr ? io.in[0][from + i] : 0.0f;
        *r = io.in[1] != nullptr ? io.in[1][from + i] : *l;
    }
    tailIndex += n; tailRemaining -= n;
    if (tailRemaining <= 0) tailTake = -1;
}

void LoopRt::playAudio (int from, int to, const BlockTime& t, LoopIo& io, bool overdub, const LoopContext& ctx) noexcept {
    if (numTakes == 0) return;
    auto& tk = takes[activeTake];
    if (tk.lengthFrames <= 0 || tk.lengthBeats <= 0) return;
    const double len = (double) tk.lengthFrames;
    const double framesPerBeat = len / tk.lengthBeats;
    const double ratio = t.beatsPerSample * framesPerBeat;      // 1.0 at the recording tempo
    double p = positiveMod (t.beatAt (from) - anchorBeat, tk.lengthBeats) * framesPerBeat;
    if (p >= len) p -= len;
    const bool isMuted = muted.load (std::memory_order_relaxed);
    const float target = isMuted ? 0.0f : volume.load (std::memory_order_relaxed);
    const float envTarget = state == LoopState::Stopped ? 0.0f : 1.0f;
    const int n = to - from;
    const float gStep = (target - lastGain) / (float) std::max (1, n);
    const float fb = settings.overdubFeedback;
    const int64_t L = tk.lengthFrames;
    float peak = 0;
    const bool exact = std::abs (ratio - 1.0) < 1.0e-9 && std::abs (p - std::round (p)) < 1.0e-6;
    if (exact) p = std::round (p);

    // Source frames per output sample at the original pitch (differs from 1 only when the take
    // was recorded at another device sample rate). Reading at `ratio` instead is tape-style.
    const double pitchRate = tk.sampleRate > 0 && t.sampleRate > 0 ? tk.sampleRate / t.sampleRate : 1.0;
    const bool stretch = ! exact && ! settings.varispeed && std::abs (ratio - pitchRate) > 1.0e-6;
    const int grainLen = std::clamp (2 * (int) std::lround (0.018 * t.sampleRate), 256, 8192);   // ~36 ms
    const int hop = grainLen / 2;
    auto readAt = [&] (double pos, float& l, float& r) noexcept {
        pos = std::fmod (pos, len); if (pos < 0) pos += len;
        int64_t a = (int64_t) pos; if (a >= L) a -= L;
        int64_t b = a + 1; if (b >= L) b = 0;
        const float fr = (float) (pos - std::floor (pos));
        l = tk.sample (0, a) + (tk.sample (0, b) - tk.sample (0, a)) * fr;
        r = tk.sample (1, a) + (tk.sample (1, b) - tk.sample (1, a)) * fr;
    };
    if (stretch && ! stretching) {
        // Enter with a half-finished grain so the overlapping windows sum to 1 from the first sample.
        grains[0] = { p - hop * pitchRate, hop, true };
        grains[1] = {};
        nextGrain = 1; grainClock = 0;
    }
    stretching = stretch;
    const double twoPiOverG = 2.0 * 3.14159265358979323846 / grainLen;

    for (int i = 0; i < n; ++i) {
        int64_t i0 = (int64_t) p;
        if (i0 >= L) i0 -= L;
        float sl = 0, sr = 0;
        if (exact) { sl = tk.sample (0, i0); sr = tk.sample (1, i0); }
        else if (stretch) {
            if (grainClock == 0) {
                // WSOLA: start the new grain near the beat-synced position, at the offset that best
                // continues the grain that is fading out (avoids phasing / pitch wobble). The
                // offset is bounded, so the loop never drifts from the beat.
                double start = p;
                const auto& old = grains[nextGrain ^ 1];
                if (old.on) start = bestGrainStart (tk, p, old.src + old.age * pitchRate, pitchRate, hop / 2);
                grains[nextGrain] = { start, 0, true };
                nextGrain ^= 1;
            }
            if (++grainClock >= hop) grainClock = 0;
            for (auto& gr : grains) {
                if (! gr.on) continue;
                const float w = (float) (0.5 - 0.5 * std::cos (twoPiOverG * gr.age));
                float a, b;
                readAt (gr.src + gr.age * pitchRate, a, b);
                sl += a * w; sr += b * w;
                if (++gr.age >= grainLen) gr.on = false;
            }
        } else {
            const float fr = (float) (p - (double) (int64_t) p);
            int64_t i1 = i0 + 1; if (i1 >= L) i1 = 0;
            sl = tk.sample (0, i0) + (tk.sample (0, i1) - tk.sample (0, i0)) * fr;
            sr = tk.sample (1, i0) + (tk.sample (1, i1) - tk.sample (1, i0)) * fr;
        }
        if (env != envTarget) env = envTarget > env ? std::min (envTarget, env + kEnvStep) : std::max (envTarget, env - kEnvStep);
        const float g = (lastGain + gStep * (float) i) * env;
        if (io.out[0] != nullptr) io.out[0][from + i] += sl * g;
        if (io.out[1] != nullptr) io.out[1][from + i] += sr * g;
        peak = std::max (peak, std::max (std::abs (sl), std::abs (sr)) * g);
        if (overdub) {
            int64_t w = i0 - recordLatency; while (w < 0) w += L;
            float* wl = tk.writePtr (0, w); float* wr = tk.writePtr (1, w);
            if (wl != nullptr && wr != nullptr) {
                const float inL = io.in[0] != nullptr ? io.in[0][from + i] : 0.0f;
                const float inR = io.in[1] != nullptr ? io.in[1][from + i] : inL;
                *wl = *wl * fb + inL; *wr = *wr * fb + inR;
            }
        }
        p += exact ? 1.0 : ratio;
        if (p >= len) p -= len;
    }
    lastGain = target;
    if (state == LoopState::Stopped && env <= 0) lastGain = 0;
    uiPeak.store (std::max (peak, uiPeak.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    (void) ctx;
}

double LoopRt::bestGrainStart (const TakeAudio& tk, double synced, double target, double pitchRate, int radius) const noexcept {
    const int64_t L = tk.lengthFrames;
    if (L <= 0 || radius <= 0) return synced;
    auto mono = [&] (double pos) noexcept {
        int64_t i = (int64_t) std::floor (pos);
        i %= L; if (i < 0) i += L;
        return tk.sample (0, i) + tk.sample (1, i);
    };
    // Reference: what the fading grain would play next.
    constexpr int kPoints = 96;
    float ref[kPoints];
    double refEnergy = 0;
    for (int k = 0; k < kPoints; ++k) { ref[k] = mono (target + k * 4 * pitchRate); refEnergy += (double) ref[k] * ref[k]; }
    if (refEnergy < 1.0e-9) return synced;   // silence: nothing to match
    auto score = [&] (double cand, int step) noexcept {
        double c = 0, e = 0;
        for (int k = 0; k < kPoints; k += step) {
            const float x = mono (cand + k * 4 * pitchRate);
            c += (double) x * ref[k]; e += (double) x * x;
        }
        return e > 1.0e-12 ? c / std::sqrt (e) : -1.0e30;
    };
    int best = 0; double bestScore = -1.0e30;
    for (int d = -radius; d <= radius; d += 3) {          // coarse
        const double s = score (synced + d, 2);
        if (s > bestScore) { bestScore = s; best = d; }
    }
    const int c0 = best;
    bestScore = -1.0e30;
    for (int d = std::max (-radius, c0 - 2); d <= std::min (radius, c0 + 2); ++d) {   // refine
        const double s = score (synced + d, 1);
        if (s > bestScore) { bestScore = s; best = d; }
    }
    return synced + best;
}

void LoopRt::playMidi (int from, int to, const BlockTime& t, LoopIo& io) noexcept {
    if (io.midiOut == nullptr || numTakes == 0) return;
    const MidiSequence* seq = midiSequences[activeTake].get();
    if (seq != lastSeq) { allNotesOff (io.midiOut, from); lastSeq = seq; }
    if (seq == nullptr || seq->events.empty() || seq->lengthBeats <= 0 || t.beatsPerSample <= 0) return;
    const double L = seq->lengthBeats;
    double b0 = positiveMod (t.beatAt (from) - anchorBeat, L);
    double remaining = (to - from) * t.beatsPerSample;
    int sampleBase = from;
    const float vol = volume.load (std::memory_order_relaxed);
    while (remaining > 1.0e-12) {
        const double b1 = std::min (L, b0 + remaining);
        auto it = std::lower_bound (seq->events.begin(), seq->events.end(), b0,
                                    [] (const MidiEvent& e, double b) { return e.beat < b; });
        for (; it != seq->events.end() && it->beat < b1; ++it) {
            int s = sampleBase + (int) ((it->beat - b0) / t.beatsPerSample);
            s = std::clamp (s, from, to - 1);
            uint8_t d[3] = { it->data[0], it->data[1], it->data[2] };
            const int ch = d[0] & 0x0F;
            if (it->isNoteOn()) {
                d[2] = (uint8_t) std::clamp ((int) std::lround (d[2] * std::min (1.0f, vol)), 1, 127);
                heldNotes[ch][d[1] >> 6] |= (1ull << (d[1] & 63));
            } else if (it->isNoteOff()) {
                if (! (heldNotes[ch][d[1] >> 6] & (1ull << (d[1] & 63)))) continue;
                heldNotes[ch][d[1] >> 6] &= ~(1ull << (d[1] & 63));
            }
            io.midiOut->add (s, d, it->size);
        }
        const double consumed = b1 - b0;
        sampleBase += (int) std::lround (consumed / t.beatsPerSample);
        remaining -= consumed;
        b0 = 0; // wrapped
        if (b1 < L) break;
    }
}

void LoopRt::recordMidi (int from, int to, const BlockTime& t, LoopIo& io, const LoopContext& ctx, double originBeat) noexcept {
    if (io.midiIn == nullptr || ctx.events == nullptr) return;
    const double L = state == LoopState::Recording ? 0.0 : lengthBeats();
    for (int i = 0; i < io.midiIn->count; ++i) {
        const auto& m = io.midiIn->events[i];
        if (m.sample < from || m.sample >= to) continue;
        const uint8_t status = m.data[0] & 0xF0;
        if (status < 0x80 || status == 0xF0) continue;
        double b = t.beatAt (m.sample) - ctx.midiLatency * t.beatsPerSample - originBeat;
        if (L > 0) b = positiveMod (b, L);
        else b = std::max (0.0, b);
        EngineEvent e; e.type = EngineEventType::MidiRecorded; e.loopId = id; e.trackId = trackId;
        e.take = state == LoopState::Recording ? recordTake : activeTake; e.beat = b;
        e.midi.beat = b; e.midi.size = m.size; std::memcpy (e.midi.data, m.data, 3);
        ctx.events->push (e);
    }
}

void LoopRt::allNotesOff (BlockMidi* out, int sample) noexcept {
    for (int ch = 0; ch < 16; ++ch)
        for (int w = 0; w < 2; ++w) {
            uint64_t bits = heldNotes[ch][w];
            while (bits != 0) {
                int b = 0; while (! (bits & (1ull << b))) ++b;
                bits &= ~(1ull << b);
                if (out != nullptr) { uint8_t d[3] = { (uint8_t) (0x80 | ch), (uint8_t) (w * 64 + b), 0 }; out->add (sample, d, 3); }
            }
            heldNotes[ch][w] = 0;
        }
}

void LoopRt::transportStopped (const LoopContext& ctx, BlockMidi* midiOut) noexcept {
    if (state == LoopState::Recording) {
        abortRecording (ctx);   // an incomplete loop is not kept when the transport stops
    }
    if (pending == PendingAction::Record || pending == PendingAction::CloseRecord) pending = PendingAction::None;
    env = 0; lastGain = 0;
    allNotesOff (midiOut, 0);
    publish (0);
}

void LoopRt::transportStarted() noexcept {
    anchorBeat = 0;
    if (pending != PendingAction::None) pendingBeat = 0;   // armed actions fire on the downbeat
}

void LoopRt::releaseAll (ChunkPool& pool) noexcept {
    for (int i = 0; i < kMaxTakes; ++i) takes[i].release (pool);
    numTakes = 0;
}

bool LoopRt::loadTake (int takeIndex, const float* const* ch, int numCh, int64_t frames, double beats, ChunkPool& pool, double sampleRate) {
    if (takeIndex < 0 || takeIndex >= kMaxTakes || frames <= 0) return false;
    auto& tk = takes[takeIndex];
    tk.release (pool);
    const int needed = (int) ((frames + kChunkFrames - 1) >> kChunkShift);
    pool.reserve (needed + 4);
    for (int64_t f = 0; f < frames; f += kChunkFrames) if (! tk.ensure (f, pool)) return false;
    for (int64_t f = 0; f < frames; ++f) {
        *tk.writePtr (0, f) = ch[0][f];
        *tk.writePtr (1, f) = numCh > 1 ? ch[1][f] : ch[0][f];
    }
    tk.lengthFrames = frames; tk.lengthBeats = beats; tk.hasContent = true; tk.sampleRate = sampleRate;
    numTakes = std::max (numTakes, takeIndex + 1);
    if (state == LoopState::Empty) state = LoopState::Stopped;
    publish (0);
    return true;
}

void LoopRt::publish (double beatNow) noexcept {
    uiState.store (state, std::memory_order_relaxed);
    uiPending.store (pending, std::memory_order_relaxed);
    uiActiveTake.store (activeTake, std::memory_order_relaxed);
    uiNumTakes.store (numTakes, std::memory_order_relaxed);
    if (state == LoopState::Recording) {
        uiRecordedBeats.store (std::max (0.0, beatNow - anchorBeat), std::memory_order_relaxed);
        uiLengthBeats.store (recordTargetBeats, std::memory_order_relaxed);
        uiPhaseBeats.store (std::max (0.0, beatNow - anchorBeat), std::memory_order_relaxed);
    } else {
        uiLengthBeats.store (lengthBeats(), std::memory_order_relaxed);
        uiPhaseBeats.store (isAudible() ? phaseBeatsAt (beatNow) : 0.0, std::memory_order_relaxed);
    }
}

} // namespace el
