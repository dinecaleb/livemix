#pragma once
#include "Processor.h"
#include "Biquad.h"
#include "Core/Smoother.h"

namespace livemix
{

// High-pass / low-pass with 12 or 24 dB/oct slopes (Butterworth). Zero latency.
class FilterProcessor : public Processor
{
public:
    struct Params
    {
        bool hpfEnabled = false;
        float hpfHz = 80.0f;
        int hpfSlopeDbPerOct = 12; // 12 or 24
        bool lpfEnabled = false;
        float lpfHz = 18000.0f;
        int lpfSlopeDbPerOct = 12;
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    void setParams (const Params& p) noexcept;

private:
    void updateCoefficients() noexcept;

    Params params;
    double sr = 48000.0;
    int channels = 1;
    Smoother hpfFreq, lpfFreq;
    Biquad hpf1, hpf2, lpf1, lpf2;
    bool hpfWasEnabled = false, lpfWasEnabled = false;
    bool needsUpdate = true;
};

} // namespace livemix
