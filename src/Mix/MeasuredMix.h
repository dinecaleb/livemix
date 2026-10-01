#pragma once
#include <atomic>
#include "MixPlanner.h"
#include "MixAI/RelationshipEngine.h"

namespace livemix::MeasuredMix
{
// Worker/offline only. Each render owns a fresh engine; never touches the running console.
struct Render
{
    bool valid = false;
    MixCapture::Result capture;
    std::vector<AnalysisResult> processed;
    std::array<AnalysisResult, int (MixBus::Count)> busOutputs {};
    std::vector<std::vector<AnalysisResult>> windows; // synchronized processed half-second windows
    int nonFiniteBlocks = 0, clampedBlocks = 0;
};
struct Verification
{
    bool available = false, safe = false;
    float loudnessErrorLu = 0, spectralErrorDb = 0, relationshipPenaltyDb = 0;
    float score = 0;
    std::vector<MixRelationship> relationships;
    std::vector<std::string> warnings;
};
struct Result
{
    MixPlan plan;
    Render before, initial, after;
    Verification verification;
    int renders = 0, acceptedPasses = 0;
    bool fallback = true;
};
Render render (const MixSession&, const MixParameters&, const MixCapture::Replay&,
               const SampleBankTable* banks = nullptr, const std::atomic<bool>* cancel = nullptr);
Verification verify (const MixPlanContext&, const MixParameters&, const Render&);
// Same captured PCM/parameters and settings produce the same absolute proposal even
// after it has been kept. current is only the BEFORE snapshot, not the refinement seed.
Result plan (const MixPlanContext&, const SampleBankTable* banks = nullptr,
             const std::atomic<bool>* cancel = nullptr,
             const MixPlanner::PlanSelection* selection = nullptr);
Result refine (const MixPlanContext&, const MixPlan&, int maxPasses = 3,
               const SampleBankTable* banks = nullptr, const std::atomic<bool>* cancel = nullptr,
               const MixPlanner::PlanSelection* selection = nullptr);
}
