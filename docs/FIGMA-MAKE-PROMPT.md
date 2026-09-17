# Figma Make prompt — DLIVE design revamp

Paste everything below the line into Figma Make. It asks for a complete new design
language for DLIVE, built against a function inventory that may not shrink.

---

## MISSION

Redesign **DLIVE** — a live recording, mixing and broadcast DAW for churches — from
the ground up, visually. Every screen, every control and every piece of information
listed below must still be there and still be reachable in the same number of moves
or fewer. **Nothing is allowed to disappear in the name of a cleaner picture.**

This is a revamp, not a reskin and not a re-spec. You are free to change the visual
language completely: palette, type, density, shape, motion, how a workspace is
navigated, how a channel is opened, how a panel folds. You are **not** free to drop a
control, hide a state behind a mode that did not exist, or replace a number with a
vibe.

Deliver a working, clickable, high-fidelity prototype (React + Tailwind), dark by
default, plus a light variant of the token set. Mock data only — no backend.

## WHO IS LOOKING AT IT

Two people, on the same screen, at the same time:

1. **A volunteer.** Sunday morning, 20 minutes before the service, possibly their
   second time ever. They do not know what a gate, a de-esser or a bus is. They need
   to be told what to do in a sentence of plain English, press one big obvious thing,
   and get a professional mix.
2. **An engineer.** Wants every parameter, every meter, every dB, and wants to see
   exactly what the software decided and why, so they can overrule it.

The design must serve both without two apps and without a "simple/advanced" toggle
that hides the mix from the person responsible for it. The pattern that already works
and must survive in some form: **plain language on the surface, engineering one click
down, and the machine's reasoning always printed in sentences beside its numbers.**

## THE PHYSICAL SITUATION (this is what should drive the aesthetic)

- A dark sound booth at the back of a room, often with a bright stage in the eyeline.
  The UI is dark because of the room, not because dark is fashionable.
- It is **live**. A wrong click is audible to 400 people. Destructive and
  hard-to-reverse actions must look different from safe ones, always.
- It is read **at a glance and from a distance** — "is it recording, is anything
  clipping, is anything muted" must be answerable from two metres away.
- Trackpad and mouse, on a Mac, 1180×760 minimum, 1520×960 typical, often on a
  large external display. Design for 1280 and for 1920 both; the middle of the
  window must be able to take the whole width when side panels fold.
- Sessions run for two hours. Nothing may pulse, breathe or animate continuously.
  Motion is for state changes and for meters only.

## VOCABULARY — non-negotiable

These are product verbs. They are set in caps, they are the only caps in the UI, and
they are never renamed or translated into engineer-speak:

`TUNE MIX` · `TUNE LIVE MIX` · `TUNE CHANNEL` · `RE-TUNE` · `KEEP` · `REVERT` ·
`BEFORE` · `AFTER` · `BYPASS` · `LIVE SAFE` · `MATCH TO REFERENCE`

Plain-language rules that must hold in every string you write:

- The app never says "arm". It says **"set to record"**, **"N TO RECORD"**, and the
  red **R** key. (The engineer's word may appear once, in that key's tooltip.)
- Group buses are **DRUMS, BASS, MUSIC, VOCALS, SPEECH, AMBIENCE, MASTER** — in that
  order, everywhere, master last. Speech (the preacher) is never lumped in with
  Vocals (the singers), and the design must make that visible.
- When the software refuses to do something, it says what it refused and why, in a
  sentence. There is no silent failure and no bare error code anywhere in this product.
- Advice is advice: a gain-staging warning never changes the mix by itself.

## THE SIX STATES THAT MUST ALWAYS BE UNMISTAKABLE

Design a coherent, non-colour-only treatment for each; they appear on many surfaces
and must read identically on all of them:

| State | Meaning | Where it appears |
|---|---|---|
| **Muted** | signal is arriving, it is not being heard | strips, timeline headers, LIVE tiles |
| **Soloed** | this and nothing else | same |
| **Set to record** | this input will be written to disk | timeline headers, mixer strips |
| **Monitoring** | you are hearing the live input, not the timeline | same |
| **Tuned by DLIVE** vs **hand-edited** | who set this value | every parameter, everywhere |
| **LIVE SAFE on** | the sound is locked; re-routes and re-tunes are blocked | globally |

A muted channel must still show its meter moving (grey), because "nothing is there"
and "it is there but not heard" are different problems and must never look alike.

---

# FUNCTION INVENTORY — the contract

Nine screens plus six sheets. Redesign all of them. Keep every item.

## Shell

- **Left sidebar (224 px today, collapsible):** the wordmark; a Library item; a Set-up
  group (Audio Device, Inputs, Purpose and Sound); a Workspace group (TRACKS, MIXER,
  TUNE, LIVE, INSPECTOR); and at its foot a live device readout — a status dot, one of
  *Running / Recording / Not running / Device lost*, the interface name, sample rate,
  buffer size, and a dropped-buffer count when there is one. "Device lost" must not
  settle into a calm grey resting state.
- **Unified toolbar (56 px today):** sidebar toggle · session name + its menu ·
  transport cluster (return to start, stop, play, record, loop) with a timecode clock
  `00:00:00.000` and a session length · workspace tabs · BYPASS · the current output
  and its picker. The transport keeps its width before the session name does.
- **Page body**, ending in a **chain strip**: the selected channel's processing chain,
  stage by stage, with what each stage is set to. Clicking it opens the Inspector.
- **Toast** for confirmations. **Menu bar**: File, Edit, Track, Mix, Transport, View,
  Help — full item list in the appendix; every menu item must have a home in the UI too.
- **Collapsible panels**: sidebar, TUNE's input rail, the Inspector's channel rail and
  its "what DLIVE did" column. A folded panel must leave a visible, named handle
  behind — the middle of the workspace gets the width, the panel stays one click away.
  `[` and `]` toggle the panel on each side of whatever page you are on.

## 1. SESSIONS (the library)

Every saved session as a row or card: name, what it sounds like (style profile), what
it was for (purpose), when it was last opened, and **a single bar showing how its
inputs fall across the groups**, in the group colours. Search, filters, New Session,
open on click. This is where the app opens when there is a library to open into.

## 2. AUDIO DEVICE

Pick an input device and an output device from lists, with sample rate and buffer.
Live per-channel input meters, so "the console is plugged in but channel 9 is dead"
is visible before anything is named. Import a multitrack folder from here. A note
explaining that macOS opens one device at a time and that two at once means an
Aggregate Device, with a way to open Audio MIDI Setup.

## 3. INPUTS (the patch)

A table of every device channel: number, what is arriving on it (live meter), the
name, what it is (source role), which group it feeds, stereo pairing. Grouped by the
bus each input will feed, with clickable group headers that select the whole group.
Selecting inputs turns the toolbar into a bulk toolbar: **set what they are · fill a
kit down them in order · name them from their role · link them as pairs · drop them**.
Filter chips per group. Save the whole patch as a reusable input mapping, and apply a
saved mapping — applying one must show what the current device cannot supply rather
than routing audio to a channel that does not exist.

## 4. PURPOSE AND SOUND

Pick what the session is for and which sonic profile it aims at, as cards with a
radio. Each card states in plain words what the mix will aim for — those promises are
the actual numeric targets, so they must be presented as a commitment, not as flavour
text. Modern Gospel is the default; Modern Worship is a documented variant.

## 5. TRACKS (timeline)

- Tool row: row height S/M/L · Snap · Follow · Split · Marker · what is selected ·
  loop · zoom · **All to record**.
- One ruler band that holds: the loop strip (drag to set a loop), the marker lane
  (click to jump, drag to move, double-click empty to add, right-click to rename or
  delete) and the time ticks.
- Track headers, in three heights. Each carries: a status dot, number, source icon,
  name (amber with a warning glyph when the name no longer matches the audio under
  it), R/A/M/S keys as a 2×2 block, a level meter, a volume fader, a level readout,
  and a third line that exists only when it has something to say — the balance when it
  is off-centre, or gain-staging advice.
- Clips with waveforms, coloured by group; a clip you cannot hear (muted or soloed
  out) is drawn grey. Split at the playhead, delete, undo.
- Snap is magnetic to the grid, markers, the playhead, the loop and every other clip
  edge. Pinch or Cmd-wheel zooms about the pointer.
- **Drag a header up or down to reorder the channel** — a drop line shows where it
  lands, and the change is felt on every other workspace. A click that wanders a few
  pixels must still be a click.
- Header right-click: Rename · Use the clip's name · Match every track to its clips ·
  Source · Icon · Fix the assignments… · Open in the Inspector · Move up / Move down.
- Single click selects (the chain strip follows), double click opens the Inspector.

## 6. MIXER (the console)

One console surface, not a row of cards. Two layouts and a filter:

- **STRIPS** — a vertical bank at three widths (58 / 92 / 116 px today). A column
  reads top to bottom: number and name · gain-staging chip · **INSERTS** (the chain
  stages that are actually on) · **SENDS** (the FX slots in use, read-only here) ·
  **PAN** · fader and meter · level and peak · R/A/M/S · the bus it feeds.
- **The slots are fixed** — three inserts, two sends, a gain row and a pan row are
  reserved on every channel and every bus — so empty slots hold their place and the
  sections line up straight across the whole console. A short strip drops whole
  sections in a fixed order rather than squeezing the fader.
- Groups are separated by a gap carrying the group's colour as a band. The master is
  pinned to the right and carries LUFS-I, short-term and true-peak against a −23
  target.
- **There is no per-strip dB ruler.** The one mark a bank is read against is the 0 dB
  unity line, drawn across the fader and the meter at the same height in every strip.
- **LIST** — a row per source, so names, levels and keys line up down the page and
  nothing is ever off the right edge.
- Filter: All / Inputs / Groups. Click selects, double-click opens the Inspector,
  right-click offers TUNE CHANNEL. The console also opens in its own window.

## 7. TUNE (the mix engineer)

The heart of the product.

- **Mix health**, in sentences, with the gain-staging note first, naming the inputs.
- **TUNE MIX** — the deterministic one. Press it, DLIVE listens to the band for ~30
  seconds, then proposes a whole mix.
- **TUNE LIVE MIX** — the same listen with a reasoning layer on top, then a second
  listen to verify what it did. It can fail, time out, or be offline: when it does,
  the user is still left with a professional mix and a sentence explaining it.
- **Reference** — add a finished recording and MATCH TO REFERENCE. Before the button
  is pressed, the sheet draws the two tonal balances against each other and prints
  both what matching will aim for **and what it refuses to copy**.
- **AI Mix Chat** — say "bring the lead vocal forward" in plain words. The transcript
  shows what it intends to do and on which channels **before** it is yours; nothing is
  committed by asking; each past turn can be read back and undone by name. It works
  offline.
- The five **group strips** with meters, and the five **macro controls** (50 = the
  plan, so the centre is "as tuned" and every macro is bipolar).
- An **input rail** down the side: every source, with TUNE CHANNEL on each.
- **Three transient screens that are as important as the resting one:** *listening*
  (a countdown and what it is hearing), *working* (a run that is thinking has to look
  like a run that is thinking — real progress, not a spinner), and *the result*.
- The result sheet is the product's biggest moment: **BEFORE / AFTER** audition,
  what changed, why, in sentences, and **KEEP / REVERT**. Also Try Another Mix, and
  undo/redo of whole mixes.

## 8. LIVE (during the service)

Big, calm, readable from across the room. Only what matters while it is happening:

- Is it recording (with the clock and the disk space), is the output going out, is
  anything clipping, what is the master headroom, is anything muted or soloed.
- One tile per group bus, **plus one for the FX returns** (which has MUTE and nothing
  to solo against). A muted tile reads **NOT HEARD**; a soloed one reads **SOLO**.
- The engineer's own monitoring: MONITOR SOLO / SOLO IN PLACE, AFL/PFL, Dim, Clear
  solo, monitor level. None of it can change what the room or the stream hears — the
  design must make that obvious.
- **LIVE SAFE**, which fills and reads "LIVE SAFE ON" with a sentence beside it saying
  exactly what is locked.

## 9. INSPECTOR (the engineer's drill-down)

Three columns:

- **Left rail** — every channel under its bus, with a dot, name, a mini level bar and
  its fader; the engine's state (rate, buffer, latency) along the foot.
- **Middle** — the channel head (colour, what it is, name, IN and OUT meters either
  side of the chain, input gain, level, pan, keys). Under it the **signal path**: a
  chip per stage with a lamp, a number, an icon, its setting, a bar for how hard it is
  working, and a dot for where the value came from (tuned by DLIVE vs hand-edited).
  Under that, the stage you picked, opened as a device: its name, a plain sentence,
  IN/OUT, **Back to DLIVE**, and what it is doing **drawn** — a draggable EQ curve with
  nodes, a compressor's or gate's in/out line with live gain reduction and the last 8
  seconds of history, a trim's bars with its gain staging — beside a knob for every
  number it owns.
- **Right column** — what TUNE MIX did: a line per stage with its value, a
  TUNED / EDITED / NOT USED badge, and the sentence from the report that explains it.
  RE-TUNE, REVERT, and the headroom (the master shows loudness instead).
- Chain stages available: Input, filters, gate, corrective EQ, de-esser, compressor,
  transient, tone EQ, saturation, width (stereo only), output trim, limiter (master
  only), loudness meter. An unavailable processor is shown **with its reason**, never
  silently missing.

## Sheets

**Listening** · **Result (BEFORE/AFTER, KEEP/REVERT)** · **Reference** ·
**Outputs** (up to 4 simultaneous feeds: device output pair, source = master or a
group bus, level, mono, mute) · **Channel Tune** (drops over whatever workspace you
are on, so the console keeps playing behind it) · **AI Mix Chat**.

## Gain staging — its own design problem

One input's level has seven states: **Faint / Not heard / Low / Hot / Clipping /
Digital / Healthy**, each with a console move in dB and a sentence. "Digital" is the
one that matters in a church: the level works, but only because DLIVE raised it
digitally, which lifts the preamp's hiss with the source. It appears as a chip on
timeline headers and mixer strips, as a card at the top of the Inspector's right
column, as the plan's first note, and in mix health. Design a chip that survives at
58 px wide and still says which of the seven it is.

---

# INTERACTION RULES THAT MUST SURVIVE

1. **A fader moves when it is dragged and at no other time.** A two-finger swipe
   across a bank of faders is a scroll, never twenty-four small changes to the mix.
   No slider anywhere in this app responds to the wheel.
2. Scrolling surfaces move as far as the fingers do — native trackpad distance.
3. Switching workspaces never changes the sound. TRACKS, MIXER, TUNE, LIVE and the
   INSPECTOR are five views of one session, never five states.
4. BYPASS lets you hear the raw console feed; it never touches the kept mix, and
   faders are disabled while it is on.
5. LIVE SAFE blocks anything that re-routes or re-tunes, and says so by name.
6. Hand-edited values stay visibly hand-edited until "Back to DLIVE" or REVERT.
7. Nothing is committed by asking — every proposal lands on BEFORE / AFTER first.
8. Keyboard: Space play/stop · Return to start · R record · L loop · B bypass ·
   T tune channel · M marker · `[` `]` panels · ⌃⌘S sidebar · ⌘1–5 workspaces ·
   ⌘= / ⌘− / ⌘0 zoom · ⌘Z undo · ⌘S save · ⌘N new · ⌘O open · ⌘E split.

---

# WHAT I WANT BACK

1. **A design language**, stated first and stated once: palette (and how the seven
   group colours sit inside it, distinguishable for the ~8% of men who are
   colour-blind — the current Vocals green / Speech pink / Drums amber set is a known
   weak point), type scale (numbers are tabular and monospaced — dB values that jitter
   in width while they change are unreadable), spacing and density scale, radii,
   elevation, iconography, focus and hover treatment, and a motion policy.
2. **A component set** built from it: meter (vertical segmented and horizontal bar,
   with peak hold and a muted variant), fader, knob, pan control, console key,
   filter chip, status pill, popup, nav item, panel handle, card, section caption,
   stacked group bar, radio, sheet, toast, transport key, clip, track header,
   mixer strip, chain-stage chip, EQ curve, dynamics curve.
3. **All nine screens plus six sheets**, at 1280 and 1920, dark and light.
4. **The three TUNE transient states** — listening, working, result — designed as
   carefully as the resting screens, because they are what the volunteer actually
   stares at.
5. A short written note per screen on what you changed and why, and **an explicit
   list of anything from the inventory above you could not place** — with your
   recommendation. An honest gap is fine; a silent drop is not.

Use realistic mock data throughout: a 24-input church band (kick, snare top/bottom,
hats, two toms, overheads L/R, bass DI, bass amp, electric guitar, acoustic DI, keys
L/R, aux keys, drum machine, lead vocal, three backing vocals, pastor's headset, a
handheld for announcements, ambience mics L/R, playback L/R) with plausible names,
levels, mutes, one clipping input and one "Digital" gain-staging warning.

---

# APPENDIX A — today's tokens (for reference; you are not bound by any of it)

Materials `#101113` desk · `#1b1c1e` window · `#232528` card · `#26282c` sidebar ·
`#1e1f22` toolbar · `#2c2e32` sheet · `#34363a` popover · `#1f2023` rail.
Ink `#f2f3f5` / `#a0a5ad` / `#7e838c` / `#5b606a`, glyph `#8b9099`.
Accent `#4db8a4`, ok `#4cc98a`, warn `#f0a33f`, critical `#e5645e`.
Keys: mute `#f0a33f`, solo `#4db8a4`, record `#e5645e`, monitoring `#6ea8e0`.
Groups: DRUMS `#f0a33f` · BASS `#4db8a4` · MUSIC `#8fa2d8` · VOCALS `#4cc98a` ·
SPEECH `#cf8fb4` · AMBIENCE `#b59b7a` · MASTER `#c8ccd4`.
Radii 5–11. Sidebar 224, toolbar 56, footer 52, inspector rail 206 + 272, setup rail
252, page gutter 30×26, controls 24–26, panel handle 15. Meter scale: green to −6,
amber to −1, red above; the scale bottoms out at −60 dB. Window minimum 1180×760,
default 1520×960. Type is the system face; every number is monospaced.

# APPENDIX B — menu bar (every item needs a home in the UI as well)

**File** New Session · Open Session… · Save · Save As… · Import Multitrack Folder… ·
Add a Reference Mix… · Save Input Mapping… · Input Mappings… · Export Stereo Mix
(WAV)… · Export Stereo Mix (MP3)…
**Edit** Undo · Split at Playhead · Delete Clip · Add Marker at Playhead
**Track** Set Every Track to Record · Set No Tracks to Record · Move Track Up · Move
Track Down · Monitoring: Input / Auto / Off
**Mix** TUNE LIVE MIX · TUNE MIX · TUNE CHANNEL · MATCH TO REFERENCE · (add/clear
reference) · AI Mix Chat… · Try Another Mix · Undo Mix · Redo Mix · Reset Macros ·
Clear Solo · Bypass: Hear the Inputs · Mix Engineer: Use the Cloud Model
**Transport** Play / Stop · Record · Return to Start · Loop
**View** Tracks · Mixer · Tune · Live · Inspector · Open Mixer in a New Window ·
Outputs… · Show/Hide Sidebar · Show/Hide the two side panels · Zoom In / Out / to Fit
**Help** About DLIVE
