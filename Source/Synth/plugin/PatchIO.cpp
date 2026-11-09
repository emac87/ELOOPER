#include "Synth/plugin/ChimeraProcessor.h"
#include <cmath>

// Patch <-> XML. Every field of Patch, LayerParams and the structs they contain is written as an
// attribute (or a child for nested structs) and read back tolerantly: a missing attribute keeps the
// default, enums and numbers are clamped to their valid ranges, unknown children are ignored.

namespace el::chimera {

namespace {

// Enum sizes that the headers do not export as kNum* constants.
constexpr int kNumWaves = 5;          // Wave: Sine, Triangle, Saw, Square, Noise
constexpr int kNumVoiceModes = 3;     // VoiceMode: Poly, Mono, Legato
constexpr int kNumLoopModes = 3;      // LoopMode: Off, Forward, PingPong
constexpr int kNumWavetables = 8;     // built-in wavetable tables (Oscillators.h)
constexpr int kMaxPolyphony = 32;     // LayerParams::polyphony is 1..32
constexpr int kVcedSize = 155;        // dx7ToVced() length

constexpr const char* kMacroAttr[kMacros] = { "macroName1", "macroName2", "macroName3", "macroName4" };

// ---- attribute writers ------------------------------------------------------------------------
void put (juce::XmlElement& e, const char* name, float v)  { e.setAttribute (name, (double) v); }
void put (juce::XmlElement& e, const char* name, int v)    { e.setAttribute (name, v); }
void put (juce::XmlElement& e, const char* name, bool v)   { e.setAttribute (name, v ? 1 : 0); }
void put (juce::XmlElement& e, const char* name, const std::string& v) { e.setAttribute (name, juce::String (v)); }

// ---- attribute readers (fallback = current value, then clamped) -------------------------------
float getF (const juce::XmlElement& e, const char* name, float def, float lo, float hi)
{
    const double d = e.getDoubleAttribute (name, (double) def);
    return std::isfinite (d) ? clampv ((float) d, lo, hi) : clampv (def, lo, hi);
}

int getI (const juce::XmlElement& e, const char* name, int def, int lo, int hi)
{
    return clampv (e.getIntAttribute (name, def), lo, hi);
}

bool getB (const juce::XmlElement& e, const char* name, bool def)
{
    return e.getIntAttribute (name, def ? 1 : 0) != 0;
}

template <typename E>
E getE (const juce::XmlElement& e, const char* name, E def, int count)
{
    return (E) getI (e, name, (int) def, 0, count - 1);
}

juce::String layerLabel (int i) { return "Layer " + juce::String (i + 1); }

// ---- envelopes, LFOs, modulation ----------------------------------------------------------------
void writeEnv (juce::XmlElement& e, const EnvParams& v)
{
    put (e, "attack", v.attack); put (e, "decay", v.decay);
    put (e, "sustain", v.sustain); put (e, "release", v.release);
}

void readEnv (const juce::XmlElement& e, EnvParams& v)
{
    v.attack  = getF (e, "attack",  v.attack,  0.0f, 10.0f);
    v.decay   = getF (e, "decay",   v.decay,   0.0f, 10.0f);
    v.sustain = getF (e, "sustain", v.sustain, 0.0f, 1.0f);
    v.release = getF (e, "release", v.release, 0.0f, 10.0f);
}

void writeLfo (juce::XmlElement& e, const LfoParams& v)
{
    put (e, "shape", (int) v.shape);
    put (e, "rateHz", v.rateHz); put (e, "sync", v.sync); put (e, "beats", v.beats);
    put (e, "retrigger", v.retrigger); put (e, "fadeIn", v.fadeIn);
}

void readLfo (const juce::XmlElement& e, LfoParams& v)
{
    v.shape     = getE (e, "shape", v.shape, kNumLfoShapes);
    v.rateHz    = getF (e, "rateHz", v.rateHz, 0.01f, 40.0f);
    v.sync      = getB (e, "sync", v.sync);
    v.beats     = getF (e, "beats", v.beats, 0.0625f, 16.0f);
    v.retrigger = getB (e, "retrigger", v.retrigger);
    v.fadeIn    = getF (e, "fadeIn", v.fadeIn, 0.0f, 10.0f);
}

void writeMod (juce::XmlElement& e, const ModSlot& v)
{
    put (e, "src", (int) v.src); put (e, "dst", (int) v.dst); put (e, "amount", v.amount);
}

void readMod (const juce::XmlElement& e, ModSlot& v)
{
    v.src    = getE (e, "src", v.src, kNumModSrcs);
    v.dst    = getE (e, "dst", v.dst, kNumModDsts);
    v.amount = getF (e, "amount", v.amount, -1.0f, 1.0f);
}

// ---- engines ------------------------------------------------------------------------------------
void writeAnalog (juce::XmlElement& e, const AnalogParams& v)
{
    put (e, "wave1", (int) v.wave1); put (e, "wave2", (int) v.wave2);
    put (e, "pw1", v.pw1); put (e, "pw2", v.pw2);
    put (e, "osc2Semi", v.osc2Semi); put (e, "osc2Fine", v.osc2Fine);
    put (e, "mix", v.mix); put (e, "sub", v.sub); put (e, "noise", v.noise);
    put (e, "sync", v.sync);
}

void readAnalog (const juce::XmlElement& e, AnalogParams& v)
{
    v.wave1    = getE (e, "wave1", v.wave1, kNumWaves);
    v.wave2    = getE (e, "wave2", v.wave2, kNumWaves);
    v.pw1      = getF (e, "pw1", v.pw1, 0.05f, 0.95f);
    v.pw2      = getF (e, "pw2", v.pw2, 0.05f, 0.95f);
    v.osc2Semi = getF (e, "osc2Semi", v.osc2Semi, -24.0f, 24.0f);
    v.osc2Fine = getF (e, "osc2Fine", v.osc2Fine, -100.0f, 100.0f);
    v.mix      = getF (e, "mix", v.mix, 0.0f, 1.0f);
    v.sub      = getF (e, "sub", v.sub, 0.0f, 1.0f);
    v.noise    = getF (e, "noise", v.noise, 0.0f, 1.0f);
    v.sync     = getB (e, "sync", v.sync);
}

void writeSampler (juce::XmlElement& e, const SamplerParams& v)
{
    put (e, "start", v.start); put (e, "loop", (int) v.loop);
    put (e, "loopStart", v.loopStart); put (e, "loopEnd", v.loopEnd);
    put (e, "reverse", v.reverse);
}

void readSampler (const juce::XmlElement& e, SamplerParams& v)
{
    v.start     = getF (e, "start", v.start, 0.0f, 1.0f);
    v.loop      = getE (e, "loop", v.loop, kNumLoopModes);
    v.loopStart = getF (e, "loopStart", v.loopStart, 0.0f, 1.0f);
    v.loopEnd   = getF (e, "loopEnd", v.loopEnd, 0.0f, 1.0f);
    v.reverse   = getB (e, "reverse", v.reverse);
}

void writePluck (juce::XmlElement& e, const PluckParams& v)
{
    put (e, "decay", v.decay); put (e, "damping", v.damping);
    put (e, "pickPos", v.pickPos); put (e, "noiseColor", v.noiseColor);
}

void readPluck (const juce::XmlElement& e, PluckParams& v)
{
    v.decay      = getF (e, "decay", v.decay, 0.0f, 1.0f);
    v.damping    = getF (e, "damping", v.damping, 0.0f, 1.0f);
    v.pickPos    = getF (e, "pickPos", v.pickPos, 0.0f, 0.5f);
    v.noiseColor = getF (e, "noiseColor", v.noiseColor, 0.0f, 1.0f);
}

// ---- layers -------------------------------------------------------------------------------------
void writeLayer (juce::XmlElement& e, const LayerParams& l)
{
    put (e, "engine", (int) l.engine);
    put (e, "name", l.name);
    put (e, "wtTable", l.wtTable); put (e, "wtPosition", l.wtPosition);
    put (e, "fmAmount", l.fmAmount);
    put (e, "filter", (int) l.filter);
    put (e, "cutoff", l.cutoff); put (e, "resonance", l.resonance);
    put (e, "drive", l.drive); put (e, "keyTrack", l.keyTrack);
    put (e, "filterEnvAmount", l.filterEnvAmount);
    put (e, "useAmpEnv", l.useAmpEnv);
    put (e, "level", l.level); put (e, "pan", l.pan);
    put (e, "transpose", l.transpose); put (e, "fine", l.fine);
    put (e, "keyLo", l.keyLo); put (e, "keyHi", l.keyHi);
    put (e, "velLo", l.velLo); put (e, "velHi", l.velHi);
    put (e, "velocitySens", l.velocitySens);
    put (e, "unison", l.unison); put (e, "unisonDetune", l.unisonDetune);
    put (e, "unisonSpread", l.unisonSpread);
    put (e, "glide", l.glide);
    put (e, "mode", (int) l.mode);
    put (e, "polyphony", l.polyphony);
    put (e, "output", l.output);

    writeAnalog (*e.createNewChildElement ("Analog"), l.analog);
    writeSampler (*e.createNewChildElement ("Sampler"), l.sampler);
    writePluck (*e.createNewChildElement ("Pluck"), l.pluck);
    writeEnv (*e.createNewChildElement ("AmpEnv"), l.ampEnv);
    writeEnv (*e.createNewChildElement ("FilterEnv"), l.filterEnv);
    writeEnv (*e.createNewChildElement ("ModEnv"), l.modEnv);

    for (int i = 0; i < kLayerLfos; ++i) {
        auto* c = e.createNewChildElement ("Lfo");
        c->setAttribute ("index", i);
        writeLfo (*c, l.lfo[i]);
    }
    for (int i = 0; i < kMatrixSlots; ++i) {
        auto* c = e.createNewChildElement ("Mod");
        c->setAttribute ("index", i);
        writeMod (*c, l.mods[i]);
    }

    // The 155-byte VCED form, as hex text.
    const auto vced = dx7ToVced (l.fm);
    e.createNewChildElement ("Fm")->setAttribute ("vced", juce::String::toHexString (vced.data(), (int) vced.size(), 0));
}

void readLayer (const juce::XmlElement& e, LayerParams& l)
{
    l.engine        = getE (e, "engine", l.engine, kNumEngines);
    l.name          = e.getStringAttribute ("name", juce::String (l.name)).toStdString();
    l.wtTable       = getI (e, "wtTable", l.wtTable, 0, kNumWavetables - 1);
    l.wtPosition    = getF (e, "wtPosition", l.wtPosition, 0.0f, 1.0f);
    l.fmAmount      = getF (e, "fmAmount", l.fmAmount, 0.0f, 2.0f);
    l.filter        = getE (e, "filter", l.filter, kNumFilterTypes);
    l.cutoff        = getF (e, "cutoff", l.cutoff, 20.0f, 20000.0f);
    l.resonance     = getF (e, "resonance", l.resonance, 0.0f, 1.0f);
    l.drive         = getF (e, "drive", l.drive, 0.0f, 1.0f);
    l.keyTrack      = getF (e, "keyTrack", l.keyTrack, 0.0f, 1.0f);
    l.filterEnvAmount = getF (e, "filterEnvAmount", l.filterEnvAmount, -1.0f, 1.0f);
    l.useAmpEnv     = getB (e, "useAmpEnv", l.useAmpEnv);
    l.level         = getF (e, "level", l.level, 0.0f, 1.0f);
    l.pan           = getF (e, "pan", l.pan, -1.0f, 1.0f);
    l.transpose     = getI (e, "transpose", l.transpose, -48, 48);
    l.fine          = getF (e, "fine", l.fine, -100.0f, 100.0f);
    l.keyLo         = getI (e, "keyLo", l.keyLo, 0, 127);
    l.keyHi         = getI (e, "keyHi", l.keyHi, 0, 127);
    l.velLo         = getI (e, "velLo", l.velLo, 0, 127);
    l.velHi         = getI (e, "velHi", l.velHi, 0, 127);
    l.velocitySens  = getF (e, "velocitySens", l.velocitySens, 0.0f, 1.0f);
    l.unison        = getI (e, "unison", l.unison, 1, kMaxUnison);
    l.unisonDetune  = getF (e, "unisonDetune", l.unisonDetune, 0.0f, 1.0f);
    l.unisonSpread  = getF (e, "unisonSpread", l.unisonSpread, 0.0f, 1.0f);
    l.glide         = getF (e, "glide", l.glide, 0.0f, 10.0f);
    l.mode          = getE (e, "mode", l.mode, kNumVoiceModes);
    l.polyphony     = getI (e, "polyphony", l.polyphony, 1, kMaxPolyphony);
    l.output        = getI (e, "output", l.output, 0, kOutputPairs - 1);

    if (auto* c = e.getChildByName ("Analog"))   readAnalog (*c, l.analog);
    if (auto* c = e.getChildByName ("Sampler"))  readSampler (*c, l.sampler);
    if (auto* c = e.getChildByName ("Pluck"))    readPluck (*c, l.pluck);
    if (auto* c = e.getChildByName ("AmpEnv"))   readEnv (*c, l.ampEnv);
    if (auto* c = e.getChildByName ("FilterEnv")) readEnv (*c, l.filterEnv);
    if (auto* c = e.getChildByName ("ModEnv"))   readEnv (*c, l.modEnv);

    if (auto* f = e.getChildByName ("Fm")) {
        juce::MemoryBlock mb;
        mb.loadFromHexString (f->getStringAttribute ("vced"));
        if (mb.getSize() == kVcedSize)
            l.fm = dx7FromVced ((const uint8_t*) mb.getData());
    }

    for (auto* c = e.getFirstChildElement(); c != nullptr; c = c->getNextElement()) {
        if (c->hasTagName ("Lfo")) {
            const int i = c->getIntAttribute ("index", -1);
            if (i >= 0 && i < kLayerLfos) readLfo (*c, l.lfo[i]);
        } else if (c->hasTagName ("Mod")) {
            const int i = c->getIntAttribute ("index", -1);
            if (i >= 0 && i < kMatrixSlots) readMod (*c, l.mods[i]);
        }
    }
}

// Sample and cart children sit under their Layer element but need the whole Patch to store into.
void readLayerSample (const juce::XmlElement& e, Patch& p, int i, juce::StringArray* warnings)
{
    const juce::String path = e.getStringAttribute ("path");
    const juce::File file (path);
    juce::String err;
    SamplePtr loaded = file.existsAsFile() ? loadSampleFile (file, err) : nullptr;

    if (! loaded) {
        p.samples[(size_t) i] = nullptr;
        if (warnings) {
            const juce::String name = e.getStringAttribute ("name", file.getFileName());
            warnings->add (layerLabel (i) + ": sample " + name + " not found");
        }
        return;
    }

    auto copy = std::make_shared<SampleData> (*loaded);
    copy->rootNote = getI (e, "rootNote", copy->rootNote, 0, 127);
    p.samples[(size_t) i] = copy;
}

void readLayerCart (const juce::XmlElement& e, Patch& p, int i, juce::StringArray* warnings)
{
    juce::MemoryBlock mb;
    auto voices = std::make_shared<std::vector<Dx7Patch>>();
    std::string err;
    const bool ok = mb.fromBase64Encoding (e.getAllSubText())
                 && parseDx7SysEx ((const uint8_t*) mb.getData(), mb.getSize(), *voices, err)
                 && ! voices->empty();

    if (! ok) {
        if (warnings) warnings->add (layerLabel (i) + ": cart " + e.getStringAttribute ("name") + " could not be read");
        return;
    }
    p.carts[(size_t) i] = voices;
    p.cartNames[(size_t) i] = e.getStringAttribute ("name").toStdString();
}

// ---- global effects -----------------------------------------------------------------------------
void writeChorus (juce::XmlElement& e, const ChorusParams& v)
{
    put (e, "on", v.on); put (e, "rate", v.rate); put (e, "depth", v.depth); put (e, "mix", v.mix);
}

void readChorus (const juce::XmlElement& e, ChorusParams& v)
{
    v.on    = getB (e, "on", v.on);
    v.rate  = getF (e, "rate", v.rate, 0.05f, 5.0f);
    v.depth = getF (e, "depth", v.depth, 0.0f, 1.0f);
    v.mix   = getF (e, "mix", v.mix, 0.0f, 1.0f);
}

void writeDelay (juce::XmlElement& e, const DelayParams& v)
{
    put (e, "on", v.on); put (e, "sync", v.sync); put (e, "beats", v.beats); put (e, "ms", v.ms);
    put (e, "feedback", v.feedback); put (e, "mix", v.mix); put (e, "tone", v.tone);
    put (e, "pingPong", v.pingPong);
}

void readDelay (const juce::XmlElement& e, DelayParams& v)
{
    v.on       = getB (e, "on", v.on);
    v.sync     = getB (e, "sync", v.sync);
    v.beats    = getF (e, "beats", v.beats, 0.0625f, 4.0f);
    v.ms       = getF (e, "ms", v.ms, 1.0f, 2000.0f);
    v.feedback = getF (e, "feedback", v.feedback, 0.0f, 0.95f);
    v.mix      = getF (e, "mix", v.mix, 0.0f, 1.0f);
    v.tone     = getF (e, "tone", v.tone, 0.0f, 1.0f);
    v.pingPong = getB (e, "pingPong", v.pingPong);
}

void writeReverb (juce::XmlElement& e, const ReverbParams& v)
{
    put (e, "on", v.on); put (e, "size", v.size); put (e, "damping", v.damping); put (e, "mix", v.mix);
    put (e, "predelayMs", v.predelayMs); put (e, "width", v.width);
}

void readReverb (const juce::XmlElement& e, ReverbParams& v)
{
    v.on         = getB (e, "on", v.on);
    v.size       = getF (e, "size", v.size, 0.0f, 1.0f);
    v.damping    = getF (e, "damping", v.damping, 0.0f, 1.0f);
    v.mix        = getF (e, "mix", v.mix, 0.0f, 1.0f);
    v.predelayMs = getF (e, "predelayMs", v.predelayMs, 0.0f, 200.0f);
    v.width      = getF (e, "width", v.width, 0.0f, 1.0f);
}

} // namespace

std::unique_ptr<juce::XmlElement> patchToXml (const Patch& p)
{
    auto root = std::make_unique<juce::XmlElement> ("ChimeraPatch");
    root->setAttribute ("version", 1);
    put (*root, "name", p.name);
    put (*root, "category", p.category);
    put (*root, "master", p.masterVolume);
    put (*root, "bendRange", p.bendRange);
    for (int i = 0; i < kMacros; ++i) put (*root, kMacroAttr[i], p.macroNames[(size_t) i]);

    for (int i = 0; i < kMaxLayers; ++i) {
        const auto idx = (size_t) i;
        auto* layer = root->createNewChildElement ("Layer");
        layer->setAttribute ("index", i);
        writeLayer (*layer, p.layers[idx]);

        if (const auto& s = p.samples[idx]) {
            auto* c = layer->createNewChildElement ("Sample");
            put (*c, "path", s->path);
            put (*c, "name", s->name);
            c->setAttribute ("rootNote", s->rootNote);
        }

        if (const auto& cart = p.carts[idx]; cart && ! cart->empty()) {
            auto* c = layer->createNewChildElement ("Cart");
            put (*c, "name", p.cartNames[idx]);
            const auto bytes = dx7MakeCart (*cart);
            const juce::MemoryBlock mb (bytes.data(), bytes.size());
            c->addTextElement (mb.toBase64Encoding());
        }
    }

    for (int i = 0; i < kGlobalLfos; ++i) {
        auto* c = root->createNewChildElement ("GlobalLfo");
        c->setAttribute ("index", i);
        writeLfo (*c, p.globalLfo[i]);
    }
    writeChorus (*root->createNewChildElement ("Chorus"), p.chorus);
    writeDelay (*root->createNewChildElement ("Delay"), p.delay);
    writeReverb (*root->createNewChildElement ("Reverb"), p.reverb);

    return root;
}

Patch patchFromXml (const juce::XmlElement& x, juce::StringArray* warnings)
{
    Patch p;   // defaults; every stored value overrides them below
    p.name     = x.getStringAttribute ("name", juce::String (p.name)).toStdString();
    p.category = x.getStringAttribute ("category", juce::String (p.category)).toStdString();
    p.masterVolume = getF (x, "master", p.masterVolume, 0.0f, 1.0f);
    p.bendRange    = getF (x, "bendRange", p.bendRange, 0.0f, 24.0f);
    for (int i = 0; i < kMacros; ++i)
        p.macroNames[(size_t) i] = x.getStringAttribute (kMacroAttr[i], juce::String (p.macroNames[(size_t) i])).toStdString();

    for (const auto* c = x.getFirstChildElement(); c != nullptr; c = c->getNextElement()) {
        if (c->hasTagName ("Layer")) {
            const int i = c->getIntAttribute ("index", -1);
            if (i < 0 || i >= kMaxLayers) continue;
            readLayer (*c, p.layers[(size_t) i]);
            if (auto* s = c->getChildByName ("Sample")) readLayerSample (*s, p, i, warnings);
            if (auto* k = c->getChildByName ("Cart"))   readLayerCart (*k, p, i, warnings);
        } else if (c->hasTagName ("GlobalLfo")) {
            const int i = c->getIntAttribute ("index", -1);
            if (i >= 0 && i < kGlobalLfos) readLfo (*c, p.globalLfo[i]);
        } else if (c->hasTagName ("Chorus")) {
            readChorus (*c, p.chorus);
        } else if (c->hasTagName ("Delay")) {
            readDelay (*c, p.delay);
        } else if (c->hasTagName ("Reverb")) {
            readReverb (*c, p.reverb);
        }
    }

    return p;
}

} // namespace el::chimera
