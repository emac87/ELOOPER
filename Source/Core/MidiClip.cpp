#include "MidiClip.h"
#include <algorithm>
#include <cmath>

namespace el {

void MidiClip::sortNotes() {
    std::stable_sort (notes.begin(), notes.end(), [] (const MidiNote& a, const MidiNote& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
}

void MidiClip::quantize (double grid, float strength, bool ends) {
    if (grid <= 0) return;
    for (auto& n : notes) {
        const double end = n.start + n.length;
        const double q = std::round (n.start / grid) * grid;
        n.start += (q - n.start) * strength;
        if (ends) {
            const double qe = std::max (n.start + grid, std::round (end / grid) * grid);
            n.length = std::max (grid * 0.25, (end + (qe - end) * strength) - n.start);
        } else {
            n.length = std::max (1.0e-3, end - n.start);
        }
        if (n.start >= lengthBeats) n.start = std::fmod (n.start, lengthBeats);
    }
    sortNotes();
}

static void buildNotes (const std::vector<MidiEvent>& events, double lengthBeats,
                        std::vector<MidiNote>& notes, std::vector<MidiEvent>& ctrls) {
    struct Held { bool on = false; double start = 0; uint8_t vel = 0; };
    Held held[16][128];
    std::vector<MidiEvent> sorted (events);
    std::stable_sort (sorted.begin(), sorted.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.beat < b.beat; });
    auto wrap = [&] (double b) { return lengthBeats > 0 ? std::fmod (std::max (0.0, b), lengthBeats) : b; };
    for (const auto& e : sorted) {
        const int ch = e.data[0] & 0x0F;
        if (e.isNoteOn()) {
            auto& h = held[ch][e.data[1] & 0x7F];
            if (h.on) notes.push_back ({ wrap (h.start), std::max (1.0e-3, e.beat - h.start), e.data[1], h.vel, (uint8_t) (ch + 1), false });
            h = { true, e.beat, e.data[2] };
        } else if (e.isNoteOff()) {
            auto& h = held[ch][e.data[1] & 0x7F];
            if (! h.on) continue;
            notes.push_back ({ wrap (h.start), std::max (1.0e-3, e.beat - h.start), e.data[1], h.vel, (uint8_t) (ch + 1), false });
            h.on = false;
        } else {
            MidiEvent c = e; c.beat = wrap (e.beat); ctrls.push_back (c);
        }
    }
    // Close hanging notes at the end of the recording pass.
    double last = lengthBeats;
    if (! sorted.empty()) last = std::max (last, sorted.back().beat);
    for (int ch = 0; ch < 16; ++ch)
        for (int p = 0; p < 128; ++p)
            if (held[ch][p].on)
                notes.push_back ({ wrap (held[ch][p].start), std::max (1.0e-3, last - held[ch][p].start), (uint8_t) p, held[ch][p].vel, (uint8_t) (ch + 1), false });
    for (auto& n : notes)
        if (lengthBeats > 0) n.length = std::min (n.length, lengthBeats);
}

MidiClip MidiClip::fromEvents (const std::vector<MidiEvent>& events, double lengthBeats) {
    MidiClip c; c.lengthBeats = lengthBeats;
    buildNotes (events, lengthBeats, c.notes, c.controllers);
    c.sortNotes();
    return c;
}

void MidiClip::mergeEvents (const std::vector<MidiEvent>& events) {
    buildNotes (events, lengthBeats, notes, controllers);
    sortNotes();
}

MidiSequence MidiSequence::fromClip (const MidiClip& clip) {
    MidiSequence s; s.lengthBeats = clip.lengthBeats;
    const double L = clip.lengthBeats;
    for (const auto& n : clip.notes) {
        if (n.muted || n.start >= L || n.length <= 0) continue;
        const uint8_t ch = (uint8_t) ((n.channel - 1) & 0x0F);
        MidiEvent on;  on.beat = n.start;  on.data[0] = (uint8_t) (0x90 | ch); on.data[1] = n.pitch; on.data[2] = std::max<uint8_t> (1, n.velocity);
        // Notes that run past the loop end are cut just before it, so they never hang across the wrap.
        MidiEvent off; off.beat = std::min (n.start + n.length, L - 1.0e-6); off.data[0] = (uint8_t) (0x80 | ch); off.data[1] = n.pitch; off.data[2] = 0;
        s.events.push_back (on); s.events.push_back (off);
    }
    for (const auto& c : clip.controllers) if (c.beat < L) s.events.push_back (c);
    std::stable_sort (s.events.begin(), s.events.end(), [] (const MidiEvent& a, const MidiEvent& b) {
        if (a.beat != b.beat) return a.beat < b.beat;
        return a.isNoteOff() && ! b.isNoteOff();
    });
    return s;
}

} // namespace el
