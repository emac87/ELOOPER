#pragma once
// One mixer channel strip (track, bus or master). Owned by MixerPanel.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "UI/LevelMeter.h"

namespace el {

class GraphicEqEditor;
class StripKnob;
class StripSlotRow;
class StripSendRow;
class StripContent;

class ChannelStrip : public juce::Component {
public:
    ChannelStrip (EngineController&, Id trackId);
    ~ChannelStrip() override;

    Id getTrackId() const noexcept { return trackId; }
    void rebuild();            // model changed: refresh names/plugins/sends/routing (cheap if nothing changed)
    void refreshValues();      // UI timer: parameter values, meters, LEDs
    void setSelected (bool);

    std::function<void (Id trackId)> onSelect;
    std::function<void (Id slotId, juce::Component*)> onPluginPresetMenu;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    friend class StripSlotRow;
    friend class StripSendRow;

    TrackPtr track() const { return controller.session().findTrack (trackId); }
    juce::String makeSignature (const TrackModel&) const;
    void updateHeader (const TrackModel&);
    void buildContent (const TrackModel&);
    void clearContent();
    void addPluginMenu (FxChain chain, juce::Component* target);
    void showSlotMenu (FxChain chain, int index, PluginSlotPtr slot, juce::Component* target);
    void showAddSendMenu (juce::Component* target);
    void showSendMenu (int sendIndex);
    void showOutputMenu();
    void showHardwareOutputMenu();
    void openEqEditor();
    void cycleMonitor();
    void updateMonitorText (MonitorMode);
    void alertRouting (const juce::String& msg);

    EngineController& controller;
    const Id trackId;
    bool selected = false;
    bool isMaster = false;
    TrackKind kind = TrackKind::Audio;
    juce::Colour colour { 0xff4fc3f7 };
    juce::String lastSignature;

    juce::Label nameLabel;
    juce::Viewport viewport;
    std::unique_ptr<StripContent> content;
    std::vector<std::unique_ptr<juce::Component>> owned;   // children of `content`
    std::vector<StripSlotRow*> slotRows;
    std::vector<StripSendRow*> sendRows;
    StripKnob* inputKnob = nullptr;
    GraphicEqEditor* eqThumb = nullptr;
    juce::TextButton* eqToggle = nullptr;

    std::unique_ptr<StripKnob> panKnob, fader;
    juce::Label dbLabel;
    LevelMeter meter;
    juce::TextButton muteButton { "M" }, soloButton { "S" }, monitorButton, outputButton;
    juce::Rectangle<int> faderArea;
};

} // namespace el
