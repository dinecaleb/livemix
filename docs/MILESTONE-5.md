# Milestone 5 report: Dine Vocals, Dine Keys, Dine Master (one shared channel plugin)

Date: 2026-09-07. Builds on `MILESTONE-4.md`. Three new products from Dine Core, a plain-language pass on every Simple
view, and the first validation against recorded church multitracks.
Machine: Apple Silicon, macOS 26.4, JUCE 8.0.8, CMake 4.4.3, Release builds.

## 1. What was built

- **One channel plugin for four products.** `modules/Common/ChannelPluginProcessor` + `ChannelPluginEditor` are the former
  Dine Drums processor/editor made generic; a `ProductDefinition` (`src/Core/ProductDefinition.h`, data in
  `src/Profiles/ProductData.cpp`) supplies the sources, the five Simple knobs (id, plain label, tooltip), the chain stages
  and the wording. `modules/Drums|Vocals|Keys|Master/` are thin subclasses plus one CMake call
  (`modules/Common/DineChannelProduct.cmake`), each producing an AU + Standalone, a headless plugin test and a snapshot tool.
  Dine Drums behaviour, parameter ids, state format and DSP are unchanged (regression renders and the Drums suites pass).
- **Sources.** `ChannelRole` grew from 12 to 27 (drum indices untouched): Lead Vocal, Backing Vocal, Choir, Speech, Vocal
  Bus; Piano, Electric Piano, Organ, Synth Pad, Synth Lead, Keys Bus; Livestream, Broadcast, Recording, Room PA (the
  master's "source" is the delivery). 18 families, each with Modern Gospel targets + baseline and Modern Worship deltas.
- **DSP** (`src/DSP`, JUCE-free): `DeEsser` (LR4 split, high band ducked only while an S is loud, allpass-flat otherwise),
  `StereoWidth` (mid/side with the side high-passed below "mono below", correlation meter), `Limiter` (1.5 ms lookahead,
  exact sliding-minimum gain, clip safety, delay kept during A/B so latency is constant), `LoudnessMeter` (BS.1770
  K-weighting designed from the analogue prototypes, momentary / short-term / gated integrated, interpolated true peak).
  `ChannelProcessor` gained the four stages behind per-product `Options`; drums pay ~4 % for the disabled checks.
- **Analysis**: sibilance (95th percentile of the 5 kHz+ band vs full band on loud 10 ms frames), stereo correlation,
  ungated loudness and true peak of the capture.
- **Tune**: `VocalStrategies`, `KeysStrategies`, `MasterStrategy` on the shared toolkit plus `controlSibilance`,
  `setWidth`, `setLoudness`. Toolkit wording is product-aware ("hits" / "phrases" / "notes" / "peaks", "a drum mix" /
  "a vocal mix"). Section names read INPUT / TONE / LEVEL / CLARITY (ATTACK on drums) / CLEAN-UP / MIX.
- **Parameters**: one table per product (`channelParameterSpecs (Product)`); stages a product does not expose have no host
  parameter and stay at their defaults. `findParameterSpec` searches every table. Every product's knobs are forbidden ids
  for Tune and AI. AI limits added for de-esser range, width and limiter ceiling.
- **Simple view wording**: Drums PUNCH BODY ATTACK TONE BLEED (tooltips rewritten in plain words); Vocals WARMTH CLARITY
  SMOOTH STEADY CLEAN-UP; Keys WARMTH SHINE CLEAN-UP STEADY WIDTH; Master WARMTH CLARITY GLUE LOUD WIDTH. The chain strip
  shows BLEED / CLEAN-UP, TONE, S CONTROL, LEVEL / GLUE, SNAP, WIDTH, SAFETY with the engineer's term as a small subtitle;
  Advanced keeps the engineer's names. Master shows "−15.2 LUFS · target −14 · 1.2 dB quiet · peak −1.0" under the knobs.
- **AI assistance is switched off on purpose** (`src/Intelligence/AIFeature.h`): no provider, no button, no menu, Standard
  Tune only. The code paths remain behind the constant.
- **Tools**: `livemix_tune_stems <source> <file>` runs the analysis + Tune offline on a recording and prints every
  measurement and decision; `livemix_<product>_ui_snapshots` renders every source, stage and Tune state of a product.

## 2. What the church stems taught (and what changed because of it)

`/Users/calebwork/Downloads/stems recording` (lead, four backing vocals, pastor, stereo keys, drums, room, bass) at 48 kHz.
Four rules were wrong on real audio and are now covered by tests (`tests/Tune/ProductTuneTests.cpp`):

| Finding | Fix |
|---|---|
| Drum bus / master re-tune kept cutting the high shelf 0.5 dB per pass (cut computed from the current value) | Every cut rule (`shapeBody`, `shapeAttack`, `shapeAir`) computes from the profile template; re-tune converges in one step |
| Male lead (126 Hz fundamental) got its high-pass moved to 140 Hz, above the voice | A high-pass is never placed above 0.8 x the measured fundamental (toolkit and vocal strategy); vocal low-band targets made male-friendly |
| Continuous singing read as "bleed 1.0" and got an expander fitted 6 dB under the phrases | Sustained sources (no gaps, < 0.5 events/s, floor within 20 dB) never get an expander; the template one is bypassed with an explanation |
| A −42 LUFS capture had the master push +12 dB and ask for more on re-tune | When the input is below the healthy capture range, loudness waits for the preamp advice instead of pushing plugin gain |

After the fixes every stem re-tunes to NO CHANGE REQUIRED (lead, backing, speech, piano, organ, kick, snare, drum bus,
livestream). Other observations worth a listening pass: the keys stem is very dark (brilliance −49 dB re total) and the
profile asks for only +2 dB of air; the pastor mic gets −3.5 dB at 350 Hz and +3 dB at 3 kHz, which reads right.

## 3. Tests

| Suite | Cases |
|---|---|
| `tests/DSP/NewStageTests.cpp` | de-esser flat below threshold at 5 frequencies and band-selective above it; width 0 / 1 / mono-below / correlation / mono streams; limiter ceiling, sample-exact delay, bypass, no allocation; loudness meter −23 / −20 LUFS and gating; ChannelProcessor limiter latency + A/B delay |
| `tests/Analysis/VoiceAnalysisTests.cpp` | sibilance high vs smooth voice; correlation, loudness, true peak |
| `tests/Tune/ProductTuneTests.cpp` | products / tables / hidden stages; every new family has targets + baseline + strategy; idempotence for 15 sources; vocal S control, boom, choir, speech; keys width and mud; master loudness per delivery; macro mapping per product; the four stem regressions |
| `tests/Integration/ChannelPluginTests.cpp` (x4 products) | identity + hidden parameters, state round trip, foreign state ignored, latency (72 samples on Master only), A/B delay, allocation-free processBlock, Tune alongside processing with product-only parameters and BEFORE/AFTER/REVERT, presets, knob idempotence, editor lifecycle, 32 instances |

Engine: 106 cases, 0 failures. Plugin suites: Drums (kit/AI parsing), Drums, Vocals, Keys, Master, FX all PASSED.
`auval`: SUCCEEDED for `Lmdr Lmvo Lmky Lmma Lmfx` (same default-value round-trip warning class as before on skewed ranges).

## 4. CPU / latency

| Measure | Result |
|---|---|
| Latency | 0 on Drums / Vocals / Keys; 72 samples (1.5 ms @ 48 kHz) on Master, constant on/off and during A/B |
| `ChannelProcessor` benchmark, 48 instances @ 48 kHz / 64 | 183 us per block (13.7 % of budget); Milestone 3: 176 us |
| 32 real plugin instances @ 64 | Drums 116 us, Vocals 117 us, Keys 98 us, Master 106 us per block for all, 0 over-budget blocks |
| `processBlock` steady state | 0 allocations / 2000 blocks on every product |

## 5. Known weaknesses

- Profile numbers for voices, keys and master are engineering starting points validated on synthetic signals plus one
  set of quiet church stems; a listening pass with the plugins in Logic is the next step.
- The de-esser is split-band only (no wideband mode) and the limiter has no oversampling (true-peak overshoot between
  samples is possible by a fraction of a dB; the meter's interpolated peak shows it).
- Group tuning (KIT) exists only for drums; a "tune all the vocals together" view would reuse the same machinery.
- FX Tune is still not written; Bass / Guitar / Sax products are placeholders.
- Two auval default-value warnings on Drums / Vocals and one on Master (skewed-range round trip), as before.

## 6. Listening validation, in priority order

1. Lead vocal and speech baselines on the real mics (warmth shelf at 180 Hz, presence at 3.5 kHz, S control 6.5 kHz).
2. Keys width and mono-below (120 Hz) on the stereo keys stem; organ drive.
3. Master: livestream at −14 LUFS with the limiter under 3 dB of gain reduction on a full-band mix.
