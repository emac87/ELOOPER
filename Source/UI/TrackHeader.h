#pragma once
// Header of one track row on the loop screen: a compact horizontal strip with the track icon,
// name, input summary, mute / solo / arm / monitor, input gain / volume / pan bars, a meter, and
// a row of plug-in and send chips (click a plug-in to show / hide it, its dot to bypass it).
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/PresetManager.h"
#include "UI/Selection.h"
#include <functional>
#include <memory>
#include <vector>

namespace el {

class PluginIconButton;
class ValueBar;
class FxChip;

class TrackHeader : public juce::Component {
public:
    static constexpr int rowHeight = 96;
    static constexpr int defaultWidth = 452;

    TrackHeader (EngineController&, Selection&, PresetManager&, Id trackId, int width = defaultWidth);
    ~TrackHeader() override;

    Id getTrackId() const noexcept { return trackId; }
    void refresh();   // re-read name / colour / summaries / plug-ins / sends (cheap if unchanged)
    void poll();      // cheap 30 Hz state + meter poll; repaints only on change

    // Called when the header was dragged vertically and released (reorder).
    std::function<void (const juce::MouseEvent&)> onReorderDrop;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Used by the chips (they live in TrackHeader.cpp).
    void showSlotMenu (FxChain chain, int index, Id slotId, juce::Component* target);
    void showSendMenu (int sendIndex, juce::Component* target);
    void showAddFxMenu (juce::Component* target);
    void showAddSendMenu (juce::Component* target);
    void showOverflowMenu (juce::Component* target);
    EngineController& getController() noexcept { return controller; }

private:
    enum Button { None = -1, Mute, Solo, Arm, Monitor, Folder };
    Button buttonAt (juce::Point<int>) const;
    void showMenu();
    void rename();
    void confirmBounce();
    void confirmRemove();
    juce::String chainSignature (const TrackModel&) const;
    void rebuildChips (const TrackModel&);
    void layoutChips();

    EngineController& controller;
    Selection& selection;
    PresetManager& presets;
    const Id trackId;

    juce::Colour colour;
    juce::String nameText, line1;
    TrackKind kind = TrackKind::Audio;
    bool isFolderSum = false;
    int numOuts = 0;                 // multi-out instrument: number of aux outputs
    bool folderOpen = true;
    juce::Rectangle<int> muteR, soloR, armR, monR, folderR, meterR;

    // polled state
    bool muted = false, soloed = false, armed = false;
    MonitorMode monitor = MonitorMode::Auto;
    int meterL = 0, meterR2 = 0;

    std::unique_ptr<PluginIconButton> iconButton;
    std::unique_ptr<ValueBar> gainBar, volBar, panBar;

    // chip row
    juce::String lastChainSig;
    std::vector<std::unique_ptr<juce::Component>> chips;   // FxChip / ValueBar (sends) / buttons
    std::vector<FxChip*> fxChips;
    std::vector<ValueBar*> sendBars;
    std::unique_ptr<juce::TextButton> overflowButton;

    bool dragging = false;
};

} // namespace el
