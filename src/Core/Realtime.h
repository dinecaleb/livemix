#pragma once

// LIVEMIX_NONBLOCKING marks a real-time entry point: a function that must never allocate,
// lock, log, do I/O or otherwise block, and everything it calls. It expands to clang's
// [[clang::nonblocking]] where the compiler has it (Apple clang 17+, LLVM 20+) and to
// nothing elsewhere, so the annotation costs nothing in a normal build. It goes on the
// function *type*, after noexcept and before override:
//
//     void process (AudioBlockView&) noexcept LIVEMIX_NONBLOCKING override;
//
// With -DLIVEMIX_RTSAN=ON (upstream LLVM: -fsanitize=realtime) RealtimeSanitizer treats every
// call inside such a function - however deep - as real-time context and aborts on an
// intercepted unsafe call (malloc, a mutex, a syscall) with the stack that got there. The
// compile-time -Wfunction-effects analysis is deliberately left off: it cannot see through
// JUCE, so the runtime check is the one that means something.
#if defined(__clang__) && defined(__has_cpp_attribute)
  #if __has_cpp_attribute(clang::nonblocking)
    #define LIVEMIX_NONBLOCKING [[clang::nonblocking]]
  #endif
#endif
#ifndef LIVEMIX_NONBLOCKING
  #define LIVEMIX_NONBLOCKING
#endif
