#include "FFT.h"
#include <cmath>

namespace livemix
{

void RealFFT::prepare (int size)
{
    fftSize = size;
    work.assign (size_t (size), {});
    twiddles.assign (size_t (size / 2), {});
    bitReverse.assign (size_t (size), 0);

    for (int i = 0; i < size / 2; ++i)
    {
        const double angle = -2.0 * M_PI * double (i) / double (size);
        twiddles[size_t (i)] = { float (std::cos (angle)), float (std::sin (angle)) };
    }

    int bits = 0;
    while ((1 << bits) < size) ++bits;
    for (int i = 0; i < size; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitReverse[size_t (i)] = r;
    }
}

void RealFFT::forwardPower (const float* input, float* powerOut) noexcept
{
    const int n = fftSize;
    for (int i = 0; i < n; ++i)
        work[size_t (bitReverse[size_t (i)])] = { input[i], 0.0f };

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2;
        const int step = n / len;
        for (int i = 0; i < n; i += len)
        {
            for (int j = 0; j < half; ++j)
            {
                const auto w = twiddles[size_t (j * step)];
                const auto u = work[size_t (i + j)];
                const auto v = work[size_t (i + j + half)] * w;
                work[size_t (i + j)] = u + v;
                work[size_t (i + j + half)] = u - v;
            }
        }
    }

    for (int k = 0; k <= n / 2; ++k)
        powerOut[k] = std::norm (work[size_t (k)]);
}

} // namespace livemix
