#include "Synth/plugin/ChimeraProcessor.h"
#if ! ELOOPER_HEADLESS
 #include "Synth/ui/ChimeraEditor.h"
#endif

namespace el::chimera {

namespace {
    juce::String layerId (int layer, const char* what) { return "l" + juce::String (layer + 1) + what; }

    juce::AudioParameterFloat* addFloat (ChimeraProcessor& p, const juce::String& id, const juce::String& name,
                                         juce::NormalisableRange<float> range, float def,
                                         std::function<juce::String (float, int)> toText = nullptr)
    {
        auto attrs = juce::AudioParameterFloatAttributes();
        if (toText) attrs = attrs.withStringFromValueFunction (std::move (toText));
        auto* param = new juce::AudioParameterFloat (juce::ParameterID { id, 1 }, name, range, def, attrs);
        p.juce::AudioProcessor::addParameter (param);   // AudioPluginInstance hides it; the base one is fine for us
        return param;
    }
}

juce::PluginDescription ChimeraProcessor::makeDescription()
{
    juce::PluginDescription d;
    d.name = "Chimera";
    d.descriptiveName = "Chimera multi-engine synthesizer (built in)";
    d.pluginFormatName = kFormatName;
    d.category = "Synth";
    d.manufacturerName = "ELooper";
    d.version = "1.0";
    d.fileOrIdentifier = kIdentifier;
    d.uniqueId = 0x43484d52;   // 'CHMR'
    d.deprecatedUid = d.uniqueId;
    d.isInstrument = true;
    d.numInputChannels = 0;
    d.numOutputChannels = 2 * kOutputPairs;
    d.hasSharedContainer = false;
    return d;
}

namespace { const std::vector<Patch>& factory(); }

ChimeraProcessor::ChimeraProcessor()
    : juce::AudioPluginInstance (BusesProperties()
          .withOutput ("Main", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Out 3-4", juce::AudioChannelSet::stereo(), false)
          .withOutput ("Out 5-6", juce::AudioChannelSet::stereo(), false)
          .withOutput ("Out 7-8", juce::AudioChannelSet::stereo(), false))
{
    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
    for (int i = 0; i < kMacros; ++i)
        macros[(size_t) i] = addFloat (*this, "macro" + juce::String (i + 1), "Macro " + juce::String (i + 1), { 0.0f, 1.0f }, 0.0f, pct);
    master = addFloat (*this, "master", "Master Volume", { 0.0f, 1.0f }, 0.7f, pct);
    for (int l = 0; l < kMaxLayers; ++l) {
        const auto ln = "Layer " + juce::String (l + 1) + " ";
        level[(size_t) l] = addFloat (*this, layerId (l, "level"), ln + "Level", { 0.0f, 1.0f }, 0.75f, pct);
        juce::NormalisableRange<float> cutRange (20.0f, 20000.0f);
        cutRange.setSkewForCentre (1000.0f);
        cutoff[(size_t) l] = addFloat (*this, layerId (l, "cutoff"), ln + "Cutoff", cutRange, 12000.0f,
                                       [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; });
        resonance[(size_t) l] = addFloat (*this, layerId (l, "reso"), ln + "Resonance", { 0.0f, 1.0f }, 0.15f, pct);
        pan[(size_t) l] = addFloat (*this, layerId (l, "pan"), ln + "Pan", { -1.0f, 1.0f }, 0.0f,
                                    [] (float v, int) { const int p = juce::roundToInt (std::abs (v) * 50.0f); return p == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (p); });
    }
    midiScratch.resize (4096);
    WavetableBank::get();   // build the tables now, off the audio thread

    if (! factory().empty()) working = factory().front();
    syncParamsFromPatch();
    publish();
}

ChimeraProcessor::~ChimeraProcessor() = default;

bool ChimeraProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    if (layout.outputBuses.isEmpty() || layout.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    // Enabled aux pairs must be a contiguous run after Main: the synth maps output pair N to channels 2N, 2N+1.
    bool gap = false;
    for (int i = 1; i < layout.outputBuses.size(); ++i) {
        const auto& set = layout.outputBuses.getReference (i);
        if (set.isDisabled()) { gap = true; continue; }
        if (gap || set != juce::AudioChannelSet::stereo()) return false;
    }
    return layout.inputBuses.isEmpty() || layout.getMainInputChannelSet().isDisabled();
}

void ChimeraProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    synth.prepare (sampleRate, juce::jmax (32, maximumExpectedSamplesPerBlock));
}

void ChimeraProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);
    int numEvents = 0;
    const int cap = (int) midiScratch.size();
    for (const auto meta : midi) {
        if (numEvents >= cap) {   // too dense to keep: end on all-notes-off so nothing hangs
            midiScratch[(size_t) cap - 1] = { buffer.getNumSamples() - 1, 0xB0, 123, 0 };
            break;
        }
        const auto* d = meta.data;
        if (meta.numBytes < 1 || meta.numBytes > 3) continue;
        midiScratch[(size_t) numEvents++] = { juce::jlimit (0, buffer.getNumSamples() - 1, meta.samplePosition),
                                              d[0], (uint8_t) (meta.numBytes > 1 ? d[1] : 0), (uint8_t) (meta.numBytes > 2 ? d[2] : 0) };
    }
    midi.clear();

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;

    LiveParams live;
    for (int i = 0; i < kMacros; ++i) live.macros[i] = macros[(size_t) i]->get();
    live.master = master->get();
    for (int l = 0; l < kMaxLayers; ++l) {
        live.level[l] = level[(size_t) l]->get();
        live.cutoff[l] = cutoff[(size_t) l]->get();
        live.resonance[l] = resonance[(size_t) l]->get();
        live.pan[l] = pan[(size_t) l]->get();
    }

    float* outs[2 * kOutputPairs] {};
    const int ch = juce::jmin (buffer.getNumChannels(), 2 * kOutputPairs);
    for (int c = 0; c < ch; ++c) outs[c] = buffer.getWritePointer (c);
    for (int c = ch; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
    synth.process (outs, ch, buffer.getNumSamples(), midiScratch.data(), numEvents, bpm, live);
}

// ---- patch editing ------------------------------------------------------------------------------
void ChimeraProcessor::publish()
{
    synth.patches().publish (std::make_shared<const Patch> (working));
}

void ChimeraProcessor::syncParamsFromPatch()
{
    auto set = [] (juce::AudioParameterFloat* p, float v) {
        if (p && std::abs (p->get() - v) > 1.0e-6f) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    set (master, working.masterVolume);
    for (int l = 0; l < kMaxLayers; ++l) {
        const auto& lp = working.layers[(size_t) l];
        set (level[(size_t) l], lp.level);
        set (cutoff[(size_t) l], juce::jlimit (20.0f, 20000.0f, lp.cutoff));
        set (resonance[(size_t) l], lp.resonance);
        set (pan[(size_t) l], lp.pan);
    }
}

void ChimeraProcessor::syncPatchFromParams()
{
    working.masterVolume = master->get();
    for (int l = 0; l < kMaxLayers; ++l) {
        auto& lp = working.layers[(size_t) l];
        lp.level = level[(size_t) l]->get();
        lp.cutoff = cutoff[(size_t) l]->get();
        lp.resonance = resonance[(size_t) l]->get();
        lp.pan = pan[(size_t) l]->get();
    }
}

void ChimeraProcessor::editPatch (const std::function<void (Patch&)>& fn)
{
    syncPatchFromParams();
    fn (working);
    syncParamsFromPatch();
    publish();
    sendChangeMessage();
}

void ChimeraProcessor::setPatch (const Patch& p)
{
    working = p;
    syncParamsFromPatch();
    publish();
    sendChangeMessage();
}

// ---- samples / carts ----------------------------------------------------------------------------
SamplePtr loadSampleFile (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (! reader) { error = "Can't read " + file.getFileName() + " (WAV, AIFF, FLAC, Ogg or MP3)."; return nullptr; }
    const auto maxLen = (juce::int64) (reader->sampleRate * 120.0);   // 2 minutes is plenty for a sampler layer
    const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
    if (len < 4) { error = file.getFileName() + " is empty."; return nullptr; }
    juce::AudioBuffer<float> buf ((int) juce::jmin (2u, reader->numChannels), len);
    reader->read (&buf, 0, len, 0, true, buf.getNumChannels() > 1);
    auto s = std::make_shared<SampleData>();
    s->left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
    if (buf.getNumChannels() > 1) s->right.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len);
    s->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    s->name = file.getFileName().toStdString();
    s->path = file.getFullPathName().toStdString();
    // Root note from the file's metadata if it has one (WAV smpl / AIFF basc), else C3 (60).
    const auto root = reader->metadataValues.getValue ("MidiUnityNote", reader->metadataValues.getValue ("BaseNote", "60"));
    s->rootNote = juce::jlimit (0, 127, root.getIntValue() > 0 ? root.getIntValue() : 60);
    return s;
}

juce::String ChimeraProcessor::loadSample (int layer, const juce::File& file)
{
    if (layer < 0 || layer >= kMaxLayers) return "Bad layer.";
    juce::String err;
    auto s = loadSampleFile (file, err);
    if (! s) return err;
    editPatch ([&] (Patch& p) {
        p.samples[(size_t) layer] = s;
        auto& lp = p.layers[(size_t) layer];
        lp.engine = Engine::Sampler;
        if (lp.name.empty()) lp.name = file.getFileNameWithoutExtension().toStdString();
    });
    return {};
}

juce::String ChimeraProcessor::loadCart (int layer, const juce::File& file)
{
    if (layer < 0 || layer >= kMaxLayers) return "Bad layer.";
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) return "Can't read " + file.getFileName() + ".";
    auto voices = std::make_shared<std::vector<Dx7Patch>>();
    std::string err;
    if (! parseDx7SysEx ((const uint8_t*) mb.getData(), mb.getSize(), *voices, err) || voices->empty())
        return file.getFileName() + " is not a DX7 voice or cartridge file" + (err.empty() ? juce::String (".") : ": " + juce::String (err) + ".");
    editPatch ([&] (Patch& p) {
        p.carts[(size_t) layer] = voices;
        p.cartNames[(size_t) layer] = file.getFileNameWithoutExtension().toStdString();
        auto& lp = p.layers[(size_t) layer];
        lp.engine = Engine::FM;
        lp.fm = voices->front();
        lp.useAmpEnv = false;
    });
    return {};
}

void ChimeraProcessor::selectCartVoice (int layer, int index)
{
    if (layer < 0 || layer >= kMaxLayers) return;
    const auto cart = working.carts[(size_t) layer];
    if (! cart || index < 0 || index >= (int) cart->size()) return;
    editPatch ([&] (Patch& p) { p.layers[(size_t) layer].fm = (*cart)[(size_t) index]; p.layers[(size_t) layer].engine = Engine::FM; });
}

juce::String ChimeraProcessor::saveCart (int layer, const juce::File& file)
{
    if (layer < 0 || layer >= kMaxLayers) return "Bad layer.";
    std::vector<Dx7Patch> voices;
    if (auto c = working.carts[(size_t) layer]) voices = *c;
    if (voices.empty()) voices.push_back (working.layers[(size_t) layer].fm);
    const auto bytes = dx7MakeCart (voices);
    if (! file.replaceWithData (bytes.data(), bytes.size())) return "Can't write " + file.getFullPathName() + ".";
    return {};
}

void ChimeraProcessor::loadBuiltinCart (int layer) { loadLibraryCart (layer, 0); }

void ChimeraProcessor::loadLibraryCart (int layer, int cart, int voice)
{
    if (layer < 0 || layer >= kMaxLayers) return;
    cart = juce::jlimit (0, fmLibCartCount() - 1, cart);
    auto voices = std::make_shared<std::vector<Dx7Patch>> (fmLibCart (cart));
    if (voices->empty()) return;
    voice = juce::jlimit (0, (int) voices->size() - 1, voice);
    editPatch ([&] (Patch& p) {
        p.carts[(size_t) layer] = voices;
        p.cartNames[(size_t) layer] = fmLibCartName (cart);
        auto& lp = p.layers[(size_t) layer];
        lp.engine = Engine::FM;
        lp.fm = (*voices)[(size_t) voice];
        lp.useAmpEnv = false;
    });
}

int ChimeraProcessor::libraryCartIndex (int layer) const
{
    if (layer < 0 || layer >= kMaxLayers || ! working.carts[(size_t) layer]) return -1;
    return fmLibFindCart (working.cartNames[(size_t) layer]);
}

void ChimeraProcessor::stepLibraryCart (int layer, int delta)
{
    const int n = fmLibCartCount(), cur = libraryCartIndex (layer);
    loadLibraryCart (layer, cur < 0 ? 0 : ((cur + delta) % n + n) % n);
}

int ChimeraProcessor::randomLibraryVoice (int layer, bool sameCategory)
{
    if (layer < 0 || layer >= kMaxLayers) return -1;
    auto& rng = juce::Random::getSystemRandom();
    const int cur = libraryCartIndex (layer);
    int cart = 0;
    if (sameCategory && cur >= 0) {
        const int c = fmLibCartCategory (cur);
        cart = fmLibCategoryFirstCart (c) + rng.nextInt (fmLibCategoryCarts (c));
    } else {
        // every voice equally likely: the last cart of a category can be short
        int v = rng.nextInt (fmLibVoiceCount());
        for (cart = 0; cart < fmLibCartCount() - 1; ++cart) {
            const int size = (int) fmLibCart (cart).size();
            if (v < size) break;
            v -= size;
        }
    }
    const int voice = rng.nextInt ((int) fmLibCart (cart).size());
    loadLibraryCart (layer, cart, voice);
    return voice;
}

int ChimeraProcessor::randomCartVoice (int layer)
{
    if (layer < 0 || layer >= kMaxLayers) return -1;
    if (! working.carts[(size_t) layer] || working.carts[(size_t) layer]->empty()) loadBuiltinCart (layer);
    const auto cart = working.carts[(size_t) layer];
    if (! cart || cart->empty()) return -1;
    const int i = juce::Random::getSystemRandom().nextInt ((int) cart->size());
    selectCartVoice (layer, i);
    return i;
}

namespace {
    juce::File chimeraDataDir()
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        base = base.getChildFile ("Application Support");   // userApplicationDataDirectory is ~/Library on macOS
       #endif
        return base.getChildFile ("ELooper");
    }
}

// ---- presets ------------------------------------------------------------------------------------
namespace {
    const std::vector<Patch>& factory()
    {
        static const std::vector<Patch> list = makeFactoryPresets();
        return list;
    }
}

int ChimeraProcessor::numFactoryPresets() { return (int) factory().size(); }
juce::String ChimeraProcessor::factoryPresetName (int i)
{
    return i >= 0 && i < numFactoryPresets() ? juce::String (factory()[(size_t) i].name) : juce::String();
}
juce::String ChimeraProcessor::factoryPresetCategory (int i)
{
    return i >= 0 && i < numFactoryPresets() ? juce::String (factory()[(size_t) i].category) : juce::String();
}

void ChimeraProcessor::loadFactoryPreset (int i)
{
    if (i < 0 || i >= numFactoryPresets()) return;
    currentProgram = i;
    setPatch (factory()[(size_t) i]);
    for (auto* m : macros) m->setValueNotifyingHost (0.0f);   // presets start from neutral macros
}

juce::String ChimeraProcessor::randomPreset()
{
    auto& rnd = juce::Random::getSystemRandom();
    const auto user = presetFolder().findChildFiles (juce::File::findFiles, false, "*.chimera");
    const int total = numFactoryPresets() + user.size();
    if (total <= 1) return {};
    int pick = rnd.nextInt (total);
    if (pick == currentProgram && total > 1) pick = (pick + 1 + rnd.nextInt (total - 1)) % total;   // never the same one twice
    if (pick < numFactoryPresets()) { loadFactoryPreset (pick); return factoryPresetName (pick); }
    const auto& f = user[pick - numFactoryPresets()];
    if (loadPreset (f).isEmpty()) return f.getFileNameWithoutExtension();
    loadFactoryPreset (rnd.nextInt (numFactoryPresets()));
    return factoryPresetName (currentProgram);
}

void ChimeraProcessor::randomisePatch()
{
    const auto seed = (uint32_t) juce::Random::getSystemRandom().nextInt (1 << 30);
    setPatch (randomPatch (seed, chimeraCart()));
    for (auto* m : macros) m->setValueNotifyingHost (0.0f);
}

juce::File ChimeraProcessor::presetFolder()
{
    auto dir = chimeraDataDir().getChildFile ("Chimera Presets");
    dir.createDirectory();
    return dir;
}

juce::String ChimeraProcessor::savePreset (const juce::File& file)
{
    syncPatchFromParams();
    auto xml = patchToXml (working);
    for (int i = 0; i < kMacros; ++i) xml->setAttribute ("macro" + juce::String (i + 1), macros[(size_t) i]->get());
    if (! xml->writeTo (file)) return "Can't write " + file.getFullPathName() + ".";
    return {};
}

juce::String ChimeraProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (! xml || ! xml->hasTagName ("ChimeraPatch")) return file.getFileName() + " is not a Chimera preset.";
    juce::StringArray warnings;
    auto p = patchFromXml (*xml, &warnings);
    if (p.name.empty() || p.name == "Init") p.name = file.getFileNameWithoutExtension().toStdString();
    setPatch (p);
    for (int i = 0; i < kMacros; ++i)
        macros[(size_t) i]->setValueNotifyingHost ((float) xml->getDoubleAttribute ("macro" + juce::String (i + 1), 0.0));
    return warnings.joinIntoString ("\n");
}

// ---- state --------------------------------------------------------------------------------------
void ChimeraProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    syncPatchFromParams();
    auto xml = patchToXml (working);
    for (int i = 0; i < kMacros; ++i) xml->setAttribute ("macro" + juce::String (i + 1), macros[(size_t) i]->get());
    xml->setAttribute ("program", currentProgram);
    copyXmlToBinary (*xml, dest);
}

void ChimeraProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml || ! xml->hasTagName ("ChimeraPatch")) return;
    currentProgram = xml->getIntAttribute ("program", 0);
    working = patchFromXml (*xml);
    syncParamsFromPatch();
    for (int i = 0; i < kMacros; ++i)
        macros[(size_t) i]->setValueNotifyingHost ((float) xml->getDoubleAttribute ("macro" + juce::String (i + 1), 0.0));
    publish();
    sendChangeMessage();
}

juce::AudioProcessorEditor* ChimeraProcessor::createEditor()
{
#if ELOOPER_HEADLESS
    return new juce::GenericAudioProcessorEditor (*this);
#else
    return new ChimeraEditor (*this);
#endif
}

} // namespace el::chimera
