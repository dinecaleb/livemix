# Milestone 4 report: Dine FX (reverb + delay)

Date: 2026-09-06. Builds on `MILESTONE-3.md`. Second product target, proving the Dine Core abstractions.
Machine: Apple Silicon, macOS 26.4, JUCE 8.0.8, CMake 4.4.3, Release builds.

## 1. What was built

- **Engine** (`src/FX`, JUCE-free): `ReverbAlgorithm` (Dattorro figure-of-eight plate tank with pre-delay, early reflections,
  input filtering, size scaling and modulation), `DelayAlgorithm` (mono / stereo / ping-pong, tempo sync, feedback filtering,
  width, modulation, ducking, click-free retiming), `FxChain` (trim, parallel delay + reverb, delay-to-reverb send, mix,
  loudness-matched A/B, meters), `FxParameters` + `forEachFxField`, `FxParameterSpecs` (the FX table), `FxMacroMapping`
  (SPACE LENGTH WARMTH CLARITY DISTANCE), `FxProfiles` (Modern Gospel baselines for 14 types, Modern Worship deltas,
  six named starting points).
- **Plugin** (`modules/FX`, AU + Standalone "Dine FX", `aufx Lmfx Lvmx`): `FxProcessor`, `FxEditor`, `FxSimplePanel`,
  `FxAdvancedPanel`, `FxPresets`. Same shell, same Live Safe / A/B semantics as Dine Drums.
- **Shared changes**: `createParameterLayout (specs)` overload; `findParameterSpec` falls back to the FX table;
  `TopBar::setModuleName / setSourceCaption`, `SubBar::setKitTabVisible`; the font binary-data target moved to the root
  CMake so both products link it. No drum parameter, baseline or DSP path changed (Drums snapshots and tests unchanged).

## 2. Reverb design notes

- Reference network at 29761 Hz, every length scaled by `sr / 29761` and by SIZE (0.35 .. 1.6 x). Allpass-interpolated reads
  inside the loop: linear interpolation at fractional lengths darkened every pass (found by the damping test).
- RT60: the decay gain is applied once per tank half; `g = 10^(-3 T / RT60)` with T = mean half-loop time. Measured
  (Schroeder EDC, -5 .. -35 dB) at 48 kHz: 0.6 s -> 0.72, 1.5 s -> 1.51, 4.0 s -> 4.03.
- Damping is a one-pole low-pass in each half (0 % = 20 kHz, 100 % = 625 Hz). Early reflections: 6 taps per side from the
  pre-delay line, scaled with SIZE. The tank is fed a mono sum; left/right come from Dattorro's tap sets (tail correlation
  measured < 0.5).

## 3. Delay design notes

- Two read heads per channel; a time change starts a 40 ms crossfade from the old to the new head (a sine through a
  retiming delay stays within 5 % of its amplitude in the test). Modulation is a separate fractional offset.
- Loop gain is < 1 by construction (feedback <= 95 %, unity-gain filters); the loop only clamps non-finite runaways.
  An earlier tanh limiter was removed because it squashed normal-level repeats.
- Ducking: peak envelope of the dry mono sum, 5 ms attack, release parameter; up to 30 dB of wet reduction scaled by the
  source's presence between -50 and -20 dBFS. Ping-pong feeds the input into the left line, left into right at unity,
  right back into left at the feedback amount.

## 4. Tests added

| Suite | Cases |
|---|---|
| `tests/FX/ReverbTests.cpp` | RT60 follows decay and grows with it; pre-delay holds the onset and early reflections land first; extreme settings finite and bounded, no allocation with size sweeps; stereo decorrelation and mono streams; damping / high cut darken the tail, low cut thins it; level scaling, disabled = silence, tail dies out |
| `tests/FX/DelayTests.cpp` | sample-exact free time and feedback per repeat; tempo sync, retiming after a tempo change, no clicks / no allocation while retiming; ping-pong alternation and stereo offset; ducking and width 0; loop filters, disabled, tail estimate |
| `tests/FX/FxChainTests.cpp` | mix 0 and A/B pass the dry signal; tail after the source stops, no allocation with parameter churn; loudness-matched A/B within 2 dB; every profile/type baseline in mono and stereo |
| `tests/FX/FxParameterTests.cpp` | every field has a spec (type, range, default), ids unique, no drum ids in the FX table; every baseline in range and unchanged by macros at 50; macro moves bounded, monotonic and limited to the affected ids; use presets valid |
| `tests/Integration/FxPluginTests.cpp` | state round trip, malformed state, latency 0 / tail / finite output across sample rates and blocks, allocation-free `processBlock`, host bypass, host tempo drives synced delays, type / profile / macros / presets / user preset round trip / Live Safe, editor lifecycle |

Engine: 91 cases, 0 failures. Plugin integration (Drums + FX): PASSED. `auval -v aufx Lmfx Lvmx`: AU VALIDATION
SUCCEEDED, 41 parameters, one default-value round-trip warning on a skewed range (same class as the two on Drums).
UI snapshots: 12 FX states render (`tools/FxUISnapshots.cpp`); the 22 Drums states are unchanged.

## 5. CPU / latency

| Measure | Result |
|---|---|
| Latency | 0 samples (pre-delay is part of the sound, not reported latency); tail reported from decay / feedback |
| `FxChain` @ 48 kHz / 64, stereo | 4.2 us per block for one instance (0.3 % of budget); 16 instances 77 us (5.8 %) |
| `FxChain` @ 48 kHz / 256 | 17 us per instance (0.3 %); 16 instances 328 us (6.1 %) |

The reverb dominates (four input diffusers, two tank halves, fourteen output taps per sample, all allpass-interpolated).

## 6. Known weaknesses

- Every baseline, macro slope and ducking curve is an engineering starting point validated on synthetic signals only.
- The tank takes a mono sum; a true-stereo tank (two independent networks) would preserve input placement for stereo sources.
- No FX Tune yet (see `modules/FX/README.md`); the plugin has no TUNE button, only types, profiles, macros and presets.
- The delay visual draws the repeat pattern from the parameters, not the measured output.
- Retiming crossfades are restarted on every block while a time parameter is being dragged; a slow tape-style glide would be
  smoother for continuous automation.

## 7. Recommended next

Listening pass on real vocal, BGV and drum stems (plate density, hall bloom, throw ducking), then adjust `FxProfiles.cpp` only.
Then FX Tune: capture on the aux, measure the dry source's tempo-related decay and spectral weight, fit pre-delay /
decay / division, reuse `TuneResult` and the BEFORE / AFTER card.
