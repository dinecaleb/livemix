# DLIVE v2 design: what exists, what differs, what is waiting

The audit that phase UI-0 produced, and the running record every later UI phase updates. One
row per Figma frame and per Figma component: the class that implements it today, what already
matches, what differs, what is new, and - for anything new whose behaviour does not exist yet -
which engine phase owns it.

The design file is **`2wv5QSnvrSSQXtDzfuShIn`** ("DLIVE Desktop v2", Sept 2026).
The baseline this is all measured against is `docs/design/baseline/`.

**Status: UI-0 done. No UI has changed yet.**

---

## 1. Two pages of the design file do not exist

The file's own index frame says `Pages: Screens · Components · Foundations · Handoff`.
The file has **two** pages: `Screens` (35 frames) and `Components` (9 components). There is no
`Foundations` page and no `Handoff` page.

What that costs, and what was done instead:

| Missing page | What it was to hold | Where the information came from instead |
| --- | --- | --- |
| Foundations | every colour variable with its `Dine::` code syntax, the 17 text styles, spacing / radius / layout numbers, contrast ratios | `get_variable_defs` on the frames themselves returns every bound variable with its `Dine::` name and every text style with family, weight, size, line height and tracking. §2 and §3 below are that dump. It is complete for what the frames use; a token the design defined but never applied would not appear. |
| Handoff | shell dimensions, channel-strip anatomy (y offsets), screen → code map, behaviour numbers, each marked "Code" or "Proposed" | The shell and strip numbers are quoted in the phase prompts themselves (title row 52, toolbar 56, sidebar 184, page bar 44, status foot 50; strip 48 × 680, fader travel 394 px, the seven scale stops). Exact y offsets are read per phase with `get_metadata` on the strip component. **The "Code" / "Proposed" marking is lost** - which is the one thing that cannot be recovered, so §6 is this document's own answer to it. |

Every frame is otherwise readable and every component is a real component with variants.

---

## 2. Tokens: AppTheme against the design's variables

`AppTheme.h` is already on the v2 values - `desk #070809`, `window #0e1014`, `card #1a1e26`,
`accent #6db8a8` and the rest match the design exactly. Four tokens the design uses are
missing, and one differs.

| Design variable | Value | In `AppTheme` today | Action (UI-1) |
| --- | --- | --- | --- |
| `Dine::lead` | `#f07f8f` | **missing** | add as `busLead`. The LEAD bus itself is engine work - see §6. |
| `Dine::fx` | `#7fc4d8` | **missing** | add as `busFx`. FX returns exist today, so the colour can be used at once. |
| `Dine::autopilot` | `#121b27` | **missing** | add as `autopilotGround`. Nothing uses it until Autopilot ships. |
| `Dine::scrim` | `#000000` | **missing as a token** | drawn today as `Colours::black.withAlpha(...)` in five places at 0.65 / 0.55 / 0.45 / 0.35. Make it a token; the alphas stay where they are except Appearance (see §6). |
| `Dine::on-accent` | `#0b0d10` | `onAccent #070809` | a 4-value difference on near-black type over teal. Move to the design's value. |
| `Dine::hair` | `#20252e` | `hair 0x14ffffff` | **no change.** 8 % white over `window #0e1014` resolves to `#212329`; the design's opaque value is the same hairline, flattened. The translucent token is better - it reads the same over every material. |

Every new token gets a default in every built-in theme and in `ThemeStore`'s read and write,
so every existing theme file still loads unchanged. `ThemeStore` carries 128 token keys today.

## 3. Type: 15 styles found, and Barlow Condensed is embedded but unused

| Style | Family | Weight | Size / line | Tracking |
| --- | --- | --- | --- | --- |
| Display/Wordmark | Barlow Condensed | 700 | 24 / 28 | 0.10 em |
| Heading/Page | Barlow | 600 | 17 / 22 | 0 |
| Heading/Card | Barlow | 600 | 14 / 18 | 0 |
| Body/Default | Barlow | 500 | 13 / 18 | 0 |
| Body/Small | Barlow | 500 | 12 / 16 | 0 |
| Body/Caption | Barlow | 500 | 11 / 14 | 0 |
| Label/Tab | Barlow Condensed | 600 | 13 / 16 | 0.08 em |
| Label/Control | Barlow Condensed | 600 | 12 / 14 | 0.05 em |
| Label/Strip | Barlow Condensed | 600 | 11 / 13 | 0.02 em |
| Label/Section | Barlow Condensed | 600 | 10 / 12 | 0.08 em |
| Label/Micro | Barlow Condensed | 700 | 9 / 10 | 0.06 em |
| Mono/Clock | IBM Plex Mono | 500 | 16 / 20 | 0 |
| Mono/Value | IBM Plex Mono | 500 | 12 / 16 | 0 |
| Mono/Small | IBM Plex Mono | 400 | 10 / 12 | 0 |
| Mono/Meter | IBM Plex Mono | 400 | 8 / 10 | 0 |

Figma writes `letterSpacing` as a percentage, so `Label/Section`'s 8 is 0.08 em - the same
number `Dine::caps` already defaults to.

Two problems, both in UI-1:

1. **Every `Label/*` style is Barlow Condensed, and no label in DLIVE uses it.**
   `BarlowCondensed-{Bold,Medium,SemiBold}.ttf` are embedded and
   `LiveMixLookAndFeel::condensed()` exists, but family 0 is deliberately mapped to Barlow:
   *"Labels share Barlow with body so tracking-heavy condensed caps don't dominate"*
   (`LiveMixLookAndFeel.cpp:23`). That was a decision for the plug-in surface; the design
   overrules it for DLIVE. This changes the width of every caps label in the app, so it is
   verified by looking at the PNGs at all four sizes, not by reasoning.
2. **Tracking is being clamped away.** `Dine::caps (px, tracking)` routes to
   `LiveMixLookAndFeel::body`, which does `jmin (spacingEm, 0.04f)`. So `caps(12, 0.08)` -
   the section label - renders at 0.04 em, half the design's tracking. `condensed()` does not
   clamp, so moving the label roles onto it fixes the tracking at the same time.

## 4. The screens

Read in full against the code: **00 INDEX, 02 ROUTING, 04 MIXER, 05 TUNE**. The rest are
entered from the frame name, the phase prompt's own description and the code as it stands;
each phase reads its own frames properly before building, and corrects its rows here.

| # | Frame | Implemented today by | Matches | Differs / new | Phase |
| --- | --- | --- | --- | --- | --- |
| 00 | INDEX | — | — | contents page, not a screen | — |
| 01 | SESSIONS + RECOVER | `SetupPages.cpp` `SessionsPage` | the library list, open, name, date | the Recover card | UI-7 |
| 02 | ROUTING | `DevicePage` + `AssignPage` + `OutputsSheet`, three separate places | every behaviour: device list, input map, saved maps (`InputMapStore`), output feeds, solo device, Dante | one workspace, three columns; APPLY confirms under LIVE SAFE; sample rate / buffer / reported latency card; input-access card | UI-4 |
| 02b | ROUTING · INPUT ACCESS REFUSED | `DeviceState` carries the sentence already | the refusal sentence exists | the card that shows it, and hot-plug | UI-4 |
| 03 | TRACKS | `TracksPage.cpp` | timeline, clips, markers, loop, row heights, arm, monitor, per-track TUNE | restyle only | UI-2/3 |
| 04 | MIXER | `MixerPage.cpp` | strips, linked faders, per-bus solo menus, FX returns, monitor bus, LIVE SAFE clamp, chain foot | strip is 48 × 680 with a fixed anatomy; 4 insert slots that open that stage in the Inspector; 2 px gain-reduction meter; group panel always on the right; horizontal scroll, 16 strips at 1440 | UI-3 |
| 05 | TUNE | `MixPage.cpp` | TUNE MIX, `startTuneBus`, TUNE CHANNEL, the decisions, the diff, the sentences, KEEP / REVERT / RE-TUNE | scope picker as one control; per-change UNDO; groups rail with last-tune time; **AIM AT ★favourite** and the **NOW / FAVOURITE** comparison; **SPEAKING / SINGING** per voice | UI-5 (+ §6) |
| 06 | LIVE | `LivePage.cpp` | scenes, the five macros, flat group faders, loudness, ON AIR | the Autopilot card | UI-5 (+ §6) |
| 07 | INSPECTOR · Sample replacement | `AdvancedPage` + `ChainEditor` | the Sample stage in full | restyle | UI-6 |
| 08 | MIX HISTORY (drawer) | `HistorySheet.cpp` | persistent checkpoints landed in Phase 1 (`a09d287`) | drawer styling | UI-7 |
| 09 | RESET MIX TO RAW | — | — | the whole thing, and a "Before reset" checkpoint | UI-7 (+ §6) |
| 10 | MIXER · LARGER TEXT | — | — | Text size 1.0 / 1.2 / 1.35, stored, in View > Appearance | UI-1 |
| 11 | PURPOSE & SOUND | `SetupPages.cpp` `PurposePage` | every field and effect | becomes a sheet; opens from the session name too | UI-4 |
| 12 | TUNE · LISTENING + MACRO PADS | `MacroPad.cpp`, `MacroRibbon` | BODY × VOICE, DRIVE × ROOM, ENERGY, snaps, LIVE SAFE fencing | restyle only - behaviour is not to move | UI-5 |
| 13 | TUNE CHANNEL | `ChannelTuneSheet.cpp` | all of it | restyle | UI-7 |
| 14 | CHECK INPUTS | `CheckSheet.cpp` | all of it | restyle | UI-7 |
| 15 | MIX BUDDY | `ChatSheet.cpp` | plan-before-apply, the refusal sentences | becomes a drawer | UI-7 |
| 16 | REFERENCE MIX | `ReferenceSheet.cpp` | all of it | restyle | UI-5/7 |
| 17 | INSPECTOR · EQ & DYNAMICS | `ChainEditor.cpp`, `EqCurveComponent` | the bands, the curve | bands drawn over the post spectrum | UI-6 |
| 17a–17k | INSPECTOR, stage by stage | `ChainEditor.cpp` | every control is an existing `ChannelParameters` field | one panel per stage, and the visualisations - see §5 | UI-6 |
| 18 | EXPORT | `MixBounce.cpp`, menu 105/106 | stereo WAV and MP3 | group stems, raw multitrack, range by markers, delivery loudness | UI-7 |
| 19 | APPEARANCE | `ThemeSheet.cpp` | themes, import, folder | text size added; scrim 25 % | UI-1/7 |
| 20 | FIRST SUNDAY (coach) | `Tutorial.cpp` | all of it | restyle | UI-7 |
| 21 | FAVOURITE MIXES | — | — | the whole library | UI-7 (+ §6) |

## 5. The components

| Component | Variants in the design | Today | Phase |
| --- | --- | --- | --- |
| Button | Primary / Secondary / Ghost / Danger / **Solo**, each M and S | `TextButtonV2`, `DinePopup`, `ToolbarToggle` cover the first four informally | UI-1/2 |
| Workspace Tab | Default / Active / **Deliberate** | `WorkspaceTab`, no Deliberate | UI-2 |
| Bus Tag | Lead / **BGV** / Speech / Drums / Bass / **Band** / **FX** / Ambience / Master, 18 % tinted ground | `Dine::busTint` gives the colour; no tag component | UI-3 |
| Status | OK / Warn / Crit / Monitor / Neutral, dot + plain word | drawn ad hoc in the status foot and device list | UI-2 |
| Segment | Off / On, inside a `menubar` track | `Dine::drawSegmentTrack` exists | UI-1 |
| Knob | 270° arc, value in Mono/Value, label in Label/Section, double-click returns to the plan value | `LiveMixLookAndFeel` knob | UI-6 |
| Channel Strip | Channel / Group × selected | `MixerPage` strip | UI-3 |
| Meter | 21 level steps, 4 × 394 | `MeterComponent` | UI-3 |
| Fader | 22 dB positions, 22 × 426 | `MixerPage` fader | UI-3 |

The Bus Tag component's own note says **"Lead is its own bus (new), BGV replaces VOCALS"** -
so the design's bus set is LEAD, BGV, SPEECH, DRUMS, BASS, BAND, FX, MASTER against today's
`MixBus` of Drums, Bass, Music, Vocals, Speech, Ambience, Master. That is §6's first row.

## 6. Waiting on engine

Nothing in this list is built until the engine phase that owns it is merged. Where a screen is
built before then, the part that depends on one of these is simply left out - never faked.

| What | Where it shows | Engine phase (`docs/ROADMAP-RELIABILITY.md`) |
| --- | --- | --- |
| **LEAD and BGV buses** (`MixBus` gains Lead; Vocals becomes BGV; Music reads BAND) | the group panel on 04, the groups rail on 05, every Bus Tag | Phase 3 · Mix features. `MixBus` is a **stored enum** - appended to, never reordered - and `SessionStore` bumps its version and remaps. |
| **Autopilot** | AUTOPILOT in the toolbar on every frame; the card on 06 | Phase 4 |
| **Favourite mixes / relationship profiles** | 21; AIM AT and the NOW / FAVOURITE comparison on 05 | Phase 3 |
| **Speaking / Singing per voice** | the QUICK START · VOICES card on 05 | Phase 3 |
| **Reset mix to raw**, with a "Before reset" checkpoint | 09 | Phase 3 |
| **Device hot-plug and input-access refusal as a live state** | 02b | Phase 1 landed `DeviceState` and the sentence; the *hot-plug* half is Phase 2 |
| **Autosave state and MIX HISTORY in the toolbar** | every frame's toolbar; 08 | **done** - Phase 1, `a09d287`. Build these in UI-2/UI-7. |

Two things the mockups show that are **not** waiting on anything and can ship at once: the
**global solo bar** (`MixController::clearSolos`, `numSoloed` exist) and **Export** beyond
stereo (`MixBounce` renders offline already).

### Visualisations that need a measurement that does not exist yet

UI-6 draws a scope for most stages. These read what the engine already publishes through
`AnalysisFifo` / `TripleBuffer` / the meter atomics. Each is confirmed or proposed when UI-6
reaches it; **none is drawn from invented data**, and anything needing new publishing gets it
in the same lock-free style, covered by `tests/AllocationTracker` and RTSan.

---

## 7. Decisions this audit could not make on its own

1. **⌘0 is already taken.** The design gives ROUTING ⌘0; ⌘0 is Zoom to Fit on TRACKS
   (command 607) and the reachability test now pins it. ROUTING is proposed as **⌘6** - the
   sixth tab, consistent with ⌘1–5 running left to right - leaving ⌘0 alone.
2. **The baseline PNGs are not committed.** 70 renders at 2× plus the takes they came from is
   340 MB, and `dlive_ui_snapshots` is not deterministic, so a committed PNG could not be
   diffed. `docs/design/baseline/README.md` holds the commands that put them back.
3. **RTSan cannot run on this machine.** `build-rtsan` was linked against an LLVM 21 in a
   scratchpad directory that has since been emptied, so every instrumented binary aborts in
   `dyld` before `main`. `scripts/rtsan.sh` reports this correctly (it exits 1). There is no
   real-time coverage until LLVM 21 is fetched again to a durable path. Nothing in UI-1 to
   UI-5 runs on the audio thread, but UI-3's gain-reduction meter and UI-6's scopes may add
   engine-side publishing, and that must not land without RTSan working.
4. **No screen exercises the speech group.** The reference recording's pastor microphone is
   silent, so SPEECH renders empty in every baseline image. 05 and 17k both lean on speech.
