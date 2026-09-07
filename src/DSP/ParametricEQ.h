#pragma once
#include <array>
#include "Processor.h"
#include "Biquad.h"
#include "Core/Smoother.h"

namespace livemix
{

struct EQBandParams
{
    bool enabled = false;
    FilterType type = FilterType::Peak;
    float freqHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
};

// Multi-band parametric EQ built from cascaded biquads. Minimum phase, zero latency.
class ParametricEQ : public Processor
{
public:
    static constexpr int kMaxBands = 8;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    void setNumBands (int n) noexcept { numBands = n < kMaxBands ? n : kMaxBands; }
    int getNumBands() const noexcept { return numBands; }
    void setBand (int index, const EQBandParams& p) noexcept;
    void setEnabled (bool e) noexcept { enabled = e; }

private:
    struct Band
    {
        EQBandParams params;
        Smoother freq, gain, q;
        Biquad filter;
        bool active = false;
        bool needsUpdate = true;
    };

    void updateBand (Band& b) noexcept;

    std::array<Band, kMaxBands> bands;
    int numBands = 4;
    bool enabled = true;
    double sr = 48000.0;
};

} // namespace livemix
