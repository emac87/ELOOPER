#include "Engine/SessionSerializer.h"
#include "Engine/AudioEngine.h"
#include "Engine/PresetManager.h"

namespace el {

namespace {

// ---- attribute helpers (full-precision floating point) -----------------------------------------
void setD (juce::XmlElement& e, const char* name, double v) { e.setAttribute (name, juce::String (v, 12)); }
void setF (juce::XmlElement& e, const char* name, float v)  { e.setAttribute (name, juce::String ((double) v, 9)); }
Id   getId (const juce::XmlElement& e, const char* name)    { return (Id) e.getStringAttribute (name).getLargeIntValue(); }
void setId (juce::XmlElement& e, const char* name, Id v)    { e.setAttribute (name, juce::String ((juce::int64) v)); }

template <typename Enum>
Enum getEnum (const juce::XmlElement& e, const char* name, Enum def, int maxValue)
{
    return (Enum) juce::jlimit (0, maxValue, e.getIntAttribute (name, (int) def));
}

// ---- LoopSettings ---------------------------------------------------------------------------------
void writeLoopSettings (juce::XmlElement& e, const LoopSettings& s)
{
    e.setAttribute ("startQuantize", (int) s.startQuantize);
    e.setAttribute ("stopQuantize", (int) s.stopQuantize);
    e.setAttribute ("lengthMode", (int) s.lengthMode);
    e.setAttribute ("recordBars", (int) s.recordBars);
    e.setAttribute ("afterRecord", (int) s.afterRecord);
    e.setAttribute ("playMode", (int) s.playMode);
    e.setAttribute ("varispeed", s.varispeed);
    e.setAttribute ("recordMode", (int) s.recordMode);
    e.setAttribute ("cycleTakes", s.cycleTakes);
    setF (e, "overdubFeedback", s.overdubFeedback);
}

LoopSettings readLoopSettings (const juce::XmlElement& e)
{
    LoopSettings s;
    s.startQuantize = getEnum (e, "startQuantize", s.startQuantize, (int) Quantize::Master);
    s.stopQuantize  = getEnum (e, "stopQuantize", s.stopQuantize, (int) Quantize::Master);
    s.lengthMode    = getEnum (e, "lengthMode", s.lengthMode, (int) RecordLength::Free);
    s.recordBars    = (int16_t) e.getIntAttribute ("recordBars", s.recordBars);
    s.afterRecord   = getEnum (e, "afterRecord", s.afterRecord, (int) AfterRecord::Stop);
    s.playMode      = getEnum (e, "playMode", s.playMode, (int) PlayMode::OneShot);
    s.varispeed     = e.getBoolAttribute ("varispeed", false);
    s.recordMode    = getEnum (e, "recordMode", s.recordMode, (int) RecordMode::TakeFolder);
    s.cycleTakes    = e.getBoolAttribute ("cycleTakes", s.cycleTakes);
    s.overdubFeedback = (float) e.getDoubleAttribute ("overdubFeedback", s.overdubFeedback);
    return s;
}

// ---- automation -------------------------------------------------------------------------------------
void writeLane (juce::XmlElement& parent, const AutomationLane& lane)
{
    auto* e = parent.createNewChildElement ("LANE");
    e->setAttribute ("kind", (int) lane.target.kind);
    setId (*e, "objectId", lane.target.objectId);
    e->setAttribute ("index", (int) lane.target.index);
    e->setAttribute ("enabled", lane.enabled);
    for (const auto& p : lane.points)
    {
        auto* pe = e->createNewChildElement ("P");
        setD (*pe, "beat", p.beat);
        setF (*pe, "value", p.value);
        setF (*pe, "curve", p.curve);
    }
}

AutomationLane readLane (const juce::XmlElement& e)
{
    AutomationLane lane;
    lane.target.kind = getEnum (e, "kind", ParamKind::Volume, (int) ParamKind::LoopVolume);
    lane.target.objectId = getId (e, "objectId");
    lane.target.index = e.getIntAttribute ("index", -1);
    lane.enabled = e.getBoolAttribute ("enabled", true);
    for (auto* pe : e.getChildWithTagNameIterator ("P"))
    {
        AutoPoint p;
        p.beat = pe->getDoubleAttribute ("beat");
        p.value = (float) pe->getDoubleAttribute ("value");
        p.curve = (float) pe->getDoubleAttribute ("curve");
        lane.points.push_back (p);
    }
    lane.sort();
    return lane;
}

// ---- plugin slots ------------------------------------------------------------------------------------
void writeSlot (juce::XmlElement& parent, const PluginSlot& s)
{
    auto* e = parent.createNewChildElement ("SLOT");
    setId (*e, "id", s.id);
    e->setAttribute ("bypassed", s.bypassed.load());
    e->setAttribute ("numOutputs", s.numOutputChannels);

    juce::MemoryBlock state;
    if (s.instance != nullptr) s.instance->getStateInformation (state);
    else state = s.pendingState;
    e->setAttribute ("state", state.toBase64Encoding());

    if (auto d = PresetManager::descriptionToXml (s.description))
        e->addChildElement (d.release());
}

PluginSlotPtr readSlot (const juce::XmlElement& e)
{
    auto s = std::make_shared<PluginSlot>();
    s->id = getId (e, "id");
    s->bypassed = e.getBoolAttribute ("bypassed");
    s->numOutputChannels = e.getIntAttribute ("numOutputs", 2);
    auto st = e.getStringAttribute ("state");
    if (st.isNotEmpty()) s->pendingState.fromBase64Encoding (st);
    if (auto* d = e.getChildByName ("PLUGIN"))
        s->description = PresetManager::descriptionFromXml (*d);
    return s;
}

void writeChain (juce::XmlElement& track, const char* tag, const std::vector<PluginSlotPtr>& chain)
{
    auto* c = track.createNewChildElement (tag);
    for (const auto& s : chain) if (s) writeSlot (*c, *s);
}

void readChain (const juce::XmlElement& track, const char* tag, std::vector<PluginSlotPtr>& chain)
{
    if (auto* c = track.getChildByName (tag))
        for (auto* se : c->getChildWithTagNameIterator ("SLOT"))
            chain.push_back (readSlot (*se));
}

// ---- audio files ------------------------------------------------------------------------------------------
juce::String takeFileName (Id loopId, int take) { return "loop_" + juce::String ((juce::int64) loopId) + "_take" + juce::String (take) + ".wav"; }

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0) return false;
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, (unsigned) buffer.getNumChannels(), 32, {}, 0));
    if (writer == nullptr) return false;
    stream.release();   // owned by the writer now
    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

// Reads a WAV and resamples (linear) to targetRate if it was stored at a different rate.
bool readWav (const juce::File& file, double targetRate, juce::AudioBuffer<float>& out)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr || reader->lengthInSamples <= 0) return false;

    const int n = (int) reader->lengthInSamples;
    const int ch = (int) juce::jmin (2u, reader->numChannels);
    juce::AudioBuffer<float> raw (ch, n);
    if (! reader->read (&raw, 0, n, 0, true, ch > 1)) return false;

    if (targetRate <= 0 || std::abs (reader->sampleRate - targetRate) < 0.5)
    {
        out = std::move (raw);
        return true;
    }
    const double ratio = reader->sampleRate / targetRate;
    const int m = (int) std::floor ((double) n / ratio);
    out.setSize (ch, m);
    for (int c = 0; c < ch; ++c)
    {
        const float* src = raw.getReadPointer (c);
        float* dst = out.getWritePointer (c);
        for (int i = 0; i < m; ++i)
        {
            const double pos = (double) i * ratio;
            const int i0 = juce::jmin (n - 1, (int) pos);
            const int i1 = juce::jmin (n - 1, i0 + 1);
            const float f = (float) (pos - i0);
            dst[i] = src[i0] + (src[i1] - src[i0]) * f;
        }
    }
    return true;
}

// ---- MIDI --------------------------------------------------------------------------------------------------------
void writeMidiTake (juce::XmlElement& parent, const MidiClip& clip)
{
    auto* e = parent.createNewChildElement ("MIDITAKE");
    setD (*e, "lengthBeats", clip.lengthBeats);
    setD (*e, "stepBeats", clip.stepBeats);
    for (const auto& n : clip.notes)
    {
        auto* ne = e->createNewChildElement ("NOTE");
        setD (*ne, "start", n.start);
        setD (*ne, "length", n.length);
        ne->setAttribute ("pitch", (int) n.pitch);
        ne->setAttribute ("velocity", (int) n.velocity);
        ne->setAttribute ("channel", (int) n.channel);
        ne->setAttribute ("muted", n.muted);
        if (n.ratchet > 1) ne->setAttribute ("ratchet", (int) n.ratchet);
    }
    if (! clip.stepMapName.empty()) e->setAttribute ("stepMap", juce::String (clip.stepMapName));
    for (const auto& l : clip.stepLanes)
    {
        auto* le = e->createNewChildElement ("LANE");
        le->setAttribute ("pitch", (int) l.pitch);
        le->setAttribute ("name", juce::String (l.name));
    }
    for (const auto& c : clip.controllers)
    {
        auto* ce = e->createNewChildElement ("CC");
        setD (*ce, "beat", c.beat);
        ce->setAttribute ("size", (int) c.size);
        ce->setAttribute ("d0", (int) c.data[0]);
        ce->setAttribute ("d1", (int) c.data[1]);
        ce->setAttribute ("d2", (int) c.data[2]);
    }
}

MidiClip readMidiTake (const juce::XmlElement& e)
{
    MidiClip clip;
    clip.lengthBeats = e.getDoubleAttribute ("lengthBeats", 16);
    clip.stepBeats = e.getDoubleAttribute ("stepBeats", 0.25);
    for (auto* ne : e.getChildWithTagNameIterator ("NOTE"))
    {
        MidiNote n;
        n.start = ne->getDoubleAttribute ("start");
        n.length = ne->getDoubleAttribute ("length", 0.25);
        n.pitch = (uint8_t) juce::jlimit (0, 127, ne->getIntAttribute ("pitch", 60));
        n.velocity = (uint8_t) juce::jlimit (0, 127, ne->getIntAttribute ("velocity", 100));
        n.channel = (uint8_t) juce::jlimit (1, 16, ne->getIntAttribute ("channel", 1));
        n.muted = ne->getBoolAttribute ("muted");
        n.ratchet = (uint8_t) juce::jlimit (1, 8, ne->getIntAttribute ("ratchet", 1));
        clip.notes.push_back (n);
    }
    clip.stepMapName = e.getStringAttribute ("stepMap").toStdString();
    for (auto* le : e.getChildWithTagNameIterator ("LANE"))
    {
        StepLane l;
        l.pitch = (uint8_t) juce::jlimit (0, 127, le->getIntAttribute ("pitch", 36));
        l.name = le->getStringAttribute ("name").toStdString();
        clip.stepLanes.push_back (l);
    }
    for (auto* ce : e.getChildWithTagNameIterator ("CC"))
    {
        MidiEvent ev;
        ev.beat = ce->getDoubleAttribute ("beat");
        ev.size = (uint8_t) juce::jlimit (1, 3, ce->getIntAttribute ("size", 3));
        ev.data[0] = (uint8_t) ce->getIntAttribute ("d0");
        ev.data[1] = (uint8_t) ce->getIntAttribute ("d1");
        ev.data[2] = (uint8_t) ce->getIntAttribute ("d2");
        clip.controllers.push_back (ev);
    }
    return clip;
}

// ---- loops -----------------------------------------------------------------------------------------------------------
// Returns false (and appends to `errors`) if an audio file could not be written.
void writeLoop (EngineController& c, juce::XmlElement& parent, const LoopModel& l, const juce::File& audioDir,
                double sampleRate, juce::StringArray& errors)
{
    auto* e = parent.createNewChildElement ("LOOP");
    setId (*e, "id", l.id);
    e->setAttribute ("type", (int) l.type);
    e->setAttribute ("name", l.name);
    if (l.colour) e->setAttribute ("colour", l.colour->toString());
    setF (*e, "volume", l.volume);
    setD (*e, "automationLengthBeats", l.automationLengthBeats);
    writeLoopSettings (*e->createNewChildElement ("SETTINGS"), l.settings);

    for (const auto& clip : l.midiTakes) writeMidiTake (*e, clip);
    for (const auto& lane : l.automation) writeLane (*e, lane);

    if (l.rt != nullptr)
    {
        e->setAttribute ("activeTake", l.rt->activeTakeIndex());
        for (int k = 0; k < l.rt->takeCount(); ++k)
        {
            juce::AudioBuffer<float> buf;
            double beats = 0;
            if (! c.exportTakeAudio (l.id, k, buf, beats)) continue;
            const auto name = takeFileName (l.id, k);
            if (! writeWav (audioDir.getChildFile (name), buf, sampleRate))
            {
                errors.add ("Could not write " + name);
                continue;
            }
            auto* te = e->createNewChildElement ("AUDIOTAKE");
            te->setAttribute ("index", k);
            te->setAttribute ("file", name);
            setD (*te, "beats", beats);
        }
    }
}

LoopPtr readLoop (const juce::XmlElement& e, Id trackId, const juce::File& audioDir, double sampleRate,
                  juce::StringArray& errors)
{
    auto l = std::make_shared<LoopModel>();
    l->id = getId (e, "id");
    l->trackId = trackId;
    l->type = getEnum (e, "type", LoopType::Audio, (int) LoopType::Automation);
    l->name = e.getStringAttribute ("name");
    if (e.hasAttribute ("colour")) l->colour = juce::Colour::fromString (e.getStringAttribute ("colour"));
    l->volume = (float) e.getDoubleAttribute ("volume", 1.0);
    l->automationLengthBeats = e.getDoubleAttribute ("automationLengthBeats", 16);
    if (auto* s = e.getChildByName ("SETTINGS")) l->settings = readLoopSettings (*s);
    l->pendingActiveTake = e.getIntAttribute ("activeTake", 0);

    for (auto* m : e.getChildWithTagNameIterator ("MIDITAKE")) l->midiTakes.push_back (readMidiTake (*m));
    for (auto* a : e.getChildWithTagNameIterator ("LANE")) l->automation.push_back (readLane (*a));

    for (auto* te : e.getChildWithTagNameIterator ("AUDIOTAKE"))
    {
        LoopModel::PendingTake take;
        take.beats = te->getDoubleAttribute ("beats");
        if (readWav (audioDir.getChildFile (te->getStringAttribute ("file")), sampleRate, take.audio))
            l->pendingAudio.push_back (std::move (take));
        else
        {
            errors.add ("Missing or unreadable audio: " + te->getStringAttribute ("file"));
            // Keep take indices aligned with an empty placeholder is not possible; drop it.
        }
    }
    l->pendingActiveTake = juce::jlimit (0, juce::jmax (0, (int) l->pendingAudio.size() - 1), l->pendingActiveTake);
    return l;
}

// ---- tracks -------------------------------------------------------------------------------------------------------------
void writeTrack (EngineController& c, juce::XmlElement& parent, const TrackModel& t, const juce::File& audioDir,
                 double sampleRate, juce::StringArray& errors)
{
    auto* e = parent.createNewChildElement ("TRACK");
    setId (*e, "id", t.id);
    e->setAttribute ("kind", (int) t.kind);
    e->setAttribute ("name", t.name);
    e->setAttribute ("colour", t.colour.toString());
    e->setAttribute ("inputChannel", t.inputChannel);
    e->setAttribute ("stereoInput", t.stereoInput);
    e->setAttribute ("midiInputDevice", t.midiInputDevice);
    e->setAttribute ("midiChannel", t.midiChannel);
    setId (*e, "recordSource", t.recordSource);
    setId (*e, "outputBus", t.outputBus);
    e->setAttribute ("hardwareOutput", t.hardwareOutput);
    setId (*e, "auxParent", t.auxParent);
    e->setAttribute ("auxPair", t.auxPair);
    setId (*e, "folderOf", t.folderOf);
    e->setAttribute ("folderOpen", t.folderOpen);
    e->setAttribute ("bounced", t.bounced);
    setId (*e, "bouncedInto", t.bouncedInto);

    // Mixer state lives in the real-time mirror.
    if (t.rt != nullptr)
    {
        auto* m = e->createNewChildElement ("MIXER");
        setF (*m, "inputGain", t.rt->inputGain.load());
        setF (*m, "volume", t.rt->volume.load());
        setF (*m, "pan", t.rt->pan.load());
        m->setAttribute ("mute", t.rt->mute.load());
        m->setAttribute ("solo", t.rt->solo.load());
        m->setAttribute ("eqEnabled", t.rt->eqEnabled.load());
        m->setAttribute ("monitor", (int) t.rt->monitor.load());
        juce::StringArray eq;
        for (int i = 0; i < kEqBands; ++i) eq.add (juce::String ((double) t.rt->eq[i].load(), 6));
        m->setAttribute ("eq", eq.joinIntoString (" "));
    }

    writeChain (*e, "MIDIFX", t.midiFx);
    {
        auto* inst = e->createNewChildElement ("INSTRUMENT");
        if (t.instrument) writeSlot (*inst, *t.instrument);
    }
    writeChain (*e, "PREFX", t.preFx);
    writeChain (*e, "POSTFX", t.postFx);

    for (size_t i = 0; i < t.sends.size(); ++i)
    {
        auto* s = e->createNewChildElement ("SEND");
        setId (*s, "busId", t.sends[i].busId);
        s->setAttribute ("preFader", t.sends[i].preFader);
        setF (*s, "level", t.rt != nullptr && i < (size_t) kMaxSends ? t.rt->sendLevel[i].load() : 0.0f);
    }

    for (const auto& lane : t.automation) writeLane (*e, lane);
    for (const auto& l : t.loops) if (l) writeLoop (c, *e, *l, audioDir, sampleRate, errors);
}

TrackPtr readTrack (const juce::XmlElement& e, const juce::File& audioDir, double sampleRate, juce::StringArray& errors)
{
    auto t = std::make_shared<TrackModel>();
    t->id = getId (e, "id");
    t->kind = getEnum (e, "kind", TrackKind::Audio, (int) TrackKind::AuxOut);
    t->name = e.getStringAttribute ("name");
    t->colour = juce::Colour::fromString (e.getStringAttribute ("colour", "ff4fc3f7"));
    t->inputChannel = e.getIntAttribute ("inputChannel", 0);
    t->stereoInput = e.getBoolAttribute ("stereoInput");
    t->midiInputDevice = e.getStringAttribute ("midiInputDevice");
    t->midiChannel = e.getIntAttribute ("midiChannel", 0);
    t->recordSource = getId (e, "recordSource");
    t->outputBus = e.hasAttribute ("outputBus") ? getId (e, "outputBus") : kMasterId;
    t->hardwareOutput = e.getIntAttribute ("hardwareOutput", 0);
    t->auxParent = getId (e, "auxParent");
    t->auxPair = e.getIntAttribute ("auxPair", 0);
    t->folderOf = getId (e, "folderOf");
    t->folderOpen = e.getBoolAttribute ("folderOpen", true);
    t->bounced = e.getBoolAttribute ("bounced");
    t->bouncedInto = getId (e, "bouncedInto");

    t->rt = std::make_shared<TrackRt> (t->id);
    t->rt->bounced = t->bounced;
    if (auto* m = e.getChildByName ("MIXER"))
    {
        t->rt->inputGain = (float) m->getDoubleAttribute ("inputGain", 0.5);
        t->rt->volume = (float) m->getDoubleAttribute ("volume", pm::kUnityVolume);
        t->rt->pan = (float) m->getDoubleAttribute ("pan", 0.5);
        t->rt->mute = m->getBoolAttribute ("mute");
        t->rt->solo = m->getBoolAttribute ("solo");
        t->rt->eqEnabled = m->getBoolAttribute ("eqEnabled", true);
        t->rt->monitor = getEnum (*m, "monitor", MonitorMode::Auto, (int) MonitorMode::Off);
        auto eq = juce::StringArray::fromTokens (m->getStringAttribute ("eq"), " ", "");
        for (int i = 0; i < kEqBands && i < eq.size(); ++i) t->rt->eq[i] = eq[i].getFloatValue();
    }

    readChain (e, "MIDIFX", t->midiFx);
    if (auto* inst = e.getChildByName ("INSTRUMENT"))
        if (auto* se = inst->getChildByName ("SLOT"))
            t->instrument = readSlot (*se);
    readChain (e, "PREFX", t->preFx);
    readChain (e, "POSTFX", t->postFx);

    int sendIndex = 0;
    for (auto* se : e.getChildWithTagNameIterator ("SEND"))
    {
        if (sendIndex >= kMaxSends) break;
        t->sends.push_back ({ getId (*se, "busId"), se->getBoolAttribute ("preFader") });
        t->rt->sendLevel[sendIndex++] = (float) se->getDoubleAttribute ("level");
    }

    for (auto* a : e.getChildWithTagNameIterator ("LANE")) t->automation.push_back (readLane (*a));
    for (auto* le : e.getChildWithTagNameIterator ("LOOP"))
        t->loops.push_back (readLoop (*le, t->id, audioDir, sampleRate, errors));
    return t;
}

// ---- transport / timeline -----------------------------------------------------------------------------------------------------
void writeTransport (juce::XmlElement& parent, const TransportSettings& s)
{
    auto* e = parent.createNewChildElement ("TRANSPORT");
    setD (*e, "tempo", s.tempo);
    e->setAttribute ("beatsPerBar", s.beatsPerBar);
    e->setAttribute ("beatUnit", s.beatUnit);
    e->setAttribute ("masterBars", s.masterBars);
    e->setAttribute ("metronome", s.metronome);
    setF (*e, "metronomeVolume", s.metronomeVolume);
    e->setAttribute ("linkEnabled", s.linkEnabled);
    e->setAttribute ("linkStartStopSync", s.linkStartStopSync);
    e->setAttribute ("mode", (int) s.mode);
    e->setAttribute ("timelineLoop", s.timelineLoop);
    setD (*e, "timelineLoopStart", s.timelineLoopStart);
    setD (*e, "timelineLoopEnd", s.timelineLoopEnd);
    e->setAttribute ("recordLatencyOffset", s.recordLatencyOffset);
    writeLoopSettings (*e->createNewChildElement ("DEFAULTLOOP"), s.defaultLoopSettings);
}

TransportSettings readTransport (const juce::XmlElement& e)
{
    TransportSettings s;
    s.tempo = e.getDoubleAttribute ("tempo", 120.0);
    s.beatsPerBar = e.getIntAttribute ("beatsPerBar", 4);
    s.beatUnit = e.getIntAttribute ("beatUnit", 4);
    s.masterBars = e.getIntAttribute ("masterBars", 4);
    s.metronome = e.getBoolAttribute ("metronome");
    s.metronomeVolume = (float) e.getDoubleAttribute ("metronomeVolume", 0.5);
    s.linkEnabled = e.getBoolAttribute ("linkEnabled");
    s.linkStartStopSync = e.getBoolAttribute ("linkStartStopSync", true);
    s.mode = getEnum (e, "mode", TransportMode::Loops, (int) TransportMode::Timeline);
    s.timelineLoop = e.getBoolAttribute ("timelineLoop");
    s.timelineLoopStart = e.getDoubleAttribute ("timelineLoopStart", 0);
    s.timelineLoopEnd = e.getDoubleAttribute ("timelineLoopEnd", 64);
    s.recordLatencyOffset = e.getIntAttribute ("recordLatencyOffset", 0);
    if (auto* d = e.getChildByName ("DEFAULTLOOP")) s.defaultLoopSettings = readLoopSettings (*d);
    return s;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
juce::Result SessionSerializer::save (EngineController& controller, const juce::File& projectFolder)
{
    if (! projectFolder.createDirectory())
        return juce::Result::fail ("Could not create project folder " + projectFolder.getFullPathName());
    auto audioDir = projectFolder.getChildFile ("audio");
    if (! audioDir.createDirectory())
        return juce::Result::fail ("Could not create audio folder");

    // Remove stale take files from a previous save into the same folder.
    for (auto& f : audioDir.findChildFiles (juce::File::findFiles, false, "loop_*.wav"))
        f.deleteFile();

    auto& session = controller.session();
    const double sr = controller.engine().sampleRate();
    juce::StringArray errors;

    juce::XmlElement root ("ELooperSession");
    root.setAttribute ("version", kFormatVersion);
    setD (root, "sampleRate", sr);
    setId (root, "nextId", session.nextId);
    writeTransport (root, session.transport);

    auto* tracks = root.createNewChildElement ("TRACKS");
    for (const auto& t : session.tracks) if (t) writeTrack (controller, *tracks, *t, audioDir, sr, errors);

    auto* tl = root.createNewChildElement ("TIMELINE");
    for (const auto& c : session.timeline)
    {
        auto* ce = tl->createNewChildElement ("CLIP");
        setId (*ce, "id", c.id);
        setId (*ce, "trackId", c.trackId);
        setId (*ce, "loopId", c.loopId);
        setD (*ce, "startBeat", c.startBeat);
        setD (*ce, "lengthBeats", c.lengthBeats);
    }

    if (! errors.isEmpty())
        return juce::Result::fail (errors.joinIntoString ("\n"));
    if (! root.writeTo (projectFolder.getChildFile ("session.xml")))
        return juce::Result::fail ("Could not write session.xml");

    session.file = projectFolder;
    return juce::Result::ok();
}

void SessionSerializer::load (EngineController& controller, const juce::File& projectFolder,
                              std::function<void (juce::Result)> done)
{
    auto fail = [&done] (const juce::String& msg) { if (done) done (juce::Result::fail (msg)); };

    auto xml = juce::parseXML (projectFolder.getChildFile ("session.xml"));
    if (xml == nullptr || ! xml->hasTagName ("ELooperSession"))
        return fail ("Not a valid ELooper project: " + projectFolder.getFileName());
    if (xml->getIntAttribute ("version", 1) > kFormatVersion)
        return fail ("This project was saved by a newer version of ELooper.");

    const double sr = controller.engine().sampleRate();
    const auto audioDir = projectFolder.getChildFile ("audio");
    juce::StringArray errors;

    auto session = std::make_unique<Session>();
    session->nextId = juce::jmax ((Id) 2, getId (*xml, "nextId"));
    session->file = projectFolder;
    if (auto* t = xml->getChildByName ("TRANSPORT")) session->transport = readTransport (*t);

    if (auto* tracks = xml->getChildByName ("TRACKS"))
        for (auto* te : tracks->getChildWithTagNameIterator ("TRACK"))
            session->tracks.push_back (readTrack (*te, audioDir, sr, errors));

    if (session->findTrack (kMasterId) == nullptr)
        return fail ("Project has no master track.");

    // Keep id allocation ahead of every id in the file.
    Id maxId = 1;
    for (auto& t : session->tracks)
    {
        maxId = juce::jmax (maxId, t->id);
        for (auto& p : t->allPlugins()) maxId = juce::jmax (maxId, p->id);
        for (auto& l : t->loops) maxId = juce::jmax (maxId, l->id);
    }

    if (auto* tl = xml->getChildByName ("TIMELINE"))
        for (auto* ce : tl->getChildWithTagNameIterator ("CLIP"))
        {
            TimelineClip c;
            c.id = getId (*ce, "id");
            c.trackId = getId (*ce, "trackId");
            c.loopId = getId (*ce, "loopId");
            c.startBeat = ce->getDoubleAttribute ("startBeat");
            c.lengthBeats = ce->getDoubleAttribute ("lengthBeats", 16);
            maxId = juce::jmax (maxId, c.id);
            session->timeline.push_back (c);
        }
    session->nextId = juce::jmax (session->nextId, maxId + 1);

    controller.loadSession (std::move (session), [done, errors] (juce::StringArray loadErrors)
    {
        auto all = errors;
        all.addArray (loadErrors);
        if (done) done (all.isEmpty() ? juce::Result::ok() : juce::Result::fail (all.joinIntoString ("\n")));
    });
}

} // namespace el
