// AUTOPILOT: the operator's own mix, held where they left it.
//
// The decision is a pure function, so this is where it is really tested: no engine, no audio,
// no clock. The most important test in this file is the first one, and it is the one that
// asserts nothing happens - because a mix drifts by a decibel all morning and that is a mix,
// not a fault, and an autopilot that chases it is worse than no autopilot at all.
#include "TestFramework.h"
#include "Mix/Autopilot.h"
#include <cmath>

using namespace livemix;

namespace
{
    // A church mix, an hour into a Sunday: the master at -12, the groups where the engineer
    // left them. The numbers are dB against the master, which is the only thing Autopilot
    // holds - never an absolute level, because "the drums at -6" means nothing an hour later.
    AutopilotTarget aMix()
    {
        AutopilotTarget t;
        t.valid = true;
        const struct { MixBus bus; float below; } set[] = {
            { MixBus::Drums, -4.0f }, { MixBus::Bass, -6.0f }, { MixBus::Music, -8.0f },
            { MixBus::Lead, -3.0f }, { MixBus::Vocals, -6.0f }, { MixBus::Speech, -3.0f },
            { MixBus::Ambience, -15.0f }
        };
        for (const auto& s : set)
        {
            t.busBelowMasterDb[size_t (s.bus)] = s.below;
            t.measured[size_t (s.bus)] = true;
            t.playing[size_t (s.bus)] = true;
        }
        return t;
    }

    // What the meters say when the mix is exactly where it was left.
    AutopilotReading steady (const AutopilotTarget& t, float masterRms = -12.0f)
    {
        AutopilotReading r;
        r.masterRmsDb = masterRms;
        r.masterShortLufs = -14.0f;
        for (int b = 0; b < int (MixBus::Master); ++b)
        {
            if (! t.measured[size_t (b)]) continue;
            r.busRmsDb[size_t (b)] = masterRms + t.busBelowMasterDb[size_t (b)];
            r.busActive[size_t (b)] = true;
        }
        return r;
    }

    std::array<float, int (MixBus::Count)> nothingMoved() { return {}; }
    std::array<bool, int (MixBus::Count)> nothingHeld() { return {}; }

    const AutopilotMove* moveFor (const std::vector<AutopilotMove>& moves, MixBus bus)
    {
        for (const auto& m : moves) if (m.bus == bus) return &m;
        return nullptr;
    }
}

TEST_CASE ("Autopilot: within tolerance it does nothing, and that is the usual answer")
{
    const AutopilotLimits limits;
    const auto target = aMix();

    // Exactly where it was left.
    CHECK (Autopilot::decide (target, steady (target), nothingMoved(), nothingHeld(), limits).empty());

    // The whole band got louder together - which is a band, not a fault. Every group moved
    // with the master, so nothing drifted against it and nothing happens.
    CHECK (Autopilot::decide (target, steady (target, -8.0f), nothingMoved(), nothingHeld(), limits).empty());

    // A group drifting inside the tolerance is a mix, not a fault.
    for (float drift : { -1.9f, -1.0f, -0.2f, 0.5f, 1.9f })
    {
        auto now = steady (target);
        now.busRmsDb[size_t (MixBus::Lead)] += drift;
        CHECK_MESSAGE (Autopilot::decide (target, now, nothingMoved(), nothingHeld(), limits).empty(),
                       "Autopilot moved a fader for a drift of " + std::to_string (drift) + " dB");
    }

    // No target, no decision.
    AutopilotTarget none;
    CHECK (Autopilot::decide (none, steady (target), nothingMoved(), nothingHeld(), limits).empty());

    // Nothing playing: the sermon, and between the songs. Doing nothing is the right answer.
    AutopilotReading silent;
    CHECK (Autopilot::decide (target, silent, nothingMoved(), nothingHeld(), limits).empty());

    // A group that is not playing is not a group that has drifted.
    auto quiet = steady (target);
    quiet.busRmsDb[size_t (MixBus::Speech)] = -70.0f;     // the pastor is not speaking
    CHECK (moveFor (Autopilot::decide (target, quiet, nothingMoved(), nothingHeld(), limits), MixBus::Speech) == nullptr);
}

TEST_CASE ("Autopilot: what it does when the lead steps back, and how far it will go")
{
    const AutopilotLimits limits;
    const auto target = aMix();

    // The lead has stepped away from the microphone: 3 dB down against the rest of the mix.
    auto now = steady (target);
    now.busRmsDb[size_t (MixBus::Lead)] -= 3.0f;
    const auto moves = Autopilot::decide (target, now, nothingMoved(), nothingHeld(), limits);
    const auto* lead = moveFor (moves, MixBus::Lead);
    REQUIRE (lead != nullptr);

    // THE SMALLEST MOVE THAT HELPS, in the right direction, never more than one step.
    CHECK (lead->deltaDb > 0.0f);
    CHECK_NEAR (lead->deltaDb, limits.maxStepDb, 0.001f);
    // ...and it says what it noticed in the words a person reads on Monday.
    CHECK (lead->what.find ("Autopilot: LEAD") == 0);
    CHECK (lead->why.find ("The lead fell below") == 0);
    // Nothing else moved: one group drifted, one group is corrected.
    CHECK (moves.size() == 1u);

    // BOUNDED, FOR GOOD. Once it has spent its travel on a group it stops, however far the
    // mix goes - Autopilot holds a mix, it does not rescue one.
    auto spent = nothingMoved();
    spent[size_t (MixBus::Lead)] = limits.maxTotalDb;
    auto gone = steady (target);
    gone.busRmsDb[size_t (MixBus::Lead)] -= 20.0f;
    CHECK (moveFor (Autopilot::decide (target, gone, spent, nothingHeld(), limits), MixBus::Lead) == nullptr);

    // Part-spent: it moves only what is left, never past the bound.
    spent[size_t (MixBus::Lead)] = limits.maxTotalDb - 0.2f;
    const auto last = Autopilot::decide (target, gone, spent, nothingHeld(), limits);
    REQUIRE (moveFor (last, MixBus::Lead) != nullptr);
    CHECK_NEAR (moveFor (last, MixBus::Lead)->deltaDb, 0.2f, 0.001f);

    // The other direction works the same way: the backing voices found their confidence.
    auto loudBgv = steady (target);
    loudBgv.busRmsDb[size_t (MixBus::Vocals)] += 4.0f;
    const auto down = Autopilot::decide (target, loudBgv, nothingMoved(), nothingHeld(), limits);
    REQUIRE (moveFor (down, MixBus::Vocals) != nullptr);
    CHECK (moveFor (down, MixBus::Vocals)->deltaDb < 0.0f);
    CHECK (moveFor (down, MixBus::Vocals)->why.find ("over the lead") != std::string::npos);
}

TEST_CASE ("Autopilot: hysteresis, so a group on the boundary is not nudged all morning")
{
    const AutopilotLimits limits;
    const auto target = aMix();

    // 2.5 dB out: past the tolerance, so it starts holding this group.
    auto out = steady (target);
    out.busRmsDb[size_t (MixBus::Drums)] += 2.5f;
    CHECK (moveFor (Autopilot::decide (target, out, nothingMoved(), nothingHeld(), limits), MixBus::Drums) != nullptr);

    // Now it is holding it. Back to 1.5 dB out - inside the tolerance, but not *well* inside -
    // and it keeps holding rather than letting go and grabbing again a second later.
    auto held = nothingHeld();
    held[size_t (MixBus::Drums)] = true;
    auto nearly = steady (target);
    nearly.busRmsDb[size_t (MixBus::Drums)] += 1.5f;
    CHECK (moveFor (Autopilot::decide (target, nearly, nothingMoved(), held, limits), MixBus::Drums) != nullptr);

    // Well inside: it lets go.
    auto settled = steady (target);
    settled.busRmsDb[size_t (MixBus::Drums)] += 0.5f;
    CHECK (moveFor (Autopilot::decide (target, settled, nothingMoved(), held, limits), MixBus::Drums) == nullptr);
}

TEST_CASE ("Autopilot: it never touches anything but a group fader")
{
    const AutopilotLimits limits;
    const auto target = aMix();

    // Every group as wrong as it can be, in both directions, and the master too.
    auto wrong = steady (target);
    for (int b = 0; b < int (MixBus::Master); ++b) wrong.busRmsDb[size_t (b)] += (b % 2 == 0 ? 9.0f : -9.0f);
    wrong.clipping = true;
    wrong.masterShortLufs = 0.0f;

    for (const auto& m : Autopilot::decide (target, wrong, nothingMoved(), nothingHeld(), limits))
    {
        // The master is never one of them: it is what everyone hears, and it is the thing
        // every other group is measured against.
        CHECK_MESSAGE (m.bus != MixBus::Master, "Autopilot proposed a move on the master");
        CHECK (int (m.bus) >= 0);
        CHECK (int (m.bus) < int (MixBus::Master));
        // No move is ever bigger than one step, whatever it saw.
        CHECK (std::fabs (m.deltaDb) <= limits.maxStepDb + 0.001f);
        // ...and every one of them carries its sentence.
        CHECK (! m.what.empty());
        CHECK (! m.why.empty());
    }

    // The drift a panel prints and the drift the decision uses are one number.
    auto now = steady (target);
    now.busRmsDb[size_t (MixBus::Music)] -= 3.5f;
    CHECK_NEAR (Autopilot::driftDb (target, now, MixBus::Music), -3.5f, 0.001f);
    CHECK_NEAR (Autopilot::driftDb (target, now, MixBus::Master), 0.0f, 0.001f);   // not a group
}

TEST_CASE ("Autopilot: a different arrangement is not a drift, and it holds still until the one it knows comes back")
{
    const AutopilotLimits limits;
    auto target = aMix();
    target.playing[size_t (MixBus::Speech)] = false;          // learnt during the worship set
    target.measured[size_t (MixBus::Speech)] = false;

    // The band stops and the pastor starts: the lead's group reads far above where it was
    // against the (now much smaller) mix, and pulling it down would be exactly wrong.
    auto sermon = steady (target);
    sermon.busActive[size_t (MixBus::Drums)] = false;
    sermon.busActive[size_t (MixBus::Bass)] = false;
    sermon.busActive[size_t (MixBus::Music)] = false;
    sermon.busActive[size_t (MixBus::Speech)] = true;
    sermon.busRmsDb[size_t (MixBus::Lead)] += 8.0f;
    CHECK (Autopilot::decide (target, sermon, nothingMoved(), nothingHeld(), limits).empty());

    // A verse with the drums out is the same: a different mix, not a louder lead.
    auto verse = steady (target);
    verse.busActive[size_t (MixBus::Drums)] = false;
    verse.busRmsDb[size_t (MixBus::Lead)] += 4.0f;
    CHECK (Autopilot::decide (target, verse, nothingMoved(), nothingHeld(), limits).empty());

    // The arrangement it learnt comes back, and so does the holding.
    auto chorus = steady (target);
    chorus.busRmsDb[size_t (MixBus::Lead)] -= 4.0f;
    CHECK (moveFor (Autopilot::decide (target, chorus, nothingMoved(), nothingHeld(), limits), MixBus::Lead) != nullptr);
}

TEST_CASE ("Autopilot: a reading that is not a number moves nothing")
{
    const AutopilotLimits limits;
    const auto target = aMix();
    auto broken = steady (target);
    broken.busRmsDb[size_t (MixBus::Lead)] = std::nanf ("");
    CHECK (moveFor (Autopilot::decide (target, broken, nothingMoved(), nothingHeld(), limits), MixBus::Lead) == nullptr);
    auto noMix = steady (target);
    noMix.masterRmsDb = std::nanf ("");
    CHECK (Autopilot::decide (target, noMix, nothingMoved(), nothingHeld(), limits).empty());
    CHECK_NEAR (Autopilot::driftDb (target, broken, MixBus::Lead), 0.0f, 0.001f);
}
