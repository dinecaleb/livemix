#pragma once
#include <juce_core/juce_core.h>
#include "IIntelligenceProvider.h"
#include "AISettings.h"

namespace livemix
{

// AI-assisted Analyze via the OpenAI Chat Completions API (raw HTTPS through
// juce::URL). Sends the deterministic analysis summary, targets, current
// settings and kit context as JSON - never audio - and requires a strict
// JSON-schema response. Everything it returns still passes through
// SafetyValidator before the user sees it. Only ever invoked from
// AnalyzeCoordinator's worker after an explicit user action with AI
// assistance enabled.
class OpenAIProvider final : public IIntelligenceProvider
{
public:
    std::string getName() const override;
    bool isAvailable() const override { return AISettings::load().hasKey(); }
    bool sendsDataExternally() const override { return true; }
    IntelligenceResponse interpret (const IntelligenceRequest& request, const std::atomic<bool>& shouldCancel) override;

    // Building blocks, exposed for tests and the settings dialog.
    static juce::String buildRequestBody (const IntelligenceRequest& request, const AISettings& settings);
    static IntelligenceResponse parseResponse (const juce::String& body, int httpStatus, const juce::String& model);
    // shouldCancel (optional) is polled while waiting for the server; setting it aborts the request promptly.
    static bool testConnection (const AISettings& settings, juce::String& messageOut, const std::atomic<bool>* shouldCancel = nullptr);

private:
    static juce::String post (const juce::String& body, const AISettings& settings, int& statusOut, juce::String& errorOut,
                              const std::atomic<bool>* shouldCancel);
};

} // namespace livemix
