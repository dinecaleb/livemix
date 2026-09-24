# Drum sample replacement in DLIVE: scope

Written 2026-09-24, before any implementation; Phase 1 was built the same day (see **What was built** at the
end). The question: replace or blend the live kick, snare and tom hits with samples, in a live mix, without
breaking what DLIVE promises - a sample-synchronous channel path, honest latency, an audio thread that never
allocates, a Tune that is deterministic and idempotent, and plain words on the surface. This document says
how each part works, what it costs and what it threatens; where the build differed from the plan, the
section at the end says so.

## The short answer

- **Do it as a stage on the strip, in the engine, zero lookahead.** A detector reads the microphone; a
  sample voice plays into the chain the moment a hit is recognised. Nothing is delayed, so the channel path
  stays sample-synchronous, `getLatencySamples` does not change, and the kit stays phase-coherent with the
  overheads and the room microphones. The price is that the sample starts 0.2 to 1.5 ms after the hit; the
  design absorbs that (onset-trimmed samples, an ALIGN nudge that moves the sample only).
- **Blend, never replace, by default.** A mis-trigger in a service is worse than a slightly dull snare. The
  default is a blend that the room hears as reinforcement, and a trigger that is only just over the line
  plays the sample quietly rather than not at all.
- **Cost is small.** Measured on this machine (`scripts/trigger_bench.cpp`, 2026-09-24): the detector is
  0.7 µs and four interpolated voices 0.7 µs per 128-sample block at 48 kHz, against 8.4 µs for one full
  channel chain. Six replaced strips add about 8 µs per block, 0.3 % of the 2.67 ms budget. Memory is
  about 2.5 MB per drum for a bank of eight one-and-a-half-second samples, 15 MB for a kit.
- **Measure before building.** Phase 0 is an offline tool that runs the detector over the QUEENSVIEW takes
  (the real 21-input service; where the recordings are is in `docs/BUILD-AND-VERIFY.md`) and reports hits found, bleed
  ignored, mis-triggers and detection delay per drum. The threshold rules are written from what it shows,
  not from what this document guesses.
- **DLIVE only, in the first version.** Kick, snare and toms; not the plug-ins, not hi-hat, overheads or
  room (a room microphone is never gated and is never replaced either).

## Where it sits

The chain is fixed and shared (`docs/ARCHITECTURE-DINE-CORE.md`): Input, Filters, Gate, Corrective EQ,
De-esser, Comp, Transient, Tone EQ, Saturation, Width, Output trim. The stage goes **between the Gate and
the Corrective EQ** as an option on `ChannelProcessor::Options` (`sampleReplacement`), configured by
`MixEngine` only for strips whose role is `KickIn`, `KickOut`, `SnareTop`, `SnareBottom`, `RackTom` or
`FloorTom`. Reasons:

- The detector reads the strip **after trim and polarity and before the gate** (the tap the gate's own
  detector reads), on a copy, through its own band filter. The gate must not decide what the detector
  hears, and the detector must not decide what the gate hears.
- The sample is summed **after the gate**, so the gate never chops a sample's tail and the gate can stay as
  TUNE set it for the microphone. The blended signal then runs through the corrective EQ, compressor,
  transient shaper, tone EQ and saturation that TUNE fitted, so a blend sounds like the channel, not like
  a second channel. (A tuned chain was fitted to the microphone's spectrum; a re-tune after the stage is
  switched on refits it to the blend - the listen hears the processed signal, so this comes for free.)
- A stage that is off is bit-transparent, so `tests/reference/*.f32` keep passing and the plug-ins are
  untouched. The order invariant holds: nothing moves, one optional stage is added.

The state lives in three places, by the existing rules:

- `ChannelParameters` (numbers, visited by `forEachDspField`, ids appended never renamed): `replaceOn`,
  `replaceBlend` (0 to 1), `replaceThresholdDb`, `replaceRiseDb`, `replaceDetHpfHz`, `replaceDetLpfHz`,
  `replaceMaskMs`, `replaceSteady` (velocity off), `replaceOffsetMs` (sample only, 0 to +5), `replacePolarity`
  (auto / normal / flip), `replaceRateSemitones` (varispeed, -5 to +5), `replaceGainDb`.
- `StripParameters` (the kept mix, saved): `replaceSampleId` - a string, the bank chosen; strings are not
  DSP fields.
- `src/Profiles/ProfileData.cpp`: per-role defaults (blend, band, mask, rise) and per-style default sample
  (Modern Gospel picks a tight dry kick and a fat snare; every other profile is a documented delta). A
  strategy holds the decision logic, never a target.

## Hit detection

One detector per replaced strip, per sample, allocation-free, on the audio thread:

1. **Band.** A high-pass and a low-pass on the detector copy (two biquads, the gate's `detectorHpfHz` idea
   plus a ceiling). Kick 30 to 150 Hz, snare 150 Hz to 4 kHz, toms 60 Hz to 1 kHz, from the profile. The band
   is what keeps a snare out of the kick detector and a kick out of the snare-bottom detector; most bleed is
   rejected here before any threshold is looked at.
2. **Two followers.** The differential-envelope idea already in `TransientProcessor`: a fast peak follower
   (0.1 ms attack, 20 ms release) and a slow one (10 ms attack, 200 ms release). A hit is the moment the
   fast follower exceeds the slow one by the **rise** (12 dB by default) *and* exceeds the **threshold**
   (absolute, dBFS at the detector). Rise rejects what arrives through the air (bleed rises slowly; a
   close-microphone hit jumps); threshold rejects what is simply quiet.
3. **Mask.** After a hit the detector is masked (kick 40 ms, snare 30 ms, toms 60 ms). Inside the mask a
   louder onset, more than 6 dB above the one that opened it, re-triggers (a flam, a roll); a softer one is
   the same hit ringing. A buzz roll at 30 hits a second therefore triggers at most about 30 times a
   second.
4. **Velocity.** The fast follower's peak in the first 2 ms after the crossing, in dB, mapped onto the
   drum's own range (see Bleed rejection for where that range comes from). "STEADY" ignores it.
5. **Confidence.** A hit that clears the threshold by less than 3 dB plays the sample at reduced level
   (linearly, 0 at the threshold to full at +3 dB) rather than full or nothing. A doubtful trigger is a
   quiet sample, not a phantom snare.

Two microphones on one drum (`KickIn` + `KickOut`, `SnareTop` + `SnareBottom`): each strip could trigger
itself, but the recommended set-up, and TUNE's proposal, replaces on the inside / top microphone only. The
outside / bottom strip keeps its own microphone and its own chain.

## Timing

Why not lookahead: a delay on the kick strip alone puts the kick out of time with its own bleed in the
overheads and the room microphones, which is the comb filtering every engineer has heard. A lookahead
would have to be applied to **every** strip, reported through `getLatencySamples` the way the limiter's
1.5 ms is, and it would add to the broadcast delay and to the solo bus. A 2 ms lookahead is 96 samples at
48 kHz on top of the 2.7 ms the 128-sample buffer already costs. Ruled out for the first version; kept in
the design as a switch that could come later if measurement says the zero-lookahead delay is audible.

What the zero-lookahead design does about the delay:

- **Detection delay** is the time from the drum's onset to the crossing: about 10 to 40 samples (0.2 to
  0.8 ms) on a snare and 40 to 70 samples (0.8 to 1.5 ms) on a kick, whose beater click is fast but whose
  body rises over a quarter of a 55 Hz cycle. These are estimates; Phase 0 measures them per drum.
- **Onset-trimmed samples.** Every sample in the bank is trimmed on load to its own first sample above
  -40 dBFS, so no part of the delay is the sample's own leading silence. The sample starts at the trigger's
  sample index inside the block - not at the block edge.
- **ALIGN** (Advanced) nudges the sample later by 0 to 5 ms, sub-sample, never the microphone. On a kick a
  1 ms offset at 55 Hz is 20 degrees - audible as a slight thickening of the low end rather than a flanged
  attack - and the nudge is what an engineer uses to sit the sample on the microphone by ear.
- **Polarity.** During TUNE the correlation of the sample against the microphone over the first 5 ms of
  the measured hits decides the sample's polarity (auto, with normal / flip by hand). A sample that fights
  the microphone's sub is the most common way replacement makes a kick smaller.
- **Level.** The sample plays at a gain that puts its own peak at the microphone's `hitLevelDb` from the
  listen (`AnalysisResult`), so a 50 % blend neither drops nor jumps the drum's level. Velocity then scales
  it by the drum's own range.

Everything above is deterministic on the signal, so playing a recorded take through the same strip
re-triggers the same samples at the same places: a bounce reproduces the service, and the take itself is
still the raw microphone (`app/native/Recorder` records what arrived, never the blend).

## Bleed rejection

Bleed is the whole problem in a live drum microphone, and the listen already measures it: `AnalysisResult::
bleedLevelDb` is where the quiet group of events sits when a close microphone's events split into two
groups, and `hitLevelDb` / `typicalHitLevelDb` where the real hits sit. The rules, all absolute from the
capture so a re-tune on the same listen says NO CHANGE REQUIRED:

- **Threshold** = bleed level + 0.6 x (hit level - bleed level), never lower than bleed + 6 dB. A strip
  whose listen found no separable bleed (`bleedLevelDb` = -120) is not set up automatically; the engineer
  can still switch it on and set SENSITIVITY by hand, and the sheet says why TUNE left it.
- **Band and rise** as above; between them they reject the slow, filtered, lower-level arrival of another
  drum through the air. Kick in the snare-bottom microphone: killed by the band. Snare in the rack-tom
  microphone: slow rise, under the threshold. Hi-hat in the snare-top microphone: above the band's
  useful energy, low level.
- **Velocity range** is the span from the threshold to the hit level + 6 dB, so bleed that does sneak over
  the line lands at the bottom of the velocity map (the softest layer, and quiet by confidence).
- **Cross-strip veto (Phase 2).** All drum strips run in one `MixEngine::process` call, so the engine can
  keep a tiny wait-free "kit trigger" table: the last trigger time and level per role. A tom trigger within
  2 ms of a snare trigger that was 10 dB louder in its own microphone is bleed unless the tom's own level
  is within 6 dB of its hit level. Strips are processed in order, so the table sees the previous block and
  the strips before this one; that is enough for the common case and Phase 0 says whether it is needed.

Failure mode to design against, in this order: a phantom hit in a quiet moment (confidence, threshold),
a missed soft hit (velocity map, the blend leaves the microphone in), a double trigger on a flam (mask).

## Sample selection

- **A built-in bank**, shipped with DLIVE, recorded dry and close, 48 kHz 24-bit mono, four velocity
  layers with two round-robins each - eight files per drum - for a few kicks, snares and toms. Licensing:
  our own recordings, or a library licensed for redistribution; not something to pick up from the web.
- **User samples**: a WAV dropped onto the strip (the TRACKS page already accepts audio files on the
  timeline; the same drop on a strip's Sample stage) becomes a single-layer bank.
- **Which sample**: the profile's default per role and style; the engineer picks another from a short list
  in plain words ("Tight kick", "Deep kick", "Your own..."). The choice is saved with the kept mix.
- **Which layer**: velocity picks the layer, with a 3 dB crossfade between neighbours so a hit on the
  boundary is not a lottery; round-robin alternates within a layer so a roll does not machine-gun.
- **Tuning to the drum**: the listen measures the drum's `fundamentalHz`, the bank's sidecar records the
  sample's, and the sample plays at the rate that puts them together, clamped to five semitones either way
  (cubic-interpolated varispeed, cheap; no pitch-shifter). This is what makes toms usable: a tom sample a
  fourth away from the drum is worse than no sample. Phase 2.
- **Loading** is the app's job, off the audio thread: `app/native/SampleLibrary` decodes with JUCE, trims the
  onset, measures the fundamental with the existing analysis, and publishes a `SampleBank` (plain float
  buffers) to the engine through a wait-free pointer swap, the way a `MixParameters` snapshot is published.
  The audio thread never touches a file; the old bank is freed on the message thread after the swap. `src/`
  stays JUCE-free: the bank is a struct of vectors, filled by the app.

Voices: four per strip, oldest stolen with a 2 ms fade. A voice starts at the trigger's sample index, plays
to the end of its sample or until stolen, and is summed after the gate at the blend and confidence gain.

## Per-track blend controls

Plain words in Simple view and on the strip; the engineer's words once, in Advanced or a tooltip.

| Where | Control | What it does |
| --- | --- | --- |
| Mixer strip INSERTS, Inspector stage "Sample" | switch | On or off. BYPASS switches it off with the rest. |
| Strip and stage | BLEND 0 to 100 % | 0 = the microphone only, 100 = the sample only. Defaults from the profile: kick 40, snare 30, toms 50. |
| Stage | SOUND | Which sample: the profile's pick, the list, "Your own...". |
| Stage | SENSITIVITY | The threshold, with a live readout: a lamp on each hit found and a count of hits found / ignored over the last ten seconds. |
| Stage, Advanced | FEEL: follows the drummer / steady | Velocity on or off (engineer: dynamic / fixed). |
| Stage, Advanced | ALIGN, polarity | The sample's nudge and polarity (auto / normal / flip). |
| Stage, Advanced | PITCH | Varispeed in semitones; auto from the drum's fundamental once Phase 2 lands. |

Rules it inherits:

- **TUNE / RE-TUNE** propose the threshold, the band, the level and the polarity from the listen, and the
  profile's blend and sample; the same listen gives the same proposal. Tune explains WHAT then WHY: "Snare:
  sample blended at 30 %. The microphone hears the hi-hat 14 dB under the snare; the sample carries the
  crack the gate would have cut." A refusal carries its sentence ("No sample on Room: a room microphone is
  never replaced.").
- **LIVE SAFE** treats the switch and BLEND as Inspector edits: allowed, bounded to a 10 % step, with the
  sentence. Solo and monitoring are untouched; the solo bus hears the strip as the room does.
- **Track history** records the stage's changes like any chain change, so PUT BACK returns a blend.
- **The plug-ins** (Dine Drums) do not get the stage in the first version; the parameters exist in the
  table but the product does not use them, which is how an unused field already behaves.

## Cost

Latency:

| Design | Added latency | Reported |
| --- | --- | --- |
| Zero lookahead (proposed) | 0 samples on every strip; the sample starts 0.2 to 1.5 ms after the onset | `getLatencySamples` unchanged |
| 2 ms lookahead on every strip (not proposed) | 96 samples at 48 kHz, on the broadcast and the solo bus | reported constantly, like the limiter |

CPU, measured 2026-09-24 on this machine with the engine's own `Biquad` and `EnvelopeFollower` at
-O3, 48 kHz, 128-sample blocks (`scripts/trigger_bench.cpp`, compile line at its head; generation overhead subtracted):

| Part | µs per block |
| --- | --- |
| Detector: HPF + LPF, fast + slow follower, mask | 0.7 |
| One voice, cubic interpolation, varispeed | 0.15 |
| Four voices | 0.7 |
| One full channel chain today, for scale (`scripts/benchmark-baseline.txt`, 1 instance) | 8.4 |
| Six replaced strips, detector + four voices each, worst case | about 8 |

Six replaced strips are about 0.3 % of the 2.67 ms a 128-sample block has; at 32 samples the ratio is the
same. The benchmark gets a row of its own so the existing chain rows (and the 15 % CI fence) are not
disturbed by a stage that is off by default.

Memory: eight samples of 1.5 s at 48 kHz in float is 2.3 MB per drum, 14 MB for six; held in RAM, no disk
in the callback. Loading a bank is a message-thread decode of about 20 ms per drum.

## What it threatens, and the answer

- *The audio thread never allocates, locks or does I/O.* Banks are published by pointer swap; voices and
  trigger arrays are fixed-size; `tests/AllocationTracker` and RTSan cover the new stage like every other.
- *Tune is deterministic and idempotent.* Every threshold and level is computed from the capture, never from
  the current value; the existing re-tune test extends to a strip with the stage on.
- *Latency is reported honestly.* Nothing changes; the lookahead variant, if ever built, reports.
- *Numbers live in the profile data.* Bands, masks, blends, rise, the per-style sample: `ProfileData.cpp`.
- *Plain words.* The table above; "trigger", "velocity", "varispeed" appear once each, in Advanced.
- *A take survives a crash, and is the microphone.* Unchanged; the blend is a mix decision.
- *Reference renders.* The stage is bit-transparent when off; a new render with it on becomes its own
  reference.

## Phases

- **Phase 0, measure (small).** `dlive_trigger_check <take folder>`: runs the detector offline over the
  QUEENSVIEW kick, snare and tom takes with the overheads as the reference for "a real hit", prints hits
  found, bleed ignored, mis-triggers, double triggers and detection delay per drum for a grid of rise,
  threshold and band settings. The profile's numbers are written from this table. No engine change.
- **Phase 1, the stage (medium).** `src/DSP/SampleTrigger`, `src/DSP/SamplePlayer`, the option on
  `ChannelProcessor`, the parameters, the profile defaults, TUNE's proposal in the drum strategy
  (`TuneDecisions::move()`, changes recorded by diffing), `app/native/SampleLibrary` with the built-in bank,
  the Inspector stage and the strip chip, BLEND / SOUND / SENSITIVITY, tests (a synthetic kit with snare
  bleed 20 dB under the kick: every kick triggers, no bleed does, timing error under 1 ms, re-tune says NO
  CHANGE REQUIRED), the benchmark row, a reference render, snapshots.
- **Phase 2, the finish (medium).** Cross-strip veto, tuning to the drum's fundamental, user samples,
  ALIGN and polarity by hand, the round-robin bank.

## Decisions needed before Phase 1

1. The bank: record our own, or license one that allows redistribution.
2. DLIVE only first, or the Dine Drums plug-in in the same step (recommended: DLIVE only).
3. The word on the surface: "Sample" (proposed), "Reinforce", or "Trigger" (engineer's word, Advanced only).
4. Whether Phase 0's numbers are good enough to skip the lookahead switch for good.

## What was built (Phase 1, 2026-09-24)

Everything in the plan for Phase 1, with three changes the diagnostics forced:

- **The onset rule is a jump, not a ratio to a slow follower.** A slow follower with a fast enough attack
  chased the drum's own swell and blocked the second hit; a slow enough one let a decaying tail count as a
  rise. The detector now fires when the fast follower is above the threshold *and* has jumped by the
  profile's rise (kick 6 dB, snare 8, toms 6) within the last two milliseconds; after the mask it re-arms
  once the hit has fallen 6 dB from its peak. A ring, a tail and a roll's stroke are now told apart by the
  thing that differs: whether the level jumped. `src/DSP/SampleTrigger`.
- **The hit is reported 1.5 ms after it is recognised**, with the loudest level in that time as its velocity.
  At the crossing itself the level *is* the threshold, so velocity and confidence read from the crossing were
  always zero. Those 1.5 ms are the only delay a sample has; the microphone is never delayed. On the
  synthetic kick that rings for a quarter of a second, a hit is recognised 1 to 3.5 ms after its onset
  depending on how loud the previous tail still is, and reported 1.5 ms after that. Phase 0's measurement on
  real takes stands; the timing claims above (0.2 to 1.5 ms) were the estimate before the build.
- **The mask's reference is measured until the body has peaked** (up to 10 ms), so a kick's own swell never
  re-triggers it; a louder onset after that still does.

The rest is as planned: `SampleReplacer` between the gate and the corrective EQ on kick, snare and tom
strips only (`MixEngine` configures it by role family; a plug-in never has it; off it is bit-transparent
and the reference renders pass); thirteen `replace*` parameters appended to `ChannelParameters`, bounded in
`dspParameterSpecs()` so the safety validator and the history can name them; per-family numbers in
`ProfileData.cpp`; `tune::setSampleReplacement` fitting threshold, level, band, mask and rise from the
listen and never touching the switch, blend or sound; a `SampleBank` published by pointer through
`MixEngine::setSampleBanks`; `app/native/SampleLibrary` decoding the bank (the shipped one in `app/Samples`,
copied into the bundle; the engineer's own in `~/Music/DLIVE/Samples`); the SAMPLE stage on every list that
reads a chain and as a device in the Inspector; five engine tests, a tune test and an app test; a benchmark
section of its own. Measured (`livemix_benchmark`): six kick strips with the stage on cost about 7 µs more
per 128-sample block than with it off.

## Phase 2 and Phase 0 (the same day)

- **Follows the drum.** `measureFundamental` reads a hit's pitch (autocorrelation over 5 to 125 ms, the first
  clear peak between 35 and 500 Hz) for every loaded sound and for the placeholders; TUNE writes the drum's
  own fundamental into `replaceDrumHz`; with TUNING set to "Follows the drum" (the toms' default) the sample
  plays at `drumHz / the bank's pitch`, never more than five semitones either way, times the PITCH knob.
- **The cross-strip veto.** `KitTriggerTable` in `MixEngine`: each drum strip notes its last hit (time, and
  how far under the strip's own loudest recent hit it was; the loudest falls 1 dB a second so one accident
  fades). A tom hit more than 9 dB under the tom's own loudest, within two milliseconds of a kick or snare
  hit that was within 9 dB of theirs, is that drum through the air and plays nothing. Strips run in order,
  so a strip sees the hits of the strips before it in the same block and everything from the blocks
  before; the stage reads "N hits, M held" in the Inspector.
- **A hit readout.** The path chip's bar lights while a sample plays; the stage's sentence carries the count.
- **The kit tuned around the samples.** With a sample on, TUNE gates that microphone far harder (the sample carries
  the body; the microphone supplies the attack), and once any drum is sampled the hi-hat gets a gentle expander,
  the unsampled drum microphones close further, and the hat, overheads and room take their high-pass to the top
  of its range - what they hear of the sampled drums is the dirt in a clean kit (`docs/DLIVE-MIX-ENGINEER.md`).
- **Two defaults for a first service.** A sampled snare keeps a shallow expander (20 dB, 4:1, threshold low) so its
  ghost notes - which the sample never fires on - still come through the microphone; a sampled kick or tom closes
  hard. Toms play their sample as recorded until "Follows the drum" is switched on by ear, because the drum's pitch
  comes from a listen on a microphone that is mostly bleed.
- **HEAR IT.** A button on the stage plays the chosen sound once, at the level the stage would play it, into the
  engineer's listen (the monitor bus) and nowhere else; the broadcast never hears an audition, and with no solo
  output the app says so rather than playing it into the room.
- **Phase 0, on the QUEENSVIEW takes** (`build/app/dlive_trigger_check "<folder>" 60 <offset> _002`): the tool
  measures each kick, snare and tom take as a listen does, fits the stage as TUNE fits it, runs the detector
  as the engine runs it, and prints hits, a level histogram, gaps and coincidences. The first run said the
  kick and snare trigger cleanly (tight clusters, no soft hits) and the toms did not: the listen's "event
  level" on a tom microphone is mostly the rest of the kit, so the halfway-to-the-floor threshold sat in the
  bleed - the rack tom fired 141 times a minute for two real hits. Three rules changed from that table:

  | Rule | Before | After |
  | --- | --- | --- |
  | The sample's level | the event level (a 10 ms frame level, 12 dB under a kick's peak) | the microphone's musical peak |
  | A tom's threshold | halfway between floor and events when no bleed was separable | never more than 12 dB under the peak (kick and snare: 18) |
  | The veto's "soft" | 6 dB under the fitted level | 9 dB under the strip's own loudest recent hit |

  | Take, 60 s window | hits a minute before | after | kick / snare coincidences after |
  | --- | --- | --- | --- |
  | Kick, from 120 s / 300 s | 205 / 182 | 205 / 182 | - |
  | Snare | 53 / 44 | 53 / 44 | - |
  | Floor tom | 158 / 39 | 53 / 31 | 5 / 0 |
  | Rack tom 1 | 141 / 114 | 16 / 21 | 8 / 10 |
  | Rack tom 2 | 11 / 10 | 11 / 8 | 0 / 0 |

  Whether 16 rack-tom hits a minute are all real needs ears on the take; the histogram (most within 3 dB
  of the threshold) says some are still bleed, and the veto holds back the ones that coincide with a snare
  once the tom has been hit for real. Ground truth by hand-marking the takes is the next measurement.

Still open: velocity layers for the shipped bank (each sound is one recording; a folder of files is the way
to add layers), and hand-marked ground truth for the detection delay and the miss rate on real drums.
