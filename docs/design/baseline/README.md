# The UI-0 baseline (2026-09-27)

What DLIVE looked like, cost and passed **before** the v2 design work started. Every later
phase is measured against the numbers in this folder, and its renders are compared against
these ones by eye.

The images are not committed (see `.gitignore`). These commands put them back exactly:

```sh
export PATH="$HOME/.local/bin:$PATH"
STEMS="/Users/calebwork/Downloads/caleb"          # the real service multitrack

build/app/dlive_ui_snapshots --stems "$STEMS" docs/design/baseline/shots   # every state, 70 PNGs
DLIVE_STEMS="$STEMS" build/app/dlive_ui_snapshots --sizes docs/design/baseline/sizes
build/app/dlive_ui_snapshots --frames 48 120 > docs/design/baseline/frames.txt
build/tests/livemix_benchmark                > docs/design/baseline/benchmark.txt
scripts/rtsan.sh                             > docs/design/baseline/rtsan.txt
```

## What the renders are made of

`--stems` was added in this phase. Before it, every screen was rendered from sixteen sine
tones and noise bursts: enough to prove a meter moved, not enough to look at. A spectrum of a
sine is a spike and a waveform of a noise burst is a block, so nothing about whether the
console *reads* right could be judged from them - which is the only thing these renders are
for. Every baseline image is now rendered from a real 19-source service multitrack, a minute
in, thirty seconds long. `stems.txt` records exactly which sources that folder gave and what
each one peaked at. The tones remain the fallback, so CI and a machine with no recordings on
it still render every screen.

Five sources in that recording are silent - Overhead, DRUM PAD, bgv 6, Pastor and Crowd - and
they are left silent rather than substituted. A console with a dead crowd mic on it is a real
state and the screens should be legible in it. It does mean **no screen in this baseline
exercises the speech group**, because the pastor's microphone is not in this recording.

## The numbers

| Check | Result |
| --- | --- |
| `ctest` | 10/10 suites pass (62 s) |
| `dlive_app_tests` | pass |
| benchmark | `benchmark.txt`; compare against the previous commit, not the committed baseline (it is ~55 % off this Mac) |
| RTSan | **cannot run on this machine** - see below |
| frame cost | `frames.txt`; TRACKS 1.26 / MIXER 1.29 / TUNE 1.26 / LIVE 1.27 / INSPECTOR 1.28 ms per tick |

### RTSan

`scripts/rtsan.sh` exits 1 and every instrumented binary aborts before `main`:

```
dyld: Library not loaded: @rpath/libclang_rt.rtsan_osx_dynamic.dylib
```

`build-rtsan` was linked against an LLVM 21 that lived in a previous session's scratchpad
directory, which has since been emptied. The script is behaving correctly - it reports the
failure rather than a pass - but there is no real-time coverage on this machine until LLVM 21
is fetched again to a durable path and `build-rtsan` is reconfigured against it. Nothing in
the UI phases runs on the audio thread, but the moment a phase adds engine-side publishing
(the gain-reduction meter in UI-3, the scopes in UI-6) this has to be working again.
