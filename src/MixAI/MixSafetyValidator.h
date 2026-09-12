#pragma once
#include <string>
#include <vector>
#include "DspCapabilityRegistry.h"
#include "MixContext.h"
#include "ProcessingPlan.h"
#include "Mix/MixParameters.h"

namespace livemix
{

// IS THIS SAFE AND VALID.
//
// No plan reaches the audio without passing through here, whoever made it. The validator is
// deliberately suspicious: it would rather refuse an action it cannot account for than clamp
// it into something unrelated and let it through. What it rejects is kept in the plan with
// the reason attached, so REVIEW CHANGES can show what DLIVE declined to do.
namespace MixSafetyValidator
{
    struct Context
    {
        const DspCapabilityRegistry* registry = nullptr;
        const MixParameters* baseline = nullptr;      // what the plan is a delta on
        const RoutingGraph* graph = nullptr;
        const MixContext* mix = nullptr;              // for the master's measured headroom and each source's family
        StyleProfileId profile = StyleProfileId::ModernGospel;

        bool valid() const noexcept { return registry != nullptr && baseline != nullptr && graph != nullptr; }
    };

    struct Report
    {
        int accepted = 0;
        int clamped = 0;
        int rejected = 0;
        std::vector<std::string> notes;      // one plain line per refusal, ready to show
    };

    ProcessingPlan validate (const ProcessingPlan&, const Context&, Report* report = nullptr);
}

} // namespace livemix
