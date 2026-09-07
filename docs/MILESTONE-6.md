# Milestone 6 report: Dine Guitar and Dine Bass

Date: 2026-09-07. Builds on `MILESTONE-5.md`. Two more channel products from Dine Core with no new DSP: the work is
sources, profile numbers, Tune rules and wording. Dine Bass is the first of the two validated on a real church stem.
Machine: Apple Silicon, macOS 26.4, JUCE 8.0.8, CMake 4.4.3, Release builds.

## 1. What was built

- **Dine Guitar** (`modules/Guitar`, AU `aufx Lmgt Lvmx`, bundle `com.dine.guitar`, target `LiveMixGuitar`): a thin
  `ChannelPluginProcessor (Product::Guitar)` plus one `dine_add_channel_product` call, so it gets the AU + Standalone, the
  headless plugin tests and the UI snapshot tool for free.
- **Sources.** `ChannelRole` grew from 27 to 31 (every released index untouched): Acoustic Guitar, Electric Clean,
  Electric Drive, Guitar Bus. Three new families (AcousticGuitar, ElectricGuitar, GuitarBus), each with Modern Gospel
  targets + baseline and Modern Worship deltas (a touch more air on the acoustic, cleaner electrics). Clean / Drive is a
  role refinement on the ElectricGuitar family: Clean has no template low-pass or drive and more air; Drive has a lower
  crest range and lighter compression.
- **Chain**: Input → CLEAN-UP (expander) → TONE (corrective + tone EQ) → LEVEL (compressor) → WIDTH (mid/side, mono below)
  → Output. No transient shaper, de-esser or limiter ids in the table (`usesStage (Product::Guitar)`). Width is there for
  stereo modelers; on a mono channel Tune leaves it at 1.0.
- **Simple knobs**: WARMTH (body shelf + low-mid fullness), CLARITY (pick definition + sparkle), SMOOTH (deeper cut on the
  quack / fizz band, a touch less top; below 50 the template cut fades out for more bite), STEADY (compression),
  CLEAN-UP (high-pass + expander; 0 switches the expander off; never an expander on the bus).
- **Tune** (`src/Tune/GuitarStrategies.cpp`): the shared toolkit rules in the order a guitar engineer works (high-pass for
  boom / amp low end, body, low-mid mud, a notch for the acoustic body's boom or a boxy cab resonance, pick definition,
  harshness = quack / fizz, air, compression, width, expander) plus one rule of its own, `rollOffFizz`: when brilliance and
  air sit more than 3 dB above the profile tolerance on an electric, the low-pass is fitted from the profile template
  (6–12 kHz, never from the current value) so re-tuning the same capture lands on the same frequency; when the top end is
  already under target an existing roll-off is opened. Continuous strumming never gets an expander; the bus is never gated.
- **Wording**: source hints ("Bright and natural · boom controlled · strums stay even", "Full amp tone · fizz tamed ·
  level held"), player prompt "have the guitarist play normally", events are "notes".

## 1b. Dine Bass

- **Dine Bass** (`modules/Bass`, AU `aufx Lmba Lvmx`, bundle `com.dine.bass`, target `LiveMixBass`), same thin shape.
- **Sources.** Bass DI, Bass Amp (ElectricBass family, Amp = role refinement: 5 kHz low-pass, deeper mud cut, more drive,
  darker targets), Synth Bass (sustained, never gated, sub kept), Bass Bus (glue only). `ChannelRole` is now 35 sources in
  24 families.
- **Chain**: Input → CLEAN-UP (expander) → TONE (EQ + drive) → LEVEL (compressor) → Output. No width (bass is mono), no
  transient shaper, de-esser or limiter ids.
- **Simple knobs**: WARMTH (80 Hz shelf + 180 Hz body), CLARITY (1.5 kHz definition + 4 kHz finger top), GRIT (drive
  around the template; 0 switches it off; new `grit` macro id, forbidden for Tune and AI), STEADY (compression, bass takes
  more than most sources), CLEAN-UP (a small high-pass move, at most +20 % so the low E is never touched, plus an expander
  on the electric families only).
- **Tune** (`src/Tune/BassStrategies.cpp`): `protectTheFundamental` uses the analysis fundamental (30–130 Hz): rumble under
  the lowest note raises the high-pass but never above 0.8 x the note, and a high-pass that sits above the note is lowered;
  then body, mud, a boxy-note notch (200–600 Hz), string definition (1.5 kHz), fret clank (2–5 kHz), `addGrit` (bounded
  saturation when the crest factor is well above the profile), compression, and an expander whose detector follows
  0.7 x the note. Synth bass and the bus are never gated.

## 2. Numbers (Modern Gospel, `src/Profiles/ProfileData.cpp`)

| Family | High-pass range | Mud / harshness regions | Compression | Clean-up | Width |
|---|---|---|---|---|---|
| Acoustic | 70–140 Hz (template 90) | 250 Hz boom, 2–4.5 kHz quack | 2–4:1, 10–30 ms, ~3 dB GR | expander ≤ 18 dB, threshold 0.4 | 0.8–1.2, mono below 120 Hz |
| Electric | 60–120 Hz (template 80) | 350 Hz mud, 2.5–5 kHz fizz, LPF 9 kHz (Drive) | 2–4:1, 15–40 ms, ~3 dB GR (Drive: ~2 dB, ≤ 3:1) | expander ≤ 30 dB, threshold 0.35 | 0.8–1.3, mono below 150 Hz |
| Guitar Bus | 50–90 Hz (template 60) | cuts ≤ 3 dB | 1.5–2.5:1, 20–40 ms, ~2 dB GR | none | 0.8–1.2, mono below 120 Hz |
| Electric Bass | 25–45 Hz (template 30), capped at 0.8 x the note | 300 Hz mud, 2–5 kHz clank, drive ≤ 0.35 | 3–6:1, 10–40 ms, ~4 dB GR | expander ≤ 20 dB, detector ≥ 30 Hz | none (mono) |
| Synth Bass | 20–35 Hz (template 25) | 300 Hz, drive ≤ 0.15 | 2–4:1, 15–40 ms, ~3 dB GR | none | none |
| Bass Bus | 22–35 Hz (template 25) | cuts ≤ 3 dB, no drive | 1.5–2.5:1, 20–40 ms, ~2 dB GR | none | none |

## 3. Verification

| Check | Result |
|---|---|
| `build-engine/tests/livemix_tests` | 107 cases, 0 failures (new: guitar Tune rules, idempotency for the four guitar sources, guitar macros, product/profile coverage) |
| `build/modules/Guitar/livemix_guitar_plugin_tests` | 0 failures; `processBlock` 0 allocations / 2000 blocks, 3.3 µs per 64-sample stereo block; 32 instances 7.8 % of budget; Tune on the strummed test signal: input Healthy, ACOUSTIC GUITAR TUNED |
| `build/modules/Guitar/livemix_guitar_ui_snapshots` | every source, stage and Tune state rendered; GUITAR badge, plain knobs, CLEAN-UP / TONE / LEVEL / WIDTH chain, Tune card in sentences |
| `auval -v aufx Lmgt Lvmx` | AU VALIDATION SUCCEEDED (the two skewed-range default-value warnings the other products also show) |
| `build/modules/Bass/livemix_bass_plugin_tests` | 0 failures; 0 allocations / 2000 blocks, 4.1 µs per block; 32 instances 5.6 % of budget; Tune on the plucked test line: input Healthy, BASS DI TUNED |
| `build/modules/Bass/livemix_bass_ui_snapshots` | every source, stage and Tune state rendered; BASS badge, WARMTH / CLARITY / GRIT / STEADY / CLEAN-UP, CLEAN-UP / TONE / LEVEL chain with "sat" on the output tile |
| `auval -v aufx Lmba Lvmx` | AU VALIDATION SUCCEEDED (same two skewed-range warnings) |
| Every other product's plugin tests | see the end of this file (rebuilt after the enum changes) |

## 3b. What the church bass stem taught (`Bass#10.aif`, DI, 10 minutes)

Measured on three 20 s windows: peaks −21 to −24 dBFS, crest 11–13 dB, fundamental 87–118 Hz, notes ringing 700–1100 ms
with the level between notes only 9–13 dB under them, bands re total Low 0 / Low-Mid −10 to −13 / Mid −36 to −39 /
Presence −41 to −45 / Brilliance −61 to −68. Two things changed because of it:

- **The expander was wrong for a ringing bass.** The shared rule saw "floor 13 dB under the notes, bleed 0.9" and enabled
  a 20 dB expander that would have chopped every sustained note. `BassStrategies::cleanTheFloor` now treats a floor closer
  than 24 dB, or notes ringing longer than 600 ms, as the instrument itself: no expander, and one left on is bypassed with
  the reason in plain words. Only a real quiet floor (hum well under the playing) reaches the shared expander rule.
- **The first band targets were far too bright for a DI** (they asked for +13 dB of presence). They were re-centred on the
  stem, a few dB brighter than raw, so a dark DI gets a bounded definition boost and a normal one is left alone.

After that, every window tunes to a short, sensible list and re-tunes to NO CHANGE REQUIRED: preamp +4 to +7 dB (the
capture is quiet), body shelf moved just above the measured note (125 / 135 Hz), compression re-fitted to 4:1 at −34 dB,
"+2.5 dB definition at 1.5 kHz" on the darker windows only, no clean-up. Bass Amp on the same stem gives the same list
with the darker amp targets.

## 4. What changed in shared code

- `ChannelRole` / `RoleFamily` / `Product` gained entries at the end; `productOf`, `isBusFamily`, `roleHint`,
  `channelParameterSpecs`, `productDefinition`, `MacroMapping::apply / affectedParameterIds` and `strategyFor` gained one
  case each. No released index, id or default moved (the Drums / Vocals / Keys / Master tables are byte-for-byte the same).
- `ProfileDefinition` arrays are sized by `RoleFamily::Count`, so the three new families were filled for Modern Gospel and
  the Worship deltas; `ProductTuneTests` covers them in every profile (targets present, baseline inside the parameter
  bounds, hidden stages off, idempotent Tune, macros bounded and centre-neutral).
- Test signals: `ChannelPluginTests` and `ChannelUISnapshots` synthesize a strummed E chord with a pick click for Guitar.

## 5. Known weaknesses

- The guitar numbers are engineering starting points validated on synthetic signals only: the church multitracks in
  `~/Downloads/stems recording` contain no guitar stem. A listening pass with a DI acoustic and a miked / modeled electric
  in Logic is the next step, and `livemix_tune_stems "Acoustic Guitar" <file>` prints every decision offline.
- The bass numbers were checked against one DI stem (one player, one instrument). Bass Amp and Synth Bass targets are
  derived from it, not measured; the GRIT knob and `addGrit` want a listening pass on a real PA / phone speaker.
- `rollOffFizz` is a filter rule, not a dynamic one: a "fizz only on the loud chords" problem would need a dynamic EQ.
- Sax remains a placeholder; FX Tune is still not written.

## 6. Cross-product check after the enum changes

Full `cmake --build build` (every product, Release): Drums plugin tests, Drums kit/AI-parsing tests, Vocals, Keys,
Master and FX plugin tests all PASSED with 0 failures; `livemix_tests` 107 cases, 0 failed.

## 7. Fix: INPUT LOW shown while the host meter was hot

Reported in Logic on every product: the health chip said INPUT LOW while Logic's channel meter sat near the top.
Cause: `LevelMeter` published the peak of the *last audio block* and the editor sampled it on a 60 Hz timer. At a 64-sample
buffer that is one block in twelve; a pluck or a hit that peaks inside one block was usually never seen, so the meter,
the 2 s hold, the "recent" window and the chip all read many dB too low on transient sources. Tune itself was not
affected (it analyses the raw capture directly).
Fix: the meter now also max-accumulates a peak since the last read (lock-free compare-exchange on the audio thread) and
the editors read it with `consumeMaxPeakDb()`; `MeterTests` covers a one-block hit followed by eleven quiet blocks.
Both channel products and Dine FX use the new reader.
