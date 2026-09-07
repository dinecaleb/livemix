#include "AnalyzeCoordinator.h"
#include "SafetyValidator.h"
#include "Tune/TuneEngine.h"
#include <chrono>
#include <future>

namespace livemix
{

AnalyzeCoordinator::AnalyzeCoordinator()
    : provider (std::make_shared<NullIntelligenceProvider>())
{
}

AnalyzeCoordinator::~AnalyzeCoordinator()
{
    cancel();
    joinWorker();
}

void AnalyzeCoordinator::joinWorker()
{
    if (worker.joinable()) worker.join();
}

void AnalyzeCoordinator::setProvider (std::shared_ptr<IIntelligenceProvider> p)
{
    std::lock_guard<std::mutex> lock (mutex);
    provider = p ? std::move (p) : std::make_shared<NullIntelligenceProvider>();
}

std::shared_ptr<IIntelligenceProvider> AnalyzeCoordinator::getProvider() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return provider;
}

void AnalyzeCoordinator::cancel()
{
    cancelFlag.store (true);
    generation.fetch_add (1);
}

RecommendationResult AnalyzeCoordinator::getResult() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return result;
}

TuneResult AnalyzeCoordinator::getTuneResult() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return tuneResult;
}

std::string AnalyzeCoordinator::getAIInterpretation() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return interpretation;
}

std::string AnalyzeCoordinator::getStatusMessage() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return status;
}

void AnalyzeCoordinator::setResult (const RecommendationResult& r)
{
    std::lock_guard<std::mutex> lock (mutex);
    result = SafetyValidator::validate (r, false);
    tuneResult = TuneResult {};
    tuneResult.valid = result.valid;
    tuneResult.report = result;
    tuneResult.noChangeRequired = true; // restored report: the proposal was already applied (or kept) in that session
    stage.store (int (r.valid ? Stage::StandardComplete : Stage::Idle));
}

void AnalyzeCoordinator::setTuneResult (const TuneResult& t)
{
    std::lock_guard<std::mutex> lock (mutex);
    tuneResult = t;
    result = t.report;
}

void AnalyzeCoordinator::run (const AnalysisResult& analysis, ChannelRole role, StyleProfileId style,
                              const ChannelParameters& current, const Options& options,
                              const OutputStats* output, const std::vector<KitMember>& kitContext,
                              const std::string& groupName)
{
    cancel();
    joinWorker();
    cancelFlag.store (false);

    // 1. Standard Tune: deterministic, always. TuneEngine validates its own report.
    TuneContext ctx;
    ctx.analysis = analysis;
    ctx.role = role;
    ctx.profile = style;
    ctx.current = current;
    if (output != nullptr) { ctx.output = *output; ctx.hasOutput = true; }
    const TuneResult tuned = TuneEngine::tune (ctx);
    RecommendationResult standard = tuned.report;

    std::shared_ptr<IIntelligenceProvider> prov;
    {
        std::lock_guard<std::mutex> lock (mutex);
        result = standard;
        tuneResult = tuned;
        interpretation.clear();
        status = "Standard Tune";
        prov = provider;
    }

    if (! standard.valid)
    {
        stage.store (int (Stage::Idle));
        return;
    }

    // 2. Optional AI interpretation.
    if (! options.useAI)
    {
        stage.store (int (Stage::StandardComplete));
        return;
    }
    if (prov == nullptr || ! prov->isAvailable())
    {
        std::lock_guard<std::mutex> lock (mutex);
        status = "AI assistance is on but no AI provider is available. Showing Standard Tune results.";
        stage.store (int (Stage::AIFallback));
        return;
    }

    stage.store (int (Stage::WaitingForAI));
    const uint64_t myGeneration = generation.load();
    auto request = std::make_shared<IntelligenceRequest>();
    request->analysis = analysis;
    request->role = role;
    request->style = style;
    request->currentParameters = current;
    request->standardRecommendations = standard;
    if (output != nullptr) request->output = *output;
    request->kitContext = kitContext;
    request->groupName = groupName;
    const int timeoutMs = options.aiTimeoutMs;

    worker = std::thread ([this, prov, request, standard, myGeneration, timeoutMs]
    {
        // The provider runs on a helper thread so this worker can enforce the timeout.
        // The helper is always joined (never detached, never a std::async future whose
        // destructor would block): on timeout or cancel we raise cancelFlag, which the
        // provider polls, so the join returns promptly instead of after the full timeout.
        IntelligenceResponse response;
        std::promise<void> finished;
        std::future<void> finishedFuture = finished.get_future();
        std::thread helper ([&]
        {
            response = prov->interpret (*request, cancelFlag);
            finished.set_value();
        });
        bool timedOut = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
        while (finishedFuture.wait_for (std::chrono::milliseconds (20)) != std::future_status::ready)
        {
            if (generation.load() != myGeneration) { cancelFlag.store (true); break; } // cancelled or superseded
            if (std::chrono::steady_clock::now() >= deadline) { timedOut = true; cancelFlag.store (true); break; }
        }
        helper.join();

        if (generation.load() != myGeneration) return; // superseded or cancelled

        std::lock_guard<std::mutex> lock (mutex);
        if (timedOut)
        {
            status = "AI response timed out. Showing Standard Tune results.";
            stage.store (int (Stage::AIFallback), std::memory_order_release);
            return;
        }
        if (! response.valid)
        {
            status = "AI unavailable (" + (response.error.empty() ? std::string ("no result") : response.error) + "). Showing Standard Tune results.";
            stage.store (int (Stage::AIFallback), std::memory_order_release);
            return;
        }

        // Merge: standard items first (objective measurements), AI items after validation.
        RecommendationResult aiOnly;
        aiOnly.valid = true;
        aiOnly.items = response.recommendations;
        SafetyValidator::Report report;
        RecommendationResult validated = SafetyValidator::validate (aiOnly, true, &report);

        RecommendationResult merged = standard;
        for (auto& item : validated.items)
        {
            item.what = "[AI] " + item.what;
            merged.items.push_back (item);
        }
        result = merged;
        tuneResult.report = merged; // AI items are opt-in: they never enter the proposed parameters
        interpretation = response.interpretation;
        status = "AI-assisted (" + response.providerName + ")";
        if (report.changesClamped > 0 || report.changesDropped > 0 || report.itemsDropped > 0)
            status += " - " + std::to_string (report.changesClamped) + " values clamped, "
                    + std::to_string (report.changesDropped + report.itemsDropped) + " unsafe suggestions removed";
        stage.store (int (Stage::AIComplete), std::memory_order_release);
    });
}

} // namespace livemix
