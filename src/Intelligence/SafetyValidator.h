#pragma once
#include "Recommendations/Recommendation.h"

namespace livemix
{

// Clamps and filters recommendations before they are shown to the user or
// applied. Every path (standard and AI) runs through this so that a malformed
// or over-enthusiastic suggestion can never push a parameter outside its
// range or touch a parameter that recommendations are not allowed to change.
namespace SafetyValidator
{
    struct Report
    {
        int changesClamped = 0;
        int changesDropped = 0;
        int itemsDropped = 0;
    };

    // Rules for AI-origin items are stricter (smaller EQ moves, no makeup gain, etc.).
    RecommendationResult validate (const RecommendationResult& input, bool fromAI, Report* report = nullptr);
}

} // namespace livemix
