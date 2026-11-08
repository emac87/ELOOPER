#include "Engine/MidiMapping.h"

namespace el {

using Kind = MappingTarget::Kind;

static const char* kindNames[] = { "TriggerLoop", "RecordLoop", "StopLoop", "PlayStop", "Record", "StopAll", "TapTempo", "Param", "Scene" };
static constexpr int kNumKinds = (int) (sizeof (kindNames) / sizeof (kindNames[0]));

juce::File MidiMapping::storeFile() { return PluginHost::appDataDir().getChildFile ("midi-mappings.xml"); }

MidiMapping::MidiMapping (EngineController& c) : controller (c)
{
    load();

    auto& dm = controller.deviceManager();
    auto devices = juce::MidiInput::getAvailableDevices();
    for (auto& d : devices) knownDevices.add (d.identifier);

    auto marker = PluginHost::appDataDir().getChildFile ("midi-initialised");
    if (! marker.existsAsFile())
    {
        bool anyEnabled = false;
        for (auto& d : devices) anyEnabled = anyEnabled || dm.isMidiInputDeviceEnabled (d.identifier);
        if (! anyEnabled)
            for (auto& d : devices) dm.setMidiInputDeviceEnabled (d.identifier, true);
        marker.create();
    }

    dm.addMidiInputDeviceCallback ({}, this);
    controller.addListener (this);

    gestureTimer.fn = [this] { endIdleGestures (false); };
    deviceTimer.fn = [this] { checkDevices(); };
    deviceTimer.startTimer (2000);
}

MidiMapping::~MidiMapping()
{
    deviceTimer.stopTimer();
    gestureTimer.stopTimer();
    controller.removeListener (this);
    controller.deviceManager().removeMidiInputDeviceCallback ({}, this);
    cancelPendingUpdate();
    endIdleGestures (true);
}

void MidiMapping::checkDevices()
{
    auto devices = juce::MidiInput::getAvailableDevices();
    juce::StringArray now;
    for (auto& d : devices)
    {
        now.add (d.identifier);
        if (! knownDevices.contains (d.identifier))
            controller.deviceManager().setMidiInputDeviceEnabled (d.identifier, true);
    }
    knownDevices = now;
}

// ---- MIDI thread -----------------------------------------------------------------------
void MidiMapping::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    if (m.isSysEx() || m.isMidiClock() || m.isActiveSense() || m.isMidiStart() || m.isMidiStop()
        || m.isMidiContinue() || m.isSongPositionPointer() || m.isQuarterFrame())
        return;
    {
        const juce::ScopedLock sl (queueLock);
        if (queue.size() < 1024)
            queue.push_back ({ m, source != nullptr ? source->getIdentifier() : juce::String() });
    }
    triggerAsyncUpdate();
}

void MidiMapping::handleAsyncUpdate()
{
    std::vector<Incoming> items;
    {
        const juce::ScopedLock sl (queueLock);
        items.swap (queue);
    }
    for (auto& i : items) process (i);
}

// ---- message thread --------------------------------------------------------------------
void MidiMapping::process (const Incoming& in)
{
    if (controller.onMidiInput) controller.onMidiInput (in.message, in.deviceId);

    const auto& m = in.message;
    const bool noteOn = m.isNoteOn();
    const bool cc = m.isController();
    if (! noteOn && ! cc) return;

    if (learning) { learnFrom (in); return; }

    const int number = noteOn ? m.getNoteNumber() : m.getControllerNumber();
    const int value = noteOn ? 127 : m.getControllerValue();
    const juce::String key = in.deviceId + "|" + juce::String (m.getChannel()) + "|" + (cc ? "c" : "n") + juce::String (number);
    const int prev = lastCC.count (key) ? lastCC[key] : 0;
    if (cc) lastCC[key] = value;

    // Copy: dispatch may call into controller which may notify and prune mappings.
    auto snapshot = maps;
    for (auto& mp : snapshot)
    {
        if (mp.isCC != cc || mp.number != number || mp.channel != m.getChannel()) continue;
        if (mp.deviceId.isNotEmpty() && mp.deviceId != in.deviceId) continue;
        if (mp.target.kind == Kind::Param)
            dispatch (mp, value, noteOn);
        else if (noteOn || (value >= 64 && prev < 64))
            dispatch (mp, value, noteOn);
    }
}

void MidiMapping::dispatch (const Mapping& mp, int value, bool isNoteOn)
{
    const auto& t = mp.target;
    switch (t.kind)
    {
        case Kind::TriggerLoop: controller.triggerLoop (t.loopId); break;
        case Kind::RecordLoop:  controller.recordLoop (t.loopId); break;
        case Kind::StopLoop:    controller.stopLoop (t.loopId); break;
        case Kind::PlayStop:    controller.togglePlay(); break;
        case Kind::Record:
            if (selectedLoopProvider)
                if (auto id = selectedLoopProvider(); id != kInvalidId) controller.triggerLoop (id);
            break;
        case Kind::StopAll:     controller.stopAllLoops(); break;
        case Kind::Scene:       controller.launchScene ((int) t.loopId); break;
        case Kind::TapTempo:    controller.tapTempo(); break;
        case Kind::Param:
        {
            if (controller.session().findTrack (t.trackId) == nullptr) break;
            const auto now = juce::Time::getMillisecondCounter();
            bool found = false;
            for (auto& g : gestures)
                if (g.trackId == t.trackId && g.param == t.param) { g.lastMs = now; found = true; }
            if (! found)
            {
                controller.beginParamGesture (t.trackId, t.param);
                gestures.push_back ({ t.trackId, t.param, now });
                gestureTimer.startTimer (100);
            }
            const float v = isNoteOn ? 1.0f : (float) value / 127.0f;
            controller.setParam (t.trackId, t.param, v);
            break;
        }
    }
}

void MidiMapping::endIdleGestures (bool all)
{
    const auto now = juce::Time::getMillisecondCounter();
    for (size_t i = gestures.size(); i-- > 0;)
    {
        if (all || now - gestures[i].lastMs >= 300)
        {
            auto g = gestures[i];
            gestures.erase (gestures.begin() + (long) i);
            if (controller.session().findTrack (g.trackId) != nullptr)
                controller.endParamGesture (g.trackId, g.param);
        }
    }
    if (gestures.empty()) gestureTimer.stopTimer();
}

// ---- learn -----------------------------------------------------------------------------
void MidiMapping::startLearn (const MappingTarget& target) { learning = true; learnTarget = target; }
void MidiMapping::cancelLearn() { learning = false; }
bool MidiMapping::isLearning() const { return learning; }

void MidiMapping::learnFrom (const Incoming& in)
{
    const auto& m = in.message;
    Mapping mp;
    mp.target = learnTarget;
    mp.deviceId = in.deviceId;
    mp.channel = m.getChannel();
    mp.isCC = m.isController();
    mp.number = mp.isCC ? m.getControllerNumber() : m.getNoteNumber();
    mp.description = describe (learnTarget);
    learning = false;
    addLearned (std::move (mp));
}

void MidiMapping::addLearned (Mapping m)
{
    auto sameTarget = [] (const MappingTarget& a, const MappingTarget& b)
    {
        return a.kind == b.kind && a.loopId == b.loopId && a.trackId == b.trackId && a.param == b.param;
    };
    maps.erase (std::remove_if (maps.begin(), maps.end(), [&] (const Mapping& o)
    {
        const bool sameSource = o.isCC == m.isCC && o.number == m.number && o.channel == m.channel && o.deviceId == m.deviceId;
        return sameSource || sameTarget (o.target, m.target);
    }), maps.end());
    maps.push_back (std::move (m));
    changed();
}

juce::String MidiMapping::describe (const MappingTarget& t) const
{
    auto& s = controller.session();
    switch (t.kind)
    {
        case Kind::TriggerLoop:
        case Kind::RecordLoop:
        case Kind::StopLoop:
        {
            auto loop = s.findLoop (t.loopId);
            auto name = loop != nullptr ? loop->name : juce::String ("?");
            const char* verb = t.kind == Kind::TriggerLoop ? "Trigger" : t.kind == Kind::RecordLoop ? "Record" : "Stop";
            return juce::String (verb) + ": " + name;
        }
        case Kind::PlayStop: return "Play / Stop";
        case Kind::Record:   return "Record selected loop";
        case Kind::StopAll:  return "Stop all loops";
        case Kind::Scene:    return "Scene " + juce::String ((int) t.loopId + 1);
        case Kind::TapTempo: return "Tap tempo";
        case Kind::Param:
        {
            auto track = s.findTrack (t.trackId);
            juce::String pname = "?";
            for (auto& p : controller.listParams (t.trackId))
                if (p.target == t.param) { pname = p.name; break; }
            return "Param: " + (track != nullptr ? track->name : juce::String ("?")) + "/" + pname;
        }
    }
    return {};
}

// ---- editing / persistence ---------------------------------------------------------------
void MidiMapping::removeMapping (int index)
{
    if (index < 0 || index >= (int) maps.size()) return;
    maps.erase (maps.begin() + index);
    changed();
}

void MidiMapping::clearAll()
{
    if (maps.empty()) return;
    maps.clear();
    changed();
}

void MidiMapping::changed()
{
    save();
    if (onMappingsChanged) onMappingsChanged();
}

void MidiMapping::save() const
{
    juce::XmlElement root ("MidiMappings");
    for (auto& m : maps)
    {
        auto* e = root.createNewChildElement ("Map");
        e->setAttribute ("kind", kindNames[(int) m.target.kind]);
        e->setAttribute ("track", (int) m.target.trackId);
        e->setAttribute ("loop", (int) m.target.loopId);
        e->setAttribute ("pkind", (int) m.target.param.kind);
        e->setAttribute ("pobj", (int) m.target.param.objectId);
        e->setAttribute ("pidx", (int) m.target.param.index);
        e->setAttribute ("device", m.deviceId);
        e->setAttribute ("channel", m.channel);
        e->setAttribute ("cc", m.isCC);
        e->setAttribute ("number", m.number);
        e->setAttribute ("desc", m.description);
    }
    storeFile().replaceWithText (root.toString());
}

void MidiMapping::load()
{
    maps.clear();
    auto xml = juce::XmlDocument::parse (storeFile());
    if (xml == nullptr || ! xml->hasTagName ("MidiMappings")) return;
    for (auto* e : xml->getChildWithTagNameIterator ("Map"))
    {
        Mapping m;
        const auto kind = e->getStringAttribute ("kind");
        bool ok = false;
        for (int i = 0; i < kNumKinds; ++i)
            if (kind == kindNames[i]) { m.target.kind = (Kind) i; ok = true; }
        if (! ok) continue;
        m.target.trackId = (Id) e->getIntAttribute ("track");
        m.target.loopId = (Id) e->getIntAttribute ("loop");
        m.target.param.kind = (ParamKind) e->getIntAttribute ("pkind");
        m.target.param.objectId = (Id) e->getIntAttribute ("pobj");
        m.target.param.index = (int32_t) e->getIntAttribute ("pidx", -1);
        m.deviceId = e->getStringAttribute ("device");
        m.channel = juce::jlimit (1, 16, e->getIntAttribute ("channel", 1));
        m.isCC = e->getBoolAttribute ("cc", true);
        m.number = juce::jlimit (0, 127, e->getIntAttribute ("number"));
        m.description = e->getStringAttribute ("desc");
        maps.push_back (std::move (m));
    }
}

void MidiMapping::sessionChanged (Change what, Id)
{
    if (what == Change::Structure || what == Change::Session)
        pruneInvalid();
}

void MidiMapping::pruneInvalid()
{
    auto& s = controller.session();
    const auto before = maps.size();
    maps.erase (std::remove_if (maps.begin(), maps.end(), [&] (const Mapping& m)
    {
        const auto k = m.target.kind;
        if (k == Kind::TriggerLoop || k == Kind::RecordLoop || k == Kind::StopLoop)
            return s.findLoop (m.target.loopId) == nullptr;
        if (k == Kind::Param)
            return s.findTrack (m.target.trackId) == nullptr;
        return false;
    }), maps.end());
    if (maps.size() != before) changed();
}

} // namespace el
