#pragma once
#include <array>
#include <string>
#include <vector>
#include "MixSession.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// AUTOPILOT
//
// The second thing in DLIVE allowed to move a level by itself, and the rules it lives under
// are in CLAUDE.md because they are not negotiable: deterministic (there is no AI anywhere in
// this file), off by default, engaged only on purpose, group faders only, bounded to a few dB
// of the mix it was engaged on, and every move a Mix history entry with the sentence that
// says why. It never touches a channel, a chain, the master fader, the returns, the room or
// the engineer's listen.
//
// WHAT IT IS FOR. A volunteer sets a mix at 9:30 and then has a camera to run, a laptop to
// watch and a door to answer. By 10:20 the band has got louder, the lead has stepped back
// from the microphone and the backing voices have found their confidence - and nobody is at
// the desk. Autopilot is not a mixing engine: it is the operator's own mix, held where they
// left it, by the smallest move that will do it.
//
// So it works in *relationships*, exactly as the planner does: where each group sat against
// the master at the moment it was engaged. It never aims at an absolute level, because "the
// drums at -6" means nothing an hour later - what the engineer set was the drums *against the
// rest of it*, and that is the thing to hold.
//
// This header is the whole decision. It is a pure function of a target, a reading and what
// has already been moved, so it is tested with no engine, no audio and no clock, and
// MixController's only job is to read the meters, call it, and apply what comes back.
// ---------------------------------------------------------------------------

// Where the mix sat when Autopilot was engaged. Measured, never read off a fader: a fader at
// -6 dB means nothing without knowing what arrived at it.
struct AutopilotTarget
{
    bool valid = false;
    std::array<float, int (MixBus::Count)> busBelowMasterDb {};   // each group against the master, dB
    std::array<bool, int (MixBus::Count)> measured {};
    float deliveryLufs = -14.0f;        // what the session is aiming the master at
    float deliveryToleranceLu = 1.0f;
};

// What is true right now, from the engine's own meters. Nothing here is a fader position.
struct AutopilotReading
{
    std::array<float, int (MixBus::Count)> busRmsDb {};
    std::array<bool, int (MixBus::Count)> busActive {};   // making sound at all: a silent group is not a problem
    float masterRmsDb = -120.0f;
    float masterShortLufs = -120.0f;    // the last 3 seconds: what a mix is judged by
    float masterTruePeakDb = -120.0f;
    bool clipping = false;
};

// How far it may ever go, and how sure it has to be before it goes at all. Not profile data:
// these are the fence, not the taste, and the profiles have no opinion about them.
struct AutopilotLimits
{
    // Inside this, NOTHING HAPPENS. A mix drifts by a decibel all morning and that is a mix,
    // not a fault; an autopilot that chases it is worse than no autopilot at all.
    float toleranceDb = 2.0f;
    float maxStepDb = 0.5f;           // one move, so what a listener hears is a slow hand
    float maxTotalDb = 4.0f;          // ever, from the mix it was engaged on, per group
    // Once it has corrected a group it does not correct it back until the error has come well
    // inside the tolerance again: without this a group sitting on the boundary is a fader
    // being nudged up and down for an hour.
    float hysteresisDb = 0.8f;
    float quietGroupDb = -45.0f;      // below this a group is not playing, so it is left alone
};

// One thing Autopilot wants to do, in the words the Mix history will carry.
struct AutopilotMove
{
    MixBus bus = MixBus::Count;
    float deltaDb = 0.0f;             // to add to the group's fader, already limited
    std::string what;                 // "Autopilot: LEAD +0.5 dB"
    std::string why;                  // the sentence: what it noticed, in plain words
};

namespace Autopilot
{
    // The whole decision. `movedSoFarDb` is what Autopilot has already added to each group
    // since it was engaged (so the total bound is real), and `correcting` is which groups it
    // is currently holding (so the hysteresis is real). Empty means do nothing, which is the
    // usual answer and the one the tests spend most of their time on.
    std::vector<AutopilotMove> decide (const AutopilotTarget&, const AutopilotReading&,
                                       const std::array<float, int (MixBus::Count)>& movedSoFarDb,
                                       const std::array<bool, int (MixBus::Count)>& correcting,
                                       const AutopilotLimits& limits);

    // What one group's error is, dB: how far it has drifted from where the engineer left it
    // against the master. Positive means it is louder than it was. Public because the UI says
    // it, and because a number the UI prints and a number the decision uses must be one number.
    float driftDb (const AutopilotTarget&, const AutopilotReading&, MixBus) noexcept;
}

} // namespace livemix
