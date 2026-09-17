#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "CapabilityResolver.h"
#include "MixContext.h"
#include "MixReasoningProvider.h"
#include "MixSafetyValidator.h"
#include "ProcessingPlan.h"

namespace livemix
{

// TUNE LIVE MIX, as one explicit state machine.
//
// The lifecycle lives here and nowhere else: no page, no sheet and no button holds a piece of
// it. The coordinator never touches audio, never owns a device and never blocks the message
// thread - the one slow step, asking the reasoning provider, runs on a worker that is polled.
// Whatever happens to that worker (no network, a timeout, a malformed reply, a crash inside a
// provider), the mix that is already running is untouched and the state machine ends in
// Failed with a sentence a volunteer can read.
//
// The captures themselves belong to MixController, which owns the engine: it starts a listen
// and hands the result here. That keeps one owner for the audio and one owner for the process.
class TuneLiveCoordinator
{
public:
    enum class State : int
    {
        Idle = 0,
        CapturingInitial,        // the band is playing; DLIVE is listening
        AnalyzingInitial,        // measurements, relationships, the deterministic plan
        WaitingForReasoning,     // the worker is asking the provider
        Resolving,               // intent -> processing plan
        Validating,              // bounds and safety
        Applying,                // the controller is putting it on the mix
        CapturingVerify,         // listening again to what was applied
        AnalyzingVerify,
        WaitingForRefinement,
        ValidatingRefinement,
        ApplyingRefinement,
        Ready,                   // LIVE MIX READY
        Failed,
        Cancelled,
        Count
    };

    static const char* stateName (State) noexcept;

    struct Settings
    {
        bool refinementPass = true;      // one conservative correction, never an open loop
        std::string userRequest;         // "make the drums bigger" - empty for a plain Tune
        // 0 is the repeatable mix and is what the TUNE LIVE MIX button always asks for.
        // TRY ANOTHER MIX asks for 1, 2, 3 ... - a different reading of the same band, wanted
        // on purpose rather than arrived at by accident.
        int variation = 0;
        // Reuse an answer already given for this exact listen and variation. On by default:
        // running the same thing twice must give the same mix, and it should not cost a round
        // trip to find that out. TRY ANOTHER MIX still reuses, because variation 2 asked twice
        // is still the same question.
        bool reuseAnswers = true;
        // AI MIX CHAT: what has already been said, so "and a bit more" follows on. Empty for
        // an ordinary Tune.
        std::vector<MixReasoningRequest::Turn> conversation;
    };

    // Everything a development build wants to know about a run, and nothing a user's session
    // should carry off the machine. No keys, no audio, no names of anything outside this mix.
    struct Diagnostics
    {
        std::string sessionId;
        std::string provider, model;
        bool sentDataExternally = false;
        double reasoningMs = 0.0;
        double refinementMs = 0.0;
        int requestBytes = 0;
        int intentObjectives = 0;
        int actionsProposed = 0, actionsAccepted = 0, actionsClamped = 0, actionsRejected = 0;
        bool refinementRan = false;
        bool refinementChanged = false;
        int variation = 0;
        std::uint64_t contextFingerprint = 0;    // the identity of the listen this run reasoned about
        bool answerFromCache = false;            // this exact question had already been answered
        std::vector<std::string> problems;      // anything a reply carried that had to be dropped
    };

    TuneLiveCoordinator();
    ~TuneLiveCoordinator();

    void setProvider (std::shared_ptr<MixReasoningProvider>);
    std::shared_ptr<MixReasoningProvider> getProvider() const;
    void setSettings (const Settings& s) { settings = s; }
    const Settings& getSettings() const noexcept { return settings; }

    // ---- Driven by MixController ----
    void beginListening (const std::string& sessionId);
    void onListenComplete (const MixPlanContext&, const MixPlan& baseline);
    void poll();                                   // message thread, ~30 Hz
    void onApplied();                              // the proposal is on the mix
    void beginVerifyListening();
    void onVerifyComplete (const MixPlanContext&, const MixPlan& baseline);
    void finish();                                 // LIVE MIX READY
    void cancel();
    void reset();
    // Answers already given for this session. Cleared when the routing is rebuilt: a mix of a
    // different shape is a different question.
    void clearAnswers();

    // ---- What the UI reads ----
    State getState() const noexcept { return State (state.load (std::memory_order_acquire)); }
    bool isBusy() const noexcept;
    bool isWaitingOnProvider() const noexcept;
    bool wantsApply() const noexcept;              // the controller should apply getProposed() now
    bool wantsVerifyListen() const noexcept;
    std::string getStatusText() const;
    std::string getFailure() const;

    // True once the run has a mix worth keeping - the deterministic plan from the listen,
    // with whatever the reasoning layer was able to add on top. Whether the reasoning layer
    // changed anything at all is getPlan().countApplied().
    bool hasProposal() const;
    const MixContext& getContext() const;
    const MixContext& getVerification() const;
    const MixIntent& getIntent() const;
    const ProcessingPlan& getPlan() const;         // validated: accepted, clamped and refused
    const MixSafetyValidator::Report& getReport() const;
    const MixParameters& getProposed() const;      // the deterministic plan plus every accepted action
    const MixPlan& getBaseline() const;
    Diagnostics getDiagnostics() const;

    // REVIEW CHANGES. One entry per decision, in the order they were made, with what DLIVE
    // could only approximate and what it refused kept in rather than quietly dropped. An
    // engineer can read every value in the Inspector; this is the sentence version.
    struct ReviewLine
    {
        enum class Kind { Summary = 0, Decision, Approximated, NotPossible, Refused };
        Kind kind = Kind::Decision;
        std::string what;
        std::string why;
    };
    std::vector<ReviewLine> getReview() const;
    // The same, flattened: for the session record and for logs.
    std::vector<std::string> getReviewLines() const;
    // What is stored with the session so the run can be read back without a provider.
    json::Value toJson() const;

private:
    void startWorker (bool refinement);
    void joinWorker();
    void resolveAndValidate (bool refinement);
    void setState (State s) noexcept { state.store (int (s), std::memory_order_release); }
    void fail (std::string why);

    Settings settings;
    std::shared_ptr<MixReasoningProvider> provider;
    mutable std::mutex mutex;

    std::atomic<int> state { int (State::Idle) };
    std::atomic<bool> cancelFlag { false };
    std::atomic<bool> workerDone { false };
    std::thread worker;

    MixPlanContext planContext;          // the listen the run is reasoning about
    MixPlan baselinePlan;                // the deterministic mix: what the actions are deltas on
    DspCapabilityRegistry registry;
    MixContext context, verification;
    MixReasoningRequest pending;
    MixReasoningResponse response;
    MixReasoningCache answers;
    MixIntent intent;
    ProcessingPlan plan;
    MixSafetyValidator::Report report;
    MixParameters proposed;
    bool haveProposal = false;
    std::string failure;
    Diagnostics diagnostics;
};

} // namespace livemix
