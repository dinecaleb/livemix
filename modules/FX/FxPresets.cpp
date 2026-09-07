#include "FxPresets.h"
#include "FX/FxProfiles.h"
#include "FX/FxMacroMapping.h"
#include "State/ParameterIDs.h"

namespace livemix
{

namespace
{
    juce::ValueTree buildTree (const juce::String& name, FxType type, StyleProfileId style, const FxMacros& m)
    {
        juce::ValueTree t ("LiveMixFxPreset");
        t.setProperty ("name", name, nullptr);
        t.setProperty ("type", int (type), nullptr);
        t.setProperty ("profile", int (style), nullptr);
        juce::ValueTree macros ("Macros");
        macros.setProperty (FxParamID::space, m.space, nullptr);
        macros.setProperty (FxParamID::length, m.length, nullptr);
        macros.setProperty (FxParamID::warmth, m.warmth, nullptr);
        macros.setProperty (FxParamID::clarity, m.clarity, nullptr);
        macros.setProperty (FxParamID::distance, m.distance, nullptr);
        t.addChild (macros, -1, nullptr);
        juce::ValueTree params ("Params");
        FxParameters p = FxMacroMapping::apply (FxProfiles::baseline (style, type), m, fxFamily (type));
        forEachFxParameter (p, [&] (const std::string& id, auto& v) { params.setProperty (juce::Identifier (id), float (v), nullptr); });
        t.addChild (params, -1, nullptr);
        return t;
    }
}

juce::File FxPresets::getUserPresetDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support").getChildFile ("LiveMix").getChildFile ("Presets").getChildFile ("FX").getChildFile ("User");
    dir.createDirectory();
    return dir;
}

std::vector<FxPresets::Info> FxPresets::getFactoryPresets()
{
    std::vector<Info> v;
    for (int s = 0; s < int (StyleProfileId::Count); ++s)
        for (int t = 0; t < int (FxType::Count); ++t)
        {
            Info i;
            i.name = juce::String (styleProfileName (StyleProfileId (s))) + " / " + fxTypeName (FxType (t));
            i.kind = Kind::Factory; i.type = FxType (t); i.style = StyleProfileId (s);
            v.push_back (i);
        }
    return v;
}

std::vector<FxPresets::Info> FxPresets::getUsePresets()
{
    std::vector<Info> v;
    const auto& use = FxProfiles::usePresets();
    for (size_t k = 0; k < use.size(); ++k)
    {
        Info i;
        i.name = use[k].name; i.kind = Kind::Use; i.type = use[k].type; i.style = use[k].profile; i.useIndex = int (k);
        v.push_back (i);
    }
    return v;
}

std::vector<FxPresets::Info> FxPresets::getUserPresets()
{
    std::vector<Info> v;
    for (const auto& f : getUserPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.livemixpreset"))
    {
        Info i;
        i.name = f.getFileNameWithoutExtension();
        i.kind = Kind::User;
        i.file = f;
        if (auto xml = juce::XmlDocument::parse (f))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            i.type = fxTypeFromIndex (t.getProperty ("type", 0));
            i.style = styleProfileFromIndex (t.getProperty ("profile", 0));
        }
        v.push_back (i);
    }
    std::sort (v.begin(), v.end(), [] (const Info& a, const Info& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return v;
}

juce::ValueTree FxPresets::presetTree (const Info& info)
{
    switch (info.kind)
    {
        case Kind::Factory: return buildTree (info.name, info.type, info.style, FxMacros {});
        case Kind::Use:
        {
            const auto& use = FxProfiles::usePresets();
            if (info.useIndex < 0 || info.useIndex >= int (use.size())) return {};
            const auto& u = use[size_t (info.useIndex)];
            return buildTree (u.name, u.type, u.profile, u.macros);
        }
        case Kind::User:
        default:
            if (auto xml = juce::XmlDocument::parse (info.file))
            {
                auto t = juce::ValueTree::fromXml (*xml);
                if (t.hasType ("LiveMixFxPreset")) return t;
            }
            return {};
    }
}

juce::String FxPresets::sanitiseName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).substring (0, 60);
}

bool FxPresets::saveUserPreset (const juce::String& name, const juce::ValueTree& tree, Info* savedInfo)
{
    const juce::String clean = sanitiseName (name);
    if (clean.isEmpty() || ! tree.isValid()) return false;
    juce::ValueTree copy = tree.createCopy();
    copy.setProperty ("name", clean, nullptr);
    auto file = getUserPresetDirectory().getChildFile (clean + ".livemixpreset");
    if (auto xml = copy.createXml())
    {
        if (! xml->writeTo (file)) return false;
        if (savedInfo != nullptr)
        {
            savedInfo->name = clean; savedInfo->kind = Kind::User; savedInfo->file = file;
            savedInfo->type = fxTypeFromIndex (copy.getProperty ("type", 0));
            savedInfo->style = styleProfileFromIndex (copy.getProperty ("profile", 0));
        }
        return true;
    }
    return false;
}

bool FxPresets::deleteUserPreset (const Info& info)
{
    return info.kind == Kind::User && info.file.existsAsFile() && info.file.deleteFile();
}

} // namespace livemix
