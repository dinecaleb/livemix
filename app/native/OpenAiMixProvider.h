#pragma once
#include <juce_core/juce_core.h>
#include "MixAI/MixReasoningProvider.h"
#include "Intelligence/AISettings.h"

namespace livemix
{

// The cloud mix engineer: a MixReasoningProvider backed by the OpenAI Chat Completions API.
//
// What leaves the machine is the MixContext document and the capability list - measurements,
// source names, roles and what DLIVE can do - and nothing else. No audio, ever. No session
// file, no recordings, no device information, no key material in any log. The reply is
// required to match a strict JSON schema and is still put through validateMixIntent, the
// resolver and MixSafetyValidator before a single parameter moves, so a bad reply is a bad
// reply and never a bad mix.
//
// Only ever constructed when the user has configured a key, and only ever called from
// TuneLiveCoordinator's worker thread.
class OpenAiMixProvider final : public MixReasoningProvider
{
public:
    std::string getName() const override;
    std::string getModel() const override { return AISettings::load().model.toStdString(); }
    bool isAvailable() const override { return AISettings::load().hasKey(); }
    bool sendsDataExternally() const override { return true; }

    MixReasoningResponse reason (const MixReasoningRequest&, const std::atomic<bool>& shouldCancel) override;

    // Exposed for tests: neither of these touches the network.
    static juce::String buildRequestBody (const MixReasoningRequest&, const AISettings&);
    static MixReasoningResponse parseResponse (const juce::String& body, int httpStatus, const juce::String& model);
};

} // namespace livemix
