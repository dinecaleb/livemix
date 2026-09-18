# DLIVE: from a plugin family to one standalone mix engine

Date: 2026-09-07. This is the audit and plan for the product pivot: DLIVE becomes a standalone live/broadcast
mixing application built on the existing engine. The plugins stay; they become thin hosts of the same core.

The product promise this document serves:

> Connect your inputs. Tell DLIVE what they are. Choose your sound. Play. DLIVE builds the mix.

---

## 1. What exists today (audit)

Repository: ~24.7k lines of C++20. Two layers, already separated:

| Layer | Target | JUCE | Size | State |
|---|---|---|---|---|
| Engine (`src/`) | `livemix_engine` | none | ~9k lines | 109 unit/integration tests pass; allocation-tracked |
| Plugins (`modules/`, `src/State`, `src/UI`) | `LiveMix<Product>` AU + Standalone | yes | ~6k lines | 7 AUs validate with `auval` |

Engine contents and their reuse status for DLIVE:

| Area | Files | Reusable as-is | Notes |
|---|---|---|---|
| Channel DSP | `src/DSP/*` (`ChannelProcessor` = filters, gate, corrective EQ, de-esser, comp, transient, tone EQ, saturation, width, trim, limiter, loudness meter) | Yes | Each strip is mono or stereo (`kMaxChannels = 2`). Zero latency except the master limiter (1.5 ms, reported). ~3.5 µs per strip per 64-sample block at 48 kHz: 64 strips ≈ 17 % of one core's budget. |
| FX DSP | `src/FX/*` (`FxChain` = reverb + delay, 14 types incl. Vocal Plate, Vocal Hall, Worship Hall, Room, Drum Room, Snare Plate, slap/quarter/eighth/dotted/stereo/ping-pong/throw, tempo sync, ducking) | Yes | Already the FX list the brief asks for. Wet-only use (`mix = 1`) turns a chain into a return. ~4.6 µs per instance per 64-sample block. |
| Analysis | `src/Analysis/AnalysisEngine` (wait-free FIFO + one worker: level, crest, hits, floor, transients, decay, fundamental, bands, third-octave, resonances, sibilance, correlation, LUFS, true peak) | Logic yes, threading no | One `std::thread` per instance; 64 inputs would mean 64 threads. The accumulator must be separated from the worker (see §4). |
| Tune | `src/Tune/*` (`TuneEngine`, `SourceStrategy` per family, `StrategyToolkit` rules, `TuneDecisions` diff recording) | Yes | Per-source decisions. Idempotent, bounded, explained. This is the "channel" half of the mix engine. |
| Profiles | `src/Profiles/ProfileData.cpp` (Modern Gospel + Worship deltas; targets and baselines for 24 families incl. Drum Bus, Vocal Bus, Keys Bus, Guitar Bus, Bass Bus, Master) | Yes | Bus families already exist, so bus processing needs data, not new DSP. `mixPeakTargetDb` per family is a first mix-balance model. |
| Kit rules | `src/Analysis/KitAnalysis` | Yes, generalise | Already reasons about kick/snare reference, close mics vs overheads, balance trims. This is the seed of the RelationshipEngine. |
| Safety | `src/Intelligence/SafetyValidator`, `src/State/ParameterSpecs` | Yes | Bounds defined once. Every proposed change goes through it. |
| Macros | `src/Profiles/MacroMapping` | Yes (Advanced) | Per-strip knobs; DLIVE's mix-level macros (VOCALS Warm/Bright, DRUMS Tight/Big, SPACE, ENERGY) are new and sit above these. |
| AI | `src/Intelligence/*Provider*`, `AnalyzeCoordinator` | Keep, unused | Switched off on purpose. Nothing here changes. |
| Kit communication | `src/Communication/*` | Not needed in the app | Solves inter-plugin discovery; in one process the mix engine simply owns every strip. Keep for the plugins. |
| Plugin host | `modules/Common/ChannelPluginProcessor` | No | Tune preview flow (BEFORE/AFTER/KEEP/REVERT), state (ValueTree), parameter path (APVTS + `ParameterBridge`), `kTuneSeconds = 12`. The flow is worth copying; the code is JUCE-plugin specific. |
| UI | `src/UI/*` (DINE design system, Simple/Advanced panels, Tune overlay, snapshot tool) | Partly | Look-and-feel, widgets, fonts, EQ curve, meters, Advanced panel are reusable for the app's Advanced drill-down. The Simple panel is per-strip; the app's top level is new. |
| Offline check | `tools/TuneStems.cpp` | Pattern yes | Runs the engine on recorded stems with no plugin host. The mix-level version of this tool is the vertical slice's success test. |

Real stems for listening: `/Users/calebwork/Downloads/stems recording` (16 input channels at 48 kHz: kick, snare, two
toms, stereo drum pair, room, bass, stereo keys, lead, four backing vocals, pastor).

## 2. Plugin-specific coupling (what stops the engine being an app today)

1. **Orchestration lives in the plugin.** Capture -> `AnalyzeCoordinator::run` -> preview/keep/revert is in
   `ChannelPluginProcessor`. The engine has no object that owns "a mix".
2. **One analysis thread per source.** `AnalysisEngine` couples accumulation to its own worker.
3. **Parameters reach the DSP through host parameters.** `ParameterBridge` reads APVTS atomics each block. The app
   needs its own message-thread -> audio-thread path (a published snapshot, no host).
4. **`TuneContext` is one source.** No knowledge of what else is in the mix, no faders, sends or buses.
5. **No summing, no routing, no returns, no master chain as a graph.** Each plugin is one strip.
6. **Persistence is a ValueTree per plugin.** The app needs a session (assignments, plan, macros) of its own.
7. **`kTuneSeconds = 12` and the trigger logic** are plugin constants; the mix capture wants 30-60 s and one shared
   trigger ("the band started").

None of these require touching the DSP, the strategies, the profile data or the validator.

## 3. Minimum architectural change for DineCore

DineCore already exists: it is `src/` (`livemix_engine`). It needs three additions and one split. Nothing moves.

```
src/
  Analysis/   AnalysisAccumulator (split out of AnalysisEngine; pure, single-thread, no FIFO)  <- split
              AnalysisEngine      (unchanged API; FIFO + worker + trigger, uses the accumulator)
  Mix/        MixSession          input assignments (name, role, mono/stereo input indices)      <- new
              RoutingGraph        assignments -> strips, buses, FX slots, sends (deterministic)
              MixParameters       fixed-capacity snapshot of everything the audio thread needs
              MixEngine           real-time graph: strips -> buses -> FX returns -> master
              MixCapture          one worker, N FIFOs, N accumulators (Tune Mix listening)
              MixPlanner          per-strip Tune + relationships + balance + FX + master -> MixPlan
              Relationships       kick<->bass, lead<->BGV, lead<->keys, toms<->overheads, vocals<->FX
  Profiles/   MixProfileData      numbers for routing, sends, balance, relationships (Modern Gospel)  <- new
  Core/       TripleBuffer        message thread -> audio thread publication without locks           <- new
```

Existing rules keep holding: strategies edit `ChannelParameters` inside `TuneDecisions::move()`, numbers live in
profile data, everything proposed passes `SafetyValidator`, re-tuning the same capture changes nothing.

## 4. Standalone audio host architecture

```
CoreAudio device (Dante Virtual Soundcard, USB console, interface)   via juce::AudioDeviceManager
        │  N input channels, 32-128 samples, 44.1-96 kHz
        ▼
app/native  AudioHost (juce::AudioIODeviceCallback)             <- the only JUCE audio code
        │  builds AudioBlockView per strip from device channels (mono or stereo-linked)
        ▼
src/Mix     MixEngine::process()                                 <- pure C++, allocation-free
              strips (ChannelProcessor xN)  -> fader/pan -> bus accumulators + FX send accumulators
              buses  (ChannelProcessor, stereo): DRUMS, BASS, MUSIC, VOCALS
              FX     (FxChain wet-only): Vocal Plate, Vocal Delay, BGV Hall, Snare Plate, Drum Room
              master (ChannelProcessor with limiter + loudness meter)
              taps: MixCapture FIFOs (raw inputs, buses, master) while listening; meters (atomics) always
        │  stereo
        ▼
device outputs 1-2 (or any pair)  -> OBS / Ecamm / recorder      (recording engine later: raw + stems + master)
```

Control path: the app (message thread) edits a `MixParameters` snapshot and publishes it through a `TripleBuffer`.
The audio thread checks the version once per block and, only when it changed, copies the snapshot and pushes it
into the strips (`ChannelProcessor::setParameters` is already audio-thread safe). Meters and capture state come back
through atomics and the FIFOs. No locks, no allocation, no strings on the audio thread. Whatever happens to the UI,
the callback keeps running on the last published snapshot.

Sizing: 64 strips + 4 buses + 5 FX + master ≈ 64×3.5 + 5×3.5 + 5×4.6 + 4 ≈ 270 µs per 64-sample block, about
20 % of the 1333 µs budget at 48 kHz on one core. No parallel DSP is needed for V1. Memory: MixParameters with 64
strips ≈ 30 KB; capture FIFOs at 16384 frames × 2 ch × 70 streams ≈ 9 MB, preallocated.

## 5. UI: JUCE-native vs React + native bridge

Decision for the vertical slice: **JUCE-native UI, behind a UI-agnostic control boundary.**

Why:
- The DINE design system already exists in JUCE (look-and-feel, fonts, widgets, EQ curve, meters, Advanced panel,
  headless PNG snapshot tool). The app's Advanced drill-down is this code.
- The reliability goal ("UI failure must not stop audio") is met by architecture, not toolkit: the audio callback
  depends only on the published snapshot and preallocated state. A hung message thread stalls meters, not audio.
- A web UI adds a second language, a bundler, a bridge protocol and a WebView process to a live-production app for
  no gain until the product's top-level screens are settled.

What keeps the door open: the app talks to the engine only through `MixController` (commands in, `MixSnapshot`
out: assignments, plan, health, macro values, per-strip state, meter frames). That boundary is plain data and can
be serialised to JSON. If a React front end is wanted later, JUCE 8's `WebBrowserComponent` (WKWebView on macOS,
native functions + events, no Electron) is the bridge, and the spike to validate it is bounded: 64-strip meters at
30 Hz, command round trip under 50 ms, memory, and behaviour when the page crashes. That spike is scheduled after
the slice, not before it.

## 6. Technical risks

| Risk | Mitigation |
|---|---|
| Multi-stream Tune windows: the band may not start together; a 30 s window per input wastes silence | One shared trigger ("any input above -45 dBFS") starts all captures; per-strip silence is already measured and lowers confidence |
| Relationship rules fighting per-channel rules (idempotence) | Relationships run after strip Tune, edit through `TuneDecisions::move`, compute from profile templates, and the whole plan is re-run on the same capture in tests (must produce NO CHANGE) |
| Balance needs processed levels, but processing is what Tune decides | Two-pass listen: raw analysis while the baseline chain runs, then faders fitted from how loud each source is while it plays (`mixLevelTargetDb`, an active-RMS target); the offline tool renders a second pass to verify |
| `mixPeakTargetDb` was written for one plugin at a time, not a full mix | Mix-level balance targets move to `MixProfileData` (relative to the lead vocal); the per-family values become the fallback |
| Device channel counts change (Dante VSC 32/64, USB 18) | Assignments are stored by input index and name; missing inputs are flagged, never silently reassigned |
| Sample-rate / buffer changes mid-session | `prepare()` on the message thread with audio stopped; every module already supports re-prepare |
| Recording on the audio thread | Later milestone; design is a FIFO per stream and a writer thread, like analysis |
| Drum reinforcement | There is no existing code; it is a planned engine (`ReinforcementEngine`) after the slice, with a slot reserved in the strip chain |

## 7. Vertical-slice milestone (Milestone 7)

Goal: load the 16-channel church multitrack, assign channels, choose Modern Gospel, press TUNE MIX, listen.

Steps, each keeping every existing test and plugin build green:

1. **Engine: `MixSession`, `RoutingGraph`, `MixParameters`, `MixEngine`** with tests (routing is deterministic
   from roles; the graph sums correctly; allocation-free processing; latency reported; 64 strips within budget).
2. **Engine: `AnalysisAccumulator` split + `MixCapture`** (one worker thread, N FIFOs, shared trigger).
3. **Engine: `MixProfileData` + `MixPlanner` + relationships**: per-strip Tune, bus baselines, FX sends, balance,
   kick<->bass, lead<->BGV, lead<->keys, toms<->overheads; idempotence test on synthetic and real stems.
4. **Tool: `dlive_mix_stems`**: renders BEFORE (raw sum) and AFTER (Tune Mix) of the church stems to WAV and
   prints the plan. This is the success test: listen.
5. **App: `app/`** JUCE standalone: device page, assignment page, purpose + sound, TUNE MIX overlay, mix overview
   with five macros and BEFORE/AFTER, Advanced drill-down reusing `AdvancedPanel`. Session saved as versioned JSON.
6. **Then**: recording engine, House Sound, SQLite when there is more than one table to keep, reinforcement.

## 8. What this pivot does not do

No AI, no cloud, no accounts, no automatic source detection, no console control, no new sonic profiles, no
renaming of released parameter IDs, no change to the plugins' sound.

---

## 9. Status after the first implementation pass (2026-09-07)

Done, all tests green (133 engine cases, 7 plugin suites, plugins unchanged in sound):

| Piece | Where | Notes |
|---|---|---|
| Mix graph | `src/Mix/MixSession.h`, `RoutingGraph`, `MixParameters.h`, `MixEngine` | 64 strips + 4 buses + 5 returns + master; ~165 us per 64-sample block for 64 strips (12 % of budget); allocation-free; parameters published whole through `Core/TripleBuffer.h` |
| Multi-input listen | `src/Analysis/AnalysisAccumulator` (split out of `AnalysisEngine`, whose API is unchanged), `src/Mix/MixCapture` (one worker, one FIFO per stream, shared trigger), `src/Mix/OfflineCapture` (synchronous, for tools and tests) | Captures every strip raw and processed, every bus input and the master output |
| Mix planner | `src/Mix/MixPlanner`, numbers in `src/Profiles/MixProfileData.cpp` | Per-strip Tune (unchanged `TuneEngine`) -> digital input gain (the preamp recommendation, applied where DLIVE owns the input) -> relationships (kick/bass sub, lead/music pocket, lead/backing group, toms/overheads, room mics/drum room) -> faders from processed loudness while playing (peak only as a headroom guard) -> lead-as-reference offset -> buses and master (loudness closed-loop from the measured master output). Idempotent on the same listen (tested on synthetic and real stems) |
| Mix macros | `src/Mix/MixMacros` | VOCALS Warm/Bright, DRUMS Tight/Big, BASS Clean/Huge, SPACE, ENERGY; 50 = the plan; bounded, tested |
| Offline success test | `app/Tools/MixStems.cpp` -> `build/app/dlive_mix_stems <stems folder>` | Renders raw / before / after / after-retuned WAVs from a stems folder, prints every decision, proves idempotence. On the church multitrack the re-tuned mix lands within 1 LU of the -23 LUFS broadcast target |
| Standalone app | `app/` -> `build/app/DLive_artefacts/Release/DLIVE.app` | Device -> Assign -> Purpose/Sound -> Mix (health, groups, TUNE MIX, macros, BEFORE/AFTER, KEEP/REVERT) -> Advanced (strips, buses, gains, faders, sends, every WHAT/WHY). `MixController` (no JUCE) owns the mix; `AudioHost` owns the device; JSON session in ~/Library/Application Support/DLIVE |
| UI verification | `build/app/dlive_ui_snapshots <dir>` | Headless PNGs of every page and state, driven without a device |
| App tests | `build/app/dlive_app_tests` | Controller state machine (listen -> preview -> keep / revert / abort / no signal), macros on top of the kept mix, Advanced edits, JSON session round trip |
| Rate / buffer matrix | `MixEngineMatrixTests` in `build/tests/livemix_tests` | 44.1 / 48 / 88.2 / 96 kHz x 32 / 64 / 128 / 256 samples: finite, level-sane, 5-12 % of the budget for 16 processed strips; the planner reaches the same decisions at 44.1 and 96 kHz |
| Recording playback | `app/native/MultitrackSource`, "PLAY A RECORDING..." on the device page | A folder of stems (AIFF / WAV / FLAC / CAF) becomes the inputs: streamed from disk on a thread, looped at the longest file, names and sources guessed from the file names (`native/StemNames.h`, shared with the stems tool). The band, or anyone with a multitrack, walks through the real app and hears the mix from the chosen output. `dlive_device_check 4 "<stems folder>"` exercises it: 14 strips, 0 dropouts, ~280 us peak per 128-sample block |
| Real device | `build/app/dlive_device_check [seconds] [input] [output] [buffer]` | Opens a CoreAudio device through `AudioHost`, runs the callback, reports blocks / cost / dropouts, reconfigures while running. On this Mac: 48 kHz, 64 samples, 0 dropouts, ~40 us peak per block |

Mix rules added from listening to the church report: a speech microphone that is "heard" while the lead vocal sings is
spill and is left alone (Tune Mix again while the pastor speaks); a stereo drum mix from the console is a source of its
own ("Drum mix", the Drum Bus family) rather than a pair of overheads.

Two latent rules in the existing strategies were fixed on the way, because Tune Mix re-plans exposed them: every
"raise the high-pass" rule multiplied the *current* frequency (it walked on re-tune) and the transient "sharpen"
rule added to the current amount. Both now compute from the profile template, as the project rule says.

### Second pass (2026-09-07, later): the first TUNE MIX lands

Listening to the church stems showed the first plan landing at -27.2 LUFS and a second listen moving faders by up to
8 dB: the faders were fitted from peaks measured under the *baseline* chain, the digital input gain was limited to one
"human preamp step" (+10 dB), and the master trim did not know the compressor the plan was about to choose. Now:

- **Input gain in one move.** `tune::captureGainToHealthyDb` is the unbounded move into the profile's healthy capture
  range; the per-source rule still limits a person to one step, Tune Mix applies the whole move digitally (bounded by
  `maxInputGainDb` and the chain-input ceiling).
- **Predicted processed levels.** `MixPlanner::predictedProcessedPeakDb` = measured processed peak + gain change +
  (compressor curve under the proposed chain - under the chain that ran), where the compressor has reached
  `1 - exp(-rise / attack)` of its static reduction by the time the source's peak arrives (`compPeakRise*Ms` per family;
  close drum mics let nearly the whole hit through, voices see ~60 %). `predictedProcessedRmsDb` does the same for the
  average level with the reduction taken at a level between RMS and peak (`compDetectorCrestShare*`), and
  `predictedProcessedActiveRmsDb` lifts that by the source's own active-to-overall offset to give the loudness the
  balance is fitted to. Buses are planned
  from the predicted RMS of their strips; the master from the predicted bus outputs, and it is tuned twice so its trim
  accounts for the compressor it chose. On the church stems the first pass lands at -23.8 LUFS
  (`stems recording`) and -22.9 LUFS (`caleb`), both inside 1 LU of the target, and the re-tune says "Loudness on
  target"; per-strip prediction errors are within about 1 dB, two backing vocals within 2.6 dB.
  `dlive_mix_stems` prints the prediction check.
- **Tempo and the effects.** A live console has no host play head, so a tempo-synced delay would run at the engine's
  default forever. `AnalysisAccumulator` autocorrelates the onset envelope of every stream (`tempoBpm`,
  `tempoConfidence`); `MixPlanner` takes the consensus of the sources that actually play a rhythm (a minimum onset
  density gates the vote - a stage has more sustained sources than drums, and letting them vote buries the kick) and
  publishes it as `MixParameters::tempoBpm`. `MixEngine` hands it to every return, and the same tempo fits the reverb
  tails through `MixProfile::reverbBeats`, bounded by each effect's own decay so the character survives. Measured
  100 BPM (`caleb`, true ~100) and 126 BPM (`stems recording`, true ~125).
- **Faint inputs.** A raw peak that never got above `faintInputDb` (-38 dBFS at the device) is a mic that is off, a
  bad cable or a player who sat out: nothing is tuned, raised or balanced and the plan says "check this input" (the
  church Kick and Rack Tom). Previously they got +20 dB of gain.
- **App.** The session is saved a second after KEEP / REVERT / a macro or Advanced move (not only on quit); a saved mix
  survives a launch without its device and is applied when the same inputs are prepared; a device that stops on its own
  is announced once; Mix Health is the share of inputs heard, not faint and at a healthy level, with the reasons in
  the status line; the HUD toast sits at the foot of the content.
- **Look (v2, 2026-09).** The app follows the "DLIVE v2" design (Claude Design project `DLIVE v2.dc.html`):
  native macOS materials (desk #101113, window #1b1c1e, card #232528, vibrancy sidebar), half-pixel hairlines,
  5-11 px radii, 24-26 px controls, the system face at Mac sizes with capitals kept for the product verbs only
  (TUNE MIX / RE-TUNE / KEEP / REVERT / BEFORE / AFTER), one accent (#4db8a4) and three semantic colours
  (heard #4cc98a, faint #f0a33f, muted #e5645e). The tokens, the icon set, the buttons, the source list, the switch,
  the meter and the look-and-feel live in `app/ui/AppTheme.{h,cpp}` (`Dine::`, `DineLookAndFeel`); the plug-in keeps
  its own `Tokens` in `src/UI`. The shell is sidebar (Library / Set up / Mix + the device's state) + unified toolbar
  (setup name and its menu, Mix | Advanced, output) + one page; TUNE MIX and its result arrive as sheets from under
  the toolbar, and the mix page carries an input rail with a meter per input.
- **Themes (2026-09-17).** Every `Dine::` colour is a token a theme may set; View > Appearance picks one for
  the whole app and remembers it on this Mac, and the Appearance sheet edits, saves, imports and exports one.
  The document, the built-ins and the folder are `app/native/ThemeStore` (JUCE-core, tested); the tokens'
  rebinding, `Dine::applyTheme` and `Dine::refreshAllWindows` are in `AppTheme`; the sheet is
  `app/ui/ThemeSheet`. `docs/THEMES.md` has the file format, the built-in list and the rules for new widgets.

Known limits of the prediction: it is a model, fitted to one recording (numbers in `MixProfileData.cpp`); EQ ahead of
the compressor, transient shaping and saturation are not modelled. The exact answer would be to keep the raw listen
and render it through the proposed chain inside the planner (memory: ~6 MB per mono input per 30 s); that is the
next step if more recordings show the model drifting.

Not done (next): recording engine, House Sound, per-parameter Advanced editing (the strip's EQ/comp tiles from the
plugins), reinforcement, SQLite (JSON is enough for one session document), the React bridge spike, real-device
soak tests at 44.1/88.2/96 kHz and 32/256-sample buffers, a mix-level check that strip, bus and master "harshness"
cuts do not stack (the after mix loses ~4 dB of upper-mids against the raw sum), a per-family high-pass floor for
vocals (the lead's 82 Hz "fundamental" is bleed).

---

## 10. The DAW milestone (2026-09-08)

The brief for DLIVE changed from "a standalone mixer" to **the live recording and broadcast DAW**. The mix
layer above is unchanged; one object was added between the device and it. Full report: `docs/MILESTONE-7.md`.

```
CoreAudio device
      │  N input channels
      ▼
app/native  AudioHost                                    <- the only JUCE audio-device code
      │
      ▼
app/native  DawEngine::processBlock()                    <- no allocation, no locks, no files
      │  1. Recorder::write   raw device inputs -> lock-free FIFO -> writer thread -> WAV per armed track
      │  2. TimelinePlayer::read   ring filled ahead by a reader thread from ClipSource
      │  3. the input matrix: every device channel, with playback swapped in per track
      │     (monitorUsesLiveInput: Off / Input / Auto — one rule, in Project.h)
      ▼
src/Mix     MixEngine::process()                         <- exactly as before
      ▼
stereo output  -> OBS / Ecamm / recorder
```

Consequences worth remembering:

- **TUNE MIX did not change.** It listens to whatever the matrix carries, so it works identically on live inputs
  and on a recorded service played back from the timeline (§42 of the brief).
- **The raw recording is the raw input** (§41). Processing, faders and the master never touch what is written.
- **The loop wraps to the sample** in both `Transport::advance` and the player's fill, so the playhead and what is
  heard cannot drift apart.
- **A session is a folder**: `~/Music/DLIVE/<name>/<name>.dlive.json` with `Audio Files/` beside it.
  `SessionStore` is version 2; version 1 documents open with an empty timeline.
- **There is one playback path.** `MultitrackSource` (a stems folder streamed as fake device inputs) is gone;
  `MultitrackImport` turns a folder into tracks and clips, which record, edit, save and export like anything else.
- **LIVE SAFE** (§36) locks TUNE MIX, imports, new sessions and timeline edits; mutes, faders and the transport
  stay available, because an operator must always be able to act.

Still open after this milestone: fades and crossfades, comping, moving clips between tracks, markers drawn in the
ruler, count-in and metronome, processed-stem and stereo-master recording alongside the raw inputs, House Sound,
drum reinforcement, third-party plugin hosting, and the React bridge spike.
