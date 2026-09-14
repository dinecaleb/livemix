#include "TestFramework.h"
#include "Mix/RoutingGraph.h"
#include "Mix/MixPlanner.h"
#include "Profiles/MixProfileData.h"
#include "Profiles/StyleProfile.h"
#include "Core/Constants.h"
#include <cmath>

using namespace livemix;

namespace
{
    MixSession churchSession()
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
}

TEST_CASE ("RoutingGraph: every source lands on its bus without the user building anything")
{
    const auto g = RoutingGraph::build (churchSession());
    REQUIRE (g.numStrips() == 13);
    CHECK (g.stripsOnBus (MixBus::Drums) == 6);
    CHECK (g.stripsOnBus (MixBus::Bass) == 1);
    CHECK (g.stripsOnBus (MixBus::Music) == 1);
    // The pastor is not one of the singers: the four voices are on VOCALS, the speaking
    // microphone has the SPEECH group to itself.
    CHECK (g.stripsOnBus (MixBus::Vocals) == 4);
    CHECK (g.stripsOnBus (MixBus::Speech) == 1);
    CHECK (g.strips[12].bus == MixBus::Speech);
    CHECK (g.stripsOnBus (MixBus::Master) == 0);
    for (int b = 0; b < int (MixBus::Count); ++b) CHECK (g.busUsed[size_t (b)]);
    CHECK (g.strips[4].numChannels() == 2);   // overhead pair
    CHECK (g.strips[7].numChannels() == 2);   // keys
    CHECK (g.strips[0].numChannels() == 1);
}

TEST_CASE ("RoutingGraph: FX returns exist only when something sends to them, and the sends follow the profile")
{
    const auto g = RoutingGraph::build (churchSession());
    CHECK (g.fxUsed[size_t (FxSlot::VocalPlate)]);
    CHECK (g.fxUsed[size_t (FxSlot::VocalDelay)]);
    CHECK (g.fxUsed[size_t (FxSlot::BgvHall)]);
    CHECK (g.fxUsed[size_t (FxSlot::SnarePlate)]);
    CHECK (g.fxUsed[size_t (FxSlot::DrumRoom)]);
    // The lead sends to the plate and the delay, never to the backing hall; the pastor stays dry.
    const auto& lead = g.strips[8];
    CHECK (lead.sendDb[size_t (FxSlot::VocalPlate)] > kSilenceDb);
    CHECK (lead.sendDb[size_t (FxSlot::VocalDelay)] > kSilenceDb);
    CHECK (lead.sendDb[size_t (FxSlot::BgvHall)] <= kSilenceDb);
    const auto& pastor = g.strips[12];
    for (float s : pastor.sendDb) CHECK (s <= kSilenceDb);

    // A drums-only session builds no vocal returns.
    MixSession drums;
    drums.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Snare", ChannelRole::SnareTop, 1, -1 } };
    const auto d = RoutingGraph::build (drums);
    CHECK (d.fxUsed[size_t (FxSlot::SnarePlate)]);
    CHECK (! d.fxUsed[size_t (FxSlot::VocalPlate)]);
    CHECK (! d.fxUsed[size_t (FxSlot::BgvHall)]);
    CHECK (! d.busUsed[size_t (MixBus::Vocals)]);
    CHECK (! d.busUsed[size_t (MixBus::Speech)]);   // nobody is speaking: no speech group
    CHECK (d.busUsed[size_t (MixBus::Master)]);
}

TEST_CASE ("RoutingGraph: several backing vocals are spread across the image, the lead and kick stay centred")
{
    const auto g = RoutingGraph::build (churchSession());
    CHECK (g.strips[0].pan == 0.0f);          // kick
    CHECK (g.strips[8].pan == 0.0f);          // lead
    CHECK (g.strips[9].pan < -0.3f);          // Vox 1 left
    CHECK_NEAR (g.strips[10].pan, 0.0f, 1e-6); // Vox 2 centre
    CHECK (g.strips[11].pan > 0.3f);          // Vox 3 right
    CHECK (g.strips[2].pan < 0.0f);           // rack tom left of centre
    CHECK (g.strips[3].pan > 0.0f);           // floor tom right of centre
    CHECK (g.strips[4].pan == 0.0f);          // a stereo pair is not panned
}

TEST_CASE ("RoutingGraph: disabled, unpatched and master-role inputs are skipped; the graph is deterministic")
{
    MixSession s = churchSession();
    s.inputs[1].enabled = false;
    s.inputs[2].inputA = -1;
    s.inputs.push_back ({ "Oops", ChannelRole::MasterBroadcast, 20, -1 });
    const auto g = RoutingGraph::build (s);
    CHECK (g.numStrips() == 11);
    const auto h = RoutingGraph::build (s);
    CHECK (g.describe() == h.describe());
    CHECK (g.describe().find ("Kick") != std::string::npos);
}

TEST_CASE ("RoutingGraph: the starting point is the profile baseline for every strip, bus and return")
{
    const auto s = churchSession();
    const auto g = RoutingGraph::build (s);
    const auto p = startingPoint (s, g);
    REQUIRE (p.numStrips == 13);
    const auto kick = StyleProfile::baseline (ChannelRole::KickIn, StyleProfileId::ModernGospel);
    CHECK (p.strips[0].channel.hpfHz == kick.hpfHz);
    CHECK (p.strips[0].channel.compEnabled == kick.compEnabled);
    CHECK (p.strips[0].faderDb == 0.0f);
    const auto master = StyleProfile::baseline (ChannelRole::MasterBroadcast, StyleProfileId::ModernGospel);
    CHECK (p.master().channel.limiterEnabled);
    CHECK (p.master().channel.limiterCeilingDb == master.limiterCeilingDb);
    CHECK (p.buses[size_t (MixBus::Drums)].channel.compEnabled);
    for (int f = 0; f < int (FxSlot::Count); ++f)
    {
        CHECK (p.fx[size_t (f)].fx.mix == 1.0f);
        CHECK (p.fx[size_t (f)].enabled == g.fxUsed[size_t (f)]);
    }
    CHECK (p.fx[size_t (FxSlot::VocalDelay)].fx.delayEnabled);
    CHECK (p.fx[size_t (FxSlot::VocalPlate)].fx.reverbEnabled);
    CHECK (! p.bypassProcessing);
}

TEST_CASE ("RoutingGraph: a kit of toms walks across the image and nothing is thrown at the wall")
{
    // Three toms and a pair of mono overheads, the way a desk exports them: two of the toms share a role.
    // Spreading per role added the spread to a home position that was already off centre, so the second
    // floor tom landed hard right - a drum nobody pans there, and in mono it comes back changed.
    MixSession s;
    s.profile = StyleProfileId::ModernGospel;
    s.inputs = {
        { "Kick",  ChannelRole::KickIn,         0, -1 },
        { "Floor", ChannelRole::FloorTom,       1, -1 },
        { "Tom 1", ChannelRole::RackTom,        2, -1 },
        { "Tom 2", ChannelRole::FloorTom,       3, -1 },
        { "OV L",  ChannelRole::OverheadLeft,   4, -1 },
        { "OV R",  ChannelRole::OverheadRight,  5, -1 },
    };
    const auto g = RoutingGraph::build (s);
    REQUIRE (g.numStrips() == 6);
    auto pan = [&g] (const char* name)
    {
        for (const auto& st : g.strips) if (st.name == name) return st.pan;
        return 99.0f;
    };
    CHECK (pan ("Kick") == 0.0f);
    for (const char* t : { "Floor", "Tom 1", "Tom 2" }) CHECK (std::fabs (pan (t)) <= 0.75f);
    CHECK (pan ("Tom 1") < pan ("Floor"));      // the rack tom sits left of the floor toms
    CHECK (pan ("Floor") < pan ("Tom 2"));      // and the kit walks left to right
    CHECK_NEAR (pan ("OV L"), -0.9f, 1e-5);     // a pair of overheads is wide, not against the wall
    CHECK_NEAR (pan ("OV R"),  0.9f, 1e-5);
    CHECK (RoutingGraph::build (s).describe() == g.describe());
}

// ---------------------------------------------------------------------------
// Rearranging the channels. A track, a mixer strip and an input are one thing seen three
// ways, so moving one has to move all of them - and cost nothing. These are the tests for
// the rule that decides which input is which after the list has changed shape.
// ---------------------------------------------------------------------------
TEST_CASE ("MixSession: an input is identified by the channel it arrives on, not by its place in the list")
{
    const auto before = churchSession();

    // Reordered: the pastor moved to the top, the lead vocal to second.
    MixSession after = before;
    auto pastor = after.inputs[12];
    auto lead = after.inputs[8];
    after.inputs.erase (after.inputs.begin() + 12);
    after.inputs.erase (after.inputs.begin() + 8);
    after.inputs.insert (after.inputs.begin(), lead);
    after.inputs.insert (after.inputs.begin(), pastor);

    const auto match = matchInputs (before, after);
    REQUIRE (match.size() == after.inputs.size());
    CHECK (match[0] == 12);         // Pastor
    CHECK (match[1] == 8);          // Lead
    CHECK (match[2] == 0);          // Kick, where it always was
    for (size_t i = 0; i < match.size(); ++i)
        CHECK (before.inputs[size_t (match[i])].inputA == after.inputs[i].inputA);

    // An input that is new to the session has nothing to have been.
    MixSession added = before;
    added.inputs.push_back ({ "Horn", ChannelRole::SynthPad, 20, -1 });
    const auto grew = matchInputs (before, added);
    CHECK (grew.back() == -1);
    for (size_t i = 0; i + 1 < grew.size(); ++i) CHECK (grew[i] == int (i));

    // A rename does not lose an input: the device channel is the identity.
    MixSession renamed = before;
    renamed.inputs[8].name = "Pastor Mike";
    const auto still = matchInputs (before, renamed);
    CHECK (still[8] == 8);

    // A re-patched input with the same name is found by the name.
    MixSession repatched = before;
    repatched.inputs[8].inputA = 31;
    const auto byName = matchInputs (before, repatched);
    CHECK (byName[8] == 8);
}

TEST_CASE ("carryMix: moving a channel keeps every chain, level and send with its own input")
{
    const auto before = churchSession();
    const auto graph = RoutingGraph::build (before);
    MixParameters mix = startingPoint (before, graph);

    // A mix worth losing: the lead vocal tuned and up, the kick down, a send opened.
    mix.strips[8].faderDb = 3.5f;
    mix.strips[8].channel.compThresholdDb = -21.5f;
    mix.strips[8].sendDb[size_t (FxSlot::VocalPlate)] = -6.0f;
    mix.strips[0].faderDb = -2.5f;
    mix.strips[0].inputGainDb = 6.0f;
    mix.strips[12].mute = true;
    mix.buses[size_t (MixBus::Drums)].channel.compRatio = 3.3f;
    mix.master().channel.outputTrimDb = -4.0f;
    mix.tempoBpm = 96.0f;

    // The pastor is moved to the top of the list.
    MixSession after = before;
    auto pastor = after.inputs[12];
    after.inputs.erase (after.inputs.begin() + 12);
    after.inputs.insert (after.inputs.begin(), pastor);

    const auto baseline = startingPoint (after, RoutingGraph::build (after));
    const auto carried = carryMix (mix, before, baseline, after);

    REQUIRE (carried.numStrips == mix.numStrips);
    CHECK (carried.strips[0].mute);                                   // the pastor, now channel 1
    CHECK_NEAR (carried.strips[9].faderDb, 3.5f, 0.001f);             // the lead, pushed down one
    CHECK_NEAR (carried.strips[9].channel.compThresholdDb, -21.5f, 0.001f);
    CHECK_NEAR (carried.strips[9].sendDb[size_t (FxSlot::VocalPlate)], -6.0f, 0.001f);
    CHECK_NEAR (carried.strips[1].faderDb, -2.5f, 0.001f);            // the kick
    CHECK_NEAR (carried.strips[1].inputGainDb, 6.0f, 0.001f);
    CHECK (! carried.strips[1].mute);

    // Nothing that belongs to the mix as a whole is disturbed by a channel moving.
    CHECK_NEAR (carried.buses[size_t (MixBus::Drums)].channel.compRatio, 3.3f, 0.001f);
    CHECK_NEAR (carried.master().channel.outputTrimDb, -4.0f, 0.001f);
    CHECK_NEAR (carried.tempoBpm, 96.0f, 0.001f);

    // An input that is new to the session starts from its own baseline, not from silence or
    // from whatever happened to be at that index.
    MixSession added = after;
    added.inputs.push_back ({ "Horn", ChannelRole::SynthPad, 20, -1 });
    const auto grown = startingPoint (added, RoutingGraph::build (added));
    const auto withHorn = carryMix (mix, before, grown, added);
    REQUIRE (withHorn.numStrips == added.numStrips());
    CHECK (diffParameters (withHorn.strips[size_t (added.inputs.size() - 1)].channel,
                           grown.strips[size_t (added.inputs.size() - 1)].channel).empty());
    CHECK (withHorn.strips[0].mute);                                  // and the rest still followed

    // An input that became a different source does not keep a chain built for the old one.
    MixSession recast = before;
    recast.inputs[0].role = ChannelRole::LeadVocal;
    const auto recastBase = startingPoint (recast, RoutingGraph::build (recast));
    const auto afterRecast = carryMix (mix, before, recastBase, recast);
    CHECK_NEAR (afterRecast.strips[0].faderDb, recastBase.strips[0].faderDb, 0.001f);
    CHECK (diffParameters (afterRecast.strips[0].channel, recastBase.strips[0].channel).empty());
    CHECK_NEAR (afterRecast.strips[8].faderDb, 3.5f, 0.001f);         // everyone else is untouched

    // Carrying a mix onto the session it already belongs to changes nothing at all.
    const auto same = carryMix (mix, before, startingPoint (before, graph), before);
    CHECK (MixPlanner::countParameterChanges (same, mix) == 0);
    for (int i = 0; i < mix.numStrips; ++i)
        CHECK_NEAR (same.strips[size_t (i)].faderDb, mix.strips[size_t (i)].faderDb, 0.001f);
}
