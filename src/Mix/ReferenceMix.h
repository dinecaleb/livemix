#pragma once
#include <array>
#include <string>
#include <vector>
#include "Analysis/AnalysisResult.h"
#include "Core/StyleId.h"
#include "Profiles/Profile.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// REFERENCE MIX: "make it sound like this."
//
// A finished recording the mix is aimed at. DLIVE listens to it exactly the way it
// listens to the band - one AnalysisAccumulator, the same measurements - and keeps the
// part of that measurement a mix can actually be aimed at: the tonal balance, how dense
// it is and how wide it is. That is a document (schema v1), stored with the session, so
// reopening a service never has to find the file again.
//
// What a reference is NOT allowed to do matters as much as what it is:
//   - it never sets the delivery loudness. A mastered song sits at -9 LUFS and a church
//     broadcast at -23; copying the number would hand the stream to the limiter.
//   - it never moves a single source. Who is loud in your mix is a balance decision made
//     from what DLIVE heard the band play, not from somebody else's record.
//   - it never moves a target further than the profile's own bounds allow, so matching a
//     dull reference cannot leave the mix dull enough to be unusable.
// Everything it will not do is reported with its reason, the way a refused capability is.
// ---------------------------------------------------------------------------

inline constexpr int kReferenceSchemaVersion = 1;

// One recording, measured. Serialisable as it stands (fixed-size arrays, no vectors).
struct ReferenceProfile
{
    int version = kReferenceSchemaVersion;
    bool valid = false;
    std::string name;                 // what the user calls it ("Take Me To The King")
    std::string path;                 // where the file was when it was measured; a note, never re-read on its own
    float seconds = 0.0f;
    int channels = 2;

    // Tonal balance: band energy relative to the whole, so it does not care how loud the
    // reference is. This is the part of a finished record a live mix can actually be aimed at.
    std::array<float, int (Band::Count)> bandEnergyDb {};
    std::array<float, kNumThirdOctaveBands> thirdOctaveDb {};

    float crestFactorDb = 0.0f;
    float loudnessLufs = -120.0f;
    float truePeakDb = -120.0f;
    float stereoCorrelation = 1.0f;
    float spectralCentroidHz = 0.0f;
    float highFrequencyRatioDb = 0.0f;
    float tempoBpm = 0.0f;
    float tempoConfidence = 0.0f;
};

// Whether a recording is worth aiming a mix at, in the user's words. A four-second phone
// clip, a silent file or one channel of a stereo master measure something - just not a mix.
struct ReferenceAdequacy
{
    bool usable = false;
    std::string reason;      // empty when usable
    std::string guidance;    // what to do about it
};

// One band's part of the match, kept so the sheet can draw the two curves against each other.
struct ReferenceBandMatch
{
    Band band = Band::Sub;
    float referenceDb = 0.0f;   // the reference's share of its own energy
    float mixDb = 0.0f;         // this mix's share, at the master's input
    float profileAimDb = 0.0f;  // where the profile aims this band with no reference at all
    float aimDb = 0.0f;         // what the master is now aimed at (the reference, inside the profile's bounds)
    float askedDb = 0.0f;       // aim - mix: how far this band still has to travel
    float pulledDb = 0.0f;      // aim - profileAim: what having a reference actually changed
    bool bounded = false;       // the reference asked for more than a reference is allowed to move
};

// What matching decided, and what it refused to decide. Carried on the MixPlan, so
// REVIEW CHANGES, the result sheet and the session all read the same record.
struct ReferenceMatch
{
    bool used = false;
    std::string name;
    std::array<ReferenceBandMatch, int (Band::Count)> bands {};
    std::vector<std::string> aims;       // "Less low-mid: the aim moves 2 dB below the profile's own."
    std::vector<std::string> limits;     // what it would not do, each with its reason
    bool widthMatched = false;
    float widthTarget = 1.0f;
    bool densityMatched = false;
    float compTargetGrDb = 0.0f;
    float loudnessDifferenceLu = 0.0f;   // reference - this delivery's target; reported, never acted on
};

namespace Reference
{
    // Is this measurement worth aiming a mix at?
    ReferenceAdequacy adequacy (const AnalysisResult&, StyleProfileId);

    // The document, from a measurement of the file.
    ReferenceProfile profileFrom (const AnalysisResult&, std::string name, std::string path);

    // The master's targets, aimed at the reference instead of at the profile's default.
    // `masterInput` is what the master chain hears - the same analysis the strategy will
    // compare against - so the report and the decision can never disagree. Everything the
    // reference is not allowed to move is left exactly as `base` has it.
    SourceTargets targets (const SourceTargets& base, const ReferenceProfile&,
                           const AnalysisResult& masterInput, StyleProfileId, ReferenceMatch& out);

    // The plain word for a band, for a sentence a volunteer reads ("low-mid", "air").
    const char* bandWord (Band);
}

} // namespace livemix
