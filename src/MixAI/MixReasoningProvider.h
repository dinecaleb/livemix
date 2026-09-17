#pragma once
#include <atomic>
#include <cstdint>
#include <map>
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
    // The same toolbox, structured. A cloud provider is given `capabilities`; the offline one
    // reads this, because matching a name in a sentence against what is really on the console
    // has to be exact ("the sax" can only mean a saxophone that is actually assigned).
    DspCapabilityRegistry registry;
    std::string instructions;                 // how a DLIVE mix engineer is expected to behave
    std::string userRequest;                  // "make the drums bigger" - empty for a plain Tune

    // ---- Repeatability ----
    //
    // The same band, the same listen and the same session must produce the same mix. Anything
    // else is unusable in a professional room: an engineer cannot learn what DLIVE does if it
    // does something different every time. So every request carries the fingerprint of the
    // mix it is about as an explicit seed, and providers are required to ask for the least
    // random answer their model can give.
    //
    // `variation` is the deliberate exception. 0 is the repeatable answer and is what the
    // TUNE LIVE MIX button always sends. TRY ANOTHER MIX sends 1, 2, 3 ... - a different
    // question, asked on purpose, and still repeatable for that number.
    std::uint64_t seed = 0;
    int variation = 0;

    // AI MIX CHAT. What has already been said in this conversation, oldest first, so a
    // provider can follow "and a bit more" without the engineer having to repeat themselves.
    // Empty for an ordinary Tune, which is a single question with no history.
    struct Turn { bool fromEngineer = true; std::string text; };
    std::vector<Turn> conversation;

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

// Answers already given, kept for the life of a session.
//
// Re-running TUNE LIVE MIX on a mix that has not changed should not cost a round trip, a
// token, or - the point of this class - the risk of a different answer. The key is the
// context fingerprint and the variation number, so the same listen asked the same way always
// returns the same intent, and a listen that really is different is never served a stale one.
class MixReasoningCache
{
public:
    struct Key
    {
        std::uint64_t fingerprint = 0;
        int variation = 0;
        bool refinement = false;
        bool operator< (const Key& o) const noexcept
        {
            if (fingerprint != o.fingerprint) return fingerprint < o.fingerprint;
            if (variation != o.variation) return variation < o.variation;
            return int (refinement) < int (o.refinement);
        }
    };

    const MixReasoningResponse* find (const Key& k) const
    {
        const auto it = entries.find (k);
        return it == entries.end() ? nullptr : &it->second;
    }
    void store (const Key& k, const MixReasoningResponse& r)
    {
        if (! r.valid) return;                       // a failure is never worth remembering
        if (entries.size() >= 32) entries.erase (entries.begin());
        entries[k] = r;
    }
    void clear() { entries.clear(); }
    size_t size() const noexcept { return entries.size(); }

private:
    std::map<Key, MixReasoningResponse> entries;
};

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
