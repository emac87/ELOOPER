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

    // Writes the whole session into projectFolder and sets session().file (unless
    // updateSessionFile is false, e.g. for autosave copies). Atomic: everything is written to
    // "<folder>.saving", verified, then renamed into place; on any failure the previous project
    // at projectFolder is left untouched and the error is returned.
    static juce::Result save (EngineController& controller, const juce::File& projectFolder,
                              bool updateSessionFile = true);

    // Reads projectFolder into a fresh Session and hands it to controller.loadSession().
    // Takes that cannot be read are skipped (indices of the others are preserved) and reported
    // in a failed Result's text; the session is still loaded. `done` is called on the message thread (immediately on a parse failure).
    static void load (EngineController& controller, const juce::File& projectFolder,
                      std::function<void (juce::Result)> done);
};

} // namespace el
