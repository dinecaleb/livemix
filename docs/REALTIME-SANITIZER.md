# RealtimeSanitizer

The rule that nothing on the audio thread may allocate, lock, log or do I/O is enforced two ways.
`tests/AllocationTracker` counts heap allocations during steady-state processing in the unit and
plug-in tests, and has since Milestone 1. Since 2026-09-19 the same rule is also checked by clang's
RealtimeSanitizer, which catches what an allocation counter cannot: a mutex, a condition variable,
a syscall, a file write.

## How it is wired

- `src/Core/Realtime.h` defines `LIVEMIX_NONBLOCKING`. It expands to `[[clang::nonblocking]]` when the
  compiler has the attribute (Apple clang 17+, LLVM 20+) and to nothing otherwise, so it costs nothing
  in a normal build. It goes on the function *type*: after `noexcept`, before `override`.
- The real-time entry points carry it, on the declaration and the definition:
  `ChannelProcessor::process`, `MixEngine::process`, `ChannelPluginProcessor::processBlock` (every
  Dine product, `DrumsProcessor` included), `Recorder::write` and DLIVE's audio callback
  `AudioHost::audioDeviceIOCallbackWithContext`. Everything they call is real-time context: the
  sanitizer checks the whole call tree at run time, so nothing under them needs its own annotation.
- `-DLIVEMIX_RTSAN=ON` (default OFF) compiles and links every target, JUCE included, with
  `-fsanitize=realtime` when the compiler supports it, and warns and builds normally when it does not.
  Apple clang does not have the sanitizer; upstream LLVM does. JUCE 8.0.8 does not compile under
  clang 22 (`juce_AudioPluginInstance.h`'s array-parameter constructor template fails its stricter
  non-dependent overload check), so use LLVM 20 or 21.
- The compile-time `-Wfunction-effects` analysis is left off on purpose: it cannot see through JUCE
  and would flag every call into it. The runtime check is the one that means something.

## Running it

```sh
LLVM=$(brew --prefix llvm@21)            # or a release tarball from github.com/llvm/llvm-project
cmake -S . -B build-rtsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=$LLVM/bin/clang -DCMAKE_CXX_COMPILER=$LLVM/bin/clang++ \
      -DLIVEMIX_RTSAN=ON -DLIVEMIX_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build-rtsan --target livemix_tests dlive_app_tests livemix_drums_plugin_tests \
      livemix_master_plugin_tests livemix_fx_plugin_tests
scripts/rtsan.sh build-rtsan
```

`scripts/rtsan.sh` runs every test executable it finds with `halt_on_error=false`, so one run prints
every violation rather than the first, and fails if any suite reported one. The tests drive the entry
points the way a host does, so they are the coverage: `livemix_tests` covers `ChannelProcessor` and
`MixEngine`, the plug-in suites cover `processBlock`, `dlive_app_tests` covers `Recorder::write` and
`DawEngine::processBlock` (everything the audio callback does apart from clearing the outputs). The
callback itself needs a device: `build-rtsan/app/dlive_device_check 3` from a terminal.

The CI job `RealtimeSanitizer` (`.github/workflows/ci.yml`) does the same on every push and pull
request with Homebrew's `llvm@21`.

## What it found (2026-09-19)

| Where | What | Status |
| --- | --- | --- |
| `ChannelProcessor::process`, `MixEngine::process` (`livemix_tests`) | nothing | clean |
| `ChannelPluginProcessor::processBlock` (Drums, Master, FX suites) | nothing | clean |
| `Recorder::write` -> `juce::AudioFormatWriter::ThreadedWriter::Buffer::write` -> `juce::Thread::notify()` | `pthread_mutex_lock`, `pthread_cond_broadcast`, `pthread_mutex_unlock` on the audio thread, once per block while recording | **documented, not fixed** |

JUCE's threaded writer wakes its writer thread after every block it queues, and `Thread::notify()`
is a `std::mutex` lock plus a condition-variable broadcast. The lock is only ever contended with the
writer thread's own wake-up, so it is bounded, but it is a lock on the audio thread and the sanitizer
is right to say so. The fix is a FIFO writer of our own whose reader thread polls (the FIFO holds
2.7 s, so a 10 ms poll loses nothing) instead of being woken, which is a rework of `Recorder`; until
that decision is taken the violation is suppressed in `scripts/rtsan.supp` with this explanation, so
the job stays green and anything *new* still fails it.

## Suppressions

`scripts/rtsan.supp` is read by `scripts/rtsan.sh` (`RTSAN_OPTIONS=suppressions=...`). Every entry is
a violation that is documented rather than fixed, with the reason beside it. An entry without a
reason is a bug.
