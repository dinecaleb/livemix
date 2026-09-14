// REFERENCE MIX: "make it sound like this."
//
// The tests that matter here are the refusals as much as the moves: a reference is somebody
// else's finished record, and the whole feature is only trustworthy if it can be shown that
// it moves the tone, says so, and leaves the delivery loudness and the band's balance alone.
#include "TestFramework.h"
#include "TestSignals.h"
#include "Mix/MixEngine.h"
#include "Mix/MixCapture.h"
#include "Mix/MixPlanner.h"
#include "Mix/ReferenceMix.h"
#include "Analysis/AnalysisAccumulator.h"
#include "Profiles/MixProfileData.h"
#include <cmath>
#include <random>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr StyleProfileId kProfile = StyleProfileId::ModernGospel;

    SourceTargets masterTargets()
    {
        return Profiles::targets (kProfile, busRole (MixBus::Master, MixPurpose::ChurchBroadcast));
    }

    // A measurement of "a mix", with a flat-ish share in every band, so a reference's
    // difference from it is entirely the test's doing.
    AnalysisResult mixMeasurement (float perBandDb = -9.0f, int channels = 2, float correlation = 0.7f)
    {
        AnalysisResult a;
        a.valid = true;
        a.numChannels = channels;
        a.durationSeconds = 30.0f;
        a.bandEnergyDb.fill (perBandDb);
        a.stereoCorrelation = correlation;
        a.crestFactorDb = 14.0f;
        a.loudnessLufs = -23.0f;
        return a;
    }

    ReferenceProfile referenceLike (const AnalysisResult& a, const char* name = "Reference")
    {
        return Reference::profileFrom (a, name, "/tmp/reference.wav");
    }

    const ReferenceBandMatch& bandOf (const ReferenceMatch& m, Band b) { return m.bands[size_t (b)]; }
}

TEST_CASE ("Reference: a clip, a silence or an unreadable file is refused with a reason")
{
    const auto& B = MixProfile::referenceBounds (kProfile);

    AnalysisResult nothing;                       // never measured
    const auto unreadable = Reference::adequacy (nothing, kProfile);
    CHECK (! unreadable.usable);
    CHECK (! unreadable.reason.empty());
    CHECK (! unreadable.guidance.empty());

    auto shortClip = mixMeasurement();
    shortClip.durationSeconds = B.minSeconds - 1.0f;
    const auto brief = Reference::adequacy (shortClip, kProfile);
    CHECK (! brief.usable);
    CHECK (brief.reason.find ("seconds") != std::string::npos);

    auto silent = mixMeasurement();
    silent.loudnessLufs = -90.0f;
    CHECK (! Reference::adequacy (silent, kProfile).usable);

    auto gaps = mixMeasurement();
    gaps.silencePercent = B.maxSilencePercent + 5.0f;
    CHECK (! Reference::adequacy (gaps, kProfile).usable);

    // A real mixdown is usable, and that is the only case that gets through.
    CHECK (Reference::adequacy (mixMeasurement(), kProfile).usable);
}

TEST_CASE ("Reference: the master's tone aim follows the reference, and stops at the profile's bound")
{
    const auto base = masterTargets();
    const auto& B = MixProfile::referenceBounds (kProfile);

    // A reference with a great deal more air than any profile asks for.
    auto bright = mixMeasurement();
    bright.bandEnergyDb[size_t (Band::Air)] = base.bandTargetDb[size_t (Band::Air)] + 8.0f;
    bright.bandEnergyDb[size_t (Band::LowMid)] = base.bandTargetDb[size_t (Band::LowMid)] - 2.0f;

    ReferenceMatch match;
    const auto mix = mixMeasurement();
    const auto t = Reference::targets (base, referenceLike (bright, "Bright Record"), mix, kProfile, match);

    REQUIRE (match.used);
    CHECK (match.name == "Bright Record");

    // Air: the aim moved toward the reference and stopped exactly at the bound, and the
    // sentence that says so names the reason rather than silently clipping.
    CHECK_NEAR (t.bandTargetDb[size_t (Band::Air)], base.bandTargetDb[size_t (Band::Air)] + B.maxTargetShiftDb, 0.01f);
    CHECK (bandOf (match, Band::Air).bounded);
    bool saidSo = false;
    for (const auto& l : match.limits) if (l.find ("air") != std::string::npos) saidSo = true;
    CHECK (saidSo);

    // Low-mid: inside the bound, so the aim is the reference's own balance, exactly.
    CHECK_NEAR (t.bandTargetDb[size_t (Band::LowMid)], bright.bandEnergyDb[size_t (Band::LowMid)], 0.01f);
    CHECK (! bandOf (match, Band::LowMid).bounded);

    // Matching aims closer than the profile's own "that will do".
    for (int i = 0; i < int (Band::Count); ++i)
        CHECK (t.bandToleranceDb[size_t (i)] <= base.bandToleranceDb[size_t (i)] + 0.001f);

    // And it says what it is aiming for, against this mix rather than in the abstract.
    CHECK (! match.aims.empty());
}

TEST_CASE ("Reference: the delivery loudness is never copied, and it says why")
{
    auto base = masterTargets();
    base.loudnessTargetAppropriate = true;
    base.targetLufs = -23.0f;

    auto mastered = mixMeasurement();
    mastered.loudnessLufs = -9.0f;               // a commercial master
    mastered.crestFactorDb = 7.0f;               // ... after a limiter this mix has not reached

    ReferenceMatch match;
    const auto t = Reference::targets (base, referenceLike (mastered), mixMeasurement(), kProfile, match);

    // The one number a reference must never set.
    CHECK_NEAR (t.targetLufs, base.targetLufs, 0.001f);
    CHECK_NEAR (t.truePeakCeilingDb, base.truePeakCeilingDb, 0.001f);
    CHECK_NEAR (match.loudnessDifferenceLu, 14.0f, 0.01f);
    bool explained = false;
    for (const auto& l : match.limits) if (l.find ("LUFS") != std::string::npos) explained = true;
    CHECK (explained);

    // Density follows it, but only as far as a reference is allowed to push the glue, and it
    // never decides whether there is any compression at all.
    const auto& B = MixProfile::referenceBounds (kProfile);
    CHECK (t.compTargetGrDb <= base.compTargetGrDb + B.maxCompTargetShiftDb + 0.001f);
    CHECK (t.compTargetGrDb >= base.compTargetGrDb - B.maxCompTargetShiftDb - 0.001f);
    CHECK (t.compressionAppropriate == base.compressionAppropriate);
    CHECK (t.crestFactorMaxDb == base.crestFactorMaxDb);
    CHECK (t.crestFactorMinDb == base.crestFactorMinDb);

    // The balance of the band is not a reference's business, and the sheet says that out loud.
    bool balanceSaid = false;
    for (const auto& l : match.limits) if (l.find ("who is loud") != std::string::npos) balanceSaid = true;
    CHECK (balanceSaid);
}

TEST_CASE ("Reference: a wider record widens the master image, inside the bound; a mono one does not")
{
    auto base = masterTargets();
    base.widthAppropriate = true;
    base.widthTarget = 1.0f;
    base.widthMin = 0.7f;
    base.widthMax = 1.4f;
    const auto& B = MixProfile::referenceBounds (kProfile);

    auto wide = mixMeasurement();
    wide.stereoCorrelation = 0.1f;               // a lot of side energy
    ReferenceMatch m1;
    const auto t1 = Reference::targets (base, referenceLike (wide), mixMeasurement (-9.0f, 2, 0.9f), kProfile, m1);
    CHECK (m1.widthMatched);
    CHECK (t1.widthTarget > base.widthTarget);
    CHECK (t1.widthTarget <= base.widthTarget * (1.0f + B.maxWidthDelta) + 0.03f);

    auto narrow = mixMeasurement();
    narrow.stereoCorrelation = 0.98f;
    ReferenceMatch m2;
    const auto t2 = Reference::targets (base, referenceLike (narrow), mixMeasurement (-9.0f, 2, 0.3f), kProfile, m2);
    CHECK (t2.widthTarget < base.widthTarget);
    CHECK (t2.widthTarget >= base.widthTarget * (1.0f - B.maxWidthDelta) - 0.03f);

    // A mono reference has no image to copy, and says so instead of guessing.
    auto mono = mixMeasurement();
    mono.numChannels = 1;
    ReferenceMatch m3;
    const auto t3 = Reference::targets (base, referenceLike (mono), mixMeasurement(), kProfile, m3);
    CHECK (! m3.widthMatched);
    CHECK_NEAR (t3.widthTarget, base.widthTarget, 0.001f);
    bool saidMono = false;
    for (const auto& l : m3.limits) if (l.find ("mono") != std::string::npos) saidMono = true;
    CHECK (saidMono);
}

TEST_CASE ("Reference: matching the same reference twice decides the same thing")
{
    const auto base = masterTargets();
    auto ref = mixMeasurement();
    ref.bandEnergyDb[size_t (Band::Low)] += 4.0f;
    ref.bandEnergyDb[size_t (Band::Presence)] -= 2.0f;

    ReferenceMatch a, b;
    const auto t1 = Reference::targets (base, referenceLike (ref), mixMeasurement(), kProfile, a);
    const auto t2 = Reference::targets (base, referenceLike (ref), mixMeasurement(), kProfile, b);
    for (int i = 0; i < int (Band::Count); ++i)
        CHECK_NEAR (t1.bandTargetDb[size_t (i)], t2.bandTargetDb[size_t (i)], 0.0001f);
    CHECK_NEAR (t1.widthTarget, t2.widthTarget, 0.0001f);
    CHECK_NEAR (t1.compTargetGrDb, t2.compTargetGrDb, 0.0001f);
    CHECK (a.aims == b.aims);
    CHECK (a.limits == b.limits);
}

// ---------------------------------------------------------------------------
// Through the planner: the reference has to reach the master chain, change only the master,
// and leave the plan as re-tunable as it was.
// ---------------------------------------------------------------------------
namespace
{
    MixSession smallBand()
    {
        MixSession s;
        s.profile = kProfile;
        s.purpose = MixPurpose::ChurchBroadcast;
        s.inputs = {
            { "Kick",  ChannelRole::KickIn,     0, -1 },
            { "Snare", ChannelRole::SnareTop,   1, -1 },
            { "Bass",  ChannelRole::BassDI,     2, -1 },
            { "Keys",  ChannelRole::Piano,      3,  4 },
            { "Lead",  ChannelRole::LeadVocal,  5, -1 },
        };
        return s;
    }

    testsig::Buffer bandAudio (float seconds = 14.0f)
    {
        const int n = int (kSr * double (seconds));
        testsig::Buffer in (6, n);
        std::mt19937 rng (11);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float t = float (i) / float (kSr);
            const float beat = std::fmod (t, 0.5f);
            const float kickEnv = beat < 0.18f ? std::exp (-beat * 22.0f) : 0.0f;
            const float snareEnv = (beat > 0.25f && beat < 0.42f) ? std::exp (-(beat - 0.25f) * 26.0f) : 0.0f;
            in.ptrs[0][i] = 0.5f * kickEnv * std::sin (2.0f * float (M_PI) * 55.0f * t);
            in.ptrs[1][i] = 0.35f * snareEnv * (0.5f * noise (rng) + 0.5f * std::sin (2.0f * float (M_PI) * 190.0f * t));
            in.ptrs[2][i] = 0.30f * std::sin (2.0f * float (M_PI) * 82.0f * t);
            in.ptrs[3][i] = 0.18f * (std::sin (2.0f * float (M_PI) * 262.0f * t) + 0.6f * std::sin (2.0f * float (M_PI) * 392.0f * t));
            in.ptrs[4][i] = 0.18f * (std::sin (2.0f * float (M_PI) * 262.0f * t + 0.4f) + 0.6f * std::sin (2.0f * float (M_PI) * 330.0f * t));
            in.ptrs[5][i] = 0.28f * std::sin (2.0f * float (M_PI) * 220.0f * t) * (0.6f + 0.4f * std::sin (2.0f * float (M_PI) * 1.3f * t));
        }
        return in;
    }
}

TEST_CASE ("MixPlanner: a reference aims the master and nothing else, and re-planning it changes nothing")
{
    MixEngine engine;
    MixCapture capture;
    auto session = smallBand();
    engine.prepare (kSr, 128, session);
    capture.prepare (kSr, engine.getGraph());
    engine.setTap (&capture);

    auto in = bandAudio();
    std::vector<const float*> ip (in.ptrs.size());
    std::vector<float> l (128), r (128);
    float* op[2] = { l.data(), r.data() };
    capture.start ({ 10.0f, -45.0f, 2.0f, -50.0f, -1 });
    for (int i = 0; i + 128 <= in.numSamples(); i += 128)
    {
        for (size_t c = 0; c < ip.size(); ++c) ip[c] = in.ptrs[c] + i;
        engine.process (ip.data(), int (ip.size()), op, 2, 128);
    }
    for (int i = 0; i < 4000 && capture.getState() != MixCapture::State::Complete; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    REQUIRE (capture.getState() == MixCapture::State::Complete);

    MixPlanContext ctx;
    ctx.session = session;
    ctx.graph = engine.getGraph();
    ctx.current = engine.getAppliedParameters();
    ctx.atCapture = ctx.current;
    ctx.capture = capture.getResult();
    REQUIRE (ctx.capture.valid);

    const auto plain = MixPlanner::plan (ctx);
    REQUIRE (plain.valid);
    CHECK (! plain.reference.used);

    // The same listen, aimed at a deliberately dark, dense reference.
    auto refMeasurement = ctx.capture.buses[size_t (MixBus::Master)];
    REQUIRE (refMeasurement.valid);
    refMeasurement.durationSeconds = 180.0f;
    refMeasurement.loudnessLufs = -9.0f;
    refMeasurement.silencePercent = 0.0f;
    refMeasurement.bandEnergyDb[size_t (Band::Air)] -= 6.0f;
    refMeasurement.bandEnergyDb[size_t (Band::Brilliance)] -= 5.0f;
    refMeasurement.bandEnergyDb[size_t (Band::Low)] += 5.0f;
    REQUIRE (Reference::adequacy (refMeasurement, kProfile).usable);

    MixPlanContext withRef = ctx;
    withRef.reference = Reference::profileFrom (refMeasurement, "Dark Record", "/tmp/dark.wav");
    const auto matched = MixPlanner::plan (withRef);
    REQUIRE (matched.valid);
    REQUIRE (matched.reference.used);
    CHECK (matched.reference.name == "Dark Record");

    // The master heard the reference...
    const auto& plainMaster = plain.proposed.buses[size_t (MixBus::Master)].channel;
    const auto& refMaster = matched.proposed.buses[size_t (MixBus::Master)].channel;
    CHECK (! diffParameters (plainMaster, refMaster).empty());

    // ... and nothing else did. A reference never moves a source, a fader or a group.
    for (int i = 0; i < plain.proposed.numStrips; ++i)
    {
        CHECK (diffParameters (plain.proposed.strips[size_t (i)].channel, matched.proposed.strips[size_t (i)].channel).empty());
        CHECK_NEAR (plain.proposed.strips[size_t (i)].faderDb, matched.proposed.strips[size_t (i)].faderDb, 0.001f);
        CHECK_NEAR (plain.proposed.strips[size_t (i)].inputGainDb, matched.proposed.strips[size_t (i)].inputGainDb, 0.001f);
    }
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        CHECK (diffParameters (plain.proposed.buses[size_t (b)].channel, matched.proposed.buses[size_t (b)].channel).empty());
        CHECK_NEAR (plain.proposed.buses[size_t (b)].faderDb, matched.proposed.buses[size_t (b)].faderDb, 0.001f);
    }

    // The plan says what it aimed at, in the notes the result sheet reads.
    bool named = false;
    for (const auto& n : matched.notes) if (n.find ("Dark Record") != std::string::npos) named = true;
    CHECK (named);

    // Re-planning the same listen with the same reference proposes the same mix: matching is
    // computed from the reference and the profile, never from where the master happens to sit.
    MixPlanContext again = withRef;
    again.current = matched.proposed;
    const auto second = MixPlanner::plan (again);
    REQUIRE (second.valid);
    CHECK (diffParameters (matched.proposed.buses[size_t (MixBus::Master)].channel,
                           second.proposed.buses[size_t (MixBus::Master)].channel).empty());

    // TUNE CHANNEL leaves the master alone, so it must not claim a reference was applied.
    const auto channel = MixPlanner::channelOnly (matched, 0, kProfile);
    CHECK (! channel.reference.used);
    CHECK (diffParameters (channel.before.buses[size_t (MixBus::Master)].channel,
                           channel.proposed.buses[size_t (MixBus::Master)].channel).empty());
}
