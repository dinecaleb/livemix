#include "ProcessingPlan.h"
#include "Tune/TuneTypes.h"
#include <cmath>

namespace livemix
{

const char* resolutionStatusId (ResolutionStatus s) noexcept
{
    switch (s)
    {
        case ResolutionStatus::Exact:        return "exact";
        case ResolutionStatus::Approximated: return "approximated";
        case ResolutionStatus::Substituted:  return "substituted";
        case ResolutionStatus::Unsupported:
        default:                             return "unsupported";
    }
}

const char* resolutionStatusLabel (ResolutionStatus s) noexcept
{
    switch (s)
    {
        case ResolutionStatus::Exact:        return "EXACT";
        case ResolutionStatus::Approximated: return "APPROXIMATED";
        case ResolutionStatus::Substituted:  return "SUBSTITUTED";
        case ResolutionStatus::Unsupported:
        default:                             return "NOT AVAILABLE";
    }
}

const char* mixActionStatusId (MixActionStatus s) noexcept
{
    switch (s)
    {
        case MixActionStatus::Accepted: return "accepted";
        case MixActionStatus::Clamped:  return "clamped";
        case MixActionStatus::Rejected: return "rejected";
        case MixActionStatus::Unchanged: return "unchanged";
        case MixActionStatus::Proposed:
        default:                        return "proposed";
    }
}

json::Value MixAction::toJson() const
{
    auto v = json::Value::object();
    v.set ("target", target.id());
    v.set ("processor", dspProcessorId (processor));
    v.set ("parameter", paramId);
    v.set ("value", value);
    v.set ("previousValue", previousValue);
    v.set ("objective", mixObjectiveId (objective));
    v.set ("resolution", resolutionStatusId (resolution));
    v.set ("status", mixActionStatusId (status));
    v.set ("confidence", confidenceName (confidence));
    if (! reason.empty()) v.set ("reason", reason);
    if (! note.empty()) v.set ("note", note);
    return v;
}

int ProcessingPlan::countApplied() const noexcept
{
    int n = 0;
    for (const auto& a : actions) if (a.applies()) ++n;
    return n;
}

int ProcessingPlan::countRejected() const noexcept
{
    int n = 0;
    for (const auto& a : actions) if (a.status == MixActionStatus::Rejected) ++n;
    return n;
}

int ProcessingPlan::countUnchanged() const noexcept
{
    int n = 0;
    for (const auto& a : actions) if (a.status == MixActionStatus::Unchanged) ++n;
    return n;
}

int ProcessingPlan::countClamped() const noexcept
{
    int n = 0;
    for (const auto& a : actions) if (a.status == MixActionStatus::Clamped) ++n;
    return n;
}

json::Value ProcessingPlan::toJson() const
{
    auto v = json::Value::object();
    v.set ("schema", "dlive.processingPlan");
    v.set ("schemaVersion", schemaVersion);
    v.set ("valid", valid);
    v.set ("summary", summary);
    auto as = json::Value::array();
    for (const auto& a : actions) as.add (a.toJson());
    v.set ("actions", std::move (as));
    if (! unsupported.empty())
    {
        auto us = json::Value::array();
        for (const auto& u : unsupported) us.add (u);
        v.set ("unsupported", std::move (us));
    }
    return v;
}

namespace
{
    void applyFxChange (FxParameters& p, const std::string& paramId, float value)
    {
        forEachFxField (p, [&] (auto idFn, auto& v)
        {
            if (idFn() != paramId) return;
            using T = std::remove_reference_t<decltype (v)>;
            if constexpr (std::is_same_v<T, bool>) v = value >= 0.5f;
            else if constexpr (std::is_same_v<T, int>) v = int (std::lround (value));
            else v = value;
        });
    }
}

MixParameters applyProcessingPlan (const MixParameters& base, const ProcessingPlan& plan, const RoutingGraph& graph)
{
    MixParameters out = base;
    for (const auto& a : plan.actions)
    {
        if (! a.applies()) continue;
        const int i = a.target.index;
        switch (a.target.kind)
        {
            case MixTargetKind::Strip:
            {
                if (i < 0 || i >= out.numStrips) continue;
                auto& strip = out.strips[size_t (i)];
                if (a.processor == DspProcessor::Fader) { strip.faderDb = a.value; break; }
                if (a.processor == DspProcessor::Pan) { strip.pan = a.value; break; }
                if (a.processor == DspProcessor::InputGain) { strip.inputGainDb = a.value; break; }
                if (a.processor == DspProcessor::Send)
                {
                    for (int f = 0; f < int (FxSlot::Count); ++f)
                        if (a.paramId == fxSlotName (FxSlot (f)) && graph.fxUsed[size_t (f)])
                            strip.sendDb[size_t (f)] = a.value;
                    break;
                }
                strip.channel = applyChanges (strip.channel, { { a.paramId, a.value } });
                break;
            }
            case MixTargetKind::Bus:
            {
                if (i < 0 || i >= int (MixBus::Count)) continue;
                auto& bus = out.buses[size_t (i)];
                if (a.processor == DspProcessor::Fader) { bus.faderDb = a.value; break; }
                bus.channel = applyChanges (bus.channel, { { a.paramId, a.value } });
                break;
            }
            case MixTargetKind::FxSlot:
            {
                if (i < 0 || i >= int (FxSlot::Count) || ! graph.fxUsed[size_t (i)]) continue;
                auto& slot = out.fx[size_t (i)];
                if (a.processor == DspProcessor::FxReturn) { slot.returnDb = a.value; break; }
                applyFxChange (slot.fx, a.paramId, a.value);
                break;
            }
            case MixTargetKind::Count:
            default: break;
        }
    }
    return out;
}

} // namespace livemix
