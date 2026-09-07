#pragma once
#include <vector>
#include <complex>

namespace livemix
{

// Minimal radix-2 real-input FFT for offline analysis (worker thread only).
// All buffers are allocated in prepare(); forward() does not allocate.
class RealFFT
{
public:
    void prepare (int size);               // size must be a power of two
    int getSize() const noexcept { return fftSize; }

    // input: fftSize real samples. magnitudesOut: fftSize/2 + 1 power values (|X|^2).
    void forwardPower (const float* input, float* powerOut) noexcept;

private:
    int fftSize = 0;
    std::vector<std::complex<float>> work;
    std::vector<std::complex<float>> twiddles;
    std::vector<int> bitReverse;
};

} // namespace livemix
