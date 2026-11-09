#include "UI/MainComponent.h"
#include "UI/SettingsWindow.h"
#include "UI/PluginBrowser.h"
#include "Engine/SessionSerializer.h"
#include "Platform/Updater.h"

namespace el {

namespace {
    enum Commands : juce::CommandID {
        cmdNew = 0x3001, cmdOpen, cmdSave, cmdSaveAs, cmdSettings,
        cmdPlayStop, cmdRecord, cmdOverdub, cmdClear,
        cmdMixer, cmdAutomation, cmdEditor, cmdLoops, cmdTimeline, cmdBounced,
        cmdAddAudio, cmdAddInstrument, cmdAddBus, cmdBounceTrack, cmdPresets,
        cmdZoomIn, cmdZoomOut, cmdZoomReset, cmdCheckUpdates, cmdAutoUpdate,
        cmdOpenAutosave, cmdStopAll,
        cmdScene1 = 0x3200, cmdScene2, cmdScene3, cmdScene4, cmdScene5, cmdScene6, cmdScene7, cmdScene8, cmdScene9,
        cmdZoom100 = 0x3100, cmdZoom125, cmdZoom150, cmdZoom175, cmdZoom200
    };
    constexpr double zoomLevels[] = { 1.0, 1.25, 1.5, 1.75, 2.0 };
    constexpr double defaultUiScale = 1.25;
    constexpr int menuBarHeight = 24;
    constexpr int barThickness = 5;
    constexpr int autosaveIntervalMs = 2 * 60 * 1000;

    // Show the dirty marker in the title bar.
    juce::String titleFor (const juce::File& f, bool dirty)
    {
        return "ELooper - " + (f == juce::File() ? juce::String ("Untitled") : f.getFileNameWithoutExtension())
                            + (dirty ? juce::String (juce::CharPointer_UTF8 (" \xe2\x80\xa2")) : juce::String());
    }
}

MainComponent::MainComponent (EngineController& c, MidiMapping& mm, PresetManager& p)
    : controller (c), midiMap (mm), presets (p),
      transport (c),
      loopGrid (c, selection, mm, p),
      bouncedPage (c)
{
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ELooper";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.folderName = "ELooper";
        settings = std::make_unique<juce::PropertiesFile> (o);
        uiScale = juce::jlimit (0.75, 3.0, settings->getDoubleValue ("uiScale", defaultUiScale));
        juce::Desktop::getInstance().setGlobalScaleFactor ((float) uiScale);
    }

    timeline = std::make_unique<TimelineView> (controller, selection);
    mixer = std::make_unique<MixerPanel> (controller, selection, presets);
    automation = std::make_unique<AutomationPanel> (controller, selection);
    automation->onClose = [this] { setDock (Dock::Automation, false); };
    editor = std::make_unique<MidiEditor> (controller, selection);

    midiMap.selectedLoopProvider = [this] { return selection.loopId; };

    addAndMakeVisible (transport);
    addAndMakeVisible (loopGrid);
    addChildComponent (*timeline);
    addChildComponent (bouncedPage);
    for (int i = 0; i < 3; ++i) addChildComponent (*dockComponent (i));

    transport.onPage = [this] (Page pg) { setPage (pg); };
    transport.onDock = [this] (Dock d, bool on) { setDock (d, on); };
    transport.onSettings = [this] { showSettingsWindow (controller, midiMap); };
    loopGrid.onEditLoop = [this] (Id track, Id loop, bool midi) {
        selection.select (track, loop);
        if (midi) setDock (Dock::Editor, true);
    };

    // commands, menu, shortcuts
    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
#else
    menuBar = std::make_unique<juce::MenuBarComponent> (this);
    addAndMakeVisible (*menuBar);
#endif
    setApplicationCommandManagerToWatch (&commandManager);

    controller.addListener (this);
    selection.addChangeListener (this);

    // Pick a sensible initial selection.
    for (auto& t : controller.session().tracks)
        if (! t->bounced && t->kind != TrackKind::Master) { selection.select (t->id); break; }

    setPage (Page::Loops);
    updateTitle();
    setSize (1280, 800);
    startTimer (autosaveIntervalMs);
}

MainComponent::~MainComponent()
{
    stopTimer();
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
#endif
    setApplicationCommandManagerToWatch (nullptr);
    midiMap.selectedLoopProvider = nullptr;
    selection.removeChangeListener (this);
    controller.removeListener (this);
    for (auto& b : bars) b.reset();
}

juce::Component* MainComponent::dockComponent (int i)
{
    switch (i) {
        case 0: return mixer.get();
        case 1: return automation.get();
        default: return editor.get();
    }
}

void MainComponent::paint (juce::Graphics& g) { g.fillAll (theme::background); }

void MainComponent::parentHierarchyChanged()
{
    updateTitle();
    if (getPeer() != nullptr) grabKeyboardFocus();
}

// ---- layout ---------------------------------------------------------------------------------
void MainComponent::relayout()   // rebuilds the layout items (page / dock visibility changed)
{
    // Remember the dock heights the user dragged to.
    for (int i = 0; i < 3; ++i)
        if (dockItemIndex[i] >= 0 && dockVisible[i])
            dockHeight[i] = juce::jmax (80, layout.getItemCurrentAbsoluteSize (dockItemIndex[i]));
    for (auto& b : bars) b.reset();
    layout.clearAllItems();
    auto& comps = layoutComps;
    int n = 0;

    juce::Component* pageComp = page == Page::Loops ? static_cast<juce::Component*> (&loopGrid)
                              : page == Page::Timeline ? static_cast<juce::Component*> (timeline.get())
                                                       : static_cast<juce::Component*> (&bouncedPage);
    layout.setItemLayout (n, 150, -1.0, -0.6);
    comps[n++] = pageComp;

    for (int i = 0; i < 3; ++i) {
        auto* d = dockComponent (i);
        d->setVisible (dockVisible[i]);
        dockItemIndex[i] = -1;
        if (i == (int) Dock::Automation) continue;   // an overlay, placed by applyLayout()
        if (! dockVisible[i]) continue;
        layout.setItemLayout (n, barThickness, barThickness, barThickness);
        bars[i] = std::make_unique<juce::StretchableLayoutResizerBar> (&layout, n, false);
        addAndMakeVisible (*bars[i]);
        comps[n++] = bars[i].get();
        layout.setItemLayout (n, 80, -0.85, (double) dockHeight[i]);
        dockItemIndex[i] = n;
        comps[n++] = d;
    }

    layoutCount = n;
    applyLayout();
}

// Cheap re-layout (window resize / resizer-bar drag): the bars must stay alive while dragging.
void MainComponent::applyLayout()
{
    if (layoutCount == 0) return;
    auto r = getLocalBounds();
    r.removeFromTop (theme::transportHeight);
#if ! JUCE_MAC
    r.removeFromTop (menuBarHeight);
#endif
    layout.layOutComponents (layoutComps, layoutCount, r.getX(), r.getY(), r.getWidth(), r.getHeight(), true, true);

    // The automation layer sits on top of everything below the transport bar (page, mixer and
    // editor), see-through so the loops / notes / strips underneath stay visible.
    if (automation != nullptr && automation->isVisible()) {
        automation->setBounds (r);
        automation->toFront (false);
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
#if ! JUCE_MAC
    if (menuBar) menuBar->setBounds (r.removeFromTop (menuBarHeight));
#endif
    transport.setBounds (r.removeFromTop (theme::transportHeight));
    applyLayout();
}

// ---- pages / docks --------------------------------------------------------------------------
void MainComponent::setPage (Page p)
{
    page = p;
    loopGrid.setVisible (p == Page::Loops);
    timeline->setVisible (p == Page::Timeline);
    bouncedPage.setVisible (p == Page::Bounced);
    transport.setPage (p);
    if (p == Page::Timeline) controller.setTransportMode (TransportMode::Timeline);
    else if (p == Page::Loops) controller.setTransportMode (TransportMode::Loops);
    relayout();
    commandManager.commandStatusChanged();
}

void MainComponent::setDock (Dock d, bool visible)
{
    dockVisible[(int) d] = visible;
    transport.setDockOn (d, visible);
    relayout();
    commandManager.commandStatusChanged();
}

// ---- model changes --------------------------------------------------------------------------
void MainComponent::sessionChanged (Change what, Id)
{
    if (what == Change::Session) {
        selection.clear();
        for (auto& t : controller.session().tracks)
            if (! t->bounced && t->kind != TrackKind::Master) { selection.select (t->id); break; }
        setDirty (false);   // new / opened: clean (callers that load recovered work re-mark it)
        updateTitle();
    } else if (what == Change::Structure) {
        if (selection.trackId != kInvalidId && controller.session().findTrack (selection.trackId) == nullptr)
            selection.clear();
        else if (selection.loopId != kInvalidId && controller.session().findLoop (selection.loopId) == nullptr)
            selection.select (selection.trackId);
    }
    if (what != Change::Transport && what != Change::Session) {
        ++changeCount;
        setDirty (true);
    }
}

void MainComponent::setDirty (bool d)
{
    if (d) { if (dirty) return; dirty = true; }
    else   { if (! dirty) return; dirty = false; }
    updateTitle();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    commandManager.commandStatusChanged();
}

void MainComponent::updateTitle()
{
    if (auto* top = getTopLevelComponent()) {
        top->setName (titleFor (controller.session().file, dirty));
    }
}

// ---- actions --------------------------------------------------------------------------------
void MainComponent::newSession()
{
    auto* c = &controller;
    juce::Component::SafePointer<MainComponent> self (this);
    runIfSafeToDiscard ([self, c] {
        c->newSession();
        if (self != nullptr) self->setDirty (false);
    });
}

void MainComponent::runIfSafeToDiscard (std::function<void()> proceed)
{
    if (! dirty) { if (proceed) proceed(); return; }
    if (askingUnsaved) return;   // a prompt is already showing
    askingUnsaved = true;

    juce::Component::SafePointer<MainComponent> self (this);
    const auto name = controller.session().file == juce::File() ? juce::String ("Untitled")
                                                                : controller.session().file.getFileNameWithoutExtension();
    // Buttons: 1 = Save, 2 = Don't Save, 0 = Cancel (JUCE returns 0 for the last button).
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Unsaved changes")
                                      .withMessage ("Do you want to save the changes to \"" + name + "\" before continuing?")
                                      .withButton ("Save").withButton ("Don't Save").withButton ("Cancel"),
                                  [self, proceed] (int r) {
        if (self == nullptr) return;
        self->askingUnsaved = false;
        if (r == 1)
            self->saveProject ([self, proceed] (bool ok) { if (ok && self != nullptr && proceed) proceed(); });
        else if (r == 2) { if (proceed) proceed(); }
    });
}

juce::File MainComponent::defaultProjectDir() const
{
    const auto f = controller.session().file;
    if (f != juce::File()) return f.getParentDirectory();
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

juce::File MainComponent::autosaveDir()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("ELooper").getChildFile ("Autosave");
}

void MainComponent::openProject()
{
    juce::Component::SafePointer<MainComponent> self (this);
    runIfSafeToDiscard ([self] { if (self != nullptr) self->openProjectFrom (self->defaultProjectDir(), false); });
}

void MainComponent::openAutosave()
{
    juce::Component::SafePointer<MainComponent> self (this);
    runIfSafeToDiscard ([self] { if (self != nullptr) self->openProjectFrom (autosaveDir(), true); });
}

void MainComponent::openProjectFrom (const juce::File& startDir, bool isAutosave)
{
    chooser = std::make_unique<juce::FileChooser> (isAutosave ? "Open autosaved project" : "Open ELooper project", startDir);
    juce::Component::SafePointer<MainComponent> self (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [self, isAutosave] (const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (self == nullptr || f == juce::File()) return;
        self->loadProjectFolder (f, isAutosave);
    });
}

void MainComponent::loadProjectFolder (const juce::File& f, bool isAutosave)
{
    juce::Component::SafePointer<MainComponent> self (this);
    SessionSerializer::load (controller, f, [self, isAutosave] (juce::Result r) {
        if (self == nullptr) return;
        // A recovered autosave must not overwrite itself on Save: detach it from the file and
        // keep it marked as unsaved. Otherwise the freshly opened project is clean.
        if (isAutosave && self->controller.session().file != juce::File()) {
            self->controller.session().file = juce::File();
            self->setDirty (true);
        } else if (! isAutosave)
            self->setDirty (false);
        self->updateTitle();
        if (r.failed()) ui::alert ("Opened with problems", r.getErrorMessage());
    });
}

void MainComponent::saveProject (std::function<void (bool)> done)
{
    const auto f = controller.session().file;
    if (f != juce::File() && f.hasFileExtension ("elooper")) { const bool ok = writeProject (f); if (done) done (ok); }
    else saveProjectAs (std::move (done));
}

void MainComponent::saveProjectAs (std::function<void (bool)> done)
{
    const auto f = controller.session().file;
    const auto start = (f != juce::File() ? f.getParentDirectory() : defaultProjectDir())
                           .getChildFile (f != juce::File() ? f.getFileName() : juce::String ("Untitled.elooper"));
    chooser = std::make_unique<juce::FileChooser> ("Save ELooper project as", start, "*.elooper");
    juce::Component::SafePointer<MainComponent> self (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [self, done] (const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (self == nullptr) return;
        if (file == juce::File()) { if (done) done (false); return; }
        if (! file.hasFileExtension ("elooper")) file = file.withFileExtension ("elooper");
        const bool ok = self->writeProject (file);
        if (done) done (ok);
    });
}

bool MainComponent::writeProject (const juce::File& folder)
{
    // SessionSerializer::save is atomic and creates the folder itself; on failure the previous
    // project is untouched and the session stays dirty.
    const auto r = SessionSerializer::save (controller, folder);
    if (r.failed()) { ui::alert ("Save failed", r.getErrorMessage()); return false; }
    setDirty (false);
    autosavedAt = changeCount;
    updateTitle();
    return true;
}

void MainComponent::timerCallback()
{
    if (dirty && changeCount != autosavedAt && ! controller.transportStatus().playing)
        autosaveNow();
}

// Saves a copy into the Autosave folder; the session's own file and dirty flag are untouched.
void MainComponent::autosaveNow()
{
    const auto f = controller.session().file;
    auto name = (f == juce::File() ? juce::String ("Untitled") : f.getFileNameWithoutExtension()).replaceCharacters ("/\\:", "---");
    const auto target = autosaveDir().getChildFile (name + ".elooper");
    const auto r = SessionSerializer::save (controller, target, false);
    if (r.wasOk()) autosavedAt = changeCount;
    else DBG ("Autosave failed: " << r.getErrorMessage());
}

void MainComponent::clearSelectedLoop()
{
    const Id id = selection.loopId;
    if (id == kInvalidId) return;
    auto* c = &controller;
    ui::confirm ("Clear Loop", "Clear the recorded material of the selected loop?", "Clear", [c, id] { c->clearLoop (id); });
}

void MainComponent::bounceSelectedTrack()
{
    const Id id = selection.trackId;
    auto t = controller.session().findTrack (id);
    if (t == nullptr || t->kind == TrackKind::Master) return;
    auto* c = &controller;
    ui::confirm ("Bounce Track",
                 "This bakes the track's full output, including all effects, into a new audio loop on a new track. "
                 "The original track moves to the Bounced page (you can restore it later).",
                 "Bounce", [c, id] { c->bounceTrack (id); });
}

void MainComponent::addTrack (TrackKind k)
{
    const Id id = controller.addTrack (k);
    if (id == kInvalidId) return;
    selection.select (id);
    if (k == TrackKind::Instrument) showPluginMenu (controller, id, FxChain::Instrument, 0, &transport);
}

void MainComponent::setUiScale (double scale)
{
    uiScale = juce::jlimit (0.75, 3.0, scale);
    juce::Desktop::getInstance().setGlobalScaleFactor ((float) uiScale);
    if (settings != nullptr) { settings->setValue ("uiScale", uiScale); settings->saveIfNeeded(); }
    if (auto* top = getTopLevelComponent()) top->resized();
    resized();
    repaint();
    commandManager.commandStatusChanged();
}

// ---- menus & commands -----------------------------------------------------------------------
juce::StringArray MainComponent::getMenuBarNames() { return { "File", "Edit", "View", "Scenes", "Track" }; }

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    auto add = [&] (juce::CommandID id) { m.addCommandItem (&commandManager, id); };
    switch (index) {
        case 0: add (cmdNew); add (cmdOpen); add (cmdOpenAutosave); m.addSeparator(); add (cmdSave); add (cmdSaveAs); m.addSeparator();
                add (cmdCheckUpdates); add (cmdAutoUpdate); break;
        case 1: add (cmdSettings); break;
        case 2: add (cmdLoops); add (cmdTimeline); add (cmdBounced); m.addSeparator();
                add (cmdMixer); add (cmdAutomation); add (cmdEditor); m.addSeparator();
                {
                    juce::PopupMenu zoom;
                    for (int z = 0; z < 5; ++z) zoom.addCommandItem (&commandManager, cmdZoom100 + z);
                    zoom.addSeparator();
                    zoom.addCommandItem (&commandManager, cmdZoomIn);
                    zoom.addCommandItem (&commandManager, cmdZoomOut);
                    zoom.addCommandItem (&commandManager, cmdZoomReset);
                    m.addSubMenu ("Zoom", zoom);
                }
                break;
        case 3: for (int k = 0; k < 9; ++k) add (cmdScene1 + k);
                m.addSeparator(); add (cmdStopAll); break;
        case 4: add (cmdAddAudio); add (cmdAddInstrument); add (cmdAddBus); m.addSeparator();
                add (cmdBounceTrack); add (cmdPresets); break;
        default: break;
    }
    return m;
}

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& c)
{
    c.addArray ({ cmdNew, cmdOpen, cmdSave, cmdSaveAs, cmdSettings, cmdPlayStop, cmdRecord, cmdOverdub, cmdClear,
                  cmdMixer, cmdAutomation, cmdEditor, cmdLoops, cmdTimeline, cmdBounced,
                  cmdAddAudio, cmdAddInstrument, cmdAddBus, cmdBounceTrack, cmdPresets,
                  cmdZoomIn, cmdZoomOut, cmdZoomReset, cmdCheckUpdates, cmdAutoUpdate, cmdOpenAutosave, cmdStopAll,
                  cmdScene1, cmdScene2, cmdScene3, cmdScene4, cmdScene5, cmdScene6, cmdScene7, cmdScene8, cmdScene9,
                  cmdZoom100, cmdZoom125, cmdZoom150, cmdZoom175, cmdZoom200 });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& i)
{
    using juce::KeyPress; using juce::ModifierKeys;
    const auto cmd = ModifierKeys::commandModifier;
    const bool hasLoop = selection.loopId != kInvalidId;
    const bool hasTrack = selection.trackId != kInvalidId;
    switch (id) {
        case cmdNew:      i.setInfo ("New", "New session", "File", 0); i.addDefaultKeypress ('n', cmd); break;
        case cmdOpen:     i.setInfo ("Open...", "Open a project", "File", 0); i.addDefaultKeypress ('o', cmd); break;
        case cmdSave:     i.setInfo ("Save", "Save the project", "File", 0); i.addDefaultKeypress ('s', cmd); break;
        case cmdSaveAs:   i.setInfo ("Save As...", "Save the project under a new name", "File", 0);
                          i.addDefaultKeypress ('s', cmd | ModifierKeys::shiftModifier); break;
        case cmdOpenAutosave: {
            i.setInfo ("Open Autosave...", "Recover a project from the autosave folder", "File", 0);
            i.setActive (autosaveDir().isDirectory());
            break;
        }
        case cmdScene1: case cmdScene2: case cmdScene3: case cmdScene4: case cmdScene5:
        case cmdScene6: case cmdScene7: case cmdScene8: case cmdScene9: {
            const int n = (int) (id - cmdScene1) + 1;
            i.setInfo ("Launch Scene " + juce::String (n), "Launch scene " + juce::String (n), "Scenes", 0);
            i.addDefaultKeypress ('0' + n, ModifierKeys());
            i.setActive (n <= controller.numScenes());
            break;
        }
        case cmdStopAll:  i.setInfo ("Stop All Loops", "Stop every loop", "Scenes", 0);
                          i.addDefaultKeypress ('0', ModifierKeys()); break;
        case cmdCheckUpdates: i.setInfo ("Check for Updates...", "Look for a newer version of ELooper", "File", 0); break;
        case cmdAutoUpdate: i.setInfo ("Update Automatically", "Check for updates when ELooper starts", "File", 0);
                          i.setTicked (settings != nullptr && settings->getBoolValue (Updater::autoUpdateKey, true)); break;
        case cmdSettings: i.setInfo ("Settings...", "Audio, MIDI and plugin settings", "Edit", 0); i.addDefaultKeypress (',', cmd); break;
        case cmdPlayStop: i.setInfo ("Play / Stop", "Start or stop the transport", "Transport", 0);
                          i.addDefaultKeypress (KeyPress::spaceKey, ModifierKeys()); break;
        case cmdRecord:   i.setInfo ("Record / Trigger Loop", "Trigger the selected loop", "Loops", 0);
                          i.addDefaultKeypress ('r', ModifierKeys()); i.setActive (hasLoop); break;
        case cmdOverdub:  i.setInfo ("Overdub Loop", "Overdub the selected loop", "Loops", 0);
                          i.addDefaultKeypress ('o', ModifierKeys()); i.setActive (hasLoop); break;
        case cmdClear:    i.setInfo ("Clear Loop", "Clear the selected loop", "Loops", 0);
                          i.addDefaultKeypress (KeyPress::backspaceKey, ModifierKeys());
                          i.addDefaultKeypress (KeyPress::deleteKey, ModifierKeys()); i.setActive (hasLoop); break;
        case cmdMixer:    i.setInfo ("Mixer", "Show or hide the mixer", "View", 0);
                          i.addDefaultKeypress ('m', ModifierKeys()); i.setTicked (dockVisible[0]); break;
        case cmdAutomation: i.setInfo ("Automation", "Show or hide automation", "View", 0);
                          i.addDefaultKeypress ('a', ModifierKeys()); i.setTicked (dockVisible[1]); break;
        case cmdEditor:   i.setInfo ("Editor", "Show or hide the MIDI editor", "View", 0);
                          i.addDefaultKeypress ('e', ModifierKeys()); i.setTicked (dockVisible[2]); break;
        case cmdLoops:    i.setInfo ("Loops", "Loop screen", "View", 0); i.setTicked (page == Page::Loops); break;
        case cmdTimeline: i.setInfo ("Timeline", "Timeline screen", "View", 0);
                          i.addDefaultKeypress ('t', ModifierKeys()); i.setTicked (page == Page::Timeline); break;
        case cmdBounced:  i.setInfo ("Bounced", "Bounced tracks", "View", 0); i.setTicked (page == Page::Bounced); break;
        case cmdZoomIn:   i.setInfo ("Zoom In", "Make the interface larger", "View", 0);
                          i.addDefaultKeypress ('=', cmd); i.addDefaultKeypress ('+', cmd); break;
        case cmdZoomOut:  i.setInfo ("Zoom Out", "Make the interface smaller", "View", 0); i.addDefaultKeypress ('-', cmd); break;
        case cmdZoomReset: i.setInfo ("Reset Zoom", "Reset the interface size to default", "View", 0); i.addDefaultKeypress ('0', cmd); break;
        case cmdZoom100: case cmdZoom125: case cmdZoom150: case cmdZoom175: case cmdZoom200: {
            const double z = zoomLevels[id - cmdZoom100];
            i.setInfo (juce::String (juce::roundToInt (z * 100.0)) + "%", "Set interface zoom", "View", 0);
            i.setTicked (std::abs (uiScale - z) < 0.01);
            break;
        }
        case cmdAddAudio: i.setInfo ("Add Audio Track", "Add an audio track", "Track", 0); break;
        case cmdAddInstrument: i.setInfo ("Add Instrument Track", "Add an instrument track", "Track", 0); break;
        case cmdAddBus:   i.setInfo ("Add Bus", "Add a bus", "Track", 0); break;
        case cmdBounceTrack: i.setInfo ("Bounce Track", "Bounce the selected track", "Track", 0);
                          i.addDefaultKeypress ('b', cmd); i.setActive (hasTrack); break;
        case cmdPresets:  i.setInfo ("Channel Presets...", "Channel presets for the selected track", "Track", 0);
                          i.setActive (hasTrack); break;
        default: break;
    }
}

// True while a text field or combo box has keyboard focus: digits belong to it then.
bool MainComponent::sceneKeysBlocked() const
{
    auto* f = juce::Component::getCurrentlyFocusedComponent();
    if (f == nullptr) return false;
    if (dynamic_cast<juce::TextInputTarget*> (f) != nullptr || dynamic_cast<juce::ComboBox*> (f) != nullptr)
        return true;
    return f->findParentComponentOfClass<juce::ComboBox>() != nullptr;
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID) {
        case cmdNew:      newSession(); return true;
        case cmdOpen:     openProject(); return true;
        case cmdOpenAutosave: openAutosave(); return true;
        case cmdScene1: case cmdScene2: case cmdScene3: case cmdScene4: case cmdScene5:
        case cmdScene6: case cmdScene7: case cmdScene8: case cmdScene9:
            if (info.invocationMethod == InvocationInfo::fromKeyPress && sceneKeysBlocked()) return false;
            controller.launchScene ((int) (info.commandID - cmdScene1));
            return true;
        case cmdStopAll:
            if (info.invocationMethod == InvocationInfo::fromKeyPress && sceneKeysBlocked()) return false;
            controller.stopAllLoops();
            return true;
        case cmdSave:     saveProject(); return true;
        case cmdSaveAs:   saveProjectAs(); return true;
        case cmdSettings: showSettingsWindow (controller, midiMap); return true;
        case cmdCheckUpdates: if (onCheckForUpdates) onCheckForUpdates(); return true;
        case cmdAutoUpdate:
            if (settings != nullptr) {
                settings->setValue (Updater::autoUpdateKey, ! settings->getBoolValue (Updater::autoUpdateKey, true));
                settings->saveIfNeeded();
                commandManager.commandStatusChanged();
            }
            return true;
        case cmdPlayStop: controller.togglePlay(); return true;
        case cmdRecord:   if (selection.loopId != kInvalidId) controller.triggerLoop (selection.loopId); return true;
        case cmdOverdub:  if (selection.loopId != kInvalidId) controller.overdubLoop (selection.loopId); return true;
        case cmdClear:    clearSelectedLoop(); return true;
        case cmdMixer:    setDock (Dock::Mixer, ! dockVisible[0]); return true;
        case cmdAutomation: setDock (Dock::Automation, ! dockVisible[1]); return true;
        case cmdEditor:   setDock (Dock::Editor, ! dockVisible[2]); return true;
        case cmdLoops:    setPage (Page::Loops); return true;
        case cmdTimeline: setPage (page == Page::Timeline ? Page::Loops : Page::Timeline); return true;
        case cmdBounced:  setPage (Page::Bounced); return true;
        case cmdZoomIn: {
            for (double z : zoomLevels) if (z > uiScale + 0.01) { setUiScale (z); break; }
            return true;
        }
        case cmdZoomOut: {
            for (int k = 4; k >= 0; --k) if (zoomLevels[k] < uiScale - 0.01) { setUiScale (zoomLevels[k]); break; }
            return true;
        }
        case cmdZoomReset: setUiScale (defaultUiScale); return true;
        case cmdZoom100: case cmdZoom125: case cmdZoom150: case cmdZoom175: case cmdZoom200:
            setUiScale (zoomLevels[info.commandID - cmdZoom100]); return true;
        case cmdAddAudio: addTrack (TrackKind::Audio); return true;
        case cmdAddInstrument: addTrack (TrackKind::Instrument); return true;
        case cmdAddBus:   addTrack (TrackKind::Bus); return true;
        case cmdBounceTrack: bounceSelectedTrack(); return true;
        case cmdPresets:
            if (selection.trackId != kInvalidId) presets.showChannelPresetMenu (selection.trackId, &transport);
            return true;
        default: return false;
    }
}

} // namespace el
