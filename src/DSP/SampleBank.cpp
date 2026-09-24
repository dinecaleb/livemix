#include "SampleBank.h"
#include "Core/DbUtils.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace livemix
{

const std::vector<float>* SampleBank::pick (float velocity01, uint32_t& counter) const noexcept
{
    if (layers.empty()) return nullptr;
    const float v = velocity01 < 0.0f ? 0.0f : (velocity01 > 1.0f ? 1.0f : velocity01);
    int index = int (v * float (layers.size()));
    if (index >= int (layers.size())) index = int (layers.size()) - 1;
    const auto& layer = layers[size_t (index)];
    if (layer.hits.empty()) return nullptr;
    const auto& hit = layer.hits[size_t (counter % uint32_t (layer.hits.size()))];
    ++counter;
    return hit.empty() ? nullptr : &hit;
}

void prepareHit (std::vector<float>& hit, float trimBelowDb)
{
    const float trim = dbToGain (trimBelowDb);
    size_t onset = hit.size();
    float peak = 0.0f;
    for (size_t i = 0; i < hit.size(); ++i)
    {
        const float a = std::fabs (hit[i]);
        if (a > peak) peak = a;
        if (onset == hit.size() && a >= trim) onset = i;
    }
    if (onset == hit.size() || peak <= 0.0f) { hit.clear(); return; }
    // Keep up to 16 samples of run-up and fade over those only, so the fade lands before the
    // onset and never on it: a recording that starts on the hit itself is not faded at all.
    const size_t runUp = std::min<size_t> (16, onset);
    hit.erase (hit.begin(), hit.begin() + long (onset - runUp));
    const float norm = 1.0f / peak;
    for (size_t i = 0; i < hit.size(); ++i)
    {
        float g = norm;
        if (i < runUp) g *= float (i + 1) / float (runUp + 1);
        hit[i] *= g;
    }
}

float measureFundamental (const std::vector<float>& hit, double sampleRate)
{
    const int start = int (0.005 * sampleRate);
    const int end = std::min (int (hit.size()), int (0.125 * sampleRate));
    const int n = end - start;
    const int minLag = int (sampleRate / 500.0), maxLag = int (sampleRate / 35.0);
    if (n < 2 * maxLag || minLag < 2) return 0.0f;
    const float* x = hit.data() + start;
    double energy = 0.0;
    for (int i = 0; i < n; ++i) energy += double (x[i]) * x[i];
    if (energy <= 1.0e-9) return 0.0f;
    // Normalised autocorrelation; the first clear peak above 0.5 wins over a higher one at a
    // multiple, so the fundamental is found rather than an octave below it.
    int bestLag = 0;
    double best = 0.0;
    double prev = 0.0;
    bool rising = false;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double acc = 0.0;
        for (int i = 0; i + lag < n; ++i) acc += double (x[i]) * x[i + lag];
        const double r = acc / energy;
        if (r > prev) rising = true;
        else if (rising && prev > 0.5 && prev > best) { best = prev; bestLag = lag - 1; if (best > 0.8) break; }
        else if (r < prev) rising = false;
        prev = r;
    }
    return bestLag > 0 ? float (sampleRate / bestLag) : 0.0f;
}

namespace
{
    // A drum, as a synthesis: a pitched body that falls in pitch and level, a click or a
    // noise burst for the attack, and (snare) a rattle. Velocity brightens the attack and
    // shortens nothing - the level difference is what the player applies.
    std::vector<float> synthHit (RoleFamily family, int variant, float velocity, uint32_t seed, double sr)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        float bodyHz = 55.0f, bodyEndHz = 45.0f, bodyDecayS = 0.35f, clickDecayS = 0.004f, clickAmp = 0.5f, noiseDecayS = 0.0f, noiseAmp = 0.0f, lengthS = 0.8f;
        switch (family)
        {
            case RoleFamily::Kick:
                bodyHz = variant == 1 ? 75.0f : variant == 2 ? 60.0f : 90.0f;
                bodyEndHz = variant == 1 ? 42.0f : variant == 2 ? 48.0f : 52.0f;
                bodyDecayS = variant == 1 ? 0.45f : variant == 2 ? 0.30f : 0.22f;
                clickAmp = variant == 2 ? 0.25f : 0.6f;
                lengthS = 0.8f;
                break;
            case RoleFamily::Snare:
                bodyHz = variant == 2 ? 260.0f : variant == 1 ? 180.0f : 210.0f;
                bodyEndHz = bodyHz * 0.85f;
                bodyDecayS = variant == 1 ? 0.18f : 0.12f;
                clickAmp = 0.4f;
                noiseAmp = variant == 2 ? 0.9f : 0.7f;
                noiseDecayS = variant == 1 ? 0.22f : 0.16f;
                lengthS = 0.5f;
                break;
            case RoleFamily::Tom:
                bodyHz = variant == 2 ? 80.0f : variant == 1 ? 110.0f : 150.0f;
                bodyEndHz = bodyHz * 0.8f;
                bodyDecayS = 0.5f;
                clickAmp = 0.35f;
                lengthS = 1.0f;
                break;
            default: break;
        }
        const int n = int (lengthS * sr);
        std::vector<float> out (size_t (n), 0.0f);
        double phase = 0.0;
        // A one-pole low-pass on the noise, opened by velocity: a soft hit is duller.
        const float noiseCut = 0.15f + 0.6f * velocity;
        float lp = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float t = float (i) / float (sr);
            const float pitchEnv = std::exp (-t / 0.04f);
            const float hz = bodyEndHz + (bodyHz - bodyEndHz) * pitchEnv;
            phase += 2.0 * 3.14159265358979 * double (hz) / sr;
            float s = std::sin (float (phase)) * std::exp (-t / bodyDecayS);
            s += clickAmp * (0.4f + 0.6f * velocity) * noise (rng) * std::exp (-t / clickDecayS);
            if (noiseAmp > 0.0f)
            {
                lp += noiseCut * (noise (rng) - lp);
                s += noiseAmp * lp * std::exp (-t / noiseDecayS);
            }
            out[size_t (i)] = s;
        }
        // A short tail fade so the end of the hit is silent whatever the decay left.
        const int tail = std::min (n, int (0.01 * sr));
        for (int i = 0; i < tail; ++i) out[size_t (n - 1 - i)] *= float (i) / float (tail);
        return out;
    }
}

SampleBank synthesizeBank (RoleFamily family, int variant, double sampleRate)
{
    SampleBank b;
    b.sampleRate = sampleRate;
    switch (family)
    {
        case RoleFamily::Kick:  b.name = variant == 1 ? "Deep kick" : variant == 2 ? "Soft kick" : "Tight kick"; break;
        case RoleFamily::Snare: b.name = variant == 1 ? "Fat snare" : variant == 2 ? "Bright snare" : "Tight snare"; break;
        case RoleFamily::Tom:   b.name = variant == 1 ? "Mid tom" : variant == 2 ? "Floor tom" : "High tom"; break;
        default:                b.name = "Sound"; break;
    }
    b.fundamentalHz = family == RoleFamily::Kick ? (variant == 1 ? 42.0f : variant == 2 ? 48.0f : 52.0f)
                    : family == RoleFamily::Snare ? (variant == 2 ? 221.0f : variant == 1 ? 153.0f : 178.0f)
                    : family == RoleFamily::Tom ? (variant == 2 ? 64.0f : variant == 1 ? 88.0f : 120.0f) : 0.0f;
    constexpr int kLayers = 4, kRoundRobins = 2;
    for (int l = 0; l < kLayers; ++l)
    {
        SampleBank::Layer layer;
        const float velocity = float (l + 1) / float (kLayers);
        for (int r = 0; r < kRoundRobins; ++r)
        {
            auto hit = synthHit (family, variant, velocity, uint32_t (1000 * int (family) + 100 * variant + 10 * l + r + 1), sampleRate);
            prepareHit (hit);
            layer.hits.push_back (std::move (hit));
        }
        b.layers.push_back (std::move (layer));
    }
    // What the sample really rings at, measured the way a loaded sound's pitch is; the
    // nominal above is where the body settles, and the glide pulls the measurement up a little.
    if (const float measured = measureFundamental (b.layers.back().hits[0], sampleRate); measured > 0.0f) b.fundamentalHz = measured;
    return b;
}

} // namespace livemix
