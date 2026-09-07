#include "PresetManager.h"
#include "Profiles/StyleProfile.h"
#include "Profiles/MacroMapping.h"
#include "DSP/ChannelParameters.h"
#include "State/ParameterIDs.h"
#include "State/ParameterSpecs.h"

namespace livemix
{

juce::File PresetManager::getUserPresetDirectory (const ProductDefinition& product)
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support").getChildFile ("LiveMix").getChildFile ("Presets").getChildFile (product.presetFolder).getChildFile ("User");
    dir.createDirectory();
    return dir;
}

std::vector<PresetManager::Info> PresetManager::getFactoryPresets (const ProductDefinition& product)
{
    std::vector<Info> v;
    for (int s = 0; s < int (StyleProfileId::Count); ++s)
        for (auto r : product.roles)
        {
            Info i;
            i.name = juce::String (styleProfileName (StyleProfileId (s))) + " / " + channelRoleName (r);
            i.isFactory = true; i.role = r; i.style = StyleProfileId (s);
            v.push_back (i);
        }
    return v;
}

std::vector<PresetManager::Info> PresetManager::getUserPresets (const ProductDefinition& product)
{
    std::vector<Info> v;
    for (const auto& f : getUserPresetDirectory (product).findChildFiles (juce::File::findFiles, false, "*.livemixpreset"))
    {
        Info i;
        i.name = f.getFileNameWithoutExtension();
        i.isFactory = false;
        i.file = f;
        if (auto xml = juce::XmlDocument::parse (f))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            if (! t.hasType (product.presetType)) continue; // another product's preset in this folder
            i.role = channelRoleFromIndex (t.getProperty ("role", int (product.defaultRole)));
            i.style = styleProfileFromIndex (t.getProperty ("profile", 0));
        }
        v.push_back (i);
    }
    std::sort (v.begin(), v.end(), [] (const Info& a, const Info& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return v;
}

juce::ValueTree PresetManager::factoryPresetTree (const ProductDefinition& product, const Info& info)
{
    juce::ValueTree t (product.presetType);
    t.setProperty ("name", info.name, nullptr);
    t.setProperty ("role", int (info.role), nullptr);
    t.setProperty ("profile", int (info.style), nullptr);
    const MacroValues m = MacroMapping::defaults (product.product);
    juce::ValueTree macros ("Macros");
    for (size_t i = 0; i < 5; ++i) macros.setProperty (product.macros[i].id, m.v[i], nullptr);
    t.addChild (macros, -1, nullptr);
    juce::ValueTree params ("Params");
    ChannelParameters p = MacroMapping::apply (product.product, StyleProfile::baseline (info.role, info.style), m, roleFamily (info.role));
    forEachDspParameter (p, [&] (const std::string& id, auto& v)
    {
        if (productUsesParameter (product.product, id)) params.setProperty (juce::Identifier (id), float (v), nullptr);
    });
    t.addChild (params, -1, nullptr);
    return t;
}

juce::ValueTree PresetManager::loadUserPreset (const ProductDefinition& product, const Info& info)
{
    if (auto xml = juce::XmlDocument::parse (info.file))
    {
        auto t = juce::ValueTree::fromXml (*xml);
        if (t.hasType (product.presetType)) return t;
    }
    return {};
}

juce::String PresetManager::sanitiseName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).substring (0, 64);
}

bool PresetManager::saveUserPreset (const ProductDefinition& product, const juce::String& name, const juce::ValueTree& tree, Info* savedInfo)
{
    const auto clean = sanitiseName (name);
    if (clean.isEmpty() || ! tree.hasType (product.presetType)) return false;
    juce::ValueTree copy = tree.createCopy();
    copy.setProperty ("name", clean, nullptr);
    auto file = getUserPresetDirectory (product).getChildFile (clean + ".livemixpreset");
    if (auto xml = copy.createXml())
    {
        if (! xml->writeTo (file)) return false;
        if (savedInfo != nullptr)
        {
            savedInfo->name = clean; savedInfo->isFactory = false; savedInfo->file = file;
            savedInfo->role = channelRoleFromIndex (copy.getProperty ("role", int (product.defaultRole)));
            savedInfo->style = styleProfileFromIndex (copy.getProperty ("profile", 0));
        }
        return true;
    }
    return false;
}

bool PresetManager::deleteUserPreset (const Info& info)
{
    return ! info.isFactory && info.file.existsAsFile() && info.file.deleteFile();
}

} // namespace livemix
