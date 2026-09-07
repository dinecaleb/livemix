#pragma once
#include "TuneTypes.h"

namespace livemix
{

// Deterministic Tune: analysis + profile targets + source strategy -> a validated
// professional starting point. Runs on the message thread after a capture. No AI,
// no network; the optional AI layer (AnalyzeCoordinator) may append to the report
// but never bypasses this path.
namespace TuneEngine
{
    TuneResult tune (const TuneContext& ctx);
}

} // namespace livemix
