#pragma once
#include <juce_core/juce_core.h>

namespace livemix
{

// User-level AI settings for the OpenAI provider. Stored in
// ~/Library/Application Support/LiveMix/ai-settings.xml, never inside a DAW
// session (API keys must not travel with project files).
// OPENAI_API_KEY in the environment is used when no key is stored.
struct AISettings
{
    juce::String apiKey;
    juce::String model { "gpt-5" };
    juce::String effort { "medium" };   // reasoning effort for reasoning models: low | medium | high
    int timeoutSeconds = 60;

    bool hasKey() const { return apiKey.isNotEmpty(); }

    static juce::File getFile();
    // Merges stored settings with the environment. Cached in memory after the first
    // read (the editor polls this every tick); save() refreshes the cache.
    static AISettings load();
    static AISettings reload();         // re-reads the file, e.g. when opening the settings dialog
    static bool save (const AISettings& s);
    static juce::StringArray availableModels();
};

} // namespace livemix
