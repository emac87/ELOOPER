#include "Engine/PresetManager.h"

namespace el {

namespace {

constexpr const char* kPluginExt  = ".elpreset";
constexpr const char* kChannelExt = ".elchannel";

juce::String blockToBase64 (const juce::MemoryBlock& b) { return b.toBase64Encoding(); }

juce::MemoryBlock blockFromBase64 (const juce::String& s)
{
    juce::MemoryBlock b;
    if (s.isNotEmpty()) b.fromBase64Encoding (s);
    return b;
}

juce::String floatsToString (const float* v, int n)
{
    juce::StringArray a;
    for (int i = 0; i < n; ++i) a.add (juce::String (v[i], 5));
    return a.joinIntoString (" ");
}

void floatsFromString (const juce::String& s, float* v, int n)
{
    auto a = juce::StringArray::fromTokens (s, " ", "");
    for (int i = 0; i < n && i < a.size(); ++i) v[i] = a[i].getFloatValue();
}

juce::ValueTree listToTree (const juce::Identifier& id, const std::vector<PluginPresetData>& v)
{
    juce::ValueTree t (id);
    for (const auto& d : v) t.appendChild (PresetManager::pluginPresetDataToTree (d), nullptr);
    return t;
}

std::vector<PluginPresetData> listFromTree (const juce::ValueTree& t)
{
    std::vector<PluginPresetData> v;
    for (const auto& c : t) v.push_back (PresetManager::pluginPresetDataFromTree (c));
    return v;
}

bool writeXml (const juce::XmlElement& xml, const juce::File& f)
{
    f.getParentDirectory().createDirectory();
    return xml.writeTo (f);
}

// Asks for a name in a non-modal alert window, then calls `onName` (not called on cancel / empty name).
void askForName (const juce::String& title, const juce::String& message, std::function<void (juce::String)> onName)
{
    auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", {}, "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> safe (w);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, handler = std::move (onName)] (int result)
    {
        if (result == 1 && safe != nullptr)
        {
            auto name = safe->getTextEditorContents ("name").trim();
            if (name.isNotEmpty() && handler) handler (name);
        }
    }), true);
}

void showError (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "ELooper", message);
}

} // namespace

// ---- helpers ------------------------------------------------------------------------------------
juce::String PresetManager::sanitise (const juce::String& name)
{
    auto s = juce::File::createLegalFileName (name.trim());
    return s.isEmpty() ? juce::String ("Untitled") : s;
}

juce::File PresetManager::pluginPresetDir (const juce::PluginDescription& desc)
{
    return PluginHost::appDataDir().getChildFile ("Presets").getChildFile ("Plugins")
                                   .getChildFile (sanitise (desc.name.isNotEmpty() ? desc.name : "Plugin"));
}

juce::File PresetManager::channelPresetDir()
{
    return PluginHost::appDataDir().getChildFile ("Presets").getChildFile ("Channels");
}

std::unique_ptr<juce::XmlElement> PresetManager::descriptionToXml (const juce::PluginDescription& d)
{
    return d.createXml();
}

juce::PluginDescription PresetManager::descriptionFromXml (const juce::XmlElement& e)
{
    juce::PluginDescription d;
    d.loadFromXml (e);
    return d;
}

// ---- tree conversion ------------------------------------------------------------------------------
juce::ValueTree PresetManager::pluginPresetDataToTree (const PluginPresetData& d)
{
    juce::ValueTree t ("PLUGINPRESET");
    t.setProperty ("state", blockToBase64 (d.state), nullptr);
    t.setProperty ("bypassed", d.bypassed, nullptr);
    if (auto xml = d.description.createXml())
        t.appendChild (juce::ValueTree::fromXml (*xml), nullptr);
    return t;
}

PluginPresetData PresetManager::pluginPresetDataFromTree (const juce::ValueTree& t)
{
    PluginPresetData d;
    d.state = blockFromBase64 (t["state"].toString());
    d.bypassed = (bool) t["bypassed"];
    if (t.getNumChildren() > 0)
        if (auto xml = t.getChild (0).createXml())
            d.description.loadFromXml (*xml);
    return d;
}

juce::ValueTree PresetManager::channelPresetToTree (const ChannelPreset& p)
{
    juce::ValueTree t ("CHANNELPRESET");
    t.setProperty ("name", p.name, nullptr);
    t.setProperty ("inputGain", p.inputGain, nullptr);
    t.setProperty ("volume", p.volume, nullptr);
    t.setProperty ("pan", p.pan, nullptr);
    t.setProperty ("eqEnabled", p.eqEnabled, nullptr);
    t.setProperty ("eq", floatsToString (p.eq, kEqBands), nullptr);
    t.setProperty ("monitor", (int) p.monitor, nullptr);
    t.appendChild (listToTree ("MIDIFX", p.midiFx), nullptr);
    juce::ValueTree inst ("INSTRUMENT");
    if (p.instrument) inst.appendChild (pluginPresetDataToTree (*p.instrument), nullptr);
    t.appendChild (inst, nullptr);
    t.appendChild (listToTree ("PREFX", p.preFx), nullptr);
    t.appendChild (listToTree ("POSTFX", p.postFx), nullptr);
    return t;
}

ChannelPreset PresetManager::channelPresetFromTree (const juce::ValueTree& t)
{
    ChannelPreset p;
    p.name = t["name"].toString();
    p.inputGain = t.getProperty ("inputGain", 0.5f);
    p.volume = t.getProperty ("volume", pm::kUnityVolume);
    p.pan = t.getProperty ("pan", 0.5f);
    p.eqEnabled = (bool) t.getProperty ("eqEnabled", true);
    floatsFromString (t["eq"].toString(), p.eq, kEqBands);
    p.monitor = (MonitorMode) juce::jlimit (0, 2, (int) t.getProperty ("monitor", 0));
    p.midiFx = listFromTree (t.getChildWithName ("MIDIFX"));
    auto inst = t.getChildWithName ("INSTRUMENT");
    if (inst.getNumChildren() > 0) p.instrument = pluginPresetDataFromTree (inst.getChild (0));
    p.preFx = listFromTree (t.getChildWithName ("PREFX"));
    p.postFx = listFromTree (t.getChildWithName ("POSTFX"));
    return p;
}

// ---- plugin presets ---------------------------------------------------------------------------------
bool PresetManager::savePluginPreset (Id slotId, const juce::String& presetName)
{
    auto slot = controller.session().findPlugin (slotId).second;
    if (slot == nullptr || presetName.trim().isEmpty()) return false;

    PluginPresetData data;
    data.description = slot->description;
    data.state = controller.getPluginState (slotId);
    data.bypassed = slot->bypassed.load();

    juce::ValueTree root ("ELooperPluginPreset");
    root.setProperty ("name", presetName, nullptr);
    root.setProperty ("pluginName", slot->description.name, nullptr);
    root.setProperty ("pluginUid", slot->description.uniqueId, nullptr);
    root.appendChild (pluginPresetDataToTree (data), nullptr);

    auto file = pluginPresetDir (slot->description).getChildFile (sanitise (presetName) + kPluginExt);
    auto xml = root.createXml();
    return xml != nullptr && writeXml (*xml, file);
}

bool PresetManager::loadPluginPreset (Id slotId, const juce::File& file)
{
    auto slot = controller.session().findPlugin (slotId).second;
    if (slot == nullptr) return false;
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("ELooperPluginPreset")) return false;
    auto root = juce::ValueTree::fromXml (*xml);
    auto child = root.getChildWithName ("PLUGINPRESET");
    if (! child.isValid()) return false;
    auto data = pluginPresetDataFromTree (child);
    if (data.state.getSize() == 0) return false;
    controller.setPluginState (slotId, data.state);
    return true;
}

juce::Array<juce::File> PresetManager::pluginPresets (const juce::PluginDescription& desc) const
{
    juce::Array<juce::File> files;
    pluginPresetDir (desc).findChildFiles (files, juce::File::findFiles, false, juce::String ("*") + kPluginExt);
    files.sort();
    return files;
}

// ---- channel presets ----------------------------------------------------------------------------------
bool PresetManager::saveChannelPreset (Id trackId, const juce::String& name)
{
    if (controller.session().findTrack (trackId) == nullptr || name.trim().isEmpty()) return false;
    auto preset = controller.captureChannelPreset (trackId);
    preset.name = name;
    auto xml = channelPresetToTree (preset).createXml();
    return xml != nullptr && writeXml (*xml, channelPresetDir().getChildFile (sanitise (name) + kChannelExt));
}

void PresetManager::loadChannelPreset (Id trackId, const juce::File& file, std::function<void (juce::String)> done)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("CHANNELPRESET"))
    {
        if (done) done ("Not a valid channel preset: " + file.getFileName());
        return;
    }
    controller.applyChannelPreset (trackId, channelPresetFromTree (juce::ValueTree::fromXml (*xml)), std::move (done));
}

juce::Array<juce::File> PresetManager::channelPresets() const
{
    juce::Array<juce::File> files;
    channelPresetDir().findChildFiles (files, juce::File::findFiles, false, juce::String ("*") + kChannelExt);
    files.sort();
    return files;
}

// ---- menus ------------------------------------------------------------------------------------------------
// The menus' callbacks capture `this`: the PresetManager must outlive them (it lives for the app's lifetime).
void PresetManager::showPluginPresetMenu (Id slotId, juce::Component* target)
{
    auto slot = controller.session().findPlugin (slotId).second;
    if (slot == nullptr) return;
    auto presets = pluginPresets (slot->description);

    juce::PopupMenu m;
    m.addItem (1, "Save Preset...");
    m.addSeparator();
    if (presets.isEmpty()) m.addItem (-1, "No saved presets", false);
    for (int i = 0; i < presets.size(); ++i)
        m.addItem (100 + i, presets[i].getFileNameWithoutExtension());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this, slotId, presets] (int r)
    {
        if (r == 1)
        {
            askForName ("Save Plug-in Preset", "Preset name:", [this, slotId] (juce::String n)
            {
                if (! savePluginPreset (slotId, n)) showError ("Could not save the preset.");
            });
        }
        else if (r >= 100 && r - 100 < presets.size())
        {
            if (! loadPluginPreset (slotId, presets[r - 100])) showError ("Could not load the preset.");
        }
    });
}

void PresetManager::showChannelPresetMenu (Id trackId, juce::Component* target)
{
    auto presets = channelPresets();

    juce::PopupMenu m;
    m.addItem (1, "Save Channel Preset...");
    m.addSeparator();
    if (presets.isEmpty()) m.addItem (-1, "No saved presets", false);
    for (int i = 0; i < presets.size(); ++i)
        m.addItem (100 + i, presets[i].getFileNameWithoutExtension());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this, trackId, presets] (int r)
    {
        if (r == 1)
        {
            askForName ("Save Channel Preset", "Preset name:", [this, trackId] (juce::String n)
            {
                if (! saveChannelPreset (trackId, n)) showError ("Could not save the preset.");
            });
        }
        else if (r >= 100 && r - 100 < presets.size())
        {
            loadChannelPreset (trackId, presets[r - 100], [] (juce::String errors)
            {
                if (errors.isNotEmpty()) showError (errors);
            });
        }
    });
}

} // namespace el
