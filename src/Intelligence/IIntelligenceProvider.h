#pragma once
#include <string>
#include <vector>
#include <atomic>
#include "Analysis/AnalysisResult.h"
#include "Recommendations/Recommendation.h"
#include "Core/ChannelRole.h"
#include "Core/StyleId.h"
#include "DSP/ChannelParameters.h"
#include "Analysis/KitAnalysis.h"

namespace livemix
{

// Everything an intelligence provider is allowed to see. Deliberately a
// summary: the deterministic DSP analysis, the role/style context and the
// current settings. Raw audio is NOT included by default; a provider that
// needs audio must declare sendsDataExternally() so the UI can obtain consent.
struct IntelligenceRequest
{
    AnalysisResult analysis;
    ChannelRole role = ChannelRole::KickIn;
    StyleProfileId style = StyleProfileId::ModernWorship;
    ChannelParameters currentParameters;
    RecommendationResult standardRecommendations; // what Standard Tune already concluded
    OutputStats output;                           // processed-output level during the capture
    std::string groupName;
    std::vector<KitMember> kitContext;            // other channels in the group (latest analyses), for relationships
};

struct IntelligenceResponse
{
    bool valid = false;
    std::string providerName;
    std::string interpretation;                  // human-readable summary of the source / problems
    std::vector<Recommendation> recommendations; // additional or refined items (validated before use)
    std::string error;
};

// Optional interpretation layer. Implementations may call a cloud service or a
// local model. They are invoked ONLY from a background thread, ONLY after an
// explicit user Analyze action with AI assistance enabled, and never while
// audio is being processed on the real-time thread.
class IIntelligenceProvider
{
public:
    virtual ~IIntelligenceProvider() = default;

    virtual std::string getName() const = 0;
    virtual bool isAvailable() const = 0;             // configured, reachable (best effort, must not block)
    virtual bool sendsDataExternally() const = 0;     // true -> UI must show a privacy notice before use

    // Blocking call on a worker thread. Implementations should honour
    // shouldCancel (polled) and return promptly when it becomes true.
    virtual IntelligenceResponse interpret (const IntelligenceRequest& request,
                                            const std::atomic<bool>& shouldCancel) = 0;
};

// The Standard Tune path: no AI, no network, nothing to configure.
class NullIntelligenceProvider final : public IIntelligenceProvider
{
public:
    std::string getName() const override { return "None (Standard Tune)"; }
    bool isAvailable() const override { return false; }
    bool sendsDataExternally() const override { return false; }
    IntelligenceResponse interpret (const IntelligenceRequest&, const std::atomic<bool>&) override
    {
        IntelligenceResponse r;
        r.valid = false;
        r.providerName = getName();
        r.error = "No AI provider configured";
        return r;
    }
};

} // namespace livemix
