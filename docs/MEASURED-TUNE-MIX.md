# Measured Tune Mix

## Audit

The existing deterministic path is the seed, rather than an alternative mixer:

- `MixPlanner` uses `TuneEngine`, per-family strategies, `Profiles`, raw source analysis,
  processed RMS/peak statistics, relationship rules, fitted faders, group predictions and
  master loudness prediction. Its predictions approximate compressor curves and spectral
  effects; they cannot measure the phase-dependent sum or the actual nonlinear chain.
- `MixCapture` previously kept statistics only. Listening again after applying a proposal
  measures a different performance, so it cannot establish a controlled before/after result.
- `MixAI` already separates measured relationships, capabilities, intent resolution, validated
  actions and the coordinator. Its local provider is deterministic; no provider is required
  for the new path. Existing opt-in cloud code is unchanged.
- `OfflineCapture` and `MixEngine` already provide synchronous analysis and the real strip,
  sample, fader/pan, bus, FX, speech-priority and master processing paths. They are reused.
- `SafetyValidator`, `ParameterSpecs`, source targets, mix relationship targets and bounded
  snapshots remain authoritative. No alternative DSP, meter, processor chain or LLM was added.

## Implementation

`MixCapture::Result::replay` is an immutable shared converter-PCM excerpt, retained on the
capture worker. The audio callback only copies to a preallocated SPSC FIFO. Converter audio
is captured before digital gain, so replay applies the proposed gain exactly once. Streams
are consumed as a common prefix: a worker waking halfway through a device block cannot
advance one source ahead of another at the trigger.

Retention is limited to 12 seconds and 128 MiB (`MixProfile::measuredTune`). The existing
longer statistical listen is unchanged. Replay is absent on mismatched streams, dropped
source/converter frames or an insufficient excerpt. The ordinary deterministic path remains
available in that case. PCM is transient; it is not part of session serialization or provider
requests. No new network call or dependency is introduced.

`MeasuredMix` runs only on an offline/worker thread. Each candidate gets a fresh `MixEngine`
with identical PCM, initial conditions and sample banks. Parameters are published **after**
`prepare`, because prepare publishes the engine baseline. This avoids accidentally measuring
the baseline instead of the proposed chain. Input assignments are remapped to the retained
strip channels, including stereo and disabled assignments. Missing sample banks invalidate
replay rather than silently evaluating a different replacement sound.

The renderer uses the existing `AnalysisAccumulator`/`OfflineCapture` to measure raw strips,
processed strip spectra and levels, group inputs, group outputs, and the final master.
Half-second processed windows identify coactive source pairs. Relationship analysis reuses
`RelationshipEngine` and the planner's focal-source selection. It covers lead versus backing,
keys/guitars/snare/overheads, kick versus bass, and actual group hierarchy after group EQ and
compression. Upper-mid and presence energy both matter for the vocal pocket.

Individual-source findings include the actual strip chain and bus fader. They are labelled
**potential masking**, since a shared nonlinear bus cannot be attributed independently to
its member sources. Group-output hierarchy is measured directly, before speech-priority
attenuation. A masking metric is not a psychoacoustic intelligibility score.

At most three correction passes:

1. Cut a measured competitor conservatively; retain the focal source. Reuse the profile's
   presence EQ pocket on suitable music strips. Linked strips are left to group corrections
   to preserve their relative balance.
2. Refit used group chains from their actual measured inputs through the existing
   `TuneEngine` strategies and safety validation.
3. Adjust master trim toward the delivery target within the measured peak room. Do not
   manufacture loudness by increasing limiter reduction. A sermon-only plan retains the
   planner's existing master decision.
4. Reapply the scope selection so no unselected source/group/master can move.
5. Render again and accept only a strictly better objective score with finite output,
   true-peak and mono-safety checks and bounded spectral, hierarchy, loudness and crest
   regression. Stop on rejection, convergence, cancellation or the pass limit.

All distances are bounded against the initial proposal. Captured parameters anchor the seed:
keeping a result and re-planning the same listen cannot accumulate corrections. The current
snapshot supplies BEFORE and the preserved state outside a partial selection. Final counters
and Inspector values are refreshed from the exact accepted snapshot. An unsafe measured seed
is replaced by BEFORE and carries a refusal note; it is never called safe.
The planner explicitly marks unusable listens as refused. Silence and steady fault tones
retain their refusal message and BEFORE snapshot, including under a scope selection;
measurement cannot turn them into a correction request.

`MixController` runs full/scoped/reused/reference planning in an asynchronous job and polls
completion. Cancellation, a rebuild and a hand edit/revision change invalidate late results.
Reference matching selects only MASTER. Preview, KEEP, REVERT, LIVE SAFE and the existing
snapshot publication path remain in charge. `TuneLiveCoordinator` also measures its resolved
proposal before exposing it for application; its original fallback remains available when
replay cannot be performed.

## Verification and reproduction

Engine build and tests:

```sh
cmake -S . -B build-engine -DLIVEMIX_BUILD_PLUGIN=OFF
cmake --build build-engine --parallel 4
ctest --test-dir build-engine --output-on-failure
build-engine/tests/livemix_tests MeasuredMix
```

Application checks (normal JUCE build):

```sh
cmake --build build --target dlive_app_tests dlive_mix_stems --parallel 4
build/app/dlive_app_tests MeasuredPlanning
build/app/dlive_app_tests MixController
```

The new tests cover exact replay, input gain applied once, nonlinear compression/saturation,
missing/invalid PCM and sample banks, bounded deterministic passes, idempotence after KEEP,
processed-spectrum relationships, coactivity, bus faders, mutes, peak refusal, disabled
assignments, partial selection, background publication, cancellation and hand-edit races.
Existing realtime allocation tests cover the extended capture tap.

The existing regression-render test had an out-of-bounds final block (24000 samples with
128-sample blocks). Its final block and the shared test-buffer view now clip to the remaining samples (the
DetectorFilter fixtures had the same issue); reference
files and production DSP were not changed. Public headers now include the standard headers
needed for their own `size_t`/math declarations rather than relying on platform transitive
includes.

## Real-stem before/after evaluation

Run the same multitrack window, profile and delivery through both versions:

```sh
build/app/dlive_mix_stems "<stems-folder>" 30 "<output-folder>" gospel 60 livestream --check
```

Outputs include `before.wav`, `predicted.wav` (the old deterministic seed), `after.wav`
(the measured result), `after-retuned.wav` and `evaluation.json`. JSON records availability,
safety, gated LUFS, true peak, crest, loudness/spectral/hierarchy errors, objective score,
relationship measurements, render count and accepted passes. `--check` also requires
available evaluation, a safe measured result and no objective-score regression.

Evaluate several verse, chorus, dense arrangement, speech/vamp and sparse windows across
both church recordings referenced in `BUILD-AND-VERIFY.md`. Listen blind to loudness-matched
`predicted.wav`/`after.wav`, in stereo and mono. Record vocal intelligibility, kick/bass
separation, harshness, pumping, drum attack and naturalness. Repeat each request to establish
identity; use a *new* captured performance to measure retune stability separately.

The full church recordings live on the user's Mac and are not included in this repository.
No real-service quality improvement is claimed without running that dataset. The bundled
one-shot samples and synthetic regression files are not substitutes for a live multitrack.

## Evidence from this implementation

Portable GCC/JUCE 8.0.8 builds in the implementation environment passed the 245-case
engine suite, including the new measured renderer and Tune Live integration tests.
All 53 tests in the controller/application fixture suite passed, including preview/KEEP,
refused listens, cancellation, reference scope and stale manual edits. The final refusal
change also passed all 11 measured renderer tests. The platform GUI/device application was not built in this Linux environment.

A controlled synthetic over-loud BGV fixture reduced the objective score from 63.73 to
52.79 and relationship excess from 17.00 to 6.00 dB, accepting two passes. This demonstrates
a measurable correction, not a general listening-quality claim. The fixture remains
spectrally unlike real music.

A separate five-source, two-second steady-tone WAV export exercised the real file loading,
BEFORE/predicted/AFTER exports and JSON harness. Exported loudness/true peak matched the
fresh-engine evaluator, same-listen re-tune was unchanged, and safety/non-regression passed.
Its strict `--check` correctly failed the delivery loudness goal (2.83 LU short), and the
planner refused to treat the steady tones as a musical performance. Rejected/unchanged
results must remain part of the evaluation, rather than being counted as improvements.
