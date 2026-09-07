#include "TestFramework.h"
#include "TestSignals.h"
#include "DSP/ChannelProcessor.h"

using namespace livemix;

TEST_CASE ("ChannelProcessor: loudness-matched A/B plays the original at the processed level")
{
    const double sr = 48000.0;
    ChannelProcessor cp;
    cp.prepare (sr, 256, 1);
    ChannelParameters p;
    p.outputTrimDb = 6.0f;
    cp.setParameters (p);

    testsig::Buffer sig (1, int (sr) * 4);
    testsig::fillDrumHits (sig, sr, 0.4f, 0.01f, 0.25f, 0.1f);
    auto processedRms = 0.0f;
    for (int i = 0; i < sig.numSamples(); i += 256)
    {
        testsig::Buffer blk (1, 256);
        std::copy (sig.data[0].begin() + i, sig.data[0].begin() + i + 256, blk.data[0].begin());
        auto v = blk.view();
        cp.process (v);
        if (i >= sig.numSamples() / 2) processedRms += testsig::rms (blk) * testsig::rms (blk);
    }
    processedRms = std::sqrt (processedRms / float (sig.numSamples() / 2 / 256));

    p.bypassAll = true; p.abLoudnessMatch = true;
    cp.setParameters (p);
    float matchedRms = 0.0f;
    int count = 0;
    for (int i = 0; i < sig.numSamples(); i += 256)
    {
        testsig::Buffer blk (1, 256);
        std::copy (sig.data[0].begin() + i, sig.data[0].begin() + i + 256, blk.data[0].begin());
        auto v = blk.view();
        cp.process (v);
        if (i >= sig.numSamples() / 2) { matchedRms += testsig::rms (blk) * testsig::rms (blk); ++count; }
    }
    matchedRms = std::sqrt (matchedRms / float (count));
    CHECK_NEAR (testsig::toDb (matchedRms), testsig::toDb (processedRms), 0.5f);
    CHECK_NEAR (cp.getLoudnessMatchGainDb(), 6.0f, 0.5f);

    // Without matching, ORIGINAL is bit-exact input once the match gain has settled back to unity.
    p.abLoudnessMatch = false;
    cp.setParameters (p);
    for (int k = 0; k < 250; ++k) { testsig::Buffer t (1, 256); testsig::fillNoise (t, 0.3f, unsigned (k)); auto v = t.view(); cp.process (v); }
    testsig::Buffer blk2 (1, 256);
    testsig::fillNoise (blk2, 0.3f);
    auto ref2 = blk2.data[0];
    { auto v = blk2.view(); cp.process (v); }
    CHECK (blk2.data[0] == ref2);
    CHECK_NEAR (cp.getLoudnessMatchGainDb(), 0.0f, 1e-3f);
}
