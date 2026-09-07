#pragma once
#include <array>
#include <string>
#include <vector>
#include "Analysis/AnalysisResult.h"
#include "Recommendations/Recommendation.h"
#include "Core/ChannelRole.h"
#include "Core/StyleId.h"
#include "DSP/ChannelParameters.h"

namespace livemix
{

// Everything Tune needs to make a decision about one source.
struct TuneContext
{
    AnalysisResult analysis;
    ChannelRole role = ChannelRole::KickIn;
    StyleProfileId profile = StyleProfileId::ModernGospel;
    ChannelParameters current;   // what the plugin is doing right now (baseline or the user's edits)
    OutputStats output;          // processed-output level during the capture
    bool hasOutput = false;
};

struct TuneSectionSummary
{
    TuneSection section = TuneSection::Notes;
    std::string summary;         // one line: "Reduced low-mid resonance." / "No change required."
    int itemCount = 0;
    bool changed = false;        // at least one item carries parameter changes
};

// The professional starting point Tune produced, plus its explanation.
struct TuneResult
{
    bool valid = false;
    std::string headline;               // "TOM 1 TUNED" or "TOM 1: NO CHANGE REQUIRED"
    bool noChangeRequired = false;
    RecommendationResult report;        // validated, explainable items (WHAT / WHY / CONFIDENCE)
    ChannelParameters before;           // settings when Tune ran
    ChannelParameters proposed;         // before + every validated change
    std::array<TuneSectionSummary, int (TuneSection::Count)> sections {};
    int parametersChanged = 0;
};

// Applies a change list to a parameter snapshot (by parameter id). Unknown ids are ignored.
ChannelParameters applyChanges (const ChannelParameters& base, const std::vector<ParameterChange>& changes);

// Parameter-level differences between two snapshots, as changes that turn `from` into `to`.
std::vector<ParameterChange> diffParameters (const ChannelParameters& from, const ChannelParameters& to);

} // namespace livemix
