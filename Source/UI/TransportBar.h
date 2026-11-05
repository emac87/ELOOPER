#pragma once
// Top bar: transport, position, tempo, signature, master bars, metronome, Link, page switcher,
// dock toggles, CPU meter and settings.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include <functional>

namespace el {

enum class Page { Loops, Timeline, Bounced };
enum class Dock { Mixer, Automation, Editor };

class TransportBar : public juce::Component, private juce::Timer {
public:
    explicit TransportBar (EngineController&);
    ~TransportBar() override;

    std::function<void (Page)> onPage;
    std::function<void (Dock, bool)> onDock;
    std::function<void()> onSettings;

    void setPage (Page);
    void setDockOn (Dock, bool);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Play / stop button drawn as a triangle or a square.
    struct PlayButton : public juce::Button {
        PlayButton() : juce::Button ("play") { setClickingTogglesState (false); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };
    // Tempo read-out: drag vertically to change, double-click to type.
    struct TempoLabel : public juce::Label {
        std::function<void (double)> onDragTempo;
        double dragStart = 120;
        double current = 120;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
    };

    void timerCallback() override;
    void refresh (bool force);
    void commitTempo();
    void tempoChanged (double bpm);

    EngineController& controller;

    PlayButton playButton;
    TempoLabel tempoLabel;
    juce::TextButton tapButton { "Tap" };
    juce::ComboBox sigBox, masterBox;
    juce::ToggleButton metroButton { "Metro" }, linkButton { "Link" }, syncButton { "Sync" }, arrButton { "Arr Rec" };
    juce::TextButton pageButtons[3];
    juce::ToggleButton dockButtons[3];
    juce::TextButton settingsButton { juce::String::fromUTF8 ("\xe2\x9a\x99") };

    // layout results (used by paint)
    juce::Rectangle<int> positionR, phaseR, cpuR, masterLabelR, tempoLabelR;

    // cached display state
    bool playing = false;
    int shownBar = -1, shownBeat = -1, shownPhase = -1, shownCpu = -1, shownPeers = -1;
    bool shownLinkEnabled = false;
    juce::String positionText;
    int lastMasterBars = -1, lastBpb = -1, lastUnit = -1;
    double lastTempo = -1;
};

} // namespace el
