# DLIVE: reliability, workflow and intelligence — the four phases

The plan agreed on 2026-09-27, written down here so it is a thing you can open rather than a message in a
chat. One phase per working session, in order; a phase does not start until the one before it is merged and
its tests pass. Each phase writes what it learned into `docs/`, so the next one starts from facts.

| Phase | What it is | Status |
| --- | --- | --- |
| 1 | Session state foundation | **Done** — 2026-09-27, four commits, `b15b902`..`d1d96ce` |
| 2 | Mixing workflow | **5 of 6 done** by the v2 design work (2026-09-28); only device hot-plug is left |
| 3 | Mix features | Not started |
| 4 | Autopilot and the offline-model study | Not started; needs a CLAUDE.md decision first |

---

## Phase 1 — Session state foundation ✅

Canonical state, the save bug, device independence, autosave, recovery, persistent history.

The full audit and the contract are in **`docs/SESSION-STATE.md`**; the state flow is in
**`docs/ARCHITECTURE-DLIVE.md` §11**; the invariant is in **`CLAUDE.md`**.

What landed, in four commits:

1. `b15b902` — the audit. Why persistence kept breaking: the code that assembled the session document lived in
   `app/Main.cpp`, which no test target compiles.
2. `b6efd95` — one owned model. `SessionState`, `captureSession` / `applySession`, `rebuild()` split out of
   `prepare()`, `hold()` / `pending` deleted, saving driven by a revision, samples stored by name, `kVersion` 5.
3. `a09d287` — autosave on a worker thread, the crash-recovery sidecar and marker, persistent mix history and
   the Mix history sheet.
4. `d1d96ce` — device states named, output-only when the inputs will not open, the microphone answer taken
   from macOS rather than guessed.

**Left open, deliberately**, and picked up in Phase 2:

- `DawEngine::getProject()` still hands out a mutable reference, so about a dozen paths in `TracksPage` call
  `services.touchSession()` by hand instead of going through a choke point. TracksPage is being reworked in
  Phase 2 anyway; that is where this belongs.
- Device **hot-plug**: `AudioHost` still does not listen for device-list changes. That is Phase 2 item 6, and
  the `Disconnected` state it needs already exists.
- The *denied* microphone case has not been seen on a machine, only the granted one. Same code path.
- `scripts/benchmark-baseline.txt` does not match this Mac (+55 % on every row, including code nothing has
  touched). Not a regression — the commit before Phase 1 measures the same. It wants re-baselining from a CI
  artifact.
- RealtimeSanitizer cannot run on this Mac (Apple clang has no `-fsanitize=realtime`). CI's RTSan job is the
  check.

---

## Phase 2 — Mixing workflow — 0 of 6 done

**Items 1-5 were built once and are gone again.** They landed on 2026-09-27/28 as part of a v2
desktop redesign, because they *are* that design seen from the feature side - the scope picker,
the solo bar, the group panel, the ROUTING workspace and Text size. That redesign was scrapped
on 2026-09-28 and `app/ui` went back to where it stood on 2026-09-25; the features went with it.
The engine work they were built on did **not** go back and is still here, so each of them is a
UI job rather than an engine one when it is built again on the new design.

The code as it was is at the tag `v2-design-scrapped-2026-09-28`, if any of it is worth reading
rather than rewriting.


Read `CLAUDE.md`, `docs/SESSION-STATE.md`, `docs/DLIVE-APP.md`, `docs/DLIVE-DESIGN.md`. Every UI change is
verified with `dlive_ui_snapshots` PNGs. All new state goes through `SessionState` — if you find yourself
writing save/load code, stop and say so.

1. ⬅ **Tune Bus.** `startTuneBus()` exists and is reachable from MixPage but is hard to find. Make TUNE open
   with a clear scope picker: WHOLE MIX / ONE GROUP / SELECTED CHANNELS, groups listed by name. The Tune card
   always says which scope it ran on.
2. ⬅ **Solo you cannot miss.** A persistent indicator in the top bar, visible from every workspace, whenever
   anything (strip, bus, FX return) is soloed: which items, click to jump to one, one button to clear all.
   Keep "solo never changes what the room hears".
3. ⬅ **Group strip on the main mixer.** A fixed strip, always visible on MIXER and LIVE, with meter, fader, mute
   and solo for each group bus and the master. Must work on a 32+ channel session at the smallest supported
   window size.
4. ⬅ **Setup / Routing out of the everyday sidebar.** Move device choice, input assignment, output feeds,
   monitoring and saved input maps into one ROUTING workspace, reached deliberately. Under LIVE SAFE, changes
   there ask for confirmation. Keep the first-run flow working.
5. ⬅ **Larger text.** A View option (Standard / Large / Larger) scaling names, values, labels, buttons, menus,
   alerts and status text through `AppTheme` tokens — not the whole UI. Strip widths stay; names truncate with
   a tooltip. Snapshot a 32-channel session at every size.
6. ⬅ **Device hot-plug.** Listen to the device manager, rescan on change, reopen the session's device when it
   reappears, handle a mid-show disconnect with a sentence rather than silence or a crash. Mappings restored
   when the device returns.

---

## Phase 3 — Mix features

Read `CLAUDE.md`, `docs/SESSION-STATE.md`, `docs/DLIVE-MIX-ENGINEER.md`,
`docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md`. New state goes through `SessionState` and appears in the round-trip
test; each change that alters the sound creates a Mix history checkpoint.

1. **Lead and BGV as their own buses.** `MixBus` today is DRUMS BASS MUSIC VOCALS SPEECH AMBIENCE MASTER; the
   lead is a role and `MixPlanner`'s `focal`, but it is summed with the backing vocals. Add LEAD (keep VOCALS
   as BGV, renamed on screen only if that is safe for stored IDs). New groups go before MASTER,
   `busFromStoredIndex` remaps, bump `kVersion`. **Before splitting MUSIC into KEYS / GUITAR / OTHER, say what
   it costs — you were not sure it is worth it yet.** Profile numbers for the new bus in `MixProfileData.cpp`
   only. Tune and MixPlanner treat LEAD, BGV and SPEECH as distinct roles.
2. **Speaking / Singing on voice channels.** One-press starting points for pastor, host, MC, lead and guest
   mics. Tune starting points from profile data (`VocalStrategies` already has Lead and Speech), not frozen
   presets: everything stays editable and a Re-tune respects the choice.
3. **Reset Mix to Raw.** Every strip, bus, FX and the master back to the session's baseline; clears Tune
   results and sample replacement; keeps audio, clips, names, assignments, routing, scenes, reference and
   history. Takes a "Before reset" checkpoint first. Different from BYPASS, which stays untouched.
4. **Sample replacement, finished.** User samples imported from the strip (copied into the session folder so
   the session is portable), preview, and the 8-per-family cap removed or made clear. Kick, snare, toms only.
   *(Phase 1 already made the cap say so out loud and made the selection survive a moved slot.)*
5. **Favourite mixes.** Do not add a third system beside SCENES and REFERENCE MIX — extend them. "Mark as
   favourite" stores the mix plus a relationship profile computed from the capture at that moment (lead vs
   band, lead vs BGV, speech vs master, drum balance, bus levels relative to master, compression and EQ
   tendencies, master loudness and crest). Relationships measured from where sources land, not from fader
   positions (`RelationshipEngine` in `src/MixAI`). Versioned in the session. Tune can use the latest
   favourite as a target the way it uses a Reference. A list, not one slot.

---

## Phase 4 — Autopilot and the offline-model study

**Blocked on a decision from you.** `CLAUDE.md` currently says "only one thing moves a level by itself" and
"AI is never auto-applied". Autopilot needs that amended first, to something like:

> **Autopilot is the second thing allowed to move a level by itself.** It is deterministic (no AI), off by
> default, engaged only by explicit action, shown as AUTOPILOT ACTIVE on every workspace, bounded by
> `MixSafetyValidator`, limited to group and lead faders within ±N dB of the engaged mix, and every move is a
> Mix history entry with its reason. It never touches EQ, dynamics, the room, returns, or the engineer's listen.

1. **Autopilot (deterministic).** Engaging it snapshots the current mix's relationship profile (the Phase 3
   code) as the target. A worker thread — never the audio thread — reads the existing meters and analysis
   every few hundred ms and checks: lead fallen below its relationship to the band, speech intelligibility
   dropping, BGV above lead, one group suddenly dominating, clipping, master loudness outside the delivery
   range. **Within tolerance it does nothing — that is the default outcome and it is tested.** When it acts:
   smallest move, faders only, slow ramps, hysteresis, hard limits, validated. One-press off, and any engineer
   touch on a fader it moved hands that fader back. Each move is a Mix history entry: "Autopilot: Lead
   +1.2 dB. The lead fell below where you had it against the band."
2. **Offline model study — research only, no code in the product.** `docs/LOCAL-MODEL-STUDY.md`: which Mix
   Buddy / TUNE LIVE MIX requests a small model would handle better than `MixRequestParser` +
   `RelationshipEngine` today, with real example phrases (**if the answer is "few", say so**); candidate models
   (~1-4 B, quantized), runtimes on Apple Silicon, RAM, bundle size, cold-start and per-request latency on an
   M1 base, and the effect on dropouts; how it would run on demand, never continuously, never on the audio
   thread, failing without affecting the mix; and a recommendation — build, defer or drop — with the measured
   benefit that justifies it. macOS only.
