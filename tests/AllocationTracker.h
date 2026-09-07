#pragma once
#include <cstddef>

// Counts heap allocations on the current thread while enabled. Used to prove
// that steady-state process() calls never allocate.
namespace alloctrack
{
    void setEnabled (bool enabled);
    size_t getCount();
    void reset();

    struct Scope
    {
        Scope() { reset(); setEnabled (true); }
        ~Scope() { setEnabled (false); }
    };
}
