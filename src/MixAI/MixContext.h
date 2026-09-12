#pragma once
#include <array>
#include <string>
#include <vector>
#include "Core/Json.h"
#include "Mix/MixPlanner.h"
#include "RelationshipEngine.h"

namespace livemix
{

// The structured description of one mix, as the reasoning layer sees it. Deterministic,
// versioned, serialisable and provider-independent: nothing in here knows about HTTP, a
// vendor or a model. The same listen always produces the same document.
//
// Bump this when a field changes meaning. Adding a field does not need a bump; removing
// or re-interpreting one does, because a stored context has to keep reading back.
inline constexpr int kMixContextSchemaVersion = 1;

// The measurements that support an engineering decision, taken from the listen. A subset
// of AnalysisResult on purpose: everything here is something an engineer would act on.
struct MixContextMeasurements
{
    float peakDb = -120.0f;
    float truePeakDb = -120.0f;
    float rmsDb = -120.0f;
    float activeRmsDb = -120.0f;       // loudness while the source is playing: what a fader is set from
    float musicalPeakDb = -120.0f;     // the peak in normal playing, with isolated clicks discounted
    float hitLevelDb = -120.0f;
    float crestFactorDb = 0.0f;
    float noiseFloorDb = -120.0f;
    float dynamicRangeDb = 0.0f;
    float silencePercent = 0.0f;
    int clipCount = 0;
    float loudnessLufs = -120.0f;

    float spectralCentroidHz = 0.0f;
    float highFrequencyRatioDb = 0.0f;
    float fundamentalHz = 0.0f;
    std::array<float, int (Band::Count)> bandEnergyDb {};
    std::vector<ResonancePeak> resonances;     // the three most prominent

    float transientsPerSecond = 0.0f;
    float meanTransientRiseDb = 0.0f;
    float meanDecayMs = 0.0f;
    float bleedEstimate = 0.0f;
    float sibilanceDb = -120.0f;

    int numChannels = 1;
    float stereoCorrelation = 1.0f;
    float stereoBalanceDb = 0.0f;

    float tempoBpm = 0.0f;
    float tempoConfidence = 0.0f;

    static MixContextMeasurements from (const AnalysisResult&);
    json::Value toJson() const;
};

// One source in the mix.
struct MixContextTrack
{
    int id = -1;                        // the strip index: stable inside a session, and what every plan targets
    std::string name;
    std::string role;                   // "Lead Vocal"
    std::string family;                 // "LeadVocal": the group the profile's targets are defined for
    std::string bus;                    // "VOCALS"
    bool stereo = false;

    bool heard = false;
    bool faint = false;
    bool bleedOnly = false;             // heard, but only as spill

    float faderDb = 0.0f;
    float pan = 0.0f;
    float inputGainDb = 0.0f;
    std::vector<std::pair<std::string, float>> sends;   // used returns only, dB
    std::vector<std::string> processing;                // the chain stages actually on

    std::string signalHealth;           // "Healthy" / "Low" / "Hot" / "Clipping" / "No signal" / "Faint"
    float capturePeakDb = -120.0f;      // at the device, before any DLIVE gain
    float consoleMoveDb = 0.0f;         // what the preamp itself should still do; 0 = nothing

    MixContextMeasurements measurements;

    json::Value toJson() const;
};

struct MixContextBus
{
    std::string name;
    bool used = false;
    float faderDb = 0.0f;
    int sourceCount = 0;
    std::vector<std::string> processing;
    MixContextMeasurements measurements;   // what the bus chain received during the listen

    json::Value toJson() const;
};

// Whether the listen is worth reasoning about at all. A confident mix built from a band
// that was not playing is worse than no mix: DLIVE says what it needs instead.
struct MixCaptureAdequacy
{
    bool sufficient = false;
    float seconds = 0.0f;
    int tracksAssigned = 0;
    int tracksActive = 0;
    int tracksFaint = 0;
    int tracksClipping = 0;
    std::string reason;      // empty when sufficient
    std::string guidance;    // what to do about it, in plain words

    json::Value toJson() const;
};

struct MixContext
{
    int schemaVersion = kMixContextSchemaVersion;
    std::string sessionName;
    std::string profile;                // "Modern Gospel"
    std::string purpose;                // "Church Broadcast"
    double sampleRate = 48000.0;
    float listenSeconds = 0.0f;
    float tempoBpm = 0.0f;

    std::vector<MixContextTrack> tracks;
    std::array<MixContextBus, int (MixBus::Count)> buses {};
    MixContextMeasurements master;      // what left the master: the loudness the room heard
    std::vector<MixRelationship> relationships;
    MixCaptureAdequacy adequacy;

    // What the deterministic planner already decided, so the reasoning layer refines a
    // professional mix rather than starting from the console's flat state. Summary only;
    // the plan itself never leaves the machine.
    std::vector<std::string> baselineDecisions;

    json::Value toJson() const;
    std::string write (bool pretty = false) const { return toJson().write (pretty); }
};

// Built from the listen and the deterministic plan made from it. `plan` supplies which
// strips were really heard, what the chains now do and what the baseline already decided,
// so the context and the mix can never disagree about what happened.
MixContext buildMixContext (const MixPlanContext& ctx, const MixPlan& plan);

} // namespace livemix
