# Dine (repo name LiveMix / Calive) — notes for Claude Code

- Build: `export PATH="$HOME/.local/bin:$PATH"` (cmake/ninja from `uv tool`), then `scripts/build.sh`.
  The app on its own (the fast loop, no plug-ins): `scripts/dlive.sh` builds DLIVE and opens it;
  `--build` builds only, `--tests` runs the app + engine tests, `--shots [dir]` renders the UI snapshots,
  `--debug` uses `build-debug`.
  A build to hand someone else: `scripts/package.sh` (`--universal` for Intel too, `--no-build`, `--out <dir>`) writes
  `dist/DLIVE-<version>-<date>.zip` + `dist/NOTES.txt` — send both; the signature is ad-hoc, so the tester opens it
  once by right-click > Open. Every build/run/share command, and what to tell the tester: `BUILD-RUN-SHARE.md`.
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
- **TUNE LIVE MIX** (the AI Mix Engineer, 2026-09-12; see `docs/ARCHITECTURE-DLIVE-AI.md`) is a reasoning layer
  **above** `MixPlanner`, never instead of it. The deterministic plan is built first and always, so a dead network,
  a timeout or a malformed reply leaves the user with a professional mix and a sentence. Everything is JUCE-free in
  `src/MixAI` (+ `src/Core/Json`): `RelationshipEngine` (measures, never decides - masking, hierarchy, low-end
  ownership, kit balance, what reaches the master), `MixContext` (schema v1: the session as a versioned serialisable
  document, with `MixCaptureAdequacy` refusing a listen that is not worth mixing from), `MixIntent` (schema v1:
  sonic outcomes with a signed strength, never parameters), `DspCapabilityRegistry` (generated from the real
  parameter tables and from how `MixEngine` configures each stage - the limiter is master-only because that is
  where the stage is turned on; an unavailable processor is reported **with its reason**, never silently missing),
  `CapabilityResolver` (intent -> `ProcessingPlan`, deterministic, as deltas on the deterministic plan; every action
  carries EXACT / APPROXIMATED / SUBSTITUTED / UNSUPPORTED - a spring reverb is built from the plate and says what
  it will not have, a gated reverb is refused), `MixSafetyValidator` (refuses rather than reinterprets: capture gain
  is the console's, a gate never goes on a sustained source, the master keeps its headroom; a refused EQ gain takes
  its band with it and every refusal keeps its reason for REVIEW CHANGES) and `TuneLiveCoordinator` (the whole
  state machine, one worker, polled). Numbers live in `MixProfile::aiRanges` / `aiBounds` (`MixProfileData.cpp`),
  versioned. `MixReasoningProvider` is the seam: `LocalMixReasoningProvider` is the default - deterministic,
  offline, reasons from the measured relationships, and is what every test runs against; `OpenAiMixProvider`
  (`app/native`) is opt-in, strict JSON schema, and sends the MixContext and the capability list and **never audio**.
  The result is an ordinary `MixPlan`, so BEFORE / AFTER, KEEP, REVERT, the mixer, the chain strips and the
  Inspector work on it unchanged - one source of truth. `MixController::startTuneLiveMix` drives it; the verify
  listen keeps the applied mix audible (`liveVerifying`); LIVE SAFE blocks it like any re-tune. The session stores
  the run's record under `tuneLive` next to the kept mix, so **reopening a session never contacts a provider**.
  Verify with `build-engine/tests/livemix_tests`, `build/app/dlive_app_tests` and the `08b` / `09b` UI snapshots.
- **REFERENCE MIX** ("make it sound like this") aims the mix at a finished recording. The engine half is
  JUCE-free in `src/Mix/ReferenceMix.{h,cpp}`: `ReferenceProfile` (schema v1 - the tonal balance, crest,
  loudness, correlation and tempo of a record, stored with the session so reopening never re-reads the file),
  `Reference::adequacy` (a four-second clip, a silence or an unreadable file is refused **with its reason**)
  and `Reference::targets`, which is the whole decision: it returns the master's `SourceTargets` aimed at the
  reference instead of at the profile's own. Nothing there writes a parameter - `MasterStrategy` does the work
  it always did - so every bound, every sentence and the idempotency rule come along unchanged. The seam is
  `TuneContext::targetsOverride` (null = the profile), set only by `MixPlanner` and only for the master:
  a reference is a finished stereo record, so the master is the only thing in a plan it can honestly be
  compared with. Band energy is measured relative to the whole, which is why a -9 LUFS master and a -23 LUFS
  live mix are directly comparable and why the plan's fader moves do not disturb the comparison.
  What a reference is **not** allowed to do is the other half of the feature and is reported, never silent:
  it never sets the delivery loudness (that belongs to the broadcast), never moves a source, a fader or a
  group (`MixPlanner` proves this in `ReferenceMixTests`), and never pulls one band target further than
  `MixProfile::referenceBounds` allows (3 dB), nor the master image or glue past their own bounds. Each
  refusal keeps its sentence in `MixPlan::reference` (`ReferenceMatch`), which is what the sheet and the plan
  notes read. The app half: `app/native/ReferenceAudio` decodes and measures the file (up to 4 minutes, on a
  thread of its own), `MixController::setReference` / `clearReference` / `startReferenceMatch` own it, and the
  listen is kept (`getLastListen`) so MATCH TO REFERENCE re-plans from what the band already played instead of
  asking them to play again - the result is an ordinary `MixPlan`, so BEFORE / AFTER, KEEP and REVERT are
  unchanged. The UI is `app/ui/ReferenceSheet` (the Reference button on TUNE, File > Add a Reference Mix...,
  Mix > MATCH TO REFERENCE), which draws the two balances against each other and prints what matching will aim
  for and what it refuses to copy *before* the button is pressed. `SessionStore` stores the measurement under
  `reference`; a document from a schema this build does not know is ignored rather than half-read. Verify with
  `build-engine/tests/livemix_tests` (`Reference*`), `build/app/dlive_app_tests`, the `07c` / `07d` snapshots
  and `build/app/dlive_mix_stems "<stems>" 30 <outdir> gospel -1 broadcast <reference.wav>` (the REFERENCE MIX
  block, and RE-TUNE still saying NO CHANGE REQUIRED).
- **THE MONITOR (SOLO) BUS** (2026-09-16, `src/Mix/MonitorBus.h`). Pressing S never changes what the room and the
  stream hear. Solo feeds a stereo accumulator that sits beside the master and never into it (`MixEngine::Monitor`),
  and it leaves by an output feed with `monitor = true` (`OutputFeeds.h`) - headphones, a pair of nearfields, an
  Aggregate Device. `MonitorState` (in `MixParameters`, so solo flags and the monitor are applied in one breath)
  carries the mode, the tap point, the level, dim, mute and what the monitor follows when nothing is soloed.
  `SoloMode::Monitor` is the default and the whole point; `SoloMode::InPlace` is the old destructive behaviour, kept
  because it is right for mixing a recording, never the default, and said out loud on the LIVE page when it is on.
  `SoloPoint::PFL` taps before the fader (a muted channel is still audible - that is what a pre-fade listen is for),
  `AFL` after it. Strips, group buses and FX returns can all be soloed (`FxSlotParameters::solo`). The whole monitor
  path is skipped when no feed carries it, so a session that never uses it costs nothing; solo with nowhere to go is
  said once rather than silently doing nothing (`MixController::hasMonitorOutput`). Verify with the `MixEngine: solo
  ...` / `MixEngine: PFL ...` tests and `Monitor: solo is monitoring ...` in `dlive_app_tests`.
  **From the user's side the whole feature is two pickers** on the Outputs sheet - "Broadcast"
  and "Solo" - and everything under them is DLIVE's problem, because macOS opens exactly one
  audio device at a time. Two different devices makes `app/native/MonitorDevice` build the
  combined CoreAudio device itself: **unstacked** (a *stacked* aggregate is a Multi-Output
  Device, which mirrors one bus to every device in it - which is precisely why a private solo
  was impossible before), with the broadcast as clock master and **drift correction on the
  solo device**, because Dante and a USB interface do not share a clock. The same device with
  four or more outputs needs no aggregate at all: solo takes its outputs 3-4. Choosing
  "nowhere" removes what DLIVE made and puts the Mac back. The words "Aggregate Device" never
  reach the user; a device the *user* built is never touched (only ours carries our UID). The
  picking rules live apart from CoreAudio in `MonitorDevicePick.cpp` so they are tested
  without a device - the one that matters is "never suggest the laptop speaker when a real
  interface is plugged in".
  **The broadcast and the engineer's listen are always a real stereo pair**, enforced by
  `normaliseOutputs` at the single chokepoint every routing passes through
  (`MixController::setOutputFeeds`), never at the call sites: a mix that reaches the stream
  summed to mono, or on one leg because a pair was half-chosen, is the kind of fault nobody
  notices until it is on the recording. The optional extra feeds keep their mono switch,
  because that is what it is for (one fill speaker, a feed to a phone).
  **The words are the volunteer's, not the engineer's**: WHAT I HEAR, "Only I hear it" /
  "Everyone hears it", "In the mix" / "On its own", "My headphones". The engineer's terms
  (monitor bus, solo in place, AFL, PFL, aggregate device) survive once each, in tooltips -
  the same plain-language-on-the-surface rule the rest of the app follows.
- **LIVE SAFE is a policy, not a tooltip** (`src/Mix/LiveSafe.h`), enforced in `MixController` rather than in a menu
  handler - a guard in `MainView` only covers the menu, and the AI, the chat, a macro and a keyboard shortcut all
  reach the mix without passing one. It never locks the emergency controls (mute, solo, the monitor, the transport,
  recording, UNDO/REDO); it refuses what changes the mix wholesale or interrupts the audio (TUNE / TUNE CHANNEL /
  TUNE LIVE MIX / MATCH TO REFERENCE, KEEP, REVERT, BYPASS, routing, output routing, the device, timeline edits,
  opening a session); and it *limits* what is still allowed - `maxFaderStepDb` 6, `maxMasterStepDb` 3,
  `maxInputGainStepDb` 6, a pan step - so one slip cannot throw a fader across the console. A refusal always carries
  the sentence saying what the risk was. Moving the *monitor* feed stays legal mid-service (`onlyMonitorChanged`).
  `DawEngine::setLiveSafe` is the one place both halves are set (the timeline's lock on `Project`, the mix's policy
  on `MixController`), so they can never disagree.
- **REPEATABILITY** is a requirement of the reasoning layer, not a setting. The same band, the same listen and the
  same settings must produce the same mix. `MixContext::fingerprint()` (FNV-1a over the canonical document, written
  out so it does not move with the toolchain) identifies a listen; it is the model's `seed` and the key of
  `MixReasoningCache`, so a question already answered is answered the same way without a round trip.
  `OpenAiMixProvider` sends `temperature: 0` and `top_p: 1` for the primary mix (a reasoning model takes only the
  seed), and the brief tells every provider to be repeatable. **TRY ANOTHER MIX** is the only way to a different
  reading: `LiveTuneSettings::variation` 1, 2, 3 ... - asked for by name, and itself repeatable -
  and it works from the listen DLIVE already has (`reuseListen`), so two readings are compared against the same
  performance. Verify with the `Repeatability: ...` tests, which pin a deliberately drifting provider.
- **AI MIX CHAT** (`app/ui/ChatSheet`, `MixController::sendChatRequest`) is not a second mixing engine: a sentence
  goes through the same pipeline as TUNE LIVE MIX - intent, `CapabilityResolver`, `MixSafetyValidator` - and comes
  out as an ordinary `MixPlan`, so BEFORE / AFTER, KEEP, REVERT and the Inspector work on it unchanged and nothing
  typed into a chat can reach a parameter by a path the reasoning layer could not. With no cloud model configured
  the request is read by `MixAI/MixRequestParser` - deterministic, offline, and honest: a sentence it cannot read
  comes back with what to try instead rather than a confident change to something nobody asked about, and a capture
  problem ("the singer is off mic") is named as a capture problem. The transcript and the conversation ride in
  `MixReasoningRequest::conversation`. Beside it is mix-level UNDO / REDO (`markMixChange` / `undoMix` / `redoMix`),
  which LIVE SAFE deliberately never locks.
- **HOW LOUD THE FINISHED MIX SHOULD BE** is a setting now (`MixSession::delivery`, `DeliveryLoudness`). It used to
  be a hidden consequence of the purpose: "Church Broadcast" quietly meant EBU R128, which is -23 LUFS - right for a
  television feed and about 9 dB under what a church stream is expected to be - and nothing said so. The target is
  what the whole gain structure is fitted against (`MixPlanner` sets it on the master's `targetsOverride`, after any
  reference and winning over it, because a reference sets the tone and is never allowed to set the delivery
  loudness), not a gain added at the end: at -14 the stems land at -14.7 LUFS, -3.2 dBTP and the same 14.5 dB crest
  as the -23 mix, and RE-TUNE still says NO CHANGE REQUIRED. `MixController::getMasterLoudness()` is the one place
  the master's LUFS-I / short-term / true peak / limiter reduction / target / headroom are read, so no two pages can
  disagree. Check with `dlive_mix_stems "<stems>" 30 <out> gospel -1 broadcast:-14`.
- **AMBIENCE is the sixth group bus** (`MixBus::Ambience`, before MASTER) and `SessionStore` is **version 4**, which
  remaps a version <= 3 document's bus slots (`busFromStoredIndex` + `storedBusCount`: the last stored slot has
  always been the master, wherever it sat). `ChannelRole::CrowdMic` / `AmbienceMic` / `AmbienceBus` and
  `RoleFamily::Ambience` / `AmbienceBus` are their own family with their own strategy
  (`src/Tune/AmbienceStrategies.cpp`): **never gated** - on a room microphone the quiet between the sounds is the
  sound - never transient-shaped, high-passed well above a stage source, compressed slowly as a ceiling rather than
  for punch, and aimed well under the band (`busBelowVocalsDb[Ambience]` = -12). A congregation microphone routed
  through the drum-room rules was gated and pushed forward, which is the bug this fixes.
  `ChannelRole::SaxAlto` / `SaxTenor` / `SaxBari` (`RoleFamily::Saxophone`, MUSIC bus, same file) are a horn and not
  a keyboard: the honk between 0.9 and 2.5 kHz is *notched* rather than shelved, the compressor is fitted for the
  range between a held note and a wailed one with an attack slow enough to keep the reed, the high-pass sits under
  the horn's own lowest note, and it is never expanded. Both are in `StemNames`, `Dine::roleGroups`, the ASSIGN
  kits and `Dine::busTint`.
- **INPUT MAPPINGS** (`app/native/InputMapStore.{h,cpp}`, `~/Music/DLIVE/Input Maps/*.dlivemap.json`, File menu).
  A church patches the same desk the same way every week; a map is the patch and nothing else - device channel,
  name, source, stereo link - deliberately not a mix. Save / rename / duplicate / import / export / apply. The one
  rule that matters: applying a map must never route audio to the wrong place, so an input the open device cannot
  provide comes back **switched off and named**, with a sentence saying what is missing, and the dialog says so
  *before* anything is applied. Two inputs wanting one channel is reported the same way.
- **The TRACKS channel panel is resizable** (`TracksPage::setPanelWidth`, the divider at `headerWidth`): the
  standard DAW drag, one width inherited by every row, persisted with the session (`Document::trackPanelWidth`).
  `kHeaderWidth` is gone - everything on that page measures from the member.
- **Desk sizes.** `build/app/dlive_ui_snapshots --sizes [dir]` renders every workspace at 1280x800, 1440x900
  and 1920x1080 - the three screens a booth actually has - so a layout that only holds together at the
  developer's window is caught before a Sunday. `dlive_ui_snapshots <dir>` (no flag) is still the full set of
  states, and `21`-`25` are the smallest window DLIVE allows.
- **UI frame budget.** `build/app/dlive_ui_snapshots --frames [channels=48] [frames=120]` builds a realistically
  large console and reports, per workspace, the cost of one `refresh()` and the cost of a full repaint. A full
  repaint is 30-130 ms at 48 channels, so **no page may call `repaint()` on itself from its 30 Hz tick** - that
  alone spends the whole frame. Every workspace now compares what it is about to draw with what it last drew
  (`Look` / `PageLook` / `InspectorLook`) and repaints only when that changed; TRACKS repaints the meter strips it
  has to (`meterCell`) and the lanes only when the playhead moved. When adding anything to a page's `paint`, add it
  to that page's `Look` too, or it will draw stale.
- **THE 2026-09 DESKTOP REVAMP** (the Claude Design file `DLIVE Desktop.dc.html`). The window used to carry two
  navigations for one thing - a 224 px sidebar of setup steps that was permanently in the way once setup was
  done, and a row of workspace tabs in the toolbar. **The tabs won.** There is now exactly one navigation:
  `SETUP TRACKS MIXER CHANNEL TUNE LIVE` in the middle of the toolbar (`MainView::kWorkspaceTabs` = 6, Cmd-1..6
  left to right). What takes the sidebar's place is not navigation at all but the session: `app/ui/ChannelRail`,
  **the channel list beside every workspace** - every channel under its group, searchable, filtered All / Inputs /
  Groups, with a live meter and the gain-staging flag, folding to a named 17 px handle (`Ctrl-Cmd-S`, `[`,
  View > Channels). It is the *only* channel list: `AdvancedPage::setRailAvailable (false)` and
  `MixPage::setRailAvailable (false)` switch the Inspector's and TUNE's own rails off for good rather than
  leaving two lists of the same channels on one screen, and TUNE CHANNEL moved onto the shared list's menu.
  Setup is a workspace rather than a mode: `MainView::isSetupPage` plus a 212 px `SetupNav` (Sessions / Audio
  device / Inputs / Purpose and sound, ticked as they are done), and the session button's popover is the same
  four steps with their current values. **A 28 px status foot** (`MainView::StatusBar`) is always on screen -
  engine, drops, disk, what is recording, the broadcast's LUFS against its target, what the engineer's own ears
  are on, LIVE SAFE - painted, compared before repainting (`Look`), and the disk asked once a second because it
  is a syscall. LIVE SAFE moved out of the LIVE page into the toolbar beside BYPASS, because it is a policy about
  the whole desk. A 2 px strip along the very top lights while the mix is going out.
  **The materials are milled now**: `Dine::drawChrome` / `drawHeaderBand` / `drawStatusBand` / `drawPanelGround` /
  `drawRaisedCard` / `drawInsetWell` / `drawSegmentTrack` are the one place a band's gradient, its inset top
  highlight and its closing hairline are decided. Keep every gradient short - never taller than ~120 px, and
  never over a whole console: `drawPanelGround` spends its ramp in the first 240 px and goes flat after it,
  because a full-height gradient is milliseconds a frame and buys nothing. The single exception to "buttons are
  flat" is the one primary action on a surface, which is lit from above (`Dine::drawFilled`, `accentTopLit` to
  `accentBotLit` with a ring of its own colour); a `Filled` button carrying a console state (`setTint`) stays flat.
  Performance rules are unchanged and the revamp obeys them: the rail is one painted component rather than
  forty-eight children, its 30 Hz tick repaints only the meter cells that moved, and `getInputAdvice` (which
  builds sentences) is re-read twice a second and immediately when a tune lands - never per frame. Check with
  `dlive_ui_snapshots --frames 48 120` (a tick is ~1.25 ms per workspace) and `--sizes`.
- **GETTING STARTED** (`app/ui/Tutorial`) is what DLIVE says to somebody who has never opened it: seven sentences
  in the order a Sunday happens - name the inputs, press record, let DLIVE listen, keep or undo what it did, lock
  the desk - each one putting the workspace it is talking about on screen and ringing the control it means
  (`MainView::spotlight`, read from the live components so the tour can never ring empty space). It is a coach,
  not a wizard: nothing is blocked behind it, Esc or "Skip the tour" ends it, and it never touches the session.
  It opens by itself only on a genuine first run (no library **and** no assigned inputs) and is remembered in
  `~/Music/DLIVE/.getting-started-seen`; after that it is Help > Getting started and the session popover.
  `MainView::setAutoTutorial (false)` is how the snapshot tool keeps it out of every other state.
- Read the PRD sections 6-8, 42, 48 and `docs/ARCHITECTURE-DINE-CORE.md` before touching the audio path or adding a product.
- DLIVE is **the live recording and broadcast DAW** (2026-09 DAW milestone; see `docs/MILESTONE-7.md`).
  Four workspaces over one session: TRACKS (timeline, clips, waveforms), MIXER, TUNE, LIVE. The DAW layer is
  `app/native`: `Transport` (the playhead, sample-exact loop), `Recorder` (raw WAV per armed track through
  `ThreadedWriter`), `ClipSource` (the one place clips become audio), `TimelinePlayer` (ring-buffered playback
  on a reader thread), `Project` (tracks, clips, markers, LIVE SAFE) and `DawEngine`, which sits between the
  device and the mix: it records the raw inputs, reads the timeline and builds the **input matrix** that
  `MixController::process` receives, so TUNE MIX works the same on live inputs and on recorded material.
  Monitoring has exactly one rule, `monitorUsesLiveInput` in `Project.h` - do not add a second. A session is a
  folder (`~/Music/DLIVE/<name>/` with `Audio Files/` inside); `SessionStore` is version 3 and still opens
  versions 1 and 2. Import a folder of stems with `MultitrackImport` (it becomes tracks and clips - there is no
  separate "play a recording" audio path any more). Export is `MixBounce::renderProject`, streamed to disk.
  App tests for all of this: `build/app/dlive_app_tests` (`app/Tests/DawTests.cpp`).
- DLIVE standalone (2026-09 pivot; see `docs/ARCHITECTURE-DLIVE.md`): the mix layer lives in `src/Mix`
  (`MixSession`/`RoutingGraph` build buses + returns from assignments; `MixEngine` is the real-time graph, parameters
  arrive whole via `Core/TripleBuffer`; `MixCapture`/`OfflineCapture` listen to every input at once; `MixPlanner` =
  per-strip Tune + input gain + relationships + balance + buses/master; `MixMacros` = the five overview controls, 50 =
  the plan). Mix-level numbers only in `src/Profiles/MixProfileData.cpp`.
  **The group buses are DRUMS, BASS, MUSIC, VOCALS, SPEECH, then MASTER** (`MixBus`, `src/Mix/MixSession.h`).
  A speaking microphone is never mixed in with the singers: `RoleFamily::Speech` routes to its own
  `MixBus::Speech`, which gets its own colour, band, meter, tile and rail section on every workspace
  (`Dine::busTint` in `app/ui/AppTheme` is the one place that colour is decided - do not re-write the
  switch per page) and its own row in the ASSIGN list. Everything that walks the group buses uses
  `b < int (MixBus::Master)`, so a new bus goes in before MASTER. That insertion moved every stored bus
  index above VOCALS, so `SessionStore` is version 3 and remaps a version <= 2 document's five bus slots
  and its output-feed sources (`busFromStoredIndex`) - a session saved before the split opens with its
  master on the master and an empty speech group. A bus chain is only fitted when something feeding it
  was actually heard playing (`busPlayed` in `MixPlanner`): the speech group is silent through most
  songs, and a compressor fitted to silence would crush the sermon the moment it arrives. The plan must stay idempotent on the same
  listen (`MixPlannerTests`); every level decision is absolute from the capture, never "current + delta". Faders and
  the master trim are fitted from levels *predicted under the proposed chain* (`MixPlanner::predictedProcessed{Peak,Rms,ActiveRms}Db`,
  compressor model numbers in `MixProfileData`), so one TUNE MIX lands. A fader is fitted from **loudness while the
  source plays** (`AnalysisResult::activeRmsDb`), never from the sample peak: desk multitrack exports carry isolated
  clicks 25 dB above anything musical, and a peak-fitted fader follows the click. `musicalPeakDb` (the peak capped at
  `hitLevelDb + 12`) is what gain staging reads for the same reason; the raw peak stays the clipping test. A drum close
  microphone hears the whole kit, so the balance lifts one only `maxCloseMicRaiseDb` (gain + fader together) and says to
  turn the preamp up instead. **The effects are timed to the song.** A live console has no host play head, so
  `AnalysisAccumulator` measures a tempo per source (`tempoBpm` / `tempoConfidence`, autocorrelation of the onset
  envelope) and `MixPlanner` takes the consensus across the sources that actually play a rhythm - a held note or an
  open room mic has no onsets and does not vote, or it buries the kick. That tempo rides in `MixParameters::tempoBpm`
  (saved with the session), reaches every return through `MixEngine`'s `FxChain::setTempo`, and also fits the reverb
  tails: `MixProfile::reverbBeats` says how many beats a return may ring for, and the FX profile's own decay stays the
  ceiling, so a quick song shortens the tail and a slow one leaves it alone. Without this every synced delay ran at the
  engine's default 120 BPM regardless of the song. Check with the stems tool's PREDICTION CHECK
  and the `after` LUFS line (target -23, within ~1 LU) when touching gain, fader, bus or master rules. Inputs below
  `faintInputDb` at the device are "faint": flagged, never tuned or raised.
  The app is `app/` (`MixController` no JUCE, `DawEngine` the timeline, `AudioHost` the device, `ui/` pages).
  **The app's look is the marketing site, translated** (`dlive-audio.dinecaleb.chatgpt.site`, source
  `dist/index.html`): tokens, icons, widgets and look-and-feel in `app/ui/AppTheme.{h,cpp}` (`Dine::`), a
  **one** toolbar shell in `MainView` (the sidebar is gone - see the revamp note below), sheets for TUNE MIX
  and its result. The site's own values
  are the tokens - the ground is the brand black `#080909` (`window`), the chrome `#0b0d0b`, a panel
  `#101310`, a tile inside one `#151815`, what is chosen `#20231f`, hairlines at 0.08 / 0.12 / 0.20 / 0.30
  of white, ink that is faintly green rather than blue (`#f3f4ef` down to `#5c625b`) and the electric lime
  `#c8ff3d`. **The lime is the signal, never the chrome**, and that is the one rule to keep: it is spent on
  the primary action, the active workspace, what is selected or soloed, what DLIVE tuned, and on meters,
  gain reduction and loudness - *what the audio is doing*. Everything the engineer **sets** - faders, knobs,
  parameter bars, slider tracks, fader caps - is the pale metal `ink2`, which is why a bank of twenty-four
  channels reads as black, white and one colour instead of a wall of green. A setting that is on or off is
  `DineButton::Style::Toggle` (a lit plane with a lime hairline), never `Filled`; `Filled` is the one primary
  action on a surface, and takes `setTint` when it means a console state instead (a mute is amber). A chosen
  row in any table or list is `Dine::drawSelectedRow` - a lit plane and a lime marker on its leading edge,
  never a bar of colour with the text knocked out of it. Buttons are flat: the site has no gradients and no
  glow. Caps only in the product verbs (TUNE MIX / RE-TUNE / KEEP / REVERT / BEFORE / AFTER / BYPASS /
  LIVE SAFE), at the site's 0.06 em; the plug-in keeps `Tokens` in `src/UI` and is unaffected.
  Colour literals do not belong outside `AppTheme`; if a page needs a new value, the token is what is new.
  MIXER (`app/ui/MixerPage`) is one `Strip` component laid out two ways - STRIPS (a vertical bank at three widths)
  and LIST (a row per source) - with a filter (All / Inputs / Groups), pan and R/A/M/S. The bank is **one console
  surface, not a row of cards**: a column is flat, carries its group's colour along its top edge and a hairline
  down its right, the groups are separated by a gap with the family's colour drawn over them (`Bank::Band`, 4 px,
  no label), and the master is pinned to the right. A STRIPS column reads top to bottom the way a console does:
  number and name, the gain-staging chip, INSERTS (the chain stages that are actually on, from `activeChainStages`),
  SENDS (the used FX slots, a readout - sends are edited in the Inspector), PAN, then the fader and meter, the level
  and peak, the keys, and the bus it feeds. **The slots are fixed** - three inserts, two sends, and the gain and pan
  rows are reserved for every channel and bus - so an empty slot holds its place and the sections line up straight
  across the console; `buildColumn` is the one place they are positioned (paint and layout read the same `Col`) and
  a short strip drops whole sections, in a fixed order, rather than squeezing the fader. There is no per-strip dB
  ruler: the one mark a bank is read against is the 0 dB unity line, drawn across the fader and the meter at the
  same height in every strip (the five group meters on TUNE carry the numbers instead). The console fader is
  `dineFader` in `DineLookAndFeel::drawLinearSlider`: a dark milled slot and a moulded cap, never a lit track.
  A fader moves when it is **dragged and at no other time** - a two-finger swipe across a bank of faders is a
  scroll, not twenty-four small changes to the mix - so every `juce::Slider` in `app/ui` goes through
  `Dine::dragOnly` (no wheel; the event passes to the surface underneath) and every scrolling surface through
  `Dine::nativeScrolling`, which sets the step that makes a trackpad swipe travel as far as the fingers do.
  A `Strip` is opaque, cached as an image and repaints only what moved (`Strip::Look` / `showLook`), so
  scrolling a 24-input console is a blit rather than twenty-four columns of text redrawn per frame. The master is pinned to the
  right of the bank (it is a child of the page, not the scrolling `Bank`) and carries the LUFS-I / short-term /
  true-peak readout against the -23 target. A click picks a strip out, a double-click opens it in the Inspector.
  It also opens in its own window (View > Open Mixer in a New Window, or the button on the
  page); the detached page is a second `MixerPage` on the same `MixController`, so both consoles always agree.
  TRACKS (`app/ui/TracksPage`) is drawn and hit-tested by hand: a tool row (row height S/M/L, Snap, Follow,
  Split, Marker, what is selected, the loop and the zoom) and then one 46 px ruler band that holds the loop
  strip along its top (drag it to mark a loop), the marker lane inside it (click to jump, drag to move,
  double-click empty to add, right-click to rename or delete, `M` / Edit menu to add at the playhead) and the
  ticks along its foot. Snap is magnetic to the grid, the markers, the playhead, the loop
  and every other clip edge (`snapSample`). `keyCell` is the one place the R/A/M/S keys are positioned (a 2 x 2
  block beside the meter), so paint and hit-test cannot disagree; a header reads a status dot (accent when the
  chain is doing something), number, source icon and name over its fader and level, and a third line that exists
  only when it has something to say - the balance when it is not centred, the gain-staging chip when there is
  advice. The name carries its own warning glyph when it no longer matches the clips, and a resting R/A/M/S key
  is a hint rather than a boxed button, so the normal row is two lines. Clip and header colours come from
  the same four group tints the mixer bands with, and a clip you cannot hear (muted, or soloed out) is drawn grey.
  A TRACKS header also carries its own volume fader (`faderCell` is the one place it is
  positioned; a tall row gets it under the name with the level beside it, a short row a slim
  bar along the foot), a click on a header picks that channel out (the chain strip along the
  foot reads it) while a *double*-click opens it in the Inspector - the header's own controls
  keep their single clicks - and pinch on the trackpad (or Cmd-wheel) zooms about the pointer
  via `zoomAround`.
  A track and its input are two lists joined by index (`Project::tracks` / `MixSession::inputs`),
  so `Project::syncTracks (previous, next)` remaps the tracks whenever the assignments are
  rebuilt - each track follows its own input by device channel, then by name - and
  `HostServices::reconfigure` hands the DAW engine the new session; without that an input
  dropped on the ASSIGN page left the clips behind and every name below it slid by one.
  **Which input a new input used to be is decided in exactly one place**, `matchInputs`
  (`src/Mix/MixSession.h`): the clips read it (`syncTracks`) and so does the kept mix
  (`carryMix`, `src/Mix/MixParameters.h`), so the timeline and the console can never end up
  disagreeing about which input is which. `carryMix` is what makes rearranging free - every
  channel that survived a rebuild keeps its chain, gain, fader, pan, keys and sends, and only
  an input that became a *different source* goes back to its baseline (a kick's gate is wrong
  on a voice); the buses, the master, the returns and the tempo are not per-input and come
  across whole. One helper, `MixController::carryKept (previousMix, previousSession, tunes)`,
  is what a host calls straight after `prepare()`, and `getPreparedSession()` is the session
  the running mix belongs to (`setSession` replaces the document before the graph is rebuilt,
  so `getSession()` is the wrong thing to read it against).
  **Rearranging the channels is a drag on the TRACKS header** (`TracksPage::moveTrack`, also
  Move up / Move down on the header's menu and in the Track menu): press a header and drag it
  up or down, a line shows where it would land, and letting go moves the *input* - the mixer's
  bank, TUNE's rail, the Inspector's list and the ASSIGN page all read the new order. The row
  only lifts after the pointer has actually travelled (`kOrderGrip`), so a click that wandered
  still just selects; dragging past the edge of the lanes scrolls. Nothing about the sound
  changes, but the graph is rebuilt, so LIVE SAFE locks it like any other re-route.
  A header whose name no longer describes the audio under it is drawn amber with a warning
  glyph (`nameMismatch`: a take suffix is not a mismatch, a different word is), and
  right-clicking a header is the one place to put it right - Rename, Use the clip's name,
  Match every track to its clips, Source, Icon, Fix the assignments..., Open in the Inspector.
  A rename goes through `MixController::setInputName`, which sets the session *and* the graph's
  copy and rebuilds nothing, so TRACKS, MIXER and the Inspector are renamed together and the
  kept mix, the plan and the clips all survive; changing the source rebuilds the routing and
  says so. The icon is the same kind of label: `InputAssignment::icon` (last in the struct,
  so the brace-initialised sessions in the tests still compile) holds a key from
  `Dine::iconChoices()`, empty meaning "whatever the role says"; `Dine::iconFor (key, role)`
  is the one place that decision is made, `MixController::setInputIcon` sets it without a
  rebuild, `RoutingGraph`/`StripRoute` carry it so MIXER and TUNE agree, `AssignPage::commit`
  carries it (it rebuilds every assignment from scratch), and `SessionStore` writes it only
  when it is set.
  Both workspaces end in `app/ui/ChainStrip`: the picked-out channel's chain stage by stage with what each is set
  to, from the same `chainStages` the Inspector's cards follow. Click it to open the Inspector.
  Outputs: the mix can leave by more than one pair at once. `src/Mix/OutputFeeds.h` is the model (up to
  4 feeds, each a device output pair + source (master or a group bus) + level + mono + mute); `MixEngine`
  takes them through their own `TripleBuffer` (`setOutputFeeds`) because monitoring is not mix - the planner,
  the macros and BYPASS never touch them, and an export is unaffected. `AudioHost` opens every output channel
  (`kMaxOutputs`). The UI is `app/ui/OutputsSheet` (toolbar output popup > Set up outputs, View menu, or the
  Device page). CoreAudio opens one device at a time: two devices at once is a macOS Aggregate Device, which
  the sheet explains and can open Audio MIDI Setup for.
  INSPECTOR (`app/ui/AdvancedPage`) is the engineer's drill-down, in three columns. Left, a 206 px rail:
  every channel under its bus (dot, name, a mini level bar, its fader) with the engine's own state - rate,
  buffer, latency - along the foot. Middle, the channel: a 96 px head (colour, what it is, its name, the IN
  and OUT meters either side of the chain, input gain, level, pan and the keys), then `SignalPath` - the
  whole chain as a chip per stage with its lamp, number, icon, setting, a bar for how hard it is working and
  a dot for where the setting came from (accent = tuned by DINE, amber = hand-edited) - and under it the
  stage you picked, opened as a device by `app/ui/ChainEditor`: its name and plain sentence, a badge, IN/OUT
  and `Back to DINE`, then what the stage is doing drawn (an EQ curve with nodes you drag, a compressor's or
  gate's in-out line with the live gain reduction and the last 8 seconds, the bars of a trim with its gain
  staging) beside a knob for every number it owns - band cards on the EQs, a knob grid and switch chips
  elsewhere. Right, a 272 px column: what TUNE MIX did, a line per stage (what it is set to, TUNED /
  EDITED / NOT USED, and the sentence from the report that explains it - matched by the parameter ids the
  stage owns), RE-TUNE and REVERT, and the headroom (the master shows its loudness instead). The chip
  labels and readouts come from `chainStages` in `ChainStrip`, so the path, the mixer's INSERTS and the
  strip along the foot of a workspace can never disagree. The limiter stage appears on the master only and
  the width stage on stereo channels only, because that is where `MixEngine` configures them; the sends
  close the path where the session uses FX. "Hand-edited" is a diff against `getPlan()->proposed`, which is
  also what `Back to DINE` and REVERT put back. Edits go through `MixController::setStripChannel` /
  `setBusChannel` as a whole `ChannelParameters`: they live on the kept mix beside the faders, survive a
  macro, are saved with the session, and the next TUNE MIX replaces them the way it replaces a fader.
  BYPASS disables them.
  Gain staging is the first move in a mix and the app says so before it says anything else:
  `MixController::getInputAdvice (strip)` is the one place that reads the plan and answers what
  one input's level needs (Faint / NotHeard / Low / Hot / Clipping / Digital / Healthy, the
  console move in dB and the sentence). `Digital` is the one that matters in a church - the
  level works, but only because DLIVE raised it more than `digitalGainAdviceDb`
  (`MixProfileData`) digitally, which lifts the preamp's noise with the source. It is surfaced
  as a chip on the TRACKS header and the MIXER strip (both layouts), as the GAIN STAGING card
  at the top of the Inspector's right column, as the plan's *first* note (naming the inputs)
  and in `getMixHealthNotes`; an input that only works on a big digital raise is not counted
  healthy. Advice only: nothing about the mix changes.
  TUNE CHANNEL is one source on its own, on click: `MixController::startTuneChannel (strip)`
  runs the same listen as TUNE MIX (every input is measured, so the channel is still decided
  in mix context) but waits for that channel (`MixCapture::Settings::triggerStrip`) and is
  shorter, and the plan is narrowed by `MixPlanner::channelOnly (plan, strip, profile)`:
  `proposed` is `before` everywhere except that strip, so the buses and the master are left
  alone and what is proposed is exactly what the mix becomes when it is kept - which is also
  what the Inspector reads as "what DINE set". Every other strip keeps what the listen
  measured about it (gain-staging advice and mix health stay fresh) with the moves the plan
  does not make taken back out. BEFORE / AFTER, KEEP and REVERT are the mix's own, and
  `getTuningStrip()` says which channel a listen or a preview is about (-1 = the whole mix).
  It is clicked from the TUNE workspace's input rail, a mixer strip's right-click menu, a
  TRACKS header's menu, the Inspector's right column, the Mix menu or `T`; the sheet
  (`app/ui/ChannelTuneSheet`) drops over whatever workspace you are on, so the console keeps
  playing behind it and MixPage's own sheets stay out of the way.
  Every panel at the edge of the window folds away, so the middle can have the width: the sidebar
  (the toolbar's leftmost button, View menu, `Ctrl-Cmd-S`, `MainView::setSidebarShown` - `sidebarWidth()`
  is the one place the rest of the window reads it), the Inspector's channel rail and its WHAT DINE DID
  column (`AdvancedPage::setRail/TrailShown`) and TUNE's input rail (`MixPage::setRailShown`). A page
  panel keeps a `Dine::Metric::panelTab` gutter with `DinePanelTab` in it - the same handle everywhere,
  chevron pointing the way the click moves the panel, the panel's name down the gutter when it is closed
  - and `[` / `]` toggle the panel on that side of whatever page you are on (`MainView::togglePanel`,
  falling back to the sidebar where a page has no left panel of its own). Nothing about the mix changes.
  State is never left to a shade. `DineKey` (`AppTheme`) is the one console key - M amber, S teal, R red, A blue,
  dark letter on a filled key when it is on - used by the mixer strips, the LIST rows and (drawn the same way by
  hand) the TRACKS headers, so a mute looks like a mute wherever it is pressed; a muted strip darkens, names itself
  in amber and its meter greys out (`DineMeter::setMuted` keeps reading the signal, so "nothing there" and "not
  heard" never look alike). On LIVE, a muted group tile goes amber and says NOT HEARD, a soloed one says SOLO, and
  LIVE SAFE fills and reads "LIVE SAFE ON" with a sentence beside it saying what is locked. LIVE carries a
  tile per group bus **and one more for the effects returns**: `MixParameters::fxReturnDb` / `fxMute` are the
  FX group's own fader and mute, folded into the return's gain in the one place `MixEngine` already decides
  it (so BYPASS and an unused slot still win) and set through `MixController::setFxReturn` / `setFxMute`.
  0 dB and not muted is "as tuned", which is also what a session saved before they existed reads as. The
  returns have nothing to solo against, so that tile offers MUTE and no more.
  BYPASS (toolbar, Mix menu, `B`) is `MixController::setBypass`: `compose()` returns `startingPoint()` with
  `bypassProcessing`, carrying only mute and solo across, so you hear the console feed. It never touches the kept
  mix - switch it off and the mix is exactly as it was - and faders are disabled while it is on.
  Verify with
  `build/app/dlive_ui_snapshots <dir>` and the real stems: `build/app/dlive_mix_stems "<stems folder>" 30 <outdir>`
  (writes raw/before/after/after-retuned WAVs, exit 0 = re-plan on the same listen changed nothing). App tests:
  `build/app/dlive_app_tests`; real device: `build/app/dlive_device_check 3`; recording playback through the host: `build/app/dlive_device_check 4 "<stems folder>"`.
  The app is **DLIVE** (renamed from DINELIVE, 2026-09-10): target `DLive`, product `DLIVE`, bundle
  `com.dine.dlive`, tools `dlive_{ui_snapshots,app_tests,mix_stems,device_check}`, sessions in `~/Music/DLIVE/`
  as `<name>.dlive.json`. Sessions written under the old name still open and are still listed - `SessionStore`
  accepts `app: "DINELIVE"`, scans `*.dinelive.json` and reads `~/Music/DINELIVE` (`formerNameFolder`) - and are
  written back under the new extension when they are next saved. The app icon is `app/resources/AppIcon.png`
  (1024 px, generated art: the D with a fader cap), handed to JUCE as `ICON_BIG`. The DINE plug-in family and the
  `Dine::` design tokens keep their name; only DINELIVE became DLIVE.
  **Setting a session up is four screens of one layout** (`app/ui/SetupPages`, 2026-09-12): SESSIONS (the
  library), AUDIO DEVICE, INPUTS and PURPOSE AND SOUND. `SetupLayout::of` is the one place the bands are
  measured - title and a readout, a toolbar, the table beside a 252 px rail of small cards, then a footer
  with a note and Back / Continue - so all four line up and a change to the shape is a change in one
  function; `drawSetupHead` / `drawSetupFooter` draw the ends. The shared parts live in `AppTheme`:
  `DineChip` (a filter chip with its group's dot, in one rounded track), `Dine::drawRadio`,
  `Dine::drawCaption` and `Dine::drawStackedBar` (one bar divided by group, in the bus colours).
  SESSIONS lists every saved session with what it sounds like, what it was for and how its inputs fall
  across the groups - read by `SessionStore::summarise`, which parses the document's header only and never
  walks the audio beside it - and is where the app opens when there is a library to open into.
  INPUTS groups the desk by the bus each input will feed (a clickable group header picks the whole group
  out), and a selection turns the toolbar into the bulk one: set what they are, fill a kit down them in
  order, name them from their role, link them as pairs, drop them. The numbers on PURPOSE AND SOUND are
  `Profiles::targets (profile, masterRoleFor (purpose))`, so what the card promises is what TUNE MIX aims
  at. AUDIO DEVICE and INPUTS draw what is arriving on each device channel from
  `DawEngine::inputPeakDb` / `numInputsCarryingSignal` - a peak per block with a slow release, stored from
  the audio thread with relaxed atomics before anything in the mix touches the signal - so "the console is
  plugged in but channel 9 is dead", and "this unnamed input is carrying signal", are visible before a
  single input has been named.
  In the app, Import a multitrack... on the device page (or File > Import Multitrack Folder...) turns a stems
  folder into tracks and clips, with names and sources guessed from the file names.
  **"Arm" is not a word the app says.** A volunteer does not know it, and the message they meet when they press
  Record is the worst place to teach it. The field stays `Track::armed`, the key stays the red **R** (a console
  key, beside A/M/S), and every sentence around it is plain: "set to record", "N TO RECORD", "No tracks are set
  to record yet - press the red R on each track you want". The engineer's word appears once, in the R key's
  tooltip, as the thing it is called elsewhere. The TRACKS keys are drawn by hand, so `TracksPage::getTooltip`
  is where R, A, M, S and the fader say what they are; the tool row's **All to record** button
  (`setAllToRecord`) is the one click for a whole session, and it mirrors the Track menu.
