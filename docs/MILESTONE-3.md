# Milestone 3 report: Dine direction, Modern Gospel profile, Tune

Date: 2026-09-06. Builds on `MILESTONE-2.md`. Direction: the "DINE — PRODUCT DIRECTION UPDATE" brief.
Machine: Apple Silicon, macOS 26.4, JUCE 8.0.8, CMake 4.4.3, Release builds.

## Gap analysis (before this milestone)

**Already supported the architecture**

- A JUCE-free engine (`src/`, target `livemix_engine`) that is Dine Core in everything but name: shared DSP
  modules, wait-free capture + worker-thread analysis, deterministic recommendations, `SafetyValidator` on
  every path, optional `IIntelligenceProvider`, one parameter table, `InstanceRegistry`/`IKitEndpoint` for
  future inter-plugin work, Simple/Advanced UI on host parameters, loudness-matched A/B, Live Safe, state
  round-trip tests, allocation tracking, regression renders, benchmark.
- Preamp (capture-gain) recommendations already distinct from plugin trim.

**Needed refactoring**

- Profiles were procedural switch statements with the wrong set (Modern Worship, Natural, Punchy Live,
  Broadcast, Warm, Aggressive) and no Modern Gospel; targets and baselines were tangled with code.
- All decision logic lived in one 280-line function with generic band rules; no source strategies.
- Results were a list of independent suggestions with per-item toggles, not a proposed starting point
  with BEFORE / AFTER and KEEP.
- User-facing wording was "Analyze"; product name was LiveMix.

**Missing**

- Fundamental and decay measurements (needed for adaptive, not static, decisions).
- Signal-triggered capture start inside the engine (the editor did a crude 4 s wait).
- Detector (sidechain) high-pass on gate and compressor (kick/tom gates false-triggered by low bleed,
  kick/bus compressors pumped by sub energy).
- FX primitives; a Tune result model; profile safe ranges.

**Should not be built yet (and was not)**

Bass / Keys / Guitar / Vocals / Sax / FX / Master plugins, reverb and delay algorithms, cloud AI work,
session-wide automatic mixing. Their architectural hooks are described in `docs/ARCHITECTURE-DINE-CORE.md`.

## 1. What changed

- Product is **Dine Drums** (AU name, bundle id `com.dine.drums`, standalone). AU codes stay `aufx Lmdr Lvmx`;
  CMake target/library names stay `LiveMix*`; C++ namespace stays `livemix`. The stale
  `LiveMix Drums.component` (same AU codes) was removed from `~/Library/Audio/Plug-Ins/Components`.
- User-facing workflow is **TUNE**: TUNE / RE-TUNE button, "Waiting for signal" card, Listening card, and a
  Tune card with a headline ("SNARE TOP TUNED" / "NO CHANGE REQUIRED" / "NO SIGNAL"), one line per section
  (INPUT, TONE, DYNAMICS, ATTACK, BLEED, MIX), the full explanation list, and BEFORE | AFTER, KEEP, REVIEW,
  REVERT. REVIEW opens Advanced mode with every decision listed, each with UNDO. LAST TUNE reopens the card.
  Kit view says TUNE KIT.
- Profiles are now **Modern Gospel** (default) and **Modern Worship** (derived, documented deltas).
- Two new parameters: `gateScHpf` and `compScHpf` (detector high-pass, 0 = off). State version 2; older
  state loads unchanged (new parameters take their defaults).
- Tune preview state (BEFORE snapshot, proposed settings, which one is audible) is persisted, so a session
  saved mid-comparison can still be kept or reverted after reload.

## 2. Architecture changes

```
src/Profiles/Profile.h        SourceTargets (targets + safe ranges), ProfileDefinition, Profiles::{definition,targets,baseline}
src/Profiles/ProfileData.cpp  Modern Gospel data (7 families x targets + baselines), Modern Worship deltas, role refinements
src/Profiles/StyleProfile.h   facade for existing callers
src/Tune/TuneTypes.h          TuneContext, TuneResult (headline, sections, before, proposed), applyChanges, diffParameters
src/Tune/SourceStrategy.h     SourceStrategy interface, TuneDecisions (move()/note(); changes recorded by diffing), toolkit API
src/Tune/StrategyToolkit.cpp  shared engineering rules (input, HPF, body, low-mid, notch, attack, harshness, air, comp, gate, mix)
src/Tune/DrumStrategies.cpp   Kick, Snare, Tom, Overhead, Room, HiHat, Bus strategies + strategyFor(family)
src/Tune/TuneEngine.cpp       analysis + profile + strategy -> validated TuneResult
src/Recommendations/          RecommendationEngine::recommend is now a facade over TuneEngine (kit rules, tests unchanged)
src/FX/                       DelayLine, SchroederAllpass, DampedComb, LFO, TempoSync (foundations only)
```

- `AnalyzeCoordinator::run` calls `TuneEngine::tune`; it keeps the report for the kit rules and AI merge and
  exposes `getTuneResult()`. AI items only ever append to the report; they never enter the proposed parameters.
- `DrumsProcessor` applies the proposed parameters as a preview when Tune completes (never in Live Safe),
  keeps the BEFORE snapshot, and offers `setTuneCompare / keepTune / revertTune / undoTuneItem`.
- `AnalysisEngine::startCapture(seconds, triggerDb, maxWaitSeconds)` adds `State::Waiting`: the window starts
  at the first 10 ms frame above the trigger (-45 dBFS), or after 20 s regardless.
- Data vs. logic: profile numbers live only in `ProfileData.cpp`; strategies contain no numeric targets of
  their own beyond structural constants (which corrective band does what).

## 3. DSP implemented

- Detector high-pass (2nd-order Butterworth, coefficients recomputed only when the value changes) on the
  gate and compressor detectors. Audio path stays full-band. Tests: a 40 Hz rumble at -6 dBFS no longer opens a
  gate whose detector sits at 120 Hz (output RMS < 20 % of the unfiltered case); sub content drives > 8 dB less
  gain reduction with a 150 Hz detector filter while a 1 kHz tone gets the same 18 dB.
- Modern Gospel baselines per family (kick, snare, hi-hat, tom, overhead, room, bus) with detector filters set
  where they matter (kick 30/60 Hz, snare 150/100 Hz, tom 80/60 Hz, bus comp 80 Hz).
- No algorithm is incomplete or placeholder in the audio path. The saturator still has no oversampling (drive
  is bounded); reverb/delay do not exist yet, only their primitives.

## 4. Tune behaviour

Deterministic, local, no AI. Measurements used: peak, RMS, crest, 10 ms frame histogram (hit level, floor,
dynamic range, silence), transient count/rate/rise, mean 20 dB decay time, band energy, third-octave curve,
resonances, fundamental (30-500 Hz, parabolic-interpolated), stereo balance, bleed estimate, clipping, DC.

Per family (all bounded by the profile's safe ranges; "no change required" whenever inside tolerance):

- **Kick**: HPF half an octave below the measured fundamental; body shelf placed just above the fundamental,
  gain from the Low-band deficit; excess sub raises the HPF instead of EQ; low-mid mud cut centred on a measured
  resonance if there is one; shell ring notch; definition (presence deficit or soft transients) via the 4 kHz
  peak and transient attack; compressor fitted to the measured hit level for a target gain reduction, ratio /
  attack / release scaled by crest severity and hit rate; expander fitted from floor/hit levels with hold and
  release from the measured decay and a detector HPF at 0.7 x fundamental.
- **Snare**: as kick plus ring notch (300-1500 Hz), harshness cut (never a boost elsewhere), air shelf.
- **Toms**: as kick plus sustain control from the measured decay (> 600 ms ring shortened, bounded).
- **Overheads / Room**: never gated or transient-shaped; kick spill raises the HPF inside the profile range,
  thin overheads lower it; harshness peak cut; air; gentle compression; room gets sustain when it decays fast.
- **Hi-hat**: filtering, harshness, light compression only.
- **Drum bus**: broad tonal moves, glue compression (2-3 dB), a touch of saturation only when peaks are wild.
- **Input**: preamp advice is bounded to one 10 dB step, worded as console/interface preamp, never a plugin
  change; "Re-Tune after adjusting" is stated.
- **Dense sources** (crest below the profile minimum) get compression bypassed with an explanation.
- Tune is idempotent: re-tuning a tuned source with the same capture yields NO CHANGE REQUIRED (tested for
  every family).

## 5. Profile behaviour

`SourceTargets` per family: capture range and max preamp step; band targets and tolerances; fundamental range,
body/boxiness/attack/harshness/air regions; HPF range; max EQ cut, boost and notch; resonance prominence
threshold; crest range; compression target GR, ratio/attack/release ranges, detector HPF; transient limits;
gate threshold/range/detector HPF; saturation limit; mix peak target and kit balance offset; one line of intent.

Modern Worship = Modern Gospel with: +1.5 dB crest tolerance, -1 dB target gain reduction, -1 ratio ceiling,
-0.1 saturation ceiling, slightly less kick sub, smoother overhead brilliance, quieter room, -0.5 baseline ratio,
lighter/no baseline saturation, kick shelf +2 dB at 60 Hz, more room sustain.

## 6. Tests added

| Suite | New cases |
|---|---|
| `tests/Tune/TuneEngineTests.cpp` | idempotence for 7 families; proposed = before + changes and within spec; kick fundamental/HPF/body; sub excess -> HPF not EQ; snare harshness cut / ring notch; dynamics threshold follows hit level + trim, dense source bypassed; gate follows bleed and decay, overheads never gated; overhead spill/harshness/stereo note; input advice bounded and preamp-worded; sections + headline; profile data sanity; Worship vs Gospel |
| `tests/DSP/DetectorFilterTests.cpp` | gate and compressor detector HPF behaviour |
| `tests/Analysis/TuneCaptureTests.cpp` | waits for signal then captures from the onset; wait timeout; abort while waiting; fundamental + decay on a synthetic tom |
| `tests/FX/FXFoundationTests.cpp` | delay line exactness / interpolation / no allocation; allpass unity magnitude; comb ring and decay; LFO; tempo sync |
| existing | updated for the new profile set, bounded mix gain, expander wording; regression references regenerated (baselines changed deliberately) |

Engine: 72 cases, 0 failures. Plugin integration: PASSED (state, malformed state, bypass, sample rates, allocations,
tune, presets, kit, 48 instances). `auval`: AU VALIDATION SUCCEEDED, 80 parameters, same two default-value
warnings as before. UI snapshots: all 22 states render (`tools/UISnapshots.cpp`).

## 7. CPU / latency

| Measure | Result |
|---|---|
| Latency | 0 samples (unchanged; detector filters are on the control path only. Dine Drums has no lookahead stage - the Milestone 5 limiter on Dine Master reports 1.5 ms) |
| `processBlock` steady state | 0 allocations / 2000 blocks; 5.9 us per 64-sample stereo block |
| 48 real plugin instances @ 48 kHz / 64 | 166 us per block for all (12.5 % of budget), 0 over-budget blocks |
| Benchmark 48 instances @ 64 / 128 / 256 | 176 us (13.2 %) / 384 us (14.4 %) / 810 us (15.2 %) |
| Tune (after capture) | 0.2 s wall in the headless test, message thread, no audio-thread involvement |

The detector filters add ~4 % to the chain cost (169 -> 176 us at 48 x 64).

## 8. Known weaknesses

- Every profile number, threshold and slope is an engineering starting point validated on synthetic signals only.
- Fundamental detection picks the strongest 30-500 Hz peak; a loud boxy component above the family's fundamental
  range makes Tune fall back to the profile body region (correct but less adaptive).
- Bleed estimate is level-based (floor vs. hits); it cannot distinguish cymbal bleed from a noisy preamp.
- Section summaries show the first decision in the section, not the most important.
- Kit tune applies each member's proposal automatically; the kit view has RE-APPLY and BALANCE but no kit-wide
  REVERT (per-channel revert works from each editor).
- A Tune preview survives a session save/reload, but the AI interpretation text does not (as before).
- Saturator without oversampling; A/B is loudness-matched but a Tune preview is not (BEFORE/AFTER is honest level).
- Warnings: two auval default-value round-trip warnings on skewed gate ranges (unchanged).

## 9. What needs listening validation

In priority order, on real multitrack captures (church gospel kit first):

1. Modern Gospel baselines for kick, snare, toms, overheads (weight, crack, harshness control).
2. Compression fit: target GR (4.5 / 4 / 3.5 dB), attack range, release-from-hit-rate rule.
3. Expander fit: 40 % of the floor-to-hit gap, range 10 + 30 x bleed, hold/release from decay.
4. Body shelf placement at 1.15 x fundamental and the boost slope (0.6 dB per dB of deficit).
5. Overhead HPF adaptation and harshness cuts (are they audible as "smooth", or as "dull"?).
6. Modern Worship deltas.
7. The trigger level (-45 dBFS) and 12 s window on a real stage with hi-hat bleed on every mic.

## 10. Recommended next milestone

**Listening pass + profile tuning (no new features).** Load real stems in Logic, Tune every channel, capture
BEFORE/AFTER preferences and the beta questions from the brief (bright/dark, compressed/open, muddy/thin, punch,
impact, cohesion), then adjust `ProfileData.cpp` only. In parallel, engineering work that does not touch
sound: a kit-wide REVERT, "most important decision" ordering for section summaries, and the second product
target (Dine Bass or Dine Vocals) as a thin composition of Dine Core to prove the abstractions before FX.
