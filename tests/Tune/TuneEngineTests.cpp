#include "TestFramework.h"
#include "Tune/TuneEngine.h"
#include "Tune/SourceStrategy.h"
#include "Profiles/StyleProfile.h"
#include "DSP/SampleBank.h"
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
        c.hasSampleStage = sampleReplacementAppropriate (roleFamily (role));   // a DLIVE drum strip, as MixPlanner builds it
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
    // Template gate, no bleed measured: the gate is bypassed. The Bleed section leads with
    // the trigger a snare microphone is always fitted with, so the gate is looked for among
    // the section's decisions rather than in the one line that summarises them.
    CHECK (! r.sections[size_t (TuneSection::Bleed)].summary.empty());
    CHECK (find (r, TuneSection::Bleed, "bypassed") != nullptr);
    CHECK (! r.proposed.gateEnabled);
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

TEST_CASE ("Tune: the sample trigger is fitted from the listen on the inside, top and hat microphones, never switched on, and never on the outside or bottom ones")
{
    for (auto role : { ChannelRole::KickIn, ChannelRole::SnareTop, ChannelRole::RackTom, ChannelRole::FloorTom })
    {
        auto a = onTarget (role);
        a.bleedEstimate = 0.5f;
        a.noiseFloorDb = a.hitLevelDb - 30.0f;
        a.bleedLevelDb = a.hitLevelDb - 20.0f;
        a.eventLevelDb = a.hitLevelDb + 2.0f;
        a.musicalPeakDb = a.hitLevelDb + 9.0f;
        auto ctx = context (role, a);
        CHECK (! ctx.current.replaceEnabled);
        auto r = TuneEngine::tune (ctx);
        REQUIRE (r.valid);
        const auto* item = find (r, TuneSection::Bleed, "Sample trigger fitted");
        REQUIRE (item != nullptr);
        CHECK (item->kind == Recommendation::Kind::Sample);
        CHECK (item->why.find ("switch it on") != std::string::npos);          // it says the stage is off
        const float threshold = changeValue (*item, "replaceThreshold");
        const float level = changeValue (*item, "replaceGain", ctx.current.replaceGainDb);   // no change recorded when the fit equals the baseline
        const float trim = ctx.current.inputTrimDb;
        // Between the bleed and the hits, never within 6 dB of the bleed, and never far under
        // the microphone's peak (12 dB on a tom, 18 on a kick or snare); the sample's level is that peak.
        const bool tom = roleFamily (role) == RoleFamily::Tom;
        CHECK (threshold > a.bleedLevelDb + trim + 5.9f);
        CHECK (threshold < a.musicalPeakDb + trim);
        CHECK (threshold >= a.musicalPeakDb + trim - (tom ? 12.0f : 18.0f) - 0.01f);
        CHECK_NEAR (level, std::round ((a.musicalPeakDb + trim) * 2.0f) * 0.5f, 0.01f);
        CHECK (! r.proposed.replaceEnabled);                                   // the switch is the engineer's
        CHECK_NEAR (r.proposed.replaceBlend, ctx.current.replaceBlend, 1.0e-6f);
        // The band never opens below the drum's own fundamental.
        CHECK (r.proposed.replaceDetHpfHz <= 0.7f * a.fundamentalHz + 0.5f);
        // And it holds: the same listen fits the same numbers.
        ctx.current = r.proposed;
        auto again = TuneEngine::tune (ctx);
        CHECK (find (again, TuneSection::Bleed, "Sample trigger fitted") == nullptr);
    }
    for (auto role : { ChannelRole::KickOut, ChannelRole::SnareBottom, ChannelRole::Overhead, ChannelRole::Room })
    {
        auto a = onTarget (role);
        a.bleedEstimate = 0.5f;
        a.bleedLevelDb = a.hitLevelDb - 20.0f;
        auto r = TuneEngine::tune (context (role, a));
        REQUIRE (r.valid);
        CHECK (find (r, TuneSection::Bleed, "Sample trigger fitted") == nullptr);
    }

    // The hat carries a sample too, and its detector is the one that never follows a
    // fundamental: what a hi-hat microphone has below 500 Hz is the kick and the snare, so
    // its band stays where the profile put it, well above the kit's bodies.
    {
        auto a = onTarget (ChannelRole::HiHat);
        a.bleedEstimate = 0.5f;
        a.noiseFloorDb = a.hitLevelDb - 30.0f;
        a.bleedLevelDb = a.hitLevelDb - 20.0f;
        a.eventLevelDb = a.hitLevelDb + 2.0f;
        a.musicalPeakDb = a.hitLevelDb + 9.0f;
        auto ctx = context (ChannelRole::HiHat, a);
        auto r = TuneEngine::tune (ctx);
        REQUIRE (r.valid);
        const auto* item = find (r, TuneSection::Bleed, "Sample trigger fitted");
        REQUIRE (item != nullptr);
        CHECK (! r.proposed.replaceEnabled);                                   // the switch is still the engineer's
        const auto& hat = StyleProfile::targets (ChannelRole::HiHat, StyleProfileId::ModernGospel);
        CHECK_NEAR (r.proposed.replaceDetHpfHz, hat.sampleDetHpfHz, 1.0f);
        CHECK (r.proposed.replaceDetHpfHz > 1000.0f);
        ctx.current = r.proposed;
        CHECK (find (TuneEngine::tune (ctx), TuneSection::Bleed, "Sample trigger fitted") == nullptr);
    }
}

TEST_CASE ("Tune: a sampled drum microphone is gated far harder, and the rest of the kit cleans up around the samples")
{
    // The kick with its sample on: the gate closes further, holds less and lets go sooner than on its own.
    auto a = onTarget (ChannelRole::KickIn);
    a.bleedEstimate = 0.5f; a.noiseFloorDb = a.hitLevelDb - 25.0f; a.bleedLevelDb = a.hitLevelDb - 20.0f; a.musicalPeakDb = a.hitLevelDb + 9.0f;
    auto plain = context (ChannelRole::KickIn, a);
    auto sampled = plain;
    sampled.current.replaceEnabled = true;
    sampled.sampled = true;
    sampled.kitSampled = true;
    const auto rPlain = TuneEngine::tune (plain);
    const auto rSampled = TuneEngine::tune (sampled);
    REQUIRE (rPlain.valid && rSampled.valid);
    CHECK (rSampled.proposed.gateEnabled);
    CHECK (rSampled.proposed.gateRangeDb > rPlain.proposed.gateRangeDb + 5.0f);
    CHECK (rSampled.proposed.gateThresholdDb > rPlain.proposed.gateThresholdDb);
    CHECK (rSampled.proposed.gateReleaseMs < rPlain.proposed.gateReleaseMs);
    CHECK (rSampled.proposed.gateRatio >= 8.0f);
    const auto* item = find (rSampled, TuneSection::Bleed, "Gate tightened for the sample");
    REQUIRE (item != nullptr);
    CHECK (item->why.find ("carries this drum's body") != std::string::npos);
    // The sample switch is the engineer's: it is still on, and untouched.
    CHECK (rSampled.proposed.replaceEnabled);
    // The gate never sits above the sample's own trigger: what fires the sample opens the microphone.
    CHECK (rSampled.proposed.gateThresholdDb <= rSampled.proposed.replaceThresholdDb - 3.0f + 0.01f);
    CHECK (item->why.find ("opens it too") != std::string::npos);
    // ... and it holds: the same listen with the same switches fits the same gate.
    sampled.current = rSampled.proposed;
    const auto again = TuneEngine::tune (sampled);
    CHECK (find (again, TuneSection::Bleed, "Gate tightened for the sample") == nullptr);
    CHECK (again.parametersChanged == 0);

    // The hi-hat in a sampled kit: a gentle expander (never on its own), and the high-pass at the top of its range.
    auto h = onTarget (ChannelRole::HiHat);
    h.bleedEstimate = 0.5f; h.noiseFloorDb = h.hitLevelDb - 20.0f;
    auto hatPlain = context (ChannelRole::HiHat, h);
    auto hatKit = hatPlain;
    hatKit.kitSampled = true;
    const auto hp = TuneEngine::tune (hatPlain);
    const auto hk = TuneEngine::tune (hatKit);
    REQUIRE (hp.valid && hk.valid);
    CHECK (! hp.proposed.gateEnabled);
    CHECK (hk.proposed.gateEnabled);
    CHECK_NEAR (hk.proposed.gateRangeDb, 10.0f, 0.01f);
    CHECK_NEAR (hk.proposed.gateRatio, 2.0f, 0.01f);
    const auto& hatTargets = StyleProfile::targets (ChannelRole::HiHat, StyleProfileId::ModernGospel);
    CHECK (hk.proposed.hpfEnabled);
    CHECK_NEAR (hk.proposed.hpfHz, hatTargets.hpfMaxHz, 1.0f);
    CHECK (hk.proposed.hpfHz >= hp.proposed.hpfHz);

    // A hat with its own sample: a shallow expander like a snare's, never the hard gate a kick gets.
    auto hatSampled = hatPlain;
    hatSampled.current.replaceEnabled = true;
    hatSampled.sampled = true;
    const auto hs = TuneEngine::tune (hatSampled);
    REQUIRE (hs.valid);
    CHECK (hs.proposed.gateEnabled);
    CHECK_NEAR (hs.proposed.gateRangeDb, 20.0f, 0.01f);
    CHECK_NEAR (hs.proposed.gateRatio, 4.0f, 0.01f);
    CHECK (hs.proposed.gateThresholdDb <= hs.proposed.replaceThresholdDb - 3.0f + 0.01f);
    CHECK (hs.proposed.replaceEnabled);
    CHECK (find (hs, TuneSection::Bleed, "Sample trigger fitted") != nullptr);   // the hat's trigger is fitted like a drum's
    hatSampled.current = hs.proposed;
    CHECK (TuneEngine::tune (hatSampled).parametersChanged == 0);

    // The overheads and the room: never gated, high-pass at the top of the range.
    for (auto role : { ChannelRole::Overhead, ChannelRole::Room })
    {
        auto o = onTarget (role);
        auto kit = context (role, o);
        kit.kitSampled = true;
        const auto r = TuneEngine::tune (kit);
        REQUIRE (r.valid);
        CHECK (! r.proposed.gateEnabled);
        const auto& targets = StyleProfile::targets (role, StyleProfileId::ModernGospel);
        CHECK (r.proposed.hpfEnabled);
        CHECK_NEAR (r.proposed.hpfHz, targets.hpfMaxHz, 1.0f);
    }

    // A tom without its own sample in a sampled kit: the expander comes on at half the bleed it would otherwise need, and closes further.
    auto tm = onTarget (ChannelRole::RackTom);
    tm.bleedEstimate = 0.2f; tm.noiseFloorDb = tm.hitLevelDb - 25.0f;     // under the profile's gate threshold on its own
    auto tomPlain = context (ChannelRole::RackTom, tm);
    tomPlain.current.gateEnabled = false;
    auto tomKit = tomPlain;
    tomKit.kitSampled = true;
    const auto tp = TuneEngine::tune (tomPlain);
    const auto tk = TuneEngine::tune (tomKit);
    REQUIRE (tp.valid && tk.valid);
    CHECK (! tp.proposed.gateEnabled);
    CHECK (tk.proposed.gateEnabled);
}

TEST_CASE ("Tune: a sampled snare keeps a shallow expander so its ghost notes come through; a sampled kick closes hard")
{
    for (auto role : { ChannelRole::SnareTop, ChannelRole::KickIn })
    {
        auto a = onTarget (role);
        a.bleedEstimate = 0.5f; a.noiseFloorDb = a.hitLevelDb - 25.0f; a.bleedLevelDb = a.hitLevelDb - 20.0f; a.musicalPeakDb = a.hitLevelDb + 9.0f;
        auto ctx = context (role, a);
        ctx.current.replaceEnabled = true;
        ctx.sampled = true;
        ctx.kitSampled = true;
        const auto r = TuneEngine::tune (ctx);
        REQUIRE (r.valid);
        CHECK (r.proposed.gateEnabled);
        if (role == ChannelRole::SnareTop)
        {
            CHECK_NEAR (r.proposed.gateRangeDb, 20.0f, 0.01f);
            CHECK_NEAR (r.proposed.gateRatio, 4.0f, 0.01f);
            const auto* item = find (r, TuneSection::Bleed, "Gate tightened for the sample");
            REQUIRE (item != nullptr);
            CHECK (item->why.find ("ghost notes") != std::string::npos);
        }
        else
        {
            CHECK (r.proposed.gateRangeDb >= 40.0f);
            CHECK_NEAR (r.proposed.gateRatio, 10.0f, 0.01f);
        }
    }
    // Toms play their sample as recorded until the engineer turns following on.
    CHECK (! StyleProfile::baseline (ChannelRole::RackTom, StyleProfileId::ModernGospel).replaceFollowDrum);
}
