#pragma once
#include <array>
#include <string>
#include <vector>
#include "MixSession.h"
#include "MixParameters.h"
#include "RoutingGraph.h"
#include "MixCapture.h"
#include "ReferenceMix.h"
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
    // REFERENCE MIX: a finished recording the master is aimed at instead of at the profile's
    // own tonal target. Invalid (the default) means the profile decides, as it always has.
    ReferenceProfile reference;
    // Whether this plan is correcting a mix that has already been tuned, rather than building
    // the first one. A first mix has to be free to put everything where it belongs; a
    // correction is being made to something somebody is already listening to, so how far one
    // fader may move in it is bounded (MixProfile::Relationships::maxRetuneFaderStepDb).
    bool retune = false;
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
    bool stuck = false;                     // a steady signal with no performance in it (a tone, a ring, a fault): not tuned, not balanced
    bool spillLimited = false;              // the lift stopped short because what the microphone hears between the sounds would come up with it
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
    bool refused = false; // unusable listen: measure if possible, but never refine it
    std::string headline;                   // "MIX TUNED" / "MIX: NO CHANGE REQUIRED" / "MIX: NO SIGNAL"
    bool noChangeRequired = false;
    MixParameters before;
    MixParameters proposed;
    std::vector<StripPlan> strips;
    std::array<BusPlan, int (MixBus::Count)> buses {};
    std::vector<Recommendation> relationships;   // what the mix decided about how sources work together
    ReferenceMatch reference;                    // what "sound like this" aimed at, and what it refused to copy
    std::vector<std::string> notes;              // plain-language summary lines
    int stripsHeard = 0;
    int stripsFaint = 0;                         // inputs with a signal too faint to be a playing source (check the mic)
    int stripsStuck = 0;                         // inputs carrying a steady signal rather than a performance (a tone, a ring, a fault)
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
    int focalStrip (const MixPlanContext&, const std::vector<bool>* eligible = nullptr);

    // TUNE CHANNEL: the same plan, narrowed to one source. A channel is never tuned by a
    // different set of rules - the listen hears the whole band and the planner decides in
    // mix context as it always does - but only `strip` is applied: its chain, its input
    // gain, its fader and its sends. The buses and the master stay where they are (a
    // channel tune is not a master decision), and every other strip keeps what the listen
    // measured about it, with the moves the full plan would have made taken back out.
    MixPlan channelOnly (const MixPlan& full, int strip, StyleProfileId profile);

    // TUNE <GROUP> (TUNE DRUMS, TUNE VOCALS, TUNE SPEECH ...): the same plan, narrowed to one
    // group bus. Every strip routed to `bus` is applied - its chain, its input gain, its fader
    // and its sends - and so is the group's own chain. The other groups, their strips and the
    // master stay exactly where they are, so tuning the band never moves the pastor's
    // microphone and tuning the pastor never moves the band. The listen still hears the whole
    // console: a group is decided in mix context, as a channel is.
    MixPlan busOnly (const MixPlan& full, MixBus bus, const RoutingGraph& graph, StyleProfileId profile);

    // KEEP SOME: apply part of a proposal. Strips and groups are picked separately; the
    // MASTER row carries the master's chain and everything that belongs to no single input
    // (the effects returns and the tempo). Anything not selected is `before` in the result,
    // so the narrowed plan's `proposed` is exactly what the mix becomes when it is kept.
    struct PlanSelection
    {
        std::array<bool, kMaxStrips> strips {};
        std::array<bool, int (MixBus::Count)> buses {};

        static PlanSelection all (int numStrips);
        static PlanSelection none() { return PlanSelection {}; }
        // The whole of one group: its strips and its own chain.
        static PlanSelection group (const RoutingGraph& graph, MixBus bus);
        bool any() const noexcept;
        bool everything (int numStrips) const noexcept;
        // Whether a strip is applied: picked itself.
        bool strip (int i) const noexcept { return i >= 0 && i < kMaxStrips && strips[size_t (i)]; }
        bool bus (MixBus b) const noexcept { return int (b) >= 0 && int (b) < int (MixBus::Count) && buses[size_t (b)]; }
    };
    MixPlan restrictTo (const MixPlan& full, const PlanSelection& selection, const RoutingGraph& graph, StyleProfileId profile);

    // Bounded application helpers shared with the app (message thread).
    void refreshSummary (MixPlan&); // recount and refresh Inspector values after a validated snapshot
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
