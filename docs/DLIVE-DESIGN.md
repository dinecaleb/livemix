# DLIVE: the desktop design

The v2 desktop, the title row, the macro pads, themes, the tutorial, the resizable TRACKS panel, desk sizes and the frame budget. Moved verbatim from the old CLAUDE.md (2026-09-19); themes in detail are `docs/THEMES.md`.

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
- **THE TITLE ROW CARRIES THE TABS** (2026-09-18, the client's review). The session-name popover is gone from the
  title row (everything it offered is in File / Help and the sidebar; `setupPopover` still exists, anchored to the
  sidebar switch, for Getting started); the five workspace tabs sit centred in the title row between the wordmark
  and the counts (`kCountsW`), and the toolbar holds the transport centred with BYPASS / LIVE SAFE / the output on
  the right. Every plane now has a **hairline seam** (`Dine::hair`): under the title row and the toolbar, the
  sidebar's right edge, the TUNE rail, both Inspector rails, the chain foot and the status foot. The LIVE page's
  ENGINEER MONITORING card has the **solo device picker** at the right of its chip row
  (`OutputsSheet::showSoloDeviceMenu`, shared with the Outputs sheet), so "solo has nowhere to go yet" is fixed
  where it is read.
- **THE V2 DESKTOP (2026-09-17, the Claude Design file `DLIVE Desktop v2.dc.html`, project
  `8592889b-694f-4bb0-8f28-598c057014f5`).** The window is the design, one to one. Top to bottom: a 52 px
  **title row** (`Dine::Metric::titleRow` - the sidebar switch, the session's name with its popover in the
  middle, "N inputs / N to record" and AI MIX CHAT on the right), the 56 px **toolbar** (the transport in its
  own pill with the clock, the five tabs `TRACKS MIXER TUNE LIVE INSPECTOR` centred - `MainView::kWorkspaceTabs`
  = 5, Cmd-1..5 - then BYPASS, LIVE SAFE ON/OFF and the output popup), then the body. Down the left is the
  184 px **sidebar** (`MainView::Sidebar`: LIBRARY > Sessions; SET-UP > Audio device / Inputs / Purpose and
  sound; WORKSPACE > the five tabs again as rows; the device, its rate and its dropped buffers along the foot),
  folding to a 17 px handle (`Ctrl-Cmd-S`, View > Show/Hide Sidebar). Beside it the workspace, then the
  picked-out channel's **chain along a 48 px foot** on every workspace (`MainView::chainFoot`, reading
  `selectedChannel()` or the last channel picked anywhere - TRACKS and MIXER no longer carry their own,
  `setFootShown (false)`), then the 50 px **status foot** (`StatusBar`: Engine, CPU - `AppServices::cpuLoad` -
  Disk, Recording, Broadcast, Monitor, Dropped, Live safe). Setup is not a mode: the four setup screens are
  rows in the sidebar, and the session popover lists the same four steps with their values.
  A workspace's own side panels belong to the workspace: TUNE's INPUTS rail (198 px, left, a name and
  TUNE CHANNEL per row - clicking the row picks the channel out for the chain foot) and the Inspector's
  CHANNELS rail (200 px) and WHAT DLIVE DID column (280 px), each folding to a named handle with `[` / `]`.
  The window-wide channel list (`ChannelRail`) is gone: the design has one navigation and per-page rails, and
  two lists of the same channels on one screen was the thing it removed.
  **Since 2026-09-17 (the client's review):** the sidebar is the same plane as the title row (`Dine::sidebar` =
  `Dine::title`, never the black desk), the DLIVE wordmark sits at the left end of the title row, after
  the sidebar switch (`MainView::kWordmarkW`) so it is on screen whatever the sidebar does, a panel handle (`DinePanelTab`) is a
  small key with a chevron pointing the way the panel will move (never three dots), and **green is not a brand
  colour**: a soloed tile or a tuned chip sits on the neutral lifted plane (`soloGround` = `selected`) with the
  teal lamp / hairline saying what it is - `Dine::ok` is only ever a status chip's colour.
  **The palette and type are the design's, not the site's.** The desk is `#070809`, the application panel
  `#0e1014` (`Dine::window`), the toolbar / rails / status foot `#13161c`, a page's tool row `#10131a`, a
  card `#1a1e26`, a row `#1c212b`, what is chosen `#222830`, a resting control `#2a303a` (hover `#3e4656`, a
  setting that is on `#4e5664`), an item inside a card `#161a22`; ink `#f4f5f7` / `#a8b0bc` / `#6b7380` /
  `#4e5664`; the accent is the **teal `#6db8a8`** (hover `#8ed0c2`, near-black type on it) and it is spent on
  the primary action, the active tab, what is selected or soloed, what DLIVE tuned, and the meters; ok
  `#57b98d`, hot `#cbbf6a`, warn `#e0a85c`, crit `#e06a64`, monitor blue `#6eafff`; the buses keep their
  colours (`Dine::busTint`). **Every surface is flat**: no gradients, no glows, no outlines - `drawCard`
  ignores the plain hairlines and only draws an edge that carries a meaning (a solo, a warning). Radii are
  12 / 10 / 8 / 6. Type is **Barlow** for words and **IBM Plex Mono** for every number, both embedded
  (`Dine::text` / `Dine::mono` / `Dine::caps` go through `LiveMixLookAndFeel::body` / `mono`), so the booth
  Mac reads like the mock whatever it has installed. A fader is a 2 px line and a flat pale cap (24 x 9
  standing, 9 x 14 lying); a meter is a translucent well and **one gradient fill** (accent to two thirds,
  yellow, red - `Dine::fillMeter`), never segments; a console key is `#2a303a` off and its own colour with
  dark type on; a chip is a 20 % tint of its colour (`Dine::drawStatusChip`, `Dine::mix`); a modal sheet is
  `Dine::drawSheet` centred over a `#070809` scrim at 0.86-0.88; the chat is a 380 px panel down the right of
  the workspace column, not a modal. The "lime is the signal" rule survives with the teal in its place.
  Colour literals still do not belong outside `AppTheme`.
  **The mixer's frame budget** is the design's problem as much as its look, and the v2 rebuild fixed the
  console that crawled at 48 channels: a strip re-reads only atomics every tick (meters, mute, solo, fader),
  re-reads its inserts when a hash of its `ChannelParameters` changes (`chainHash`, via `forEachDspField`),
  re-reads the gain advice twice a second (`MixerPage::tick`), and a meter is one fill whatever its height.
  The bank is opaque so a scroll never repaints the page under it. `dlive_ui_snapshots --frames 48 120`: a
  full repaint of MIXER went from 38 ms to 6.5 ms, TRACKS 61 to 10, INSPECTOR 68 to 23 (a 30 Hz frame is 33).
- **THE MACRO PADS** (2026-09-18, `app/ui/MacroPad.{h,cpp}`, on TUNE). The five macros are two two-axis
  pads and one ribbon: BODY x VOICE (`MixMacro::Bass` across, `Vocals` up), DRIVE x ROOM (`Space` across,
  `Drums` up) and ENERGY on a ribbon under them. 50 / 50 is the plan, at dead centre under a dashed ring; a
  press anywhere jumps the puck there and the drag is absolute; double-click re-centres that pad; three snaps
  under each pad (Speech / Choir / Plan, Tight / Room / Plan) ease the puck over 140 ms and read as chosen
  only while the puck sits exactly on the point. The pad owns nothing: every change goes through
  `MixController::setMacro`, and the pads read the controller every tick. Under LIVE SAFE the controller
  clamps each macro to `liveSafe::macroRange` (`LiveSafePolicy::maxMacroExcursion`, 50 +/- 25) and the pads
  draw that fence hatched with the reason in the tooltip. TUNE is three columns: the inputs rail, the middle
  (GROUPS stretching, MASTER, the pads - sized in `MixPage::layout` so the middle never scrolls; below the
  floor the pads drop their snap rows), and a 296 px right panel (`MixPage::SidePanel`, its own scroll: the
  verbs, the card saying which pad is held or moved, MIX HEALTH) folding with `]`. Mix > Centre Macro Pads
  (id 401) is `MixPage::centreMacroPads`. TUNE LIVE MIX is also a button in the title row
  (`MainView::tuneLiveButton`, command 405), so a live tune starts from any workspace. Verify with the `16`,
  `16c` (the fence) and `23` snapshots and `--sizes`. **Since 2026-09-18 (the client's review: "too messy, text
  everywhere")** the band is quiet: MASTER is its three controls and one mono readout (NOW / TARGET), the lift's
  sentence lives on the Raise button's tooltip; a pad's head is its title alone, the values are read inside the
  square (under the top corner words) only once the puck has left the plan, the corner words and the ribbon's ends
  are small `ink4` caps, the snaps sit in one track, and the two pads + ribbon are one block centred in the column
  (`kMaxPad` 236).
- **THEMES** (2026-09-17, `docs/THEMES.md`). The look is a table of named colours and a theme is that table
  written down: pick one under View > Appearance and every window follows; nothing about the session or the
  mix depends on it. `app/native/ThemeStore` (JUCE-core, tested in `ThemeTests.cpp`) is the document
  (`~/Music/DLIVE/Themes/<name>.dlivetheme.json`, schema 1, a *partial* map of key -> `#rrggbb`/`#aarrggbb`
  resolved over its `basedOn` built-in over Studio Teal), the five built-ins (Studio Teal = the design, Lime
  Desk, Slate, Tape, Daylight) and the preference (`~/Music/DLIVE/preferences.json`, `theme`). The `Dine::`
  tokens are now mutable `inline` variables with the design as their initial values; `Dine::applyTheme`
  writes them through `Dine::themeBindings()` (the one key -> token table), `Dine::refreshAllWindows()` /
  `refreshWindow` re-applies the look-and-feel and `sendLookAndFeelChange`s every window (a repaint drops the
  strips' cached images). **Nothing captures a token at construction unless it re-reads it in
  `lookAndFeelChanged()`**: `DineButton` / `PanBar` hold an optional tint, text editors go through
  `Dine::styleTextEditor` from the constructor and from `lookAndFeelChanged()`, `DineKey::setTint`. The
  Appearance sheet (`app/ui/ThemeSheet`) is the editor: a swatch per token with a live picker, a built-in is
  never overwritten (editing one saves a theme of your own with only the diff), Import reads before it copies,
  Export writes a complete file. The snapshot tool checks the bindings and that `AppTheme.h` equals the Studio
  Teal preset, renders `30-theme-*` / `31-appearance`, takes `--theme <name>` for the whole set, and never
  writes the preference (`MainView::setStoredThemeUsed (false)`, `ThemeSheet (persisting = false)`).
- **GETTING STARTED** (`app/ui/Tutorial`) is what DLIVE says to somebody who has never opened it: seven sentences
  in the order a Sunday happens - name the inputs, press record, let DLIVE listen, keep or undo what it did, lock
  the desk - each one putting the workspace it is talking about on screen and ringing the control it means
  (`MainView::spotlight`, read from the live components so the tour can never ring empty space). It is a coach,
  not a wizard: nothing is blocked behind it, Esc or "Skip the tour" ends it, and it never touches the session.
  It opens by itself only on a genuine first run (no library **and** no assigned inputs) and is remembered in
  `~/Music/DLIVE/.getting-started-seen`; after that it is Help > Getting started and the session popover.
  `MainView::setAutoTutorial (false)` is how the snapshot tool keeps it out of every other state.
