#include "TuneLiveCoordinator.h"
#include <chrono>

namespace livemix
{

const char* TuneLiveCoordinator::stateName (State s) noexcept
{
    switch (s)
    {
        case State::Idle:                 return "IDLE";
        case State::CapturingInitial:     return "CAPTURING_INITIAL";
        case State::AnalyzingInitial:     return "ANALYZING_INITIAL";
        case State::WaitingForReasoning:  return "WAITING_FOR_REASONING";
        case State::Resolving:            return "RESOLVING";
        case State::Validating:           return "VALIDATING";
        case State::Applying:             return "APPLYING";
        case State::CapturingVerify:      return "CAPTURING_VERIFY";
        case State::AnalyzingVerify:      return "ANALYZING_VERIFY";
        case State::WaitingForRefinement: return "WAITING_FOR_REFINEMENT";
        case State::ValidatingRefinement: return "VALIDATING_REFINEMENT";
        case State::ApplyingRefinement:   return "APPLYING_REFINEMENT";
        case State::Ready:                return "READY";
        case State::Failed:               return "FAILED";
        case State::Cancelled:            return "CANCELLED";
        case State::Count:
        default:                          return "?";
    }
}

TuneLiveCoordinator::TuneLiveCoordinator()
{
    provider = std::make_shared<LocalMixReasoningProvider>();
}

TuneLiveCoordinator::~TuneLiveCoordinator()
{
    cancelFlag.store (true, std::memory_order_release);
    joinWorker();
}

void TuneLiveCoordinator::setProvider (std::shared_ptr<MixReasoningProvider> p)
{
    std::lock_guard<std::mutex> lock (mutex);
    // Never left without one: the offline engineer is always a valid answer, and TUNE LIVE MIX
    // has to work on a machine that has never seen the internet.
    provider = p != nullptr ? std::move (p) : std::static_pointer_cast<MixReasoningProvider> (std::make_shared<LocalMixReasoningProvider>());
}

std::shared_ptr<MixReasoningProvider> TuneLiveCoordinator::getProvider() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return provider;
}

bool TuneLiveCoordinator::isBusy() const noexcept
{
    const auto s = getState();
    return s != State::Idle && s != State::Ready && s != State::Failed && s != State::Cancelled;
}

bool TuneLiveCoordinator::isWaitingOnProvider() const noexcept
{
    const auto s = getState();
    return s == State::WaitingForReasoning || s == State::WaitingForRefinement;
}

bool TuneLiveCoordinator::wantsApply() const noexcept
{
    const auto s = getState();
    return s == State::Applying || s == State::ApplyingRefinement;
}

bool TuneLiveCoordinator::wantsVerifyListen() const noexcept { return getState() == State::CapturingVerify; }

void TuneLiveCoordinator::reset()
{
    cancelFlag.store (true, std::memory_order_release);
    joinWorker();
    std::lock_guard<std::mutex> lock (mutex);
    cancelFlag.store (false, std::memory_order_release);
    workerDone.store (false, std::memory_order_release);
    context = {};
    verification = {};
    intent = {};
    plan = {};
    report = {};
    response = {};
    pending = {};
    proposed = {};
    haveProposal = false;
    failure.clear();
    diagnostics = {};
    setState (State::Idle);
}

void TuneLiveCoordinator::beginListening (const std::string& sessionId)
{
    reset();
    std::lock_guard<std::mutex> lock (mutex);
    diagnostics.sessionId = sessionId;
    if (provider != nullptr)
    {
        diagnostics.provider = provider->getName();
        diagnostics.model = provider->getModel();
        diagnostics.sentDataExternally = provider->sendsDataExternally();
    }
    setState (State::CapturingInitial);
}

void TuneLiveCoordinator::cancel()
{
    const auto s = getState();
    if (s == State::Idle || s == State::Ready) return;
    cancelFlag.store (true, std::memory_order_release);
    // The worker is left to notice the flag and exit; nothing is applied and the mix that is
    // already running is exactly as it was. Cancelling never leaves a half-applied state,
    // because a plan only reaches the mix as one whole MixParameters snapshot.
    setState (State::Cancelled);
}

void TuneLiveCoordinator::fail (std::string why)
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        failure = std::move (why);
    }
    setState (State::Failed);
}

void TuneLiveCoordinator::onListenComplete (const MixPlanContext& ctx, const MixPlan& baseline)
{
    if (getState() != State::CapturingInitial) return;
    setState (State::AnalyzingInitial);

    {
        std::lock_guard<std::mutex> lock (mutex);
        planContext = ctx;
        baselinePlan = baseline;
        registry = DspCapabilityRegistry::build (ctx.session, ctx.graph);
        context = buildMixContext (ctx, baseline);
        proposed = baseline.proposed;
        haveProposal = false;
    }

    if (! context.adequacy.sufficient)
    {
        // A confident mix built from a band that was not playing is worse than no mix.
        fail (context.adequacy.reason + " " + context.adequacy.guidance);
        return;
    }

    {
        // From here the run has a mix worth keeping whatever the reasoning layer does next:
        // the deterministic plan built from this listen. hasProposal() means "there is a mix
        // to keep", not "the reasoning layer changed something" - that is countApplied().
        std::lock_guard<std::mutex> lock (mutex);
        haveProposal = true;
    }
    startWorker (false);
}

void TuneLiveCoordinator::onVerifyComplete (const MixPlanContext& ctx, const MixPlan& baseline)
{
    if (getState() != State::CapturingVerify) return;
    setState (State::AnalyzingVerify);
    {
        std::lock_guard<std::mutex> lock (mutex);
        verification = buildMixContext (ctx, baseline);
    }
    if (! settings.refinementPass) { finish(); return; }
    startWorker (true);
}

void TuneLiveCoordinator::startWorker (bool refinement)
{
    joinWorker();
    workerDone.store (false, std::memory_order_release);

    std::shared_ptr<MixReasoningProvider> p;
    {
        std::lock_guard<std::mutex> lock (mutex);
        p = provider;
        pending = {};
        pending.context = context;
        pending.capabilities = registry.toJson();
        pending.instructions = mixEngineerInstructions (planContext.session.profile, planContext.session.purpose);
        pending.userRequest = settings.userRequest;
        pending.refinement = refinement;
        if (refinement)
        {
            pending.previousIntent = intent;
            pending.verification = verification;
            pending.hasVerification = true;
            for (const auto& a : plan.actions)
                if (a.applies()) pending.applied.push_back (a.target.id() + " " + a.paramId + ": " + a.reason);
        }
    }

    if (p == nullptr) { fail ("No reasoning provider is configured."); return; }

    setState (refinement ? State::WaitingForRefinement : State::WaitingForReasoning);
    worker = std::thread ([this, p, refinement]
    {
        MixReasoningResponse r;
        const auto started = std::chrono::steady_clock::now();
        try
        {
            MixReasoningRequest requestCopy;
            {
                std::lock_guard<std::mutex> lock (mutex);
                requestCopy = pending;
            }
            r = p->reason (requestCopy, cancelFlag);
        }
        catch (const std::exception& e)
        {
            // A provider is third-party code as far as DLIVE is concerned. It is allowed to
            // fail; it is not allowed to take the application down with it.
            r = {};
            r.error = std::string ("The reasoning provider failed: ") + e.what();
        }
        catch (...)
        {
            r = {};
            r.error = "The reasoning provider failed.";
        }
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count();
        {
            std::lock_guard<std::mutex> lock (mutex);
            response = std::move (r);
            if (response.elapsedMs <= 0.0) response.elapsedMs = ms;
            if (refinement) diagnostics.refinementMs = response.elapsedMs;
            else diagnostics.reasoningMs = response.elapsedMs;
            diagnostics.requestBytes = response.requestBytes;
        }
        workerDone.store (true, std::memory_order_release);
    });
}

void TuneLiveCoordinator::joinWorker()
{
    if (worker.joinable()) worker.join();
}

void TuneLiveCoordinator::poll()
{
    const auto s = getState();
    if (s != State::WaitingForReasoning && s != State::WaitingForRefinement) return;
    if (! workerDone.load (std::memory_order_acquire)) return;
    joinWorker();

    const bool refinement = s == State::WaitingForRefinement;
    if (cancelFlag.load (std::memory_order_acquire)) { setState (State::Cancelled); return; }

    MixReasoningResponse r;
    {
        std::lock_guard<std::mutex> lock (mutex);
        r = response;
        for (const auto& p : r.problems) diagnostics.problems.push_back (p);
    }

    if (! r.valid || ! r.intent.valid)
    {
        if (refinement)
        {
            // The first pass is already applied and good. A refinement that cannot be reached
            // is not a failure of the mix - it is the end of the run.
            finish();
            return;
        }
        fail (r.error.empty() ? "DLIVE could not complete TUNE LIVE MIX. Your mix has not been changed."
                              : r.error + " Your mix has not been changed.");
        return;
    }

    setState (refinement ? State::ValidatingRefinement : State::Resolving);
    resolveAndValidate (refinement);
}

void TuneLiveCoordinator::resolveAndValidate (bool refinement)
{
    std::vector<std::string> problems;
    MixIntent validated;
    {
        std::lock_guard<std::mutex> lock (mutex);
        validated = validateMixIntent (response.intent, registry, &problems);
    }

    if (! validated.valid || (validated.noChangeRequired && validated.targets.empty()))
    {
        std::lock_guard<std::mutex> lock (mutex);
        intent = validated;
        for (const auto& p : problems) diagnostics.problems.push_back (p);
        diagnostics.intentObjectives = validated.objectiveCount();
        if (refinement) diagnostics.refinementChanged = false;
        // "MIX READY - no further changes" is a professional answer, not a failure.
        plan = {};
        plan.valid = true;
        plan.summary = validated.summary;
        haveProposal = ! refinement ? haveProposal : haveProposal;
    }
    else
    {
        CapabilityResolver::Context rc;
        MixSafetyValidator::Context vc;
        ProcessingPlan resolved;
        MixSafetyValidator::Report rep;
        {
            std::lock_guard<std::mutex> lock (mutex);
            rc.registry = &registry;
            rc.mix = refinement ? &verification : &context;
            rc.baseline = &proposed;         // refine what is running now, not what the console started at
            rc.graph = &planContext.graph;
            rc.profile = planContext.session.profile;
            resolved = CapabilityResolver::resolve (validated, rc);

            setState (refinement ? State::ValidatingRefinement : State::Validating);
            vc.registry = &registry;
            vc.baseline = &proposed;
            vc.graph = &planContext.graph;
            vc.mix = refinement ? &verification : &context;
            vc.profile = planContext.session.profile;
            resolved = MixSafetyValidator::validate (resolved, vc, &rep);

            intent = validated;
            plan = resolved;
            report = rep;
            proposed = applyProcessingPlan (proposed, resolved, planContext.graph);
            haveProposal = true;

            for (const auto& p : problems) diagnostics.problems.push_back (p);
            diagnostics.intentObjectives = validated.objectiveCount();
            diagnostics.actionsProposed += int (resolved.actions.size());
            diagnostics.actionsAccepted += rep.accepted;
            diagnostics.actionsClamped += rep.clamped;
            diagnostics.actionsRejected += rep.rejected;
            if (refinement) diagnostics.refinementChanged = resolved.countApplied() > 0;
        }
    }

    if (refinement) { diagnostics.refinementRan = true; setState (State::ApplyingRefinement); }
    else setState (State::Applying);
}

void TuneLiveCoordinator::onApplied()
{
    const auto s = getState();
    if (s == State::Applying)
    {
        if (settings.refinementPass) beginVerifyListening();
        else finish();
        return;
    }
    if (s == State::ApplyingRefinement) finish();
}

void TuneLiveCoordinator::beginVerifyListening()
{
    if (getState() != State::Applying) return;
    setState (State::CapturingVerify);
}

void TuneLiveCoordinator::finish() { setState (State::Ready); }

std::string TuneLiveCoordinator::getFailure() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return failure;
}

bool TuneLiveCoordinator::hasProposal() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return haveProposal;
}

const MixContext& TuneLiveCoordinator::getContext() const { return context; }
const MixContext& TuneLiveCoordinator::getVerification() const { return verification; }
const MixIntent& TuneLiveCoordinator::getIntent() const { return intent; }
const ProcessingPlan& TuneLiveCoordinator::getPlan() const { return plan; }
const MixSafetyValidator::Report& TuneLiveCoordinator::getReport() const { return report; }
const MixParameters& TuneLiveCoordinator::getProposed() const { return proposed; }
const MixPlan& TuneLiveCoordinator::getBaseline() const { return baselinePlan; }

TuneLiveCoordinator::Diagnostics TuneLiveCoordinator::getDiagnostics() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return diagnostics;
}

std::string TuneLiveCoordinator::getStatusText() const
{
    switch (getState())
    {
        case State::Idle:                 return "Press TUNE LIVE MIX and have the band play normally.";
        case State::CapturingInitial:     return "Listening to the full band...";
        case State::AnalyzingInitial:     return "Working out what is happening between the sources...";
        case State::WaitingForReasoning:  return "Building the mix...";
        case State::Resolving:            return "Working out how to do it with what DLIVE has...";
        case State::Validating:           return "Checking every change is safe...";
        case State::Applying:             return "Applying the mix...";
        case State::CapturingVerify:      return "Listening again to what it did...";
        case State::AnalyzingVerify:      return "Comparing before and after...";
        case State::WaitingForRefinement: return "Making the last corrections...";
        case State::ValidatingRefinement: return "Checking the corrections...";
        case State::ApplyingRefinement:   return "Applying the corrections...";
        case State::Ready:                return "LIVE MIX READY";
        case State::Failed:               return getFailure();
        case State::Cancelled:            return "TUNE LIVE MIX was stopped. Your mix has not been changed.";
        case State::Count:
        default:                          return {};
    }
}

std::vector<TuneLiveCoordinator::ReviewLine> TuneLiveCoordinator::getReview() const
{
    std::lock_guard<std::mutex> lock (mutex);
    std::vector<ReviewLine> out;
    auto named = [this] (const MixTargetRef& t)
    {
        const auto* caps = registry.find (t);
        return caps != nullptr && ! caps->name.empty() ? caps->name : t.id();
    };
    auto already = [&out] (const std::string& what)
    {
        for (const auto& l : out) if (l.what == what) return true;
        return false;
    };

    if (! intent.summary.empty()) out.push_back ({ ReviewLine::Kind::Summary, intent.summary, {} });

    for (const auto& a : plan.actions)
    {
        // One line per decision, not per control: a single objective writes five parameters
        // of an EQ band, and they share one sentence.
        if (a.status == MixActionStatus::Unchanged) continue;
        if (a.status == MixActionStatus::Rejected)
        {
            const std::string what = named (a.target) + ": not done";
            if (! already (what)) out.push_back ({ ReviewLine::Kind::Refused, what, a.note });
            continue;
        }
        if (! a.applies() || a.reason.empty() || already (a.reason)) continue;
        out.push_back ({ a.resolution == ResolutionStatus::Exact ? ReviewLine::Kind::Decision : ReviewLine::Kind::Approximated,
                         a.reason,
                         a.resolution == ResolutionStatus::Exact
                             ? std::string()
                             : std::string (resolutionStatusLabel (a.resolution)) + (a.note.empty() ? "" : ". " + a.note) });
    }

    for (const auto& u : plan.unsupported)
        if (! already (u)) out.push_back ({ ReviewLine::Kind::NotPossible, u, {} });

    return out;
}

std::vector<std::string> TuneLiveCoordinator::getReviewLines() const
{
    std::vector<std::string> out;
    for (const auto& l : getReview())
    {
        std::string prefix;
        switch (l.kind)
        {
            case ReviewLine::Kind::Approximated: prefix = "APPROXIMATED - "; break;
            case ReviewLine::Kind::NotPossible:  prefix = "NOT POSSIBLE - "; break;
            case ReviewLine::Kind::Refused:      prefix = "REFUSED - "; break;
            default: break;
        }
        out.push_back (prefix + l.what + (l.why.empty() ? "" : " " + l.why));
    }
    return out;
}

json::Value TuneLiveCoordinator::toJson() const
{
    std::lock_guard<std::mutex> lock (mutex);
    auto v = json::Value::object();
    v.set ("state", stateName (State (state.load (std::memory_order_acquire))));
    v.set ("provider", diagnostics.provider);
    if (! diagnostics.model.empty()) v.set ("model", diagnostics.model);
    v.set ("intent", intent.toJson());
    v.set ("plan", plan.toJson());
    auto counts = json::Value::object();
    counts.set ("accepted", report.accepted);
    counts.set ("clamped", report.clamped);
    counts.set ("rejected", report.rejected);
    v.set ("validation", std::move (counts));
    return v;
}

} // namespace livemix
