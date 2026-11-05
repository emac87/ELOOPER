#pragma once
// MIDI controller input: routes incoming CC / note messages to EngineController actions,
// with MIDI learn. Mappings persist to appDataDir()/midi-mappings.xml. Message thread API;
// the MIDI-thread callback only queues messages and triggers an async update.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include <functional>
#include <map>
#include <vector>

namespace el {

struct MappingTarget {
    enum class Kind { TriggerLoop, RecordLoop, StopLoop, PlayStop, Record /*selected loop*/, StopAll, TapTempo, Param } kind = Kind::PlayStop;
    Id trackId = 0; Id loopId = 0; ParamTarget param;
};

class MidiMapping : private juce::MidiInputCallback,
                    private juce::AsyncUpdater,
                    private EngineController::Listener {
public:
    explicit MidiMapping (EngineController&);
    ~MidiMapping() override;

    void startLearn (const MappingTarget& target);   // next incoming CC/note gets mapped to target
    void cancelLearn();
    bool isLearning() const;

    std::function<void()> onMappingsChanged;
    std::function<Id()> selectedLoopProvider;        // used by Kind::Record (MainComponent sets it)

    struct Mapping {
        MappingTarget target;
        juce::String deviceId;
        int channel = 1;        // 1..16
        bool isCC = true;
        int number = 0;
        juce::String description;
    };
    const std::vector<Mapping>& mappings() const { return maps; }
    void removeMapping (int index);
    void clearAll();

private:
    struct Incoming { juce::MidiMessage message; juce::String deviceId; };
    struct LambdaTimer : juce::Timer {
        std::function<void()> fn;
        void timerCallback() override { if (fn) fn(); }
    };
    struct Gesture { Id trackId; ParamTarget param; juce::uint32 lastMs; };

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;
    void sessionChanged (Change what, Id id) override;

    void process (const Incoming&);
    void learnFrom (const Incoming&);
    void dispatch (const Mapping&, int value, bool isNoteOn);
    void addLearned (Mapping m);
    juce::String describe (const MappingTarget&) const;
    void changed();
    void save() const;
    void load();
    void pruneInvalid();
    void endIdleGestures (bool all);
    void checkDevices();
    static juce::File storeFile();

    EngineController& controller;
    std::vector<Mapping> maps;
    juce::CriticalSection queueLock;
    std::vector<Incoming> queue;
    bool learning = false;
    MappingTarget learnTarget;
    std::map<juce::String, int> lastCC;
    std::vector<Gesture> gestures;
    juce::StringArray knownDevices;
    LambdaTimer gestureTimer, deviceTimer;
};

} // namespace el
