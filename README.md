# Dine / DLIVE

**DLIVE** (2026-09) is **the live recording and broadcast DAW**, built on the same engine.
Connect. Record. Mix. Tune. Broadcast.

Four workspaces over one session — **TRACKS** (timeline, clips, waveforms), **MIXER**, **TUNE**, **LIVE** — with a real
transport, multitrack recording of the raw inputs to WAV, playback and basic clip editing, session folders, and a
stereo master export. TUNE MIX sits on top of the DAW rather than replacing it: play the band for thirty seconds and
DLIVE builds the mix, on live inputs or on what you just recorded. See `docs/MILESTONE-7.md` (the DAW milestone)
and `docs/ARCHITECTURE-DLIVE.md` (the mix engine); code in `src/Mix` (engine), `app/native` (DAW + mix host) and
`app/ui` (workspaces). Run it with `open build/app/DLive_artefacts/Release/DLIVE.app` (or copy the bundle to
/Applications); without a band, use **Import a multitrack...** on the first page and point it at a folder of stems.

A family of professional live/broadcast mixing plugins built on one shared engine (Dine Core). The goal is
to behave like a professional Gospel/worship engineer who already knows the sound you are after: excellent
DSP, source-specific tuning, deterministic analysis and carefully designed sonic profiles. AI is optional
and off by default; Dine must sound excellent without it.

Products: **Dine Drums**, **Dine Vocals**, **Dine Keys**, **Dine Master**, **Dine Guitar**, **Dine Bass** and **Dine FX** (Audio
Unit + Standalone, macOS / Logic Pro). Sonic profiles: **Modern Gospel** (default) and **Modern Worship**. Repo, CMake targets and the C++ namespace keep
the working name `livemix`. Every Simple view uses plain words (WARMTH, CLARITY, SMOOTH, STEADY, CLEAN-UP, LOUD ...) so a
volunteer can get a good mix; engineer terms live in Advanced. AI assistance is switched off on purpose (Standard Tune only).

Status: Drums foundation complete through Milestone 3: DSP chain, AU plugin, Simple/Advanced/Kit UI,
presets, loudness-matched A/B, Live Safe, data-driven profiles, drum source strategies, deterministic
**Tune** (waits for signal, listens, proposes a professional starting point with BEFORE/AFTER, KEEP,
REVIEW, REVERT), preamp recommendations, kit tune, optional-AI architecture (OpenAI provider, key required),
FX primitives. **Dine FX** (Milestone 4): reverb (plate tank with pre-delay, early reflections, size, modulation) and
delay (mono / stereo / ping-pong, tempo sync, ducking) engines, 14 types, SPACE / LENGTH / WARMTH / CLARITY / DISTANCE
macros, profiles and presets, its own AU. **Dine Vocals / Keys / Master** (Milestone 5): one shared channel plugin
(`modules/Common`) driven by a `ProductDefinition`; de-esser, stereo width, lookahead limiter and BS.1770 loudness meter
in the chain; sibilance / correlation / loudness measurements; vocal, keys and master Tune strategies and profile data;
plain-language Simple views. **Dine Guitar** (Milestone 6): the same shared channel plugin for Acoustic / Electric Clean /
Electric Drive / Guitar Bus; boom, mud, quack and fizz rules with a template-relative fizz low-pass, expander for amp noise,
WARMTH / CLARITY / SMOOTH / STEADY / CLEAN-UP knobs. **Dine Bass** (Milestone 6): Bass DI / Bass Amp / Synth Bass / Bass
Bus; the high-pass never goes above 0.8 x the measured note, grit for small speakers, WARMTH / CLARITY / GRIT / STEADY /
CLEAN-UP knobs, checked on the church bass stem. Reports: `docs/MILESTONE-1.md` .. `-6.md`; architecture:
`docs/ARCHITECTURE-DINE-CORE.md`. Not started: Sax product, FX Tune, session intelligence, VST3/AAX.

## Layout

```
src/                 livemix_engine (Dine Core): pure C++20, no JUCE, unit-tested
  Core/              block view, dB/fast math, smoothers, envelope followers, FFT, roles/profiles
  DSP/               Biquad, FilterProcessor, ParametricEQ, GateExpander + Compressor (detector HPF),
                     TransientProcessor, Saturator, DeEsser, StereoWidth, Limiter (lookahead), LoudnessMeter (BS.1770),
                     LevelMeter, ChannelProcessor, ChannelParameters
  Analysis/          AnalysisFifo (wait-free SPSC), AnalysisEngine (signal-triggered capture, worker thread:
                     level, spectrum, fundamental, transients, decay, bleed), KitAnalysis (kit rules)
  Tune/              TuneEngine, SourceStrategy + TuneDecisions, StrategyToolkit (shared engineering rules),
                     DrumStrategies, VocalStrategies (Lead / Backing / Choir / Speech / Bus), KeysStrategies
                     (Piano / EP / Organ / Synth / Bus), MasterStrategy (loudness per delivery),
                     GuitarStrategies (Acoustic / Electric / Bus, fizz roll-off), BassStrategies (DI / Amp / Synth / Bus)
  Profiles/          Profile.h (SourceTargets: targets + safe ranges), ProfileData.cpp (Modern Gospel,
                     Modern Worship: numbers only, 24 source families), ProductData.cpp (products, knobs, wording),
                     MacroMapping (Simple controls per product)
  Recommendations/   Recommendation items + sections (WHAT / WHY / CONFIDENCE); facade over Tune
  Intelligence/      IIntelligenceProvider (optional AI), AnalyzeCoordinator, SafetyValidator, OpenAIProvider
  FX/                DelayLine, Allpass/DampedComb, LFO, TempoSync; ReverbAlgorithm, DelayAlgorithm, FxChain,
                     FxParameters (+ visitor), FxParameterSpecs, FxMacroMapping, FxProfiles (Dine FX engine)
  Communication/     InstanceRegistry + IKitEndpoint (process-local discovery), KitController (ANALYZE KIT)
  State/             ParameterIDs, ParameterSpecs (engine), ParameterLayout/Bridge, PresetManager (JUCE)
  UI/                DINE design system: LiveMixLookAndFeel (tokens, embedded Barlow / Barlow Condensed /
                     IBM Plex Mono), Widgets (FlatButton, DropdownButton, ParamTile, MacroKnob), ShellBars
                     (top bar + SIMPLE/ADVANCED/KIT tabs), SimplePanel (InputPanel, macro knobs, ChainStrip),
                     AdvancedPanel (module rail, EqCurve, gate scope, comp curve, value tiles, Tune decisions with UNDO),
                     KitPanel, AnalyzeOverlay (waiting / listening / Tune card), MeterComponent
modules/Common/      ChannelPluginProcessor (juce::AudioProcessor, Tune preview: BEFORE/AFTER/KEEP/REVERT), ChannelPluginEditor,
                     DineChannelProduct.cmake (one function builds a product's AU + Standalone + tests + snapshot tool)
modules/Drums|Vocals|Keys|Master|Guitar|Bass/  thin product classes + CMake (see each README.md)
modules/FX/          FxProcessor, FxEditor, FxPanels (Simple / Advanced), FxPresets, plugin target (see modules/FX/README.md)
app/                 DLIVE, the application
  native/            Project (tracks, clips, markers), Transport, Recorder (raw WAV per armed track),
                     ClipSource + TimelinePlayer (clips -> audio), DawEngine (device -> record / play / mix),
                     MixController (the mix, no JUCE), AudioHost (CoreAudio), SessionStore (versioned JSON),
                     MultitrackImport, MixBounce (offline stereo bounce), ThemeStore (themes: the document,
                     the built-ins, the folder, the preference - see docs/THEMES.md)
  ui/                MainView (sidebar + toolbar + workspace + transport), TracksPage, MixerPage, MixPage (TUNE),
                     LivePage, AdvancedPage (the Channel Inspector), TransportBar, AppTheme (DLIVE v2 tokens,
                     themeable), ThemeSheet (View > Appearance: pick, edit, save, import, export a theme)
  Tools/             dlive_mix_stems, dlive_ui_snapshots, dlive_device_check
  Tests/             dlive_app_tests (controller, DAW, documents, import, bounce)
tests/               unit tests (custom header-only framework), plugin integration tests, benchmark,
                     regression renders (tests/reference/*.f32, regenerate with LIVEMIX_REGEN_REFERENCES=1)
scripts/             bootstrap / build / dlive (build + run) / test / package (zip for a tester) / validate_au
external/JUCE/       vendored JUCE 8.0.8 (git-ignored; scripts/bootstrap.sh clones it)
```

## Build

Requirements: Xcode command line tools, CMake >= 3.22, Ninja. On this machine CMake/Ninja were
installed with `uv tool install cmake ninja` (binaries in `~/.local/bin`).

```sh
scripts/bootstrap.sh            # clones JUCE 8.0.8 into external/JUCE, configures build/
scripts/build.sh                # Release build: AU + Standalone + tests
scripts/build.sh Debug          # Debug build into build-debug/
cmake -S . -B build-universal -G Ninja -DLIVEMIX_UNIVERSAL_BINARY=ON   # arm64 + x86_64
```

Artefacts: `build/modules/<Product>/LiveMix<Product>_artefacts/Release/{AU,Standalone}/` for Drums, Vocals, Keys, Master
and FX. The AUs are copied to `~/Library/Audio/Plug-Ins/Components/Dine <Product>.component` after each build
(`-DLIVEMIX_COPY_PLUGIN_AFTER_BUILD=OFF` to disable).

## Run DLIVE

```sh
scripts/dlive.sh                # build (Release) and open DLIVE.app — the fast loop: only the app, not the plug-ins
scripts/dlive.sh --build        # build only     --debug  from build-debug/
scripts/dlive.sh --tests        # dlive_app_tests + livemix_tests
scripts/dlive.sh --shots [dir]  # every workspace as PNGs (default: build/app-snapshots)
```

## Share a test build

```sh
scripts/package.sh              # build and zip for this Mac      --universal  arm64 + x86_64
scripts/package.sh --no-build   # zip what is already built       --out <dir>  default: dist/
```

Leaves `dist/DLIVE-<version>-<date>.zip` and `dist/NOTES.txt` — **send both**. The signature is ad-hoc, so the
tester right-click > Opens it once.

**Every command, and what to tell the tester: [`BUILD-RUN-SHARE.md`](BUILD-RUN-SHARE.md).**

## Test

```sh
scripts/test.sh                 # ctest (unit + plugin integration) then the benchmark
build/tests/livemix_tests       # engine unit tests (optionally: livemix_tests <name-filter>)
build/modules/Drums/livemix_plugin_tests            # Drums kit / AI-parsing specifics
build/modules/<P>/livemix_<p>_plugin_tests           # shared channel-plugin suite, one per product (drums, vocals, keys, master)
build/modules/FX/livemix_fx_plugin_tests
build/modules/Drums/livemix_tune_stems "<Source>" <file.aif> [seconds]   # offline Tune on a recorded stem
build/tests/livemix_benchmark   # drums: 1/8/16/32/48 instances x 32/64/128/256 samples; FX: 1/4/8/16 x 64/128/256
build/app/dlive_app_tests                          # DLIVE: controller, transport, recorder, timeline, documents
build/app/dlive_mix_stems "<stems folder>" 30 out/ # TUNE MIX on a real multitrack; exit 0 = idempotent re-tune
build/app/dlive_device_check 8 "<stems folder>"    # import + timeline playback through a real CoreAudio device
build/app/dlive_ui_snapshots out/                  # every DLIVE workspace and state as PNGs
scripts/validate_au.sh          # auval for every Dine AU (Lmdr Lmvo Lmky Lmma Lmfx)
scripts/benchmark_compare.py build/benchmark.txt   # the benchmark against scripts/benchmark-baseline.txt (fails > 15 % slower)
scripts/rtsan.sh build-rtsan    # RealtimeSanitizer over the test suites (a -DLIVEMIX_RTSAN=ON build with upstream LLVM)
```

CI (`.github/workflows/ci.yml`, every push to main and every pull request, macOS): bootstrap, the full build, ctest,
the benchmark against its committed baseline (`scripts/benchmark-baseline.txt`, regenerated with
`scripts/benchmark_compare.py <run> --update` on the machine class that runs the comparison) and `auval` over every AU.

## Principles baked into the code

- The audio callback (`DrumsProcessor::processBlock` -> `ChannelProcessor::process`) never allocates,
  locks, or waits. Verified by tests that count heap allocations during steady-state processing, and by
  clang's RealtimeSanitizer over the entry points marked `LIVEMIX_NONBLOCKING` (`-DLIVEMIX_RTSAN=ON`,
  `scripts/rtsan.sh`; see `docs/REALTIME-SANITIZER.md` for what it found and what is documented instead of fixed).
- **Latency, honestly.** The channel path is sample-synchronous and minimum-phase and adds no latency: filters,
  gate, EQs, de-esser, compressor (no lookahead), transient shaper, saturation, width and trim all report 0. The one
  exception is the lookahead limiter (`Limiter::kLookaheadMs` = 1.5 ms, 72 samples at 48 kHz), which only a product
  that turns the stage on has - Dine Master, and DLIVE's master bus - and it is reported to the host through
  `setLatencySamples` *constantly*, whether the limiter is on, off or in a loudness-matched A/B, so the host's
  compensation never jumps. Dine FX is zero latency (pre-delay is part of the sound, not reported latency).
  `tests/DSP/NewStageTests.cpp` ("Latency honesty ...") pins the reported number to the limiter's lookahead.
- **A take survives a crash.** DLIVE's recorder (`app/native/Recorder`) has the writer thread rewrite each WAV's
  header every 15 s of audio and keep a `<take>.wav.recording.json` sidecar beside it (rate, channels, track,
  timeline start, frames so far) that a clean stop deletes; a sidecar found on the next open is a take the app
  died in, and `Recorder::recoverUnfinishedTakes` rebuilds its header from the bytes on disk and puts it back on
  its track. Takes over 4 GB need an RF64 header, which the repair does not write yet (JUCE's writer does, on a clean close).
- Analysis runs on a worker thread fed by a wait-free FIFO. Results never touch DSP directly;
  the user applies recommendations through the normal host parameter path.
- **Tune is deterministic.** Analysis + profile targets + source strategy produce a bounded starting point
  (`TuneResult`: before, proposed, sections, explanations). It is applied as a preview; the user keeps,
  compares or reverts. "No change required" is a legitimate result. Profile numbers live only in
  `src/Profiles/ProfileData.cpp`; strategies hold decision logic, not targets.
- **AI is optional and off by default.** Standard Tune is fully deterministic and offline.
  An `IIntelligenceProvider` may be plugged into `AnalyzeCoordinator`; its output is validated
  by `SafetyValidator`, is never auto-applied, never enters the proposed parameters, and any failure
  falls back to Standard results. The OpenAI provider is inert without a key.
- Simple and Advanced modes drive the same host parameters. Macro moves rewrite only the
  parameters listed in `MacroMapping::affectedParameterIds()`.
- Kit intelligence is process-local: instances register an `IKitEndpoint` in `InstanceRegistry`;
  any instance can run TUNE KIT for its group via `KitController`. All of it is message-thread only.
- Presets: Factory (Style / Channel, generated from `StyleProfile`) and User
  (`~/Library/Application Support/LiveMix/Presets/Drums/User/*.livemixpreset`).

## UI verification

`cmake --build build --target livemix_ui_snapshots && build/modules/Drums/livemix_ui_snapshots out/`
renders every editor state (all views, Tune overlay incl. BEFORE/AFTER, kit with three instances, input health cases,
Live Safe, a larger window) to PNG headlessly — see `tools/UISnapshots.cpp`. Vocals / Keys / Master:
`build/modules/<P>/livemix_<p>_ui_snapshots out/` (`tools/ChannelUISnapshots.cpp`, every source + every stage). Dine FX:
`cmake --build build --target livemix_fx_ui_snapshots && build/modules/FX/livemix_fx_ui_snapshots out/`
(`tools/FxUISnapshots.cpp`). Fonts in `assets/fonts` are SIL OFL (licences alongside).
