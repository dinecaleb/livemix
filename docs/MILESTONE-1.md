# Milestone 1 report — LiveMix Drums foundation

Date: 2026-09-05. Machine: Apple Silicon (arm64), macOS 26.4, Xcode 26.6, JUCE 8.0.8, CMake 4.4.3.

## Milestone checklist (PRD section 54)

| # | Criterion | Result |
|---|-----------|--------|
| 1 | LiveMix Drums builds | Yes (Release, AU + Standalone, engine + tests) |
| 2 | Logic Pro recognizes the AU | `auval -v aufx Lmdr Lvmx`: **AU VALIDATION SUCCEEDED**; component installed in `~/Library/Audio/Plug-Ins/Components`. Not yet opened inside Logic interactively. |
| 3 | Standalone launches | Yes (launched, rendered UI, quit cleanly) |
| 4 | Audio passes through correctly | Default parameters are bit-exact pass-through (test); auval render tests pass at 11025–192000 Hz |
| 5 | Kick/Snare/Tom/Overhead modes | All 12 PRD roles exist; families Kick/Snare/HiHat/Tom/Overhead/Room/Bus each have baselines |
| 6–9 | EQ, gate, compressor, transient work | Unit-tested against measured responses |
| 10 | Input/output metering | Peak + RMS + clip, pre-trim input and post-chain output |
| 11 | Parameters restore after reopening | Plugin integration test: full round trip of all 77 parameters + extras + analysis results; malformed state is ignored safely |
| 12 | Reports zero latency if truly none | `kAudioUnitProperty_Latency` = 0.0 s (queried via AudioToolbox); chain is minimum-phase, no lookahead/buffering. (Still true of Dine Drums. The lookahead limiter added in Milestone 5 makes Dine Master report 1.5 ms; see README, "Latency, honestly".) |
| 13 | Unit tests pass | 46 engine test cases + plugin integration test, 0 failures |
| 14 | 16+ instances without abnormal CPU | Benchmark: 48 stereo instances @ 48 kHz / 64 samples ≈ 16% of one core |
| 15 | Analyze infrastructure without blocking audio | Wait-free FIFO + worker thread; test asserts `pushAudio` never allocates and `processBlock` stays < 1 ms while analyzing |

## Measured performance (Release, arm64, stereo, full chain enabled)

`build/tests/livemix_benchmark`, µs per block for all instances, % of real-time budget on one core:

| instances | 32 smp | 64 smp | 128 smp | 256 smp |
|-----------|--------|--------|---------|---------|
| 1  | 2.1 µs (0.3%) | 5.2 µs (0.4%) | 10.5 µs (0.4%) | 21 µs (0.4%) |
| 16 | 31 µs (4.7%) | 74 µs (5.6%) | 155 µs (5.8%) | 340 µs (6.4%) |
| 32 | 64 µs (9.6%) | 147 µs (11%) | 308 µs (11.5%) | 661 µs (12.4%) |
| 48 | 98 µs (14.7%) | 212 µs (15.9%) | 463 µs (17.3%) | 952 µs (17.8%) |

Real plugin path (`DrumsProcessor::processBlock`, includes parameter snapshot + meters): mean 5.6 µs per
64-sample stereo block, 0 heap allocations across 2000 blocks with parameters changing.

## Architecture decisions that affect later phases

- **Engine is JUCE-free.** `src/` compiles as `livemix_engine` with only the C++20 standard library. VST3/AAX
  later means adding formats to `juce_add_plugin`; the engine and tests do not change.
- **One parameter table.** `ParameterSpecs` (engine) generates the JUCE layout and bounds the safety validator.
  `forEachDspField` fixes the visiting order used by both the audio-thread reader and message-thread writer.
- **Macros write real parameters.** Simple-mode controls (Character/Punch/Body/Attack/Bleed) are host parameters;
  moving one rewrites a fixed subset of underlying parameters from `baseline(role, style) + macros`. Advanced edits to
  parameters outside that subset survive; edits inside it are overwritten by the next macro move (documented).
  Role/profile changes from the UI load the factory baseline; state restore never does.
- **Analysis pipeline.** Audio thread → `AnalysisFifo` → `AnalysisEngine` worker (streaming stats, 4096-pt FFT,
  1/3-octave resonance detection) → `AnalyzeCoordinator` → `RecommendationEngine` → `SafetyValidator` → UI.
  Capture-gain items carry no parameter changes (physical action); plugin-side items are applied via host parameters.
- **AI is optional (PRD amendment).** `IIntelligenceProvider` interface; `NullIntelligenceProvider` ships. AI assist
  is Off by default, persisted per instance, disabled by Live Safe, only used on explicit Analyze, consent dialog when a
  provider declares `sendsDataExternally()`. Timeout/invalid/unavailable → Standard results with a status message.
  AI items are never auto-applied and get tighter clamps (EQ −6..+4 dB, ratio ≤ 8, etc.).
- **Kit intelligence seed.** `InstanceRegistry` (process-local, mutex, message thread) tracks role + group per instance.
  Phase 3 can build discovery/kit-analyze on it; nothing crosses the audio thread.
- **Fast math in dynamics.** Gate/compressor/transient use polynomial log2/pow2 (≈0.0002 dB error) for per-sample
  dB conversion; metering and analysis use exact math.
- **Coefficient smoothing is block-rate** for EQ/filters (20 ms one-pole). Inaudible at live buffer sizes; if 1024+
  sample blocks matter later (MIX/MASTERING modes), switch to sub-block updates.

## Known issues / not done

- Not yet exercised inside Logic Pro interactively (auval passes; that is Apple's gate for Logic).
- Compressor detector is the DAFx log-domain topology with no pre-envelope; on a raw sine it shows ~0.5–1 dB ripple
  in gain reduction. Fine on drums, but a peak pre-detector could be added if metering precision matters.
- Saturator has no oversampling (drive is bounded); noted for a future MIX/MASTERING mode.
- Recommendation thresholds (band targets, crest limits, bleed heuristic) are first-pass values, not yet tuned by
  listening tests with real drum captures.
- UI has no visual polish and no per-parameter tooltips beyond a few; Advanced view is a scrolling grid.
- A/B is not loudness-matched yet (PRD 39 says "prefer").
- One CTest run of `livemix_plugin_tests` hung once during development while a rebuild was in flight; not reproduced
  after (both suites pass in < 1 s). `ctest --timeout` is a cheap guard if it recurs.
- `external/JUCE` is git-ignored; `scripts/bootstrap.sh` re-clones it. The repo is initialised but nothing is committed.
