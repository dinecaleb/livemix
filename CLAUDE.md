# Dine (repo name LiveMix / Calive) — notes for Claude Code

- Build: `export PATH="$HOME/.local/bin:$PATH"` (cmake/ninja from `uv tool`), then `scripts/build.sh`.
  Engine-only iteration (fast, no JUCE): `cmake -S . -B build-engine -G Ninja -DLIVEMIX_BUILD_PLUGIN=OFF && cmake --build build-engine && build-engine/tests/livemix_tests`.
- `src/` must stay JUCE-free. JUCE-dependent code lives in `src/State/ParameterLayout|Bridge`, `src/UI`, `modules/`.
- Products: Dine Drums / Vocals / Keys / Master / Guitar / Bass are ONE shared plugin (`modules/Common/ChannelPlugin{Processor,Editor}`)
  parameterised by `ProductDefinition` (`src/Core/ProductDefinition.h`, data in `src/Profiles/ProductData.cpp`: sources,
  five knobs with plain-language labels/tooltips, chain stages, wording). `modules/<Product>/` is a thin
  `ChannelPluginProcessor (Product::X)` + CMake (`modules/Common/DineChannelProduct.cmake`). Dine FX is separate (`src/FX`).
- Wording rule: anything a volunteer sees in Simple view is plain language (WARMTH, CLARITY, SMOOTH, STEADY, CLEAN-UP, LOUD...);
  engineer terms (gate, comp, de-ess) appear only in Advanced and as small subtitles. Tune explains WHAT then WHY in sentences.
- AI assistance is switched off on purpose (`src/Intelligence/AIFeature.h`, `kAIAssistAvailable = false`): no provider,
  no AI button / menu, Standard Tune only. Keep the code paths; do not re-enable without the user asking.
- Real-time rules: nothing in `process()`/`processBlock()` may allocate, lock, log, or build strings.
  `tests/AllocationTracker` enforces this in `ChannelProcessorTests` and `PluginStateTests`.
- Parameters are defined once in `src/State/ParameterSpecs.cpp` (one table per product via `channelParameterSpecs (Product)`,
  `usesStage` decides which DSP stages a product exposes; hidden fields stay at struct defaults) and visited in one order by
  `forEachDspField` (`src/DSP/ChannelParameters.h`). Adding a DSP parameter = add the struct field, the ParamID, the spec,
  and the visitor line; `ParameterSpecTests` / `ProductTuneTests` catch mismatches. Chain order: ChannelProcessor (Input,
  filters, gate, corrective EQ, de-esser, comp, transient, tone EQ, saturation, width, output trim, [limiter], [loudness meter]).
- Never rename a released parameter ID (sessions/automation depend on them).
- Tune (`src/Tune`): `TuneEngine` = analysis + `Profiles::targets (profile, role)` + `strategyFor(family)`
  (`DrumStrategies`, `VocalStrategies`, `KeysStrategies`, `MasterStrategy`, `GuitarStrategies`, `BassStrategies`). Strategies edit a `ChannelParameters` inside
  `TuneDecisions::move()`; changes are recorded by diffing, so never hand-write `ParameterChange` lists. Every numeric
  target/safe range belongs in `src/Profiles/ProfileData.cpp`, not in a strategy. Tune must stay idempotent (re-tune with
  the same capture -> NO CHANGE REQUIRED; tested): cut rules compute from the profile template, never from the current
  value. A high-pass never goes above 0.8 x the measured fundamental. Sustained sources never get an expander.
- Real stems for listening/offline checks: `/Users/calebwork/Downloads/stems recording` (church multitracks). Run
  `build/modules/Drums/livemix_tune_stems "<Source>" <file.aif> [seconds] [gospel|worship]` to see measurements + decisions.
- User-facing wording is TUNE / RE-TUNE / TUNE KIT (internal names like `AnalysisEngine`, `startAnalyze` stay).
- AI is optional: default Off, explicit user action only, `SafetyValidator` on every path, Standard
  fallback on any failure, AI items never enter the proposed parameters, nothing AI-related on the audio thread.
- Profiles: Modern Gospel is the default; Modern Worship is a documented delta. Do not add profiles that
  cannot be tuned by listening.
- Verify UI changes with `cmake --build build --target livemix_ui_snapshots && build/modules/Drums/livemix_ui_snapshots <dir>`
  (Drums) and `build/modules/<Vocals|Keys|Master|Guitar|Bass>/livemix_<vocals|keys|master|guitar|bass>_ui_snapshots <dir>` and look at the PNGs;
  regression references are regenerated only when baselines change on purpose. Plugin tests per product:
  `build/modules/<P>/livemix_<p>_plugin_tests` (+ `livemix_plugin_tests` for Drums kit/AI-parsing specifics).
  AU ids: Drums `Lmdr`, Vocals `Lmvo`, Keys `Lmky`, Master `Lmma`, Guitar `Lmgt`, Bass `Lmba`, FX `Lmfx` (manufacturer `Lvmx`); `auval -v aufx <code> Lvmx`.
- Dine FX (`src/FX`, `modules/FX`): parameters in `src/FX/FxParameterSpecs.cpp` visited by `forEachFxField`
  (`src/FX/FxParameters.h`); numbers only in `src/FX/FxProfiles.cpp`; macros in `FxMacroMapping` (50 = baseline, tested
  idempotent). Verify with `build/tests/livemix_tests Reverb|Delay|FxChain`, `build/modules/FX/livemix_fx_plugin_tests`,
  `auval -v aufx Lmfx Lvmx` and `build/modules/FX/livemix_fx_ui_snapshots <dir>`.
- Read the PRD sections 6-8, 42, 48 and `docs/ARCHITECTURE-DINE-CORE.md` before touching the audio path or adding a product.
