#pragma once
// The application shell: transport bar, page area (Loops / Timeline / Bounced), a resizable
// lower dock (mixer / automation / MIDI editor), menu bar and keyboard shortcuts.
#include <JuceHeader.h>
#include "Engine/EngineController.h"
#include "Engine/MidiMapping.h"
#include "Engine/PresetManager.h"
#include "UI/Selection.h"
#include "UI/LookAndFeel.h"
#include "UI/TransportBar.h"
#include "UI/LoopGrid.h"
#include "UI/BouncedPage.h"
#include "UI/MixerPanel.h"
#include "UI/AutomationPanel.h"
#include "UI/MidiEditor.h"
#include "UI/TimelineView.h"
#include <memory>

namespace el {

class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      public juce::ApplicationCommandTarget,
                      private EngineController::Listener,
                      private juce::ChangeListener {
public:
    MainComponent (EngineController&, MidiMapping&, PresetManager&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int, int) override {}

    // ApplicationCommandTarget
    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

private:
    struct LafInstaller {
        explicit LafInstaller (juce::LookAndFeel& l) { juce::LookAndFeel::setDefaultLookAndFeel (&l); }
        ~LafInstaller() { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
    };

    void sessionChanged (Change what, Id id) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void setPage (Page);
    void setDock (Dock, bool visible);
    void relayout();
    void applyLayout();
    juce::Component* dockComponent (int i);

    void newSession();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void writeProject (const juce::File& folder);
    void updateTitle();
    void clearSelectedLoop();
    void bounceSelectedTrack();
    void addTrack (TrackKind);
    juce::File defaultProjectDir() const;

    EngineController& controller;
    MidiMapping& midiMap;
    PresetManager& presets;

    ELooperLookAndFeel laf;
    LafInstaller lafInstaller { laf };
    Selection selection;
    juce::ApplicationCommandManager commandManager;
    juce::TooltipWindow tooltips { nullptr, 600 };

    TransportBar transport;
    LoopGrid loopGrid;
    std::unique_ptr<TimelineView> timeline;
    BouncedPage bouncedPage;
    std::unique_ptr<MixerPanel> mixer;
    std::unique_ptr<AutomationPanel> automation;
    std::unique_ptr<MidiEditor> editor;

#if ! JUCE_MAC
    std::unique_ptr<juce::MenuBarComponent> menuBar;
#endif

    juce::StretchableLayoutManager layout;
    std::unique_ptr<juce::StretchableLayoutResizerBar> bars[3];
    juce::Component* layoutComps[8] {};
    int layoutCount = 0;
    int dockItemIndex[3] { -1, -1, -1 };
    bool dockVisible[3] { false, false, false };
    int dockHeight[3] { 280, 200, 320 };

    Page page = Page::Loops;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace el
