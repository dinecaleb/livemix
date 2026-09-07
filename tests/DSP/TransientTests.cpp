#include "TestFramework.h"
#include "TestSignals.h"
#include "DSP/TransientProcessor.h"

using namespace livemix;

namespace
{
    // Ratio of peak to late-sustain level of a synthetic hit after processing.
    struct HitShape { float attackPeak; float sustainRms; };

    HitShape shape (float attack, float sustain)
    {
        const double sr = 48000.0;
        TransientProcessor t;
        t.prepare (sr, 512, 1);
        t.setParams ({ true, attack, sustain });
        testsig::Buffer in (1, int (sr) / 2);
        testsig::fillDrumHits (in, sr, 0.5f, 0.0f, 0.5f, 0.15f);
        auto v = in.view();
        t.process (v);
        float pk = 0.0f;
        for (int i = 0; i < 480; ++i) pk = std::max (pk, std::fabs (in.data[0][size_t (i)]));
        return { pk, testsig::rms (in, 0, 4800, 9600) };
    }
}

TEST_CASE ("TransientProcessor: attack and sustain move in the expected direction")
{
    const auto neutral = shape (0.0f, 0.0f);
    const auto moreAttack = shape (0.8f, 0.0f);
    const auto lessSustain = shape (0.0f, -0.8f);
    const auto moreSustain = shape (0.0f, 0.8f);

    CHECK (moreAttack.attackPeak > neutral.attackPeak * 1.2f);
    CHECK (lessSustain.sustainRms < neutral.sustainRms * 0.7f);
    CHECK (moreSustain.sustainRms > neutral.sustainRms * 1.2f);
}

TEST_CASE ("TransientProcessor: neutral setting is transparent and disabled is bit-exact")
{
    const double sr = 48000.0;
    TransientProcessor t;
    t.prepare (sr, 512, 2);
    t.setParams ({ true, 0.0f, 0.0f });
    testsig::Buffer in (2, 4096);
    testsig::fillDrumHits (in, sr, 0.5f, 0.001f, 0.1f, 0.05f);
    auto ref = in.data[0];
    auto v = in.view();
    t.process (v);
    CHECK (in.data[0] == ref);

    t.setParams ({ false, 1.0f, 1.0f });
    t.process (v);
    CHECK (in.data[0] == ref);
}
