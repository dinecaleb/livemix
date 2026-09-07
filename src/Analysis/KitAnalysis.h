#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "AnalysisResult.h"
#include "Recommendations/Recommendation.h"
#include "Core/ChannelRole.h"
#include "Core/StyleId.h"

namespace livemix
{

// Snapshot of one instance, gathered on the message thread after ANALYZE KIT.
struct KitMember
{
    uint64_t instanceId = 0;
    ChannelRole role = ChannelRole::KickIn;
    std::string name;
    AnalysisResult analysis;
    RecommendationResult recommendations;
    OutputStats output;
    float outputTrimDb = 0.0f;
};

struct KitRecommendationResult
{
    bool valid = false;
    int membersAnalyzed = 0;

    struct CaptureRow { uint64_t instanceId; std::string name; std::string health; float captureGainDb; };
    std::vector<CaptureRow> capture;

    struct ProcessingNote { uint64_t instanceId; std::string name; std::string what; std::string why; Confidence confidence; };
    std::vector<ProcessingNote> processing;

    struct BalanceItem
    {
        uint64_t instanceId; std::string name; std::string what; std::string why; Confidence confidence;
        float outputTrimDeltaDb; // 0 = informational
    };
    std::vector<BalanceItem> balance;
    std::vector<std::string> notes; // "Kick/snare relationship is healthy."
};

// Deterministic kit-level rules: capture summary, notable per-channel processing
// items, and close-mic / overhead balance relative to the kick/snare reference.
namespace KitAnalysis
{
    KitRecommendationResult analyze (const std::vector<KitMember>& members, StyleProfileId style);
}

} // namespace livemix
