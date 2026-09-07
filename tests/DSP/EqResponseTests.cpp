#include "TestFramework.h"
#include "DSP/EqResponse.h"

using namespace livemix;

TEST_CASE ("EqResponse: analytic magnitude matches the filter design")
{
    const double sr = 48000.0;
    auto peak = BiquadCoefficients::make (FilterType::Peak, sr, 1000.0f, 1.0f, 6.0f);
    CHECK_NEAR (EqResponse::biquadMagnitudeDb (peak, sr, 1000.0f), 6.0f, 0.05f);
    CHECK_NEAR (EqResponse::biquadMagnitudeDb (peak, sr, 40.0f), 0.0f, 0.1f);

    ChannelParameters p;
    p.hpfEnabled = true; p.hpfHz = 100.0f; p.hpfSlope = 1;
    p.toneBands[0] = { true, FilterType::LowShelf, 200.0f, -3.0f, 0.7f };
    p.toneBands[2] = { true, FilterType::Peak, 4000.0f, 2.0f, 1.0f };
    CHECK_NEAR (EqResponse::chainMagnitudeDb (p, sr, 100.0f), -3.0f - 3.0f, 0.6f); // 4th-order HPF -3 dB + shelf
    CHECK_NEAR (EqResponse::chainMagnitudeDb (p, sr, 4000.0f), 2.0f, 0.15f);
    p.toneEqEnabled = false;
    CHECK_NEAR (EqResponse::chainMagnitudeDb (p, sr, 4000.0f), 0.0f, 0.15f);
}
