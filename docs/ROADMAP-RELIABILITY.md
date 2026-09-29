# DLIVE: reliability, workflow and intelligence — the four phases

The plan agreed on 2026-09-27, written down here so it is a thing you can open rather than a message in a
chat. One phase per working session, in order; a phase does not start until the one before it is merged and
its tests pass. Each phase writes what it learned into `docs/`, so the next one starts from facts.


| Phase | What it is                            | Status                                                                           |
| ----- | ------------------------------------- | -------------------------------------------------------------------------------- |
| 1     | Session state foundation              | **Done** — 2026-09-27, four commits, `b15b902`..`d1d96ce`                        |
| 2     | Mixing workflow                       | **Done** — 2026-09-28, five commits                                              |
| 3     | Mix features                          | In progress — 2026-09-28                                                         |
| 4     | Autopilot and the offline-model study | In progress — the invariant was amended on 2026-09-28                            |


---



## Phase 1 — Session state foundation ✅

Canonical state, the save bug, device independence, autosave, recovery, persistent history.

The full audit and the contract are in `docs/SESSION-STATE.md`; the state flow is in
`docs/ARCHITECTURE-DLIVE.md` **§11**; the invariant is in `CLAUDE.md`.

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



## Phase 2 — Mixing workflow ✅

Items 1-4 and Larger text were built once, as part of a v2 desktop redesign, and went with it when that
redesign was scrapped on 2026-09-28 and `app/ui` went back to where it stood on 2026-09-25. The engine work
they were built on did not go back, so each of them was a UI job when it was built again rather than an engine
one. The code as it was is at the tag `v2-design-scrapped-2026-09-28`.

All six landed on 2026-09-28, one commit each except the two that share MixController and MainView:

| Item | What it is | Commit |
| --- | --- | --- |
| Larger text | Standard / Large / Larger through `Dine::setTextScale`; the words grow, no metric does | `DLIVE: the words get bigger, the console does not` |
| 5 Device hot-plug | `AudioHost` listens; `deviceReturned()` is the rule; the session is untouched | `DLIVE: a console pulled out mid-service comes back by itself` |
| 1 Tune Bus | `MixPage::ScopeSheet` — the whole mix / one group / some channels, and `startTuneStrips` | `DLIVE: TUNE asks what to tune, and a solo says so from everywhere` |
| 2 Solo you cannot miss | `MainView::SoloBar`, on every workspace, from `MixController::getSoloed()` | (the same commit) |
| 3 Group strip | the group buses pinned beside the master on MIXER; LIVE already had its tiles | `DLIVE MIXER: the groups are where you can reach them, not seven screens away` |
| 4 ROUTING | `app/ui/RoutingPage` — five sections, reached deliberately, covered under LIVE SAFE | `DLIVE: set-up is one workspace now, and LIVE SAFE covers it` |

**What it cost along the way**, all of it found by looking at the PNGs rather than by reading layout code, and
all of it wrong before the change that exposed it: TUNE's pad card measured its body two pixels wider than it
drew it; the Inspector drew a caption and a value into one row on fixed widths; LIVE left a monitoring chip
lying where the last layout put it; the tooltip was measured in JUCE's face and drawn in DLIVE's; a pinned
mixer strip was laid out in the page's coordinates in LIST view and drawn over the tool row. A button now
gives up its padding and then its type size before it gives up a letter, because "M..." on a key says nothing.

**Two things moved, and the reachability test says so rather than quietly agreeing with the window**: the
three set-up rows left the everyday sidebar for one ROUTING row, and Outputs left its sheet for a section of
ROUTING. Nothing was removed.

Read `CLAUDE.md`, `docs/SESSION-STATE.md`, `docs/DLIVE-APP.md`, `docs/DLIVE-DESIGN.md`. Every UI change is
verified with `dlive_ui_snapshots` PNGs. All new state goes through `SessionState`.

---



## Phase 3 — Mix features

Read `CLAUDE.md`, `docs/SESSION-STATE.md`, `docs/DLIVE-MIX-ENGINEER.md`,
`docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md`. New state goes through `SessionState` and appears in the round-trip
test; each change that alters the sound creates a Mix history checkpoint.

1. ✅ **Lead and BGV as their own buses.** *(2026-09-28.* `MixBus` is DRUMS BASS MUSIC VOCALS SPEECH AMBIENCE
   LEAD MASTER; VOCALS is BGV on screen; `mixBusInDisplayOrder` is the console's order; `SessionStore::kVersion`
   6. The group balance is set against the lead, so adding a backing singer no longer moves the band.*)

   **MUSIC was not split into KEYS / GUITAR / OTHER, and here is what it would cost.** The split itself is
   cheap - the same four edits LEAD needed. What it costs is everything downstream of it. Three group faders
   where there was one means three rows in `busBelowVocalsDb` for each of the six profiles, and those numbers
   cannot be reasoned out: they are what a keyboard sits at against a guitar in a gospel mix, which is a thing
   you find by listening to a real service on a real desk. Every profile that has not been listened to with
   them would be guessing, and this file's own rule is that a profile has to be tunable by listening. It also
   costs the console two more strips in a rail that is already tight at 1180 px, and it costs an operator the
   one thing MUSIC is good at: "the band is too loud" is one fader today. **The case for it is real but
   narrower than LEAD's**: LEAD fixed a hierarchy rule that could not be expressed at all, while KEYS / GUITAR
   is a convenience for churches with a big band. Recommendation: defer until there is a multitrack of a church
   with three keyboard players and two guitarists to tune the numbers against - the same way the QUEENSVIEW
   recording settled the spill and master-bound work.

   The original note:
1. **Lead and BGV as their own buses.** `MixBus` today is DRUMS BASS MUSIC VOCALS SPEECH AMBIENCE MASTER; the
  lead is a role and `MixPlanner`'s `focal`, but it is summed with the backing vocals. Add LEAD (keep VOCALS
   as BGV, renamed on screen only if that is safe for stored IDs). New groups go before MASTER,
   `busFromStoredIndex` remaps, bump `kVersion`. **Before splitting MUSIC into KEYS / GUITAR / OTHER, say what
   it costs — you were not sure it is worth it yet.** Profile numbers for the new bus in `MixProfileData.cpp`
   only. Tune and MixPlanner treat LEAD, BGV and SPEECH as distinct roles.
2. ✅ **Speaking / Singing on voice channels.** *(2026-09-28. "This microphone is" on the MIXER strip's menu
   and the TRACKS header's menu: SPEAKING / SINGING LEAD / SINGING BACKING / CHOIR, from
   `MixController::voiceJobs`; `setInputRole` moves the input to its group and applies the profile's starting
   point for the new role.)* The original note:
2. **Speaking / Singing on voice channels.** One-press starting points for pastor, host, MC, lead and guest
  mics. Tune starting points from profile data (`VocalStrategies` already has Lead and Speech), not frozen
   presets: everything stays editable and a Re-tune respects the choice.
3. ✅ **Reset Mix to Raw.** *(2026-09-28. `MixController::resetMixToRaw`, Mix > Reset Mix to Raw, asked out
   loud and answered with what it keeps.)* The original note:
3. **Reset Mix to Raw.** Every strip, bus, FX and the master back to the session's baseline; clears Tune
  results and sample replacement; keeps audio, clips, names, assignments, routing, scenes, reference and
   history. Takes a "Before reset" checkpoint first. Different from BYPASS, which stays untouched.
4. ✅ **Sample replacement, finished.** *(2026-09-28. ADD A SOUND on the strip; decoded before it is copied;
   copied into `<session>/Samples/` so the session is portable; the cap is 24 rather than 8.
   `docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md` has it.)* The original note:
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

