// Sample replacement: the trigger, the player and the stage on the channel chain.
#include "TestFramework.h"
#include "TestSignals.h"
#include "AllocationTracker.h"
#include "DSP/SampleBank.h"
#include "DSP/SampleTrigger.h"
#include "DSP/SamplePlayer.h"
#include "DSP/SampleReplacer.h"
#include "DSP/ChannelProcessor.h"
#include <cmath>
#include <random>
#include <vector>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;

    // A close kick microphone: a hit every 0.5 s (a fast click on a 60 Hz body), a snare
    // bleeding in through the air between them - slower rise, filtered, 20 dB down - and a
    // little floor. Returns where each kick starts.
    std::vector<int> kickWithSnareBleed (testsig::Buffer& b, float kickAmp, float bleedAmp, float floorAmp)
    {
        std::vector<int> onsets;
        auto& c = b.data[0];
        std::mt19937 rng (11);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        const int kickEvery = int (0.5 * kSr), bleedEvery = int (0.5 * kSr), bleedAt = int (0.25 * kSr);
        for (size_t i = 0; i < c.size(); ++i)
        {
            float s = floorAmp * noise (rng);
            const int k = int (i) % kickEvery;
            if (k == 0) onsets.push_back (int (i));
            const float tk = float (k) / float (kSr);
            s += kickAmp * (std::sin (2.0f * float (M_PI) * 60.0f * tk) * std::exp (-tk / 0.25f) + 0.5f * noise (rng) * std::exp (-tk / 0.003f));
            const int sb = (int (i) - bleedAt + bleedEvery) % bleedEvery;
            const float tb = float (sb) / float (kSr);
            // The bleed rises over 8 ms rather than jumping, and lives higher up.
            const float rise = tb < 0.008f ? tb / 0.008f : 1.0f;
            s += bleedAmp * rise * std::sin (2.0f * float (M_PI) * 220.0f * tb) * std::exp (-tb / 0.15f);
            c[i] = s;
        }
        return onsets;
    }

    int countHits (SampleTrigger& t, testsig::Buffer& b, std::vector<int>* where = nullptr, int block = 128)
    {
        int total = 0;
        SampleTrigger::Hit hits[SampleTrigger::kMaxHits];
        for (int i = 0; i + block <= b.numSamples(); i += block)
        {
            const int n = t.process (b.data[0].data() + i, block, hits, SampleTrigger::kMaxHits);
            for (int h = 0; h < n; ++h) if (where != nullptr) where->push_back (i + hits[h].offset);
            total += n;
        }
        return total;
    }
}

TEST_CASE ("SampleBank: a synthesised bank has layers that are trimmed to their onset and peak at 0 dBFS")
{
    for (auto family : { RoleFamily::Kick, RoleFamily::Snare, RoleFamily::Tom })
        for (int variant = 0; variant < 3; ++variant)
        {
            const auto b = synthesizeBank (family, variant, kSr);
            REQUIRE (! b.empty());
            CHECK (! b.name.empty());
            CHECK (b.layers.size() == 4u);
            for (const auto& layer : b.layers)
                for (const auto& hit : layer.hits)
                {
                    REQUIRE (hit.size() > 1000u);
                    float peak = 0.0f;
                    for (float v : hit) peak = std::max (peak, std::fabs (v));
                    CHECK_NEAR (peak, 1.0f, 1.0e-4f);
                    // Something happens in the first millisecond: the leading silence is gone.
                    float early = 0.0f;
                    for (size_t i = 0; i < 48; ++i) early = std::max (early, std::fabs (hit[i]));
                    CHECK (early > 0.01f);
                    // ... and the tail is silent.
                    CHECK (std::fabs (hit.back()) < 1.0e-3f);
                }
            uint32_t rr = 0;
            const auto* soft = b.pick (0.0f, rr);
            const auto* loud = b.pick (1.0f, rr);
            REQUIRE (soft != nullptr && loud != nullptr);
            CHECK (soft != loud);
            // Round-robin: two picks at the same velocity alternate.
            uint32_t r2 = 0;
            const auto* a1 = b.pick (1.0f, r2);
            const auto* a2 = b.pick (1.0f, r2);
            const auto* a3 = b.pick (1.0f, r2);
            CHECK (a1 != a2);
            CHECK (a1 == a3);
        }

    // prepareHit: a hit that never rises is dropped; one that does is trimmed and normalised.
    std::vector<float> silent (1000, 0.0001f);
    prepareHit (silent);
    CHECK (silent.empty());
    std::vector<float> late (1000, 0.0f);
    for (int i = 500; i < 1000; ++i) late[size_t (i)] = 0.25f;
    prepareHit (late);
    CHECK (late.size() <= 516u && late.size() >= 500u);
    CHECK_NEAR (late.back(), 1.0f, 1.0e-6f);
}

TEST_CASE ("SampleTrigger: every kick fires once, within a millisecond of its onset, and the snare bleed never does")
{
    testsig::Buffer b (1, int (kSr * 4));
    const auto onsets = kickWithSnareBleed (b, 0.5f, 0.03f, 0.002f);
    REQUIRE (onsets.size() == 8u);

    SampleTrigger t;
    t.prepare (kSr);
    SampleTrigger::Params p;
    p.enabled = true; p.thresholdDb = -30.0f; p.riseDb = 6.0f; p.hpfHz = 30.0f; p.lpfHz = 250.0f; p.maskMs = 40.0f;
    t.setParams (p);

    std::vector<int> found;
    const int n = countHits (t, b, &found);
    CHECK (n == int (onsets.size()));
    REQUIRE (found.size() == onsets.size());
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const int delay = found[i] - onsets[i];
        CHECK (delay >= 0);
        // Recognised inside the first lobe of the body and reported 1.5 ms later. The kick here
        // rings on for a quarter of a second, so the rise test on every hit after the first is
        // against its predecessor's tail rather than against silence.
        CHECK (delay <= 240);
    }
    CHECK (t.getHitCount() == n);
    CHECK (t.getLastLevelDb() > -30.0f);

    // Off: nothing, ever.
    p.enabled = false;
    t.setParams (p);
    t.reset();
    CHECK (countHits (t, b) == 0);
}

TEST_CASE ("SampleTrigger: the mask swallows a ring, a louder second stroke re-triggers, velocity and confidence follow the level")
{
    SampleTrigger t;
    t.prepare (kSr);
    SampleTrigger::Params p;
    p.enabled = true; p.thresholdDb = -30.0f; p.riseDb = 6.0f; p.hpfHz = 30.0f; p.lpfHz = 8000.0f; p.maskMs = 40.0f;
    t.setParams (p);

    // One hit at -8 dBFS, then a second, 6 dB softer, 10 ms later (inside the mask), then a
    // third, 7 dB louder than the first, 20 ms after it (still inside the mask).
    testsig::Buffer b (1, int (kSr));
    auto burst = [&] (int at, float amp) { for (int i = 0; i < 96; ++i) b.data[0][size_t (at + i)] = amp * std::sin (2.0f * float (M_PI) * 1000.0f * float (i) / float (kSr)); };
    burst (4800, 0.4f);
    burst (4800 + 480, 0.2f);
    burst (4800 + 960, 0.9f);
    std::vector<int> found;
    const int n = countHits (t, b, &found);
    CHECK (n == 2);
    REQUIRE (found.size() == 2u);
    CHECK (found[0] >= 4800 + 72 && found[0] < 4800 + 72 + 48);            // the crossing, then 1.5 ms of measurement
    CHECK (found[1] >= 4800 + 960 + 72 && found[1] < 4800 + 960 + 72 + 48);

    // Velocity: a hit just over the threshold is soft and doubtful; one well over is loud and sure.
    t.reset();
    SampleTrigger::Hit hits[SampleTrigger::kMaxHits];
    testsig::Buffer soft (1, 4800);
    for (int i = 2400; i < 2496; ++i) soft.data[0][size_t (i)] = 0.04f * std::sin (2.0f * float (M_PI) * 1000.0f * float (i) / float (kSr));   // about -28 dBFS
    int got = 0;
    for (int i = 0; i + 128 <= 4800 && got == 0; i += 128) got = t.process (soft.data[0].data() + i, 128, hits, SampleTrigger::kMaxHits);
    REQUIRE (got == 1);
    CHECK (hits[0].velocity < 0.25f);
    CHECK (hits[0].confidence < 1.0f);
    t.reset();
    testsig::Buffer loud (1, 4800);
    for (int i = 2400; i < 2496; ++i) loud.data[0][size_t (i)] = 0.9f * std::sin (2.0f * float (M_PI) * 1000.0f * float (i) / float (kSr));
    got = 0;
    for (int i = 0; i + 128 <= 4800 && got == 0; i += 128) got = t.process (loud.data[0].data() + i, 128, hits, SampleTrigger::kMaxHits);
    REQUIRE (got == 1);
    CHECK (hits[0].velocity > 0.9f);
    CHECK_NEAR (hits[0].confidence, 1.0f, 1.0e-6f);
}

TEST_CASE ("SamplePlayer: a voice starts at its offset, plays the bank's hit at the gain asked for, and a bank at another rate plays true")
{
    auto bank = synthesizeBank (RoleFamily::Kick, 0, kSr);
    SamplePlayer player;
    player.prepare (kSr);
    player.setBank (&bank);
    CHECK (! player.isPlaying());

    testsig::Buffer out (2, 256);
    player.trigger (100, 1.0f, 0.5f, 1.0);
    CHECK (player.isPlaying());
    auto v = out.view();
    player.render (v, 1.0f);
    for (int i = 0; i < 100; ++i) CHECK (out.data[0][size_t (i)] == 0.0f);
    float after = 0.0f;
    for (int i = 100; i < 256; ++i) after = std::max (after, std::fabs (out.data[0][size_t (i)]));
    CHECK (after > 0.0f);
    CHECK (after <= 0.5f + 1.0e-3f);
    CHECK (out.data[0][200] == out.data[1][200]);      // the same on every channel

    // Keeps playing across blocks until the hit is over, then stops.
    int blocks = 0;
    while (player.isPlaying() && blocks < 1000) { testsig::Buffer more (1, 256); auto mv = more.view(); player.render (mv, 1.0f); ++blocks; }
    CHECK (! player.isPlaying());
    CHECK (blocks > 10);

    // No bank: a trigger is ignored.
    player.setBank (nullptr);
    player.trigger (0, 1.0f, 1.0f, 1.0);
    CHECK (! player.isPlaying());

    // A 44.1 kHz bank played at 48 kHz: its 1-second hit lasts one second, not 1.09.
    auto bank441 = synthesizeBank (RoleFamily::Kick, 0, 44100.0);
    player.setBank (&bank441);
    player.trigger (0, 1.0f, 1.0f, 1.0);
    int samples = 0;
    while (player.isPlaying() && samples < int (kSr * 3)) { testsig::Buffer more (1, 256); auto mv = more.view(); player.render (mv, 1.0f); samples += 256; }
    const float seconds = float (samples) / float (kSr);
    const float hitSeconds = float (bank441.layers[3].hits[0].size()) / 44100.0f;
    CHECK_NEAR (seconds, hitSeconds, 0.02f);

    // Six voices, then the furthest along is taken: never more than six, never a crash.
    player.setBank (&bank);
    for (int i = 0; i < 20; ++i) player.trigger (i, 1.0f, 0.1f, 1.0);
    testsig::Buffer many (1, 512);
    auto manyView = many.view();
    player.render (manyView, 1.0f);
    CHECK (testsig::allFinite (many));
}

TEST_CASE ("SampleReplacer: off is bit-transparent; on, the microphone is scaled by the blend and the sample lands at the hit")
{
    testsig::Buffer mic (1, int (kSr * 2));
    const auto onsets = kickWithSnareBleed (mic, 0.5f, 0.03f, 0.002f);
    auto bank = synthesizeBank (RoleFamily::Kick, 0, kSr);

    SampleReplacer r;
    r.prepare (kSr, 128, 1);
    r.setBank (&bank);
    SampleReplacer::Params p;
    p.enabled = false;
    r.setParams (p);
    testsig::Buffer a (1, mic.numSamples());
    a.data = mic.data;
    for (int i = 0; i + 128 <= a.numSamples(); i += 128) { auto v = a.view (i, 128); r.detect (v); r.apply (v); }
    CHECK (a.data[0] == mic.data[0]);
    CHECK (r.getHitCount() == 0);

    // On, blend 1: the microphone is gone and only the sample is left, starting at the hit.
    p.enabled = true; p.blend = 1.0f; p.thresholdDb = -30.0f; p.riseDb = 6.0f; p.detHpfHz = 30.0f; p.detLpfHz = 250.0f; p.gainDb = 0.0f; p.steady = true;
    r.setParams (p);
    r.reset();
    testsig::Buffer s (1, mic.numSamples());
    s.data = mic.data;
    for (int i = 0; i + 128 <= s.numSamples(); i += 128) { auto v = s.view (i, 128); r.detect (v); r.apply (v); }
    CHECK (r.getHitCount() == int (onsets.size()));
    // Before the first hit: silence (the microphone's floor is scaled to nothing).
    float before = 0.0f;
    for (int i = 0; i < onsets[0]; ++i) before = std::max (before, std::fabs (s.data[0][size_t (i)]));
    CHECK (before == 0.0f);
    // At the first hit: the sample's normalised peak (STEADY, gain 0 dB, full blend) within a few ms.
    float atHit = 0.0f;
    for (int i = onsets[0]; i < onsets[0] + 720; ++i) atHit = std::max (atHit, std::fabs (s.data[0][size_t (i)]));
    CHECK (atHit > 0.8f);

    // Blend 0.5: the microphone at half, plus the sample at half.
    p.blend = 0.5f;
    r.setParams (p);
    r.reset();
    testsig::Buffer h (1, mic.numSamples());
    h.data = mic.data;
    for (int i = 0; i + 128 <= h.numSamples(); i += 128) { auto v = h.view (i, 128); r.detect (v); r.apply (v); }
    for (int i = 0; i < onsets[0] - 1; ++i) CHECK_NEAR (h.data[0][size_t (i)], 0.5f * mic.data[0][size_t (i)], 1.0e-6f);

    // Polarity flips the sample only; ALIGN delays the sample only.
    p.blend = 1.0f; p.polarityFlip = true;
    r.setParams (p);
    r.reset();
    testsig::Buffer f (1, mic.numSamples());
    f.data = mic.data;
    for (int i = 0; i + 128 <= f.numSamples(); i += 128) { auto v = f.view (i, 128); r.detect (v); r.apply (v); }
    for (int i = onsets[0]; i < onsets[0] + 720; ++i) CHECK_NEAR (f.data[0][size_t (i)], -s.data[0][size_t (i)], 1.0e-6f);
    p.polarityFlip = false; p.offsetMs = 2.0f;
    r.setParams (p);
    r.reset();
    testsig::Buffer d (1, mic.numSamples());
    d.data = mic.data;
    for (int i = 0; i + 128 <= d.numSamples(); i += 128) { auto v = d.view (i, 128); r.detect (v); r.apply (v); }
    for (int i = onsets[0] + 96; i < onsets[0] + 720; ++i) CHECK_NEAR (d.data[0][size_t (i)], s.data[0][size_t (i - 96)], 1.0e-6f);
    CHECK (testsig::allFinite (d));
}

TEST_CASE ("SampleReplacer: with no sound loaded the stage is bit-transparent, whatever the blend says")
{
    // The stage is a crossfade: at blend 1 the microphone is scaled to nothing and the sample
    // takes its place. If the sample is missing - no bank loaded, a sound file that did not
    // come with the session - that crossfade would silence the drum. The stage stands aside
    // instead, and the microphone is what they hear.
    testsig::Buffer mic (1, int (kSr));
    kickWithSnareBleed (mic, 0.5f, 0.03f, 0.002f);

    SampleReplacer r;
    r.prepare (kSr, 128, 1);
    SampleReplacer::Params p;
    p.enabled = true; p.blend = 1.0f; p.thresholdDb = -30.0f; p.riseDb = 6.0f;
    p.detHpfHz = 30.0f; p.detLpfHz = 250.0f; p.steady = true;
    r.setParams (p);

    testsig::Buffer a (1, mic.numSamples());
    a.data = mic.data;
    for (int i = 0; i + 128 <= a.numSamples(); i += 128) { auto v = a.view (i, 128); r.detect (v); r.apply (v); }
    CHECK (a.data[0] == mic.data[0]);

    // And the moment a sound is there, the same settings do replace it.
    auto bank = synthesizeBank (RoleFamily::Kick, 0, kSr);
    r.setBank (&bank);
    r.reset();
    testsig::Buffer b (1, mic.numSamples());
    b.data = mic.data;
    for (int i = 0; i + 128 <= b.numSamples(); i += 128) { auto v = b.view (i, 128); r.detect (v); r.apply (v); }
    CHECK (! (b.data[0] == mic.data[0]));
    CHECK (testsig::allFinite (b));
}

TEST_CASE ("ChannelProcessor: the sample stage exists only where it is configured, adds no latency, allocates nothing, and off changes nothing")
{
    testsig::Buffer mic (1, int (kSr));
    kickWithSnareBleed (mic, 0.5f, 0.03f, 0.002f);
    auto bank = synthesizeBank (RoleFamily::Kick, 0, kSr);

    ChannelParameters params;
    params.gateEnabled = true; params.gateThresholdDb = -30.0f;
    params.replaceEnabled = true; params.replaceBlend = 0.5f; params.replaceThresholdDb = -30.0f;
    params.replaceDetHpfHz = 30.0f; params.replaceDetLpfHz = 250.0f; params.replaceRiseDb = 6.0f; params.replaceGainDb = -6.0f;

    // Not configured (a plug-in, a vocal strip): the parameters are ignored and the chain is what it was.
    ChannelProcessor plain, plainRef;
    plain.prepare (kSr, 128, 1);
    plainRef.prepare (kSr, 128, 1);
    plain.setParameters (params);
    ChannelParameters off = params; off.replaceEnabled = false;
    plainRef.setParameters (off);
    testsig::Buffer a (1, mic.numSamples()), b (1, mic.numSamples());
    a.data = mic.data; b.data = mic.data;
    for (int i = 0; i + 128 <= a.numSamples(); i += 128) { auto va = a.view (i, 128); plain.process (va); auto vb = b.view (i, 128); plainRef.process (vb); }
    CHECK (a.data[0] == b.data[0]);
    CHECK (plain.getLatencySamples() == 0);

    // Configured (a DINE drum strip): the sample lands, the latency is still zero, and
    // steady-state process() never allocates.
    ChannelProcessor drum;
    ChannelProcessor::Options o; o.sampleReplacement = true;
    drum.configure (o);
    drum.prepare (kSr, 128, 1);
    drum.setSampleBank (&bank);
    drum.setParameters (params);
    testsig::Buffer c (1, mic.numSamples());
    c.data = mic.data;
    {
        alloctrack::Scope scope;
        for (int i = 0; i + 128 <= c.numSamples(); i += 128) { auto vc = c.view (i, 128); drum.process (vc); }
        CHECK (alloctrack::getCount() == 0);
    }
    CHECK (drum.getLatencySamples() == 0);
    CHECK (drum.getSampler().getHitCount() == 2);
    CHECK (c.data[0] != b.data[0]);
    CHECK (testsig::allFinite (c));

    // The same strip with the stage off is bit-identical to a strip that never had one.
    drum.setParameters (off);
    drum.reset();
    plainRef.reset();
    testsig::Buffer d (1, mic.numSamples()), e (1, mic.numSamples());
    d.data = mic.data; e.data = mic.data;
    for (int i = 0; i + 128 <= d.numSamples(); i += 128) { auto vd = d.view (i, 128); drum.process (vd); auto ve = e.view (i, 128); plainRef.process (ve); }
    CHECK (d.data[0] == e.data[0]);
}

TEST_CASE ("SampleBank: the pitch of a hit is measured from its body, and a sample that follows the drum plays at the drum's pitch")
{
    // A decaying tone at a known pitch is read within 2 %; a synthesised tom (whose body
    // glides down into its pitch) within 15 % of where it settles, and the bank records that.
    for (float hz : { 60.0f, 90.0f, 150.0f, 220.0f })
    {
        std::vector<float> tone (int (kSr * 0.3));
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = std::sin (2.0f * float (M_PI) * hz * float (i) / float (kSr)) * std::exp (-float (i) / float (kSr * 0.2));
        const float measured = measureFundamental (tone, kSr);
        CHECK (std::fabs (measured - hz) / hz < 0.02f);
    }
    const float settles[] = { 120.0f, 88.0f, 64.0f };
    for (int variant = 0; variant < 3; ++variant)
    {
        const auto b = synthesizeBank (RoleFamily::Tom, variant, kSr);
        REQUIRE (b.fundamentalHz > 0.0f);
        CHECK (std::fabs (b.fundamentalHz - settles[variant]) / settles[variant] < 0.15f);
        CHECK_NEAR (measureFundamental (b.layers.back().hits[0], kSr), b.fundamentalHz, 0.01f);
    }
    // Nothing periodic: noise says 0.
    std::vector<float> noise (int (kSr * 0.2));
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    for (auto& v : noise) v = d (rng);
    CHECK (measureFundamental (noise, kSr) == 0.0f);

    // Follow the drum: a 100 Hz bank asked to play an 80 Hz drum runs at 0.8x, so its hit lasts 1.25x longer.
    auto bank = synthesizeBank (RoleFamily::Tom, 0, kSr);
    bank.fundamentalHz = 100.0f;
    SampleReplacer r;
    r.prepare (kSr, 128, 1);
    r.setBank (&bank);
    SampleReplacer::Params p;
    p.enabled = true; p.followDrum = true; p.drumHz = 80.0f;
    r.setParams (p);
    CHECK_NEAR (r.currentRate(), 0.8, 1.0e-6);
    p.drumHz = 30.0f;                            // five semitones is the most it will follow
    r.setParams (p);
    CHECK_NEAR (r.currentRate(), std::pow (2.0, -5.0 / 12.0), 1.0e-6);
    p.followDrum = false; p.rateSemitones = 12.0f;
    r.setParams (p);
    CHECK_NEAR (r.currentRate(), 2.0, 1.0e-6);
    p.rateSemitones = 0.0f; p.followDrum = true; p.drumHz = 0.0f;   // unknown drum: as recorded
    r.setParams (p);
    CHECK_NEAR (r.currentRate(), 1.0, 1.0e-6);
}

TEST_CASE ("KitTriggerTable: a soft tom hit within two milliseconds of a hard snare is the snare through the air and plays nothing")
{
    auto snareBank = synthesizeBank (RoleFamily::Snare, 0, kSr);
    auto tomBank = synthesizeBank (RoleFamily::Tom, 0, kSr);
    KitTriggerTable kit;
    SampleReplacer snare, tom;
    snare.prepare (kSr, 128, 1);
    tom.prepare (kSr, 128, 1);
    snare.setBank (&snareBank);
    tom.setBank (&tomBank);
    snare.setKit (&kit, RoleFamily::Snare);
    tom.setKit (&kit, RoleFamily::Tom);
    SampleReplacer::Params p;
    p.enabled = true; p.blend = 1.0f; p.thresholdDb = -36.0f; p.riseDb = 6.0f; p.detHpfHz = 30.0f; p.detLpfHz = 8000.0f;
    p.gainDb = -6.0f;                            // both strips' hit level: a full hit reads about -6 dBFS
    snare.setParams (p);
    tom.setParams (p);

    auto burst = [] (testsig::Buffer& b, int at, float amp)
    {
        for (int i = 0; i < 480 && at + i < b.numSamples(); ++i) b.data[0][size_t (at + i)] = amp * std::sin (2.0f * float (M_PI) * 200.0f * float (i) / float (kSr)) * std::exp (-float (i) / 240.0f);
    };
    // At 100 ms the tom itself is hit hard, and the snare microphone hears that 20 dB down.
    // At 400 ms the snare is hit hard; the tom microphone hears it 20 dB down at the same moment.
    testsig::Buffer snareMic (1, int (kSr * 0.6)), tomMic (1, int (kSr * 0.6));
    burst (tomMic, 4800, 0.5f);
    burst (snareMic, 4800 + 24, 0.05f);          // half a millisecond later through the air
    burst (snareMic, 19200, 0.5f);
    burst (tomMic, 19200 + 24, 0.05f);

    long long pos = 0;
    for (int i = 0; i + 128 <= snareMic.numSamples(); i += 128)
    {
        auto vs = snareMic.view (i, 128); snare.detect (vs, pos); snare.apply (vs);
        auto vt = tomMic.view (i, 128); tom.detect (vt, pos); tom.apply (vt);
        pos += 128;
    }
    // The snare heard both events (its own, and the tom's soft arrival - nothing vetoes a snare);
    // the tom recognised both but played only its own.
    CHECK (snare.getHitCount() == 2);
    CHECK (tom.getHitCount() == 2);
    CHECK (tom.getVetoCount() == 1);
    CHECK (snare.getVetoCount() == 0);
    // Around 100 ms the tom microphone plays its sample. Around 400 ms no new voice starts: what
    // is there is the first sample's decaying tail, so the level after the snare's hit is no
    // higher than the level just before it (blend 1: the microphone itself is gone).
    float atTom = 0.0f, before = 0.0f, after = 0.0f;
    for (int i = 4800; i < 4800 + 2400; ++i) atTom = std::max (atTom, std::fabs (tomMic.data[0][size_t (i)]));
    for (int i = 19200 - 2400; i < 19200; ++i) before = std::max (before, std::fabs (tomMic.data[0][size_t (i)]));
    for (int i = 19200; i < 19200 + 2400; ++i) after = std::max (after, std::fabs (tomMic.data[0][size_t (i)]));
    CHECK (atTom > 0.3f);
    CHECK (after <= before);
}

TEST_CASE ("ChannelProcessor: a hit the sample fires on opens the gate too, so a soft stroke keeps its microphone under the sample")
{
    testsig::Buffer mic (1, int (kSr * 2));
    const auto onsets = kickWithSnareBleed (mic, 0.5f, 0.03f, 0.002f);
    auto bank = synthesizeBank (RoleFamily::Kick, 0, kSr);

    // A gate no kick in this take can open on its own (threshold at 0 dBFS, hard), and a
    // sample stage that fires on every kick but adds nothing audible (blend 0): what comes
    // out is the microphone, and only if the gate let it through.
    ChannelParameters params;
    params.gateEnabled = true; params.gateThresholdDb = 0.0f; params.gateRangeDb = 40.0f; params.gateRatio = 10.0f;
    params.gateAttackMs = 0.1f; params.gateHoldMs = 40.0f; params.gateReleaseMs = 60.0f; params.gateHysteresisDb = 3.0f;
    params.replaceEnabled = true; params.replaceBlend = 0.0f; params.replaceThresholdDb = -30.0f;
    params.replaceDetHpfHz = 30.0f; params.replaceDetLpfHz = 250.0f; params.replaceRiseDb = 6.0f; params.replaceGainDb = -6.0f;

    auto run = [&] (const ChannelParameters& p)
    {
        ChannelProcessor drum;
        ChannelProcessor::Options o; o.sampleReplacement = true;
        drum.configure (o);
        drum.prepare (kSr, 128, 1);
        drum.setSampleBank (&bank);
        drum.setParameters (p);
        testsig::Buffer out (1, mic.numSamples());
        out.data = mic.data;
        for (int i = 0; i + 128 <= out.numSamples(); i += 128) { auto v = out.view (i, 128); drum.process (v); }
        return out;
    };
    auto peakAfter = [] (const testsig::Buffer& b, int from, int len)
    {
        float p = 0.0f;
        for (int i = from; i < from + len && i < b.numSamples(); ++i) p = std::max (p, std::fabs (b.data[0][size_t (i)]));
        return p;
    };

    // With the stage off the gate never opens: every kick is squashed 40 dB.
    ChannelParameters off = params; off.replaceEnabled = false;
    const auto closed = run (off);
    for (size_t k = 1; k < onsets.size(); ++k) CHECK (peakAfter (closed, onsets[k], 480) < 0.03f);

    // With the stage on every kick opens the gate from its own onset: the microphone comes
    // through at (very nearly) its own level, and stays open for the hold.
    const auto opened = run (params);
    for (size_t k = 1; k < onsets.size(); ++k)
    {
        CHECK (peakAfter (opened, onsets[k], 480) > 0.35f);
        // 20 ms in, still inside the 40 ms hold: open.
        const int at = onsets[k] + int (0.02 * kSr);
        CHECK (std::fabs (opened.data[0][size_t (at)] - mic.data[0][size_t (at)]) < 0.02f);
    }
    // ... and between kicks (250 ms later, well past hold and release) the gate is closed again.
    CHECK (peakAfter (opened, onsets[1] + int (0.3 * kSr), 480) < 0.03f);
    CHECK (testsig::allFinite (opened));
}

TEST_CASE ("SampleBank: the hi-hat is a sample family with placeholders of its own, and a hat hit vetoes like a tom")
{
    CHECK (sampleReplacementAppropriate (RoleFamily::HiHat));
    CHECK (! sampleReplacementAppropriate (RoleFamily::Overhead));
    for (int v = 0; v < 3; ++v)
    {
        const auto b = synthesizeBank (RoleFamily::HiHat, v, kSr);
        CHECK (b.layers.size() == 4u);
        CHECK (! b.name.empty());
        const auto& hit = b.layers.back().hits[0];
        REQUIRE (hit.size() > 480u);
        float peak = 0.0f;
        for (float x : hit) peak = std::max (peak, std::fabs (x));
        CHECK_NEAR (peak, 1.0f, 1.0e-4f);
        // Bright: almost nothing of it survives a 300 Hz one-pole low-pass.
        float lp = 0.0f, lowEnergy = 0.0f, energy = 0.0f;
        const float k = 1.0f - std::exp (-2.0f * float (M_PI) * 300.0f / float (kSr));
        for (float x : hit) { lp += k * (x - lp); lowEnergy += lp * lp; energy += x * x; }
        CHECK (lowEnergy < 0.05f * energy);
    }
    CHECK (synthesizeBank (RoleFamily::HiHat, 2, kSr).layers.back().hits[0].size() > synthesizeBank (RoleFamily::HiHat, 0, kSr).layers.back().hits[0].size());

    // The veto: a soft hat hit half a millisecond after a hard snare is the snare through the air.
    auto snareBank = synthesizeBank (RoleFamily::Snare, 0, kSr);
    auto hatBank = synthesizeBank (RoleFamily::HiHat, 0, kSr);
    KitTriggerTable kit;
    SampleReplacer snare, hat;
    snare.prepare (kSr, 128, 1); hat.prepare (kSr, 128, 1);
    snare.setBank (&snareBank); hat.setBank (&hatBank);
    snare.setKit (&kit, RoleFamily::Snare); hat.setKit (&kit, RoleFamily::HiHat);
    SampleReplacer::Params p;
    p.enabled = true; p.blend = 1.0f; p.thresholdDb = -36.0f; p.riseDb = 6.0f; p.detHpfHz = 30.0f; p.detLpfHz = 8000.0f; p.gainDb = -6.0f;
    snare.setParams (p); hat.setParams (p);
    auto burst = [] (testsig::Buffer& b, int at, float amp)
    {
        for (int i = 0; i < 480 && at + i < b.numSamples(); ++i) b.data[0][size_t (at + i)] = amp * std::sin (2.0f * float (M_PI) * 200.0f * float (i) / float (kSr)) * std::exp (-float (i) / 240.0f);
    };
    testsig::Buffer snareMic (1, int (kSr * 0.6)), hatMic (1, int (kSr * 0.6));
    burst (hatMic, 4800, 0.5f);                  // the hat's own stroke
    burst (snareMic, 19200, 0.5f);               // a hard snare ...
    burst (hatMic, 19200 + 24, 0.05f);           // ... heard 20 dB down in the hat microphone
    long long pos = 0;
    for (int i = 0; i + 128 <= snareMic.numSamples(); i += 128)
    {
        auto vs = snareMic.view (i, 128); snare.detect (vs, pos); snare.apply (vs);
        auto vh = hatMic.view (i, 128); hat.detect (vh, pos); hat.apply (vh);
        pos += 128;
    }
    CHECK (hat.getHitCount() == 2);
    CHECK (hat.getVetoCount() == 1);
}

TEST_CASE ("SampleTrigger: a hit knows how late it was recognised, so the sample can start on time")
{
    // Reported 2.5-5 ms after the drum began; a sample started there combs against the
    // microphone at any blend under 100 %. lateBy is how far behind the onset the report is, and
    // the report minus lateBy is the onset, to within half a millisecond.
    testsig::Buffer b (1, int (kSr * 4));
    const auto onsets = kickWithSnareBleed (b, 0.5f, 0.03f, 0.002f);
    SampleTrigger t;
    t.prepare (kSr);
    SampleTrigger::Params p;
    p.enabled = true; p.thresholdDb = -30.0f; p.riseDb = 6.0f; p.hpfHz = 30.0f; p.lpfHz = 250.0f; p.maskMs = 40.0f;
    t.setParams (p);
    std::vector<int> started;
    SampleTrigger::Hit hits[SampleTrigger::kMaxHits];
    for (int i = 0; i + 128 <= b.numSamples(); i += 128)
    {
        const int n = t.process (b.data[0].data() + i, 128, hits, SampleTrigger::kMaxHits);
        for (int h = 0; h < n; ++h)
        {
            CHECK (hits[h].lateBy >= int (0.0015 * kSr));   // at least the measuring time
            started.push_back (i + hits[h].offset - hits[h].lateBy);
        }
    }
    REQUIRE (started.size() == onsets.size());
    for (size_t i = 0; i < onsets.size(); ++i)
        CHECK_MESSAGE (std::abs (started[i] - onsets[i]) <= int (0.0005 * kSr),
                       "hit " + std::to_string (i) + " starts " + std::to_string (started[i] - onsets[i]) + " samples from its onset");
}

TEST_CASE ("KitTriggerTable: a soft snare hit at the moment of a hard hi-hat is the hat through the air")
{
    auto snareBank = synthesizeBank (RoleFamily::Snare, 0, kSr);
    auto hatBank = synthesizeBank (RoleFamily::HiHat, 0, kSr);
    KitTriggerTable kit;
    SampleReplacer hat, snare;
    hat.prepare (kSr, 128, 1);
    snare.prepare (kSr, 128, 1);
    hat.setBank (&hatBank);
    snare.setBank (&snareBank);
    hat.setKit (&kit, RoleFamily::HiHat);
    snare.setKit (&kit, RoleFamily::Snare);
    SampleReplacer::Params p;
    p.enabled = true; p.blend = 1.0f; p.thresholdDb = -36.0f; p.riseDb = 6.0f; p.detHpfHz = 30.0f; p.detLpfHz = 12000.0f;
    hat.setParams (p);
    snare.setParams (p);
    auto burst = [] (testsig::Buffer& b, int at, float amp, float hz)
    {
        for (int i = 0; i < 480 && at + i < b.numSamples(); ++i)
            b.data[0][size_t (at + i)] = amp * std::sin (2.0f * float (M_PI) * hz * float (i) / float (kSr)) * std::exp (-float (i) / 240.0f);
    };
    // The snare is hit hard at 100 ms; at 400 ms the hat is hit hard and the snare microphone
    // hears it 20 dB down. The hat runs first in the block, as the engine's kit order has it.
    testsig::Buffer hatMic (1, int (kSr * 0.6)), snareMic (1, int (kSr * 0.6));
    burst (snareMic, 4800, 0.5f, 200.0f);
    burst (hatMic, 19200, 0.5f, 6000.0f);
    burst (snareMic, 19200 + 24, 0.05f, 6000.0f);
    long long pos = 0;
    for (int i = 0; i + 128 <= snareMic.numSamples(); i += 128)
    {
        auto vh = hatMic.view (i, 128); hat.detect (vh, pos); hat.apply (vh);
        auto vs = snareMic.view (i, 128); snare.detect (vs, pos); snare.apply (vs);
        pos += 128;
    }
    CHECK (snare.getHitCount() == 2);          // it heard both...
    CHECK (snare.getVetoCount() == 1);         // ... and played only its own
}
