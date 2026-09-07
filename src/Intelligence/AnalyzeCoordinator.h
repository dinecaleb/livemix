#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include "IIntelligenceProvider.h"
#include "Recommendations/Recommendation.h"
#include "Tune/TuneTypes.h"

namespace livemix
{

// Orchestrates the post-capture pipeline on the message thread + one worker:
//   Standard Tune: Analysis -> TuneEngine (strategy + profile) -> SafetyValidator -> result
//   AI-assisted:   Standard Tune -> [worker: provider] -> SafetyValidator -> merged report
// AI is used only when explicitly enabled for this run and never touches the
// audio thread. Any AI failure (unavailable, timeout, invalid) leaves the
// Standard result in place, so the user always gets deterministic results.
class AnalyzeCoordinator
{
public:
    enum class Stage : int { Idle = 0, StandardComplete, WaitingForAI, AIComplete, AIFallback };

    struct Options
    {
        bool useAI = false;
        int aiTimeoutMs = 10000;
    };

    AnalyzeCoordinator();
    ~AnalyzeCoordinator();

    void setProvider (std::shared_ptr<IIntelligenceProvider> provider);
    std::shared_ptr<IIntelligenceProvider> getProvider() const;

    // Message thread. Computes the deterministic result synchronously (cheap) and,
    // when requested and possible, starts the AI worker.
    void run (const AnalysisResult& analysis, ChannelRole role, StyleProfileId style,
              const ChannelParameters& current, const Options& options,
              const OutputStats* output = nullptr,
              const std::vector<KitMember>& kitContext = {},
              const std::string& groupName = {});

    // Raises the cancel flag; the worker (and the provider's request) stop promptly.
    void cancel();

    Stage getStage() const noexcept { return Stage (stage.load (std::memory_order_acquire)); }
    bool isBusy() const noexcept { return getStage() == Stage::WaitingForAI; }

    RecommendationResult getResult() const;   // validated report; standard or merged
    TuneResult getTuneResult() const;         // the proposed starting point (before / proposed / sections)
    std::string getAIInterpretation() const;
    std::string getStatusMessage() const;     // e.g. "AI unavailable - showing Standard Analyze results"

    // Restore (from saved state) without recomputation.
    void setResult (const RecommendationResult& r);
    void setTuneResult (const TuneResult& t);

private:
    void joinWorker();

    mutable std::mutex mutex;
    std::shared_ptr<IIntelligenceProvider> provider;
    RecommendationResult result;
    TuneResult tuneResult;
    std::string interpretation;
    std::string status;

    std::thread worker;
    std::atomic<int> stage { int (Stage::Idle) };
    std::atomic<bool> cancelFlag { false };
    std::atomic<uint64_t> generation { 0 };
};

} // namespace livemix
