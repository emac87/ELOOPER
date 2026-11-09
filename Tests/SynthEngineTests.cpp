// Chimera voice engine: notes, layers, zones, mono, sustain, voice limits, patch swaps, safety.
#include "TestFramework.h"
#include "Synth/dsp/Synth.h"

using namespace el::chimera;

namespace {
struct Rig {
    Synth s;
    std::vector<float> buf[8];
    float* outs[8] {};
    double sr = 48000;
    explicit Rig (const Patch& p, int channels = 2)
    {
        s.prepare (sr, 256);
        for (int c = 0; c < 8; ++c) { buf[c].assign (256, 0.0f); outs[c] = c < channels ? buf[c].data() : nullptr; }
        s.patches().publish (std::make_shared<const Patch> (p));
        nch = channels;
        live = LiveParams::fromPatch (p);
    }
    int nch = 2;
    LiveParams live;
    // Renders `blocks` blocks of 256; returns the RMS of channel `ch` (all blocks), events in the first block.
    double run (int blocks, std::vector<MidiEvent> ev = {}, int ch = 0)
    {
        double sum = 0; long count = 0;
        for (int b = 0; b < blocks; ++b) {
            s.process (outs, nch, 256, b == 0 && ! ev.empty() ? ev.data() : nullptr, b == 0 ? (int) ev.size() : 0, 120.0, live);
            for (int i = 0; i < 256; ++i) { const double v = buf[ch][(size_t) i]; sum += v * v; ++count; }
        }
        return std::sqrt (sum / std::max (1L, count));
    }
};
MidiEvent on (int note, int vel = 100, int off = 0) { return { off, 0x90, (uint8_t) note, (uint8_t) vel }; }
MidiEvent offEv (int note, int off = 0) { return { off, 0x80, (uint8_t) note, 0 }; }
}

EL_TEST (synth_silent_without_notes)
{
    Patch p;
    Rig r (p);
    EL_CHECK (r.run (10) < 1.0e-6);
}

EL_TEST (synth_note_on_off_every_engine)
{
    for (auto e : { Engine::Analog, Engine::Wavetable, Engine::FM, Engine::Pluck }) {
        Patch p;
        p.layers[0].engine = e;
        p.layers[0].ampEnv = { 0.001f, 0.2f, 0.8f, 0.05f };
        Rig r (p);
        const double playing = r.run (40, { on (60) });
        EL_CHECK (playing > 0.01);
        EL_CHECK (r.s.activeVoiceCount() == 1);
        r.run (1, { offEv (60) });
        r.run (400);   // ~2 s
        EL_CHECK (r.s.activeVoiceCount() == 0);
        EL_CHECK (r.run (10) < 1.0e-4);
    }
}

EL_TEST (synth_sampler_layer_plays_and_ends)
{
    Patch p;
    p.layers[0].engine = Engine::Sampler;
    auto smp = std::make_shared<SampleData>();
    smp->left.resize (4800);
    for (size_t i = 0; i < smp->left.size(); ++i) smp->left[i] = std::sin ((float) i * 0.0576f);
    smp->sampleRate = 48000; smp->rootNote = 60;
    p.samples[0] = smp;
    Rig r (p);
    EL_CHECK (r.run (8, { on (60) }) > 0.05);
    r.run (40);   // past the 0.1 s one-shot
    EL_CHECK (r.s.activeVoiceCount() == 0);
    // No sample loaded: the note is ignored, no crash.
    Patch q; q.layers[0].engine = Engine::Sampler;
    Rig r2 (q);
    EL_CHECK (r2.run (4, { on (60) }) < 1.0e-6);
}

EL_TEST (synth_layers_zones_and_outputs)
{
    Patch p;
    p.layers[0].engine = Engine::Analog; p.layers[0].keyHi = 59; p.layers[0].output = 0;
    p.layers[1].engine = Engine::Analog; p.layers[1].keyLo = 60; p.layers[1].output = 1;
    Rig r (p, 4);
    r.run (20, { on (48) });
    EL_CHECK (r.s.activeVoiceCount() == 1);
    double main = r.run (4, {}, 0), aux = r.run (4, {}, 2);
    EL_CHECK (main > 0.01);
    EL_CHECK (aux < 1.0e-6);
    r.run (1, { on (72) });
    main = r.run (4, {}, 0); aux = r.run (4, {}, 2);
    EL_CHECK (r.s.activeVoiceCount() == 2);
    EL_CHECK (aux > 0.01);
    // With only the main pair connected, layer 1 falls back to the main output.
    Rig r2 (p, 2);
    EL_CHECK (r2.run (20, { on (72) }) > 0.01);
}

EL_TEST (synth_polyphony_limit_and_pool)
{
    Patch p;
    p.layers[0].polyphony = 4;
    Rig r (p);
    std::vector<MidiEvent> ev;
    for (int n = 0; n < 10; ++n) ev.push_back (on (48 + n));
    r.run (4, ev);
    EL_CHECK (r.s.activeVoiceCount() == 4);
    // 4 layers x 32 voices would exceed the 64-voice pool: still bounded.
    Patch q;
    for (auto& l : q.layers) { l.engine = Engine::Analog; l.polyphony = 32; }
    Rig r2 (q);
    std::vector<MidiEvent> ev2;
    for (int n = 0; n < 40; ++n) ev2.push_back (on (30 + n));
    r2.run (4, ev2);
    EL_CHECK (r2.s.activeVoiceCount() <= kMaxVoices);
}

EL_TEST (synth_mono_legato_and_sustain)
{
    Patch p;
    p.layers[0].mode = VoiceMode::Legato;
    Rig r (p);
    r.run (4, { on (60), on (64, 100, 10) });
    EL_CHECK (r.s.activeVoiceCount() == 1);
    r.run (4, { offEv (64) });   // falls back to 60, still one voice sounding
    EL_CHECK (r.s.activeVoiceCount() == 1);
    EL_CHECK (r.run (4) > 0.01);

    Patch q;
    q.layers[0].ampEnv.release = 0.05f;
    Rig s (q);
    s.run (4, { { 0, 0xB0, 64, 127 }, on (60) });
    s.run (4, { offEv (60) });
    s.run (100);
    EL_CHECK (s.s.activeVoiceCount() == 1);   // held by the pedal
    s.run (1, { { 0, 0xB0, 64, 0 } });
    s.run (100);
    EL_CHECK (s.s.activeVoiceCount() == 0);
}

EL_TEST (synth_patch_swap_while_playing)
{
    Patch p;
    Rig r (p);
    r.run (4, { on (60), on (64), on (67) });
    for (int i = 0; i < 50; ++i) {
        Patch q = p;
        q.layers[0].engine = (i & 1) ? Engine::Wavetable : Engine::Analog;
        q.layers[0].cutoff = 200.0f + 100.0f * (float) i;
        r.s.patches().publish (std::make_shared<const Patch> (q));
        r.run (1, { on (50 + i % 20) });
    }
    EL_CHECK (std::isfinite (r.run (10)));
    r.s.patches().collect();
}

EL_TEST (synth_modulation_matrix_reaches_destinations)
{
    // Macro 1 -> Level -1 silences the layer; LFO -> Pitch and everything else stays finite.
    Patch p;
    p.layers[0].mods[0] = { ModSrc::Macro1, ModDst::Level, -1.0f };
    Rig r (p);
    const double loud = r.run (20, { on (60) });
    r.live.macros[0] = 1.0f;
    const double quiet = r.run (20);
    EL_CHECK (loud > 0.01);
    EL_CHECK (quiet < 1.0e-5);

    Patch q;
    for (int i = 0; i < kMatrixSlots; ++i)
        q.layers[0].mods[i] = { (ModSrc) (1 + i * 2 % (kNumModSrcs - 1)), (ModDst) (1 + (i * 3) % (kNumModDsts - 1)), 1.0f };
    q.layers[0].unison = 7;
    Rig r2 (q);
    for (int i = 0; i < kMacros; ++i) r2.live.macros[i] = 1.0f;
    EL_CHECK (std::isfinite (r2.run (100, { on (60), { 0, 0xE0, 0, 127 }, { 0, 0xB0, 1, 127 } })));
}

EL_TEST (synth_dx7_held_release_level_cannot_stick)
{
    // A DX7 voice whose carrier sustains forever (level 4 > 0) is faded out 12 s after release.
    Patch p;
    p.layers[0].engine = Engine::FM;
    p.layers[0].useAmpEnv = false;
    p.layers[0].fm.op[0].egLevel[3] = 99;
    Rig r (p);
    r.run (4, { on (60) });
    r.run (4, { offEv (60) });
    EL_CHECK (r.s.activeVoiceCount() == 1);
    r.run ((int) (13.0 * 48000 / 256));
    EL_CHECK (r.s.activeVoiceCount() == 0);
}
