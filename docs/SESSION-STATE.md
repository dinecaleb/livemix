# DLIVE session state: what it is, who owns it, and why it keeps breaking

Date: 2026-09-27. This is the audit that Phase 1 of the reliability work asked for, written before any
code changed. Baseline at the time of writing: `9cb3140`, `dlive_app_tests` 81/81 and `livemix_tests`
204/204 green.

The short answer to "why has persistence been re-fixed four times in a week" is at the bottom of §1:
**the code that keeps breaking is the only code in the app that no test can reach.** Everything else in
this document follows from that.

---

## 1. The findings, in the order they matter

### 1.1 The document is assembled by hand, in a file the tests do not compile

`app/Main.cpp` holds `HostServices`, a class in an anonymous namespace, and inside it the three functions
that own persistence:

| Function | `app/Main.cpp` | What it does |
| --- | --- | --- |
| `writeDocument (file)` | 490-521 | Builds a `SessionStore::Document` from ~14 getters on `MixController`, `DawEngine` and its own fields, then saves it |
| `hold()` | 464-481 | Builds a **second**, partial `Document` and parks it in `pending`, to carry the mix across a device change |
| `applyPendingMix()` | 443-461 | Pushes `pending` back into the controller, and only if the controller is prepared |

`app/CMakeLists.txt:32` lists `DLIVE_APP_SOURCES`, which `dlive_app_tests` compiles. `Main.cpp` is not in
it (it is added only to the `DLive` target, line 73). So:

- `SessionStore::toVar` / `fromVar` are tested — and pass.
- The code that decides **what goes into the Document in the first place** has never been executed by a test.

Every one of the four fixes landed in the untested half. That is the root cause, not a symptom of one.

### 1.2 `pending` is a third, competing copy of the session

`hold()` fills `pending` with seven fields and leaves the rest default:

```
snap.session   = controller.getPreparedSession();
snap.macros    = controller.getMacros();
snap.outputs   = controller.getOutputFeeds();
snap.tuneCount = controller.getTuneCount();
snap.hasMix    = controller.hasKeptMix();
snap.mix, snap.history                      (when hasMix)
snap.scenes    = controller.getScenes();
```

Not set, and therefore default: `project`, `inputDevice`, `outputDevice`, `soloDevice`, **`reference`**,
`tuneLive`, `trackPanelWidth`.

`applyPendingMix()` then writes some of those defaults back into the controller **unconditionally**:

```cpp
controller.setOutputFeeds (pending->outputs);
controller.setReference   (pending->reference);   // ← an empty ReferenceProfile after a hold()
controller.restoreScenes  (pending->scenes);
```

**This is a live bug.** `hold()` → open → `applyPendingMix()` is the path taken by `openDevices`,
`openOutputOnly`, `changeOutput`, `reconfigure` and `setSoloOutputDevice`. Every one of them therefore
**discards REFERENCE MIX**, and `setReference` fires `onMixChanged`, which arms the one-second debounced
save — so the loss is written to disk a second later without anyone touching Save. Choosing a different
output device, or adding one input on ASSIGN, silently throws away what the mix was aimed at.

This is not on the user's list of six. It is the same three lines as §1.4 and should be fixed with them.

### 1.3 The mix is only saved when the audio device is open — confirmed

`MixController::prepare()` is called from exactly three places, all in `AudioHost` (`AudioHost.cpp:162`,
`171`, `251`), and all three are device paths — `reconfigure()`, `audioDeviceAboutToStart()`. `prepare()`
is also what builds the mix at all:

```cpp
kept = startingPoint (session, engine.getGraph());   // MixController.cpp:235
```

So with no device open: `prepared == false`, `kept.numStrips == 0`, and `applyPendingMix()` returns at its
first line. The session opens, the assignments are listed, and there is no mix behind them — no faders, no
chains, no scenes, no reference, no output feeds, no strip history.

Then `writeDocument()` says:

```cpp
d.hasMix = controller.isPrepared() && controller.hasKeptMix();   // Main.cpp:501
```

`false`. And `toVar` writes `"mix"` only `if (d.hasMix)` (`SessionStore.cpp:483`). The kept mix is dropped
from the file.

Worth noting explicitly: `AudioHost::reconfigure()` **already** has a no-device prepare path
(`AudioHost.cpp:159-164`, `controller.prepare (controller.getSampleRate(), controller.getBlockSize())`,
with `sampleRate` defaulting to 48000 and `blockSize` to 64). `MixEngine::prepare` is pure C++ and needs
nothing from a device but a rate and a block size. The capability exists; nothing calls it when a session
opens without its console.

### 1.4 The fallback that should cover §1.3 is dead code — confirmed

`Main.cpp:517-518`:

```cpp
else if (pending.has_value()) d.tuneLive = pending->tuneLive;
else if (pending.has_value() && pending->hasMix) { d.hasMix = true; d.mix = pending->mix; ... }
```

The second condition is a strict subset of the first. The branch is unreachable under every input. So in the
§1.3 case the mix is *not* recovered from `pending`, and any of the 39 `saveSession()` calls overwrites the
file with `hasMix: false`.

**And it is worse than "any of the 39".** `DLiveApplication::shutdown()` (`Main.cpp:655`) calls
`saveSession()` unconditionally whenever the session has inputs. So the reproduction is:

> Unplug the interface. Open DLIVE (it reloads the last session through `last-session.txt`). Quit.
> The tuned mix, the scenes, the reference, the strip history and the output feeds are gone from the file.

No user action beyond launching and quitting. That is data loss on disk, and it is the sharpest thing in
this document.

A second consequence of the same two lines: after a `hold()`, `pending->tuneLive` is a void `var`, so the
first branch writes void, and `toVar` omits the property (`SessionStore.cpp:489`). A device change followed
by a save therefore also erases the stored TUNE LIVE MIX record.

### 1.5 Saving is triggered by hand from 39 call sites — confirmed, with a nuance

Exactly 39 `saveSession()` calls: `Main.cpp` 6, `MainView.cpp` 11, `TracksPage.cpp` 17, `MixerPage.cpp` 3,
`LivePage.cpp` 2. Plus one debounce: `MainView.cpp:809` sets `controller.onMixChanged = requestSave`, and
`requestSave()` (`MainView.h:56`) sets `saveTicks = 30`, drained at ~30 Hz in the tick (`MainView.cpp:1971`).

The nuance worth having before the rewrite: **mix mutations are actually well covered.** 41 sites in
`MixController.cpp` fire `onMixChanged`, and they include every strip, bus, FX, monitor, macro, scene, link,
undo and reference setter. What is *not* covered is everything else:

- `DawEngine` has **no change signal at all**. Every timeline mutation — arm, monitor mode, row height,
  clips, markers, loop, tempo — depends on the UI remembering to save.
- `MixController` setters with neither `onMixChanged` nor a reliable caller: `setSession`, `setSessionName`,
  `setInputName`, `setInputIcon`, `setPurpose`, `setProfile`, `setLiveSafe`, `setBypass`, `setKept`,
  `restoreScenes`, `keepPlanSelection`.

Three concrete paths that mutate and never save:

| Path | Where | What is lost until something else saves |
| --- | --- | --- |
| Drag one track's row height | `TracksPage.cpp:1987` | that track's `height` |
| Loop toggle on the transport bar | `TransportBar.cpp:197` | `loopEnabled`, `loopStart`, `loopEnd` |
| Purpose / Sound tiles on Set-up | `SetupPages.cpp:1793`, `1816` | `purpose`, `profile` |

In practice `shutdown()`'s save usually catches these — which is exactly why the bug in §1.4 is so
damaging: the one save that is guaranteed to run is the one that destroys the mix.

### 1.6 Sample selection is an index into a list that can move — confirmed, with a correction

`replaceSound` (`ChannelParameters.h:51`, `ParameterIDs.h:67`) is an `int` in `0..7` and a **released
parameter ID** used by the plug-ins as well (`ParameterSpecs.cpp:96`). It is serialised with the rest of the
channel through `forEachDspParameter`, so it round-trips faithfully — as a number.

`SampleLibrary::load()` (`SampleLibrary.cpp:40-93`) fills the slots:

1. built-in folder first (bundle `Contents/Resources/Samples`, else `DLIVE_SAMPLES_DIR`),
2. then `~/Music/DLIVE/Samples/<kick|snare|toms>`,

each pass sorting its own entries by `compareNatural` and appending into a per-family counter capped at
`SampleBankTable::kSounds == 8` (`SampleBank.h:43`).

**Correction to the premise.** Because the two passes are sequential, a *user* sample can never displace a
*built-in* one — user samples always land after them. What actually shifts an index is:

- adding, removing or renaming a **built-in** (shifts every user sample in that family down or up);
- adding, removing or renaming a **user** sample that sorts before another user sample in the same family;
- a file that fails to decode one day and succeeds the next (`decodeSound` returns `nullptr` and `continue`s
  *before* `++slot`, so a broken file consumes no slot);
- a family with nothing loadable falling back to three synthesised placeholders (`load()`, lines 52-62).

This matters for the Phase-1 test list: *"add a user sample that sorts first, reopen, same sound selected"*
**passes today** if there are no other user samples in that family. The test has to add two user samples and
then a third that sorts between them.

The cap is confirmed and silent: `if (slot >= SampleBankTable::kSounds) break;` — the ninth sound in a family
never loads and nothing says so.

Failure mode when an index no longer resolves: `MixEngine.cpp:214` calls `table->bank (family, replaceSound)`,
which returns `nullptr` past the loaded count, and the strip's Sample stage plays nothing. `ChainEditor.cpp:295`
and `:305` clamp the index for *display only*, so the Inspector shows the last loaded sound while the engine
plays silence. Silent divergence between what the screen says and what is heard.

### 1.7 UNDO / REDO on the mix is memory-only — confirmed

`MixController::history` and `future` are `std::vector<MixSnapshot>` (`MixController.h:561`), never
serialised, and cleared by `prepare()` (`MixController.cpp:252-253`) along with `stripHistory`, `tuneCount`,
`macros`, `mixed`, `lastCapture` and the chat. So the mix history is lost on quit **and** on any device
change or assignment change.

The per-strip tune history (`std::vector<StripTuneRecord>`, `MixHistory.h:21`) and the four scenes
(`MixScene`, `MixHistory.h:46`) *are* persisted, and carried across a rebuild by `carryStripHistory` /
`carryKept`. Confirmed.

### 1.8 What else I found

- **`prepare()` does two unrelated jobs.** It builds the audio graph *and* resets the session's mix state
  (lines 220-262: `kept`, `atCapture`, `plan`, `macros`, `bypassed`, `tuneCount`, `mixed`, `lastCapture`,
  `listened`, `history`, `future`, `stripHistory`, `liveKept`, `tuneLive`). Because only a device opening
  calls it, the lifetime of the session's state is tied to the lifetime of a device. That coupling *is*
  §1.3; `hold()`/`pending` exists only to work around it.
- **`LiveSafePolicy`'s numbers are not persisted.** `Project::liveSafe` (the on/off) is saved and restored
  through `DawEngine::setProject` → `controller.setLiveSafe`, and that path does **not** need a device. But
  the step limits (`maxFaderStepDb`, `maxMasterStepDb`, `maxInputGainStepDb`, `maxPanStep`,
  `maxSendStepDb`, and the reasoning bounds) live only in `MixController::safety` and reset to their
  defaults on every launch. Harmless today because nothing in the UI edits them; a trap the moment
  something does.
- **The session name lives in three places.** `MixController::session.name`, `DawEngine::session.name`, and
  the folder/file name derived from it by `SessionStore::fileFor`. `saveSessionAs` keeps them in step by
  hand, including rewriting every clip path (`Main.cpp:187-202`).
- **`SessionStore::save` is already atomic-ish.** `File::replaceWithText` writes a temporary file and
  replaces the target. It runs on the message thread, though, and a 32-channel session with a long strip
  history is a non-trivial JSON serialise inside the UI tick.
- **Migration coverage is one test, no fixtures.** `DawTests.cpp:1725` builds a version-3 document in code
  and checks the bus remap and the output feed. There is no version-1 or version-2 case, and no fixture
  files on disk. `mixFromVar` is robust in a way the header undersells: it remaps from `buses->size()` (the
  count actually in the file) rather than from the document version, so it survives a document whose
  version header is wrong.
- **Ad-hoc signing makes microphone permission unstable across rebuilds.** See §4.

---

## 2. The state table

Every piece of state that can change what the session sounds like or how it is wired. "Device?" means
*restore depends on an audio device being open*.

### 2.1 Session identity and setup — `MixSession`, owned by `MixController::session` (copy in `DawEngine::session`)

| State | Saved | Restored | Device? |
| --- | --- | --- | --- |
| `name` | ✓ `name` | ✓ | no |
| `profile` (`StyleProfileId`) | ✓ | ✓ | no |
| `purpose` (`MixPurpose`) | ✓ | ✓ | no |
| `delivery` (`DeliveryLoudness`) | ✓ | ✓ | no |
| `voicing` (`MasterVoicing`) | ✓ | ✓ | no |
| `inputs[]`: name, role, icon, `inputA`/`inputB`, `enabled` | ✓ `inputs` | ✓ | no |
| **focal / pinned input** (`inputs[].focus`) | ✓ `focus` | ✓ | no |
| **speech priority** (`speechPriority`) | ✓ | ✓ | no |

### 2.2 The kept mix — `MixController::kept`, a `MixParameters`, written as `"mix"`

Everything in this block is saved **only when a device is open** (§1.3) and restored **only when a device is
open** (`applyPendingMix`). That is the bug, not a property of the data.

| State | Saved | Restored | Device? |
| --- | --- | --- | --- |
| `numStrips` | ✓ | ✓ | **yes** |
| per strip: every DSP field via `forEachDspParameter` (filters, gate, **sample stage incl. `replaceSound`**, corrective EQ, de-esser, comp, transient, tone EQ, saturation, width, trim, limiter) | ✓ `channel` | ✓ | **yes** |
| per strip: `inputGainDb`, `faderDb`, `pan`, `mute`, `solo`, `sendDb[5]` | ✓ | ✓ | **yes** |
| per strip: `linkGroup` (**linked faders**) | ✓ (omitted when 0) | ✓ | **yes** |
| buses[7]: `channel`, `faderDb`, `mute`, `solo` | ✓ `buses` | ✓, remapped by count | **yes** |
| FX slots[5]: `FxParameters` via `forEachFxParameter`, `returnDb`, `enabled`, `solo` | ✓ `fx` | ✓ | **yes** |
| `fxReturnDb`, `fxMute` | ✓ | ✓ | **yes** |
| **tempo** (`tempoBpm`) | ✓ | ✓, clamped 20-300 | **yes** |
| **monitor / solo bus** (`mode`, `point`, `gainDb`, `mute`, `dim`, `dimDb`, `source`) | ✓ `monitor` | ✓ | **yes** |
| `speechDuck` | ✗ by design — recomputed in `compose()` from `session.speechPriority` + `MixProfile::speechPriority` | n/a | no |
| `bypassProcessing` | ✗ by design (BYPASS is a way of listening) | n/a | n/a |
| `broadcastDim`, `broadcastMute` | ✗ by design (a session must not open muted) | n/a | n/a |

### 2.3 Mix-level state beside the parameters

| State | Owner | Saved | Restored | Device? |
| --- | --- | --- | --- | --- |
| **macros** (5 values) | `MixController::macros` | ✓ `macros` | ✓, inside the `hasMix` branch | **yes** |
| `tuneCount` | `MixController` | ✓ | ✓ via `carryKept` | **yes** |
| **scenes** (4 × mix + macros + input names) | `MixController::scenes` | ✓ `scenes` | ✓ `restoreScenes` | **yes** |
| **REFERENCE MIX** (`ReferenceProfile`) | `MixController::reference` | ✓ `reference` | ✓ — **and wiped by any device change**, §1.2 | **yes** |
| **TUNE LIVE record** (`tuneLive` var + review lines) | `TuneLiveCoordinator` | ✓ `tuneLive`, read-only | ✓ | no (it is never re-applied) |
| **strip tune history** (`StripTuneRecord[]`) | `MixController::stripHistory[64]` | ✓ `history` | ✓ `carryStripHistory` | **yes** |
| **UNDO / REDO** (`MixSnapshot` stacks) | `MixController::history`, `future` | ✗ | ✗ — cleared by `prepare()` | n/a |
| Mix Buddy chat turns | `MixController::chat` | ✗ | ✗ | n/a |
| plan / preview / `compare` / `planSelection` | `MixController` | ✗ by design | n/a | n/a |
| `bypassed` | `MixController` | ✗ by design | n/a | n/a |
| **LIVE SAFE on/off** | `Project::liveSafe` | ✓ inside `project` | ✓ `setProject` → `setLiveSafe` | **no** |
| LIVE SAFE step limits (`LiveSafePolicy` numbers) | `MixController::safety` | ✗ | ✗ — defaults each launch | n/a |

### 2.4 Timeline — `Project`, owned by `DawEngine`

None of this is device-gated. All of it depends on a UI call site remembering to save (§1.5).

| State | Saved | Restored |
| --- | --- | --- |
| `sampleRate`, `tempo` | ✓ | ✓ |
| `tracks[]`: `armed`, `monitor` (`MonitorMode`), `height` | ✓ | ✓, re-matched by `syncTracks` |
| `tracks[].clips[]`: `name`, `file`, `start`, `offset`, `length`, `fileSampleRate` | ✓ | ✓ |
| `markers[]` | ✓ | ✓ |
| `loopEnabled`, `loopStart`, `loopEnd` | ✓ | ✓ |
| `folder` | ✗ — derived from where the document was found | ✓ |
| unfinished takes | `<take>.wav.recording.json` sidecars beside the audio | ✓ `recoverUnfinishedTakes()` |

### 2.5 Devices, outputs and monitoring

| State | Owner | Saved | Restored | Device? |
| --- | --- | --- | --- | --- |
| **output feeds** (count; per feed `left`, `right`, `source`, `gainDb`, `mute`, `mono`, `monitor`) | `MixController::outputs` | ✓ `outputs` | ✓ `setOutputFeeds` | **yes** |
| input device (the console) | derived: `Main::consoleInputDevice` else `host` | ✓ `inputDevice` | ✓ via `planDevicesForSession` | n/a |
| output device (the broadcast) | derived: `Main::broadcastDevice` else `host` | ✓ `outputDevice` | ✓ via `planDevicesForSession` | n/a |
| solo device | `Main::soloDevice` | ✓ `soloDevice` | ✓ `restoreSolo`, needs `host.isOpen()` | **yes** |
| the DLIVE-built aggregate device | CoreAudio | ✗ by design | rebuilt from `soloDevice` | n/a |
| whether solo actually goes anywhere | derived from `hasMonitorFeed(outputs)` | n/a | n/a | n/a |

### 2.6 Preferences

| State | Where | Scope |
| --- | --- | --- |
| TRACKS channel-panel width | `Main::panelWidth` → `trackPanelWidth` in the document | per session |
| theme | `~/Music/DLIVE/preferences.json`, `"theme"` | per Mac |
| saved input maps | `~/Music/DLIVE/Input Maps/*.json` (`InputMapStore`) | per Mac, separate documents |
| last session pointer | `~/Library/Application Support/DLIVE/last-session.txt` | per Mac |

---

## 3. How state reaches the audio thread today

```
UI / Tune / Mix Buddy / macros / scenes / undo
        │  (message thread, every setter)
        ▼
MixController::kept          the mix without macros — baselines, then the kept plan, then hand edits
   + macros, voicing, speechPriority, bypassed, plan/BEFORE/AFTER
        │  MixController::compose()      one place decides what is audible
        ▼
MixController::running       = compose()
        │  MixController::publish()  →  MixEngine::setParameters (running)
        ▼
TripleBuffer<MixParameters>  (src/Core/TripleBuffer.h)   wait-free both sides
        │  the audio thread checks the version once per block
        ▼
MixEngine::process()         strips → buses → FX returns → master → output feeds
```

`OutputFeeds` has its own `TripleBuffer` (`MixEngine.h:180`) because changing where the sound leaves the
device is not a mix change.

Who writes into `kept`:

| Writer | Entry point |
| --- | --- |
| TUNE MIX / TUNE CHANNEL / TUNE <GROUP> | `keepPlan`, `keepPlanSelection` |
| TUNE LIVE MIX, Mix Buddy | `applyLiveProposal` → the same `kept` |
| macros, voicing | not `kept` — applied in `compose()` on top of it |
| scenes | `recallScene` replaces `kept` + `macros` wholesale |
| the Inspector and the mixer | `setStrip*` / `setBus*` / `setFx*` |
| UNDO / REDO | `applySnapshot` |
| session restore | `setKept` / `restoreKept` / `carryKept` |
| `prepare()` | **resets** it to `startingPoint (session, graph)` |

Two things to preserve through the rewrite: `compose()` staying the single place that decides what is
audible, and `publish()` staying the single place that hands it over.

---

## 4. Why a refused microphone blocks DLIVE

Asked for in Phase 1 step 4. What I could establish from the code and the built bundle:

**Enumeration does not need permission, and is not the problem.** `AudioHost::listInputDevices()` uses
`type->scanForDevices()`, `type->getDeviceNames(true)` and `type->createDevice({}, name)` followed by
`getInputChannelNames().size()`. Those are CoreAudio property queries; none of them starts a stream, and
none is gated by TCC. Devices will list whatever the permission says.

**Input and output are opened in one call, and the call is all-or-nothing.** `AudioHost::open()`
(`AudioHost.cpp:72-95`) sets `inputDeviceName` *and* `outputDeviceName` on one
`AudioDeviceManager::AudioDeviceSetup` and passes it to `setAudioDeviceSetup`. If the input half cannot be
opened, the whole call returns an error string and `getCurrentAudioDevice()` is null — so DLIVE has no
output either. There is an `openOutputOnly()`, and `planDevicesForSession` (`DevicePlan.h`) already uses it
when the session's *input device is absent*, but nothing retries output-only when a **present** input device
**fails to open**. That is the gap.

**The permission plumbing is right for a shipped build and wrong for a local one.** `app/CMakeLists.txt:90-92`
sets `HARDENED_RUNTIME_ENABLED`, `MICROPHONE_PERMISSION_ENABLED` and the usage text.
`NSMicrophoneUsageDescription` does reach the built `Info.plist` — verified. But JUCE's
`_juce_add_xcode_entitlements` (`JUCEUtils.cmake:685-702`) applies the entitlements file and the hardened
runtime through `XCODE_ATTRIBUTE_*` properties, which the **Ninja** generator ignores. `codesign -dvvv` on
`build/app/DLive_artefacts/Release/DLIVE.app` reports `flags=0x2(adhoc)`, no `runtime` flag, no
entitlements, `TeamIdentifier=not set`. `scripts/package.sh` signs properly with
`scripts/DLIVE.entitlements` (which does grant `com.apple.security.device.audio-input`), so the packaged
build is fine.

The consequence for the everyday `scripts/dlive.sh` loop: an ad-hoc signature has no stable identity, and its
code-directory hash changes on every rebuild. macOS keys TCC grants to that identity, so a grant given to
yesterday's build does not necessarily apply to today's — the prompt returns, or the grant silently stops
matching. That is the most likely explanation for "it worked and then it did not" on a development build, and
it is a signing problem rather than a code one.

**What I have not established, and how to.** Whether a *denied* grant makes `setAudioDeviceSetup` fail, or
makes it succeed and deliver silence, is a runtime question. `dlive_device_check` is the right tool: run it
with the grant revoked (`tccutil reset Microphone com.dine.dlive`) and record the exact return of
`AudioHost::open` and whether the callback receives non-zero input. I have not run it, because it needs a
decision about resetting a real TCC grant on this Mac. Step 2 should not guess: the device-state model below
has a distinct `InputRefused` state either way, and the sentence differs.

---

## 5. The canonical model proposed for step 2

### 5.1 Shape

One owned aggregate in `app/native/SessionState.{h,cpp}`, JUCE-free in its core fields so it is unit-testable,
and the only thing `SessionStore` serialises:

```
struct SessionState
{
    MixSession        session;      // who is what, purpose, sound, delivery, voicing, focal, speech priority
    MixParameters     mix;          // the kept mix — valid with or without a device
    MixMacroValues    macros;
    ReferenceProfile  reference;
    Project           project;      // the timeline
    OutputFeeds       outputs;
    DeviceChoice      devices;      // console input, broadcast output, solo output (what the user picked)
    LiveSafePolicy    safety;       // the on/off and the numbers
    std::array<MixScene, kMixScenes>                     scenes;
    std::array<std::vector<StripTuneRecord>, kMaxStrips> stripHistory;
    std::vector<MixCheckpoint>                           checkpoints;   // step 3
    SampleChoice      samples[kMaxStrips];                              // step 2, §5.4
    int    tuneCount = 0;
    int    trackPanelWidth = 0;
    juce::var tuneLive;             // the record, read-only
    uint64_t revision = 0;          // bumped by every mutation
};
```

`MixController`, `DawEngine` and the device fields in `Main` read from and write to it. Nothing assembles a
`Document` from getters. `hold()` and `pending` go away: carrying state across a graph rebuild becomes
`carryMix`/`carryStripHistory` applied *inside* the state, which is where they belonged all along.

### 5.2 Splitting `prepare()`

The single change that removes the class of bug:

- `SessionState::rebuild()` — pure, no device: recompute `mix` baselines from `session` via `startingPoint`,
  carry the surviving strips across with `carryMix`, keep scenes, reference, feeds, macros, history. Called
  when the assignments change, and once when a session is loaded.
- `MixController::prepare (rate, block)` — builds the audio graph from the state and publishes. It must
  never decide whether the state exists, and must never clear it.

This split is cheap, because the routing and the baselines are already rate-free:
`RoutingGraph::build (const MixSession&)` (`RoutingGraph.h:37`) and
`startingPoint (const MixSession&, const RoutingGraph&)` (`RoutingGraph.h:47`) take no sample rate at all.
Only `MixEngine::prepare`, which allocates the DSP objects, needs one. So `rebuild()` is genuinely
device-free and rate-free: a session that opens with nothing plugged in gets its full mix, shows it, edits
it and saves it, and the graph is built for whatever rate a device eventually arrives with.

### 5.3 Saving driven by revision

Every mutation bumps `revision`. One owner watches it (`SessionAutosave`) and decides when to write. The 39
`saveSession()` calls and the `onMixChanged`/`requestSave` debounce both go away; `DawEngine` stops needing
a change signal it does not have. Manual Save stays, as a forced write.

### 5.4 Sample selection by identity

`replaceSound` is a released parameter ID and stays exactly as it is — an index, `0..7`, with the same
meaning to the engine and to the plug-ins. The identity is stored **beside** it, per strip:

```json
"sampleChoice": { "family": "kick", "name": "Ludwig 24 Soft", "source": "builtin" }
"sampleChoice": { "family": "snare", "name": "My Snare", "source": "user", "path": "Snare/My Snare.wav" }
```

On load, `SampleLibrary` is asked for the slot that currently holds that family + name + source; the answer
becomes `replaceSound`. When the name is gone, the strip reports it in a sentence — *"The kick sound
'Ludwig 24 Soft' is not in your samples folder any more, so this strip is back on its own microphone."* —
and the sample stage is switched **off** rather than left pointing at whatever sits in that slot. Migration
from a document with no `sampleChoice`: read the index, resolve it against the library **as it is at load
time**, and write the identity back. That is the best available answer and it is right for every user who has
not changed their samples folder since.

The 8-slot cap is a separate decision and belongs in Phase 3; step 2 only needs to stop the cap being silent.

### 5.5 Device state, modelled

```
enum class DeviceStage { Absent, Present, Selected, ChannelsKnown, OutputOpen, Open, InputRefused, Disconnected };
```

`Absent` and `Present` come from enumeration, which needs no permission. `OutputOpen` is the new state that
§4 asks for: DLIVE plays and mixes, and says in one sentence that macOS is not letting it hear the inputs.
`Disconnected` is what `deviceStoppedUnexpectedly()` already detects.

### 5.6 Version and migration

`SessionStore::kVersion` 4 → 5. Every earlier version opens:

| From | What has to happen |
| --- | --- |
| 1 | no timeline; `project` empty; document in `legacyFolder()`, so `project.folder` stays empty |
| 2 | timeline present; four group buses, master at stored index 4 |
| 3 | five group buses, master at 5; no monitor block, so the monitor takes its defaults |
| 4 | six group buses, master at 6; sample indices with no identity → resolve against the library at load and write the identity back |

`mixFromVar`'s existing behaviour — remapping from the bus count actually present in the file rather than
from the version header — is the right rule and should be kept.

### 5.7 Realtime invariants

Nothing in this touches the audio thread. `compose()` and `publish()` keep their jobs and stay the only path
to `MixEngine`. Autosave serialises from an **immutable snapshot** taken on the message thread and writes on
a background thread, so the UI tick stops carrying a JSON serialise. `SessionState` is message-thread-owned;
the audio thread continues to see only the published `MixParameters` and `OutputFeeds` triple buffers.

### 5.8 Migration risks

| Risk | Why | Mitigation |
| --- | --- | --- |
| A version-5 document read by a version-4 build | The user may have two builds | Keep `hasMix`/`mix` in the same shape, add fields; a v4 reader ignores what it does not know |
| Removing the 39 call sites too early | A missed revision bump becomes a silent regression | Land the revision counter *first*, leave the 39 calls in, assert in a debug build that no explicit save ever finds unsaved work, then delete them |
| Splitting `prepare()` changes what a rebuild clears | `prepare()` clears the listen, the plan, the chat and the mix history on purpose | Keep exactly that list in `rebuild()`, and keep the existing test at `AppTests.cpp:664` ("editing the session keeps the sound; preparing again rebuilds the graph") green |
| A device arriving after the state was rebuilt | `prepare()` currently overwrites `kept` with `startingPoint`, so the first device to open would wipe the mix the user has been editing | `prepare()` must build the graph and publish only; the existing rate/buffer matrix test (`MixEngineMatrixTests`) covers that the graph is happy at any rate, and a new test opens a device *after* editing a mix and checks `countParameterChanges == 0` |
| Sample identity resolved against a library that has not loaded yet | `SampleLibrary::load()` runs before the session in `initialise()` — but only there | Make the resolve explicit and ordered, and test the reverse order |
| `pending` removed while `setSoloOutputDevice` still rebuilds devices | That path calls `hold()` five times over | Rewrite it against `SessionState` in the same commit; it has a test already (`Monitoring:` cases) |

---

## 6. The test list for step 5

In `app/Tests` (`dlive_app_tests`). The first requirement is structural: **the document assembly must move
out of `Main.cpp` into `DLIVE_APP_SOURCES`**, or none of the rest can be written.

1. **Full round trip, device open.** Build a state touching every row of §2 — faders, pans, mutes, solos,
   every chain stage on at least one strip, sends, every FX slot, bus chains, the master limiter, all five
   macros, a link group, LIVE SAFE on with non-default limits, speech priority, a pinned focal input,
   delivery, voicing, four scenes, a reference, strip history, checkpoints, output feeds including a monitor
   feed, tempo, and a sample selection on kick / snare / tom including one user sample. Save, destroy
   everything, load, compare field by field, and compare the `MixParameters` `MixEngine` ends up with
   (`countParameterChanges == 0`).
2. **The same round trip with no device open at any point.** Nothing missing, nothing default.
3. **Open without a device → save → reopen with a device: nothing lost.** This is §1.3/§1.4 and must fail
   against `9cb3140`.
4. **Launch and quit with no device does not change the file.** Byte-compare the document across the
   `shutdown()` save. This is the reproduction in §1.4.
5. **A device change preserves the reference, the scenes and the TUNE LIVE record.** This is §1.2 and must
   fail against `9cb3140`.
6. **Sample identity.** Two user samples in a family; reopen and confirm the same sound. Then add a third
   that sorts between them, reopen, and confirm the same sound again. Then remove the chosen one and confirm
   the strip reports it and switches the stage off rather than playing a different sound.
7. **Sample cap.** A family with nine sounds says so.
8. **Every older `kVersion` opens.** Fixture documents for 1, 2, 3 and 4 in `app/Tests/fixtures/`, each
   checked for bus mapping, monitor defaults, timeline presence and sample migration. Keep
   `DawTests.cpp:1725` as the in-code v3 case.
9. **Autosave and recovery.** Revision bumps write; ~2 s of quiet coalesces to one write; Tune / scene
   recall / reference change write immediately; a simulated crash (autosave present, clean-exit marker
   absent, autosave newer) offers recovery and recovers exactly.
10. **Mix history persists.** Checkpoints survive a save/load, are bounded and pruned oldest-first with Tune
    checkpoints kept longest, and restoring one is itself a checkpoint.
11. **The completeness test.** Walk every field through `forEachDspParameter` and `forEachFxParameter` and
    fail if the serialised JSON does not contain its id. This is what makes "a field was added and not
    saved" impossible rather than unlikely.
12. **Device states.** `planDevicesForSession` keeps its cases; add output-only fallback when a present input
    device fails to open, and `InputRefused` carrying its own sentence.
13. **Unchanged:** `tests/reference/*.f32` renders, `scripts/rtsan.sh`, and the benchmark against
    `scripts/benchmark-baseline.txt`.

---

## 7. What was built (2026-09-27)

Step 2 of the phase, all of §5 except where noted. `dlive_app_tests` 90/90, `livemix_tests` 204/204, every
product builds, the UI snapshots render unchanged.

| §5 | Built | Where |
| --- | --- | --- |
| 5.1 One owned model | yes | `app/native/SessionState.h`; `SessionStore` serialises nothing else. `SessionStore::Document` is an alias for it |
| 5.1 One capture, one apply | yes | `captureSession()` / `applySession()` in `SessionState.cpp`, both in `DLIVE_APP_SOURCES` and therefore tested. `hold()`, `pending`, `applyPendingMix()` and `holdMix()` deleted |
| 5.2 Splitting `prepare()` | yes | `MixController::rebuild()` (pure, no device, no rate) and `prepare()` (the audio graph only). `resetDocument()` is the blank slate a different document is loaded onto |
| 5.3 Saving by revision | yes | `MixController::touch()` / `getRevision()`; `MainView`'s tick writes a second after the revision stops moving. 39 `saveSession()` calls became 3 |
| 5.4 Sample identity | yes | `SampleChoice` + `SampleLibrary::slotFor()`; `readSampleChoices` / `resolveSampleChoices`. The cap says so now (`whatWasLeftOut()`) |
| 5.5 Device state, modelled | **no** | `DeviceChoice` is stored, but the `DeviceStage` enum and the output-only fallback on a refused input are Phase 2 work with the hot-plug listener |
| 5.6 Version 5 + migration | yes | `kVersion = 5`; a test downgrades this build's document to 1, 2, 3 and 4 and opens each |
| 5.7 Realtime invariants | yes | Nothing new on the audio thread; `compose()` → `publish()` → `TripleBuffer` untouched. `MixEngine::getStrip()` gained a bounds check, which it needed the moment the UI could draw a strip with no device open |

Two things were found while building it that were not in the audit:

- **LIVE SAFE was rewriting the document it protects.** Arming it before restoring a session clamped the
  restored macros to `50 ± maxMacroExcursion` and could refuse the stored output feeds. `applySession()` arms
  it last: it bounds what a *person* may move mid-service, not what a document contains.
- **`MixEngine::getStrip()` read past the end of its vector** as soon as the session's graph could be wider
  than the engine's - which is the normal state of a session opened with nothing plugged in.

Still open from this phase:

- **The timeline has no choke point.** `DawEngine::getProject()` hands out a mutable reference and
  `TracksPage` edits it in place in about a dozen places, so those paths call `services.touchSession()` by
  hand. `refresh()`, `setProject()`, `setLoop()` and `setSession()` bump on their own. Making `Project`
  mutation go through `DawEngine` belongs with the TracksPage work in Phase 2. A missed bump now delays a
  save rather than losing one - the next bump writes the whole document - which a missed `saveSession()` did
  not.
- **Step 3** (autosave on a background thread, the crash-recovery sidecar and marker, persistent mix history)
  and **step 4** (device states, output-only on a refused input) are not started. The serialiser they need is
  in place and takes one `SessionState`.
- The microphone-permission question in §4 still needs the runtime experiment described there.

---

## 8. What this document commits to

- `SessionState` is the one owned model; `SessionStore` serialises nothing else.
- `prepare()` builds a graph. It never decides whether the session exists.
- Saving follows a revision, not a call site.
- Sample selection is stored by identity, and `replaceSound` keeps its released meaning.
- Nothing here touches the audio thread, and `compose()` → `publish()` → `TripleBuffer` stays the only path
  to `MixEngine`.
