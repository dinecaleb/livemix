#pragma once
#include <string>
#include <vector>
#include "Core/Json.h"
#include "DspCapabilityRegistry.h"
#include "Recommendations/Recommendation.h"

namespace livemix
{

inline constexpr int kMixIntentSchemaVersion = 1;

// WHAT SHOULD THIS SOUND LIKE.
//
// An intent says nothing about processors. "The lead should come forward while staying
// warm, and the keys should make room for it" is an intent; a 2.5 dB cut at 2.8 kHz with
// a Q of 1.2 is the answer DLIVE works out afterwards. Keeping the two apart is what lets
// the reasoning layer be creative without being able to reach into the audio path, and it
// is what lets the same intent be satisfied differently as DLIVE's toolbox grows.
enum class MixObjectiveType : int
{
    Presence = 0,       // forward or back in the mix's foreground
    Warmth,             // low-mid body
    Body,               // weight at the bottom
    Brightness,         // air and top end
    Clarity,            // definition: less mud, more of the source's own shape
    DynamicStability,   // how steady the source sits
    Punch,              // attack and impact
    Cleanup,            // what should not be heard between the notes
    Sibilance,          // sharp S sounds
    SpatialDepth,       // how far back in the room it sits
    Width,              // how wide it is
    Level,              // how loud it is against the rest
    Separation,         // room made for a named other source
    Character,          // an effect's kind: "plate", "hall", "spring"
    Count
};

const char* mixObjectiveId (MixObjectiveType) noexcept;         // "presence"
const char* mixObjectiveLabel (MixObjectiveType) noexcept;      // "Presence"
MixObjectiveType mixObjectiveFromId (const std::string&) noexcept;   // Count when unknown

// One desired outcome. `strength` carries the direction in its sign: +1 is as far
// towards the objective as DLIVE will go in one Tune, -1 is as far the other way
// (back, darker, looser), 0 is "leave this alone".
struct MixObjective
{
    MixObjectiveType type = MixObjectiveType::Presence;
    float strength = 0.0f;                  // -1 .. +1
    MixTargetRef against;                   // Separation: who the room is being made for
    std::string character;                  // Character: the kind of space asked for
    bool preserveArticulation = false;      // depth must not cost the words
    bool preserveTransients = false;        // control must not cost the attack

    bool hasAgainst() const noexcept { return against.index >= 0; }
    json::Value toJson() const;
};

struct MixTargetIntent
{
    MixTargetRef target;
    std::string targetName;                 // as the reasoning layer knew it; for the report only
    std::vector<MixObjective> objectives;
    std::string reason;                     // the sentence a user reads in REVIEW CHANGES
    Confidence confidence = Confidence::Medium;

    json::Value toJson() const;
};

struct MixIntent
{
    int schemaVersion = kMixIntentSchemaVersion;
    bool valid = false;
    bool noChangeRequired = false;          // the model is allowed to say the mix is finished
    std::string summary;                    // one or two plain sentences about the whole mix
    std::vector<MixTargetIntent> targets;
    // Anything asked for that DLIVE was told about but could not express as an objective.
    // Kept so the honest answer survives all the way to the user.
    std::vector<std::string> unsupportedRequests;

    int objectiveCount() const noexcept;
    json::Value toJson() const;
    std::string write (bool pretty = false) const { return toJson().write (pretty); }
};

// Reads an intent out of a structured reply. Anything that does not parse is dropped and
// named in `problems` rather than guessed at: a machine action is never taken from prose.
MixIntent parseMixIntent (const json::Value&, std::vector<std::string>* problems = nullptr);
// Same, from raw text - a literal would otherwise be ambiguous against the json::Value overload.
MixIntent parseMixIntentText (const std::string& jsonText, std::vector<std::string>* problems = nullptr);

// Drops objectives that name nothing real, clamps strengths into range and removes empty
// targets. Runs before the resolver on every path, including the mock provider's.
MixIntent validateMixIntent (const MixIntent&, const DspCapabilityRegistry&, std::vector<std::string>* problems = nullptr);

} // namespace livemix
