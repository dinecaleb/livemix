# Dine FX (reverb + delay, AU + Standalone)

One instance of Dine FX is one effect for an aux/send: a reverb, a delay, or a delay that blooms into a reverb
(VOCAL THROW). Everything is composed from Dine Core (`src/FX`), nothing is duplicated from Dine Drums.
AU codes `aufx Lmfx Lvmx`, bundle id `com.dine.fx`, product name "Dine FX". CMake target `LiveMixFX`.

## Engine (`src/FX`, JUCE-free)

| Piece | File | What it is |
|---|---|---|
| `ReverbAlgorithm` | `ReverbAlgorithm.*` | pre-delay, input low/high cut, 6-tap early reflections, 4 input diffusers and a modulated figure-of-eight plate tank (Dattorro 1997). Lengths scale with SIZE and the sample rate; allpass-interpolated reads keep the tank flat at fractional lengths; RT60 is set from DECAY via the tank loop length. Parameters: decay, pre-delay, size, damping, diffusion, low cut, high cut, mod rate/depth, early level, level. |
| `DelayAlgorithm` | `DelayAlgorithm.*` | mono / stereo / ping-pong, free ms or host-synced note division (`TempoSync`), stereo offset, feedback with low/high cut in the loop, width, modulation, ducking from the dry signal (up to 30 dB, attack 5 ms, release parameter). Time changes crossfade between two read heads (40 ms) so automation and tempo changes never click. |
| `FxChain` | `FxChain.*` | Input meter -> Trim -> [Delay] and [Reverb] in parallel on the dry signal (+ DELAY TO REVERB send) -> Mix -> Output trim -> Output meter. Loudness-matched A/B like the drum chain. Zero latency; tail reported to the host. |
| `FxParameters` | `FxParameters.h` | the POD struct, `FxType` (7 reverb types, 7 delay types), `DelayMode`, `forEachFxField` (one visitor drives the audio-thread reader, the message-thread writer, presets and tests) |
| `FxParameterSpecs` | `FxParameterSpecs.*` | the FX parameter table (shell ids `profile` `bypass` `abMatch` `liveSafe`, `fxType`, the five macros, every DSP field). The JUCE layout is generated from it; `findParameterSpec` falls back to it so shared widgets work for any product. |
| `FxMacroMapping` | `FxMacroMapping.*` | SPACE, LENGTH, WARMTH, CLARITY, DISTANCE (50 = baseline) -> bounded engineering moves; `affectedParameterIds()` lists what a macro may rewrite |
| `FxProfiles` | `FxProfiles.*` | Modern Gospel baseline per type, Modern Worship deltas (+15 % decay, +5 ms pre-delay, 10 % darker, delays repeat a little more and duck harder), one line of intent per type, the six named use presets |

Parameter ids (never rename): `fxType`, `space length warmth clarity distance`, `fxInputTrim`, `rv*` (reverb), `dl*` (delay),
`fxMix`, `fxOutputTrim`, plus the shared shell ids. State root `LiveMixFxState` version 1.

## Types and defaults (Modern Gospel)

| Type | Engine | Starting point |
|---|---|---|
| Vocal Plate | reverb | 1.9 s, pre 28 ms, size 45 %, dense, 160 Hz / 8.5 kHz |
| Vocal Hall | reverb | 2.6 s, pre 40 ms, size 70 % |
| Worship Hall | reverb | 3.4 s, pre 55 ms, size 85 %, darker |
| Room | reverb | 0.8 s, pre 10 ms, size 30 %, mostly early reflections |
| Drum Room | reverb | 0.9 s, bright, explosive early energy, 90 Hz low cut |
| Snare Plate | reverb | 1.4 s, bright, 220 Hz low cut |
| Large Ambient | reverb | 6.5 s, pre 80 ms, size 100 %, dark, heavy modulation |
| Slap Delay | delay | mono 105 ms, no feedback |
| 1/4, 1/8, Dotted 1/8 Delay | delay | stereo, synced, 28-35 % feedback, 20-25 % ducking |
| Stereo Delay | delay | 1/4, right channel -25 % offset |
| Ping-Pong Delay | delay | dotted 1/8, 40 % feedback, ducked |
| Vocal Throw | delay + reverb | ping-pong 1/4, 45 % feedback, 70 % ducking, 40 % into a 2.2 s plate |

MIX defaults to 100 % (aux/send use). Use presets: Modern Gospel Lead Vocal, Modern Gospel BGV, Worship Lead Vocal,
Large Worship Ambient, Gospel Snare Plate, Live Recording Vocal (profile + type + macro offsets, `FxProfiles::usePresets`).

## Plugin (`modules/FX`)

- `FxProcessor`: `juce::AudioProcessor`; `processBlock` = read the play-head tempo, `bridge.read()` (cached atomics),
  `chain.process()`. Type / profile / presets / macros are message-thread only (`reloadPreset`, `applyMacros` on a timer).
- `FxEditor`: the common Dine shell (`TopBar` with TYPE / PROFILE, ORIGINAL | DINE, LIVE SAFE; `SubBar` SIMPLE / ADVANCED),
  `FxSimplePanel` (input/output meters, six knobs, engine line, chain strip INPUT -> DELAY -> REVERB -> OUTPUT) and
  `FxAdvancedPanel` (rail + decay envelope / repeat-pattern visual + parameter tiles). Live Safe locks type, profile and presets.
- `FxPresets`: Starting points, Factory (Profile / Type) and User (`~/Library/Application Support/LiveMix/Presets/FX/User`).

## Verify

```sh
build/tests/livemix_tests Reverb      # or Delay, FxChain, "FX "
build/modules/FX/livemix_fx_plugin_tests
auval -v aufx Lmfx Lvmx
cmake --build build --target livemix_fx_ui_snapshots && build/modules/FX/livemix_fx_ui_snapshots <dir>
```

## Not done yet

- Tune for FX (fit pre-delay / decay to tempo and the dry source) and a TUNE button: the `AnalysisEngine` and the
  `TuneResult` shape are ready; the FX strategy is not written.
- Listening validation of every baseline on real stems (all numbers are engineering starting points).
- Output-stage true-stereo input to the tank (the tank is fed a mono sum; early reflections are per channel).
