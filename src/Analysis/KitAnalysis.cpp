#include "KitAnalysis.h"
#include "Profiles/StyleProfile.h"
#include "Core/DbUtils.h"
#include <cmath>
#include <cstdio>

namespace livemix
{

namespace
{
    std::string db1 (float v) { char b[32]; std::snprintf (b, sizeof (b), "%+.0f dB", double (std::round (v))); return b; }

    // Level of the processed channel as it enters the mix.
    float mixLevelDb (const KitMember& m)
    {
        if (m.output.valid && m.output.peakDb > -60.0f) return m.output.peakDb;
        return m.analysis.hitLevelDb + m.outputTrimDb;
    }

    bool isCloseMic (RoleFamily f) { return f == RoleFamily::Kick || f == RoleFamily::Snare || f == RoleFamily::Tom; }
}

namespace KitAnalysis
{

KitRecommendationResult analyze (const std::vector<KitMember>& members, StyleProfileId style)
{
    KitRecommendationResult r;
    std::vector<const KitMember*> valid;
    for (const auto& m : members)
        if (m.analysis.valid && m.recommendations.valid) valid.push_back (&m);
    r.membersAnalyzed = int (valid.size());
    if (valid.empty()) return r;
    r.valid = true;

    // ---- Capture ----
    for (const auto* m : valid)
    {
        KitRecommendationResult::CaptureRow row { m->instanceId, m->name, m->recommendations.inputHealth, 0.0f };
        for (const auto& item : m->recommendations.items)
            if (item.kind == Recommendation::Kind::CaptureGain) row.captureGainDb = m->recommendations.suggestedCaptureGainDb;
        r.capture.push_back (row);
    }

    // ---- Processing highlights ----
    for (const auto* m : valid)
        for (const auto& item : m->recommendations.items)
        {
            const bool notable = item.kind == Recommendation::Kind::EQ || item.kind == Recommendation::Kind::Gate
                              || item.kind == Recommendation::Kind::Compression || item.kind == Recommendation::Kind::Transient;
            if (notable && item.confidence != Confidence::Low && ! item.changes.empty())
                r.processing.push_back ({ m->instanceId, m->name, item.what, item.why, item.confidence });
        }

    // ---- Balance ----
    // Reference: the louder of kick / snare (the backbone of the kit). Fall back to the loudest close mic.
    float referenceDb = -120.0f;
    bool haveReference = false;
    for (const auto* m : valid)
    {
        const auto f = roleFamily (m->role);
        if (f == RoleFamily::Kick || f == RoleFamily::Snare)
        {
            referenceDb = std::max (referenceDb, mixLevelDb (*m));
            haveReference = true;
        }
    }
    if (! haveReference)
        for (const auto* m : valid)
            if (isCloseMic (roleFamily (m->role))) { referenceDb = std::max (referenceDb, mixLevelDb (*m)); haveReference = true; }

    if (haveReference && referenceDb > -60.0f)
    {
        float closeSum = 0.0f; int closeCount = 0;
        float ohSum = 0.0f; int ohCount = 0;
        const KitMember* kick = nullptr; const KitMember* snare = nullptr;

        for (const auto* m : valid)
        {
            const auto f = roleFamily (m->role);
            if (f == RoleFamily::Bus) continue;
            const RoleTargets t = StyleProfile::targets (m->role, style);
            const float level = mixLevelDb (*m);
            if (level <= -60.0f) continue;
            const float target = referenceDb + t.kitBalanceRelDb;
            const float delta = target - level;

            if (isCloseMic (f)) { closeSum += level; ++closeCount; }
            if (f == RoleFamily::Overhead) { ohSum += level; ++ohCount; }
            if (f == RoleFamily::Kick && (kick == nullptr || level > mixLevelDb (*kick))) kick = m;
            if (f == RoleFamily::Snare && (snare == nullptr || level > mixLevelDb (*snare))) snare = m;

            if (std::fabs (delta) >= 2.5f)
            {
                KitRecommendationResult::BalanceItem b;
                b.instanceId = m->instanceId;
                b.name = m->name;
                b.outputTrimDeltaDb = clamp (std::round (delta), -8.0f, 8.0f);
                b.what = m->name + " is approximately " + db1 (-delta) + " " + (delta > 0 ? "below" : "above") + " target kit balance";
                b.why = "Measured at " + db1 (level) + "FS peak against a kick/snare reference of " + db1 (referenceDb)
                      + "FS; this role usually sits " + db1 (t.kitBalanceRelDb) + " relative to it. Suggested mix trim " + db1 (b.outputTrimDeltaDb) + ".";
                b.confidence = std::fabs (delta) >= 5.0f ? Confidence::High : Confidence::Medium;
                r.balance.push_back (b);
            }
        }

        if (kick != nullptr && snare != nullptr)
        {
            const float diff = mixLevelDb (*kick) - mixLevelDb (*snare);
            if (std::fabs (diff) <= 3.0f) r.notes.push_back ("Kick/snare relationship is healthy.");
            else r.notes.push_back (std::string ("Kick is ") + db1 (diff) + " relative to snare; they usually sit within 3 dB of each other.");
        }
        if (closeCount > 0 && ohCount > 0)
        {
            const float closeAvg = closeSum / float (closeCount), ohAvg = ohSum / float (ohCount);
            if (ohAvg > closeAvg - 3.0f) r.notes.push_back ("Overheads dominate the close microphones (" + db1 (ohAvg - closeAvg) + " relative to the close-mic average).");
            else if (ohAvg < closeAvg - 14.0f) r.notes.push_back ("Overheads are very low relative to the close microphones; cymbals may lack presence.");
        }
    }
    if (r.balance.empty() && r.notes.empty()) r.notes.push_back ("Kit balance is within the profile targets.");
    return r;
}

} // namespace KitAnalysis
} // namespace livemix
