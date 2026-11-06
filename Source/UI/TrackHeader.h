#pragma once
// Header of one track column on the loop screen: colour stripe, name, kind, input summary,
// mute / solo / arm / monitor buttons and a small meter.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/PresetManager.h"
#include "UI/Selection.h"
#include <functional>
#include <memory>

namespace el {

class PluginIconButton;

class TrackHeader : public juce::Component {
public:
    static constexpr int headerHeight = 184;

    TrackHeader (EngineController&, Selection&, PresetManager&, Id trackId);
    ~TrackHeader() override;

    Id getTrackId() const noexcept { return trackId; }
    void refresh();   // re-read name / colour / summaries
    void poll();      // cheap 30 Hz state + meter poll; repaints only on change

    // Called when the header was dragged horizontally and released.
    std::function<void (const juce::MouseEvent&)> onReorderDrop;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum Button { None = -1, Mute, Solo, Arm, Monitor };
    Button buttonAt (juce::Point<int>) const;
    void showMenu();
    void rename();
    void confirmBounce();
    void confirmRemove();

    EngineController& controller;
    Selection& selection;
    PresetManager& presets;
    const Id trackId;

    juce::Colour colour;
    juce::String nameText, kindText, line1, line2;
    TrackKind kind = TrackKind::Audio;
    juce::Rectangle<int> muteR, soloR, armR, monR, meterR;

    // polled state
    bool muted = false, soloed = false, armed = false;
    MonitorMode monitor = MonitorMode::Auto;
    int meterL = 0, meterR2 = 0;

    std::unique_ptr<PluginIconButton> iconButton;

    bool dragging = false;
    int downX = 0;
};

} // namespace el
