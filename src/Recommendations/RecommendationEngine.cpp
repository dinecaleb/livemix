// Facade kept for existing callers (kit rules, coordinator, tests). The decision
// logic lives in src/Tune (TuneEngine + source strategies).
#include "RecommendationEngine.h"
#include "Tune/TuneEngine.h"

namespace livemix
{

namespace RecommendationEngine
{

RecommendationResult recommend (const AnalysisResult& analysis, ChannelRole role, StyleProfileId style, const ChannelParameters& current, const OutputStats* output)
{
    TuneContext ctx;
    ctx.analysis = analysis;
    ctx.role = role;
    ctx.profile = style;
    ctx.current = current;
    if (output != nullptr) { ctx.output = *output; ctx.hasOutput = true; }
    return TuneEngine::tune (ctx).report;
}

} // namespace RecommendationEngine
} // namespace livemix
