#include "FilterProcessor.h"

namespace livemix
{

void FilterProcessor::prepare (double sampleRate, int, int numChannels)
{
    sr = sampleRate;
    channels = numChannels;
    hpfFreq.prepare (sr, 20.0f);
    lpfFreq.prepare (sr, 20.0f);
    hpfFreq.snapTo (params.hpfHz);
    lpfFreq.snapTo (params.lpfHz);
    reset();
    updateCoefficients();
}

void FilterProcessor::reset() noexcept
{
    hpf1.reset(); hpf2.reset(); lpf1.reset(); lpf2.reset();
}

void FilterProcessor::setParams (const Params& p) noexcept
{
    if (p.hpfSlopeDbPerOct != params.hpfSlopeDbPerOct || p.lpfSlopeDbPerOct != params.lpfSlopeDbPerOct)
        needsUpdate = true;
    params = p;
    hpfFreq.setTarget (p.hpfHz);
    lpfFreq.setTarget (p.lpfHz);
}

void FilterProcessor::updateCoefficients() noexcept
{
    // Butterworth Q for a single 2nd-order section. For 24 dB/oct two cascaded
    // sections with Q = 0.541 and 1.307 form a 4th-order Butterworth.
    const float hf = hpfFreq.getCurrent();
    const float lf = lpfFreq.getCurrent();
    if (params.hpfSlopeDbPerOct >= 24)
    {
        hpf1.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, hf, 0.5412f, 0.0f));
        hpf2.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, hf, 1.3066f, 0.0f));
    }
    else
    {
        hpf1.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, hf, 0.7071f, 0.0f));
    }
    if (params.lpfSlopeDbPerOct >= 24)
    {
        lpf1.setCoefficients (BiquadCoefficients::make (FilterType::LowPass, sr, lf, 0.5412f, 0.0f));
        lpf2.setCoefficients (BiquadCoefficients::make (FilterType::LowPass, sr, lf, 1.3066f, 0.0f));
    }
    else
    {
        lpf1.setCoefficients (BiquadCoefficients::make (FilterType::LowPass, sr, lf, 0.7071f, 0.0f));
    }
}

void FilterProcessor::process (AudioBlockView& block) noexcept
{
    if (! params.hpfEnabled && ! params.lpfEnabled)
    {
        hpfWasEnabled = lpfWasEnabled = false;
        return;
    }

    // Clear stale state when a filter is switched on to avoid a thump.
    if (params.hpfEnabled && ! hpfWasEnabled) { hpf1.reset(); hpf2.reset(); }
    if (params.lpfEnabled && ! lpfWasEnabled) { lpf1.reset(); lpf2.reset(); }
    hpfWasEnabled = params.hpfEnabled;
    lpfWasEnabled = params.lpfEnabled;

    // Coefficients are refreshed per block while a frequency is smoothing.
    // Smoothing runs at block rate for the whole block; at live buffer sizes
    // (32-256 samples) this is inaudible.
    const bool smoothing = hpfFreq.isSmoothing() || lpfFreq.isSmoothing();
    if (smoothing)
        for (int i = 0; i < block.numSamples; ++i) { hpfFreq.next(); lpfFreq.next(); }
    if (smoothing || needsUpdate)
    {
        updateCoefficients();
        needsUpdate = false;
    }

    const int n = block.numSamples;
    for (int ch = 0; ch < block.numChannels && ch < kMaxChannels; ++ch)
    {
        float* data = block.channel (ch);
        if (params.hpfEnabled)
        {
            hpf1.processBlock (ch, data, n);
            if (params.hpfSlopeDbPerOct >= 24) hpf2.processBlock (ch, data, n);
        }
        if (params.lpfEnabled)
        {
            lpf1.processBlock (ch, data, n);
            if (params.lpfSlopeDbPerOct >= 24) lpf2.processBlock (ch, data, n);
        }
    }
}

} // namespace livemix
