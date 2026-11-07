#include "UI/StepPresets.h"
#include "Engine/PluginHost.h"

namespace el::steppresets {

namespace {
    const char* kGm[] = {
        "Kick 2", "Kick", "Side Stick", "Snare", "Clap", "Snare 2", "Low Floor Tom", "Closed HH",
        "High Floor Tom", "Pedal HH", "Low Tom", "Open HH", "Low-Mid Tom", "Hi-Mid Tom", "Crash",
        "High Tom", "Ride", "China", "Ride Bell", "Tambourine", "Splash", "Cowbell", "Crash 2",
        "Vibraslap", "Ride 2", "Hi Bongo", "Low Bongo", "Mute Hi Conga", "Open Hi Conga", "Low Conga",
        "High Timbale", "Low Timbale", "High Agogo", "Low Agogo", "Cabasa", "Maracas", "Short Whistle",
        "Long Whistle", "Short Guiro", "Long Guiro", "Claves", "Hi Wood Block", "Low Wood Block",
        "Mute Cuica", "Open Cuica", "Mute Triangle", "Open Triangle"
    };   // pitches 35..81

    std::vector<Lane> lanes (std::initializer_list<std::pair<int, const char*>> l)
    {
        std::vector<Lane> v;
        for (auto& [p, n] : l) v.push_back ({ p, n });
        return v;
    }

    // Kick at the top, then snare, hats, toms high to low, cymbals. GM note numbers, which the
    // big acoustic libraries (Toontrack, XLN, Bogren Digital, GetGood...) accept for the core kit.
    std::vector<Lane> gmCoreKit()
    {
        return lanes ({ { 36, "Kick" }, { 38, "Snare" }, { 40, "Snare Rimshot" }, { 37, "Side Stick" },
                        { 42, "HH Closed" }, { 44, "HH Pedal" }, { 46, "HH Open" },
                        { 48, "Tom 1" }, { 47, "Tom 2" }, { 45, "Tom 3" }, { 43, "Floor Tom" }, { 41, "Floor Tom 2" },
                        { 49, "Crash L" }, { 57, "Crash R" }, { 52, "China" }, { 55, "Splash" },
                        { 51, "Ride" }, { 53, "Ride Bell" } });
    }
    std::vector<Lane> pads16()
    {
        std::vector<Lane> v;
        for (int i = 0; i < 16; ++i) v.push_back ({ 36 + i, "Pad " + juce::String (i + 1) });
        return v;
    }
}

juce::String gmDrumName (int pitch)
{
    if (pitch >= 35 && pitch <= 81) return kGm[pitch - 35];
    return {};
}

const std::vector<Group>& drumMaps()
{
    static const std::vector<Group> groups = [] {
        const juce::String gmNote = "GM layout - if a lane is silent, set the plug-in's MIDI map to GM or change the lane's note";
        std::vector<Group> g;

        g.push_back ({ "Baby Audio", {
            // Tekno's 18 voices sit chromatically from C1 (36) to F2 (53), in its voice-list order.
            { "Tekno (18 voices)", "C1-F2, same layout repeats at C3-F4",
              lanes ({ { 36, "Kick A" }, { 37, "Kick B" }, { 38, "Snare A" }, { 39, "Snare B" }, { 40, "Hat A" },
                       { 41, "Hat B" }, { 42, "Hat OP" }, { 43, "Tom L" }, { 44, "Tom H" }, { 45, "Clap" },
                       { 46, "Rimshot" }, { 47, "Conga L" }, { 48, "Conga H" }, { 49, "Ride" }, { 50, "Tonal" },
                       { 51, "Cowbell" }, { 52, "Block" }, { 53, "Crash" } }) },
            { "Tekno (core 8)", "kicks, snares, hats, clap",
              lanes ({ { 36, "Kick A" }, { 37, "Kick B" }, { 38, "Snare A" }, { 45, "Clap" },
                       { 40, "Hat A" }, { 41, "Hat B" }, { 42, "Hat OP" }, { 53, "Crash" } }) } } });

        g.push_back ({ "Kontakt drum kits", {
            { "Eloy Drums (Bogren Digital)", gmNote, gmCoreKit() },
            { "Krimh / Trivium Drums (Bogren Digital)", gmNote, gmCoreKit() },
            { "Extinction Level Event (Spectre Digital)", gmNote, gmCoreKit() },
            { "GetGood Drums (Modern & Massive, Invasion...)", gmNote, gmCoreKit() },
            { "Kontakt Factory / Studio Drummer", gmNote, gmCoreKit() } } });

        g.push_back ({ "Drum plug-ins", {
            { "Superior Drummer 3 (Toontrack)", gmNote, gmCoreKit() },
            { "EZdrummer 3 (Toontrack)", gmNote, gmCoreKit() },
            { "Addictive Drums 2 (XLN)", gmNote, gmCoreKit() } } });

        g.push_back ({ "Pads (chromatic from C1)", {
            { "16 pads (Battery 4, Ableton Drum Rack, MPC, Maschine)", "C1-D#2", pads16() },
            { "Logic Drum Machine Designer / Drum Kit Designer", "GM layout",
              lanes ({ { 36, "Kick" }, { 38, "Snare" }, { 39, "Clap" }, { 37, "Rim" }, { 42, "HH Closed" },
                       { 44, "HH Pedal" }, { 46, "HH Open" }, { 50, "Tom High" }, { 47, "Tom Mid" },
                       { 45, "Tom Low" }, { 49, "Crash" }, { 51, "Ride" } }) } } });

        g.push_back ({ "General MIDI", {
            { "GM rock kit (8 lanes)", "", lanes ({ { 36, "Kick" }, { 38, "Snare" }, { 42, "Closed HH" }, { 46, "Open HH" },
                                                      { 48, "Hi-Mid Tom" }, { 45, "Low Tom" }, { 49, "Crash" }, { 51, "Ride" } }) },
            { "GM 808 / 909 style", "", lanes ({ { 36, "Kick" }, { 38, "Snare" }, { 39, "Clap" }, { 37, "Rim" },
                                                   { 42, "Closed HH" }, { 46, "Open HH" }, { 50, "High Tom" }, { 47, "Mid Tom" },
                                                   { 45, "Low Tom" }, { 56, "Cowbell" }, { 49, "Crash" }, { 51, "Ride" } }) },
            { "GM full kit", "", gmCoreKit() },
            { "GM percussion", "", lanes ({ { 54, "Tambourine" }, { 56, "Cowbell" }, { 60, "Hi Bongo" }, { 61, "Low Bongo" },
                                              { 62, "Mute Hi Conga" }, { 63, "Open Hi Conga" }, { 64, "Low Conga" },
                                              { 69, "Cabasa" }, { 70, "Maracas" }, { 75, "Claves" }, { 76, "Hi Wood Block" },
                                              { 77, "Low Wood Block" } }) } } });
        return g;
    }();
    return groups;
}

const std::vector<Scale>& scales()
{
    static const std::vector<Scale> s {
        { "Major", { 0, 2, 4, 5, 7, 9, 11 } },
        { "Natural Minor", { 0, 2, 3, 5, 7, 8, 10 } },
        { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 } },
        { "Melodic Minor", { 0, 2, 3, 5, 7, 9, 11 } },
        { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } },
        { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
        { "Phrygian Dominant", { 0, 1, 4, 5, 7, 8, 10 } },
        { "Lydian", { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } },
        { "Locrian", { 0, 1, 3, 5, 6, 8, 10 } },
        { "Major Pentatonic", { 0, 2, 4, 7, 9 } },
        { "Minor Pentatonic", { 0, 3, 5, 7, 10 } },
        { "Blues", { 0, 3, 5, 6, 7, 10 } },
        { "Hungarian Minor", { 0, 2, 3, 6, 7, 8, 11 } },
        { "Whole Tone", { 0, 2, 4, 6, 8, 10 } },
        { "Diminished (W-H)", { 0, 2, 3, 5, 6, 8, 9, 11 } },
        { "Chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    };
    return s;
}

Preset makeScale (const Scale& scale, int rootPitch)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    auto noteName = [] (int p) { return juce::String (names[p % 12]) + juce::String (p / 12 - 2); };
    Preset p;
    p.name = juce::String (names[rootPitch % 12]) + " " + scale.name;
    std::vector<int> pitches;
    for (int iv : scale.intervals) pitches.push_back (rootPitch + iv);
    pitches.push_back (rootPitch + 12);
    for (auto it = pitches.rbegin(); it != pitches.rend(); ++it)
        if (*it >= 0 && *it <= 127) p.lanes.push_back ({ *it, noteName (*it) });
    return p;
}

// ---- user presets ---------------------------------------------------------------------------
juce::File userDir()
{
    auto d = PluginHost::appDataDir().getChildFile ("StepPresets");
    d.createDirectory();
    return d;
}

juce::Array<juce::File> userPresetFiles()
{
    auto files = userDir().findChildFiles (juce::File::findFiles, false, "*.elsteps");
    struct ByName { static int compareElements (const juce::File& a, const juce::File& b) {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()); } };
    ByName cmp;
    files.sort (cmp);
    return files;
}

bool saveUserPreset (const juce::String& name, const std::vector<Lane>& lanesIn, const MidiClip* pattern)
{
    juce::XmlElement root ("ELSTEPS");
    root.setAttribute ("name", name);
    for (auto& l : lanesIn) {
        auto* e = root.createNewChildElement ("LANE");
        e->setAttribute ("pitch", l.pitch);
        e->setAttribute ("name", l.name);
    }
    if (pattern != nullptr) {
        auto* pe = root.createNewChildElement ("PATTERN");
        pe->setAttribute ("stepBeats", pattern->stepBeats);
        pe->setAttribute ("lengthBeats", pattern->lengthBeats);
        for (auto& n : pattern->notes) {
            auto* ne = pe->createNewChildElement ("NOTE");
            ne->setAttribute ("start", n.start);
            ne->setAttribute ("length", n.length);
            ne->setAttribute ("pitch", (int) n.pitch);
            ne->setAttribute ("velocity", (int) n.velocity);
            ne->setAttribute ("channel", (int) n.channel);
            ne->setAttribute ("ratchet", (int) n.ratchet);
            ne->setAttribute ("muted", n.muted);
        }
    }
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) safe = "Preset";
    return root.writeTo (userDir().getChildFile (safe + ".elsteps"));
}

std::optional<UserPreset> loadUserPreset (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("ELSTEPS")) return std::nullopt;
    UserPreset p;
    p.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
    for (auto* e : xml->getChildWithTagNameIterator ("LANE"))
        p.lanes.push_back ({ juce::jlimit (0, 127, e->getIntAttribute ("pitch", 36)), e->getStringAttribute ("name") });
    if (auto* pe = xml->getChildByName ("PATTERN")) {
        p.hasPattern = true;
        p.stepBeats = juce::jmax (1.0e-3, pe->getDoubleAttribute ("stepBeats", 0.25));
        p.lengthBeats = juce::jmax (p.stepBeats, pe->getDoubleAttribute ("lengthBeats", 16));
        for (auto* ne : pe->getChildWithTagNameIterator ("NOTE")) {
            MidiNote n;
            n.start = ne->getDoubleAttribute ("start");
            n.length = ne->getDoubleAttribute ("length", 0.25);
            n.pitch = (uint8_t) juce::jlimit (0, 127, ne->getIntAttribute ("pitch", 60));
            n.velocity = (uint8_t) juce::jlimit (1, 127, ne->getIntAttribute ("velocity", 100));
            n.channel = (uint8_t) juce::jlimit (1, 16, ne->getIntAttribute ("channel", 1));
            n.ratchet = (uint8_t) juce::jlimit (1, 8, ne->getIntAttribute ("ratchet", 1));
            n.muted = ne->getBoolAttribute ("muted");
            p.notes.push_back (n);
        }
    }
    if (p.lanes.empty() && ! p.hasPattern) return std::nullopt;
    return p;
}

} // namespace el::steppresets
