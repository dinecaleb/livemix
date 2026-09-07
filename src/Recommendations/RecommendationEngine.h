#pragma once
#include "Recommendation.h"
#include "Analysis/AnalysisResult.h"
#include "Core/ChannelRole.h"
#include "Core/StyleId.h"
#include "DSP/ChannelParameters.h"

namespace livemix
{

// Deterministic, rule-based recommendation engine. Runs on the message
// thread after analysis completes. Produces explainable WHAT / WHY / CONFIDENCE.
namespace RecommendationEngine
{
    RecommendationResult recommend (const AnalysisResult& analysis,
                                    ChannelRole role,
                                    StyleProfileId style,
                                    const ChannelParameters& current,
                                    const OutputStats* output = nullptr);
}

} // namespace livemix
