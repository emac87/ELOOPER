#include "Synth/dsp/Synth.h"
#include <algorithm>

namespace el::chimera {

// ---- names / live params ------------------------------------------------------------------------
const char* engineName (Engine e) noexcept
{
    switch (e) {
        case Engine::Off: return "Off";
        case Engine::Analog: return "Analog";
        case Engine::Wavetable: return "Wavetable";
        case Engine::FM: return "FM";
        case Engine::Sampler: return "Sampler";
        case Engine::Pluck: return "Pluck";
    }
    return "?";
}

const char* lfoShapeName (LfoShape s) noexcept
{
    switch (s) {
        case LfoShape::Sine: return "Sine";
        case LfoShape::Triangle: return "Triangle";
        case LfoShape::SawUp: return "Saw Up";
        case LfoShape::SawDown: return "Saw Down";
        case LfoShape::Square: return "Square";
        case LfoShape::SampleHold: return "S&H";
        case LfoShape::SmoothRandom: return "Smooth Random";
    }
    return "?";
}

const char* modSrcName (ModSrc s) noexcept
{
    static const char* names[] = { "None", "LFO 1", "LFO 2", "Global LFO 1", "Global LFO 2", "Amp Env", "Filter Env",
                                   "Mod Env", "Velocity", "Key Track", "Mod Wheel", "Aftertouch", "Pitch Bend", "Random",
                                   "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    const int i = (int) s;
    return i >= 0 && i < kNumModSrcs ? names[i] : "?";
}

const char* modDstName (ModDst d) noexcept
{
    static const char* names[] = { "None", "Pitch", "Cutoff", "Resonance", "Drive", "Level", "Pan", "Pulse Width",
                                   "Osc Mix", "Osc 2 Pitch", "Noise", "Wave Position", "FM Amount", "Sample Start",
                                   "Pluck Damping", "LFO 1 Rate", "LFO 2 Rate", "Filter Env Amount" };
    const int i = (int) d;
    return i >= 0 && i < kNumModDsts ? names[i] : "?";
}

LiveParams LiveParams::fromPatch (const Patch& p) noexcept
{
    LiveParams l;
    l.master = p.masterVolume;
    for (int i = 0; i < kMaxLayers; ++i) {
        l.level[i] = p.layers[(size_t) i].level;
        l.cutoff[i] = p.layers[(size_t) i].cutoff;
        l.resonance[i] = p.layers[(size_t) i].resonance;
        l.pan[i] = p.layers[(size_t) i].pan;
    }
    return l;
}

// ---- patch exchange -----------------------------------------------------------------------------
void PatchExchange::publish (std::shared_ptr<const Patch> p)
{
    if (! p) return;
    live.store (p.get(), std::memory_order_release);
    // Any block that loaded the old pointer started before now, so it ends at epoch > `at`.
    if (current) retired.push_back ({ current, epoch.load (std::memory_order_acquire) });
    current = std::move (p);
    collect();
}

void PatchExchange::collect()
{
    const uint64_t e = epoch.load (std::memory_order_acquire);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [e] (const Retired& r) { return e > r.at + 1; }),
                   retired.end());
    // If the audio thread isn't running, old patches pile up; keep the newest few hundred only
    // (an audio thread that's stopped can't be holding them).
    if (retired.size() > 512) retired.erase (retired.begin(), retired.begin() + 256);
}

// ---- voice --------------------------------------------------------------------------------------
struct Synth::Voice {
    bool active = false;
    int layer = 0, note = 60;
    float velocity = 1.0f;
    uint64_t order = 0;
    bool keyDown = false, sustained = false, releasing = false;
    float pitch = 60.0f, targetPitch = 60.0f;
    float random = 0.0f;
    double age = 0.0;               // seconds since note on
    double releasedAt = 0.0;        // age when released
    Engine engine = Engine::Off;
    int unison = 1;
    const SampleData* sample = nullptr;
    Envelope amp, filt, mod;
    Lfo lfo[kLayerLfos];
    AnalogOsc analog[kMaxUnison];
    WavetableOsc wt[kMaxUnison];
    FmVoice fm[kMaxUnison];
    SamplerVoice smp[kMaxUnison];
    PluckVoice pluck[kMaxUnison];
    Dx7Patch fmPatch;               // the voice's own copy: the patch may be replaced while it plays
    Filter filtL, filtR;
    float panGainL[kMaxUnison] {}, panGainR[kMaxUnison] {}, detune[kMaxUnison] {};
};

Synth::Synth()
{
    voices.reserve (kMaxVoices);
    for (int i = 0; i < kMaxVoices; ++i) voices.push_back (std::make_unique<Voice>());
}

Synth::~Synth() = default;

void Synth::prepare (double sr, int block)
{
    sampleRate = sr > 0 ? sr : 48000.0;
    maxBlock = std::max (32, block);
    for (auto& b : bus) b.assign ((size_t) maxBlock, 0.0f);
    const float fsr = (float) sampleRate;
    for (auto& v : voices) {
        v->active = false;
        v->amp.setSampleRate (fsr); v->filt.setSampleRate (fsr); v->mod.setSampleRate (fsr);
        v->amp.kill(); v->filt.kill(); v->mod.kill();
        for (auto& l : v->lfo) l.setSampleRate (fsr);
        for (int u = 0; u < kMaxUnison; ++u) {
            v->fm[u].prepare (fsr);
            v->pluck[u].prepare (fsr);
        }
        v->filtL.prepare (fsr); v->filtR.prepare (fsr);
    }
    for (int i = 0; i < kGlobalLfos; ++i) { globalLfo[i].setSampleRate (fsr); globalLfo[i].reset (0.0f, 0x51u + (uint32_t) i); }
    chorus.prepare (sampleRate, maxBlock);
    delay.prepare (sampleRate, maxBlock);
    reverb.prepare (sampleRate, maxBlock);
    for (auto& ls : layerState) ls = LayerState();
    modWheel = aftertouch = bend = 0;
    sustainPedal = false;
    prepared = true;
}

void Synth::allNotesOff() noexcept
{
    sustainPedal = false;
    for (auto& v : voices) if (v->active) { v->keyDown = false; v->sustained = false; releaseVoice (*v); }
    for (auto& ls : layerState) ls.stackSize = 0;
}

void Synth::panic() noexcept
{
    for (auto& v : voices) { v->active = false; v->amp.kill(); }
    for (auto& ls : layerState) ls = LayerState();
    chorus.reset(); delay.reset(); reverb.reset();
    sustainPedal = false;
}

// ---- MIDI ---------------------------------------------------------------------------------------
void Synth::handleEvent (const MidiEvent& e, const Patch& p) noexcept
{
    const int type = e.status & 0xF0;
    switch (type) {
        case 0x90:
            if (e.data2 > 0) noteOn (e.data1 & 127, e.data2 & 127, p);
            else noteOff (e.data1 & 127, p);
            break;
        case 0x80: noteOff (e.data1 & 127, p); break;
        case 0xB0:
            if (e.data1 == 1) modWheel = (float) e.data2 / 127.0f;
            else if (e.data1 == 64) {
                const bool down = e.data2 >= 64;
                if (sustainPedal && ! down)
                    for (auto& v : voices) if (v->active && v->sustained) { v->sustained = false; releaseVoice (*v); }
                sustainPedal = down;
            }
            else if (e.data1 == 120) panic();
            else if (e.data1 == 123) allNotesOff();
            break;
        case 0xD0: aftertouch = (float) e.data1 / 127.0f; break;
        case 0xE0: bend = (float) (((e.data2 & 127) << 7 | (e.data1 & 127)) - 8192) / 8192.0f; break;
        default: break;
    }
}

Synth::Voice* Synth::allocateVoice (int layer, const Patch& p) noexcept
{
    const auto& lp = p.layers[(size_t) layer];
    // Per-layer polyphony: steal this layer's oldest (released first) when at the limit.
    int count = 0;
    Voice* oldestRel = nullptr; Voice* oldest = nullptr;
    for (auto& v : voices) {
        if (! v->active || v->layer != layer) continue;
        ++count;
        if (v->releasing && (! oldestRel || v->order < oldestRel->order)) oldestRel = v.get();
        if (! oldest || v->order < oldest->order) oldest = v.get();
    }
    if (count >= clampv (lp.polyphony, 1, 32)) return oldestRel ? oldestRel : oldest;
    for (auto& v : voices) if (! v->active) return v.get();
    // Pool full: steal the globally oldest released voice, else the oldest voice.
    oldestRel = nullptr; oldest = nullptr;
    for (auto& v : voices) {
        if (v->releasing && (! oldestRel || v->order < oldestRel->order)) oldestRel = v.get();
        if (! oldest || v->order < oldest->order) oldest = v.get();
    }
    return oldestRel ? oldestRel : oldest;
}

void Synth::startVoice (Voice& v, int layer, int note, float velocity, const Patch& p, bool glideFromLast) noexcept
{
    const auto& lp = p.layers[(size_t) layer];
    auto& ls = layerState[layer];
    const bool wasActive = v.active && v.layer == layer;
    v.active = true;
    v.layer = layer;
    v.note = note;
    v.velocity = velocity;
    v.order = ++noteCounter;
    v.keyDown = true; v.sustained = false; v.releasing = false;
    v.engine = lp.engine;
    v.targetPitch = (float) note;
    v.pitch = (glideFromLast && lp.glide > 0.0f && ls.lastPitch >= 0.0f) ? ls.lastPitch : (float) note;
    v.random = rng.bi();
    v.age = 0.0;
    v.unison = clampv (lp.unison, 1, kMaxUnison);
    if (! wasActive) { v.amp.kill(); v.filt.kill(); v.mod.kill(); }
    v.amp.noteOn(); v.filt.noteOn(); v.mod.noteOn();
    for (int i = 0; i < kLayerLfos; ++i)
        v.lfo[i].reset (lp.lfo[i].retrigger ? 0.0f : rng.uni(), rng.nextU());
    if (! wasActive) { v.filtL.reset(); v.filtR.reset(); }

    const int n = v.unison;
    for (int u = 0; u < n; ++u) {
        const float t = n == 1 ? 0.0f : (float) u / (float) (n - 1) * 2.0f - 1.0f;   // -1..1
        v.detune[u] = t * lp.unisonDetune * 50.0f;                                   // cents
        const float pan = clampv (t * lp.unisonSpread, -1.0f, 1.0f);
        const float g = 1.0f / std::sqrt ((float) n);
        v.panGainL[u] = g * std::cos ((pan + 1.0f) * kPi * 0.25f) * 1.41421356f;
        v.panGainR[u] = g * std::sin ((pan + 1.0f) * kPi * 0.25f) * 1.41421356f;
    }

    const float sr = (float) sampleRate;
    const float hz = noteToHz ((float) note + (float) lp.transpose + lp.fine * 0.01f);
    v.sample = nullptr;
    switch (lp.engine) {
        case Engine::Analog:
            for (int u = 0; u < n; ++u) v.analog[u].reset (sr, rng.nextU(), n == 1 ? 0.0f : rng.uni());
            break;
        case Engine::Wavetable:
            for (int u = 0; u < n; ++u) v.wt[u].reset (sr, n == 1 ? 0.0f : rng.uni());
            break;
        case Engine::FM:
            v.fmPatch = lp.fm;
            for (int u = 0; u < n; ++u)
                v.fm[u].noteOn (v.fmPatch, clampv (note + lp.transpose, 0, 127), clampv ((int) (velocity * 127.0f + 0.5f), 1, 127));
            break;
        case Engine::Sampler: {
            v.sample = p.samples[(size_t) layer].get();
            if (v.sample == nullptr || v.sample->length() < 4) { v.active = false; return; }
            for (int u = 0; u < n; ++u)
                v.smp[u].start (v.sample, (float) note + (float) lp.transpose + lp.fine * 0.01f + v.detune[u] * 0.01f,
                                velocity, lp.sampler, sampleRate);
            break;
        }
        case Engine::Pluck:
            for (int u = 0; u < n; ++u) v.pluck[u].start (hz * semisToRatio (v.detune[u] * 0.01f), velocity, lp.pluck, rng.nextU());
            break;
        case Engine::Off:
            v.active = false;
            return;
    }
}

void Synth::releaseVoice (Voice& v) noexcept
{
    if (! v.active || v.releasing) return;
    v.releasing = true;
    v.releasedAt = v.age;
    v.amp.noteOff(); v.filt.noteOff(); v.mod.noteOff();
    if (v.engine == Engine::FM) for (int u = 0; u < v.unison; ++u) v.fm[u].noteOff();
}

void Synth::noteOn (int note, int velocity, const Patch& p) noexcept
{
    const float vel = (float) velocity / 127.0f;
    for (int L = 0; L < kMaxLayers; ++L) {
        const auto& lp = p.layers[(size_t) L];
        if (lp.engine == Engine::Off) continue;
        if (note < lp.keyLo || note > lp.keyHi || velocity < lp.velLo || velocity > lp.velHi) continue;
        auto& ls = layerState[L];

        if (lp.mode != VoiceMode::Poly) {
            // Mono / legato: one voice per layer, last-note priority with a note stack.
            if (ls.stackSize < 16) {
                int k = 0; for (int i = 0; i < ls.stackSize; ++i) if (ls.monoStack[i] != note) ls.monoStack[k++] = ls.monoStack[i];
                ls.stackSize = k;
                ls.monoStack[ls.stackSize++] = note;
            }
            Voice* mv = nullptr;
            for (auto& v : voices) if (v->active && v->layer == L) { if (! mv || v->order > mv->order) mv = v.get(); }
            for (auto& v : voices) if (v->active && v->layer == L && v.get() != mv) releaseVoice (*v);   // keep one
            if (mv && ! mv->releasing && lp.mode == VoiceMode::Legato && mv->engine == lp.engine) {
                mv->note = note; mv->targetPitch = (float) note; mv->keyDown = true; mv->velocity = vel;
                mv->order = ++noteCounter;
                if (lp.glide <= 0.0f) mv->pitch = (float) note;
                ls.lastPitch = (float) note;
                continue;
            }
            Voice* v = mv ? mv : allocateVoice (L, p);
            if (v) startVoice (*v, L, note, vel, p, mv != nullptr || ls.lastPitch >= 0.0f);
            ls.lastPitch = (float) note;
            continue;
        }

        Voice* v = allocateVoice (L, p);
        if (v) startVoice (*v, L, note, vel, p, true);
        ls.lastPitch = (float) note;
    }
}

void Synth::noteOff (int note, const Patch& p) noexcept
{
    for (int L = 0; L < kMaxLayers; ++L) {
        const auto& lp = p.layers[(size_t) L];
        auto& ls = layerState[L];
        if (lp.mode != VoiceMode::Poly) {
            int k = 0; for (int i = 0; i < ls.stackSize; ++i) if (ls.monoStack[i] != note) ls.monoStack[k++] = ls.monoStack[i];
            const bool wasTop = ls.stackSize > 0 && ls.monoStack[ls.stackSize - 1] == note;
            ls.stackSize = k;
            for (auto& v : voices) {
                if (! v->active || v->layer != L || v->releasing || v->note != note) continue;
                if (k > 0 && wasTop) {   // fall back to the previous held note
                    const int prev = ls.monoStack[k - 1];
                    v->note = prev; v->targetPitch = (float) prev;
                    if (lp.glide <= 0.0f) v->pitch = (float) prev;
                    if (lp.mode == VoiceMode::Mono) { v->amp.noteOn(); v->filt.noteOn(); v->mod.noteOn(); }
                    ls.lastPitch = (float) prev;
                } else if (k == 0) {
                    v->keyDown = false;
                    if (sustainPedal) v->sustained = true; else releaseVoice (*v);
                }
            }
            continue;
        }
        for (auto& v : voices) {
            if (! v->active || v->layer != L || v->note != note || ! v->keyDown) continue;
            v->keyDown = false;
            if (sustainPedal) v->sustained = true; else releaseVoice (*v);
        }
    }
}

// ---- rendering ----------------------------------------------------------------------------------
void Synth::renderVoice (Voice& v, const Patch& p, const LiveParams& live, int offset, int len, int connectedPairs) noexcept
{
    const auto& lp = p.layers[(size_t) v.layer];
    if (lp.engine != v.engine || (v.engine == Engine::Sampler && p.samples[(size_t) v.layer].get() != v.sample)) {
        v.active = false;   // the layer's engine / sample was changed while the note played
        return;
    }
    const float sr = (float) sampleRate;
    const int L = v.layer;

    // ---- modulation sources (values at the start of this sub-block)
    float src[kNumModSrcs] {};
    for (int i = 0; i < kLayerLfos; ++i) {
        const float fade = lp.lfo[i].fadeIn > 0.0f ? clamp01 ((float) v.age / lp.lfo[i].fadeIn) : 1.0f;
        src[(int) ModSrc::Lfo1 + i] = v.lfo[i].value() * fade;
    }
    src[(int) ModSrc::GlobalLfo1] = globalLfoValue[0];
    src[(int) ModSrc::GlobalLfo2] = globalLfoValue[1];
    src[(int) ModSrc::AmpEnv] = v.amp.current();
    src[(int) ModSrc::FilterEnv] = v.filt.current();
    src[(int) ModSrc::ModEnv] = v.mod.current();
    src[(int) ModSrc::Velocity] = v.velocity;
    src[(int) ModSrc::KeyTrack] = ((float) v.note - 60.0f) / 48.0f;
    src[(int) ModSrc::ModWheel] = modWheel;
    src[(int) ModSrc::Aftertouch] = aftertouch;
    src[(int) ModSrc::PitchBend] = bend;
    src[(int) ModSrc::Random] = v.random;
    for (int i = 0; i < kMacros; ++i) src[(int) ModSrc::Macro1 + i] = clamp01 (live.macros[i]);

    float dst[kNumModDsts] {};
    for (const auto& m : lp.mods)
        if (m.src != ModSrc::None && m.dst != ModDst::None && (int) m.src < kNumModSrcs && (int) m.dst < kNumModDsts)
            dst[(int) m.dst] += src[(int) m.src] * m.amount;

    // advance the per-voice modulators for the next sub-block
    for (int i = 0; i < kLayerLfos; ++i)
        v.lfo[i].advance (lp.lfo[i], len, bpmNow, std::exp2 (dst[(int) (i == 0 ? ModDst::Lfo1Rate : ModDst::Lfo2Rate)] * 4.0f));
    const float filtEnv = v.filt.process (lp.filterEnv, len);
    v.mod.process (lp.modEnv, len);
    v.age += (double) len / sampleRate;

    // ---- pitch (glide is exponential, roughly reaching the target in `glide` seconds)
    if (v.pitch != v.targetPitch) {
        if (lp.glide <= 0.0f) v.pitch = v.targetPitch;
        else {
            const float c = 1.0f - std::exp (-4.6f * (float) len / (lp.glide * sr));
            v.pitch += (v.targetPitch - v.pitch) * c;
            if (std::abs (v.targetPitch - v.pitch) < 0.001f) v.pitch = v.targetPitch;
        }
    }
    const float modPitch = dst[(int) ModDst::Pitch] * 24.0f;
    const float playPitch = v.pitch + (float) lp.transpose + lp.fine * 0.01f + bend * p.bendRange + modPitch;

    // ---- engine
    float vl[kSubBlock], vr[kSubBlock], tmp[kSubBlock], tmpR[kSubBlock];
    std::fill (vl, vl + len, 0.0f);
    std::fill (vr, vr + len, 0.0f);
    bool alive = false;
    const int n = v.unison;
    switch (v.engine) {
        case Engine::Analog: {
            AnalogParams ap = lp.analog;
            ap.mix = clamp01 (ap.mix + dst[(int) ModDst::OscMix]);
            ap.osc2Semi = clampv (ap.osc2Semi + dst[(int) ModDst::Osc2Pitch] * 24.0f, -48.0f, 48.0f);
            ap.noise = clamp01 (ap.noise + dst[(int) ModDst::Noise]);
            const float pw = dst[(int) ModDst::PulseWidth] * 0.45f;
            for (int u = 0; u < n; ++u) {
                v.analog[u].render (tmp, len, noteToHz (playPitch + v.detune[u] * 0.01f), ap, pw);
                for (int i = 0; i < len; ++i) { vl[i] += tmp[i] * v.panGainL[u]; vr[i] += tmp[i] * v.panGainR[u]; }
            }
            alive = true;
            break;
        }
        case Engine::Wavetable: {
            const float pos = clamp01 (lp.wtPosition + dst[(int) ModDst::WavePosition]);
            for (int u = 0; u < n; ++u) {
                v.wt[u].render (tmp, len, noteToHz (playPitch + v.detune[u] * 0.01f), lp.wtTable, pos);
                for (int i = 0; i < len; ++i) { vl[i] += tmp[i] * v.panGainL[u]; vr[i] += tmp[i] * v.panGainR[u]; }
            }
            alive = true;
            break;
        }
        case Engine::FM: {
            const float amt = clampv (lp.fmAmount + dst[(int) ModDst::FmAmount], 0.0f, 2.0f);
            const float base = (float) clampv (v.note + lp.transpose, 0, 127);
            for (int u = 0; u < n; ++u) {
                v.fm[u].render (tmp, len, playPitch - base + v.detune[u] * 0.01f, amt);
                alive = alive || v.fm[u].active();
                for (int i = 0; i < len; ++i) { vl[i] += tmp[i] * v.panGainL[u]; vr[i] += tmp[i] * v.panGainR[u]; }
            }
            break;
        }
        case Engine::Sampler: {
            SamplerParams sp = lp.sampler;
            sp.start = clamp01 (sp.start + dst[(int) ModDst::SampleStart]);
            for (int u = 0; u < n; ++u) {
                const bool more = v.smp[u].render (tmp, tmpR, len, playPitch + v.detune[u] * 0.01f, sp);
                alive = alive || more;
                const float gl = n == 1 ? 1.0f : v.panGainL[u], gr = n == 1 ? 1.0f : v.panGainR[u];
                for (int i = 0; i < len; ++i) { vl[i] += tmp[i] * gl; vr[i] += tmpR[i] * gr; }
            }
            break;
        }
        case Engine::Pluck: {
            PluckParams pp = lp.pluck;
            pp.damping = clamp01 (pp.damping + dst[(int) ModDst::PluckDamping]);
            for (int u = 0; u < n; ++u) {
                v.pluck[u].render (tmp, len, noteToHz (playPitch + v.detune[u] * 0.01f), pp);
                for (int i = 0; i < len; ++i) { vl[i] += tmp[i] * v.panGainL[u]; vr[i] += tmp[i] * v.panGainR[u]; }
            }
            alive = true;
            break;
        }
        case Engine::Off: break;
    }

    // ---- filter
    const bool stereo = n > 1 || v.engine == Engine::Sampler;
    if (lp.filter != FilterType::Off) {
        const float baseNote = 69.0f + 12.0f * std::log2 (std::max (20.0f, live.cutoff[L]) / 440.0f);
        const float envAmt = clampv (lp.filterEnvAmount + dst[(int) ModDst::FilterEnvAmount], -1.0f, 1.0f);
        const float cutNote = baseNote + lp.keyTrack * (playPitch - 60.0f) + envAmt * 60.0f * filtEnv
                            + dst[(int) ModDst::Cutoff] * 60.0f;
        const float cutHz = clampv (noteToHz (cutNote), 20.0f, 0.45f * sr);
        const float res = clamp01 (live.resonance[L] + dst[(int) ModDst::Resonance]);
        const float drv = clamp01 (lp.drive + dst[(int) ModDst::Drive]);
        v.filtL.set (lp.filter, cutHz, res, drv);
        v.filtL.process (vl, len);
        if (stereo) { v.filtR.set (lp.filter, cutHz, res, drv); v.filtR.process (vr, len); }
        else std::copy (vl, vl + len, vr);
    }

    // ---- amp
    const bool useAmp = lp.useAmpEnv || v.engine != Engine::FM;
    float ampBuf[kSubBlock];
    if (useAmp) v.amp.process (lp.ampEnv, len, ampBuf);
    else {
        // FM without the layer envelope: the operator EGs shape it; just declick a steal / end.
        v.amp.process ({ 0.001f, 0.01f, 1.0f, 0.02f }, len, ampBuf);
        if (v.releasing) {
            // A DX7 voice whose carrier level 4 is above 0 never decays: fade it out over the
            // last second of a 12 s release limit so it can't get stuck.
            const float t = (float) (v.age - v.releasedAt);
            const float g = t < 11.0f ? 1.0f : std::max (0.0f, 12.0f - t);
            for (int i = 0; i < len; ++i) ampBuf[i] = g;
            if (g <= 0.0f) alive = false;
        }
    }
    const float lvl = clamp01 (live.level[L]);
    const float velGain = 1.0f - clamp01 (lp.velocitySens) * (1.0f - v.velocity);
    const float gain = lvl * lvl * velGain * std::max (0.0f, 1.0f + dst[(int) ModDst::Level]);
    const float pan = clampv (live.pan[L] + dst[(int) ModDst::Pan], -1.0f, 1.0f);
    const float gL = gain * std::cos ((pan + 1.0f) * kPi * 0.25f) * 1.41421356f;
    const float gR = gain * std::sin ((pan + 1.0f) * kPi * 0.25f) * 1.41421356f;

    int pair = clampv (lp.output, 0, kOutputPairs - 1);
    if (pair >= connectedPairs) pair = 0;
    float* outL = bus[(size_t) pair * 2].data() + offset;
    float* outR = bus[(size_t) pair * 2 + 1].data() + offset;
    for (int i = 0; i < len; ++i) {
        outL[i] += vl[i] * ampBuf[i] * gL;
        outR[i] += vr[i] * ampBuf[i] * gR;
    }

    // ---- end of voice
    if (useAmp) { if (v.amp.idle()) v.active = false; }
    if (! alive && v.engine != Engine::Analog && v.engine != Engine::Wavetable && v.engine != Engine::Pluck) v.active = false;
    if (v.engine == Engine::Pluck && v.releasing && v.amp.idle()) v.active = false;
}

void Synth::renderChunk (const Patch& p, const LiveParams& live, int offset, int len, int connectedPairs) noexcept
{
    for (int i = 0; i < kGlobalLfos; ++i) globalLfoValue[i] = globalLfo[i].advance (p.globalLfo[i], len, bpmNow);
    for (auto& v : voices) if (v->active) renderVoice (*v, p, live, offset, len, connectedPairs);
}

void Synth::process (float* const* outs, int numChannels, int numSamples, const MidiEvent* events, int numEvents,
                     double bpm, const LiveParams& live) noexcept
{
    const Patch* p = exchange.acquire();
    if (! prepared || p == nullptr) {
        for (int c = 0; c < numChannels; ++c) if (outs[c]) std::fill (outs[c], outs[c] + numSamples, 0.0f);
        exchange.blockDone();
        return;
    }
    bpmNow = bpm > 1.0 ? bpm : 120.0;
    int connectedPairs = 0;
    while (connectedPairs < kOutputPairs && (connectedPairs + 1) * 2 <= numChannels
           && outs[connectedPairs * 2] && outs[connectedPairs * 2 + 1]) ++connectedPairs;

    int ev = 0;
    float pk = 0.0f;
    for (int done = 0; done < numSamples;) {
        const int block = std::min (maxBlock, numSamples - done);
        for (auto& b : bus) std::fill (b.begin(), b.begin() + block, 0.0f);
        int pos = 0;
        while (pos < block) {
            while (ev < numEvents && events[ev].offset - done <= pos) handleEvent (events[ev++], *p);
            int len = std::min (kSubBlock, block - pos);
            if (ev < numEvents) len = std::min (len, std::max (1, events[ev].offset - done - pos));
            renderChunk (*p, live, pos, len, connectedPairs);
            pos += len;
        }
        // master FX on the main pair, then master volume on every pair
        chorus.process (bus[0].data(), bus[1].data(), block, p->chorus);
        delay.process (bus[0].data(), bus[1].data(), block, p->delay, bpmNow);
        reverb.process (bus[0].data(), bus[1].data(), block, p->reverb);
        const float mg = clamp01 (live.master) * clamp01 (live.master);
        bool bad = false;
        for (int c = 0; c < kOutputPairs * 2; ++c) {
            float* b = bus[(size_t) c].data();
            for (int i = 0; i < block; ++i) {
                b[i] *= mg;
                const float a = std::abs (b[i]);
                if (! (a < 64.0f)) bad = true;   // NaN / inf / runaway: catches all of them
                pk = std::max (pk, a);
            }
        }
        if (bad) {   // never pass garbage to the speakers: silence and reset
            for (auto& b : bus) std::fill (b.begin(), b.begin() + block, 0.0f);
            panic();
            pk = 0.0f;
        }
        for (int c = 0; c < numChannels; ++c) {
            if (! outs[c]) continue;
            if (c < connectedPairs * 2) std::copy (bus[(size_t) c].begin(), bus[(size_t) c].begin() + block, outs[c] + done);
            else std::fill (outs[c] + done, outs[c] + done + block, 0.0f);
        }
        done += block;
    }
    while (ev < numEvents) handleEvent (events[ev++], *p);

    int count = 0;
    for (auto& v : voices) if (v->active) ++count;
    activeCount.store (count, std::memory_order_relaxed);
    peak.store (pk, std::memory_order_relaxed);
    exchange.blockDone();
}

} // namespace el::chimera
