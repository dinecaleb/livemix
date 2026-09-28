# DLIVE v2 design: what exists, what differs, what is waiting

The audit that phase UI-0 produced, and the running record every later UI phase updates. One
row per Figma frame and per Figma component: the class that implements it today, what already
matches, what differs, what is new, and - for anything new whose behaviour does not exist yet -
which engine phase owns it.

The design file is **`2wv5QSnvrSSQXtDzfuShIn`** ("DLIVE Desktop v2", Sept 2026).
The baseline this is all measured against is `docs/design/baseline/`.

**Status: UI-0 to UI-6 done (UI-5 has two items left, §3e). UI-7 part done (§3g). New sources and the guides, §3h.**

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
`accent #6db8a8` and the rest match the design exactly. Five tokens the design uses were
missing, and one differed.

| Design variable | Value | Was | Now |
| --- | --- | --- | --- |
| `Dine::lead` | `#f07f8f` | missing | **added** as `busLead`. The LEAD bus itself is engine work - see §6 - so nothing draws with it yet. |
| `Dine::fx` | `#7fc4d8` | missing | **added** as `busFx`. FX returns exist today, so UI-3 can use it at once. |
| `Dine::autopilot` | `#121b27` | missing | **added** as `autopilotGround`. Unused until Autopilot ships. |
| `Dine::scrim` | `#000000` | drawn as `Colours::black.withAlpha(...)` in five places | **added** as a token. The alphas belong to the thing being opened, not to the token, and are unchanged; Appearance moves 0.35 → 0.25 in UI-7. |
| `Dine::on-hot` | `#17150a` | missing | **added** as `onHot` in UI-2 - the type on the solo bar. Daylight gets a light value instead: its `hot` is dark enough that near-black type would vanish on it. |
| `Dine::on-accent` | `#0b0d10` | `onAccent #070809` | **changed** to the design's value. |
| `Dine::hair` | `#20252e` | `hair 0x14ffffff` | **no change.** 8 % white over `window #0e1014` resolves to `#212329`; the design's opaque value is the same hairline, flattened. The translucent token is better - it reads the same over every material. |

All five new tokens are in every built-in theme and in `ThemeStore`'s read and write, taking it
from 128 token keys to 133. `busLead`, `busFx` and `autopilotGround` are given their own values
in Lime Desk, Tape and Daylight rather than inherited, because a dark Autopilot ground would be
a hole in a light desk. Every older theme file still loads unchanged: a key a file does not
carry is resolved from the default.

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

Both problems found here are **fixed**:

1. **Every `Label/*` style is Barlow Condensed, and no label in DLIVE used it.**
   `BarlowCondensed-{Bold,Medium,SemiBold}.ttf` are embedded, but `typefaceFor` never loaded
   them: `LiveMixLookAndFeel::condensed()` maps its label family to plain Barlow on purpose -
   *"Labels share Barlow with body so tracking-heavy condensed caps don't dominate"*
   (`LiveMixLookAndFeel.cpp:23`). That is right for a plug-in panel and wrong for DLIVE. So
   `Dine::condensed` loads the face in `AppTheme.cpp` instead, and **the six plug-ins look
   exactly as they did.**
2. **Tracking was being clamped away.** Both `body()` and `condensed()` do
   `jmin (spacingEm, 0.04f)`, so `caps(10, 0.08)` - the section label - rendered at half the
   design's tracking. `Dine::condensed` does not clamp.

`Dine::caps` now routes to `Dine::condensed`, which moves all 61 existing call sites onto the
design's family and tracking in one edit rather than by hand. The 15 roles in `Dine::Type` are
the named API pages should ask for from here on.

**It cost nothing.** Condensed labels lay out *less* than Barlow ones, so the full-repaint cost
went down on every page: TRACKS 15.19 → 12.96 ms, MIXER 8.43 → 8.00, TUNE 17.96 → 15.52,
LIVE 11.23 → 10.01, INSPECTOR 33.75 → 31.67. Per-tick cost is unchanged at 1.28 ms.

### Text size

`Standard 1.0 / Large 1.2 / Larger 1.35`, in **View > Appearance > Text Size**, stored in
`~/Music/DLIVE/preferences.json` beside the theme - a preference of the Mac, never of the
session. It scales the type roles and nothing else: strip widths, row heights and meters keep
their pixels, so a 32-channel console is still a 32-channel console and a name that no longer
fits gets an ellipsis. A size the build does not offer (a later version's, or a hand edit)
reads back as Standard, so no file can leave somebody at a scale no menu gets them out of.

The wordmark is a mark, not a name, so its box is measured from its type rather than fixed at
58 px - at Larger it read `DLI...`. That measurement is cached per text size: measuring a
string builds a glyph layout, and doing it on every title-row paint cost **15-18 % of the
frame budget** on all five pages before it was caught.

Not yet in the Appearance *sheet* - only in the menu. The sheet is restyled to frame 19 in
UI-7 and the control lands there then, rather than being built twice.

## 3b. UI-2: the application shell

Every frame of the design shares one shell, and it is now the shell DLIVE has.

**Title row (52).** The wordmark, then the **session's name**, then the six tabs, then the
counts and MIX BUDDY. ROUTING comes first as the design's **Deliberate** variant - outlined,
and set apart by a divider - because it is not a mixing workspace: what happens there changes
what the room hears. The active tab is a filled accent pill carrying near-black type, which is
the design's Active variant and replaces the underline DLIVE used.

Putting the session's name back gave `setupPopover` a caller again. It had had **none** since
the name left the row on 2026-09-18 - a whole menu (new, open, save, save as, import, reference
mix, input mappings, recording destination, getting started) that no longer had a way in.

**Toolbar (56).** The autosave state and MIX HISTORY on the left, the transport in the middle,
then the solo bar, BYPASS, LIVE SAFE and the output on the right.

- **The solo bar** needed no engine work and is built. Solo goes to the engineer's own device,
  so the room and the stream carry on exactly as before and nothing else on the console says
  why the engineer is listening to one microphone. It appears on **every** workspace whenever
  a strip, a group or an FX return is soloed, names them, goes to the first of them on click,
  and clears everything with one press. `dlive_ui_tests` asserts all of that, on all five
  workspaces.
- **The autosave state** is real: `SessionAutosave::lastWrite()` was added (an atomic the
  worker sets after a write lands) and reaches the UI through `AppServices::lastAutosave()`.
  Before this, nothing anywhere said the session had written itself down.
- **MIX HISTORY** had no menu item at all - only two buttons inside MixPage and LivePage. It
  is now **View > Mix History** (command 633), the toolbar button and a SAFETY row.
- **AUTOPILOT** is not built: it waits on its engine phase (§6).

**Sidebar (184).** LIBRARY (Sessions), WORKSPACE (the five), SAFETY (Mix history, Scenes), and
the audio device along the foot under an AUDIO DEVICE caption with **CHANGE IN ROUTING**. The
SET-UP rows left it, as the design asks. Nothing became unreachable: the ROUTING tab, ⌘6,
**View > Setup** (613) and that foot button all go to the same place. Favourite mixes waits on
its engine phase.

**Status foot (50).** AUTOSAVE added at the end of the row.

### Two deviations from the design, both deliberate

1. **ROUTING is ⌘6, not ⌘0.** ⌘0 is Zoom to Fit on TRACKS and the reachability test pins it.
   Taking a shortcut away from something that already has it is the one thing this work is not
   allowed to do, and ⌘6 follows ⌘1-5 running left to right anyway.
2. **DIM and MUTE stay on the toolbar**, though the design's toolbar does not show them. They
   were put there on purpose (`f26eacb`, "before the service") as the emergency keys a hand
   finds without reading. An emergency key in a menu is not an emergency key. **TUNE LIVE MIX**
   did leave the row, as the design asks - it is on TUNE and in the Mix menu - except while it
   is *running*, when it stays as the global stop, because a thing that is moving the mix by
   itself has to be stoppable from wherever you are standing.

### The frame cost, and a regression to accept or reject

| | tick ms (baseline) | full repaint ms (baseline) |
| --- | --- | --- |
| TRACKS | 1.30 (1.26) | 14.52 (15.19) |
| MIXER | 1.30 (1.29) | **10.37 (8.43)** |
| TUNE | 1.27 (1.26) | 16.96 (17.96) |
| LIVE | 1.27 (1.27) | 10.66 (11.23) |
| INSPECTOR | 1.29 (1.28) | 36.71 (33.75) |

**The per-tick cost is unchanged.** That is the number the 30 Hz budget depends on and the one
the "no page repaints itself wholesale from its tick" rule is about.

**The full-repaint cost is up by 1.5-2.5 ms on every page**, which is +21 % on MIXER - over the
15 % line. It was measured with the two binaries run alternately, so it is not thermal drift.
Per-child timing says where it goes: the chrome simply carries more labels than it did - the
session name, MIX HISTORY, the autosave lamp, a sixth tab and an extra status field - and each
small label costs about **0.1 ms** to draw, because a Barlow Condensed run with a kerning factor
cannot take JUCE's fast glyph path. It is not one expensive thing; it is eight ordinary ones.

Full repaint is the price of a **page switch or a resize**, not of a frame. It was already over
one frame on INSPECTOR at the baseline (33.75 ms). **This needs a decision**: accept it as the
cost of the design's chrome, or pay down the kerned-text draw cost before UI-3.

## 3c. UI-3: the mixer and the channel strip

The Handoff page does not exist, but the **Channel Strip component does**, and
`get_metadata` on it gives every y offset. So the strip is built to the design's own numbers
rather than to an eye: 48 x 680, bus bar at 0, name at 7, input and tuned lamp at 22, trim at
35, four insert slots at 50/67/84/101, sends at 121 and 133, the pan at 148, its readout at
174, the keys at 190, the throw 216..610 with the fader track 3 px wide at x24 under a 22 px
cap, the meter at x39, the gain reduction at x44, the readouts at 616 and 632 and the plate at
652. The seven scale stops come out of the same metadata at 0 / 14 / 28 / 42 / 62 / 80 / 100 %,
which is exactly what the phase prompt said they were.

**What is new on the strip:** the trim readout, four *fixed* insert slots (always four, so the
eye finds GATE in the same place on every strip that has one), the sends as bars rather than
words, the design's 270 degree pan knob, the scale down the left of the throw, the
gain-reduction meter, and the bus plate on a 18 % tint of its own group's colour.

**Gain reduction needed no engine work.** `MixEngine::getStrip(i).getCompressor()` is the same
running processor the meters are already read from, so the 2 px column reads
`getGainReductionDb()` on the message thread exactly as the meters do. Nothing new is
published, nothing is allocated, and the audio thread is untouched. It gets its own small
repaint rectangle so it can move every frame without the strip being redrawn around it.

**Insert slots open their stage.** Clicking GATE on a strip opens the Inspector with the gate
already picked out - `ChainEditor::selectStageNamed` matches on the word the slot prints, which
is the same word the chain foot and the Inspector use, so the three cannot disagree. An unknown
stage opens the channel and changes nothing else. Asserted in `dlive_ui_tests`.

**The group panel** is pinned down the right, always visible, one strip per `MixBus` in stored
order with the master at the end; the scrolling bank holds the channels alone. Sixteen channel
strips fit at 1440 with the panel beside them, which is what the prompt asked for. LEAD and BGV
are not there because they are not `MixBus` values yet (§6).

**Solo went from teal to `hot`.** The design's S key and its solo bar are the same colour, and
it is not the accent. That is right: teal in DLIVE means *this is what DLIVE did, or what is
chosen*, and solo is neither - it is the engineer's own listen, which the room never hears.
`CLAUDE.md` said "solo teal" and now says otherwise.

### One thing left the mixer strip

**Record-arm (R) and monitoring (A).** The design's strip carries two keys, mute and solo, and
that is what DLIVE's does now. Neither became impossible: every TRACKS row still has its own R
and A, and ROUTING has a REC column - which is where somebody setting a service up already is.
This is the change in this phase most worth arguing with.

### The frame cost

| | tick ms | full repaint ms |
| --- | --- | --- |
| MIXER, UI-2 | 1.51 | 10.4 |
| MIXER, UI-3 | 1.51 | 14.0 |

Per-tick is unchanged again. The cold full repaint is up because a strip now draws seven scale
labels, four slots, two send bars, a knob, a plate and a reduction meter where it used to draw
a short list - and the measurement rebuilds all 48 strips' image caches at once. In use they
are cached (`setBufferedToImage`), so this is the price of a page switch, not of a frame. It
compounds with UI-2's increase: MIXER's full repaint is 8.4 ms at the UI-0 baseline and 14.0 ms
now. **Still awaiting the decision in §3b.**

## 3d. UI-4: the ROUTING workspace, and Purpose as a sheet

Which device the sound comes in on, what each input is, and where the mix goes out were three
screens with Back and Continue between them. That is the right shape the first time somebody
sets a service up and the wrong one every Sunday after, when the question is never "what is
step two" but "why is there nothing on channel 9". So they are one workspace in three columns.

**Nothing was rewritten.** The columns *are* `DevicePage` and `AssignPage`, shown embedded:
`SetupLayout::of` gained an `embedded` flag, so a page gives up the title, the sentence under
it and the Back / Continue footer - the things that belong to a whole page - and keeps
everything else. Every behaviour moved intact: the device list and its states, the microphone
answer, the saved input maps (`InputMapStore`), the stereo pairs, the bulk actions, the solo
device and the Dante case. The third column states the output feeds and the monitoring and
opens the Outputs sheet to change them.

**INPUT ACCESS is real, and built.** Phase 1 landed `DeviceState` and the sentence, so the card
says ALLOWED / REFUSED / NOT ASKED YET and, when macOS has refused, prints what to do about it.
Device **hot-plug** is still waiting on its engine phase and is left out.

**APPLY.** The page bar carries Input map... / Save map / APPLY. Under LIVE SAFE, APPLY asks
first, through the window's own `liveSafeBlocks` sentence so it reads the same everywhere.
**A deviation worth knowing about:** the phase prompt says "nothing changes routing without
APPLY", and today an assignment still takes effect as it is made - which is how the page has
always worked and how the meters beside each row stay meaningful. Turning every routing edit
into a staged one is a change to how the page *behaves*, not to how it looks, and it could
itself be a regression, so it is written here rather than done quietly. APPLY currently
rebuilds the graph and confirms under LIVE SAFE.

**PURPOSE AND SOUND is a sheet.** Two questions answered once and revisited when the service
changes, not when the console does. `showPage(Page::Purpose)` opens it, so every way in still
works - the first run, the setup popover, the old sidebar row - and the workspace underneath
stays where it was. TUNE THE MIX closes it on the way through; Back closes it; Escape closes it.

**Restoring the session name in UI-2 gave this a front door.** `setupPopover` - new, open, save
as, import, reference mix, input mappings, the recording destination, getting started - had had
no caller at all since the name left the title row on 2026-09-18, and PURPOSE AND SOUND is one
of its rows.

**The guard records the moves.** `dlive_ui_tests` now asserts that Page::Device and Page::Assign
both land on ROUTING, that Purpose opens as a sheet over the workspace and leaves it where it
was, and - a new test - that the first Sunday still works end to end: sessions, a device, the
inputs, the purpose, the console, with the session intact at the other end.

**Columns give way from the outside in** as the window narrows: the outputs first, then the
device list, so the input map - the thing somebody came here for - is the last thing to go. At
1180 x 760 it is device + input map.

## 3e. UI-5: TUNE and LIVE

### TUNE - done

**The scope picker.** WHOLE MIX / ONE GROUP / SELECTED CHANNELS, in a page bar of its own, wired
to `startTuneMix`, `startTuneBus` and `startTuneChannel`. It is one control because the three
of them are one decision: the same listen and the same planner either way, narrowed by
`MixPlanner::busOnly` or `channelOnly`. Somebody who has tuned the whole mix and now wants to
fix only the pastor should not have to know that TUNE CHANNEL is a different verb.
SELECTED CHANNELS uses the channel picked out on the rail, and says so when there is none.

**Last tuned, per group.** Each group tile says "Tuned 8:27" or "Not tuned", read off the mix
history rather than remembered separately: a checkpoint is taken at every tune and is named for
what did it - "TUNE MIX", "RE-TUNE", "TUNE DRUMS" - so the answer is already written down. A
whole-mix tune counts for every group, because it set every group.

**The result card already existed.** `ResultSheet` shows WHAT then WHY per change, BEFORE /
AFTER, KEEP / REVERT / TRY ANOTHER MIX and per-group KEEP chips - the design's content, driven
by the existing decisions and sentences. It is a sheet over the workspace rather than the
design's inline middle column, which is the one thing left to change here. Per-change UNDO
*after* keeping is not built: the existing per-group KEEP chips are the same control at the
moment before, and post-keep undo would need `putBack` wiring that is worth doing deliberately.

**The macro pads keep their behaviour exactly** - BODY x VOICE, DRIVE x ROOM, ENERGY, the snaps
and the LIVE SAFE fencing - as the phase asked.

Speaking / Singing and the favourite-mix comparison are **left out**: both wait on engine
phases (§6).

### LIVE - part done

**MACROS - WHOLE MIX is built**: the five real macros (VOCALS DRUMS BASS SPACE ENERGY) as the
design's arc knobs, reading and writing the same `MixMacroValues` the TUNE pads move, so moving
one here and moving it there are the same move. LIVE SAFE fences them the way it fences the
pads. They render with live state. The tiles give up their spare height for them, and the
macros are the first band to go when even the tiles' floor will not fit.

**The page bar is built**: "Live", the **ON AIR** lamp (lit whenever the engine is running and
the broadcast is not muted) and "Now: <marker> · next marker ... at 01:25", read straight off
the timeline so it is the same list TRACKS shows.

**The BROADCAST card is built**: the short-term LUFS big enough to read from the back of a
booth, then Integrated, True peak and the target bar. The design's third reading is **Range**
(loudness range, LRA) and DLIVE does not measure it, so rather than print a number that is not
real the card shows **Against target** in LU - `deltaLu()`, which is what somebody reading that
card is looking for anyway. LRA is noted below as a measurement worth proposing.

**Still owed to the design:**

- **scenes as cards** with their kept times and RECALL / KEEP - they are a row of pickers today. The kept time is derivable (`mark("Scene kept: ...")` is already in the history), so this is restyling;
- **groups as flat horizontal faders** instead of vertical tiles.

The **Autopilot card** is not built and must not be: it waits on its engine phase.

**Frame cost.** Per-tick is unchanged at 1.27-1.32 ms. LIVE's cold full repaint went 11.2 → 21.6
ms with the macros row, the broadcast card and the page bar. Two real costs were found and
fixed on the way - `markerLine()` was scanning the timeline and building strings on every one
of the thirty ticks a second (it is rebuilt now only when the playhead crosses a marker), and
the big LUFS number was measuring its own string on every paint (a monospaced face does not
need measuring). What is left is the drawing itself, and it is page-switch cost, not frame cost.

## 3f. UI-6: the Inspector

**Most of this phase was already built.** `AdvancedPage` + `ChainEditor` already had the
header, the signal path with every stage's on/off lamp and value, the channels rail, the
"What DLIVE did" column with PUT BACK, one panel per `ChannelProcessor` stage, and every
panel's controls bound to that stage's `ChannelParameters` fields through the existing
parameter path. The stage list is already built from what that product and role actually has,
in processing order. The compressor panel already drew its transfer curve, the live gain
reduction and the last eight seconds of it; the EQ panels already drew their curve and bands.

Per-stage gain reduction was already wired for **every** stage that has one - gate, compressor,
de-esser, limiter - so 17c, 17d, 17e and 17j's reduction readings needed nothing.

### What this phase added

- **17c, the gate's second threshold.** A gate has two, and the second one is the entire reason
  a gate on a tom does not chatter: it opens at the threshold and does not close again until
  the sound has fallen `hysteresis` below it. Drawing one was the half of the story that makes
  a correctly-set gate look badly set. Both are now drawn and labelled OPENS / CLOSES.
- **17i, correlation.** `StereoWidth::getCorrelation()` is an atomic the width stage already
  keeps; the panel reads it the same way the meters are read. The scale says which end is
  which, because "0.24" on its own says nothing and *out of phase* is the end that matters -
  it is the one that disappears when the stream sums to mono.
- **17j, the reported latency.** `getLatencySamples()` in milliseconds and samples, said either
  way rather than only when it is bad news - the channel path adds none, and the panel says so.

### What it did *not* add, and why

**17b's measured fundamental and the 0.8 x cap.** The measurement exists
(`AnalysisResult::fundamentalHz`) but nothing keeps it after a tune: the controller does not
retain a per-strip `AnalysisResult`. Drawing the cap would mean **new retention** - not a new
measurement - and the rule is to propose that rather than build it quietly.

> **Proposed:** `MixController` keeps the last `AnalysisResult` per strip from the most recent
> listen, as it already keeps `StripTuneRecord`. It costs one struct per strip, it is read on
> the message thread only, and it would also let the Filter panel show what the high-pass is
> actually being held below. **Please say whether to do this.**

### A note on measuring frame cost in this repo

The numbers move with the machine's thermal state by about 15 %, uniformly across all five
workspaces. A before/after taken minutes apart is not evidence. UI-6 was measured by building
both binaries and running them **alternately**: INSPECTOR per tick was 1.46-1.53 ms before and
1.46-1.51 ms after - identical. That is the method that found the real UI-2 regression too, and
it is the only one worth quoting.

## 3g. UI-7: the sheets

### Done

**Text size moved into the Appearance sheet**, beside the themes - "can I read it" and "does it
suit the room" are the same moment's question, and having the answer to one in a menu and the
other in a sheet is how a preference gets lost. The menu item stays, so both ways in work.
The Appearance scrim goes 50 % → the design's **25 %**: that sheet is the one place where the
console behind it *is* the preview, and a theme cannot be judged through a curtain.

**EXPORT (frame 18) is built.** Bouncing a service out is four questions - what, how much of
it, what format, how loud - and it was two menu items that asked none of them and always
rendered the whole recording. The sheet asks the four and then says in one sentence exactly
what is about to be written, before anybody waits ten minutes for it:

- **How much of it**: the whole recording, or the stretch after any marker on the timeline -
  so "the sermon" is whatever somebody called it while the service was running.
  `MixBounce::renderProject` already took a range; `ExportJob` now carries one.
- **Format**: WAV or MP3, as before.
- **Delivery loudness**: the session's target, whether the mix is on it, and what to do if it
  is not. **Read-only on purpose** - it is a property of the session's purpose, and changing it
  inside an export dialog would change a mix somebody had already approved.

The two straight-to-a-file menu items stay under the new one: somebody who exports the same
thing every Sunday should not have to answer four questions to do it again.

**Group stems and the raw multitrack are not offered.** They are in the design and they are
honest work - an offline render per group and per input, never real-time - but they do not
exist yet, and a control that does nothing is worse than one that is missing.

### Still owed

- **Recover session (01)** works today as an `AlertWindow` (Recover / Open last saved / Keep both) from Phase 1. The design wants it as a proper view; the behaviour is there, the sheet is not.
- **Mix history drawer (08)**, **Mix Buddy as a drawer (15)**, and the restyles of ChannelTune (13), Check (14), Reference (16) and Tutorial (20). All four sheets already draw on v2 tokens and roles, so this is composition, not colour.
- **Reset mix to raw (09)** and **Favourite mixes (21)** wait on their engine phases and must not be built before them.

## 3h. Percussion, brass, and the first-time guides

Two things asked for outside the design file.

### New sources: percussion and brass

`ChannelRole` is a **stored** enum - every session, preset and input map on disk holds these as
integers - so the eight new roles are **appended**, and `dlive_app_tests` now pins the anchors
(`KickIn` 0, `CrowdMic` 35, `AmbienceMic` 36, `SaxBari` 40) so a future edit cannot silently
re-point somebody's console at a different instrument.

| New role | Family | Bus | Why it is not an existing family |
| --- | --- | --- | --- |
| Congas, Bongos, Djembe, Timbales | **Percussion** | DRUMS | A conga is tuned an octave above a rack tom, its slap lives where a snare's crack does, and **the ring is the instrument** - so its gate may take off 12 dB where a tom's may take 30, and it is never sustain-cut. |
| Shaker (and tambourine) | **Shaker** | DRUMS | No body at all, and it never stops. It is the one percussion source a gate must never be put on - gating something continuous is how it starts chattering. Never transient-shaped, never sampled. |
| Trumpet, Trombone, Brass section | **Brass** | MUSIC | Louder than a reed and harder on top: a trumpet's edge is 1.8-4.5 kHz where a sax's honk is 0.9-2.5, and a trombone reaches an octave below either. Compressed harder and aimed further back, because it will take the mix if it is not. |

**No new strategies were written**, and that is deliberate: `CLAUDE.md` says a strategy holds
decision logic and never a target. A hand drum is *decided* the way a tom is, a shaker the way
a hi-hat is, a horn the way a saxophone is - what is new about them is their numbers, and the
numbers live in `ProfileData.cpp`. Every other profile is built from Modern Gospel, so all six
inherit them with their own deltas.

> **These targets have not been fitted against a real recording.** They are documented deltas on
> the nearest family that *has* been - Tom, Hi-Hat and Saxophone - because the reference
> multitrack has no percussion or brass in it. `CLAUDE.md` says never add a profile that cannot
> be tuned by listening, and this is the honest version of that: it is a defensible starting
> point, it is marked as one in the source, and **the first real conga or horn take should
> re-fit it**.

Name guessing covers what a desk actually writes: conga, bongo, djembe, cajon, timbale, timbs,
shaker, tambourine, tamb, egg, perc, trumpet, tpt, trombone, tbone, brass, horns. The input
picker gains a **Percussion** kit, and the **Horns** kit gains trumpet and trombone.

### The first-time guides

The Tutorial already taught the shape of the app once, skippably. This is the other half and it
is smaller: **the first time somebody opens a workspace, one sentence says what that screen is
for and what to do first.** It takes a band off the top rather than floating over the workspace -
a sentence that covers the thing it describes is worse than no sentence - and it carries two
buttons: *Got it* (this one, for good) and *Don't show these* (all of them, for good).
**Help > Show the guides again** brings them back.

It is a preference of this Mac, in the same `preferences.json` as the theme and the text size,
so two people sharing a booth Mac share the fact that the app has been explained.

**The headless tool never reads it**, for the same reason it never reads the stored theme: a
render must not depend on whether the developer had pressed "Got it". `setForceGuideForSnapshot`
shows one anyway, so `33-guide` photographs it deterministically.

**A regression the guides work uncovered.** The Tutorial's own sentences had been made *false*
by this design work and nothing caught it: step 1 said the sidebar is where you "set up the
inputs" (they left it in UI-4) and step 5 said "R sets a track to record" while describing the
mixer (R left the strip in UI-3). Both are corrected, and step 2 now rings the ROUTING tab
itself rather than the whole row of six.

## 4. The screens

Read in full against the code: **00 INDEX, 02 ROUTING, 04 MIXER, 05 TUNE**. The rest are
entered from the frame name, the phase prompt's own description and the code as it stands;
each phase reads its own frames properly before building, and corrects its rows here.

| # | Frame | Implemented today by | Matches | Differs / new | Phase |
| --- | --- | --- | --- | --- | --- |
| 00 | INDEX | — | — | contents page, not a screen | — |
| 01 | SESSIONS + RECOVER | `SetupPages.cpp` `SessionsPage` | the library list, open, name, date | the Recover card | UI-7 |
| 02 | ROUTING | **`RoutingPage`** hosting `DevicePage` + `AssignPage` embedded, plus an outputs column | **done** - every behaviour moved intact | staged edits behind APPLY not done (see §3d) | UI-4 |
| 02b | ROUTING · INPUT ACCESS REFUSED | `DevicePage`'s INPUT ACCESS card | **done** - ALLOWED / REFUSED / NOT ASKED YET with the sentence | hot-plug waits on its engine phase | UI-4 |
| 03 | TRACKS | `TracksPage.cpp` | timeline, clips, markers, loop, row heights, arm, monitor, per-track TUNE | restyle only | UI-2/3 |
| 04 | MIXER | `MixerPage.cpp` | **done** - the strip is the design's component, the group panel is pinned, the slots open their stage | R and A left the strip (§3c) | UI-3 |
| 05 | TUNE | `MixPage.cpp` | TUNE MIX, `startTuneBus`, TUNE CHANNEL, the decisions, the diff, the sentences, KEEP / REVERT / RE-TUNE | scope picker as one control; per-change UNDO; groups rail with last-tune time; **AIM AT ★favourite** and the **NOW / FAVOURITE** comparison; **SPEAKING / SINGING** per voice | UI-5 (+ §6) |
| 06 | LIVE | `LivePage.cpp` | scenes, the five macros, flat group faders, loudness, ON AIR | the Autopilot card | UI-5 (+ §6) |
| 07 | INSPECTOR · Sample replacement | `AdvancedPage` + `ChainEditor` | the Sample stage in full | restyle | UI-6 |
| 08 | MIX HISTORY (drawer) | `HistorySheet.cpp` | persistent checkpoints landed in Phase 1 (`a09d287`) | drawer styling | UI-7 |
| 09 | RESET MIX TO RAW | — | — | the whole thing, and a "Before reset" checkpoint | UI-7 (+ §6) |
| 10 | MIXER · LARGER TEXT | — | — | Text size 1.0 / 1.2 / 1.35, stored, in View > Appearance | UI-1 |
| 11 | PURPOSE & SOUND | **`PurposeSheet`** over `PurposePage` | **done** - same fields, same effects, now a sheet | — | UI-4 |
| 12 | TUNE · LISTENING + MACRO PADS | `MacroPad.cpp`, `MacroRibbon` | BODY × VOICE, DRIVE × ROOM, ENERGY, snaps, LIVE SAFE fencing | restyle only - behaviour is not to move | UI-5 |
| 13 | TUNE CHANNEL | `ChannelTuneSheet.cpp` | all of it | restyle | UI-7 |
| 14 | CHECK INPUTS | `CheckSheet.cpp` | all of it | restyle | UI-7 |
| 15 | MIX BUDDY | `ChatSheet.cpp` | plan-before-apply, the refusal sentences | becomes a drawer | UI-7 |
| 16 | REFERENCE MIX | `ReferenceSheet.cpp` | all of it | restyle | UI-5/7 |
| 17 | INSPECTOR · EQ & DYNAMICS | `ChainEditor.cpp`, `EqCurveComponent` | the bands, the curve | bands drawn over the post spectrum | UI-6 |
| 17a–17k | INSPECTOR, stage by stage | `ChainEditor.cpp` | every control is an existing `ChannelParameters` field | one panel per stage, and the visualisations - see §5 | UI-6 |
| 18 | EXPORT | **`ExportSheet`** over `MixBounce` | **done** - range by markers, format, delivery loudness | group stems and raw multitrack need offline renders that do not exist | UI-7 |
| 19 | APPEARANCE | `ThemeSheet.cpp` | **done** - themes, import, folder, text size, 25 % scrim | — | UI-7 |
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
