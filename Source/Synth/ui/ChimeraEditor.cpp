#include "Synth/ui/ChimeraEditor.h"
#include "UI/LookAndFeel.h"

namespace el::chimera {

namespace {
    bool isAudioExt (const juce::File& f)
    {
        static const juce::StringArray exts { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg" };
        return exts.contains (f.getFileExtension().toLowerCase());
    }
    bool isCartExt (const juce::File& f) { return f.getFileExtension().equalsIgnoreCase (".syx"); }
    void showMsg (const juce::String& msg) { if (msg.isNotEmpty()) el::ui::alert ("Chimera", msg); }
}

// =====================================================================================================
// Top bar: wordmark, presets, macros, master, voices / meter
// =====================================================================================================
class TopBar : public Panel {
public:
    TopBar (Ctx& c, ChimeraProcessor& p) : Panel (c, {}), proc (p)
    {
        prev = &button ("<", [this] { stepPreset (-1); });
        next = &button (">", [this] { stepPreset (1); });
        prev->setTooltip ("Previous factory preset");
        next->setTooltip ("Next factory preset");
        presetMenu = &make<MenuButton>();
        presetMenu->getText = [this] {
            const auto& pt = ctx.P();
            return juce::String (pt.name) + (pt.category.empty() ? juce::String() : "   -   " + juce::String (pt.category));
        };
        presetMenu->onClick = [this] (juce::Component*) { showPresetMenu(); };
        pollers.push_back ([this] { presetMenu->poll(); });
        saveBtn = &button ("Save", [this] { save(); });
        saveAsBtn = &button ("Save as", [this] { saveAs(); });
        loadBtn = &button ("Load", [this] { loadFromFile(); });
        saveBtn->setTooltip ("Save over the current user preset (or ask for a name)");
        loadBtn->setTooltip ("Load a .chimera preset file");
        for (int i = 0; i < kMacros; ++i) {
            auto& b = paramBar ("Macro " + juce::String (i + 1), [this, i] { return proc.macroParam (i); });
            b.onRightClick = [this, i] (juce::Component*) { renameMacro (i); };
            b.setTooltip ("Macro (right-click to rename). Use it as a source in the mod matrix.");
            macro[i] = &b;
        }
        master = &paramBar ("Master", [this] { return proc.masterParam(); });
        voices = &make<Caption>();
        voices->just = juce::Justification::centredRight;
        meter = &make<PeakMeter>();
        meter->setTooltip ("Output level");
        pollers.push_back ([this] {
            for (int i = 0; i < kMacros; ++i) {
                const auto nm = juce::String (ctx.P().macroNames[(size_t) i]);
                if (macro[i]->label != nm) { macro[i]->label = nm; macro[i]->repaint(); }
            }
        });
    }

    void setActivity (int nVoices, float peak)
    {
        voices->setText ("Voices " + juce::String (nVoices));
        meter->setLevel (peak);
    }

    void paint (juce::Graphics& g) override
    {
        Panel::paint (g);
        g.setColour (theme::text);
        g.setFont (theme::font (21.0f, true));
        g.drawText ("CHIMERA", 14, 8, 130, 24, juce::Justification::centredLeft, false);
        for (int i = 0; i < 4; ++i) {
            g.setColour (layerColour (i));
            g.fillRoundedRectangle ((float) (15 + i * 31), 34.0f, 28.0f, 3.0f, 1.5f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 7);
        auto row1 = r.removeFromTop (30);
        r.removeFromTop (5);
        auto row2 = r;
        row1.removeFromLeft (136);
        auto right = row1.removeFromRight (300);
        meter->setBounds (right.removeFromRight (84).reduced (0, 9));
        right.removeFromRight (8);
        voices->setBounds (right.removeFromRight (66));
        right.removeFromRight (6);
        master->setBounds (right);
        row1.removeFromRight (10);
        prev->setBounds (row1.removeFromLeft (26)); row1.removeFromLeft (4);
        auto btns = row1.removeFromRight (176);
        row1.removeFromRight (8);
        next->setBounds (row1.removeFromRight (26)); row1.removeFromRight (4);
        presetMenu->setBounds (row1);
        saveBtn->setBounds (btns.removeFromLeft (52)); btns.removeFromLeft (4);
        saveAsBtn->setBounds (btns.removeFromLeft (66)); btns.removeFromLeft (4);
        loadBtn->setBounds (btns);
        const int gap = 8, w = (row2.getWidth() - 3 * gap) / 4;
        for (int i = 0; i < kMacros; ++i) macro[i]->setBounds (row2.getX() + i * (w + gap), row2.getY(), w, row2.getHeight());
    }

private:
    void stepPreset (int d)
    {
        const int n = ChimeraProcessor::numFactoryPresets();
        if (n <= 0) return;
        proc.loadFactoryPreset ((proc.getCurrentProgram() + d + n) % n);
        currentUserFile = juce::File();
    }

    void showPresetMenu()
    {
        juce::PopupMenu m;
        const int n = ChimeraProcessor::numFactoryPresets();
        const int cur = proc.getCurrentProgram();
        const bool curIsFactory = cur >= 0 && cur < n && ctx.P().name == ChimeraProcessor::factoryPresetName (cur).toStdString();
        juce::StringArray cats;
        for (int i = 0; i < n; ++i) {
            auto c = ChimeraProcessor::factoryPresetCategory (i);
            cats.addIfNotAlreadyThere (c.isEmpty() ? juce::String ("Other") : c);
        }
        for (const auto& cat : cats) {
            juce::PopupMenu s;
            for (int i = 0; i < n; ++i) {
                auto c = ChimeraProcessor::factoryPresetCategory (i);
                if ((c.isEmpty() ? juce::String ("Other") : c) == cat)
                    s.addItem (1000 + i, ChimeraProcessor::factoryPresetName (i), true, curIsFactory && i == cur);
            }
            m.addSubMenu (cat, s);
        }
        m.addSeparator();
        userFiles.clear();
        auto found = ChimeraProcessor::presetFolder().findChildFiles (juce::File::findFiles, false, "*.chimera");
        std::sort (found.begin(), found.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });
        juce::PopupMenu u;
        for (int i = 0; i < (int) found.size() && i < 400; ++i) {
            userFiles.push_back (found[(size_t) i]);
            u.addItem (5000 + i, found[(size_t) i].getFileNameWithoutExtension(), true, found[(size_t) i] == currentUserFile);
        }
        if (userFiles.empty()) u.addItem (-1, "(no saved presets yet)", false);
        m.addSubMenu ("User presets", u);
        m.addSeparator();
        m.addItem (1, "Save");
        m.addItem (2, "Save as...");
        m.addItem (3, "Load from file...");
        m.addItem (4, "Show preset folder");
        juce::Component::SafePointer<TopBar> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetMenu).withMinimumWidth (presetMenu->getWidth()),
                         [sp] (int id) { if (sp != nullptr && id > 0) sp->handleMenu (id); });
    }

    void handleMenu (int id)
    {
        if (id >= 5000) {
            const auto idx = (size_t) (id - 5000);
            if (idx < userFiles.size()) { showMsg (proc.loadPreset (userFiles[idx])); currentUserFile = userFiles[idx]; }
        } else if (id >= 1000) {
            proc.loadFactoryPreset (id - 1000);
            currentUserFile = juce::File();
        } else if (id == 1) save();
        else if (id == 2) saveAs();
        else if (id == 3) loadFromFile();
        else if (id == 4) ChimeraProcessor::presetFolder().revealToUser();
    }

    void doSave (const juce::File& file)
    {
        const auto stem = file.getFileNameWithoutExtension().toStdString();
        ctx.edit ([stem] (Patch& p) { p.name = stem; });
        const auto err = proc.savePreset (file);
        if (err.isEmpty()) currentUserFile = file;
        showMsg (err);
    }
    void save() { if (currentUserFile.existsAsFile()) doSave (currentUserFile); else saveAs(); }
    void saveAs()
    {
        auto def = ChimeraProcessor::presetFolder().getChildFile (juce::File::createLegalFileName (juce::String (ctx.P().name)) + ".chimera");
        chooser = std::make_unique<juce::FileChooser> ("Save Chimera preset", def, "*.chimera");
        juce::Component::SafePointer<TopBar> sp (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [sp] (const juce::FileChooser& fc) {
                                  auto f = fc.getResult();
                                  if (sp == nullptr || f == juce::File()) return;
                                  if (! f.hasFileExtension ("chimera")) f = f.withFileExtension ("chimera");
                                  sp->doSave (f);
                              });
    }
    void loadFromFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load Chimera preset", ChimeraProcessor::presetFolder(), "*.chimera");
        juce::Component::SafePointer<TopBar> sp (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [sp] (const juce::FileChooser& fc) {
                                  const auto f = fc.getResult();
                                  if (sp == nullptr || ! f.existsAsFile()) return;
                                  showMsg (sp->proc.loadPreset (f));
                                  sp->currentUserFile = f;
                              });
    }
    void renameMacro (int i)
    {
        juce::Component::SafePointer<TopBar> sp (this);
        el::ui::askText ("Rename macro", "Name for macro " + juce::String (i + 1) + ":", juce::String (ctx.P().macroNames[(size_t) i]),
                         [sp, i] (juce::String name) {
                             if (sp == nullptr) return;
                             name = name.trim().substring (0, 24);
                             if (name.isEmpty()) name = "Macro " + juce::String (i + 1);
                             sp->ctx.edit ([i, name] (Patch& p) { p.macroNames[(size_t) i] = name.toStdString(); });
                         });
    }

    ChimeraProcessor& proc;
    PillButton *prev {}, *next {}, *saveBtn {}, *saveAsBtn {}, *loadBtn {};
    MenuButton* presetMenu {};
    ValueBar *macro[kMacros] {}, *master {};
    Caption* voices {};
    PeakMeter* meter {};
    std::vector<juce::File> userFiles;
    juce::File currentUserFile;
    std::unique_ptr<juce::FileChooser> chooser;
};

// =====================================================================================================
// Layer tabs
// =====================================================================================================
class LayerTabBar : public juce::Component {
public:
    explicit LayerTabBar (Ctx& c) : ctx (c) {}
    std::function<void (int)> onSelect, onPower;

    void poll()
    {
        juce::String sig = juce::String (ctx.layer);
        for (int i = 0; i < kMaxLayers; ++i) sig << "|" << label (i);
        if (sig != shownSig) { shownSig = sig; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < kMaxLayers; ++i) {
            const auto b = tabBounds (i).toFloat();
            const bool sel = i == ctx.layer, on = ctx.P().layers[(size_t) i].engine != Engine::Off;
            const auto col = layerColour (i);
            g.setColour (sel ? theme::panelRaised : theme::panel);
            g.fillRoundedRectangle (b, theme::cornerRadius);
            g.setColour (col.withAlpha (on ? 1.0f : 0.25f));
            juce::Path stripe;
            stripe.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), 4.0f, 6.0f, 6.0f, true, true, false, false);
            g.fillPath (stripe);
            g.setColour (sel ? col : theme::outline);
            g.drawRoundedRectangle (b.reduced (0.5f), theme::cornerRadius, sel ? 1.6f : 1.0f);
            g.setColour (on ? col : theme::textDim);
            g.setFont (theme::font (17.0f, true));
            g.drawText (juce::String (i + 1), (int) b.getX() + 10, (int) b.getY() + 4, 20, (int) b.getHeight() - 4, juce::Justification::centredLeft, false);
            g.setColour (on ? theme::text : theme::textDim);
            g.setFont (theme::font (13.5f, on));
            auto t = b.toNearestInt().withTrimmedLeft (32).withTrimmedRight (30).withTrimmedTop (4);
            g.drawText (label (i), t, juce::Justification::centredLeft, true);
            const auto d = powerBounds (i).toFloat();
            g.setColour (on ? col : theme::outline);
            g.fillEllipse (d.reduced (3.0f));
            g.setColour (on ? col.brighter (0.4f) : theme::textDim);
            g.drawEllipse (d.reduced (3.0f), 1.2f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < kMaxLayers; ++i) {
            if (! tabBounds (i).contains (e.getPosition())) continue;
            if (powerBounds (i).expanded (4).contains (e.getPosition())) { if (onPower) onPower (i); }
            else if (onSelect) onSelect (i);
            return;
        }
    }

private:
    juce::String label (int i) const
    {
        const auto& l = ctx.P().layers[(size_t) i];
        if (l.engine == Engine::Off) return "Off";
        return l.name.empty() ? juce::String (engineName (l.engine)) : juce::String (l.name);
    }
    juce::Rectangle<int> tabBounds (int i) const
    {
        const int gap = 6, w = juce::jmin (190, (getWidth() - 3 * gap) / 4);
        return { i * (w + gap), 0, w, getHeight() };
    }
    juce::Rectangle<int> powerBounds (int i) const
    {
        auto b = tabBounds (i);
        return { b.getRight() - 26, b.getY() + (b.getHeight() + 4 - 18) / 2, 18, 18 };
    }
    Ctx& ctx;
    juce::String shownSig;
};

// =====================================================================================================
// Editor
// =====================================================================================================
ChimeraEditor::ChimeraEditor (ChimeraProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p), ctx { p, 0 }
{
    for (int i = 0; i < kMaxLayers; ++i) {
        const auto e = proc.patch().layers[(size_t) i].engine;
        lastEngine[i] = e == Engine::Off ? Engine::Analog : e;
    }

    hooks.setEngine = [this] (Engine e) { setEngine (e); };
    hooks.copyLayer = [this] {
        proc.editPatch ([] (Patch&) {});   // syncs the automatable fields (level, cutoff...) into the patch first
        const auto l = (size_t) ctx.layer;
        clip.valid = true;
        clip.params = proc.patch().layers[l];
        clip.sample = proc.patch().samples[l];
        clip.cart = proc.patch().carts[l];
        clip.cartName = proc.patch().cartNames[l];
    };
    hooks.pasteLayer = [this] {
        if (! clip.valid) return;
        const auto l = (size_t) ctx.layer;
        const auto c = clip;
        ctx.edit ([l, c] (Patch& pt) {
            pt.layers[l] = c.params;
            pt.samples[l] = c.sample;
            pt.carts[l] = c.cart;
            pt.cartNames[l] = c.cartName;
        });
        if (clip.params.engine != Engine::Off) lastEngine[ctx.layer] = clip.params.engine;
    };
    hooks.initLayer = [this] {
        const auto l = (size_t) ctx.layer;
        ctx.edit ([l] (Patch& pt) {
            LayerParams fresh;
            fresh.engine = Engine::Analog;
            pt.layers[l] = fresh;
            pt.samples[l] = nullptr;
            pt.carts[l] = nullptr;
            pt.cartNames[l].clear();
        });
        lastEngine[ctx.layer] = Engine::Analog;
    };
    hooks.canPaste = [this] { return clip.valid; };

    topBar = std::make_unique<TopBar> (ctx, proc);
    layerTabs = std::make_unique<LayerTabBar> (ctx);
    layerTabs->onSelect = [this] (int i) { selectLayer (i); };
    layerTabs->onPower = [this] (int i) { toggleLayerPower (i); };

    static const char* pageNames[numPages] = { "SOUND", "MODULATION", "EFFECTS" };
    for (int i = 0; i < numPages; ++i) {
        auto b = std::make_unique<PillButton>();
        b->text = pageNames[i];
        b->get = [this, i] { return page == i; };
        b->set = [this, i] (bool) { selectPage (i); };
        pageButtons[i] = std::move (b);
        addAndMakeVisible (*pageButtons[i]);
    }

    voice = makeVoicePanel (ctx, hooks);
    engine = makeEnginePanel (ctx, hooks);
    filter = makeFilterPanel (ctx);
    for (int i = 0; i < 3; ++i) env[i] = makeEnvPanel (ctx, i);
    for (int i = 0; i < kLayerLfos; ++i) lfo[i] = makeLfoPanel (ctx, false, i);
    for (int i = 0; i < kGlobalLfos; ++i) globalLfo[i] = makeLfoPanel (ctx, true, i);
    matrix = makeMatrixPanel (ctx);
    for (int i = 0; i < 3; ++i) fx[i] = makeFxPanel (ctx, i);

    layerPanels = { voice.get(), engine.get(), filter.get(), env[0].get(), env[1].get(), env[2].get(), lfo[0].get(), lfo[1].get(), matrix.get() };
    allPanels = layerPanels;
    for (auto& g : globalLfo) allPanels.push_back (g.get());
    for (auto& f : fx) allPanels.push_back (f.get());

    addAndMakeVisible (*topBar);
    addAndMakeVisible (*layerTabs);
    for (auto* pn : allPanels) addChildComponent (*pn);

    setResizable (true, true);
    setResizeLimits (900, 620, 2000, 1400);
    setSize (1120, 740);
    selectLayer (0);
    selectPage (pageSound);
    proc.addChangeListener (this);
    startTimerHz (20);
}

ChimeraEditor::~ChimeraEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
}

void ChimeraEditor::paint (juce::Graphics& g) { g.fillAll (theme::background); }

void ChimeraEditor::resized()
{
    auto r = getLocalBounds().reduced (8);
    topBar->setBounds (r.removeFromTop (72));
    r.removeFromTop (6);
    auto tabRow = r.removeFromTop (34);
    r.removeFromTop (6);
    const int pw = 104;
    auto pages = tabRow.removeFromRight (numPages * pw + (numPages - 1) * 6);
    for (int i = 0; i < numPages; ++i) pageButtons[i]->setBounds (pages.getX() + i * (pw + 6), pages.getY() + 3, pw, pages.getHeight() - 6);
    tabRow.removeFromRight (10);
    layerTabs->setBounds (tabRow.removeFromLeft (juce::jmin (tabRow.getWidth(), 4 * 190 + 18)));

    auto body = r;
    if (page != pageFx) {
        voice->setBounds (body.removeFromLeft (juce::jlimit (216, 250, body.getWidth() / 5)));
        body.removeFromLeft (6);
    }
    auto equalRow = [] (juce::Rectangle<int> area, std::initializer_list<Panel*> ps) {
        const int n = (int) ps.size(), gap = 6, w = (area.getWidth() - gap * (n - 1)) / n;
        int i = 0;
        for (auto* pn : ps) { pn->setBounds (area.getX() + i * (w + gap), area.getY(), i == n - 1 ? area.getRight() - (area.getX() + i * (w + gap)) : w, area.getHeight()); ++i; }
    };
    if (page == pageSound) {
        auto bottom = body.removeFromBottom (body.getHeight() < 420 ? 164 : 178);
        body.removeFromBottom (6);
        engine->setBounds (body);
        equalRow (bottom, { filter.get(), env[0].get(), env[1].get(), env[2].get() });
    } else if (page == pageMod) {
        auto top = body.removeFromTop (juce::jmin (190, body.getHeight() * 38 / 100));
        body.removeFromTop (6);
        equalRow (top, { lfo[0].get(), lfo[1].get(), globalLfo[0].get(), globalLfo[1].get() });
        matrix->setBounds (body);
    } else {
        equalRow (body.removeFromTop (juce::jmin (body.getHeight(), 300)), { fx[0].get(), fx[1].get(), fx[2].get() });
    }
}

void ChimeraEditor::selectLayer (int layer)
{
    ctx.layer = juce::jlimit (0, kMaxLayers - 1, layer);
    for (auto* pn : layerPanels) pn->setAccent (layerColour (ctx.layer));
    tick();
    repaint();
}

void ChimeraEditor::selectPage (int pg)
{
    page = juce::jlimit (0, (int) numPages - 1, pg);
    voice->setVisible (page != pageFx);
    for (auto* pn : { engine.get(), filter.get(), env[0].get(), env[1].get(), env[2].get() }) pn->setVisible (page == pageSound);
    for (auto* pn : { lfo[0].get(), lfo[1].get(), globalLfo[0].get(), globalLfo[1].get(), matrix.get() }) pn->setVisible (page == pageMod);
    for (auto& f : fx) f->setVisible (page == pageFx);
    resized();
    tick();
    repaint();
}

void ChimeraEditor::setEngine (Engine e)
{
    const int l = ctx.layer;
    if (e != Engine::Off) lastEngine[l] = e;
    ctx.editLayer ([e] (LayerParams& lp) { lp.engine = e; });
}

void ChimeraEditor::toggleLayerPower (int layer)
{
    const bool on = proc.patch().layers[(size_t) layer].engine != Engine::Off;
    if (on) lastEngine[layer] = proc.patch().layers[(size_t) layer].engine;
    const Engine target = on ? Engine::Off : (lastEngine[layer] == Engine::Off ? Engine::Analog : lastEngine[layer]);
    proc.editPatch ([layer, target] (Patch& p) { p.layers[(size_t) layer].engine = target; });
}

void ChimeraEditor::tick()
{
    if (topBar == nullptr) return;
    topBar->pollAll();
    topBar->setActivity (proc.activeVoices(), proc.outputPeak());
    layerTabs->poll();
    for (auto* pn : allPanels) pn->pollAll();
    for (auto& b : pageButtons) b->poll();
}

bool ChimeraEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) if (isAudioExt (juce::File (f)) || isCartExt (juce::File (f))) return true;
    return false;
}

void ChimeraEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& path : files) {
        const juce::File f (path);
        if (isCartExt (f)) { showMsg (proc.loadCart (ctx.layer, f)); return; }
        if (isAudioExt (f)) { showMsg (proc.loadSample (ctx.layer, f)); return; }
    }
}

} // namespace el::chimera
