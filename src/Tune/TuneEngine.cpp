#include "TuneEngine.h"
#include "SourceStrategy.h"
#include "Profiles/Profile.h"
#include "Intelligence/SafetyValidator.h"
#include <cctype>

namespace livemix
{

namespace
{
    std::string upper (std::string s)
    {
        for (auto& c : s) c = char (std::toupper (static_cast<unsigned char> (c)));
        return s;
    }
}

namespace TuneEngine
{

TuneResult tune (const TuneContext& ctx)
{
    TuneResult result;
    result.before = ctx.current;
    result.proposed = ctx.current;
    if (! ctx.analysis.valid) return result;

    const RoleFamily family = roleFamily (ctx.role);
    const SourceTargets targets = ctx.targetsOverride != nullptr ? *ctx.targetsOverride
                                                                  : Profiles::targets (ctx.profile, ctx.role);
    const SourceStrategy& strategy = strategyFor (family);
    const std::string sourceName = upper (channelRoleName (ctx.role));

    TuneDecisions decisions (ctx.current);
    RecommendationResult report;
    report.valid = true;

    const bool usable = tune::evaluateInput (ctx, targets, decisions, report);
    if (usable)
    {
        strategy.decide (ctx, targets, decisions);
        tune::setMixGain (ctx, targets, decisions);
        tune::stereoBalanceNote (ctx, decisions);
    }

    report.items = decisions.items;
    // Every path is validated: bounds from the parameter table, forbidden ids dropped.
    report = SafetyValidator::validate (report, false);

    std::vector<ParameterChange> all;
    for (const auto& item : report.items)
        all.insert (all.end(), item.changes.begin(), item.changes.end());
    result.proposed = applyChanges (ctx.current, all);
    result.parametersChanged = int (diffParameters (ctx.current, result.proposed).size());

    for (int s = 0; s < int (TuneSection::Count); ++s)
    {
        auto& sum = result.sections[size_t (s)];
        sum.section = TuneSection (s);
        const Recommendation* firstChange = nullptr;
        const Recommendation* firstNote = nullptr;
        for (const auto& item : report.items)
        {
            if (item.section != sum.section) continue;
            ++sum.itemCount;
            if (! item.changes.empty()) { sum.changed = true; if (firstChange == nullptr) firstChange = &item; }
            else if (firstNote == nullptr) firstNote = &item;
        }
        if (firstChange != nullptr) sum.summary = firstChange->what;
        else if (firstNote != nullptr) sum.summary = firstNote->what;
        else if (sum.section == TuneSection::Input) sum.summary = usable ? "Input " + report.inputHealth + (report.inputHealth == "Healthy" ? "" : " (see preamp note)") : "No signal";
        else if (sum.section == TuneSection::Notes) sum.summary.clear();
        else sum.summary = "No change required";
        if (sum.itemCount > 1 && firstChange != nullptr) sum.summary += " (+" + std::to_string (sum.itemCount - 1) + ")";
    }

    result.valid = true;
    result.noChangeRequired = result.parametersChanged == 0;
    if (! usable) result.headline = sourceName + ": NO SIGNAL";
    else if (result.noChangeRequired) result.headline = sourceName + ": NO CHANGE REQUIRED";
    else result.headline = sourceName + " TUNED";

    if (usable && result.noChangeRequired && report.suggestedCaptureGainDb == 0.0f)
    {
        bool hasItem = false;
        for (const auto& i : report.items) if (i.section != TuneSection::Notes) { hasItem = true; break; }
        if (! hasItem)
        {
            Recommendation info;
            info.kind = Recommendation::Kind::Info;
            info.section = TuneSection::Notes;
            info.what = "Capture and processing already sit inside the profile targets.";
            info.why = "Levels, spectral balance, dynamics and bleed are all within tolerance; nothing was changed.";
            info.confidence = Confidence::Medium;
            result.report.items.push_back (info);
        }
    }
    result.report = report;
    return result;
}

} // namespace TuneEngine
} // namespace livemix
