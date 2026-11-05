#pragma once
// Project save / load. A project is a folder "<name>.elooper/" holding session.xml and
// audio/loop_<loopId>_take<k>.wav (32-bit float). Message thread only.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include <functional>

namespace el {

class SessionSerializer {
public:
    static constexpr int kFormatVersion = 1;

    // Writes the whole session into projectFolder (created if needed) and sets session().file.
    static juce::Result save (EngineController& controller, const juce::File& projectFolder);

    // Reads projectFolder into a fresh Session and hands it to controller.loadSession().
    // `done` is called on the message thread (immediately on a parse failure).
    static void load (EngineController& controller, const juce::File& projectFolder,
                      std::function<void (juce::Result)> done);
};

} // namespace el
