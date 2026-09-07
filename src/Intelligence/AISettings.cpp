#include "AISettings.h"
#include <mutex>
#include <optional>

namespace livemix
{

namespace
{
    std::mutex settingsCacheMutex;
    std::optional<AISettings> settingsCache; // guarded by settingsCacheMutex
}

juce::File AISettings::getFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support").getChildFile ("LiveMix");
    dir.createDirectory();
    return dir.getChildFile ("ai-settings.xml");
}

juce::StringArray AISettings::availableModels()
{
    return { "gpt-5", "gpt-5-mini", "gpt-4.1", "gpt-4o" };
}

static AISettings readSettingsFromDisk()
{
    AISettings s;
    if (auto xml = juce::XmlDocument::parse (AISettings::getFile()))
    {
        if (xml->hasTagName ("LiveMixAISettings"))
        {
            s.apiKey = xml->getStringAttribute ("apiKey");
            s.model = xml->getStringAttribute ("model", s.model);
            s.effort = xml->getStringAttribute ("effort", s.effort);
            s.timeoutSeconds = xml->getIntAttribute ("timeoutSeconds", s.timeoutSeconds);
        }
    }
    if (s.apiKey.isEmpty())
        s.apiKey = juce::SystemStats::getEnvironmentVariable ("OPENAI_API_KEY", {});
    if (s.model.trim().isEmpty()) s.model = "gpt-5"; // any model id is allowed; the list is only a suggestion
    if (s.effort != "low" && s.effort != "medium" && s.effort != "high") s.effort = "medium";
    s.timeoutSeconds = juce::jlimit (15, 300, s.timeoutSeconds);
    return s;
}

AISettings AISettings::load()
{
    std::lock_guard<std::mutex> lock (settingsCacheMutex);
    if (! settingsCache.has_value()) settingsCache = readSettingsFromDisk();
    return *settingsCache;
}

AISettings AISettings::reload()
{
    std::lock_guard<std::mutex> lock (settingsCacheMutex);
    settingsCache = readSettingsFromDisk();
    return *settingsCache;
}

bool AISettings::save (const AISettings& s)
{
    juce::XmlElement xml ("LiveMixAISettings");
    xml.setAttribute ("apiKey", s.apiKey.trim());
    xml.setAttribute ("model", s.model);
    xml.setAttribute ("effort", s.effort);
    xml.setAttribute ("timeoutSeconds", s.timeoutSeconds);
    const bool ok = xml.writeTo (getFile());
    std::lock_guard<std::mutex> lock (settingsCacheMutex);
    settingsCache = readSettingsFromDisk(); // normalised, env merged
    return ok;
}

} // namespace livemix
