#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "MixContext.h"
#include "MixIntent.h"
#include "DspCapabilityRegistry.h"

namespace livemix
{

// Everything a reasoning provider is allowed to see: the structured description of the mix
// and the list of tools DLIVE really has. No audio, no session file, no keys, no device.
// A provider that ever needs more than this has to say so through sendsDataExternally()
// and be given consent first.
struct MixReasoningRequest
{
    MixContext context;                       // the initial listen
    json::Value capabilities;                 // DspCapabilityRegistry::toJson()
    std::string instructions;                 // how a DLIVE mix engineer is expected to behave
    std::string userRequest;                  // "make the drums bigger" - empty for a plain Tune

    // Refinement pass: what was decided, what was applied and what the second listen heard.
    bool refinement = false;
    MixIntent previousIntent;
    std::vector<std::string> applied;         // one line per accepted action
    MixContext verification;                  // measured after the plan was applied
    bool hasVerification = false;
};

struct MixReasoningResponse
{
    bool valid = false;
    std::string providerName;
    std::string model;
    std::string error;
    MixIntent intent;
    std::vector<std::string> problems;        // anything in the reply that had to be dropped
    double elapsedMs = 0.0;
    int requestBytes = 0;
};

// WHAT SHOULD THE MIX SOUND LIKE.
//
// One interface, three futures: a cloud model, a DLIVE model running locally, and the
// deterministic one below that needs neither. Nothing above this line knows which is in use.
class MixReasoningProvider
{
public:
    virtual ~MixReasoningProvider() = default;

    virtual std::string getName() const = 0;
    virtual std::string getModel() const { return {}; }
    virtual bool isAvailable() const = 0;              // configured and reachable; must not block
    virtual bool sendsDataExternally() const = 0;      // true -> the user is told before anything leaves

    // Called on a worker thread, never on the audio thread and never on the message thread.
    // Implementations poll `shouldCancel` and return promptly once it is set.
    virtual MixReasoningResponse reason (const MixReasoningRequest&, const std::atomic<bool>& shouldCancel) = 0;
};

// The brief every provider is given: how a live and broadcast engineer is expected to behave,
// and what this profile is aiming for. Written once here so a cloud model, a local model and
// the built-in one are all asked for the same thing.
std::string mixEngineerInstructions (StyleProfileId, MixPurpose);

// The offline engineer. Deterministic, needs no network and no configuration, and reasons from
// the relationships the analysis already measured. It is what TUNE LIVE MIX uses when no other
// provider is configured, and what every test runs against - a test must never need the cloud.
class LocalMixReasoningProvider final : public MixReasoningProvider
{
public:
    std::string getName() const override { return "DLIVE built-in (offline)"; }
    bool isAvailable() const override { return true; }
    bool sendsDataExternally() const override { return false; }
    MixReasoningResponse reason (const MixReasoningRequest&, const std::atomic<bool>&) override;
};

} // namespace livemix
