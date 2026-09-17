#pragma once
#include <string>
#include <vector>
#include "MixContext.h"
#include "MixIntent.h"
#include "DspCapabilityRegistry.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// "BRING THE LEAD VOCAL FORWARD"
//
// Turning what an engineer says into what DLIVE means. This is the offline half of AI MIX
// CHAT: deterministic, dependency-free, and good enough that the chat is genuinely useful
// with no API key, no network and no account - which matters, because the machine in a
// church sound booth is very often not on the internet and the service starts anyway.
//
// It produces a MixIntent and nothing else. Everything downstream is unchanged: the
// resolver turns intent into actions, the safety validator checks every one of them, and
// what comes out is an ordinary MixPlan with BEFORE / AFTER, KEEP and REVERT. So a sentence
// typed into the chat can never reach a parameter by a path the reasoning layer could not.
//
// What it will not do is guess. A request it cannot read comes back with `understood` false
// and a sentence saying what DLIVE did not follow, rather than a confident change to
// something the engineer did not ask about. When a cloud provider is configured it does the
// reading instead and this is the fallback; the pipeline after it is identical either way.
// ---------------------------------------------------------------------------
struct MixRequestReading
{
    bool understood = false;
    MixIntent intent;
    // What was read, in DLIVE's own words: "Lead - more presence." Shown back to the
    // engineer so they can see it was heard correctly before anything is applied.
    std::vector<std::string> readAs;
    // Anything in the sentence that was recognised as a wish but is not a mix decision
    // ("the singer is off-mic", "channel 9 is buzzing"). Kept, never silently dropped.
    std::vector<std::string> notMixDecisions;
    std::string failure;        // when nothing was understood: what to try instead
};

// `context` supplies the names (a target is found by what it is called, by its role, or by
// its group), `registry` supplies what is actually available on it.
MixRequestReading readMixRequest (const std::string& request, const MixContext& context,
                                  const DspCapabilityRegistry& registry);

// The vocabulary, exposed so the chat can show examples that are guaranteed to work.
std::vector<std::string> mixRequestExamples();

} // namespace livemix
