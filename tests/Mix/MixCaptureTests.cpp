#include "TestFramework.h"
#include "TestSignals.h"
#include "AllocationTracker.h"
#include "Mix/MixEngine.h"
#include "Mix/MixCapture.h"
#include <chrono>
#include <thread>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;

    MixSession session()
    {
        MixSession s;
        s.inputs = {
            { "Kick", ChannelRole::KickIn,    0, -1 },
            { "Keys", ChannelRole::Piano,     1,  2 },
            { "Lead", ChannelRole::LeadVocal, 3, -1 },
            { "Quiet", ChannelRole::Speech,   4, -1 },
        };
        return s;
    }

    // Pushes a device buffer through the engine block by block, pacing a little so the
    // capture worker can keep up the way it would in real time. Buffers are allocated
    // on construction so run() itself is allocation-free.
    struct Streamer
    {
        testsig::Buffer& in;
        int block;
        std::vector<const float*> ip;
        std::vector<float> outL, outR;
        Streamer (testsig::Buffer& buffer, int blockSize)
            : in (buffer), block (blockSize), ip (buffer.ptrs.size()),
              outL (static_cast<size_t> (blockSize)), outR (static_cast<size_t> (blockSize)) {}
        void run (MixEngine& e, bool paced = true)
        {
            float* op[2] = { outL.data(), outR.data() };
            for (int i = 0, k = 0; i + block <= in.numSamples(); i += block, ++k)
            {
                for (size_t c = 0; c < ip.size(); ++c) ip[c] = in.ptrs[c] + i;
                e.process (ip.data(), int (ip.size()), op, 2, block);
                if (paced && (k % 8) == 0) std::this_thread::sleep_for (std::chrono::microseconds (600));
            }
        }
    };
    void stream (MixEngine& e, testsig::Buffer& in, int block) { Streamer s (in, block); s.run (e); }

    bool waitFor (MixCapture& c, MixCapture::State s, int ms = 5000)
    {
        for (int i = 0; i < ms; ++i)
        {
            if (c.getState() == s) return true;
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        return false;
    }
}

TEST_CASE ("MixCapture: waits for the band, listens to every strip at once, and reports raw, processed and bus measurements")
{
    MixEngine e;
    e.prepare (kSr, 64, session());
    MixCapture cap;
    cap.prepare (kSr, e.getGraph());
    e.setTap (&cap);

    // 0.5 s of silence, then 2.5 s of band: kick clicks at 100 Hz bursts, keys a stereo chord, lead a 220 Hz tone. Input 4 stays quiet.
    testsig::Buffer in (5, 48000 * 3);
    for (int i = 24000; i < in.numSamples(); ++i)
    {
        const float t = float (i) / float (kSr);
        const bool hit = std::fmod (t, 0.5f) < 0.08f;
        in.data[0][size_t (i)] = hit ? 0.6f * std::sin (2.0f * float (M_PI) * 60.0f * t) : 0.0f;
        in.data[1][size_t (i)] = 0.2f * std::sin (2.0f * float (M_PI) * 262.0f * t);
        in.data[2][size_t (i)] = 0.2f * std::sin (2.0f * float (M_PI) * 330.0f * t);
        in.data[3][size_t (i)] = 0.3f * std::sin (2.0f * float (M_PI) * 220.0f * t);
    }

    MixCapture::Settings s;
    s.seconds = 2.0f;
    s.triggerDb = -45.0f;
    s.maxWaitSeconds = 10.0f;
    cap.start (s);
    REQUIRE (waitFor (cap, MixCapture::State::Waiting));
    CHECK (cap.isActive());

    stream (e, in, 64);
    REQUIRE (waitFor (cap, MixCapture::State::Complete));
    CHECK (! cap.isActive());
    CHECK_NEAR (cap.getProgress(), 1.0f, 1e-3);

    const auto r = cap.getResult();
    REQUIRE (r.valid);
    REQUIRE (r.strips.size() == 4);
    CHECK_NEAR (r.seconds, 2.0f, 0.05);
    CHECK (r.droppedFrames == 0);

    // Raw measurements are per strip: the kick is transient, the lead is steady, the quiet strip is silence.
    CHECK (r.strips[0].transientCount >= 3);
    CHECK (r.strips[0].peakDb > -6.0f);
    CHECK (r.strips[1].numChannels == 2);
    CHECK (r.strips[1].stereoCorrelation < 0.9f);          // two different notes left/right
    CHECK_NEAR (r.strips[2].fundamentalHz, 220.0f, 8.0f);
    CHECK (r.strips[3].silencePercent > 95.0f);
    CHECK (cap.stripHeard (0));
    CHECK (cap.stripHeard (2));
    CHECK (! cap.stripHeard (3));

    // Processed levels exist for every strip that made sound.
    CHECK (r.processed[2].valid);
    CHECK (r.processed[2].peakDb > -30.0f);
    CHECK (r.processed[3].peakDb < -60.0f);

    // Bus inputs (what each bus chain receives) were captured for the buses in use.
    CHECK (r.buses[size_t (MixBus::Vocals)].valid);
    CHECK (r.buses[size_t (MixBus::Master)].valid);
    CHECK (r.buses[size_t (MixBus::Master)].peakDb > -30.0f);
    CHECK (! r.buses[size_t (MixBus::Bass)].valid);          // nothing was routed there
    CHECK (r.masterOutput.valid);
    CHECK (r.masterOutput.loudnessLufs > -60.0f);

    // The window started when the band did, not at t = 0: the lead's silence share is small.
    CHECK (r.strips[2].silencePercent < 10.0f);
    e.setTap (nullptr);
}

TEST_CASE ("MixCapture: abort returns to idle and the tap goes quiet")
{
    MixEngine e;
    e.prepare (kSr, 64, session());
    MixCapture cap;
    cap.prepare (kSr, e.getGraph());
    e.setTap (&cap);
    MixCapture::Settings s;
    s.seconds = 5.0f;
    cap.start (s);
    REQUIRE (waitFor (cap, MixCapture::State::Waiting));
    cap.abort();
    CHECK (cap.getState() == MixCapture::State::Idle);
    CHECK (! cap.isActive());
    e.setTap (nullptr);
}

TEST_CASE ("MixCapture: pushing from the audio thread never allocates")
{
    MixEngine e;
    e.prepare (kSr, 64, session());
    MixCapture cap;
    cap.prepare (kSr, e.getGraph());
    e.setTap (&cap);
    MixCapture::Settings s;
    s.seconds = 3.0f;
    s.triggerDb = -200.0f;  // start straight away
    cap.start (s);
    REQUIRE (waitFor (cap, MixCapture::State::Listening));
    testsig::Buffer in (5, 64 * 16);
    testsig::fillNoise (in, 0.2f);
    Streamer streamer (in, 64);
    {
        alloctrack::Scope scope;
        streamer.run (e, false);
        CHECK (alloctrack::getCount() == 0);
    }
    cap.abort();
    e.setTap (nullptr);
}
