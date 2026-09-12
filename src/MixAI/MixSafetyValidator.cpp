#include "MixSafetyValidator.h"
#include "Profiles/MixProfileData.h"
#include "State/ParameterIDs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace livemix
{

namespace
{
    std::string num (const char* fmt, double v) { char b[96]; std::snprintf (b, sizeof (b), fmt, v); return b; }

    bool isEqGain (const std::string& id)
    {
        return id.size() > 4 && id.compare (id.size() - 4, 4, "Gain") == 0
            && (id.rfind ("toneEq", 0) == 0 || id.rfind ("corrEq", 0) == 0);
    }

    // "toneEq2Gain" -> "toneEq2": the band an action belongs to, so a refused gain takes the
    // rest of its band with it instead of leaving a band switched on doing nothing.
    std::string bandKey (const std::string& id)
    {
        if (id.rfind ("toneEq", 0) != 0 && id.rfind ("corrEq", 0) != 0) return {};
        size_t i = 6;
        if (i >= id.size() || ! std::isdigit (static_cast<unsigned char> (id[i]))) return {};
        while (i < id.size() && std::isdigit (static_cast<unsigned char> (id[i]))) ++i;
        return id.substr (0, i);
    }

    bool sustainedFamily (const std::string& family)
    {
        return family == "Organ" || family == "Synth" || family == "Piano" || family == "ElectricPiano"
            || family == "Choir" || family == "Room" || family == "Overhead" || family == "AcousticGuitar";
    }

    const MixContextTrack* trackFor (const MixContext* mix, const MixTargetRef& t)
    {
        if (mix == nullptr || t.kind != MixTargetKind::Strip) return nullptr;
        for (const auto& track : mix->tracks) if (track.id == t.index) return &track;
        return nullptr;
    }
}

namespace MixSafetyValidator
{

ProcessingPlan validate (const ProcessingPlan& in, const Context& ctx, Report* reportOut)
{
    ProcessingPlan out = in;
    Report report;
    if (! ctx.valid())
    {
        out.valid = false;
        report.notes.push_back ("The plan could not be checked, so none of it was applied.");
        for (auto& a : out.actions) { a.status = MixActionStatus::Rejected; a.note = "Not checked."; ++report.rejected; }
        if (reportOut != nullptr) *reportOut = report;
        return out;
    }

    const auto& B = MixProfile::aiBounds();
    const auto* caps = ctx.registry;

    auto reject = [&] (MixAction& a, std::string why)
    {
        a.status = MixActionStatus::Rejected;
        a.note = why;
        ++report.rejected;
        report.notes.push_back (a.target.id() + " / " + a.paramId + ": " + std::move (why));
    };

    int accepted = 0;
    for (auto& a : out.actions)
    {
        if (accepted >= B.maxActions)
        {
            reject (a, "This plan asks for more changes than one Tune is allowed to make.");
            continue;
        }

        const auto* target = caps->find (a.target);
        if (target == nullptr) { reject (a, "That target is not part of this mix."); continue; }

        const auto* processor = target->find (a.processor);
        if (processor == nullptr || ! processor->available)
        {
            reject (a, processor != nullptr && ! processor->unavailableBecause.empty()
                           ? processor->unavailableBecause
                           : std::string ("DLIVE does not have a ") + dspProcessorLabel (a.processor) + " on " + target->name + ".");
            continue;
        }

        const auto* spec = processor->find (a.paramId);
        if (spec == nullptr) { reject (a, "There is no such control on that processor."); continue; }

        if (! std::isfinite (a.value) || ! std::isfinite (a.previousValue))
        {
            reject (a, "The value was not a finite number.");
            continue;
        }

        // ---- Things the reasoning layer is never allowed to do at all ----
        if (a.processor == DspProcessor::InputGain && ! B.allowInputGain)
        {
            reject (a, "Capture gain belongs to the console preamp and to gain staging, not to a mix decision. "
                       "DLIVE says what the preamp should do instead.");
            continue;
        }
        if (a.processor == DspProcessor::Gate)
        {
            const auto* track = trackFor (ctx.mix, a.target);
            const bool sustained = a.target.kind == MixTargetKind::Bus
                                || (track != nullptr && sustainedFamily (track->family));
            if (sustained && a.paramId == ParamID::gateOn && a.value >= 0.5f)
            {
                reject (a, "A sustained source would be chopped by an expander, so DLIVE does not gate it.");
                continue;
            }
        }

        float value = a.value;
        bool clamped = false;
        auto limit = [&] (float lo, float hi)
        {
            const float v = std::clamp (value, lo, hi);
            if (v != value) { clamped = true; value = v; }
        };

        // ---- The registry's own range: the engine's truth about this control ----
        limit (spec->minValue, spec->maxValue);
        if (spec->type != ParameterSpec::Type::Float) value = std::round (value);

        // ---- Bounds on how far one Tune may travel from the deterministic plan ----
        if (isEqGain (a.paramId))
        {
            limit (B.minEqGainDb, B.maxEqGainDb);
            limit (a.previousValue - B.maxEqDeltaDb, a.previousValue + B.maxEqDeltaDb);
        }
        else if (a.paramId == "faderDb")
            limit (a.previousValue - B.maxFaderMoveDb, a.previousValue + B.maxFaderMoveDb);
        else if (a.processor == DspProcessor::Send || a.processor == DspProcessor::FxReturn)
            limit (a.previousValue <= kSilenceDb + 1.0f ? value : a.previousValue - B.maxSendMoveDb,
                   a.previousValue <= kSilenceDb + 1.0f ? value : a.previousValue + B.maxSendMoveDb);
        else if (a.paramId == "pan")
            limit (a.previousValue - B.maxPanMove, a.previousValue + B.maxPanMove);
        else if (a.paramId == ParamID::widthAmount)
            limit (a.previousValue - B.maxWidthDelta, a.previousValue + B.maxWidthDelta);
        else if (a.paramId == ParamID::compThreshold)
            limit (a.previousValue - B.maxCompThresholdMoveDb, a.previousValue + B.maxCompThresholdMoveDb);
        else if (a.paramId == ParamID::compRatio)
            limit (1.0f, B.maxCompRatio);
        else if (a.paramId == ParamID::gateRange)
            limit (0.0f, B.maxGateRangeDb);
        else if (a.paramId == ParamID::satDrive)
            limit (0.0f, B.maxSatDrive);
        else if (a.paramId == ParamID::deEssRange)
            limit (0.0f, B.maxDeEssRangeDb);
        else if (a.paramId == ParamID::limiterCeiling)
            limit (B.masterCeilingMinDb, B.masterCeilingMaxDb);

        // ---- The master keeps room under its ceiling ----
        // A mix that arrives at the limiter with nothing left is not finished, it is squashed,
        // and the loudness a broadcast needs is the master's decision, not a channel's.
        if (a.target.isMaster() && a.paramId == "faderDb" && ctx.mix != nullptr)
        {
            const float ceiling = ctx.baseline->master().channel.limiterEnabled
                                      ? ctx.baseline->master().channel.limiterCeilingDb : 0.0f;
            const float measured = ctx.mix->master.truePeakDb;
            if (measured > -119.0f)
            {
                const float allowed = a.previousValue + (ceiling - B.minMasterHeadroomDb - measured);
                if (value > allowed)
                {
                    value = std::max (a.previousValue, allowed);
                    clamped = true;
                }
            }
        }

        if (value == a.previousValue && a.paramId != ParamID::toneEqOn && a.paramId != ParamID::corrEqOn)
        {
            // The bounds left nothing to do. Recorded, but not as a refusal: nothing was
            // declined and nothing changed, so there is nothing for a user to read about it.
            a.status = MixActionStatus::Unchanged;
            a.note = "The mix bounds left no move to make here.";
            continue;
        }

        a.value = value;
        a.status = clamped ? MixActionStatus::Clamped : MixActionStatus::Accepted;
        if (clamped)
        {
            ++report.clamped;
            if (a.note.empty()) a.note = "Held to " + num ("%.2f", double (value)) + " by DLIVE's mix bounds.";
        }
        ++accepted;
    }

    // ---- A band whose gain was refused does not get switched on ----
    for (const auto& a : out.actions)
    {
        if (a.status != MixActionStatus::Rejected || ! isEqGain (a.paramId)) continue;
        const auto key = bandKey (a.paramId);
        if (key.empty()) continue;
        for (auto& other : out.actions)
        {
            if (&other == &a || other.status == MixActionStatus::Rejected) continue;
            if (! (other.target == a.target) || bandKey (other.paramId) != key) continue;
            other.status = MixActionStatus::Rejected;
            other.note = "The gain this band was going to carry was refused, so the band stays as it was.";
            ++report.rejected;
            --accepted;
        }
    }

    for (const auto& a : out.actions) if (a.applies()) ++report.accepted;
    report.accepted -= report.clamped;
    if (report.accepted < 0) report.accepted = 0;

    out.valid = true;
    if (reportOut != nullptr) *reportOut = report;
    return out;
}

} // namespace MixSafetyValidator
} // namespace livemix
