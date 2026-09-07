#pragma once
#include <string>
#include <vector>
#include "MixSession.h"
#include "MixParameters.h"
#include "RoutingGraph.h"
#include "MixCapture.h"
#include "Tune/TuneTypes.h"

namespace livemix
{

// What TUNE MIX needs: who is in the mix, what the engine is doing now, what was heard.
struct MixPlanContext
{
    MixSession session;
    RoutingGraph graph;
    MixParameters current;       // what the engine runs now (the plan starts from here)
    MixParameters atCapture;     // what the engine ran while listening (its faders shaped what the buses received)
    MixCapture::Result capture;
};

// One strip's part of the plan.
struct StripPlan
{
    int strip = -1;
    std::string name;
    ChannelRole role = ChannelRole::KickIn;
    bool heard = false;                     // usable signal during the listen
    bool balanced = false;                  // the fader was fitted from the processed level
    bool bleedOnly = false;                 // heard, but only as spill (a speech mic during the song): left alone
    TuneResult tune;                        // the source's own Tune (before / proposed / explanations)
    std::vector<Recommendation> mixItems;   // relationship and balance decisions about this strip
    float faderBeforeDb = 0.0f, faderDb = 0.0f;
    float inputGainBeforeDb = 0.0f, inputGainDb = 0.0f;   // digital preamp
};

struct BusPlan
{
    MixBus bus = MixBus::Master;
    bool used = false;
    TuneResult tune;
};

// The professional starting point for the whole mix, with its explanation. Applying
// `proposed` is the AFTER; `before` is what was running when the plan was made.
struct MixPlan
{
    bool valid = false;
    std::string headline;                   // "MIX TUNED" / "MIX: NO CHANGE REQUIRED" / "MIX: NO SIGNAL"
    bool noChangeRequired = false;
    MixParameters before;
    MixParameters proposed;
    std::vector<StripPlan> strips;
    std::array<BusPlan, int (MixBus::Count)> buses {};
    std::vector<Recommendation> relationships;   // what the mix decided about how sources work together
    std::vector<std::string> notes;              // plain-language summary lines
    int stripsHeard = 0;
    int parametersChanged = 0;                   // strip + bus DSP parameters
    int fadersChanged = 0;
    int sendsChanged = 0;
    int gainsChanged = 0;
};

// The deterministic "professional engineer" layer above the per-source strategies:
//   per-strip Tune (TuneEngine, unchanged)
//   -> relationships (kick <-> bass, lead <-> music, lead <-> backing, toms <-> overheads, room <-> room mics)
//   -> balance (faders fitted to the profile's mix levels from the measured processed peaks)
//   -> buses and master (TuneEngine on what each bus received, master loudness predicted after the balance)
//   -> bounds (SafetyValidator on every parameter change; fader and send moves clamped)
// Every decision is computed from the capture and the profile, never from the current
// value, so planning again on the same capture changes nothing.
namespace MixPlanner
{
    MixPlan plan (const MixPlanContext& ctx);

    // Bounded application helpers shared with the app (message thread).
    int countParameterChanges (const MixParameters& from, const MixParameters& to);
}

} // namespace livemix
