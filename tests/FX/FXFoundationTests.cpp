#include "TestFramework.h"
#include "FX/DelayLine.h"
#include "FX/Allpass.h"
#include "FX/LFO.h"
#include "FX/TempoSync.h"
#include "AllocationTracker.h"
#include <cmath>
#include <vector>

using namespace livemix;

TEST_CASE ("DelayLine: integer delay is exact, fractional read interpolates, and reads never allocate")
{
    DelayLine d;
    d.prepare (100, 2);
    for (int i = 0; i < 200; ++i)
    {
        d.write (0, float (i));
        d.write (1, float (-i));
        if (i >= 37) { CHECK_NEAR (d.read (0, 37), float (i - 37), 1e-6f); CHECK_NEAR (d.read (1, 37), float (-(i - 37)), 1e-6f); }
        if (i >= 11) CHECK_NEAR (d.readFractional (0, 10.5f), float (i - 10) - 0.5f, 1e-5f);
        d.advance();
    }
    {
        alloctrack::Scope scope;
        for (int i = 0; i < 1000; ++i) { d.write (0, 0.5f); (void) d.read (0, 10); (void) d.readFractional (1, 3.3f); d.advance(); }
        CHECK (alloctrack::getCount() == 0);
    }
}

TEST_CASE ("DelayLine: allpass interpolation keeps unity magnitude on a sine")
{
    const double sr = 48000.0;
    DelayLine d;
    d.prepare (200, 1);
    float state = 0.0f;
    double inSq = 0.0, outSq = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const float x = std::sin (2.0f * float (M_PI) * 1000.0f * float (i) / float (sr));
        d.write (0, x);
        const float y = d.readAllpass (0, 23.37f, state);
        d.advance();
        if (i > 1000) { inSq += double (x) * x; outSq += double (y) * y; }
    }
    CHECK_NEAR (10.0 * std::log10 (outSq / inSq), 0.0, 0.1);
}

TEST_CASE ("SchroederAllpass passes energy unchanged; DampedComb rings at its delay period and decays")
{
    SchroederAllpass ap;
    ap.prepare (113, 1);
    ap.setGain (0.6f);
    double energy = 0.0;
    for (int i = 0; i < 20000; ++i)
    {
        const float y = ap.process (0, i == 0 ? 1.0f : 0.0f);
        ap.advance();
        energy += double (y) * y;
    }
    CHECK_NEAR (energy, 1.0, 0.01); // allpass: impulse energy preserved

    DampedComb comb;
    comb.prepare (480, 1);   // 10 ms at 48 kHz -> 100 Hz
    comb.setFeedback (0.7f);
    comb.setDamping (0.0f);
    std::vector<float> ir (4800, 0.0f);
    for (int i = 0; i < 4800; ++i) { ir[size_t (i)] = comb.process (0, i == 0 ? 1.0f : 0.0f); comb.advance(); }
    CHECK_NEAR (ir[480], 1.0f, 1e-6f);
    CHECK_NEAR (ir[960], 0.7f, 1e-6f);
    CHECK_NEAR (ir[1440], 0.49f, 1e-5f);
    CHECK (std::fabs (ir[481]) < 1e-9f);
}

TEST_CASE ("LFO stays in range and repeats at its rate; TempoSync yields the standard delay times")
{
    LFO lfo;
    lfo.prepare (48000.0);
    lfo.setRateHz (2.0f);
    float mn = 1.0f, mx = -1.0f;
    for (int i = 0; i < 48000; ++i) { const float v = lfo.next(); mn = std::min (mn, v); mx = std::max (mx, v); }
    CHECK_NEAR (mn, -1.0f, 0.01f);
    CHECK_NEAR (mx, 1.0f, 0.01f);
    lfo.reset();
    CHECK_NEAR (lfo.next(), 0.0f, 1e-6f);
    lfo.setShape (LFO::Shape::Triangle);
    lfo.setPhase (0.25f);
    CHECK_NEAR (lfo.next(), 0.0f, 1e-5f);

    CHECK_NEAR (TempoSync::delayMs (NoteDivision::Quarter, 120.0), 500.0f, 1e-3f);
    CHECK_NEAR (TempoSync::delayMs (NoteDivision::DottedEighth, 120.0), 375.0f, 1e-3f);
    CHECK_NEAR (TempoSync::delayMs (NoteDivision::Eighth, 90.0), 333.333f, 1e-2f);
    CHECK_NEAR (TempoSync::delayMs (NoteDivision::TripletEighth, 120.0), 166.667f, 1e-2f);
    CHECK_NEAR (TempoSync::delaySamples (NoteDivision::Sixteenth, 120.0, 48000.0), 6000.0f, 1e-2f);
    CHECK (TempoSync::delayMs (NoteDivision::Quarter, 0.0) == TempoSync::delayMs (NoteDivision::Quarter, 20.0)); // clamped
}
