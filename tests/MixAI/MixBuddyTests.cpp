// MIX BUDDY: help, not a second mixing engine.
//
// The answer is a pure function of the question and a copy of the session's state, so it is
// tested here with no engine and no clock. What these pin is the part a person relies on at
// the desk: the first thing on the signal path that explains a problem is the thing said, the
// same question about the same state gets the same answer, and the only button that can
// change the mix is the one that asks TUNE LIVE MIX for a proposal - offered only when there
// is something to propose from and LIVE SAFE is off.
#include "TestFramework.h"
#include "MixAI/MixBuddy.h"

using namespace livemix;

namespace
{
    BuddySnapshot aService()
    {
        BuddySnapshot s;
        s.running = true;
        s.heard = true;
        s.monitorOutput = true;
        s.limiterOn = true;
        auto strip = [] (const char* name, ChannelRole role, MixBus bus, int input)
        {
            BuddyStrip c;
            c.name = name; c.role = role; c.bus = bus; c.input = input;
            c.inputRmsDb = -24.0f; c.inputPeakDb = -12.0f; c.faderDb = 0.0f;
            return c;
        };
        s.strips = { strip ("Kick", ChannelRole::KickIn, MixBus::Drums, 1),
                     strip ("Bass", ChannelRole::BassDI, MixBus::Bass, 2),
                     strip ("Keys", ChannelRole::Piano, MixBus::Music, 3),
                     strip ("Lead", ChannelRole::LeadVocal, MixBus::Lead, 14),
                     strip ("Pastor", ChannelRole::Speech, MixBus::Speech, 15),
                     strip ("Crowd L", ChannelRole::CrowdMic, MixBus::Ambience, 20) };
        for (auto& g : s.groups) g.used = true;
        return s;
    }

    bool offers (const BuddyAnswer& a, BuddyActionKind k)
    {
        for (const auto& x : a.actions) if (x.kind == k) return true;
        return false;
    }
}

TEST_CASE ("Mix Buddy: the channel a question is about is found by number, by name and by what it is")
{
    const auto s = aService();
    CHECK (MixBuddy::findStrip ("why can't I hear channel 14?", s) == 3);      // the console's number is the input
    CHECK (MixBuddy::findStrip ("Pastor is too quiet", s) == 4);
    CHECK (MixBuddy::findStrip ("the preacher sounds muddy", s) == 4);
    CHECK (MixBuddy::findStrip ("why is my lead vocal buried", s) == 3);
    CHECK (MixBuddy::findStrip ("the congregation is too loud", s) == 5);
    CHECK (MixBuddy::findStrip ("how do I save?", s) == -1);
}

TEST_CASE ("Mix Buddy: why can't I hear it - the first thing on the path that explains it is the thing said")
{
    auto s = aService();

    // Healthy all the way: it says so, and points at masking and TUNE MIX, not at a fader.
    auto healthy = MixBuddy::answer ("why is my lead vocal quiet?", s);
    CHECK (healthy.text.find ("Nothing on Lead's path") != std::string::npos);
    CHECK (offers (healthy, BuddyActionKind::RunTuneMix));
    CHECK (offers (healthy, BuddyActionKind::OpenInspector));

    // Nothing arriving is a source problem, said before anything DLIVE does.
    auto dead = s;
    dead.strips[3].inputRmsDb = -110.0f;
    dead.strips[3].mute = true;                       // ... even when it is muted as well
    CHECK (MixBuddy::answer ("why can't I hear channel 14", dead).text.find ("no signal arriving at input 14") != std::string::npos);

    auto muted = s;
    muted.strips[3].mute = true;
    CHECK (MixBuddy::answer ("why can't I hear the lead", muted).text.find ("is muted") != std::string::npos);

    auto group = s;
    group.groups[size_t (MixBus::Lead)].mute = true;
    CHECK (MixBuddy::answer ("why can't I hear the lead", group).text.find ("LEAD group is muted") != std::string::npos);

    // The emergency key outranks everything on the channel.
    auto air = muted;
    air.broadcastMute = true;
    CHECK (MixBuddy::answer ("why can't I hear the lead", air).text.find ("MUTE is on") != std::string::npos);

    // Speech priority doing its job is named as such, for the band only.
    auto ducked = s;
    ducked.speechPriority = true;
    ducked.speechDuckDb = -4.0f;
    CHECK (MixBuddy::answer ("why are the keys so quiet", ducked).text.find ("Speech priority") != std::string::npos);
    CHECK (MixBuddy::answer ("why is the pastor quiet", ducked).text.find ("Speech priority") == std::string::npos);

    // A compressor squashing it.
    auto squashed = s;
    squashed.strips[4].compOn = true;
    squashed.strips[4].compReductionDb = 11.0f;
    const auto sq = MixBuddy::answer ("why is the pastor quiet", squashed);
    CHECK (sq.text.find ("compressor is taking 11.0 dB") != std::string::npos);
    CHECK (offers (sq, BuddyActionKind::RunTuneChannel));

    // The same question about the same state is the same answer.
    CHECK (MixBuddy::answer ("why is the pastor quiet", squashed).text == sq.text);
}

TEST_CASE ("Mix Buddy: clipping is found where it happens")
{
    auto s = aService();
    s.strips[0].clipped = true;
    const auto at = MixBuddy::answer ("why is my mix clipping?", s);
    CHECK (at.text.find ("at the inputs: Kick") != std::string::npos);
    CHECK (offers (at, BuddyActionKind::OpenCheckInputs));

    auto noLimiter = aService();
    noLimiter.limiterOn = false;
    CHECK (MixBuddy::answer ("the master is distorting", noLimiter).text.find ("limiter is off") != std::string::npos);
}

TEST_CASE ("Mix Buddy: it never changes the mix, and the one button that can propose a change is offered only when it may")
{
    auto s = aService();
    for (const auto& q : MixBuddy::examples())
    {
        const auto a = MixBuddy::answer (q, s);
        CHECK_MESSAGE (! a.notUnderstood, "An example Mix Buddy shows was not understood: " + q);
        CHECK_MESSAGE (! a.text.empty(), "No answer for: " + q);
        // Help never comes with a proposal attached: a question is not a request.
        CHECK_MESSAGE (! offers (a, BuddyActionKind::AskForChange), "A help answer offered a change: " + q);
    }

    const auto asked = MixBuddy::answer ("turn the lead up", s);
    CHECK (asked.text.find ("does not change the mix by itself") != std::string::npos);
    CHECK (offers (asked, BuddyActionKind::AskForChange));

    auto locked = s;
    locked.liveSafe = true;
    CHECK (! offers (MixBuddy::answer ("turn the lead up", locked), BuddyActionKind::AskForChange));
    auto unheard = s;
    unheard.heard = false;
    CHECK (! offers (MixBuddy::answer ("turn the lead up", unheard), BuddyActionKind::AskForChange));

    // What it does not follow, it says so rather than guessing.
    CHECK (MixBuddy::answer ("purple elephant", s).notUnderstood);
}

TEST_CASE ("Mix Buddy: how-to answers name DLIVE's own controls")
{
    const auto s = aService();
    CHECK (MixBuddy::answer ("How do I save this mix?", s).text.find ("Cmd-S") != std::string::npos);
    CHECK (MixBuddy::answer ("How do I create a BGV bus?", s).text.find ("does not make new buses") != std::string::npos);
    CHECK (MixBuddy::answer ("what's the difference between tune mix and autopilot", s).text.find ("TUNE MIX improves the mix") == 0);
    CHECK (MixBuddy::answer ("How do I solo a bus?", s).text.find ("headphones only") != std::string::npos);
    CHECK (MixBuddy::answer ("why isn't my sample triggering", s).text.find ("Sensitivity") != std::string::npos);
    CHECK (MixBuddy::answer ("How do I change my audio interface?", s).text.find ("Audio device") != std::string::npos);
    CHECK (MixBuddy::answer ("How do I restore yesterday's session?", s).text.find ("Open Session") != std::string::npos);
    CHECK (MixBuddy::answer ("Why does my stream sound quieter than other streams?", s).text.find ("LUFS") != std::string::npos);
}
