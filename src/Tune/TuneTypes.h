#pragma once
#include <array>
#include <string>
#include <vector>
#include "Analysis/AnalysisResult.h"
#include "Recommendations/Recommendation.h"
#include "Core/ChannelRole.h"
#include "Core/StyleId.h"
#include "DSP/ChannelParameters.h"
#include "Profiles/Profile.h"

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
    // Aim somewhere other than the profile's own targets for this role. There is exactly one
    // caller: REFERENCE MIX, which measures a finished recording and hands the master the tonal
    // balance, width and density of it (Mix/ReferenceMix.h). The strategies, the safety
    // validator and the explanations are untouched - only the aim moves - so a reference can
    // never reach a parameter by a path a profile could not. Null = the profile's own targets.
    const SourceTargets* targetsOverride = nullptr;
    // Sample replacement, which changes what a drum microphone is for. `sampled`: this strip's
    // own sample stage is switched on, so the sample carries the drum's body and the microphone
    // only has to supply the attack - its gate can be far harder. `kitSampled`: any kick, snare
    // or tom in the session is sampled, so what the other drum microphones hear of those drums
    // is now the dirt in an otherwise clean kit, and they are cleaned up harder too. Both are
    // the engineer's switches (TUNE never sets a sample on), so the same settings and the same
    // listen still give the same plan. MixPlanner fills them; a plug-in has neither.
    bool sampled = false;
    bool kitSampled = false;
    // Whether this chain carries the sample stage at all. The stage exists only where MixEngine
    // turns it on - DLIVE's kick, snare, tom and hat strips - and no plug-in has it, so without
    // this a plug-in's Tune would propose a detector for a processor that is not in its chain
    // and not in its parameter table. Fitting the trigger is still done with the switch *off*
    // (that is the point: it says what to switch on), which is why `sampled` cannot answer it.
    bool hasSampleStage = false;
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
