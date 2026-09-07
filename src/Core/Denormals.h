#pragma once
#include <cstdint>

#if defined(__aarch64__) || defined(_M_ARM64)
 #define LIVEMIX_ARM64 1
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
 #define LIVEMIX_X86 1
 #include <xmmintrin.h>
 #include <pmmintrin.h>
#endif

namespace livemix
{

// RAII helper that enables flush-to-zero (and denormals-are-zero on x86) for
// the current thread while in scope. Used by the standalone engine tests and
// benchmark; the JUCE plugin uses juce::ScopedNoDenormals, which is equivalent.
class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
#if LIVEMIX_ARM64
        uint64_t fpcr;
        asm volatile ("mrs %0, fpcr" : "=r" (fpcr));
        saved = fpcr;
        fpcr |= (1ULL << 24); // FZ bit
        asm volatile ("msr fpcr, %0" : : "r" (fpcr));
#elif LIVEMIX_X86
        saved = _mm_getcsr();
        _mm_setcsr (uint32_t (saved) | 0x8040); // FTZ | DAZ
#endif
    }

    ~ScopedNoDenormals() noexcept
    {
#if LIVEMIX_ARM64
        asm volatile ("msr fpcr, %0" : : "r" (saved));
#elif LIVEMIX_X86
        _mm_setcsr (uint32_t (saved));
#endif
    }

private:
    uint64_t saved = 0;
};

} // namespace livemix
