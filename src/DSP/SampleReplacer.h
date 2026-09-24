#pragma once
#include <vector>
#include "SampleTrigger.h"
#include "SamplePlayer.h"
#include "Core/AudioBlockView.h"

namespace livemix
{

// The sample replacement stage of a drum channel. Two halves, because the detector has to
// hear the microphone before the gate and the sample has to arrive after it:
//   detect()  reads the strip after trim and polarity and before the filters, on a copy,
//             and starts a voice for every hit it finds;
//   apply()   runs after the gate: the microphone is scaled by (1 - blend), the voices are
//             added at blend, so 0 is the microphone alone and 1 the sample alone.
// Off, both are no-ops and the block is bit-identical: every reference render still holds.
// The sample plays at `gainDb` dBFS peak for a full-velocity hit (TUNE fits that to the
// microphone's own hit level), softer hits softer unless `steady`, doubtful hits quieter
// still; `offsetMs` delays the sample only, never the microphone; polarity flips the sample.
// Zero latency: nothing here delays the channel.
class SampleReplacer
{
public:
    struct Params
    {
        bool enabled = false;
        float blend = 0.4f;             // 0 .. 1
        float thresholdDb = -30.0f;
        float riseDb = 12.0f;
        float detHpfHz = 40.0f;
        float detLpfHz = 8000.0f;
        float maskMs = 40.0f;
        bool steady = false;            // true: every hit at full level (velocity ignored)
        float offsetMs = 0.0f;          // 0 .. 5, the sample later than the hit
        bool polarityFlip = false;
        float rateSemitones = 0.0f;     // varispeed, -5 .. +5
        float gainDb = -12.0f;          // the sample's peak, dBFS, on a full-velocity hit
        float velocityDepthDb = 18.0f;  // how much softer the softest hit plays than the loudest
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setParams (const Params& p) noexcept;
    const Params& getParams() const noexcept { return params; }
    void setBank (const SampleBank* b) noexcept { player.setBank (b); }
    const SampleBank* getBank() const noexcept { return player.getBank(); }

    void detect (const AudioBlockView& preGate) noexcept;
    void apply (AudioBlockView& postGate) noexcept;

    // For the UI (any thread).
    int getHitCount() const noexcept { return trigger.getHitCount(); }
    float getLastHitLevelDb() const noexcept { return trigger.getLastLevelDb(); }
    bool isPlaying() const noexcept { return player.isPlaying(); }

private:
    Params params;
    SampleTrigger trigger;
    SamplePlayer player;
    std::vector<float> mono;            // the detector's copy, maxBlockSize long
    double sr = 48000.0;
    int maxBlock = 0;
    float sampleGainLin = 0.25f;
    double rateMul = 1.0;
    int offsetSamples = 0;
    float velocityDepthLin = 0.125f;
};

} // namespace livemix
