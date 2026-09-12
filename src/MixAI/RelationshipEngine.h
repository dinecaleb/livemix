#pragma once
#include <string>
#include <vector>
#include "Mix/MixPlanner.h"

namespace livemix
{

// WHAT IS HAPPENING BETWEEN THE SOURCES.
//
// MixPlanner already reasons about relationships, but it does so while it is deciding: the
// measurement and the move are the same line of code, so nothing outside the planner can see
// what was measured. The reasoning layer needs the measurement on its own - a number, its
// tolerance and the two sources it is about - so it can be serialised into a MixContext, shown
// in REVIEW CHANGES and tested without running a mix.
//
// This engine measures and never decides. Every value is computed from the listen and the
// profile's targets, never from the current parameters, so it is the same on a re-listen.
enum class MixRelationKind : int
{
    KickAndBass = 0,     // who owns the low end
    LeadAndMusic,        // presence competition against the lead vocal
    LeadAndBacking,      // hierarchy: the lead has to stay in front
    CloseAndOverheads,   // the kit's balance, and whether the close mics agree with the overheads
    RoomAndReturns,      // real room in the mix against the artificial one
    SpeechAndBand,       // a preaching microphone open while the band plays
    ChannelAndBus,       // channel and bus processing stacking on the same source
    VocalAndFx,          // depth against intelligibility
    MixAndMaster,        // headroom, loudness and dynamics arriving at the master
    Count
};

const char* mixRelationKindName (MixRelationKind) noexcept;

// One measured fact about two things in the mix.
struct MixRelationship
{
    MixRelationKind kind = MixRelationKind::KickAndBass;
    // Machine-readable name of the quantity, e.g. "sub_overlap_db". Stable: the reasoning
    // layer and the tests both key off it, so it never changes once released.
    std::string metric;
    int stripA = -1;              // strip index, or -1 when the side is a bus / the master / the returns
    int stripB = -1;
    std::string nameA, nameB;
    float value = 0.0f;           // in the metric's own units (dB unless the name says otherwise)
    float tolerance = 0.0f;       // where the profile says it stops being comfortable
    bool concern = false;         // measured past the tolerance: worth a decision
    std::string headline;         // one plain sentence, ready to show
};

namespace RelationshipEngine
{
    // Measured from the listen in `ctx` and the profile's targets. `heardStrips`, when given,
    // is MixPlanner's own verdict on which strips carried a usable signal (so a faint input or a
    // speech mic heard only as spill never becomes a relationship); without it the engine makes
    // the same test itself.
    std::vector<MixRelationship> measure (const MixPlanContext& ctx, const std::vector<bool>* heardStrips = nullptr);
}

} // namespace livemix
