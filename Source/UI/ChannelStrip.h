#pragma once
// One mixer channel strip (track, bus or master), styled like the loop page used to be: colour
// stripe, icon + name, M / S / arm / monitor, Logic-style slots (instrument, MIDI FX, audio FX,
// EQ, sends, post FX; click an empty slot to add), In and Pan bars, a fader with a meter, and
// the output slot. Owned by MixerPanel.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/PresetManager.h"
#include <memory>
#include <vector>

namespace el {

class GraphicEqEditor;
class ValueBar;
class FxChip;
class MenuSlot;
class PluginIconButton;

class ChannelStrip : public juce::Component {
public:
    ChannelStrip (EngineController&, PresetManager&, Id trackId);
    ~ChannelStrip() override;

    Id getTrackId() const noexcept { return trackId; }
    void rebuild();            // model changed: refresh names / plug-ins / sends / routing (cheap if unchanged)
    void refreshValues();      // UI timer: values, meters, LEDs (repaints only on change)
    void setSelected (bool);

    std::function<void (Id trackId)> onSelect;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum Button { None = -1, Mute, Solo, Arm, Monitor };
    Button buttonAt (juce::Point<int>) const;
    TrackPtr track() const { return controller.session().findTrack (trackId); }
    juce::String makeSignature (const TrackModel&) const;
    void updateHeader (const TrackModel&);
    void buildContent (const TrackModel&);
    void showSlotMenu (FxChip&);
    void showSendMenu (int sendIndex, juce::Component*);
    void showHardwareOutputMenu (juce::Component*);
    void openEqEditor();

    struct Content : public juce::Component {
        std::vector<std::pair<juce::String, int>> titles;   // section label + y
        void paint (juce::Graphics&) override;
    };

    EngineController& controller;
    PresetManager& presets;
    const Id trackId;
    bool selected = false, isMaster = false, muteGestureOpen = false;
    TrackKind kind = TrackKind::Audio;
    juce::Colour colour { 0xff4fc3f7 };
    juce::String nameText, line1, lastSignature;

    juce::Rectangle<int> muteR, soloR, armR, monR, meterR;
    bool muted = false, soloed = false, armed = false;
    MonitorMode monitor = MonitorMode::Auto;
    int meterL = 0, meterR2 = 0;

    std::unique_ptr<PluginIconButton> icon;
    juce::Viewport viewport;
    Content content;
    std::vector<std::unique_ptr<juce::Component>> owned;   // children of `content`
    std::vector<FxChip*> chips;
    std::vector<ValueBar*> sendBars;
    GraphicEqEditor* eqThumb = nullptr;
    juce::TextButton* eqToggle = nullptr;
    std::unique_ptr<ValueBar> gainBar, panBar, fader;
    std::unique_ptr<MenuSlot> outputSlot;
};

} // namespace el
