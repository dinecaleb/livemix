# Milestone 2 report — Phases 1-4 for LiveMix Drums

Date: 2026-09-05. Builds on `MILESTONE-1.md`. Scope: finish Phase 1, Phase 2 (Analyze), Phase 3 (kit
intelligence), Phase 4 (hardening). Excluded on purpose: Phases 5-9, real AI providers, VST3/AAX.

## What was added

### Phase 1 completion
- **Presets** (`src/State/PresetManager`): Factory → Style → Channel (72 generated from the profile baselines) and
  User presets saved as XML in `~/Library/Application Support/LiveMix/Presets/Drums/User/`. Presets carry role,
  profile, the five macros and every DSP parameter. Menu in the header: load, save as, delete. Blocked in Live Safe.
- **Loudness-matched A/B** (`ChannelProcessor`, parameter `abMatch`, default on): ORIGINAL is played at the processed
  loudness using a ~2 s mean-square ratio (clamped ±12 dB, frozen while bypassed). Test verifies matched level within
  0.5 dB and bit-exact pass-through when matching is off.
- **EQ curve** (`src/DSP/EqResponse`, `src/UI/EqCurveComponent`): analytic combined magnitude of HPF/LPF + both EQs,
  shown above the Advanced grid, refreshed at 10 Hz.
- **Tooltips** on every Simple control, every Advanced control (name, unit, range, default), and the footer.
- **Gain-reduction readout** (gate / compressor / A/B match gain) under the meters.
- **Compressor pre-detector**: instant-attack / 1.5 ms release peak hold before the log-domain smoother; gain-reduction
  ripple on a 1 kHz sine dropped from ~1 dB to < 0.4 dB. No latency added.

### Phase 2 (Analyze)
- **Mix-gain recommendations**: the audio thread accumulates processed-output peak/RMS (atomics only) during a capture;
  the engine recommends an output-trim move toward the role's mix target. Auto-apply limited to ±8 dB.
- **Re-analyze comparison**: the previous capture and its recommendations are kept; the UI shows peak/floor deltas and
  item counts since the last analysis.
- Recommendation thresholds remain first-pass values validated only on synthetic signals (see "Not done").

### Phase 3 (kit intelligence)
- `IKitEndpoint` implemented by `DrumsProcessor` and registered in `InstanceRegistry` (process-local, no network).
- `KitController`: ANALYZE KIT from any instance starts captures on every group member, polls until all are done,
  drops members that disappear, then runs `KitAnalysis`.
- `KitAnalysis` rules: capture table (health + preamp delta per channel), processing highlights (medium/high
  confidence items), balance vs the kick/snare reference with per-role offsets (toms −3, overheads −6, hat −12,
  room −10), kick/snare relationship note, "overheads dominate" note. APPLY BALANCE writes output trims;
  APPLY ALL SAFE CHANGES fans out per instance.
- KIT tab in the editor with member list, status and the three actions.

### Phase 4 (hardening)
- **Overrun detection**: blocks whose processing time exceeds the buffer budget are counted and shown in the
  diagnostics line (orange when non-zero).
- **48-instance plugin test**: real `DrumsProcessor` × 48 at 48 kHz / 64 samples for 3 s.
- **Audio regression renders**: 4 presets rendered through the full chain, compared against `tests/reference/*.f32`
  (max diff 2e-4, RMS 5e-5; tolerances chosen so the x86_64 slice passes against arm64-generated references).
- **Universal binary**: `-DLIVEMIX_UNIVERSAL_BINARY=ON` verified for the engine + tests (`lipo`: x86_64 arm64);
  x86_64 slice runs green under Rosetta. Full plugin universal build uses the same option.
- **Crash-safe editor callbacks**: async dialogs and popup menus capture a `SafePointer`, so closing the editor
  mid-dialog cannot dereference a dead editor.
- **Bug fixed by hardening**: `FilterProcessor` did not refresh coefficients on a slope change without a frequency
  change (surfaced when the smoother became exact). `Smoother` now keeps double-precision state.

## Measurements

| Check | Result |
|---|---|
| Engine unit tests | 52 cases, 0 failures (arm64 and x86_64) |
| Plugin integration tests | PASSED: state, malformed state, bypass, sample rates, allocations, analyze, presets, kit, 48 instances |
| `processBlock` steady state | 0 allocations / 2000 blocks; ~6 µs per 64-sample stereo block |
| 48 instances @ 64 samples (real plugin path) | 162 µs per block for all ≈ 12% of the 1333 µs budget, 0 over-budget blocks |
| Kit analyze (4 instances, headless) | completes, tom flagged Low, 2 balance items, kick/snare note |
| auval | AU VALIDATION SUCCEEDED, 78 parameters. Two warnings: `gateAttack` and `gateHold` defaults do not round-trip exactly through their skewed range (float rounding; auval warning only). |
| Latency | 0 samples (unchanged; Dine Drums has no lookahead stage - the Milestone 5 limiter on Dine Master reports 1.5 ms) |

## Not done / needs a human

- **Listening tests in Logic Pro.** Everything above is verified numerically and on synthetic drum signals. The
  role/style baselines, macro ranges and every recommendation threshold still need tuning on real captures. This is
  the single most valuable next step and cannot be done from the terminal.
- **Phase-relationship analysis across kit mics** (PRD 14 "later versions") needs sample-synchronous multi-instance
  capture; not attempted.
- **Code signing / notarization** requires a Developer ID; builds are ad-hoc signed by JUCE's hardened-runtime setting.
- The auval default-value warnings could be removed by choosing skew midpoints whose defaults round-trip exactly, or
  by switching those two parameters to linear ranges; left as-is because they are warnings, not failures.
- Kit UI is a text report, not a table; fine for the milestone, not final.

## Intermittent CTest hang: found and fixed

`livemix_plugin_tests` hung in 1 of 6 CTest runs. A stack sample showed the main thread in
`~DrumsProcessor → ~AnalysisEngine → std::thread::join` while the analysis worker sat in
`condition_variable::wait`. Cause: the destructor set `shouldExit` and called `notify_all()` without holding the
mutex, so a worker that had just evaluated the wait predicate could block after the notification had already
fired (lost wakeup). Fix: flags are now changed under the mutex in the destructor and in `abort()`. A new engine
test constructs/starts/destroys the engine 400 times; CTest was then run repeatedly without a hang.
