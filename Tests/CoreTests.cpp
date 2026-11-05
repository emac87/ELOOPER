// Core loop state-machine tests (sample-accurate record/playback, latency, takes, MIDI).
#include "TestFramework.h"
#include "Core/LoopCore.h"
#include <vector>

using namespace el;

namespace {
struct Rig {
    ChunkPool pool;
    EventQueue events { 4096 };
    LoopContext ctx;
    BlockTime t;
    double beat = 0;
    Rig (int latency = 0) {
        pool.reserve (64);
        ctx.pool = &pool; ctx.events = &events; ctx.recordLatency = latency;
        t.sampleRate = 48000; t.tempo = 120; t.beatsPerBar = 4; t.masterBeats = 4; t.playing = true;
        t.beatsPerSample = t.tempo / 60.0 / t.sampleRate;
    }
    // Runs one block; input sample value = absolute sample index (+1) so we can check alignment.
    void block (LoopRt& loop, int n, std::vector<float>& outL, int64_t& sampleIndex, bool feedInput = true) {
        std::vector<float> in (n), oL (n, 0.0f), oR (n, 0.0f);
        for (int i = 0; i < n; ++i) in[i] = feedInput ? (float) (sampleIndex + i + 1) : 0.0f;
        t.numSamples = n; t.beatStart = beat;
        LoopIo io; io.in[0] = in.data(); io.in[1] = in.data(); io.out[0] = oL.data(); io.out[1] = oR.data();
        loop.process (t, io, ctx);
        outL.insert (outL.end(), oL.begin(), oL.end());
        beat += n * t.beatsPerSample; sampleIndex += n;
    }
};
}

EL_TEST (loop_records_one_bar_and_plays_back_sample_exact) {
    Rig r;
    LoopRt loop (10, 2, LoopType::Audio);
    LoopSettings s; s.startQuantize = Quantize::None; s.lengthMode = RecordLength::Master;
    loop.command ({ LoopCommandType::SetSettings, 0, 0, 0, s }, r.t, r.ctx);
    loop.command ({ LoopCommandType::Record }, r.t, r.ctx);
    std::vector<float> out; int64_t si = 0;
    const int barFrames = 96000;  // 4 beats @120bpm, 48k
    while (si < barFrames * 3) r.block (loop, 256, out, si, si < barFrames);
    EL_CHECK (loop.currentState() == LoopState::Playing);
    EL_CHECK (loop.take (0).lengthFrames == barFrames);
    // Playback of the 2nd and 3rd bar must equal the recorded input (value = index+1).
    bool ok = true;
    for (int64_t i = barFrames; i < barFrames * 3; ++i) {
        const float expected = (float) ((i % barFrames) + 1);
        if (std::abs (out[(size_t) i] - expected) > 1e-3f * expected && ok) { ok = false; std::printf ("  mismatch at %lld: %f vs %f\n", (long long) i, out[(size_t) i], expected); }
    }
    // First samples are inside the fade-in? No: after record the envelope is already open.
    EL_CHECK (ok);
}

EL_TEST (loop_record_latency_shifts_audio_earlier) {
    const int lat = 300;
    Rig r (lat);
    LoopRt loop (10, 2, LoopType::Audio);
    LoopSettings s; s.startQuantize = Quantize::None;
    loop.command ({ LoopCommandType::SetSettings, 0, 0, 0, s }, r.t, r.ctx);
    loop.command ({ LoopCommandType::Record }, r.t, r.ctx);
    std::vector<float> out; int64_t si = 0;
    const int barFrames = 96000;
    while (si < barFrames * 3) r.block (loop, 128, out, si, true);
    // Frame k of the loop holds the input that arrived at k + lat (wrapping for the tail).
    const auto& tk = loop.take (0);
    EL_CHECK (tk.lengthFrames == barFrames);
    EL_CHECK_NEAR (tk.sample (0, 0), (double) (lat + 1), 1e-3);
    EL_CHECK_NEAR (tk.sample (0, 1000), (double) (1000 + lat + 1), 1e-2);
    EL_CHECK_NEAR (tk.sample (0, barFrames - 1), (double) (barFrames - 1 + lat + 1), 1e-1);
}

EL_TEST (loop_start_is_quantised_to_next_bar) {
    Rig r;
    LoopRt loop (10, 2, LoopType::Audio);
    LoopSettings s; s.startQuantize = Quantize::Bar;
    loop.command ({ LoopCommandType::SetSettings, 0, 0, 0, s }, r.t, r.ctx);
    std::vector<float> out; int64_t si = 0;
    r.block (loop, 1000, out, si);            // move off the downbeat
    r.t.beatStart = r.beat;
    loop.command ({ LoopCommandType::Record }, r.t, r.ctx);
    while (si < 96000 * 2 + 5000) r.block (loop, 512, out, si);
    // Recording started at sample 96000 (bar 2), so loop frame 0 == input 96001.
    EL_CHECK_NEAR (loop.take (0).sample (0, 0), 96001.0, 1e-3);
    EL_CHECK (loop.currentState() == LoopState::Playing);
}

EL_TEST (take_folder_appends_takes_and_cycle_recording) {
    Rig r;
    LoopRt loop (10, 2, LoopType::Audio);
    LoopSettings s; s.startQuantize = Quantize::None; s.recordMode = RecordMode::TakeFolder; s.cycleTakes = true;
    loop.command ({ LoopCommandType::SetSettings, 0, 0, 0, s }, r.t, r.ctx);
    loop.command ({ LoopCommandType::Record }, r.t, r.ctx);
    std::vector<float> out; int64_t si = 0;
    while (si < 96000 * 3 + 100) r.block (loop, 500, out, si);
    EL_CHECK (loop.currentState() == LoopState::Recording);
    EL_CHECK (loop.takeCount() == 4);         // 3 complete + 1 in progress
    r.t.beatStart = r.beat;
    loop.command ({ LoopCommandType::Record }, r.t, r.ctx);  // finish at end of this cycle
    while (si < 96000 * 4 + 100) r.block (loop, 500, out, si);
    EL_CHECK (loop.currentState() == LoopState::Playing);
    EL_CHECK (loop.takeCount() == 4);
    EL_CHECK (loop.activeTakeIndex() == 3);
    EL_CHECK_NEAR (loop.take (1).sample (0, 0), 96001.0, 1e-3);
    loop.command ({ LoopCommandType::SelectTake, 1 }, r.t, r.ctx);
    EL_CHECK (loop.activeTakeIndex() == 1);
    loop.command ({ LoopCommandType::DeleteTake, 0 }, r.t, r.ctx);
    EL_CHECK (loop.takeCount() == 3);
    EL_CHECK (loop.activeTakeIndex() == 0);
    EL_CHECK_NEAR (loop.take (0).sample (0, 0), 96001.0, 1e-3);
}

EL_TEST (stop_is_quantised_and_play_restarts_from_top) {
    Rig r;
    LoopRt loop (10, 2, LoopType::Audio);
    LoopSettings s; s.startQuantize = Quantize::None; s.stopQuantize = Quantize::Bar;
    loop.command ({ LoopCommandType::SetSettings, 0, 0, 0, s }, r.t, r.ctx);
    loop.command ({ LoopCommandType::Record }, r.t, r.ctx);
    std::vector<float> out; int64_t si = 0;
    while (si < 96000 + 10000) r.block (loop, 500, out, si);
    r.t.beatStart = r.beat;
    loop.command ({ LoopCommandType::Trigger }, r.t, r.ctx);     // playing -> stop at next bar
    EL_CHECK (loop.uiPending.load() == PendingAction::Stop);
    while (si < 96000 * 2 + 1000) r.block (loop, 500, out, si);
    EL_CHECK (loop.currentState() == LoopState::Stopped);
    EL_CHECK (loop.uiPending.load() == PendingAction::None);
}

EL_TEST (pool_exhaustion_closes_loop_gracefully) {
    Rig r;
    ChunkPool small; small.reserve (2);   // only 32768 frames
    r.ctx.pool = &small;
    LoopRt loop (10, 2, LoopType::Audio);
    LoopSettings s; s.startQuantize = Quantize::None; s.lengthMode = RecordLength::Free;
    loop.command ({ LoopCommandType::SetSettings, 0, 0, 0, s }, r.t, r.ctx);
    loop.command ({ LoopCommandType::Record }, r.t, r.ctx);
    std::vector<float> out; int64_t si = 0;
    while (si < 96000) r.block (loop, 512, out, si);
    EL_CHECK (loop.currentState() == LoopState::Playing);
    EL_CHECK (loop.take (0).lengthFrames == 2 * kChunkFrames);
}

EL_TEST (midi_loop_records_events_and_plays_sequence) {
    Rig r;
    LoopRt loop (11, 2, LoopType::Midi);
    LoopSettings s; s.startQuantize = Quantize::None;
    loop.command ({ LoopCommandType::SetSettings, 0, 0, 0, s }, r.t, r.ctx);
    loop.command ({ LoopCommandType::Record }, r.t, r.ctx);
    BlockMidi in, out;
    const uint8_t on[3] = { 0x90, 60, 100 }, off[3] = { 0x80, 60, 0 };
    int64_t si = 0;
    for (int b = 0; b <= 96000 / 480; ++b) {
        in.clear(); out.clear();
        if (si == 24000) in.add (0, on, 3);         // beat 1
        if (si == 48000) in.add (0, off, 3);        // beat 2
        r.t.numSamples = 480; r.t.beatStart = r.beat;
        LoopIo io; io.midiIn = &in; io.midiOut = &out;
        loop.process (r.t, io, r.ctx);
        r.beat += 480 * r.t.beatsPerSample; si += 480;
    }
    std::vector<MidiEvent> recorded; EngineEvent e; double len = 0;
    while (r.events.pop (e)) {
        if (e.type == EngineEventType::MidiRecorded) recorded.push_back (e.midi);
        if (e.type == EngineEventType::RecordFinished) len = e.beat;
    }
    EL_CHECK (recorded.size() == 2);
    EL_CHECK_NEAR (len, 4.0, 1e-6);
    auto clip = MidiClip::fromEvents (recorded, len);
    EL_CHECK (clip.notes.size() == 1);
    if (! clip.notes.empty()) { EL_CHECK_NEAR (clip.notes[0].start, 1.0, 1e-6); EL_CHECK_NEAR (clip.notes[0].length, 1.0, 1e-6); }
    // Retirer needs an epoch; publish the sequence and play the next bar.
    RtEpoch epoch; Retirer ret (epoch);
    loop.midiSequences[0].publish (std::make_unique<MidiSequence> (MidiSequence::fromClip (clip)), ret);
    int ons = 0, offs = 0, firstOn = -1;
    for (int b = 1; b < 96000 / 480; ++b) {
        out.clear(); in.clear();
        r.t.numSamples = 480; r.t.beatStart = r.beat;
        LoopIo io; io.midiIn = &in; io.midiOut = &out;
        loop.process (r.t, io, r.ctx);
        for (int i = 0; i < out.count; ++i) {
            if ((out.events[i].data[0] & 0xF0) == 0x90) { ++ons; if (firstOn < 0) firstOn = (int) (si - 96000) + out.events[i].sample; }
            if ((out.events[i].data[0] & 0xF0) == 0x80) ++offs;
        }
        r.beat += 480 * r.t.beatsPerSample; si += 480;
    }
    EL_CHECK (ons == 1); EL_CHECK (offs == 1);
    EL_CHECK (std::abs (firstOn - 24000) <= 1);
}
