#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include "SampleTrigger.h"
#include "SamplePlayer.h"
#include "Core/AudioBlockView.h"

namespace livemix
{

// What the kit's drum strips tell each other, inside one audio callback: the last hit each
// family recognised, when, and how far under that strip's own hit level it was. A tom strip
// asks it before playing: a soft hit on the tom microphone within two milliseconds of a hard
// snare or kick is the snare or kick arriving through the air, not the tom, and plays nothing.
// Strips run in order, so a strip only sees the hits of strips processed before it in the
// same block, and every hit of every strip from the blocks before - written and read on the
// audio thread alone, never elsewhere.
struct KitTriggerTable
{
    struct Entry { long long time = -1; float underDb = 0.0f; };   // underDb: how far under the strip's own loudest recent hit (>= 0)
    std::array<Entry, int (RoleFamily::Count)> last {};
    void note (RoleFamily f, long long time, float underDb) noexcept { last[size_t (f)] = { time, underDb }; }
};

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
        bool followDrum = false;        // pitch the sample to the drum: rate = drumHz / the bank's own fundamental
        float drumHz = 0.0f;            // the drum's measured fundamental (TUNE writes it); 0 = unknown
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setParams (const Params& p) noexcept;
    const Params& getParams() const noexcept { return params; }
    void setBank (const SampleBank* b) noexcept { player.setBank (b); }
    const SampleBank* getBank() const noexcept { return player.getBank(); }

    // `blockStart` is the engine's running sample position of the block's first sample, for the
    // kit table; 0 is fine where there is no table.
    void detect (const AudioBlockView& preGate, long long blockStart = 0) noexcept;
    void apply (AudioBlockView& postGate) noexcept;

    // The kit table this strip reads and writes, and which drum it is. Message thread, before audio.
    void setKit (KitTriggerTable* table, RoleFamily self) noexcept { kit = table; family = self; }

    // The rate a hit will play at right now: the manual pitch, times the drum's pitch over the
    // bank's when the sample follows the drum and both are known. 1 = as recorded.
    double currentRate() const noexcept;

    // For the UI (any thread).
    // The hits that fired in the last detect() call - the ones that played a sample, never a
    // vetoed one - as sample offsets inside that block, and how far before each the drum's
    // onset was. The gate reads them so a hit the sample fires on always opens the microphone
    // too: a soft stroke is never a sample with no microphone under it. Audio thread only.
    int numHits() const noexcept { return hitOffsetCount; }
    const int* hitOffsets() const noexcept { return hitOffsetsBlock; }
    int hitLookbackSamples() const noexcept { return trigger.reportDelaySamples(); }

    int getHitCount() const noexcept { return trigger.getHitCount(); }
    int getVetoCount() const noexcept { return vetoed.load (std::memory_order_relaxed); }
    float getLastHitLevelDb() const noexcept { return trigger.getLastLevelDb(); }
    bool isPlaying() const noexcept { return player.isPlaying(); }

private:
    Params params;
    SampleTrigger trigger;
    SamplePlayer player;
    std::vector<float> mono;            // the detector's copy, maxBlockSize long
    double sr = 48000.0;
    int maxBlock = 0;
    int hitOffsetsBlock[SampleTrigger::kMaxHits] {};
    int hitOffsetCount = 0;
    float sampleGainLin = 0.25f;
    double rateMul = 1.0;
    int offsetSamples = 0;
    float velocityDepthLin = 0.125f;
    KitTriggerTable* kit = nullptr;
    RoleFamily family = RoleFamily::Kick;
    std::atomic<int> vetoed { 0 };
    float loudestDb = -120.0f;          // the loudest hit lately, in the detector's frame; falls 1 dB a second so one accident fades
};

} // namespace livemix
