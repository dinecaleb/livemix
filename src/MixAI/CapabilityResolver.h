#pragma once
#include "DspCapabilityRegistry.h"
#include "MixContext.h"
#include "MixIntent.h"
#include "ProcessingPlan.h"
#include "Mix/MixParameters.h"

namespace livemix
{

// HOW CAN THE TOOLS WE HAVE ACHIEVE THAT INTENT.
//
// Deterministic: the same intent against the same session always resolves to the same plan,
// which is what makes the reasoning layer testable and what keeps a re-run from drifting.
// The resolver is also where honesty lives. When an intent asks for something DLIVE does not
// have, it either builds a credible facsimile out of what it does have and says so, chooses a
// different tool that serves the same musical end and says so, or reports that it cannot -
// it never quietly does something else and calls it done.
namespace CapabilityResolver
{
    struct Context
    {
        const DspCapabilityRegistry* registry = nullptr;
        const MixContext* mix = nullptr;
        // The deterministic plan's proposed state: a professional mix already. Every action is
        // a delta on this, never on the console's flat starting point, which is why the moves
        // are small and why one pass lands.
        const MixParameters* baseline = nullptr;
        const RoutingGraph* graph = nullptr;
        StyleProfileId profile = StyleProfileId::ModernGospel;

        bool valid() const noexcept { return registry != nullptr && mix != nullptr && baseline != nullptr && graph != nullptr; }
    };

    ProcessingPlan resolve (const MixIntent&, const Context&);
}

} // namespace livemix
