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
    bool faint = false;                     // signal, but never above the profile's faint level at the device: check the mic, nothing changed
    TuneResult tune;                        // the source's own Tune (before / proposed / explanations)
    std::vector<Recommendation> mixItems;   // relationship and balance decisions about this strip
    float faderBeforeDb = 0.0f, faderDb = 0.0f;
    float inputGainBeforeDb = 0.0f, inputGainDb = 0.0f;   // digital preamp
    float capturePeakDb = -120.0f;          // the loudest moment at the device during the listen, before any DLIVE gain
};

struct BusPlan
{
    MixBus bus = MixBus::Master;
    bool used = false;
    TuneResult tune;
    float predictedInShiftDb = 0.0f;    // how much more (or less) this bus is predicted to receive under the plan than at the listen
    float predictedOutShiftDb = 0.0f;   // ... and to put out, after its re-fitted chain (buses only)
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
    int stripsFaint = 0;                         // inputs with a signal too faint to be a playing source (check the mic)
    int parametersChanged = 0;                   // strip + bus DSP parameters
    int fadersChanged = 0;
    int sendsChanged = 0;
    int gainsChanged = 0;
    int stripsWantPreamp = 0;                    // heard, but the console preamp itself should still move (gain staging comes first)
};

// The deterministic "professional engineer" layer above the per-source strategies:
//   per-strip Tune (TuneEngine, unchanged)
//   -> relationships (kick <-> bass, lead <-> music, lead <-> backing, toms <-> overheads, room <-> room mics)
//   -> balance (faders fitted to the profile's mix levels from how loud each source is while it plays)
//   -> buses and master (TuneEngine on what each bus received, master loudness predicted after the balance)
//   -> bounds (SafetyValidator on every parameter change; fader and send moves clamped)
// Every decision is computed from the capture and the profile, never from the current
// value, so planning again on the same capture changes nothing.
namespace MixPlanner
{
    MixPlan plan (const MixPlanContext& ctx);

    // TUNE CHANNEL: the same plan, narrowed to one source. A channel is never tuned by a
    // different set of rules - the listen hears the whole band and the planner decides in
    // mix context as it always does - but only `strip` is applied: its chain, its input
    // gain, its fader and its sends. The buses and the master stay where they are (a
    // channel tune is not a master decision), and every other strip keeps what the listen
    // measured about it, with the moves the full plan would have made taken back out.
    MixPlan channelOnly (const MixPlan& full, int strip, StyleProfileId profile);

    // Bounded application helpers shared with the app (message thread).
    int countParameterChanges (const MixParameters& from, const MixParameters& to);

    // Where strip i's processed (pre-fader) peak lands under `strip`, predicted from the listen in `ctx`
    // (the measurement itself when gain and chain are unchanged). Public for tests and the stems tool.
    float predictedProcessedPeakDb (const MixPlanContext& ctx, int i, const StripParameters& strip);
    // The same for the processed RMS (what the buses and the master loudness are predicted from).
    float predictedProcessedRmsDb (const MixPlanContext& ctx, int i, const StripParameters& strip);
    // And for the processed loudness while the source is playing, which is what the balance is fitted to.
    float predictedProcessedActiveRmsDb (const MixPlanContext& ctx, int i, const StripParameters& strip);
}

} // namespace livemix
