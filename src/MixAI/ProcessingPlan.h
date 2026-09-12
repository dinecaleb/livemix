#pragma once
#include <string>
#include <vector>
#include "Core/Json.h"
#include "DspCapabilityRegistry.h"
#include "MixIntent.h"
#include "Mix/MixParameters.h"

namespace livemix
{

inline constexpr int kProcessingPlanSchemaVersion = 1;

// How faithfully DLIVE could answer what was asked for. Reported, never hidden: a mix
// engineer who says "done" when they substituted something else is not one you can trust.
enum class ResolutionStatus : int
{
    Exact = 0,      // DLIVE has exactly this
    Approximated,   // built out of what DLIVE has; close, and said so
    Substituted,    // a different tool that serves the same musical end
    Unsupported     // DLIVE cannot do this; nothing was applied
};

const char* resolutionStatusId (ResolutionStatus) noexcept;
const char* resolutionStatusLabel (ResolutionStatus) noexcept;

// Unchanged is not a refusal: the bounds simply left nothing to do, which is an ordinary
// outcome of refining a mix that is already close. It is kept in the plan for the record and
// left out of what the user is shown, because "DLIVE declined to move a control by 0 dB" is
// not a decision anybody needs to read.
enum class MixActionStatus : int { Proposed = 0, Accepted, Clamped, Rejected, Unchanged };
const char* mixActionStatusId (MixActionStatus) noexcept;

// One executable change. Everything needed to apply it, to explain it and to refuse it.
struct MixAction
{
    MixTargetRef target;
    DspProcessor processor = DspProcessor::ToneEq;
    std::string paramId;                       // the engine's own id
    float value = 0.0f;                        // natural units (dB, Hz, ms, ratio, 0/1)
    float previousValue = 0.0f;
    std::string reason;                        // plain sentence: what this does and why
    Confidence confidence = Confidence::Medium;
    MixObjectiveType objective = MixObjectiveType::Presence;
    int intentIndex = -1;                      // which MixTargetIntent asked for it
    ResolutionStatus resolution = ResolutionStatus::Exact;
    MixActionStatus status = MixActionStatus::Proposed;
    std::string note;                          // why it was clamped or rejected, or what was approximated

    bool applies() const noexcept { return status == MixActionStatus::Accepted || status == MixActionStatus::Clamped; }
    json::Value toJson() const;
};

// What DLIVE is actually going to do, in DLIVE's own terms. Versioned and serialisable:
// this is what a session stores so that yesterday's mix opens the same tomorrow, with no
// provider, no network and no reasoning.
struct ProcessingPlan
{
    int schemaVersion = kProcessingPlanSchemaVersion;
    bool valid = false;
    std::string summary;
    std::vector<MixAction> actions;
    // Asked for, honestly not possible. Carried to the user rather than quietly dropped.
    std::vector<std::string> unsupported;

    int countApplied() const noexcept;
    int countRejected() const noexcept;
    int countUnchanged() const noexcept;
    int countClamped() const noexcept;
    bool empty() const noexcept { return countApplied() == 0; }

    json::Value toJson() const;
    std::string write (bool pretty = false) const { return toJson().write (pretty); }
};

// Applies every accepted action to a copy of `base`. The one place a plan becomes a mix:
// nothing else writes MixParameters on behalf of the reasoning layer, so what the Inspector
// shows and what the audio thread runs can never diverge from what was reviewed.
MixParameters applyProcessingPlan (const MixParameters& base, const ProcessingPlan&, const RoutingGraph&);

} // namespace livemix
