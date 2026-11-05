#pragma once
// What the user has selected. Panels listen to it (ChangeBroadcaster) to follow the focus.
#include <JuceHeader.h>
#include "Core/Types.h"

namespace el {

struct Selection : public juce::ChangeBroadcaster {
    Id trackId = kInvalidId;
    Id loopId = kInvalidId;           // kInvalidId => track itself selected
    void select (Id track, Id loop = kInvalidId) {
        if (track == trackId && loop == loopId) return;
        trackId = track; loopId = loop; sendChangeMessage();
    }
    void clear() { select (kInvalidId, kInvalidId); }
};

} // namespace el
