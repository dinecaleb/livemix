#pragma once
#include <vector>
#include <cmath>
#include <random>
#include "Core/AudioBlockView.h"

namespace testsig
{
    // Owning multichannel buffer for tests.
    struct Buffer
    {
        std::vector<std::vector<float>> data;
        std::vector<float*> ptrs;

        Buffer (int channels, int samples)
        {
            data.assign (size_t (channels), std::vector<float> (size_t (samples), 0.0f));
            for (auto& c : data) ptrs.push_back (c.data());
        }
        livemix::AudioBlockView view (int offset = 0, int length = -1)
        {
            static thread_local std::vector<float*> tmp;
            tmp.clear();
            for (auto* p : ptrs) tmp.push_back (p + offset);
            const int n = length < 0 ? int (data[0].size()) - offset : length;
            return { tmp.data(), int (ptrs.size()), n };
        }
        int numSamples() const { return int (data[0].size()); }
        int numChannels() const { return int (data.size()); }
    };

    inline void fillSine (Buffer& b, float freq, float amp, double sr, float phase = 0.0f)
    {
        for (auto& c : b.data)
            for (size_t i = 0; i < c.size(); ++i)
                c[i] = amp * std::sin (2.0f * float (M_PI) * freq * float (i) / float (sr) + phase);
    }

    inline void fillNoise (Buffer& b, float amp, unsigned seed = 1)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        for (auto& c : b.data)
            for (auto& x : c) x = amp * dist (rng);
    }

    // Pink-ish noise via Paul Kellet's filter.
    inline void fillPinkNoise (Buffer& b, float amp, unsigned seed = 1)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        for (auto& c : b.data)
        {
            float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
            for (auto& x : c)
            {
                const float white = dist (rng);
                b0 = 0.99886f * b0 + white * 0.0555179f;
                b1 = 0.99332f * b1 + white * 0.0750759f;
                b2 = 0.96900f * b2 + white * 0.1538520f;
                b3 = 0.86650f * b3 + white * 0.3104856f;
                b4 = 0.55000f * b4 + white * 0.5329522f;
                b5 = -0.7616f * b5 - white * 0.0168980f;
                const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
                b6 = white * 0.115926f;
                x = amp * pink * 0.11f;
            }
        }
    }

    // Synthetic drum hits: decaying noise bursts + low sine, repeated at an interval.
    inline void fillDrumHits (Buffer& b, double sr, float hitAmp, float floorAmp, float intervalSec, float decaySec, float toneHz = 120.0f, unsigned seed = 7)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        const int interval = int (intervalSec * sr);
        for (auto& c : b.data)
        {
            for (size_t i = 0; i < c.size(); ++i)
            {
                const int sinceHit = int (i) % interval;
                const float env = std::exp (-float (sinceHit) / float (decaySec * sr));
                const float tone = std::sin (2.0f * float (M_PI) * toneHz * float (sinceHit) / float (sr));
                c[i] = hitAmp * env * (0.6f * tone + 0.4f * dist (rng)) + floorAmp * dist (rng);
            }
        }
    }

    inline float peak (const Buffer& b, int channel = 0)
    {
        float p = 0.0f;
        for (float x : b.data[size_t (channel)]) p = std::max (p, std::fabs (x));
        return p;
    }

    inline float rms (const Buffer& b, int channel = 0, int from = 0, int to = -1)
    {
        const auto& c = b.data[size_t (channel)];
        if (to < 0) to = int (c.size());
        double s = 0.0;
        for (int i = from; i < to; ++i) s += double (c[size_t (i)]) * c[size_t (i)];
        return float (std::sqrt (s / double (to - from)));
    }

    inline float toDb (float g) { return g <= 1e-9f ? -180.0f : 20.0f * std::log10 (g); }

    inline bool allFinite (const Buffer& b)
    {
        for (auto& c : b.data) for (float x : c) if (! std::isfinite (x)) return false;
        return true;
    }
}
