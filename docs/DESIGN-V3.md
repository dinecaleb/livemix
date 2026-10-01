# DINE v3 — the Full UX Mockup, and how it is being built

The design is the Figma file `2wv5QSnvrSSQXtDzfuShIn` — **"DINE — Full UX Mockup"**, page
`v3 · Screens` (`70:9050`) with its component sheet on `v3 · Components` (`60:9144`). It was
generated from `docs/FIGMA-MAKE-PROMPT.md`, so the function inventory in that prompt is the
contract this design is measured against: **nothing in it may disappear.** The earlier
"DINE Desktop v2" work in the same file was scrapped on 2026-09-28; this is its replacement
and it is not a reskin of it.

This file is the map from the design to the code. Read it before touching `app/ui`.

## 1. The design language

Flat planes, a hairline between them, no gradients and no glow. macOS-shaped: the traffic
lights sit *in* the one toolbar, the sidebar is a source list in sentence case, and the only
caps in the product are the verbs. Type is **Inter** for words and **IBM Plex Mono** for every
number; both are embedded (`assets/fonts`, SIL OFL), so a booth Mac with nothing installed
reads like the mock.

### Tokens (Figma variables, `Dine::` names — they already match the code)

| Token | v3 | what it is |
| --- | --- | --- |
| `menubar` | `#111214` | the transport well, a segment track, the deepest plane |
| `window` | `#161719` | every workspace ground |
| `sidebar` | `#1a1b1e` | the source list |
| `toolbar` | `#1d1e21` | the one toolbar, the status foot, a side rail |
| `selected` | `#2a2c30` | the row, segment or strip that is chosen |
| `control` | `#2b2d31` | a resting button |
| `controlOn` | `#44474d` | a segment that is on |
| `hair` | `#ffffff14` (.08) | every seam |
| `ink` / `ink2` / `ink3` | `#f2f2f4` / `#c2c4c9` / `#9a9da4` | heading / body / caption |
| `accent` | `#6db8a8` | the primary action, what is on, what TUNE MIX did |
| `onAccent` | `#0b0d10` | type on an accent fill |
| `ok` `hot` `warn` `crit` | `#57b98d` `#d8c46a` `#e0a85c` `#f06a61` | meter and status |
| `keyRec` | `#e5534b` | the record key |
| `monitor` | `#6eafff` | the engineer's own ears |

Type styles: `Title 1` 22/28 SemiBold (−1 tracking) · `Headline` 13/18 SemiBold ·
`Body` 13/18 Regular · `Body Medium` 13/18 Medium · `Control` 12/16 Medium ·
`Caption` 11/14 Medium · `Verb` 11/14 SemiBold **+4 tracking** (the caps words) ·
`Mono Value` 11/14 Medium · `Mono Clock` 17/22 Medium.

Radii: **4** a broadcast key · **6** a control, chip, segment, sidebar row ·
**8** the transport well and a stage well · **10** a card or sheet · **12** the window.

Icons: 16 pt, **1.4 pt stroke, round caps and joins** — the 16 glyphs of the `Icon` component
(`61:9083`), carried in the code as their SVG path data (`Dine::Glyph`), never as bitmaps.

### The components sheet (`v3 · Components`) — what each one says

- **Button** (`61:9098`) 28 pt high, 6 pt radius. *On* = accent. **LIVE SAFE is the only amber
  control.** *Destructive* = red type on a hairline, never a red fill. *Disabled* = half plane.
- **Broadcast Key** (`61:9119`) DIM · MUTE · BYPASS · AUTOPILOT. Squarer (4 pt), a hairline
  edge and a lamp that is always visible, because they change what the broadcast hears. Lit in
  hot (DIM, BYPASS), red (MUTE), accent (AUTOPILOT).
- **Channel Key** (`61:9138`) R · A · M · S, 29 × 20, set 2 × 2.
- **Segment** (`61:9145`) a 2 pt-padded `control` track, 6 pt radius; the chosen item lifts to
  `controlOn`.
- **Sidebar Row** (`61:9156`) 28 pt, 6 pt radius, **sentence case**; selected lifts and the
  icon goes accent. Section headings are 11 pt `ink3`, never caps.
- **List Row** (`61:9171`) lamp · what (headline) · why (callout, ink3) · value (mono). No box:
  rows are separated by space and a selected row lifts. Every sheet list is built from it.
- **Meter** (`62:9124`) 6 × 416, 5 % steps; low zone is the theme accent, mid `hot`, top
  `crit`; peak hold 8 pt above the level.
- **Fader** (`62:9241`) a machined cap — two-stop gradient, a top bevel, one index line. The
  slot is a 4 pt well; **the unity mark is the one fixed line.**
- **Pan** (`62:9271`) 26 pt, 270° track, arc from centre, pointer dot, readout underneath.
- **Knob** (`62:9350`) 48 pt, 270° track, value arc in accent, one pointer; **the value sits
  under the knob, never inside it.** `Knob Large` (`112:10018`) is the 80 pt Simple-view knob.
  One class draws it everywhere: `DineKnob` (`app/ui/AppTheme.h`) - the dial size, the readout
  and the caption are what change, never the shape. Every rotary in the product is one of these,
  so a send, a trim, a channel's level and the monitor level are the same control.
- **XY Pad** (`62:9372`) BODY × VOICE and DRIVE × ROOM; 50/50 is the plan, under a dashed ring;
  values appear only once the puck has left it; hatched fence under LIVE SAFE.
- **Ribbon** (`62:9400`) ENERGY; the fill runs from the plan mark at the centre to the thumb.
- **Channel Strip** (`63:9488`) default · selected · soloed · muted · recording · LOW · SILENT
  · CLIP. No boxes inside a strip; sections are separated by space.
- **Transport v2** (`117:10151`) one filled glyph family in a 12 pt box; every key 28 × 28 at
  6 pt; the active one gets a quiet plane (`recGround` while recording) and the clock turns red.
- **Toolbar v3.4** (`117:32462`) is the live one. v3.0–v3.3 above it are superseded.
- **Sidebar** (`64:9437`) 208 pt. **Status Bar** (`64:9492`) 28 pt, sentence case.
  **Chain Foot** (`64:9511`) 44 pt, one quiet line.
- **Master Column** (`65:9316`) LUFS-I in an inset display, then the four readouts.
- **Sheet Header** (`65:9347`) Title 1 + one line of summary, close icon top right, no eyebrow.
  **Sheet Footer** (`65:9354`) Before/After left; actions right with the default (Keep) last.
- **Inspector Stage Card** (`65:9397`) the only card on the Inspector page: a 22 pt title row
  with the stage's name and the plain word it answers to, an Off / On segment at its right, the
  provenance line under it with a 6 pt lamp, then a well inset 24 - the drawing 20 inside it at
  220 tall, a row of 80 x 82 knob cells, the popups and buttons, and the choices as a 14 pt
  caption over a 28 pt segment track. The closing sentence sits along the card's foot.
- **Stage Controls / \*** (`84:*`) one symbol per chain stage: what that stage draws.

## 2. The shell

One **52 pt toolbar** across the whole window (the traffic lights are inside it at
x = 16/36/56, y = 20; the sidebar switch at x = 86). Left: the transport well at x = 224 —
`menubar` ground, hairline, 8 pt radius, 36 pt high, four 28 × 28 keys then a divider then the
clock in Mono Clock. The **solo pill** sits beside it while anything is soloed. Right, in order:
`TUNE LIVE MIX` · divider · DIM MUTE BYPASS AUTOPILOT · **LIVE SAFE** · the output picker ·
Mix Buddy. Everything is flat until it is on.

**Sidebar**, 208 pt, `sidebar` ground, a seam down its right edge, sentence case throughout:

```
Setup        Routing · (Audio device) · (Inputs) · (Outputs) · Purpose and sound
Workspace    Tracks · Mixer · Tune · Live · Inspector
Safety       Mix history · Scenes
Library      Sessions · Favourite mixes
             ───────────────────────────
             MOTU 16A
             48 kHz · 0 dropped buffers
```

The design orders the sections Library, Workspace, Safety, Setup; the code runs them the
other way up, and puts ROUTING's three everyday sections under it as indented child rows, in
the order its own tabs have them. Both are in §5 with the reason.

Caption 11 pt `ink3` at x = 16; rows 28 pt at x = 8, width 192, 10 pt left padding, 10 pt
between icon and label, 30 pt pitch; a caption sits 12 pt under the previous row and its first
row 22 pt under it. Folded, the sidebar becomes a **52 pt icon rail** (`112:10026`), not a
handle — each item keeps its icon and gains a tooltip with its shortcut.

**Status bar**, 28 pt, sentence case: Engine · CPU · Disk · Recording · Broadcast · Dropped ·
Live safe · Tempo · Autosaved, and at the right `N inputs · N to record`.

**Chain foot**, 44 pt: the picked-out channel as one quiet line — number, name, then each
stage that is on with its value; a stage TUNE MIX changed carries the accent lamp; a click
opens it in the Inspector.

## 3. The 40 frames, and where each one lands in the code

| # | Frame | node | Code |
| --- | --- | --- | --- |
| 01 | Sessions (library) | `70:9051` | `SetupPages.cpp` `SessionsPage` |
| 02 | Audio device | `70:9274` | `SetupPages.cpp` `DevicePage`, inside `RoutingPage` |
| 03 | Inputs (the patch) | `70:9496` | `SetupPages.cpp` `AssignPage` |
| 11 | Purpose and sound | `70:9783` | `SetupPages.cpp` `PurposePage` |
| 04 | Tracks | `71:9611` | `TracksPage.cpp` |
| 05 | Tune | `71:12025` | `MixPage.cpp` |
| 06 | Live (superseded by 06b) | `71:12363` | - |
| 06b | Live · decluttered | `161:18761` | `LivePage.cpp` (snapshots `17-live`, `27f-autopilot-live-safe`) |
| 07 | Inspector · Sample | `73:10195` | `AdvancedPage.cpp` / `ChainEditor.cpp` |
| 17a | Inspector · Input | `73:10629` | `ChainEditor.cpp` |
| 17b | Inspector · Filters | `73:10893` | " |
| 17c | Inspector · Gate | `73:11161` | " |
| 17 | Inspector · Corrective EQ | `73:11441` | " |
| 17d | Inspector · De-esser | `73:11719` | " |
| 17e | Inspector · Compressor | `73:11985` | " |
| 17f | Inspector · Transient | `74:11319` | " |
| 17g | Inspector · Tone EQ | `74:11581` | " |
| 17h | Inspector · Saturation | `74:11857` | " |
| 17i | Inspector · Width | `74:12109` | " |
| 17j | Inspector · Sends | `74:12584` | " |
| 17k | Inspector · Output | `74:12840` | " |
| 17l | Inspector · Limiter (master) | `74:13098` | " |
| 28 | Inspector · Simple view | `89:25394` | `AdvancedPage.cpp` |
| 08 | Mixer · strips | `75:12328` | `MixerPage.cpp` |
| 29 | Mixer · text size Larger | `89:25767` | `MixerPage.cpp` + `Dine::setTextScale` |
| 30 | Mixer · sidebar folded to the rail | `112:30939` | `MainView.cpp` |
| 09 | TUNE MIX is listening | `75:12415` | `MixPage.cpp` sheet |
| 10 | TUNE MIX is ready (result) | `75:12362` | `MixPage.cpp` sheet |
| 13 | TUNE CHANNEL | `75:12481` | `ChannelTuneSheet.cpp` |
| 12 | Check inputs | `75:12562` | `CheckSheet.cpp` |
| 16 | Sound like a record you know | `75:12950` | `ReferenceSheet.cpp` |
| 14 | Outputs | `75:13006` | `OutputsSheet.cpp` |
| 15 | Mix Buddy | `84:25602` | `ChatSheet.cpp` |
| 24 | Mix history | `88:22972` | `HistorySheet.cpp` |
| 25 | Reset the mix to raw | `88:23073` | `MainView::resetMixToRaw` |
| 26 | Favourite mixes | `88:23130` | **new** `FavouritesPage` |
| 27 | Export | `88:23516` | `MainView::exportMix` |
| 23 | Recover session? | `88:22923` | `MainView` recovery dialog |
| 21 | Appearance | `77:19087` | `ThemeSheet.cpp` |
| 22 | Getting started (the tour) | `77:19229` | `Tutorial.cpp` |
| — | Annotation | `91:26974` | "persistent mix history — the code shows it as a sheet; align to that" |

## 4. What is new, and what must not be lost

New surfaces: **Favourite mixes** (a page of its own), **Scenes** and **Mix history** as
sidebar items (both open the sheets that already exist), the folded **icon rail**, the
**Voices** Speaking/Singing control on TUNE, **Mix health out of 100**, and the **Aim at**
favourite on TUNE.

Everything in `docs/FIGMA-MAKE-PROMPT.md` §"FUNCTION INVENTORY" keeps its home. In particular
the design does not draw, and the code therefore keeps unchanged: the Mixer's **LIST** layout,
the three strip widths, the three track heights, the timeline's right-click menu, input
mappings, the menu bar, and every keyboard binding in that prompt's §"INTERACTION RULES".

## 5. Where the code and the design disagree, and why

Every one of these is a decision, not an oversight. The design is the brief; these are the
places the brief and the product's own rules met.

| What | The design | The code | Why |
| --- | --- | --- | --- |
| **The order of the groups** | Lead, BGV, Speech, Drums, Bass, Music | Drums, Bass, Music, Lead, BGV, Speech | `CLAUDE.md` fixes that order "everywhere", `mixBusInDisplayOrder` is the one place it lives, and a session's stored enum is that order. A different opinion about the same list is not worth breaking the rule for. |
| **LIVE SAFE, off** | Amber in every frame (they are all of a locked console) | Amber only while it is on; a hairline key when it is off | "Is the sound locked" is one of the six states that must always be unmistakable. A control that looks identical either way does not say it. |
| **TUNE CHANNEL on a TRACKS header** | Not drawn | Kept, and dropped first when the panel is narrow | It is a verb the inventory puts on the header; the design's header simply has fewer things on it. |
| **The stacked group bar on a session row** | Not drawn | Kept, before the date | It is the one thing on the library screen that says what kind of service a session was. The inventory asks for it by name. |
| **The Inputs table** | One flat table, three bulk buttons, All / Not used | The same, plus a list button beside the filter | Grouping by bus, one group at a time, the saved patches and Clear every assignment are all inventory items the design left no home for. They are one press away under that button and under Quick actions. |
| **LIVE's strips (06b)** | Six groups and FX returns | Every group bus, so Ambience too, then FX returns | A group the session has is a group the room can hear, so it has a strip; an unused one says "Off" rather than disappearing and moving every other strip along. The master has no strip on LIVE, as drawn: its headroom and loudness are on the health strip. |
| **LIVE's "What I hear" (06b)** | Two segment tracks, the headphones and a level | ... and a DIM key before the level | The headphones' DIM has no other home in DINE; dropping it would take away "turn my ears down to talk to someone" during a service. The two tracks stack when the rail is narrow. |
| **LIVE's rail cards when off (06b)** | Drawn on only | LIVE SAFE and Autopilot each say "... is off" with a "Turn on ›" link | The frame is of a locked console with Autopilot holding. Off, the card says so in a sentence; turning either off stays the toolbar's one press. |
| **LIVE's RECORD (06b)** | Not on the page | Not on the page | The transport's Record is in the toolbar on every workspace; the health strip says what is being written. |
| **ROUTING's sections** | Audio device, Inputs, Outputs | ... and Patches | The design draws no screen for the patches a church saves, and they are in the inventory. |
| **The Mixer's LIST layout, the three track heights, the timeline's menus** | Not drawn | Unchanged | The design drew the STRIPS console only. Everything else is in the inventory and is where it was. |
| **TUNE's right column** | TUNE MIX, Match to reference, Check inputs | ... then a quiet row of TUNE LIVE MIX, Mix Buddy, Undo, Redo, Mix history, Inspector | All six are one press elsewhere (the toolbar, the sidebar, the Mix menu), so nothing was at risk; keeping them on the panel costs one row and saves a journey during a service. |
| **The console's ground and gutter** | Strips edge to edge on the workspace ground | 8 pt between strips, and the console stands on `menubar` - the deepest plane in the product | A gutter the same value as a strip is not a gutter: at 3 pt on `window` the console read as one wall of faders, and on a busy service the eye has to find a strip before it can move it. The strip keeps `console`; only the ground under and between them changed. The LIST keeps `window`, because its rows are separated by their own hairlines and not by air. |
| **An empty slot on a strip** | A dash | An unlit lamp, and nothing beside it | The slot is still there - "nothing is said about this input" and "this input is fine" are different things, and a slot that comes and goes bends the line the console is read down. But a dash on every strip of a thirty-two channel console is thirty-two words that say nothing, and they were most of what made the strip head read as a wall. |
| **Boxes inside a strip** | `Channel Strip` (63:9488): no boxes; sections are separated by space | One box per slot: the gain chip, each of the three inserts and each of the two sends stands on its own plane - `item` when there is something in it, `inset` when the slot is reserved and empty | Space alone did the job in a frame with one strip in it. On a real console of thirty-two, the gain word, three insert lamps and two send rows are six lines of small type on the same ground, and they read as one loose list rather than as six things - the first thing a user said about the strip was that it is "just floating text". A slot is a thing of its own and is drawn as one, the way a DAW's channel strip draws its inserts. Nothing moved: the grid (`kSlotH` 22, `kRowGapIn` 4) is what it was, the slots stay reserved whether or not they are used - which is what keeps the pan knobs and the faders level straight across the console - and the plane is drawn behind what was already there. |
| **The Mixer's group rail** | `Groups - always on the mixer`: the buses pinned at full width beside the master | No rail; a group bus sits at the end of the family that feeds it and scrolls with the channels. Only the master is a fixed column | A console is read left to right, and a second fixed column turned the one run of strips into two lists with a seam between them. The GROUPS filter is what shows the buses on their own, and `busStripCount` is what the reachability test asks about. |
| **The sidebar's order** | Library, Workspace, Safety, Setup | Setup, Workspace, Safety, Library | It is the order of a Sunday: set the console up, work on the mix, then the two things that keep it safe. The library is last because opening a session is the one thing somebody does before any of that and never again during it - and the top of a list is where the eye starts. |
| **ROUTING's sections in the sidebar** | Not drawn: ROUTING is one row | Audio device, Inputs and Outputs as indented child rows under it | The device, the patch and the output feeds are what a soundcheck goes back and forth to, and reaching any of them was a workspace and then a tab. As children they are one press, they light their own row, and they fold away with the sidebar - the icon rail keeps ROUTING's icon alone, because a section of a workspace has no icon of its own. Patches is not among them: a saved patch is recalled once at the start and not touched again, so it stays a tab. |
| **The Inspector's channel rail** | A flat list of sixteen inputs | The same rows, under a quiet caption per group, with the bus strips and the master at the end | The design's frame is one sixteen-input service. A real console has group buses and a master to open, and they have to be somewhere; the caption is the only thing added. |
| **An EQ band** | A gain knob per band under the curve | ... and, for the band you picked, its frequency, Q and shape underneath | A band with no frequency or Q is not a band an engineer can use, and the Inspector is the one place those live. Picking is all that changes; the sound never does. |
| **The Inspector's head, rail and path** | Name, one line, Simple / Advanced, RE-TUNE; a rail of names and dots; a chip of a lamp and a name | The same, exactly | Implemented as drawn, which means the v2 Inspector lost: the head's IN / OUT meters, input gain, pan, level, MUTE, SOLO and EFFECTS; the rail's per-row level bar and the engine's rate / buffer / latency along its foot; and the path chip's number, icon, value and work bar. Every one of them is on MIXER, on the strip, on the chain foot, in the sidebar's device line or in the status bar - none is more than one press away, and the room the Inspector got back is spent on the thing it is actually for. |
| **The trail's stage lines** | Records with PUT BACK | The same | The v2 column listed every stage with a TUNED / EDITED / NOT USED badge and its sentence. The stage card now says who set *it* and why, in its own head and foot, so the column is what the design makes it: the channel's history, and the way back to any of it. The master's loudness readout moved with it - it is on the Mixer's master column and in the status foot. |
| **The signal path's lamp** | A lamp and the stage's name on a chip; the lamp reads as a switch | The lamp is a reading only - lit accent for DINE's setting, `monitor` for a hand edit, an empty ring for a stage out of the chain. The chip selects, and nothing else | A chip that selected when you clicked its name and toggled when you clicked its left 18 pt had two controls in one cell with only one of them drawn, so picking a stage read as broken - the same click did different things depending on a pixel. The switch is where it is written in words: `Off | On` at the top of the card the chip opens, and the chip's own menu. |
| **The Sends stage** | Not drawn (`Stage Controls / *` has no sends symbol) | The drawing is one ladder per effect return, and the controls are one 48 pt knob per send, in the same knob row every other stage uses | A send is a knob on every console ever built. It was a row of horizontal sliders, which read as neither a console nor a DAW; the knob cell was already the product's one rotary, so the sends simply became cells in it. |
| **A level on a reading** | A bar | A ladder - lit and unlit steps, the way the Meter component is drawn | The trims' "Arriving / After trim / Into the fader" and the sends' amounts are readings, and a filled bar with a track behind it is the shape of a control. Nothing on a drawing should look like something you could drag. |
| **A level that is not a channel fader** | Drawn as a fader wherever it appears | A knob: the Inspector's Simple LEVEL (56 pt), an output feed's level (26 pt), the monitor level (28 pt) | The fader is the mixer strip's, standing up, where a column of them line up and are read together. A level on its own - one feed in a table, one channel in an inspector, the engineer's own listen - is a rotary on any desk, and a fader lying on its side is the one shape in this product that read as unprofessional to a sound engineer. The Mixer's LIST row keeps its horizontal fader: it is a table, its rows do line up, and it is drawn as a real machined cap, not as a slider. |
| **The trail's gain card** | Records only | ... with a gain-staging card above them when the input needs attention | A preamp that is wrong is the one thing no amount of tuning can put right. It appears only when `InputAdvice::needsAttention()` is true, so a healthy input still leaves the column a history and nothing else. |

### The gaps: what the design draws that DINE does not do yet

*(EXPORT's Group stems, Raw multitrack, AIFF and its loudness choice were on this list and are
not any more: `app/native/MixBounce` writes all four, and `dine_app_tests` measures the
loudness one back out of the file it made.)*

- **The Appearance sheet's swatch grid.** The design shows the palette as unlabelled squares;
  the code keeps its labelled, hex-bearing cards, because that sheet is where a theme is
  *edited* and a square with no name cannot be.
- **MIX HISTORY as a right-hand panel.** The design's own annotation (`91:26974`) says the code
  shows it as a sheet and that the design should follow the code. It is still a sheet.

## 6. Order of work

1. **Language** — Inter into `LiveMixFonts`, the v3 tokens and metrics, `Dine::Glyph`.
2. **Shell** — one toolbar, the 208 pt sidebar and its 52 pt rail, the 28 pt status bar, the
   44 pt chain foot.
3. **Workspaces** — Tracks, Mixer, Tune, Live, Inspector.
4. **Setup** — Sessions, Favourite mixes, Routing (Audio device / Inputs / Outputs), Purpose.
5. **Sheets** — listening, result, TUNE CHANNEL, check, reference, outputs, Mix Buddy, history,
   appearance, export, recover, reset to raw, the tour.

Each step is verified by looking at `build/app-snapshots/*.png` (`scripts/dine.sh --shots`),
never by reasoning about layout code, and `dine_app_tests` must stay green throughout.
