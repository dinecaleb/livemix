#include "TestFramework.h"
#include "TestSignals.h"
#include "DSP/Biquad.h"

using namespace livemix;

namespace
{
    // Steady-state magnitude response of a biquad at a frequency, by running a sine through it.
    float measureGainDb (const BiquadCoefficients& c, double sr, float freq)
    {
        Biquad b; b.setCoefficients (c);
        const int n = int (sr);
        testsig::Buffer in (1, n);
        testsig::fillSine (in, freq, 0.5f, sr);
        b.processBlock (0, in.ptrs[0], n);
        return testsig::toDb (testsig::rms (in, 0, n / 2, n) / (0.5f / std::sqrt (2.0f)));
    }
}

TEST_CASE ("Biquad: peak filter gain at centre frequency")
{
    const double sr = 48000.0;
    auto c = BiquadCoefficients::make (FilterType::Peak, sr, 1000.0f, 1.0f, 6.0f);
    CHECK_NEAR (measureGainDb (c, sr, 1000.0f), 6.0f, 0.1f);
    CHECK_NEAR (measureGainDb (c, sr, 50.0f), 0.0f, 0.2f);
    CHECK_NEAR (measureGainDb (c, sr, 15000.0f), 0.0f, 0.2f);
}

TEST_CASE ("Biquad: shelves and pass filters have the expected shape")
{
    const double sr = 48000.0;
    auto ls = BiquadCoefficients::make (FilterType::LowShelf, sr, 200.0f, 0.707f, -6.0f);
    CHECK_NEAR (measureGainDb (ls, sr, 30.0f), -6.0f, 0.3f);
    CHECK_NEAR (measureGainDb (ls, sr, 8000.0f), 0.0f, 0.2f);

    auto hs = BiquadCoefficients::make (FilterType::HighShelf, sr, 5000.0f, 0.707f, 4.0f);
    CHECK_NEAR (measureGainDb (hs, sr, 15000.0f), 4.0f, 0.3f);
    CHECK_NEAR (measureGainDb (hs, sr, 100.0f), 0.0f, 0.2f);

    auto hp = BiquadCoefficients::make (FilterType::HighPass, sr, 100.0f, 0.707f, 0.0f);
    CHECK_NEAR (measureGainDb (hp, sr, 100.0f), -3.0f, 0.3f);
    CHECK (measureGainDb (hp, sr, 25.0f) < -20.0f);

    auto lp = BiquadCoefficients::make (FilterType::LowPass, sr, 1000.0f, 0.707f, 0.0f);
    CHECK_NEAR (measureGainDb (lp, sr, 1000.0f), -3.0f, 0.3f);
    CHECK (measureGainDb (lp, sr, 8000.0f) < -30.0f);
}

TEST_CASE ("Biquad: unity peak at 0 dB is transparent and stable with extreme inputs")
{
    const double sr = 96000.0;
    Biquad b;
    b.setCoefficients (BiquadCoefficients::make (FilterType::Peak, sr, 500.0f, 2.0f, 0.0f));
    testsig::Buffer in (1, 4096);
    testsig::fillNoise (in, 100.0f);
    std::vector<float> ref = in.data[0];
    b.processBlock (0, in.ptrs[0], in.numSamples());
    double errSq = 0.0, refSq = 0.0;
    for (int i = 0; i < in.numSamples(); ++i)
    {
        const double e = double (in.data[0][size_t (i)]) - ref[size_t (i)];
        errSq += e * e; refSq += double (ref[size_t (i)]) * ref[size_t (i)];
    }
    CHECK (std::sqrt (errSq / refSq) < 1e-3); // recursive float rounding only
    CHECK (testsig::allFinite (in));

    // Frequency above Nyquist is clamped, not exploded.
    b.setCoefficients (BiquadCoefficients::make (FilterType::LowPass, 44100.0, 40000.0f, 0.7f, 0.0f));
    b.reset();
    b.processBlock (0, in.ptrs[0], in.numSamples());
    CHECK (testsig::allFinite (in));
}
