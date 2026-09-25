#include "TestFramework.h"
#include "TestSignals.h"
#include "Mix/MixEngine.h"
#include "Mix/OfflineCapture.h"
#include "Mix/MixPlanner.h"
#include "Profiles/MixProfileData.h"
#include "Profiles/StyleProfile.h"
#include "FX/FxProfiles.h"
#include <cstdio>
#include <random>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;

    // A small church band on 15 device inputs.
    MixSession band()
    {
        MixSession s;
        s.profile = StyleProfileId::ModernGospel;
        s.purpose = MixPurpose::ChurchBroadcast;
        s.inputs = {
            { "Kick",   ChannelRole::KickIn,        0, -1 },
            { "Snare",  ChannelRole::SnareTop,      1, -1 },
            { "Tom L",  ChannelRole::RackTom,       2, -1 },
            { "Tom R",  ChannelRole::FloorTom,      3, -1 },
            { "OH",     ChannelRole::Overhead,      4,  5 },
            { "Room",   ChannelRole::Room,          6, -1 },
            { "Bass",   ChannelRole::BassDI,        7, -1 },
            { "Keys",   ChannelRole::Piano,         8,  9 },
            { "Lead",   ChannelRole::LeadVocal,    10, -1 },
            { "Vox 1",  ChannelRole::BackingVocal, 11, -1 },
            { "Vox 2",  ChannelRole::BackingVocal, 12, -1 },
            { "Vox 3",  ChannelRole::BackingVocal, 13, -1 },
            { "Pastor", ChannelRole::Speech,       14, -1 },
        };
        return s;
    }

    void sine (std::vector<float>& c, float hz, float amp, float from = 0.0f, float to = 1.0e9f)
    {
        for (size_t i = 0; i < c.size(); ++i)
        {
            const float t = float (i) / float (kSr);
            if (t >= from && t < to) c[i] += amp * std::sin (2.0f * float (M_PI) * hz * t);
        }
    }
    void bursts (std::vector<float>& c, float hz, float amp, float periodS, float lengthS, float phaseS, bool noise, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        for (size_t i = 0; i < c.size(); ++i)
        {
            const float t = float (i) / float (kSr);
            const float inPeriod = std::fmod (t + periodS - phaseS, periodS);
            if (inPeriod < lengthS)
            {
                const float env = 1.0f - inPeriod / lengthS;
                c[i] += amp * env * (noise ? dist (rng) : std::sin (2.0f * float (M_PI) * hz * t));
            }
        }
    }
    void noise (std::vector<float>& c, float amp, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        for (auto& x : c) x += amp * dist (rng);
    }

    // Eight seconds of "band". The pastor stays silent.
    testsig::Buffer bandAudio()
    {
        testsig::Buffer in (15, int (kSr * 8));
        bursts (in.data[0], 100.0f, 0.7f, 0.5f, 0.12f, 0.0f, false, 1);    // kick: 100 Hz, sits in the Low band
        bursts (in.data[1], 0.0f, 0.5f, 0.5f, 0.05f, 0.25f, true, 2);     // snare: noise crack
        bursts (in.data[2], 120.0f, 0.5f, 2.0f, 0.3f, 0.7f, false, 3);    // rack tom, rare
        bursts (in.data[3], 90.0f, 0.5f, 2.0f, 0.3f, 1.4f, false, 4);     // floor tom, rare
        noise (in.data[2], 0.03f, 5); noise (in.data[3], 0.03f, 6);        // bleed on the tom mics
        noise (in.data[4], 0.05f, 7); noise (in.data[5], 0.05f, 8);        // overheads (stereo pair)
        noise (in.data[6], 0.04f, 9);                                      // room
        sine (in.data[7], 40.0f, 0.8f);                                    // bass: loud and sub-heavy
        sine (in.data[8], 262.0f, 0.15f); sine (in.data[8], 2600.0f, 0.2f); // keys: bright upper mids
        sine (in.data[9], 330.0f, 0.15f); sine (in.data[9], 2800.0f, 0.2f);
        sine (in.data[10], 220.0f, 0.3f); sine (in.data[10], 440.0f, 0.1f); // lead: warm voice-like tone
        sine (in.data[11], 330.0f, 0.2f); sine (in.data[12], 392.0f, 0.2f); sine (in.data[13], 494.0f, 0.2f);
        noise (in.data[14], 0.02f, 10);                                    // pastor mic: only band spill during the song
        return in;
    }

    struct Rig
    {
        MixEngine engine;
        OfflineCapture capture;
        MixSession session;
        explicit Rig (const MixSession& s) : session (s)
        {
            engine.prepare (kSr, 128, session);
            capture.prepare (kSr, engine.getGraph());
            engine.setTap (&capture);
        }
        MixCapture::Result listen (testsig::Buffer& in)
        {
            std::vector<const float*> ip (in.ptrs.size());
            std::vector<float> l (128), r (128);
            float* op[2] = { l.data(), r.data() };
            capture.start();
            for (int i = 0; i + 128 <= in.numSamples(); i += 128)
            {
                for (size_t c = 0; c < ip.size(); ++c) ip[c] = in.ptrs[c] + i;
                engine.process (ip.data(), int (ip.size()), op, 2, 128);
            }
            return capture.finish();
        }
        MixPlanContext context (const MixCapture::Result& cap)
        {
            MixPlanContext ctx;
            ctx.session = session;
            ctx.graph = engine.getGraph();
            ctx.current = engine.getAppliedParameters();
            ctx.atCapture = ctx.current;
            ctx.capture = cap;
            return ctx;
        }
    };

    const StripPlan& stripNamed (const MixPlan& p, const char* name)
    {
        for (const auto& s : p.strips) if (s.name == name) return s;
        throw std::runtime_error ("no strip named " + std::string (name));
    }
    int stripIndex (const MixPlan& p, const char* name) { return stripNamed (p, name).strip; }
    bool hasRelationship (const MixPlan& p, const char* text)
    {
        for (const auto& r : p.relationships) if (r.what.find (text) != std::string::npos) return true;
        return false;
    }
}

TEST_CASE ("MixPlanner: the listen finds the tempo, and the delays and reverb tails are fitted to it")
{
    // The synthetic band plays its kick and snare on a 0.5 s pulse: 120 BPM, known exactly. The engine
    // starts from a deliberately wrong tempo so the planner has to measure it rather than agree by default.
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    auto ctx = rig.context (cap);
    ctx.current.tempoBpm = 150.0f;
    ctx.atCapture.tempoBpm = 150.0f;
    const auto plan = MixPlanner::plan (ctx);
    REQUIRE (plan.valid);

    // A live console has no host play head, so without this every tempo-synced delay runs at whatever the
    // engine was left at and sits out of time with the band.
    CHECK_NEAR (plan.proposed.tempoBpm, 120.0f, 6.0f);
    CHECK (hasRelationship (plan, "Delays timed to the song"));

    // Reverb tails are fitted to the song: shortened toward the profile's beat count, never past the
    // effect's own character, and never gutted.
    const float secondsPerBeat = 60.0f / plan.proposed.tempoBpm;
    bool fittedOne = false;
    for (int f = 0; f < int (FxSlot::Count); ++f)
    {
        if (! ctx.graph.fxUsed[size_t (f)]) continue;
        const auto& fx = plan.proposed.fx[size_t (f)];
        const float character = FxProfiles::baseline (StyleProfileId::ModernGospel, ctx.graph.fxType[size_t (f)]).reverbDecayS;
        const float beats = MixProfile::reverbBeats (StyleProfileId::ModernGospel, FxSlot (f));
        if (beats <= 0.0f || ! fx.fx.reverbEnabled) continue;
        CHECK (fx.fx.reverbDecayS <= character + 0.01f);            // the profile's decay stays the ceiling
        CHECK (fx.fx.reverbDecayS >= 0.5f * character - 0.01f);     // ... and it is never gutted
        CHECK_NEAR (fx.fx.reverbDecayS, std::max (std::min (beats * secondsPerBeat, character), 0.5f * character), 0.05f);
        fittedOne = true;
    }
    CHECK (fittedOne);

    // A held note or an open room microphone has no rhythm to read: only sources that play one vote.
    for (const auto& sp : plan.strips)
        if (sp.heard && roleFamily (sp.role) == RoleFamily::Speech)
            CHECK (cap.strips[size_t (sp.strip)].transientsPerSecond < 0.5f);   // the pastor's spill is not a voter

    // Planning again on the same listen lands on the same tempo and the same tails.
    MixPlanContext again = ctx;
    again.current = plan.proposed;
    const auto second = MixPlanner::plan (again);
    REQUIRE (second.valid);
    CHECK_NEAR (second.proposed.tempoBpm, plan.proposed.tempoBpm, 0.01f);
    for (int f = 0; f < int (FxSlot::Count); ++f)
        CHECK_NEAR (second.proposed.fx[size_t (f)].fx.reverbDecayS, plan.proposed.fx[size_t (f)].fx.reverbDecayS, 0.01f);
}

TEST_CASE ("MixPlanner: one listen tunes every source, balances the faders and reasons about the mix as a whole")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    const auto ctx = rig.context (cap);
    const auto plan = MixPlanner::plan (ctx);
    REQUIRE (plan.valid);
    CHECK (plan.headline == "MIX TUNED");
    CHECK (plan.strips.size() == 13);
    CHECK (plan.stripsHeard == 13);                    // the pastor's mic hears the band
    CHECK (stripNamed (plan, "Pastor").heard);
    CHECK (stripNamed (plan, "Pastor").bleedOnly);      // ... so it is left alone
    CHECK (stripNamed (plan, "Pastor").faderDb == 0.0f);
    CHECK (stripNamed (plan, "Pastor").inputGainDb == 0.0f);
    CHECK (plan.parametersChanged > 0);
    CHECK (plan.fadersChanged > 0);

    // Every heard source got its own Tune report.
    for (const auto& s : plan.strips)
        if (s.heard) CHECK (s.tune.valid);

    // Balance: every heard source's fader is fitted from how loud it is while it plays to the profile's
    // mix level, held under the peak ceiling, and inside bounds.
    const auto& R = MixProfile::relationships (StyleProfileId::ModernGospel);
    for (const auto& s : plan.strips)
    {
        CHECK (std::fabs (s.faderDb) <= R.maxFaderMoveDb + 0.01f);
        if (! s.balanced || roleFamily (s.role) == RoleFamily::BackingVocal) continue;
        const RoleFamily f = roleFamily (s.role);
        const auto& proposed = plan.proposed.strips[size_t (s.strip)];
        const float target = MixProfile::mixLevelTargetDb (StyleProfileId::ModernGospel, f);
        const float level = MixPlanner::predictedProcessedActiveRmsDb (ctx, s.strip, proposed);
        float peak = MixPlanner::predictedProcessedPeakDb (ctx, s.strip, proposed);
        // A voice is held to the musical peak, not to the loudest sample: its peaks are
        // consonants, and one click in a desk export is not the voice at all.
        const bool voice = f == RoleFamily::Speech || f == RoleFamily::LeadVocal || f == RoleFamily::BackingVocal || f == RoleFamily::Choir;
        const auto& ana = ctx.capture.strips[size_t (s.strip)];
        if (voice && ana.musicalPeakDb > -119.0f && ana.peakDb > ana.musicalPeakDb) peak -= ana.peakDb - ana.musicalPeakDb;
        float expected = std::round ((target - level) * 2.0f) * 0.5f;
        expected = std::min (expected, std::round ((MixProfile::stripPeakCeilingDb (StyleProfileId::ModernGospel, f) - peak) * 2.0f) * 0.5f);
        // A drum close microphone hears the rest of the kit, so the balance only lifts one so far.
        const bool closeMic = f == RoleFamily::Kick || f == RoleFamily::Snare || f == RoleFamily::Tom || f == RoleFamily::HiHat;
        if (closeMic)
        {
            // ... counted over the gain and the fader together, whichever way the gain went.
            expected = std::min (expected, std::max (std::round ((R.maxCloseMicRaiseDb - s.inputGainDb) * 2.0f) * 0.5f, 0.0f));
        }
        expected = std::max (-R.maxFaderMoveDb, std::min (R.maxFaderMoveDb, expected));
        // ... and a move not worth making is not made.
        if (std::fabs (expected - s.faderBeforeDb) < R.faderDeadbandDb) expected = s.faderBeforeDb;
        CHECK_NEAR (s.faderDb, expected, 0.01f);
    }
    CHECK (stripNamed (plan, "Bass").balanced);
    CHECK (stripNamed (plan, "Lead").balanced);

    // Kick <-> bass: the bass carries the sub, so its high-pass is raised (never above 0.8 x its note).
    const auto& bass = plan.proposed.strips[size_t (stripIndex (plan, "Bass"))].channel;
    CHECK (hasRelationship (plan, "Bass high-pass"));
    CHECK (bass.hpfEnabled);
    CHECK (bass.hpfHz >= std::min (R.bassHpfMinHz, 0.8f * 40.0f) - 0.5f);
    CHECK (bass.hpfHz <= 0.8f * 40.0f + 0.5f);   // the note wins: never above 0.8 x the measured fundamental

    // Lead <-> keys: the keys make room around the vocal pocket.
    const auto& keys = plan.proposed.strips[size_t (stripIndex (plan, "Keys"))].channel;
    CHECK (hasRelationship (plan, "Made room for the lead vocal in KEYS"));
    CHECK (keys.toneBands[1].enabled);
    CHECK_NEAR (keys.toneBands[1].freqHz, R.vocalPocketHz, 0.5f);
    CHECK (keys.toneBands[1].gainDb <= -1.0f);
    CHECK (keys.toneBands[1].gainDb >= -R.vocalPocketMaxCutDb - 0.01f);

    // Toms <-> overheads: if a tom gate exists it never closes fully.
    for (const char* tom : { "Tom L", "Tom R" })
    {
        const auto& p = plan.proposed.strips[size_t (stripIndex (plan, tom))].channel;
        if (p.gateEnabled) CHECK (p.gateRangeDb <= R.tomGateMaxRangeWithOverheadsDb + 0.01f);
    }

    // Room mics present: the drum room return steps back on the toms' sends.
    CHECK (hasRelationship (plan, "Drum room return stepped back"));
    CHECK_NEAR (plan.proposed.strips[size_t (stripIndex (plan, "Tom L"))].sendDb[size_t (FxSlot::DrumRoom)],
                MixProfile::defaultSendDb (StyleProfileId::ModernGospel, RoleFamily::Tom, FxSlot::DrumRoom) - R.drumRoomSendCutWithRoomMicsDb, 0.01f);

    // Lead <-> backing vocals: several voices add up, and however they were fitted the group ends up
    // behind the lead. Whether that needed a group move (the "held" relationship) or the per-voice level
    // already put them there depends on how many are singing, so the invariant is what is checked.
    {
        const float leadLevel = MixProfile::mixLevelTargetDb (StyleProfileId::ModernGospel, RoleFamily::LeadVocal);
        int voices = 0;
        for (const auto& s : plan.strips)
            if (s.balanced && roleFamily (s.role) == RoleFamily::BackingVocal) ++voices;
        REQUIRE (voices >= 2);
        const float group = MixProfile::mixLevelTargetDb (StyleProfileId::ModernGospel, RoleFamily::BackingVocal)
                          + 10.0f * std::log10 (float (voices));
        CHECK (group <= leadLevel - R.backingGroupBelowLeadDb + 0.01f);
        CHECK (hasRelationship (plan, "Backing vocals held") || hasRelationship (plan, "Lead vocal stays in front"));
    }
    CHECK (stripNamed (plan, "Vox 1").faderDb < stripNamed (plan, "Lead").faderDb);

    // Input gain: a hot kick (-3 dBFS) is brought down, quiet overheads (-26 dBFS) are brought up, both bounded.
    CHECK (stripNamed (plan, "Kick").inputGainDb < 0.0f);
    CHECK (stripNamed (plan, "OH").inputGainDb > 0.0f);
    for (const auto& s : plan.strips) CHECK (std::fabs (s.inputGainDb) <= R.maxInputGainDb + 0.01f);
    CHECK (plan.gainsChanged >= 2);
    CHECK (plan.proposed.strips[size_t (stripIndex (plan, "OH"))].inputGainDb == stripNamed (plan, "OH").inputGainDb);

    // Buses and master were tuned from what they received.
    CHECK (plan.buses[size_t (MixBus::Drums)].tune.valid);
    CHECK (plan.buses[size_t (MixBus::Vocals)].tune.valid);
    CHECK (plan.buses[size_t (MixBus::Master)].tune.valid);
    CHECK (plan.proposed.master().channel.limiterEnabled);
    CHECK (! plan.notes.empty());

    std::printf ("    %s | ", plan.headline.c_str());
    for (const auto& n : plan.notes) std::printf ("%s ", n.c_str());
    std::printf ("\n");
    for (const auto& r : plan.relationships) std::printf ("      - %s\n", r.what.c_str());
}

namespace
{
    void dumpDifferences (const MixPlan& a, const MixPlan& b)
    {
        for (size_t i = 0; i < a.strips.size(); ++i)
        {
            for (const auto& c : diffParameters (a.proposed.strips[i].channel, b.proposed.strips[i].channel))
                std::printf ("      %s: %s -> %.3g\n", a.strips[i].name.c_str(), c.paramId.c_str(), double (c.value));
            if (std::fabs (a.proposed.strips[i].faderDb - b.proposed.strips[i].faderDb) > 0.01f)
                std::printf ("      %s: fader %.1f -> %.1f\n", a.strips[i].name.c_str(), double (a.proposed.strips[i].faderDb), double (b.proposed.strips[i].faderDb));
        }
        for (int bus = 0; bus < int (MixBus::Count); ++bus)
            for (const auto& c : diffParameters (a.proposed.buses[size_t (bus)].channel, b.proposed.buses[size_t (bus)].channel))
                std::printf ("      %s bus: %s -> %.3g\n", mixBusName (MixBus (bus)), c.paramId.c_str(), double (c.value));
    }
}

TEST_CASE ("MixPlanner: planning again on the same listen changes nothing (idempotent)")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    auto ctx = rig.context (cap);
    const auto first = MixPlanner::plan (ctx);
    REQUIRE (first.valid);
    REQUIRE (! first.noChangeRequired);

    ctx.current = first.proposed;      // the user kept the plan; the listen is the same
    const auto second = MixPlanner::plan (ctx);
    REQUIRE (second.valid);
    if (! second.noChangeRequired) dumpDifferences (first, second);
    CHECK (second.noChangeRequired);
    CHECK (second.headline == "MIX: NO CHANGE REQUIRED");
    CHECK (second.parametersChanged == 0);
    CHECK (second.fadersChanged == 0);
    CHECK (second.sendsChanged == 0);
    CHECK (MixPlanner::countParameterChanges (first.proposed, second.proposed) == 0);
}

TEST_CASE ("MixPlanner: a close drum microphone is not lifted again by the next Tune Mix")
{
    // The budget for a close microphone - it hears the rest of the kit, so what lifts the drum lifts the
    // bleed - was measured against the gain that ran at the listen. After a mix is kept, the next listen
    // runs with that gain, so the raise it was meant to count reads as zero and the whole budget is handed
    // out again: a tom climbs another 6 dB a pass and brings the kit up inside its own microphone.
    Rig rig (band());
    auto in = bandAudio();
    // Under-gained tom microphones, the way a desk with the preamps left low sends them: quiet enough that
    // the whole close-mic budget is spent on digital gain before the fader is even asked for.
    for (int c : { 2, 3 }) for (auto& x : in.data[size_t (c)]) x *= 0.08f;
    const auto first = MixPlanner::plan (rig.context (rig.listen (in)));
    REQUIRE (first.valid);
    // The situation the rule is for has to actually arise, or this test guards nothing.
    REQUIRE (stripNamed (first, "Tom L").inputGainDb >= MixProfile::relationships (StyleProfileId::ModernGospel).maxCloseMicRaiseDb);

    rig.engine.setParameters (first.proposed);        // the user kept it; now DLIVE listens again through it
    auto ctx = rig.context (rig.listen (in));
    ctx.current = ctx.atCapture = first.proposed;
    const auto second = MixPlanner::plan (ctx);
    REQUIRE (second.valid);

    for (const auto& sp : first.strips)
    {
        const RoleFamily f = roleFamily (sp.role);
        if (! (f == RoleFamily::Kick || f == RoleFamily::Snare || f == RoleFamily::Tom || f == RoleFamily::HiHat)) continue;
        if (! sp.balanced) continue;
        const auto& again = stripNamed (second, sp.name.c_str());
        if (! again.balanced) continue;
        // Its total lift is gain plus fader; a second pass may trim it, but it must never add another budget.
        const float firstLift = std::max (sp.inputGainDb, 0.0f) + sp.faderDb;
        const float secondLift = std::max (again.inputGainDb, 0.0f) + again.faderDb;
        CHECK (secondLift <= firstLift + 0.75f);
    }
}

TEST_CASE ("MixPlanner: silence everywhere is reported as no signal, not as a mix")
{
    Rig rig (band());
    testsig::Buffer in (15, int (kSr * 3));
    const auto cap = rig.listen (in);
    const auto plan = MixPlanner::plan (rig.context (cap));
    CHECK (plan.headline == "MIX: NO SIGNAL");
    CHECK (plan.stripsHeard == 0);
    CHECK (MixPlanner::countParameterChanges (plan.before, plan.proposed) == 0);
}

TEST_CASE ("MixPlanner: a session without a lead vocal or bass makes no vocal or low-end relationship decisions")
{
    MixSession s;
    s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Keys", ChannelRole::Piano, 1, 2 } };
    Rig rig (s);
    testsig::Buffer in (3, int (kSr * 4));
    bursts (in.data[0], 80.0f, 0.7f, 0.5f, 0.12f, 0.0f, false, 1);
    sine (in.data[1], 262.0f, 0.2f); sine (in.data[2], 330.0f, 0.2f);
    const auto cap = rig.listen (in);
    const auto plan = MixPlanner::plan (rig.context (cap));
    REQUIRE (plan.valid);
    CHECK (! hasRelationship (plan, "Bass high-pass"));
    CHECK (! hasRelationship (plan, "Made room for the lead vocal"));
    CHECK (! hasRelationship (plan, "Backing vocals"));
    CHECK (plan.buses[size_t (MixBus::Master)].tune.valid);
}

// ---------------------------------------------------------------------------
// HOW LOUD THE FINISHED MIX SHOULD BE
//
// The master was quiet because "Church Broadcast" silently meant EBU R128 - -23 LUFS,
// correct for a television feed and about 9 dB under what a church stream is expected to
// be - and nothing in the app said so. The target is a setting now, and it is the number
// the whole gain structure is fitted against rather than a gain added at the end.
// ---------------------------------------------------------------------------
TEST_CASE ("MixPlanner: the delivery loudness moves the whole gain structure, and stays idempotent")
{
    auto session = band();
    Rig quiet (session);
    auto in = bandAudio();
    const auto cap = quiet.listen (in);
    auto broadcastCtx = quiet.context (cap);
    const auto broadcast = MixPlanner::plan (broadcastCtx);
    REQUIRE (broadcast.valid);

    // The same band, the same listen, aimed at a streaming loudness instead.
    session.delivery = DeliveryLoudness::StreamingLoud;
    auto loudCtx = broadcastCtx;
    loudCtx.session = session;
    const auto loud = MixPlanner::plan (loudCtx);
    REQUIRE (loud.valid);

    const auto& before = broadcast.proposed.master().channel;
    const auto& after = loud.proposed.master().channel;

    // The master ends up meaningfully louder: -14 LUFS against -23 is nine decibels.
    CHECK (after.outputTrimDb > before.outputTrimDb + 6.0f);
    // ...and it is not achieved by asking the limiter to do it. The ceiling comes *down*,
    // because a louder target through a lossy encoder needs more true-peak room, not less.
    CHECK (after.limiterEnabled);
    CHECK (after.limiterCeilingDb <= -1.0f);
    CHECK (after.limiterCeilingDb <= before.limiterCeilingDb + 1.5f);

    // Nothing about the balance moved: the delivery target is about level, not about who is
    // loud inside the mix.
    for (int i = 0; i < loud.proposed.numStrips; ++i)
        CHECK_NEAR (loud.proposed.strips[size_t (i)].faderDb, broadcast.proposed.strips[size_t (i)].faderDb, 0.01);

    // And re-tuning the same listen with the new target still says NO CHANGE REQUIRED.
    auto again = loudCtx;
    again.current = loud.proposed;
    const auto second = MixPlanner::plan (again);
    REQUIRE (second.valid);
    if (! second.noChangeRequired) dumpDifferences (loud, second);
    CHECK (second.noChangeRequired);

    // FromPurpose is what every session made before the setting existed had, and it is the
    // profile's own standard: identical to the plan above it.
    session.delivery = DeliveryLoudness::FromPurpose;
    auto defaultCtx = broadcastCtx;
    defaultCtx.session = session;
    const auto fromPurpose = MixPlanner::plan (defaultCtx);
    CHECK (MixPlanner::countParameterChanges (fromPurpose.proposed, broadcast.proposed) == 0);
}

// ---------------------------------------------------------------------------
// Crowd / ambience microphones
// ---------------------------------------------------------------------------
TEST_CASE ("MixPlanner: a crowd microphone gets its own group, is never gated, and sits under the band")
{
    auto session = band();
    session.inputs.push_back ({ "Crowd", ChannelRole::CrowdMic, 15, 16 });
    Rig rig (session);

    // The band, plus a wide, quiet, continuous room on 15/16 - the shape a congregation has.
    testsig::Buffer in (17, int (kSr * 8));
    {
        auto full = bandAudio();
        for (size_t c = 0; c < full.data.size() && c < in.data.size(); ++c) in.data[c] = full.data[c];
    }
    noise (in.data[15], 0.05f, 31);
    noise (in.data[16], 0.05f, 32);

    const auto cap = rig.listen (in);
    auto ctx = rig.context (cap);
    const auto plan = MixPlanner::plan (ctx);
    REQUIRE (plan.valid);

    const int crowd = stripIndex (plan, "Crowd");
    REQUIRE (crowd >= 0);
    CHECK (ctx.graph.strips[size_t (crowd)].bus == MixBus::Ambience);

    const auto& chain = plan.proposed.strips[size_t (crowd)].channel;
    // Never gated: on a room microphone the quiet between the sounds is the sound.
    CHECK (! chain.gateEnabled);
    // High-passed well above a stage source: a building's own low end carries nothing.
    CHECK (chain.hpfEnabled);
    CHECK (chain.hpfHz >= 90.0f);
    // Not transient-shaped, and not saturated: neither belongs on a room.
    CHECK (! chain.transientEnabled);
    CHECK (! chain.satEnabled);

    // It sits under the band rather than competing with it.
    const int lead = stripIndex (plan, "Lead");
    CHECK (plan.proposed.strips[size_t (crowd)].faderDb < plan.proposed.strips[size_t (lead)].faderDb + 6.0f);

    // Re-tuning the same listen changes nothing, ambience included.
    ctx.current = plan.proposed;
    const auto second = MixPlanner::plan (ctx);
    if (! second.noChangeRequired) dumpDifferences (plan, second);
    CHECK (second.noChangeRequired);
}

// ---------------------------------------------------------------------------
// Saxophone
// ---------------------------------------------------------------------------
TEST_CASE ("MixPlanner: a saxophone is a horn, not a keyboard")
{
    auto session = band();
    session.inputs.push_back ({ "Sax", ChannelRole::SaxTenor, 15, -1 });
    Rig rig (session);

    testsig::Buffer in (16, int (kSr * 8));
    {
        auto full = bandAudio();
        for (size_t c = 0; c < full.data.size() && c < in.data.size(); ++c) in.data[c] = full.data[c];
    }
    // A tenor's range, played in phrases with a hard honk at 1.2 kHz - the thing that makes a
    // sax a sax to mix - and real range between a held note and a wailed one.
    for (int phrase = 0; phrase < 4; ++phrase)
    {
        const float from = float (phrase) * 2.0f;
        const float loud = phrase % 2 == 0 ? 1.0f : 0.45f;
        sine (in.data[15], 220.0f, 0.30f * loud, from, from + 1.4f);
        sine (in.data[15], 1200.0f, 0.42f * loud, from, from + 1.4f);
        sine (in.data[15], 3300.0f, 0.10f * loud, from, from + 1.4f);
    }

    const auto cap = rig.listen (in);
    auto ctx = rig.context (cap);
    const auto plan = MixPlanner::plan (ctx);
    REQUIRE (plan.valid);

    const int sax = stripIndex (plan, "Sax");
    REQUIRE (sax >= 0);
    // It is a musical source, so it joins MUSIC - but it is tuned by its own family.
    CHECK (ctx.graph.strips[size_t (sax)].bus == MixBus::Music);
    CHECK (roleFamily (session.inputs.back().role) == RoleFamily::Saxophone);

    const auto& chain = plan.proposed.strips[size_t (sax)].channel;
    // A sustained source is never expanded: a horn player's breath between phrases is the player.
    CHECK (! chain.gateEnabled);
    // The high-pass sits under the horn and never above 0.8x of its lowest note.
    CHECK (chain.hpfEnabled);
    CHECK (chain.hpfHz <= 0.8f * 220.0f + 0.5f);
    // The honk is cut somewhere in the horn's own range rather than shelved away.
    bool honkCut = false;
    for (const auto& b : chain.correctiveBands)
        if (b.enabled && b.gainDb < -0.5f && b.freqHz >= 700.0f && b.freqHz <= 3000.0f) honkCut = true;
    CHECK (honkCut);
    // It is compressed like a horn, not like a piano.
    CHECK (chain.compEnabled);
    CHECK (chain.compRatio >= 2.5f);

    ctx.current = plan.proposed;
    const auto second = MixPlanner::plan (ctx);
    if (! second.noChangeRequired) dumpDifferences (plan, second);
    CHECK (second.noChangeRequired);
}

TEST_CASE ("MixPlanner: TUNE <GROUP> applies one group and leaves every other group and the master exactly where they are")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    const auto ctx = rig.context (cap);
    const auto full = MixPlanner::plan (ctx);
    REQUIRE (full.valid && full.headline == "MIX TUNED");

    const auto drums = MixPlanner::busOnly (full, MixBus::Drums, ctx.graph, ctx.session.profile);
    REQUIRE (drums.valid);
    CHECK (drums.headline == "DRUMS TUNED");
    CHECK (drums.parametersChanged > 0);
    CHECK (drums.parametersChanged < full.parametersChanged);
    int drumStrips = 0;
    for (int i = 0; i < full.before.numStrips; ++i)
    {
        const bool onDrums = ctx.graph.strips[size_t (i)].bus == MixBus::Drums;
        const auto& before = drums.before.strips[size_t (i)];
        const auto& after = drums.proposed.strips[size_t (i)];
        if (onDrums)
        {
            ++drumStrips;
            // Exactly what the full plan proposed for this strip: chain, gain, fader and sends.
            CHECK (diffParameters (after.channel, full.proposed.strips[size_t (i)].channel).empty());
            CHECK (after.faderDb == full.proposed.strips[size_t (i)].faderDb);
            CHECK (after.inputGainDb == full.proposed.strips[size_t (i)].inputGainDb);
        }
        else
        {
            CHECK (diffParameters (before.channel, after.channel).empty());
            CHECK (before.faderDb == after.faderDb);
            CHECK (before.inputGainDb == after.inputGainDb);
            for (int f = 0; f < int (FxSlot::Count); ++f) CHECK (before.sendDb[size_t (f)] == after.sendDb[size_t (f)]);
            CHECK (drums.strips[size_t (i)].faderDb == drums.strips[size_t (i)].faderBeforeDb);
        }
    }
    CHECK (drumStrips == 6);
    // The drum group's own chain comes along; every other group and the master do not.
    CHECK (diffParameters (drums.proposed.buses[size_t (MixBus::Drums)].channel, full.proposed.buses[size_t (MixBus::Drums)].channel).empty());
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        if (MixBus (b) == MixBus::Drums) continue;
        CHECK (diffParameters (drums.before.buses[size_t (b)].channel, drums.proposed.buses[size_t (b)].channel).empty());
        CHECK (drums.before.buses[size_t (b)].faderDb == drums.proposed.buses[size_t (b)].faderDb);
    }
    CHECK (! drums.reference.used);
    // Tuning the drums says nothing about the voices: its lines are the drum strips' own.
    for (const auto& r : drums.relationships) CHECK (r.what.find ("LEAD") == std::string::npos);

    // The pastor's microphone only heard the band, so TUNE SPEECH proposes nothing and says why.
    const auto speech = MixPlanner::busOnly (full, MixBus::Speech, ctx.graph, ctx.session.profile);
    REQUIRE (speech.valid);
    CHECK (speech.headline == "SPEECH WAS NOT HEARD");
    CHECK (speech.noChangeRequired);
    CHECK (MixPlanner::countParameterChanges (speech.before, speech.proposed) == 0);
    bool saidSpill = false;
    for (const auto& n : speech.notes) if (n.find ("rest of the stage") != std::string::npos) saidSpill = true;
    CHECK (saidSpill);

    // Keeping the drums and planning again on the same listen: the drums say NO CHANGE REQUIRED.
    MixPlanContext again = ctx;
    again.current = drums.proposed;
    const auto second = MixPlanner::busOnly (MixPlanner::plan (again), MixBus::Drums, ctx.graph, ctx.session.profile);
    CHECK (second.headline == "DRUMS: NO CHANGE REQUIRED");
}

TEST_CASE ("MixPlanner: keeping part of a plan applies exactly what was picked, by group or by channel")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    const auto ctx = rig.context (cap);
    const auto full = MixPlanner::plan (ctx);
    REQUIRE (full.valid);

    // Everything selected is the plan itself.
    const auto all = MixPlanner::restrictTo (full, MixPlanner::PlanSelection::all (full.before.numStrips), ctx.graph, ctx.session.profile);
    CHECK (all.headline == full.headline);
    CHECK (MixPlanner::countParameterChanges (all.proposed, full.proposed) == 0);
    CHECK (all.parametersChanged == full.parametersChanged);

    // Nothing selected changes nothing.
    const auto none = MixPlanner::restrictTo (full, MixPlanner::PlanSelection::none(), ctx.graph, ctx.session.profile);
    CHECK (none.noChangeRequired);
    CHECK (MixPlanner::countParameterChanges (none.proposed, full.before) == 0);

    // The drums and the master, but not the voices: the vocal strips and the vocal group are
    // exactly as they were, the drums and the master are exactly what was proposed.
    auto sel = MixPlanner::PlanSelection::group (ctx.graph, MixBus::Drums);
    sel.buses[size_t (MixBus::Master)] = true;
    const auto part = MixPlanner::restrictTo (full, sel, ctx.graph, ctx.session.profile);
    CHECK (part.headline == "MIX: DRUMS and MASTER KEPT");
    const int lead = stripIndex (full, "Lead");
    CHECK (diffParameters (part.before.strips[size_t (lead)].channel, part.proposed.strips[size_t (lead)].channel).empty());
    CHECK (part.before.strips[size_t (lead)].faderDb == part.proposed.strips[size_t (lead)].faderDb);
    CHECK (diffParameters (part.before.buses[size_t (MixBus::Vocals)].channel, part.proposed.buses[size_t (MixBus::Vocals)].channel).empty());
    const int kick = stripIndex (full, "Kick");
    CHECK (diffParameters (part.proposed.strips[size_t (kick)].channel, full.proposed.strips[size_t (kick)].channel).empty());
    CHECK (part.proposed.strips[size_t (kick)].faderDb == full.proposed.strips[size_t (kick)].faderDb);
    CHECK (diffParameters (part.proposed.master().channel, full.proposed.master().channel).empty());
    CHECK (part.proposed.tempoBpm == full.proposed.tempoBpm);
    CHECK (part.parametersChanged < full.parametersChanged);
    CHECK (part.parametersChanged > 0);

    // One channel on its own: the same promise channelOnly makes.
    MixPlanner::PlanSelection one;
    one.strips[size_t (lead)] = true;
    const auto solo = MixPlanner::restrictTo (full, one, ctx.graph, ctx.session.profile);
    CHECK (solo.headline == "MIX: VOCALS KEPT");
    const auto channel = MixPlanner::channelOnly (full, lead, ctx.session.profile);
    CHECK (MixPlanner::countParameterChanges (solo.proposed, channel.proposed) == 0);
    CHECK (solo.parametersChanged == channel.parametersChanged);
    CHECK (solo.fadersChanged == channel.fadersChanged);
}

namespace
{
    // The pastor speaks; the band is silent. Phrases of a voice-like tone with consonant-like
    // crackle, a room under them 14 dB down - a handheld in a live building. `spill` puts the
    // PA back into the overheads and the drum room, late, the way a real building does.
    testsig::Buffer sermonAudio (float spill = 0.0f)
    {
        testsig::Buffer in (15, int (kSr * 8));
        std::mt19937 rng (21);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        auto& c = in.data[14];
        for (size_t i = 0; i < c.size(); ++i)
        {
            const float t = float (i) / float (kSr);
            const float inPhrase = std::fmod (t, 0.6f);
            float s = 0.02f * dist (rng);                                  // the room, always there
            if (inPhrase < 0.35f)
            {
                const float env = 0.6f + 0.4f * std::sin (2.0f * float (M_PI) * 4.0f * t);   // syllables
                s += 0.09f * env * (std::sin (2.0f * float (M_PI) * 180.0f * t) + 0.4f * std::sin (2.0f * float (M_PI) * 360.0f * t) + 0.2f * std::sin (2.0f * float (M_PI) * 2400.0f * t));
                if (std::fmod (t, 0.125f) < 0.006f) s += 0.12f * dist (rng);               // a consonant
            }
            c[i] = s;
        }
        if (spill > 0.0f)
            for (size_t i = 480; i < c.size(); ++i)                         // 10 ms down the room
            {
                in.data[4][i] += spill * c[i - 480];
                in.data[5][i] += spill * c[i - 480];
                in.data[6][i] += spill * 1.4f * c[i - 480];
            }
        return in;
    }
}

TEST_CASE ("MixPlanner: a sermon microphone is lifted to its level, held gently, and never gated hard")
{
    Rig rig (band());
    auto in = sermonAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    const auto ctx = rig.context (cap);
    const auto plan = MixPlanner::plan (ctx);
    REQUIRE (plan.valid);
    const auto& pastor = stripNamed (plan, "Pastor");
    REQUIRE (pastor.heard);
    CHECK (! pastor.bleedOnly);
    CHECK (pastor.balanced);
    // The room under a sermon is not the band: the speech rule lets the pastor reach the level.
    CHECK (! pastor.spillLimited);
    const auto& strip = plan.proposed.strips[size_t (pastor.strip)];
    const float lands = MixPlanner::predictedProcessedActiveRmsDb (ctx, pastor.strip, strip) + strip.faderDb;
    CHECK_NEAR (lands, MixProfile::mixLevelTargetDb (ctx.session.profile, RoleFamily::Speech), 2.0f);
    // Held, not squashed: a gentle ratio, an attack that lets the start of a word through.
    const auto& t = StyleProfile::targets (ChannelRole::Speech, ctx.session.profile);
    if (strip.channel.compEnabled)
    {
        CHECK (strip.channel.compRatio <= t.compRatioMax + 0.01f);
        CHECK (strip.channel.compRatio <= 4.0f);
        CHECK (strip.channel.compAttackMs >= 8.0f);
        CHECK (strip.channel.compReleaseMs >= 100.0f);
    }
    // Any clean-up on a speech microphone is shallow.
    if (strip.channel.gateEnabled) CHECK (strip.channel.gateRangeDb <= 8.01f);

    // The same listen tuned as the speech group alone lands in the same place.
    const auto group = MixPlanner::busOnly (plan, MixBus::Speech, ctx.graph, ctx.session.profile);
    CHECK (group.headline == "SPEECH TUNED");
    CHECK (group.proposed.strips[size_t (pastor.strip)].faderDb == strip.faderDb);
}

TEST_CASE ("MixPlanner: a sermon never moves the master, and the song comes back at the level it left")
{
    // A service is a sequence of performances, and the listen only ever hears the one that is
    // happening. The master's density and its output level were fitted to the sum of a band;
    // re-fitting them to one voice is what makes a stream jump between the song and the sermon.
    Rig rig (band());
    auto song = bandAudio();
    const auto songCap = rig.listen (song);
    REQUIRE (songCap.valid);
    const auto songCtx = rig.context (songCap);
    const auto songPlan = MixPlanner::plan (songCtx);
    REQUIRE (songPlan.valid && songPlan.headline == "MIX TUNED");
    const float songDelivered = songCap.masterOutput.loudnessGatedLufs;

    for (float spill : { 0.0f, 0.06f })
    {
        // Keep the song's mix, then listen again while only the pastor speaks.
        Rig sermonRig (band());
        sermonRig.engine.setParameters (songPlan.proposed);
        auto speech = sermonAudio (spill);
        const auto cap = sermonRig.listen (speech);
        REQUIRE (cap.valid);
        MixPlanContext ctx = sermonRig.context (cap);
        ctx.current = songPlan.proposed;
        ctx.atCapture = songPlan.proposed;
        const auto plan = MixPlanner::plan (ctx);
        REQUIRE (plan.valid);

        // The master is exactly as the song left it: chain, trim and fader.
        CHECK (diffParameters (plan.before.master().channel, plan.proposed.master().channel).empty());
        CHECK (plan.before.master().faderDb == plan.proposed.master().faderDb);
        CHECK (plan.proposed.master().channel.outputTrimDb == songPlan.proposed.master().channel.outputTrimDb);
        bool saidSo = false;
        for (const auto& note : plan.notes) if (note.find ("left exactly as the band set it") != std::string::npos) saidSo = true;
        CHECK (saidSo);

        // The pastor was set by what leaves the mix, not by the profile's balance number.
        const auto& pastor = stripNamed (plan, "Pastor");
        REQUIRE (pastor.heard);
        CHECK (! pastor.bleedOnly);
        CHECK (pastor.balanced);
        CHECK (hasRelationship (plan, "set by what leaves the mix"));

        // The microphones that hear the building are not the band playing: during a sermon the
        // overheads and the drum room carry the PA, and their levels are left alone.
        for (const char* name : { "OH", "Room" })
        {
            const auto& mic = stripNamed (plan, name);
            if (! mic.heard) continue;
            CHECK (mic.bleedOnly);
            CHECK (plan.proposed.strips[size_t (mic.strip)].faderDb == plan.before.strips[size_t (mic.strip)].faderDb);
            CHECK (plan.proposed.strips[size_t (mic.strip)].inputGainDb == plan.before.strips[size_t (mic.strip)].inputGainDb);
        }

        // ... and the band, coming back through that untouched master, is where it was.
        Rig backRig (band());
        backRig.engine.setParameters (plan.proposed);
        auto again = bandAudio();
        const auto backCap = backRig.listen (again);
        REQUIRE (backCap.valid);
        CHECK_NEAR (backCap.masterOutput.loudnessGatedLufs, songDelivered, 1.0f);
    }
}

TEST_CASE ("MixPlanner: a listen with no performance in it is refused, and says what is wrong")
{
    // One channel stuck on a steady signal and nothing else playing: the QUEENSVIEW take's
    // 450 s window, where a kick channel sat at -2.5 dBFS with a 2.5 dB crest and drove the
    // master 7.5 dB up to meet it.
    Rig rig (band());
    testsig::Buffer in (15, int (kSr * 8));
    sine (in.data[0], 90.0f, 0.55f);
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    const auto ctx = rig.context (cap);
    const auto plan = MixPlanner::plan (ctx);
    REQUIRE (plan.valid);
    CHECK (plan.headline == "MIX: THAT WAS NOT A PERFORMANCE");
    CHECK (MixPlanner::countParameterChanges (plan.before, plan.proposed) == 0);
    CHECK (plan.proposed.master().channel.outputTrimDb == plan.before.master().channel.outputTrimDb);
    bool namedIt = false;
    for (const auto& note : plan.notes) if (note.find ("KICK") != std::string::npos) namedIt = true;
    CHECK (namedIt);

    // A band playing is never refused, and one sustained source among it is not a fault.
    Rig ok (band());
    auto playing = bandAudio();
    const auto bandCap = ok.listen (playing);
    const auto bandPlan = MixPlanner::plan (ok.context (bandCap));
    CHECK (bandPlan.headline == "MIX TUNED");
}

TEST_CASE ("Analysis: loudness is gated the way a delivery meter gates it")
{
    // Four seconds of tone and four of silence. Ungated, the gaps count as programme and the
    // figure lands about 3 dB low; gated, the silence is dropped and the number is the one a
    // broadcaster or a platform would report.
    Rig rig (band());
    testsig::Buffer in (15, int (kSr * 8));
    sine (in.data[10], 220.0f, 0.3f, 0.0f, 4.0f);
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    const auto& a = cap.strips[size_t (8)];        // "Lead"
    REQUIRE (a.valid);
    REQUIRE (a.loudnessGatedLufs > -100.0f);
    CHECK (a.loudnessGatedLufs > a.loudnessLufs + 1.5f);
    CHECK_NEAR (a.loudnessGatedLufs - a.loudnessLufs, 3.0f, 1.5f);
}

TEST_CASE ("MixPlanner: a balance is read over thirds of the listen, so one loud passage does not set a fader")
{
    // The same source at two levels: loud for a third of the listen, twelve dB quieter for the
    // rest. Read over the whole thirty seconds the answer lands between the two and follows
    // whichever part the listen happened to catch; read over thirds, the middle answer is the
    // one the source spends most of its time at.
    Rig rig (band());
    testsig::Buffer in (15, int (kSr * 9));
    bursts (in.data[0], 100.0f, 0.7f, 0.5f, 0.12f, 0.0f, false, 1);       // a kit, so the listen is a performance
    bursts (in.data[1], 0.0f, 0.5f, 0.5f, 0.05f, 0.25f, true, 2);
    sine (in.data[10], 220.0f, 0.40f, 0.0f, 3.0f);                         // lead: loud for the first third
    sine (in.data[10], 220.0f, 0.10f, 3.0f, 9.0f);                         // ... and quiet for the other two
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    const auto& lead = cap.strips[size_t (8)];
    REQUIRE (lead.valid);

    // The quiet two thirds are what this source mostly is, and that is what it measures as.
    const float loudDb = 20.0f * std::log10 (0.40f / std::sqrt (2.0f));
    const float quietDb = 20.0f * std::log10 (0.10f / std::sqrt (2.0f));
    CHECK (lead.activeRmsDb < 0.5f * (loudDb + quietDb));
    CHECK_NEAR (lead.activeRmsDb, quietDb, 2.0f);
    CHECK (lead.peakDb > loudDb);                     // the loud third is still the peak: nothing is hidden
}

TEST_CASE ("MixPlanner: a re-tune corrects a mix rather than rearranging it, and still says nothing changed on the same listen")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    auto ctx = rig.context (cap);
    const auto first = MixPlanner::plan (ctx);
    REQUIRE (first.valid && first.headline == "MIX TUNED");
    const auto& R = MixProfile::relationships (StyleProfileId::ModernGospel);

    // The same listen again with the plan running: still nothing to do. A listen always plans
    // the same way, which is why the flag belongs to the listen and not to the moment.
    MixPlanContext same = ctx;
    same.current = first.proposed;
    CHECK (MixPlanner::plan (same).noChangeRequired);

    // Now the mix somebody has pulled a long way off the plan, and a real listen through it.
    MixParameters pulled = first.proposed;
    for (int i = 0; i < pulled.numStrips; ++i)
        pulled.strips[size_t (i)].faderDb = std::clamp (pulled.strips[size_t (i)].faderDb - 12.0f, -60.0f, 12.0f);
    Rig again (band());
    again.engine.setParameters (pulled);
    auto more = bandAudio();
    const auto cap2 = again.listen (more);
    REQUIRE (cap2.valid);
    MixPlanContext moved = again.context (cap2);
    moved.current = pulled;
    moved.atCapture = pulled;
    moved.retune = true;

    const auto corrected = MixPlanner::plan (moved);
    REQUIRE (corrected.valid);
    int capped = 0;
    for (const auto& sp : corrected.strips)
    {
        if (! sp.balanced) continue;
        const float ran = pulled.strips[size_t (sp.strip)].faderDb;
        CHECK (std::fabs (sp.faderDb - ran) <= R.maxRetuneFaderStepDb + 0.01f);
        if (std::fabs (sp.faderDb - ran) >= R.maxRetuneFaderStepDb - 0.01f) ++capped;
    }
    CHECK (capped > 0);                                     // it really did want to move further
    bool saidSo = false;
    for (const auto& note : corrected.notes) if (note.find ("further than one re-tune") != std::string::npos) saidSo = true;
    CHECK (saidSo);

    // ... and planning that same listen again lands in exactly the same place.
    MixPlanContext twice = moved;
    twice.current = corrected.proposed;
    const auto third = MixPlanner::plan (twice);
    for (const auto& sp : third.strips)
        if (sp.balanced) CHECK_NEAR (sp.faderDb, corrected.strips[size_t (sp.strip)].faderDb, 0.01f);

    // A first mix is never held back: the same listen with retune off moves as far as it needs.
    MixPlanContext firstTime = moved;
    firstTime.retune = false;
    const auto unheld = MixPlanner::plan (firstTime);
    bool movedFurther = false;
    for (const auto& sp : unheld.strips)
        if (sp.balanced && std::fabs (sp.faderDb - pulled.strips[size_t (sp.strip)].faderDb) > R.maxRetuneFaderStepDb + 0.5f) movedFurther = true;
    CHECK (movedFurther);

    // And every plan says what it is about to do to the mix that is running.
    bool listedMoves = false;
    for (const auto& note : first.notes) if (note.find ("What moves, against the mix you have now") != std::string::npos) listedMoves = true;
    CHECK (listedMoves);
}

TEST_CASE ("MixPlanner: the groups are set against the voices, whatever the church has on the stage")
{
    // Two sessions, the same band, one with three backing voices and one with one. Per source
    // the numbers are identical; the VOCALS bus is not, and the groups have to follow it.
    auto planFor = [] (int backingVoices)
    {
        MixSession s = band();
        s.inputs.resize (size_t (9 + backingVoices));          // through "Lead", then the voices
        auto rig = std::make_unique<Rig> (s);
        auto in = bandAudio();
        const auto cap = rig->listen (in);
        REQUIRE (cap.valid);
        auto ctx = rig->context (cap);
        return std::make_pair (MixPlanner::plan (ctx), ctx);
    };

    const auto three = planFor (3);
    const auto one = planFor (1);
    REQUIRE (three.first.valid && one.first.valid);

    // The drum group sits where the profile says it should against the voices, and that is a
    // different fader in the two rooms because the voices are not the same sum.
    const auto& R = MixProfile::relationships (StyleProfileId::ModernGospel);
    CHECK (R.busBelowVocalsDb[size_t (MixBus::Drums)] != 0.0f);      // the table this rule exists to read
    CHECK (three.first.proposed.buses[size_t (MixBus::Drums)].faderDb
             != one.first.proposed.buses[size_t (MixBus::Drums)].faderDb);
    bool saidSo = false;
    for (const auto& r : three.first.relationships) if (r.what.find ("Groups set against") != std::string::npos) saidSo = true;
    CHECK (saidSo);

    // Every group fader stays a trim, and the reference group is never moved by the rule.
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        const float moveA = three.first.proposed.buses[size_t (b)].faderDb - three.first.before.buses[size_t (b)].faderDb;
        CHECK (std::fabs (moveA) <= R.maxBusFaderMoveDb + 0.01f);
    }
    CHECK (three.first.proposed.buses[size_t (MixBus::Vocals)].faderDb == three.first.before.buses[size_t (MixBus::Vocals)].faderDb);

    // ... and planning the same listen again moves nothing, group faders included.
    auto ctx2 = three.second;
    ctx2.current = three.first.proposed;
    const auto again = MixPlanner::plan (ctx2);
    CHECK (MixPlanner::countParameterChanges (again.proposed, three.first.proposed) == 0);
    for (int b = 0; b < int (MixBus::Count); ++b)
        CHECK_NEAR (again.proposed.buses[size_t (b)].faderDb, three.first.proposed.buses[size_t (b)].faderDb, 0.01f);
}

TEST_CASE ("MixPlanner: the mix is built around the focal source, pinned or measured")
{
    // Two lead microphones: one sung into, one lying open on a wedge hearing the stage. The
    // loudest is the wrong answer; the one that stands furthest above what it hears between
    // phrases is the right one.
    MixSession s = band();
    s.inputs.push_back ({ "Lead 2", ChannelRole::LeadVocal, 15, -1 });
    Rig rig (s);
    testsig::Buffer in (16, int (kSr * 8));
    bursts (in.data[0], 100.0f, 0.7f, 0.5f, 0.12f, 0.0f, false, 1);
    bursts (in.data[1], 0.0f, 0.5f, 0.5f, 0.05f, 0.25f, true, 2);
    sine (in.data[8], 262.0f, 0.15f); sine (in.data[8], 2600.0f, 0.2f);       // keys, bright
    sine (in.data[9], 330.0f, 0.15f); sine (in.data[9], 2800.0f, 0.2f);
    // The singer: quiet between phrases.
    for (size_t i = 0; i < in.data[10].size(); ++i)
    {
        const float t = float (i) / float (kSr);
        in.data[10][i] = (std::fmod (t, 1.0f) < 0.6f ? 0.25f : 0.002f) * std::sin (2.0f * float (M_PI) * 220.0f * t);
    }
    noise (in.data[15], 0.05f, 31);                                           // the spare: nothing but stage
    for (size_t i = 0; i < in.data[15].size(); ++i) in.data[15][i] += 0.06f * in.data[0][i] + 0.06f * in.data[1][i];

    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    auto ctx = rig.context (cap);
    const auto plan = MixPlanner::plan (ctx);
    REQUIRE (plan.valid);

    // The pocket the music makes is cut for the singer, and the mix says so about that one.
    CHECK (hasRelationship (plan, "Made room for the lead vocal in KEYS"));

    // Pinning the spare makes it the reference instead, and the mix says it was told to.
    ctx.session.setFocus (stripIndex (plan, "Lead 2"));
    CHECK (ctx.session.focusInput() == stripIndex (plan, "Lead 2"));
    const auto pinned = MixPlanner::plan (ctx);
    REQUIRE (pinned.valid);
    CHECK (hasRelationship (pinned, "is what this mix is built around"));

    // One input carries it at a time.
    ctx.session.setFocus (stripIndex (plan, "Lead"));
    int pins = 0;
    for (const auto& input : ctx.session.inputs) if (input.focus) ++pins;
    CHECK (pins == 1);
    ctx.session.setFocus (-1);
    CHECK (ctx.session.focusInput() == -1);
}
