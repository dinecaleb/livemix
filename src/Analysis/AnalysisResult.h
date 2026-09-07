#pragma once
#include <array>
#include <vector>

namespace livemix
{

// Named frequency bands used for coarse spectral balance.
enum class Band : int { Sub = 0, Low, LowMid, Mid, UpperMid, Presence, Brilliance, Air, Count };

inline constexpr std::array<const char*, int (Band::Count)> kBandNames {
    "Sub", "Low", "Low-Mid", "Mid", "Upper-Mid", "Presence", "Brilliance", "Air"
};
// Band edges in Hz (Sub: 20-60, Low: 60-150, LowMid: 150-400, Mid: 400-1k,
// UpperMid: 1k-3k, Presence: 3k-6k, Brilliance: 6k-12k, Air: 12k-20k).
inline constexpr std::array<float, int (Band::Count) + 1> kBandEdgesHz {
    20.0f, 60.0f, 150.0f, 400.0f, 1000.0f, 3000.0f, 6000.0f, 12000.0f, 20000.0f
};

inline constexpr int kNumThirdOctaveBands = 31; // 20 Hz .. 20 kHz ISO centres

struct ResonancePeak
{
    float frequencyHz = 0.0f;
    float prominenceDb = 0.0f; // above the local spectral trend
};

struct AnalysisResult
{
    bool valid = false;
    double sampleRate = 48000.0;
    int numChannels = 1;
    float durationSeconds = 0.0f;
    int droppedFrames = 0;

    // Level
    float peakDb = -120.0f;
    float rmsDb = -120.0f;
    float crestFactorDb = 0.0f;
    float hitLevelDb = -120.0f;      // 95th percentile of 10 ms frame levels
    float noiseFloorDb = -120.0f;    // 10th percentile of non-digital-silence frames
    float dynamicRangeDb = 0.0f;     // hitLevel - noiseFloor
    float silencePercent = 0.0f;     // frames below -60 dBFS
    float dcOffset = 0.0f;
    int clipCount = 0;

    // Transients / envelope
    int transientCount = 0;
    float transientsPerSecond = 0.0f;
    float meanTransientRiseDb = 0.0f;
    float meanDecayMs = 0.0f;        // mean time for a hit to fall 20 dB (0 = not measured); ring / sustain
    int decayCount = 0;

    // Fundamental: strongest spectral peak between 30 and 500 Hz (0 = none found).
    float fundamentalHz = 0.0f;
    float fundamentalLevelDb = -120.0f; // that peak's level re total energy

    // Spectrum (dB relative to total energy; all values <= 0)
    std::array<float, int (Band::Count)> bandEnergyDb {};
    std::array<float, kNumThirdOctaveBands> thirdOctaveDb {};
    float spectralCentroidHz = 0.0f;
    float highFrequencyRatioDb = 0.0f; // energy above 5 kHz vs below
    std::vector<ResonancePeak> resonances;

    // Stereo
    std::array<float, 2> channelRmsDb { -120.0f, -120.0f };
    float stereoBalanceDb = 0.0f; // L - R
    float stereoCorrelation = 1.0f; // 1 = mono, 0 = uncorrelated, < 0 = out of phase (mono streams: 1)

    // Sibilance: 95th percentile over loud 10 ms frames of (level above 5 kHz) - (full-band level), dB.
    // A warm voice sits around -20; sharp S sounds reach -6 and above. -120 when not measured.
    float sibilanceDb = -120.0f;
    float sibilancePercent = 0.0f;    // loud frames whose high band is within 6 dB of the full band

    // Loudness (BS.1770, ungated over the capture) and interpolated true peak.
    float loudnessLufs = -120.0f;
    float truePeakDb = -120.0f;

    // Bleed estimate 0..1 (how close the between-hit floor sits to the hit level)
    float bleedEstimate = 0.0f;
};

// Cheap statistics of the processed output gathered on the audio thread during a
// capture (atomics only). Used for mix-gain recommendations.
struct OutputStats
{
    bool valid = false;
    float peakDb = -120.0f;
    float rmsDb = -120.0f;
};

inline float thirdOctaveCentreHz (int index) noexcept
{
    // ISO 266 nominal centres: 20 Hz * 2^(index/3)
    return 20.0f * std::exp2 (float (index) / 3.0f) * 1.0f;
}

} // namespace livemix
