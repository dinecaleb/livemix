#pragma once
#include <functional>
#include <string>
#include "TuneTypes.h"
#include "Profiles/Profile.h"

namespace livemix
{

// Collects a strategy's decisions. Strategies edit `proposed` directly inside
// move(); the parameter-level changes are recorded automatically by diffing,
// so every item's explanation and its changes can never disagree.
class TuneDecisions
{
public:
    explicit TuneDecisions (const ChannelParameters& start) : proposed (start) {}

    ChannelParameters proposed;
    std::vector<Recommendation> items;

    // A decision that changes parameters. If the edit changes nothing, no item is recorded.
    void move (Recommendation::Kind kind, TuneSection section, std::string what, std::string why,
               Confidence confidence, const std::function<void (ChannelParameters&)>& edit);

    // A decision that leaves things alone, or a measurement worth telling the user.
    void note (Recommendation::Kind kind, TuneSection section, std::string what, std::string why, Confidence confidence);

    bool hasChangesIn (TuneSection s) const;
};

// One source family's engineering behaviour. Consumes structured analysis and
// profile targets, produces bounded decisions. No DSP, no UI, no allocation
// constraints (message thread).
class SourceStrategy
{
public:
    virtual ~SourceStrategy() = default;
    virtual const char* name() const = 0;
    virtual void decide (const TuneContext& ctx, const SourceTargets& targets, TuneDecisions& d) const = 0;
};

const SourceStrategy& strategyFor (RoleFamily family);
const SourceStrategy& vocalStrategyFor (RoleFamily family);   // VocalStrategies.cpp
const SourceStrategy& keysStrategyFor (RoleFamily family);    // KeysStrategies.cpp
const SourceStrategy& masterStrategy();                       // MasterStrategy.cpp
const SourceStrategy& guitarStrategyFor (RoleFamily family);  // GuitarStrategies.cpp
const SourceStrategy& bassStrategyFor (RoleFamily family);    // BassStrategies.cpp

// Shared engineering rules used by several strategies. Each is bounded by the
// profile's safe ranges and does nothing when the measurement is already inside
// tolerance ("no change required" is a legitimate result).
namespace tune
{
    struct Levels
    {
        float hitDb = -120.0f;     // hit level as the chain sees it (raw + input trim)
        float floorDb = -120.0f;
        bool sparse = false;       // long quiet sections: lower confidence on level-based moves
    };
    Levels levels (const TuneContext& ctx);

    // Capture health and the preamp recommendation. Returns false when there is no usable signal.
    bool evaluateInput (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, RecommendationResult& report);
    void removeDcOffset (const TuneContext& ctx, TuneDecisions& d);

    // The profile's high-pass for this source (the template every high-pass move is computed from,
    // never the current value, so re-tuning the same capture never walks the filter).
    float templateHighPassHz (const TuneContext& ctx, const SourceTargets& t);
    // The measured fundamental if it sits where the profile expects this source's fundamental; else 0.
    float fundamental (const TuneContext& ctx, const SourceTargets& t);
    float bandDeviation (const TuneContext& ctx, const SourceTargets& t, Band b); // measured - target (dB)
    float bandExcess (const TuneContext& ctx, const SourceTargets& t, Band b);    // > 0 above tolerance, < 0 below, 0 inside

    void placeHighPass (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float hz, const char* why);
    void controlLowMid (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d);
    void notchResonance (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float minHz, float maxHz, const char* character);
    void shapeBody (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float fundamentalHz);
    void shapeAttack (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d);
    void controlHarshness (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d);
    void shapeAir (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d);
    void setCompression (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d);
    void setGate (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float fundamentalHz);
    void setMixGain (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d);
    void stereoBalanceNote (const TuneContext& ctx, TuneDecisions& d);
    // Voices: de-esser fitted to the measured sibilance. Keys / master: stereo width and correlation.
    // Master: output level and limiter fitted to the delivery loudness target.
    void controlSibilance (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d);
    void setWidth (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d);
    void setLoudness (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d);

    // Plain words for the source being tuned ("hit" / "note" / "phrase", "a drum mix" / "a vocal mix").
    const char* eventNoun (const TuneContext& ctx);
    const char* mixNoun (const TuneContext& ctx);

    std::string fmtDb (float db, int decimals = 1);      // "+2.5 dB"
    std::string fmtHz (float hz);                         // "63 Hz" / "4.0 kHz"
}

} // namespace livemix
