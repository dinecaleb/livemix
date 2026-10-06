# DINE v4 — the gaps between the design and the code

The v4 design is `docs/design/v4/DINE v4.html`, its brief is `docs/design/v4/HANDOFF.md` (written as
"DLIVE v3"; the product is DINE and the design is filed as v4), and every feature the app has today is
listed in `docs/design/v4/INVENTORY.md`. This file lists where the three disagree.

A gap is a v4 control or behaviour that the backend cannot serve today, or a backend feature that v4
gives no place. The UI work never changes DSP, engine, audio-thread, file-format or session-serialisation
code to fit a drawing. A v4 control with no backend is built, wired to a clearly named stub marked
`// TODO(v4-backend)`, and listed here. `grep -rn "TODO(v4-backend)" app` must list exactly the rows of
the "No backend today" table. A gap is closed by landing the backend and deleting its line here, in the
same commit as the stub.

## Decisions the owner made (2026-10-06)

Each was a conflict between the brief and a rule or a test. The owner's answers, and where they landed:

- **⌘1–5:** v4's order - Mixer, Tune, Inspector, Live, Tracks (`MainView::commandForKey`, the sidebar
  tooltips, View menu order, `ReachabilityTests`). A muscle-memory change for the release notes.
- **⇧⌘E:** Export (command 105, the Export sheet). Shift is tested first; ⌘E still splits at the playhead.
  ⇧⌘S (Save As) and ⇧⌘Z (Redo) stay.
- **Esc:** the topmost layer only - a menu closes itself, then the sheet, then a channel's tune, then Mix
  Buddy (`MainView::closeTopSheet`). Each closes through its own `onClose`, so the microphone sheet's
  close is still "Not now" (asserted in `ReachabilityTests`).
- **Space on Live:** goes to the next cue (Part B). Play / stop stays on the transport pill there, and Space
  is play / stop on every other workspace. A stray Space on Live during a service never stops anything.
- **Tune's voicing menu:** the eight stored `MasterVoicing` values, drawn in v4's style. No new enum values.
- **Sample stage defaults:** `ProfileData.cpp`'s numbers; the stage reads the profile.
- **Minimum window:** 1280 x 780 (`app/Main.cpp`); the snapshot tool and the console test render there.
- **SOLO pill:** the name goes to what is soloed, the cross clears every solo.
- **Purposes:** four, as stored.
- **Smaller calls:** toast about 4 s; the sidebar hides fully; Live's FX strip keeps its S.

## No backend today (stub in the UI)

Smallest backend: **read-only** = an accessor over state that already exists; **pass-through** = an
`AppServices` method calling a host or controller function that exists; **host** = new app/native code
outside the engine; **ENGINE** or **SESSION** = an engine, audio-thread or session-format change, which
must not be done as part of the UI work.

### Shell and toolbar

| v4 control | Where | What is missing | Smallest backend |
| --- | --- | --- | --- |
| Readiness pill and Ready sheet rows with Fix (today the pill counts inputs needing attention plus open checklist items, and opens the broadcast checklist or Check inputs: `MainView::readyPillClicked`) | Toolbar | No live status aggregate; `BroadcastReadiness` is an operator checklist and must never auto-tick | read-only aggregator (device state, audio running, input advice count, armed tracks, seconds free, monitor output, autosave failing), separate from BroadcastReadiness |
| Ready: disk and recording destination | Ready sheet | `getRecordingSecondsFree()` is 0 with nothing armed; no bytes free | read-only (`File::getBytesFreeOnVolume` on the session folder) |
| Session menu "Recover Session…" | Session menu | Recovery is offered only at launch (`Main.cpp` → `MainView::offerRecovery`); nothing can be asked for later. Today it says so in a toast (`MainView::setupPopover`, case 21) | pass-through: `AppServices::findRecovery()` returning the same offer `Main.cpp` builds |
| "Edited" after the session name | Session menu | No "changed since the last explicit save" | read-only (revision at last save beside `sessionRevision`) |
| Traffic lights move with the sidebar | Toolbar | `WindowChrome.mm` places buttons at a fixed `kFirstButtonX` | host (an x-offset setter) |
| Reduce Motion | Every animation | No accessor | host (small .mm helper over NSWorkspace) |
| ⌥⌘P perf overlay: engine block time | Debug overlay | `MixEngine::getStats()` is not on AppServices | read-only |
| Sidebar "Check inputs" issue badge | Sidebar | The count is computed inside the open CheckSheet only | read-only (count from `liveCaptureAdvice`) |
| Background work (import, device open, session load, TUNE planning) | Everywhere | Runs on the message thread today | host / controller threading change; flagged, not stubbed |

### Mixer

| v4 control | Where | What is missing | Smallest backend |
| --- | --- | --- | --- |
| "Edited" dot (a value differs from DINE's tuned value) | Strip | No per-strip edited query outside the Inspector | read-only (compare to `plan->proposed`, as the Inspector does) |
| 3 insert slots | Strip | The chain is fixed (`ChannelProcessor`); there are no inserts | none: draw chain-stage chips instead. An insert would be ENGINE |
| 2 send slots | Strip | There are 5 FX slots per strip | none: show the first two or the used ones |
| Quick inspector (EQ curve, key stages) | Right rail | No compact component | none (UI: reuse ChainStrip and the Inspector's EQ drawing) |

### Tune

| v4 control | Where | What is missing | Smallest backend |
| --- | --- | --- | --- |
| "Silent, with the reason" per input during the listen | Listen list | `busHeard` is per group; per-strip flags exist only once a plan exists | read-only only if a live per-strip reason is already computed; otherwise ENGINE |

### Inspector

| v4 control | Where | What is missing | Smallest backend |
| --- | --- | --- | --- |
| Hit visualiser: per-hit events (fired / held / ghost), last-16 velocity pad | Sample stage | Only cumulative counters (`getHitCount`, `getVetoCount`, `getLastHitLevelDb`, `isPlaying`); `numHits/hitOffsets` are audio-thread only; nothing counts sub-threshold crossings | **ENGINE** (an SPSC hit FIFO written in SampleTrigger). Interim, read-only: infer hits from counter deltas at frame rate, at most one per frame |
| Hit visualiser waveform in the detector band | Sample stage | No min/max feed; only the strip's input meter peak | **ENGINE**. Interim: the input meter |
| Hi-hat sample sounds, kits and "Import a sound" | Sample stage | `SampleLibrary`, `importSound` and `DrumKits` carry kick, snare and toms only | host (a hat family in the library, app/native) |
| Loudness stage in the stage list | Stage list | No such stage | read-only view over the existing loudness and delivery readouts |
| "Back to DINE" per control | Every control | Put-back exists per stage, per record and per channel only | read-only (the proposed value per field is in the plan) |

### Tracks

| v4 control | Where | What is missing | Smallest backend |
| --- | --- | --- | --- |
| Rename a clip | Clip | Only the track name can be renamed or matched to a clip name | **SESSION** (clip names are stored). Not in the UI work |

### Setup: Audio device

| v4 control | Where | What is missing | Smallest backend |
| --- | --- | --- | --- |
| Sample-rate menu | Audio device | Only `sampleRate()` is read; no `sampleRates()` / `setSampleRate()` | pass-through mirroring `bufferSizes` / `setBufferSize` (host, not engine) |
| Rescan devices | Audio device | `AudioHost::rescanDevices()` is not on AppServices | pass-through |

### Setup: Inputs

| v4 control | Where | What is missing | Smallest backend |
| --- | --- | --- | --- |
| Desk labels as names | Inputs, right panel "suggested names" | "Use desk labels" writes "Desk NN"; only output channel names are exposed | read-only `inputChannelNames()` (JUCE `getInputChannelNames`) |
| Feeds ▾ (a group other than the role's) | Inputs right panel | The group follows the role; no feed override | **SESSION** if stored. Not in the UI work |
| Source ▾ (move to another port) | Inputs right panel | `inputA` is fixed to the row | host (reassign through `commit`); verify it needs no format change first |
| Source-port column and record dot | Inputs table | No data on AssignPage | read-only (port from the device; arm from `daw()`) |
| Patch applied undo | Input Mappings | None | host (keep the previous map before `Apply`) |

### Sessions

| v4 control | Where | What is missing | Smallest backend |
| --- | --- | --- | --- |
| "Templates" filter | Sessions | A name match only; no template model | host. Keep the name match until one exists |

## Closed

- **Setlist and cues** (2026-10-06): `Setlist` / `Cue` in `app/native/MixHistory.h`, owned by MixController
  (add, update, move, remove, go to, go to next), carried by `SessionState::setlist`, SessionStore version 9
  (an older file opens with an empty setlist). LIVE has the cue header, This cue / Groups / All / Alerts, Up
  next with Louder / Softer and Go, the setlist with Now and Next, and Edit (the Setlist sheet). Space on LIVE
  is the next cue; the sidebar's Setlist row opens LIVE at the setlist.

## Backend exists, no UI yet

These are wired by the UI work; no backend change is needed.

- Scene rename: `MixController::renameScene` (`app/native/MixController.h`) has no UI.
- Favourite rename and delete: `MixController::renameFavourite`, `removeFavourite`; FavouritesPage has neither.
- Overflow note: `SampleLibrary::whatWasLeftOut()` is never shown.
- Gain-health chip and card ("Healthy / Clipping −6 / Digital +12 / Low +6"): `InputAdvice::Level`,
  `consoleMoveDb`, `digitalGainDb`, headline and detail via `getInputAdvice` / `liveCaptureAdvice`.
  The Inspector draws it only in the trail when it needs attention; Simple view gets the card.
- Per-change confidence and why: `Recommendation::confidence` (`src/Recommendations/Recommendation.h`),
  reached through each strip's `TuneResult::report` in the plan.
- Per-change trail lines ("High-pass 80 Hz to 100 Hz"): `AdvancedPage::historyViews` builds them; the trail does not paint them.
- What-it-is typeahead: built (`AssignPage::roleFor` - role names, then `StemNames::guessRole`). "di" and
  "pb" are not in StemNames' table; adding words to it is a host change, not engine.
- Open Audio MIDI Setup: `MonitorDevice::openAudioMidiSetup()` (`app/native/MonitorDevice.h`); no AppServices call reaches it.
- Dropped buffers and CPU in the sidebar foot: `services.xrunCount()`, `services.cpuLoad()`, recorder `getDroppedSeconds()`.
- Loudness menu "Stream −16, Platforms −14": `DeliveryLoudness` Streaming / StreamingLoud. Copy only.
- Input Mappings popup: `MainView::openInputMappings` (Rename / Duplicate / Export / Delete patch) still has no
  caller; the session menu's "Input Mappings…" opens Routing > Patches, which has the same actions.
- Bulk "Name from what it is" and "Pair L and R": `AssignPage::nameFromRole`, `linkSelection` have no button.
- `DevicePage::onContinueToAssign` is wired and never fired; v4's Continue → Inputs uses it.
- Keep snapshots the previous mix: `keepPlan` checkpoints into the mix history; confirm, do not add.
- BYPASS fader-drag toast and LIVE SAFE pre-check toasts on Tune buttons: UI only; the refusal stays in
  `MixController::liveSafeRefuses`, the toast reuses `MainView::liveSafeBlocks`.
- Inputs: built (v4 fast entry and C1) - the keys through the cells, the typeahead, a pasted list filling down, Cmd-D,
  keyboard selection with no cell focused (up/down, Shift, Space, Return, Cmd-A, Esc), Cmd-V's "Paste a list of
  names" with its preview, Number them, the right panel, the preamp banner with Check again, the group chips with
  Not used and search, the Not used section ("Use it").
- UI preferences (sidebar folded, strip size, rails, window bounds, Mixer view): none persisted today;
  per-Mac keys beside theme and text size in `preferences.json`, never `SessionStore`.

## Kept although v4 draws no home

Each of these survives the rebuild. Where v4 has no slot, the nearest v4 surface gets one.

- FX key on voice strips — `MixController::setStripEffects` (`app/ui/MixerPage.cpp`).
- Effects filter on the Mixer — `MixerPage::Show::Effects`; Sends toggle — `MixerPage::sendsButton`.
- Each effect / Back to groups on Tune and Live — `MixController::setFxSlotReturn` (`MixPage.cpp`, `LivePage.cpp`).
- Link submenu (link with any input, untick to leave, "Unlink this fader"), Cmd-drag moves a linked fader alone — `MixerPage.cpp`, `TracksPage.cpp`.
- "This microphone is" job submenu, four jobs, disabled under LIVE SAFE, runs `svc.reconfigure()` — `MixerPage.cpp`, `TracksPage.cpp`.
- FOCUS chip, the only UI that pins MixPlanner's focal — `MixController::setFocusInput` (`MixPage.cpp`).
- KEEP SOME group chips (forces AFTER) and Close = revert — `MixController::setPlanSelection` (`MixPage.cpp`).
- "BEFORE / AFTER is on air" line on the result sheet — `MixPage.cpp` result head.
- TUNE LIVE MIX step lamps (eight `TuneLiveCoordinator` states) and "waiting for the band" — `MixPage.cpp`.
- Channel tunes route through `ChannelTuneSheet` when `MixController::isTuningChannel` — `MainView.cpp`.
- Per-group TUNE on each group tile — `MixController::startTuneBus`; scope sheet maps through `mixBusInDisplayOrder`.
- Raise loudness to target, disabled under LIVE SAFE, and the master card's three cells — `MixPage.cpp`.
- Two DIMs: Live's is the engineer's monitor (`MixController::setMonitorDim`); the toolbar's is the broadcast group's −20 dB (`setBroadcastDim`). Never merged.
- Live FX strip S = `MixController::setFxSoloAll` — `LivePage.cpp`.
- The LIVE SAFE and Autopilot links on Live turn on only; off is the toolbar — `LivePage.cpp`.
- Scene recall on click; Keep writes into the picked slot — `LivePage.cpp`.
- Two-press stop while recording (Space, R or the menu within 3 s) — `MainView::handleCommand` 500/501. A new pill or Live's Space must not bypass it.
- Undo domains (Mix versus Timeline by last edit, Tracks only, LIVE SAFE skips the timeline), Edit menu labels — `MainView::undoTarget` / `redoTarget`.
- LIVE SAFE gates in UI code (about 15 callers) — `MainView::liveSafeBlocks`, `HostServices::deviceChangeLocked` (`app/Main.cpp`). DIM, MUTE, BYPASS, solo, Autopilot and recording are never gated.
- Microphone permission: Continue asks first, a refusal still opens the output, any close is "Not now" — `MainView::explainMicrophone`, `followMicrophone`.
- Leaving Routing reconfigures and resets the LIVE SAFE confirmation — `MainView::showPage`.
- Sheets close via `callAsync` + SafePointer; ChoiceSheet runs onClose before its action — `MainView.cpp`.
- Mix Buddy is a column that narrows the workspace; its actions never keep a change — `MainView::performBuddyAction`.
- Inspector never edits under BYPASS — `ChainEditor::commit`; three put-backs (stage, record via `MixController::restoreStripTune`, whole channel) — `AdvancedPage.cpp`.
- Simple knobs are relative to `asTuned`, re-seeded on channel change, view switch and external edits — `AdvancedPage.cpp`.
- Inspector MUTE / SOLO in the head and the Tracks keys — `AdvancedPage.cpp`, `TracksPage.cpp`.
- Inspector in its own window, wired like the docked one — `MainView::openInspectorWindow`.
- Inspector rebuild guard and counting against `controller.getGraph()` — `AdvancedPage::rebuilding`.
- Track index is not strip index — `TracksPage::stripOf`.
- Loop strip gestures (drag new, ⌥-drag move, click toggle, tiny clears) — `TracksPage.cpp`.
- Sends knob "off" is `kSilenceDb`, never clamped to −60 — `AdvancedPage.cpp`.
- Auto-link rules (Overhead, DrumBus, Piano, E-piano, SynthPad; Fill in order pairs; unassign clears link; empty name takes the role) — `AssignPage::commit`.
- Gain staging reads the held peak (about 3 dB/s fall), "Not heard" never counts as preamp — `liveCaptureAdvice`.
- AssignPage's hidden members (search, Clear all, "Show them", patch Save/Apply, QuickAction cards) — `app/ui/AssignPage.cpp`.
- Device Continue skips to Tracks when inputs exist — `DevicePage` / `MainView::enterSession`.
- Outputs rules (feed 1 fixed, main capped at 0 dB, MONO off on Broadcast and Monitor, solo device rules, "DINE Monitoring" hidden) — `app/ui/OutputsSheet.cpp`.
- Patches: Duplicate, Export, "Apply anyway", Apply blocked under LIVE SAFE — `RoutingPage` / `InputMapStore`.
- Export guards: one export at a time, quit asks, shutdown waits 15 s, MP3 and loudness off for stems — `MainView::exportMix`, `app/Main.cpp`.
- Reference sheet cannot close while measuring; a dismissed measure still lands — `ReferenceSheet`.
- History restore disabled when the input set differs; "Aim at this" needs a measured favourite; every change calls `touchSession()` — `HistorySheet`.
- Broadcast readiness, purpose-gated, confirmations only, needs a sidebar home — `BroadcastReadinessSheet`.
- Scenes and "Open in a New Window" from the sidebar right-click need explicit homes — `MainView` sidebar.
- Tour spot names ("session", "tabs", "rail", "transport", "livesafe") and page ints shared with `WorkspaceGuide::indexForPage`, `Tutorial::steps` — `MainView::spotlight`.
- Arm, monitoring and favourite edits call `svc.touchSession()`; never `saveSession()`.

## Performance findings

What the UI does today (`app/ui/MainView.cpp` and friends), against the brief's rules.

- One 30 Hz `juce::Timer` in `MainView::timerCallback` is the main clock. It drives `controller.poll()`
  (listen and plan, Autopilot, checkpoints), the transport, the visible page's `refresh()`, open sheets,
  the status foot (1 Hz), the sidebar (3 Hz), the chain foot and SOLO pill (10 Hz), toasts and autosave.
- Detached windows run their own 30 Hz timers: `MainView::MixerWindow`, `MainView::PageWindow` (Live, Inspector).
- `MacroPad` and `MacroRibbon` each run a 60 Hz timer for the settle animation; they stop when done.
- No `VBlankAttachment` and no `AsyncUpdater` anywhere. The Reference measurer and the export thread are polled from the 30 Hz tick.
- Not idle-zero: the "on air" top strip repaints every tick while audio runs with inputs, and every visible page polls at 30 Hz whatever the signal.
- `LevelMeter::consumeMaxPeakDb()` keeps a reader-owned accumulator and supports one reader. MixerPage,
  LivePage, MixPage, AdvancedPage, TracksPage and the detached windows each consume it on their own
  timers, so two readers of one meter steal each other's peaks. A single tick that reads each meter once
  per frame into a shared snapshot fixes it with no engine change.
- The one clock must keep the message-thread work that is not drawing: `controller.poll()` (Autopilot),
  the autosave cadence, the microphone follow-up, the telemetry notice and toast expiry. VBlank stops when
  the window is minimised; Autopilot and autosave must not, so they stay on a timer of their own.
- Slow work on the message thread today: `importAudio`, `openDevices`, `loadSession`, TUNE planning
  inside `poll()`. Moving them is a host and controller threading change (see the gaps above).

The brief's targets, recorded before and after in `docs/design/v4/PERF.md`:

- One shared `VBlankAttachment` (fallback one 60 Hz timer) drives meters, the hit visualiser, playhead,
  clocks and rings; no per-component timers; hidden or minimised components take no ticks.
- Idle (transport stopped, no signal): no repaint work, about 0 % UI CPU.
- 60 fps scrolling the Mixer at 128 channels; Mixer strips, Inputs, Tracks, History and Changes virtualised.
- Input to visible response within one frame (≤ 16 ms) for faders, knobs, toggles, menus and page switches.
- UI RSS under 150 MB after visiting every page, no growth over a 30-minute soak, and 0 audio dropouts.
- A debug-only ⌥⌘P overlay with frame time, repaints per second, message-thread load and RSS.
