#include "TestFramework.h"
#include "Tune/TuneEngine.h"
#include "Tune/SourceStrategy.h"
#include "Profiles/StyleProfile.h"
#include "State/ParameterIDs.h"
#include <cmath>

using namespace livemix;

namespace
{
    // A capture that sits exactly on the profile targets for the role: nothing to fix.
    AnalysisResult onTarget (ChannelRole role, StyleProfileId profile = StyleProfileId::ModernGospel)
    {
        const auto t = StyleProfile::targets (role, profile);
        AnalysisResult a;
        a.valid = true;
        a.peakDb = 0.5f * (t.capturePeakMinDb + t.capturePeakMaxDb);
        a.hitLevelDb = a.peakDb - 2.0f;
        a.crestFactorDb = 0.5f * (t.crestFactorMinDb + t.crestFactorMaxDb);
        a.rmsDb = a.peakDb - a.crestFactorDb;
        a.noiseFloorDb = a.hitLevelDb - 45.0f;
        a.dynamicRangeDb = 45.0f;
        a.silencePercent = 20.0f;
        a.transientCount = 20; a.transientsPerSecond = 2.0f; a.meanTransientRiseDb = 22.0f;
        a.meanDecayMs = 250.0f; a.decayCount = 18;
        a.bandEnergyDb = t.bandTargetDb;
        a.bleedEstimate = 0.05f;
        a.fundamentalHz = 0.5f * (t.fundamentalMinHz + t.fundamentalMaxHz);
        a.fundamentalLevelDb = -6.0f;
        return a;
    }

    TuneContext context (ChannelRole role, const AnalysisResult& a, StyleProfileId profile = StyleProfileId::ModernGospel)
    {
        TuneContext c;
        c.analysis = a;
        c.role = role;
        c.profile = profile;
        c.current = StyleProfile::baseline (role, profile);
        return c;
    }

    const Recommendation* find (const TuneResult& r, TuneSection s, const char* needle = nullptr)
    {
        for (const auto& i : r.report.items)
            if (i.section == s && (needle == nullptr || i.what.find (needle) != std::string::npos)) return &i;
        return nullptr;
    }
    float changeValue (const Recommendation& r, const std::string& id, float fallback = -999.0f)
    {
        for (const auto& c : r.changes) if (c.paramId == id) return c.value;
        return fallback;
    }
}

TEST_CASE ("Tune is idempotent: re-tuning an already tuned source yields NO CHANGE REQUIRED")
{
    for (auto role : { ChannelRole::KickIn, ChannelRole::SnareTop, ChannelRole::RackTom, ChannelRole::Overhead, ChannelRole::HiHat, ChannelRole::Room, ChannelRole::DrumBus })
    {
        auto a = onTarget (role);
        a.bleedEstimate = 0.5f; a.noiseFloorDb = a.hitLevelDb - 25.0f;   // some bleed so close-mic gates get fitted
        a.crestFactorDb = StyleProfile::targets (role, StyleProfileId::ModernGospel).crestFactorMaxDb + 4.0f;
        auto ctx = context (role, a);
        auto first = TuneEngine::tune (ctx);
        REQUIRE (first.valid);
        CHECK (first.report.inputHealth == "Healthy");
        ctx.current = first.proposed;
        auto second = TuneEngine::tune (ctx);
        REQUIRE (second.valid);
        if (! second.noChangeRequired)
            for (const auto& i : second.report.items)
                if (! i.changes.empty()) testfw::reportFailure (__FILE__, __LINE__, std::string (channelRoleName (role)) + " moved again: " + i.what);
        CHECK (second.parametersChanged == 0);
        CHECK (second.headline.find ("NO CHANGE REQUIRED") != std::string::npos);
        CHECK (diffParameters (second.before, second.proposed).empty());
    }
}

TEST_CASE ("Tune: proposed parameters equal before + every reported change, and are within spec bounds")
{
    auto a = onTarget (ChannelRole::SnareTop);
    a.bandEnergyDb[size_t (Band::LowMid)] += 8.0f;     // boxy
    a.crestFactorDb = 27.0f;                            // wild dynamics
    a.bleedEstimate = 0.7f; a.noiseFloorDb = a.hitLevelDb - 20.0f;
    a.resonances = { { 4000.0f, 9.0f }, { 630.0f, 8.0f } };
    auto r = TuneEngine::tune (context (ChannelRole::SnareTop, a));
    REQUIRE (r.valid);
    CHECK (! r.noChangeRequired);
    std::vector<ParameterChange> all;
    for (const auto& i : r.report.items) all.insert (all.end(), i.changes.begin(), i.changes.end());
    auto rebuilt = applyChanges (r.before, all);
    CHECK (diffParameters (rebuilt, r.proposed).empty());
    CHECK (r.parametersChanged == int (diffParameters (r.before, r.proposed).size()));
    for (const auto& i : r.report.items)
    {
        CHECK (i.section != TuneSection::Notes || i.changes.empty());
        for (const auto& c : i.changes)
        {
            CHECK (c.paramId != ParamID::inputTrim);   // never touches capture gain
            CHECK (std::isfinite (c.value));
        }
    }
}

TEST_CASE ("Tune (kick): fundamental drives the high-pass and body shelf; sub excess raises the HPF instead of EQ")
{
    auto a = onTarget (ChannelRole::KickIn);
    a.fundamentalHz = 76.0f;                     // a high-tuned kick: the template (28 Hz HPF, 55 Hz shelf) is wrong for it
    a.bandEnergyDb[size_t (Band::Low)] -= 6.0f; // thin
    auto kctx = context (ChannelRole::KickIn, a);
    auto r = TuneEngine::tune (kctx);
    const auto* hpf = find (r, TuneSection::Tone, "High-pass");
    REQUIRE (hpf != nullptr);
    CHECK_NEAR (changeValue (*hpf, ParamID::hpfFreq), 38.0f, 1.5f); // half the fundamental
    const auto* body = find (r, TuneSection::Tone, "Added body");
    REQUIRE (body != nullptr);
    const float shelfHz = changeValue (*body, eqBandId ("toneEq", 0, "Freq"));
    CHECK (shelfHz >= 82.0f && shelfHz <= 92.0f);       // just above 76 Hz
    const float shelfGain = changeValue (*body, eqBandId ("toneEq", 0, "Gain"), kctx.current.toneBands[0].gainDb);
    CHECK (shelfGain > 0.0f && shelfGain <= StyleProfile::targets (ChannelRole::KickIn, StyleProfileId::ModernGospel).maxEqBoostDb + 0.01f);

    auto b = onTarget (ChannelRole::KickIn);
    b.fundamentalHz = 60.0f;
    b.bandEnergyDb[size_t (Band::Sub)] += 9.0f; // rumble
    auto rb = TuneEngine::tune (context (ChannelRole::KickIn, b));
    const auto* hpf2 = find (rb, TuneSection::Tone, "High-pass");
    REQUIRE (hpf2 != nullptr);
    CHECK (changeValue (*hpf2, ParamID::hpfFreq) > 28.0f);
    CHECK (changeValue (*hpf2, ParamID::hpfFreq) <= 45.0f); // bounded by the profile
    CHECK (find (rb, TuneSection::Tone, "Added body") == nullptr);
}

TEST_CASE ("Tune (snare): harshness is cut, never boosted around; ring is notched narrowly")
{
    auto a = onTarget (ChannelRole::SnareTop);
    a.bandEnergyDb[size_t (Band::Presence)] += 7.0f;
    a.resonances = { { 800.0f, 11.0f } };
    auto r = TuneEngine::tune (context (ChannelRole::SnareTop, a));
    const auto* harsh = find (r, TuneSection::Tone, "presence");
    const auto* eased = find (r, TuneSection::Attack, "attack");
    CHECK (harsh != nullptr || eased != nullptr);
    for (const auto& i : r.report.items)
        for (const auto& c : i.changes)
            if (c.paramId.find ("Gain") != std::string::npos && (c.paramId.rfind ("corrEq", 0) == 0))
                CHECK (c.value <= 0.0f); // corrective bands only ever cut
    const auto* ring = find (r, TuneSection::Tone, "Notched ring");
    REQUIRE (ring != nullptr);
    CHECK_NEAR (changeValue (*ring, eqBandId ("corrEq", 1, "Freq")), 800.0f, 1.0f);
    CHECK (changeValue (*ring, eqBandId ("corrEq", 1, "Q")) >= 4.0f);
    CHECK (changeValue (*ring, eqBandId ("corrEq", 1, "Gain")) >= -6.0f);
}

TEST_CASE ("Tune (dynamics): compressor threshold follows the measured hit level; dense sources are left alone")
{
    auto a = onTarget (ChannelRole::RackTom);
    a.crestFactorDb = 26.0f; a.hitLevelDb = -20.0f; a.noiseFloorDb = -65.0f; a.transientsPerSecond = 4.0f;
    auto ctx = context (ChannelRole::RackTom, a);
    ctx.current.inputTrimDb = 3.0f;
    auto r = TuneEngine::tune (ctx);
    const auto* comp = find (r, TuneSection::Dynamics, "Compression");
    REQUIRE (comp != nullptr);
    const float thr = changeValue (*comp, ParamID::compThreshold);
    const float ratio = changeValue (*comp, ParamID::compRatio, ctx.current.compRatio);
    // Hit at -17 dBFS after trim; threshold sits below it by GR * R/(R-1) with GR ~3.5-4.5 dB.
    CHECK (thr < -17.0f);
    CHECK (thr > -17.0f - 8.0f);
    const auto t = StyleProfile::targets (ChannelRole::RackTom, StyleProfileId::ModernGospel);
    CHECK (ratio >= t.compRatioMin - 0.01f && ratio <= t.compRatioMax + 0.01f);
    CHECK (changeValue (*comp, ParamID::compScHpf, ctx.current.compScHpfHz) >= 20.0f);

    auto dense = onTarget (ChannelRole::RackTom);
    dense.crestFactorDb = 6.0f;
    auto rd = TuneEngine::tune (context (ChannelRole::RackTom, dense));
    const auto* byp = find (rd, TuneSection::Dynamics, "bypassed");
    REQUIRE (byp != nullptr);
    CHECK (rd.proposed.compEnabled == false);
}

TEST_CASE ("Tune (bleed): gate follows bleed and decay on close mics; overheads never get a gate")
{
    auto a = onTarget (ChannelRole::FloorTom);
    a.bleedEstimate = 0.75f; a.noiseFloorDb = a.hitLevelDb - 18.0f; a.meanDecayMs = 180.0f; a.fundamentalHz = 80.0f;
    auto r = TuneEngine::tune (context (ChannelRole::FloorTom, a));
    const auto* gate = find (r, TuneSection::Bleed, "Expander");
    REQUIRE (gate != nullptr);
    CHECK (gate->confidence == Confidence::High);
    const float thr = changeValue (*gate, ParamID::gateThreshold);
    CHECK (thr <= a.hitLevelDb - 12.0f);
    CHECK (thr >= a.noiseFloorDb);
    CHECK (changeValue (*gate, ParamID::gateRange) <= 30.0f);
    CHECK_NEAR (changeValue (*gate, ParamID::gateHold), 108.0f, 1.0f);
    CHECK (changeValue (*gate, ParamID::gateScHpf) >= 56.0f); // 0.7 x fundamental or profile minimum

    auto quiet = onTarget (ChannelRole::FloorTom);
    quiet.bleedEstimate = 0.05f;
    auto rq = TuneEngine::tune (context (ChannelRole::FloorTom, quiet));
    CHECK (find (rq, TuneSection::Bleed, "bypassed") != nullptr);
    CHECK (! rq.proposed.gateEnabled);

    auto oh = onTarget (ChannelRole::OverheadLeft);
    oh.bleedEstimate = 0.8f; oh.noiseFloorDb = oh.hitLevelDb - 15.0f;
    auto ro = TuneEngine::tune (context (ChannelRole::OverheadLeft, oh));
    CHECK (! ro.proposed.gateEnabled);
    CHECK (! ro.proposed.transientEnabled);
}

TEST_CASE ("Tune (overheads): low spill raises the high-pass within the profile range; harsh peak is cut")
{
    auto a = onTarget (ChannelRole::Overhead);
    a.numChannels = 2;
    a.bandEnergyDb[size_t (Band::Low)] += 8.0f;
    a.resonances = { { 5000.0f, 9.0f } };
    a.stereoBalanceDb = 4.0f;
    auto r = TuneEngine::tune (context (ChannelRole::Overhead, a));
    const auto* hpf = find (r, TuneSection::Tone, "High-pass");
    REQUIRE (hpf != nullptr);
    const float hz = changeValue (*hpf, ParamID::hpfFreq);
    CHECK (hz > 180.0f && hz <= 300.0f);
    const auto* harsh = find (r, TuneSection::Tone, "harshness");
    REQUIRE (harsh != nullptr);
    CHECK (changeValue (*harsh, eqBandId ("corrEq", 2, "Gain")) < 0.0f);
    CHECK (find (r, TuneSection::Notes, "Stereo balance") != nullptr);
}

TEST_CASE ("Tune (input): preamp advice is bounded, refers to the preamp, and never writes plugin parameters")
{
    auto a = onTarget (ChannelRole::KickIn);
    a.peakDb = -40.0f; a.hitLevelDb = -42.0f;
    auto r = TuneEngine::tune (context (ChannelRole::KickIn, a));
    CHECK (r.report.inputHealth == "Low");
    const auto* in = find (r, TuneSection::Input, "preamp");
    REQUIRE (in != nullptr);
    CHECK (in->changes.empty());
    CHECK (r.report.suggestedCaptureGainDb <= 10.0f && r.report.suggestedCaptureGainDb > 0.0f);
    CHECK (in->why.find ("preamp") != std::string::npos);

    auto hot = onTarget (ChannelRole::KickIn);
    hot.peakDb = -0.2f; hot.clipCount = 12;
    auto rh = TuneEngine::tune (context (ChannelRole::KickIn, hot));
    CHECK (rh.report.inputHealth == "Clipping");
    CHECK (rh.report.suggestedCaptureGainDb <= -3.0f && rh.report.suggestedCaptureGainDb >= -10.0f);

    AnalysisResult none; none.valid = true; none.silencePercent = 100.0f; none.peakDb = -120.0f;
    auto rn = TuneEngine::tune (context (ChannelRole::KickIn, none));
    CHECK (rn.valid);
    CHECK (rn.noChangeRequired);
    CHECK (rn.headline.find ("NO SIGNAL") != std::string::npos);
}

TEST_CASE ("Tune: sections summarise the decisions; every family has a strategy and profile data")
{
    for (int f = 0; f < int (RoleFamily::Count); ++f)
    {
        CHECK (strategyFor (RoleFamily (f)).name()[0] != '\0');
        for (int p = 0; p < int (StyleProfileId::Count); ++p)
        {
            const auto& t = Profiles::targets (StyleProfileId (p), RoleFamily (f));
            CHECK (t.intent[0] != '\0');
            CHECK (t.hpfMaxHz >= t.hpfMinHz);
            CHECK (t.compRatioMax >= t.compRatioMin);
            CHECK (t.crestFactorMaxDb > t.crestFactorMinDb);
            CHECK (t.maxEqCutDb > 0.0f && t.maxEqBoostDb > 0.0f);
        }
    }
    auto a = onTarget (ChannelRole::SnareTop);
    a.bandEnergyDb[size_t (Band::LowMid)] += 9.0f;
    auto r = TuneEngine::tune (context (ChannelRole::SnareTop, a));
    const auto& tone = r.sections[size_t (TuneSection::Tone)];
    CHECK (tone.changed);
    CHECK (! tone.summary.empty());
    CHECK (find (r, TuneSection::Tone, "low-mid") != nullptr);
    CHECK (r.sections[size_t (TuneSection::Bleed)].summary.find ("bypassed") != std::string::npos); // template gate, no bleed measured
    CHECK (r.sections[size_t (TuneSection::Input)].summary.find ("Healthy") != std::string::npos);
    CHECK (r.headline == "SNARE TOP TUNED");
}

TEST_CASE ("Profiles: Modern Worship is a documented variant of Modern Gospel, not a copy")
{
    const auto& g = Profiles::definition (StyleProfileId::ModernGospel);
    const auto& w = Profiles::definition (StyleProfileId::ModernWorship);
    CHECK (std::string (g.name) == "Modern Gospel");
    CHECK (w.targets[int (RoleFamily::Kick)].crestFactorMaxDb > g.targets[int (RoleFamily::Kick)].crestFactorMaxDb);
    CHECK (w.baselines[int (RoleFamily::Kick)].compRatio < g.baselines[int (RoleFamily::Kick)].compRatio);
    CHECK (g.baselines[int (RoleFamily::Kick)].compScHpfHz >= 20.0f);
    CHECK (g.baselines[int (RoleFamily::Tom)].gateScHpfHz >= 20.0f);
    CHECK (! g.baselines[int (RoleFamily::Overhead)].gateEnabled);
    CHECK (StyleProfile::baseline (ChannelRole::FloorTom, StyleProfileId::ModernGospel).hpfHz < StyleProfile::baseline (ChannelRole::RackTom, StyleProfileId::ModernGospel).hpfHz);
}
