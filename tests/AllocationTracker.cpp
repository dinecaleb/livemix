#include "AllocationTracker.h"
#include <cstdlib>
#include <new>
#include <atomic>

namespace
{
    thread_local bool g_enabled = false;
    thread_local size_t g_count = 0;
}

namespace alloctrack
{
    void setEnabled (bool enabled) { g_enabled = enabled; }
    size_t getCount() { return g_count; }
    void reset() { g_count = 0; }
}

void* operator new (std::size_t size)
{
    if (g_enabled) ++g_count;
    if (void* p = std::malloc (size ? size : 1)) return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    if (g_enabled) ++g_count;
    if (void* p = std::malloc (size ? size : 1)) return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
