#include "UI/PluginIcons.h"
#include "Engine/PluginHost.h"
#include "Platform/PluginIconMac.h"

namespace el::icons {

namespace {
    struct Shape { juce::Path line; juce::Path fill; };   // 24x24 grid: stroked lines + solid shapes

    void wave (juce::Path& p, float x0, float x1, float y, float amp, float cycles)
    {
        const int n = 24;
        for (int i = 0; i <= n; ++i) {
            const float t = (float) i / (float) n;
            const float x = x0 + (x1 - x0) * t, yy = y - amp * std::sin (t * cycles * juce::MathConstants<float>::twoPi);
            if (i == 0) p.startNewSubPath (x, yy); else p.lineTo (x, yy);
        }
    }
    void poly (juce::Path& p, std::initializer_list<juce::Point<float>> pts, bool close = false)
    {
        bool first = true;
        for (auto& pt : pts) { if (first) p.startNewSubPath (pt); else p.lineTo (pt); first = false; }
        if (close) p.closeSubPath();
    }
    void dot (juce::Path& p, float x, float y, float r) { p.addEllipse (x - r, y - r, 2 * r, 2 * r); }
    // Arc opening towards the right of (cx, cy), radius r, half-angle a (radians).
    void arcRight (juce::Path& p, float cx, float cy, float r, float a)
    {
        const float h = juce::MathConstants<float>::halfPi;
        p.addCentredArc (cx, cy, r, r, 0.0f, h - a, h + a, true);
    }

    Shape build (Kind k)
    {
        Shape s;
        auto& L = s.line; auto& F = s.fill;
        using P = juce::Point<float>;
        constexpr float pi = juce::MathConstants<float>::pi;
        switch (k) {
            case Kind::Synth:
                poly (L, { P (3, 17), P (9, 7), P (9, 17), P (15, 7), P (15, 17), P (21, 7) });
                break;
            case Kind::Piano:
                L.addRoundedRectangle (3.0f, 5.0f, 18.0f, 14.0f, 2.0f);
                poly (L, { P (9, 13), P (9, 19) }); poly (L, { P (15, 13), P (15, 19) });
                F.addRectangle (7.0f, 5.0f, 4.0f, 8.0f); F.addRectangle (13.0f, 5.0f, 4.0f, 8.0f);
                break;
            case Kind::Drums:
                L.addEllipse (4.0f, 8.0f, 16.0f, 5.0f);
                poly (L, { P (4, 10.5f), P (4, 18) }); poly (L, { P (20, 10.5f), P (20, 18) });
                L.addCentredArc (12.0f, 18.0f, 8.0f, 2.5f, 0.0f, pi, 2 * pi, true);
                poly (L, { P (7, 3), P (11, 9) }); poly (L, { P (17, 3), P (13, 9) });
                break;
            case Kind::Sampler: {
                const float hs[] = { 3, 7, 12, 8, 14, 6, 10, 4 };
                for (int i = 0; i < 8; ++i) { const float x = 3.5f + 2.4f * (float) i; poly (L, { P (x, 12 - hs[i] / 2), P (x, 12 + hs[i] / 2) }); }
                break; }
            case Kind::Bass:
                L.startNewSubPath (6, 10);
                L.cubicTo (6, 4, 15, 4, 15, 10.5f);
                L.cubicTo (15, 15, 11, 19, 5, 21);
                dot (F, 6, 10, 1.8f); dot (F, 19.5f, 8, 1.3f); dot (F, 19.5f, 13, 1.3f);
                break;
            case Kind::Strings:
                wave (L, 3, 21, 7, 2, 2); wave (L, 3, 21, 12, 2, 2); wave (L, 3, 21, 17, 2, 2);
                break;
            case Kind::Organ: {
                const float hs[] = { 14, 10, 12, 16 };
                for (int i = 0; i < 4; ++i) L.addRoundedRectangle (4.0f + 4.5f * (float) i, 20 - hs[i], 3.0f, hs[i], 1.0f);
                break; }
            case Kind::Reverb:
                dot (F, 5, 12, 1.8f);
                arcRight (L, 5, 12, 6, 0.9f); arcRight (L, 5, 12, 10, 0.75f); arcRight (L, 5, 12, 14, 0.6f);
                break;
            case Kind::Delay: {
                const float hs[] = { 16, 11, 7, 4 };
                for (int i = 0; i < 4; ++i) { const float x = 5.0f + 4.7f * (float) i; poly (L, { P (x, 12 - hs[i] / 2), P (x, 12 + hs[i] / 2) }); }
                break; }
            case Kind::Eq: {
                const float ys[] = { 15, 8, 12 };
                for (int i = 0; i < 3; ++i) { const float x = 6.0f + 6.0f * (float) i; poly (L, { P (x, 4), P (x, 20) }); dot (F, x, ys[i], 2.6f); }
                break; }
            case Kind::Compressor:
                poly (L, { P (4, 4), P (4, 20), P (20, 20) });
                poly (L, { P (4, 20), P (11, 13), P (20, 10) });
                break;
            case Kind::Distortion:
                poly (L, { P (3, 12), P (6, 5), P (10, 5), P (14, 19), P (18, 19), P (21, 12) });
                break;
            case Kind::Modulation:
                wave (L, 3, 21, 9, 4, 1.5f); wave (L, 3, 21, 16, 4, 1.5f);
                break;
            case Kind::Effect:
                L.addEllipse (4.0f, 4.0f, 16.0f, 16.0f);
                poly (L, { P (12, 12), P (16, 7) });
                dot (F, 12, 12, 1.5f);
                break;
            case Kind::Audio:
                L.addRoundedRectangle (9.0f, 3.0f, 6.0f, 11.0f, 3.0f);
                L.addCentredArc (12.0f, 11.0f, 6.5f, 6.5f, 0.0f, pi / 2, 3 * pi / 2, true);
                poly (L, { P (12, 17.5f), P (12, 21) }); poly (L, { P (8, 21), P (16, 21) });
                break;
            case Kind::Midi:
                L.addEllipse (3.0f, 3.0f, 18.0f, 18.0f);
                dot (F, 6.5f, 11, 1.3f); dot (F, 8, 7, 1.3f); dot (F, 12, 5.5f, 1.3f); dot (F, 16, 7, 1.3f); dot (F, 17.5f, 11, 1.3f);
                L.addRoundedRectangle (9.5f, 14.5f, 5.0f, 3.0f, 1.0f);
                break;
            case Kind::Bus:
                poly (L, { P (3, 6), P (10, 6), P (14, 12) }); poly (L, { P (3, 18), P (10, 18), P (14, 12) });
                poly (L, { P (14, 12), P (21, 12) }); poly (L, { P (17, 8.5f), P (21, 12), P (17, 15.5f) });
                break;
            case Kind::Master:
                poly (L, { P (4, 9), P (8, 9), P (13, 5), P (13, 19), P (8, 15), P (4, 15) }, true);
                arcRight (L, 13, 12, 4.5f, 0.8f); arcRight (L, 13, 12, 8.5f, 0.7f);
                break;
            case Kind::AuxOut:
                poly (L, { P (3, 12), P (7, 12) });
                L.addRoundedRectangle (7.0f, 8.5f, 6.0f, 7.0f, 1.0f);
                L.addRoundedRectangle (13.0f, 10.5f, 7.0f, 3.0f, 1.0f);
                break;
        }
        return s;
    }

    // Stroked outline + solid shapes, as one fillable path (plus zero-area corner markers so that
    // every icon has the same 24x24 bounds and therefore the same scale).
    const juce::Path& iconPath (Kind k)
    {
        static std::map<int, juce::Path> cache;
        auto it = cache.find ((int) k);
        if (it != cache.end()) return it->second;
        auto s = build (k);
        juce::Path out;
        juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (out, s.line);
        out.addPath (s.fill);
        out.setUsingNonZeroWinding (true);
        out.startNewSubPath (0, 0); out.lineTo (0, 0);
        out.startNewSubPath (24, 24); out.lineTo (24, 24);
        return cache.emplace ((int) k, std::move (out)).first->second;
    }

    bool hasKeyword (const juce::String& text, const juce::StringArray& tokens, const char* kw)
    {
        const juce::String k (kw);
        if (k.length() <= 2 || k == "b3") return tokens.contains (k);
        if (k.length() == 3) { for (auto& t : tokens) if (t.startsWith (k)) return true; return false; }
        return text.contains (k);
    }
    bool anyKeyword (const juce::String& text, const juce::StringArray& tokens, std::initializer_list<const char*> kws)
    {
        for (auto* kw : kws) if (hasKeyword (text, tokens, kw)) return true;
        return false;
    }
}

const std::vector<Kind>& allKinds()
{
    static const std::vector<Kind> k { Kind::Synth, Kind::Piano, Kind::Drums, Kind::Sampler, Kind::Bass, Kind::Strings, Kind::Organ,
                                       Kind::Reverb, Kind::Delay, Kind::Eq, Kind::Compressor, Kind::Distortion, Kind::Modulation, Kind::Effect,
                                       Kind::Audio, Kind::Midi, Kind::Bus, Kind::Master, Kind::AuxOut };
    return k;
}

juce::String kindName (Kind k)
{
    switch (k) {
        case Kind::Synth: return "Synth";
        case Kind::Piano: return "Piano / Keys";
        case Kind::Drums: return "Drums";
        case Kind::Sampler: return "Sampler";
        case Kind::Bass: return "Bass";
        case Kind::Strings: return "Strings / Pad";
        case Kind::Organ: return "Organ";
        case Kind::Reverb: return "Reverb";
        case Kind::Delay: return "Delay";
        case Kind::Eq: return "EQ / Filter";
        case Kind::Compressor: return "Compressor";
        case Kind::Distortion: return "Distortion";
        case Kind::Modulation: return "Modulation";
        case Kind::Effect: return "Effect";
        case Kind::Audio: return "Audio";
        case Kind::Midi: return "MIDI";
        case Kind::Bus: return "Bus";
        case Kind::Master: return "Master";
        case Kind::AuxOut: return "Aux Out";
    }
    return {};
}

std::optional<Kind> kindFromName (const juce::String& n)
{
    for (auto k : allKinds()) if (kindName (k).equalsIgnoreCase (n)) return k;
    return std::nullopt;
}

std::unique_ptr<juce::Drawable> makeDrawable (Kind k)
{
    auto d = std::make_unique<juce::DrawablePath>();
    d->setPath (iconPath (k));
    d->setFill (juce::FillType (juce::Colours::white));
    d->setStrokeFill (juce::FillType (juce::Colours::transparentBlack));
    d->setStrokeThickness (0.0f);
    return d;
}

Kind guessKind (const juce::PluginDescription& d)
{
    const auto text = (d.name + " " + d.category).toLowerCase();
    juce::StringArray tokens;
    tokens.addTokens (text.retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789 "), " ", {});
    tokens.removeEmptyStrings();
    auto has = [&] (std::initializer_list<const char*> kws) { return anyKeyword (text, tokens, kws); };

    if (d.isInstrument) {
        if (has ({ "drum", "kit", "beat", "808", "909", "perc" })) return Kind::Drums;
        if (has ({ "sampler", "sample", "kontakt" })) return Kind::Sampler;
        if (has ({ "piano", "keys", "rhodes", "wurli", "ep" })) return Kind::Piano;
        if (has ({ "organ", "b3" })) return Kind::Organ;
        if (has ({ "bass" })) return Kind::Bass;
        if (has ({ "string", "pad", "orchestr", "choir" })) return Kind::Strings;
        return Kind::Synth;
    }
    if (has ({ "reverb", "verb", "room", "hall", "plate" })) return Kind::Reverb;
    if (has ({ "delay", "echo", "tape" })) return Kind::Delay;
    if (has ({ "eq", "equal", "filter" })) return Kind::Eq;
    if (has ({ "comp", "limit", "dynamic", "gate" })) return Kind::Compressor;
    if (has ({ "dist", "drive", "satur", "fuzz", "amp" })) return Kind::Distortion;
    if (has ({ "chorus", "flang", "phas", "trem", "vibrato", "mod" })) return Kind::Modulation;
    return Kind::Effect;
}

Kind channelKind (TrackKind k)
{
    switch (k) {
        case TrackKind::Audio: return Kind::Audio;
        case TrackKind::Instrument: return Kind::Midi;
        case TrackKind::Bus: return Kind::Bus;
        case TrackKind::Master: return Kind::Master;
        case TrackKind::AuxOut: return Kind::AuxOut;
    }
    return Kind::Audio;
}

PluginSlotPtr iconSlot (const TrackModel& t)
{
    if (t.instrument != nullptr) return t.instrument;
    if (! t.preFx.empty()) return t.preFx.front();
    if (! t.postFx.empty()) return t.postFx.front();
    return nullptr;
}

// ---- IconCache -----------------------------------------------------------------------------
IconCache& IconCache::get()
{
    static IconCache cache;
    return cache;
}

juce::Image IconCache::realIcon (const juce::PluginDescription& d)
{
    const auto key = d.createIdentifierString();
    auto it = real.find (key);
    if (it != real.end()) return it->second;
    return real.emplace (key, loadAudioUnitIcon (d)).first->second;
}

void IconCache::loadOverrides()
{
    if (loaded) return;
    loaded = true;
    if (auto xml = juce::XmlDocument::parse (PluginHost::appDataDir().getChildFile ("plugin-icons.xml")))
        for (auto* e : xml->getChildWithTagNameIterator ("Icon"))
            if (e->getStringAttribute ("key").isNotEmpty() && e->getStringAttribute ("value").isNotEmpty())
                overrides[e->getStringAttribute ("key")] = e->getStringAttribute ("value");
}

void IconCache::saveOverrides() const
{
    juce::XmlElement root ("Icons");
    for (auto& [k, v] : overrides) {
        auto* e = root.createNewChildElement ("Icon");
        e->setAttribute ("key", k);
        e->setAttribute ("value", v);
    }
    root.writeTo (PluginHost::appDataDir().getChildFile ("plugin-icons.xml"));
}

std::optional<juce::String> IconCache::override (const juce::String& key)
{
    loadOverrides();
    auto it = overrides.find (key);
    if (it == overrides.end()) return std::nullopt;
    return it->second;
}

void IconCache::setOverride (const juce::String& key, const juce::String& value)
{
    loadOverrides();
    if (value.isEmpty()) overrides.erase (key); else overrides[key] = value;
    saveOverrides();
}

juce::String IconCache::overrideKey (const TrackModel& t)
{
    if (auto s = iconSlot (t))
        if (s->description.name.isNotEmpty() || s->description.fileOrIdentifier.isNotEmpty())
            return s->description.createIdentifierString();
    return "track:" + juce::String ((int) t.kind);
}

juce::File IconCache::importImage (const juce::File& source) const
{
    auto dir = PluginHost::appDataDir().getChildFile ("Icons");
    dir.createDirectory();
    auto dest = dir.getNonexistentChildFile (source.getFileNameWithoutExtension(), source.getFileExtension(), false);
    return source.copyFileTo (dest) ? dest : source;
}

juce::Image IconCache::fileImage (const juce::String& path)
{
    auto it = files.find (path);
    if (it != files.end()) return it->second;
    return files.emplace (path, juce::ImageFileFormat::loadFrom (juce::File (path))).first->second;
}

void IconCache::drawIcon (juce::Graphics& g, juce::Rectangle<float> area, const TrackModel& t, juce::Colour tint)
{
    auto drawKind = [&] (Kind k) {
        g.setColour (tint);
        g.fillPath (iconPath (k), juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                      .getTransformToFit (juce::Rectangle<float> (0, 0, 24, 24), area));
    };
    auto drawImg = [&] (const juce::Image& img) {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (img, area, juce::RectanglePlacement::centred);
    };

    if (auto ov = override (overrideKey (t))) {
        if (ov->startsWith ("builtin:")) {
            if (auto k = kindFromName (ov->fromFirstOccurrenceOf ("builtin:", false, false))) { drawKind (*k); return; }
        } else if (auto img = fileImage (*ov); img.isValid()) {
            drawImg (img);
            return;
        }
    }
    if (auto slot = iconSlot (t)) {
        if (auto img = realIcon (slot->description); img.isValid()) { drawImg (img); return; }
        if (slot->description.name.isNotEmpty()) { drawKind (guessKind (slot->description)); return; }
    }
    drawKind (channelKind (t.kind));
}

} // namespace el::icons
