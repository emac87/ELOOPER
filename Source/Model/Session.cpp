#include "Session.h"

namespace el {

PluginSlotPtr TrackModel::findPlugin (Id slotId) const {
    for (auto& p : allPlugins()) if (p && p->id == slotId) return p;
    return nullptr;
}

std::vector<PluginSlotPtr> TrackModel::allPlugins() const {
    std::vector<PluginSlotPtr> v (midiFx.begin(), midiFx.end());
    if (instrument) v.push_back (instrument);
    v.insert (v.end(), preFx.begin(), preFx.end());
    v.insert (v.end(), postFx.begin(), postFx.end());
    return v;
}

TrackPtr Session::findTrack (Id id) const {
    for (auto& t : tracks) if (t->id == id) return t;
    return nullptr;
}

LoopPtr Session::findLoop (Id loopId) const {
    for (auto& t : tracks) for (auto& l : t->loops) if (l->id == loopId) return l;
    return nullptr;
}

TrackPtr Session::trackOfLoop (Id loopId) const {
    for (auto& t : tracks) for (auto& l : t->loops) if (l->id == loopId) return t;
    return nullptr;
}

std::pair<TrackPtr, PluginSlotPtr> Session::findPlugin (Id slotId) const {
    for (auto& t : tracks) if (auto p = t->findPlugin (slotId)) return { t, p };
    return { nullptr, nullptr };
}

std::vector<TrackPtr> Session::buses() const {
    std::vector<TrackPtr> v;
    for (auto& t : tracks) if (t->kind == TrackKind::Bus) v.push_back (t);
    return v;
}

juce::Colour paletteColour (int i) {
    static const juce::uint32 c[] = { 0xffff5252, 0xffffab40, 0xffffee58, 0xff69f0ae, 0xff40c4ff,
                                      0xff7c4dff, 0xffff4081, 0xff64ffda, 0xffb2ff59, 0xffea80fc,
                                      0xff18ffff, 0xffffd740 };
    return juce::Colour (c[(size_t) i % (sizeof (c) / sizeof (c[0]))]);
}

} // namespace el
