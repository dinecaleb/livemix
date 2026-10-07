# Claude Code prompt — implement DLIVE v3 UI in dinecaleb/livemix

Paste everything below the line into Claude Code at the repo root. First commit `DLIVE v3.html` (one self-contained file — the whole app incl. the Live page, opens offline in any browser) to the repo at `docs/design/v3/`. If you have a Figma file, paste its URL into the Figma line below.

---

## References
Design mockup (open in a browser and click through every state; ⌘1–5 switch pages):
- [docs/design/v3/DLIVE v3.html](docs/design/v3/DLIVE%20v3.html) — the whole app in one file: shell, Mixer, Tune, Live (health, scenes/setlist, autopilot, speaking mics, listen), Inspector incl. Sample stage, Tracks, Routing, Sessions, Purpose, Favourites, all sheets and menus. It is a bundled file — use it for visuals and behaviour, don't read it as source code.
- Figma: _none yet — paste URL here if one exists_

Repo docs (read first):
- [CLAUDE.md](https://github.com/dinecaleb/livemix/blob/main/CLAUDE.md)
- [docs/DESIGN-V3.md](https://github.com/dinecaleb/livemix/blob/main/docs/DESIGN-V3.md)
- [docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md](https://github.com/dinecaleb/livemix/blob/main/docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md)
- [docs/FIGMA-MAKE-PROMPT.md](https://github.com/dinecaleb/livemix/blob/main/docs/FIGMA-MAKE-PROMPT.md)

Repo UI sources: [app/ui/](https://github.com/dinecaleb/livemix/tree/main/app/ui) · [app/native/SampleLibrary.h](https://github.com/dinecaleb/livemix/blob/main/app/native/SampleLibrary.h) · [src/DSP/SampleBank.h](https://github.com/dinecaleb/livemix/blob/main/src/DSP/SampleBank.h)

You are implementing the DLIVE v3 UI redesign in this repo (`app/ui`). The design reference is `docs/design/v3/DLIVE v3.html` (whole app incl. Live). It is an HTML mockup: read them for layout, exact colours, sizes, copy and behaviour. Do NOT port HTML/JS — rebuild natively in the framework `app/ui` already uses.

## Rule zero: lose nothing
1. Before writing code, produce `docs/design/v3/INVENTORY.md`: every existing feature, command, menu item, keyboard shortcut, setting, sheet, callback, DSP binding and persisted state in `app/ui`, `app/native`, and anything `app/ui` calls in `src/`. One line each with file:symbol.
2. For each inventory line, mark where it lives in v3 (page / sheet / menu / shortcut). Anything with no home in the mockup keeps its current location or goes into the nearest menu — never deleted. Ask me only if a feature truly conflicts.
3. Never change DSP, engine, audio-thread, file format or session-serialization code to fit the UI. UI binds to existing models/parameters. If a v3 control has no backend, wire it to a clearly-named stub (`// TODO(v3-backend)`) and list it in `docs/design/v3/GAPS.md`.
4. Work in small commits, one screen per commit, build + run existing tests after each. Keep old views compiling behind a `kUseV3UI` flag until the last step, then remove the flag only when INVENTORY is 100% mapped.
5. Read `CLAUDE.md`, `docs/DESIGN-V3.md`, `docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md` first and follow their conventions.

## Screen → file map (rebuild in place)
- Shell (sidebar, toolbar, chain strip, status foot, context menus, toast): [MainView.h](https://github.com/dinecaleb/livemix/blob/main/app/ui/MainView.h), [MainView.cpp](https://github.com/dinecaleb/livemix/blob/main/app/ui/MainView.cpp), `TransportBar.*`, `ChainStrip.*`
- Mixer: `MixerPage.*`
- Tune: `MixPage.*`, `MacroPad.*`
- Live: `LivePage.*`
- Tracks: `TracksPage.*`
- Inspector + stages: `AdvancedPage.*`, [ChainEditor.cpp](https://github.com/dinecaleb/livemix/blob/main/app/ui/ChainEditor.cpp); Sample stage also SampleBank.h, SampleLibrary.h
- Routing (device / inputs / outputs / patches): `RoutingPage.*`, `SetupPages.*`, `OutputsSheet.*`
- Sessions, Purpose and sound: `SetupPages.*`
- Favourite mixes: `FavouritesPage.*`
- Sheets: `CheckSheet`, `HistorySheet`, `ExportSheet`, `ChannelTuneSheet`, `ReferenceSheet`, `ChatSheet` (Mix Buddy), `ThemeSheet` (Appearance), `Tutorial` (tour), `ChoiceSheet` (reset, recover, scope, mic)

## Visual system (centralise in the existing `app/ui/AppTheme.h` / `AppTheme.cpp` — extend it, don't add a second theme)
- Window bg `#1C1C1E`; sidebar `#2A2A2D` r16, 0.5px `rgba(255,255,255,.09)` border, soft shadow, 8px inset; workspace `#161618` r14; panels `#1A1A1C`/`#232325`/`#262628`.
- Text: primary `#F5F5F7`, secondary `rgba(235,235,245,.62)`, tertiary `rgba(235,235,245,.34)`.
- Status: red `#FF453A`, orange `#FF9F0A`, yellow `#FFD60A`, green `#30D158`, blue `#0A84FF` (menu hover), link `#64D2FF`.
- Groups: Drums `#FF9F0A`, Bass `#30D158`, Music `#BF5AF2`, Lead `#64D2FF`, BGV `#5E8BFF`, Speech `#FF6482`, Ambience `#AC8E68`.
- Strip keys: R `#FF453A`, A `#5E5CE6`, M `#FF9F0A` (dark text), S `#FFD60A` (dark text); off = `rgba(255,255,255,.08)`.
- Type: system SF Pro Text 13px base; titles 15px/700; section labels 11px/600 tertiary; numbers SF Mono, tabular.
- Controls: pill buttons (r = h/2), segmented controls (2px pad, `rgba(255,255,255,.06)` track, selected `rgba(255,255,255,.16)`), 0.5px hairlines, primary action = white pill with dark text, uppercase 700 (TUNE LIVE MIX, TUNE CHANNEL).
- Fader law: −60…−20 dB maps to 0–40% travel, −20…+10 dB to 40–100% . Meters: fast attack, ~35% release per 100 ms, peak hold decays 0.25 dB/tick, peak > −0.5 dB turns red.
- Min window 1280×780.

## Shell
- Sidebar collapse works like macOS Mail: ⌃⌘S or the sidebar button hides it completely (no icon rail). The card's width animates 214→0 with its left margin, while its content (fixed 214px inside, so nothing reflows) slides 24px left and fades out. At the same time the traffic lights and a round sidebar button slide/fade into the toolbar's left edge. Reverse to open. ~420 ms, cubic-bezier(0.32,0.72,0,1); opacity ~240 ms. Respect Reduce Motion (instant).
- Sidebar: sections Set up (Sessions, Purpose and sound, Routing → Audio device / Inputs / Outputs, Check inputs with issue badge), Mix (Mixer ⌘1, Tune ⌘2, Inspector ⌘3, Favourite mixes w/ count, Mix history), Perform (Live ⌘4, Setlist), Record (Tracks ⌘5, Export). Footer: device status dot, device name/inputs, "48 kHz · 128 smp · N dropped buffers" in mono.
- The sidebar stays where the engineer put it on every workspace, Live included (owner, 2026-10-07; it used to fold away on Live). Every folding panel slides on the sidebar's curve (`Dine::Slide`).
- Toolbar: session menu (name + profile · Edited) → full File/Session menu (New ⌘N, Open ⌘O, Save ⌘S, Import Multitrack Folder, Add Reference Mix, Save Input Mapping, Export ⇧⌘E, Open Setup, Rename/Fix Inputs, Appearance, Getting Started, Reset Mix to Raw — disabled under LIVE SAFE); transport pill (RTZ, Stop, Play, Rec, Loop) + mono clock "of total"; SOLO pill (yellow, lists soloed, click clears); readiness pill (green Ready / orange N) → Ready sheet; TUNE LIVE MIX; broadcast group DIM (−20 dB) / MUTE / BYPASS / Auto(pilot); LIVE SAFE switch; output picker; Mix Buddy toggle.
- BYPASS: white banner under toolbar; you hear raw input; kept mix untouched; all faders locked (dragging shows a toast explaining why).
- LIVE SAFE: blocks every TUNE action and Reset with an explanatory toast.
- Chain strip (bottom, above status): selected channel's processing chain as chips; click opens Inspector.
- Status foot: engine status, plus the existing status info.
- Context menus: dark translucent, title row, check column, shortcut column, separators, blue hover. Toast: bottom-centre, auto-dismiss ~4 s; use for every confirmation.

## Pages
- **Mixer**: segmented Strips/List, All/Inputs/Groups, strip width S/M/L; New Window; TUNE CHANNEL. Strip = number, name, group colour bar, gain-health chip (Healthy / Clipping −6 / Digital +12 / Low +6 with tooltip advice), 3 insert slots, 2 send slots (empty = dashed), pan bar, fader + stereo meter + peak, R/A/M/S, "NOT HEARD"/"SOLOED OUT" tag, edited dot, routed-to label. Group buses after inputs, Master at the end. Click selects, double-click → Inspector, right-click menu (TUNE CHANNEL, Open in Inspector, record, monitoring Input/Auto/Off, link with next fader, fix assignments). List view = dense table. Right rail: quick inspector for the selected channel (EQ curve, key stages).
- **Tune**: macro pads + ribbon, scope (whole mix / one group / some channels), voices (speak/sing per vocal mic), group rows with meters + M/S, health score, loudness target menu (Broadcast −23, Stream −16, Platforms −14 LUFS…), voicing menu (Room and stream / Stream first / Room first / Recording), aim-at-favourite, reference mix. Left rail toggles with `[`.
- **Tune flow (shared by TUNE MIX / LIVE MIX / CHANNEL / MATCH REFERENCE)**: modal sheet → Listening phase (30 s countdown ring, list of what's heard/silent with reason) → Changes list (channel, change, plain-English why, confidence) → Before/After A/B → Keep (snapshot previous into Mix History) / Revert / Tune again. Blocked by LIVE SAFE.
- **Inspector**: Simple / Advanced toggle, RETUNE button, "Back to DLIVE" (restore tuned value). Simple = big plain-word knobs (Warmth, Clarity, Smooth, Steady, Clean-up) centred on DLIVE's tuned values, level ring, speak/sing, gain-health card. Advanced = stage list (Input, Filters, Gate, Sample, EQ, De-esser, Compressor, Transient, Tone, Saturation, Width, Sends, Output, Limiter, Loudness) with on/off, value, N/A states; per-stage editor; "who set this" line (DLIVE/TUNE at time vs you); change trail per channel.
- **Inspector · Sample stage** (drums only; Hi-Hat shown but off by default): kit picker, SOUND list grouped Built in / Your sounds / This session, HEAR IT, Import a sound; Blend, Sensitivity (threshold), Level, Pitch, Align, Rise, Mask, listen band lo/hi; Feel, Tuning, Polarity. Per-family defaults — Kick: blend 0.4, rise 6 dB, mask 40 ms, band 30–150 Hz · Snare: 0.3, 8 dB, 30 ms, 150 Hz–4 kHz · Toms: 0.5, 6 dB, 60 ms, 60 Hz–1 kHz · Hat: 0.2, 8 dB, 20 ms, 6–16 kHz. Live hit visualiser: scrolling waveform, threshold band, fired/held/ghost markers, velocity pad with last 16 hits, counters "hits · held · under the line". Bind to `SampleBank` / `SampleLibrary`; follow DRUM-SAMPLE-REPLACEMENT-SCOPE.md.
- **Live**: mix health, scenes/setlist with cue editing, autopilot, speaking-mic tiles, listen controls. Space is reserved by Live when on that page.
- **Tracks**: track list with record arm, clip names, rename/icon/source menu, match tracks to clips, move up/down (all workspaces follow).
- **Routing** (LIVE SAFE covers the whole workspace with "I know what I am doing", once per visit — keep RoutingPage's cover logic):
  - *Audio device*: input device, output device, sample rate, buffer (all menus), "What's arriving" meters, Rescan devices, Set up outputs…, Import a multitrack folder, Open Audio MIDI Setup, Continue → Inputs. Keep every DevicePage callback (onContinue, onContinueToAssign, onImportRecording, onSetUpOutputs, onBack, mic-permission toast).
  - *Inputs* (= AssignPage, keep every member): "N inputs want the preamp moved" banner with per-input advice cards + Check again (peak-HOLD reading, slow fall); toolbar: group chips + **Not used** chip, search, Use desk labels, Quick actions ▾ (group by bus on/off = AssignPage::grouped, Name everything from what it is, Pair every L and R, Fill in order, Select every input not used, Clear all), Patch ▾ (Save this patch, Apply a saved patch); bulk bar when selected: Set what it is, Feed a group, Fill in order, Name from what it is, Pair L and R, Not used, Deselect. **Fast entry for big channel counts (priority):** Name and What-it-is are inline-editable cells in the table — no dialog. Tab moves name → what it is → next row's name (⇧Tab back); Return / ↓ / ↑ move down/up the same column; Esc cancels. What-it-is is a typeahead with aliases (bv, oh, hh, di, vox, keys, amb, pb…) and setting it also sets the group. Pasting multi-line text into any name cell fills names down from that row (Tab-separated second column sets the role); ⌘V outside a cell opens "Paste a list of names" with a live "N names → inputs 05–18" preview. ⌘D = same as the input above (or copy the first selected row to the rest). Bulk "Number them" (BV 1, BV 2…). With no cell focused: ↑/↓ move, ⇧↑/↓ extend selection, Space toggles, Return edits the name, ⌘A selects all visible. Show a one-line shortcut hint above the table. Table: checkbox, ch, source port, arriving meter with peak-hold tick, name, what it is, feeds, pair, level verdict, record dot. Click = edit in right panel, ⌘-click = add to selection, **shift-click = range**, double-click = Inspector, right-click menu. "Not used" section lists unassigned device inputs (flags ones with signal) with Use it ▾. Right panel: input n · port, ↑/↓ to step, name field + suggested names ▾ (desk label / role / clip), arriving meter + verdict + advice, What it is ▾, Feeds ▾, Pair ▾, Source ▾, Record, Flip polarity, Listen in headphones, TUNE CHANNEL, Open in Inspector, Not used. Footer: counts, Clear all, Back, Continue → Purpose.
  - *Outputs* (= OutputsSheet embedded; keep every rule): **Broadcast** device picker (always stereo on 1-2) and **Solo** device picker = `OutputsSheet::showSoloDeviceMenu` (Nowhere / Here - everyone hears [only on a one-pair device, refused under LIVE SAFE] / every output device, the broadcast's own only "on its outputs 3-4" when it has ≥4 outs) with status "only you hear this / everyone hears solo / solo has nowhere to go yet". Feed rows (max 4): name (Broadcast, Feed 2–4, or Monitor), Source ▾ (Main mix, each used bus, "Just for you: My headphones (whatever is soloed)"), Destination ▾ (device pairs + Not routed, "not on this device" warning), Level knob −60…+12 (main mix capped at 0, double-click = 0.0), MONO (disabled on Broadcast and Monitor), MUTE (amber), × remove (not on feed 1). Add a feed picks the next free pair or toasts that every pair is in use. "N of 4 feeds in use" + headphones summary.
  - *Input Mappings*: list of saved patches with Apply and ⋯ (Rename, Delete), Save this session's patch, Import a patch.
- **Toolbar output button** shows "Main · solo <device>" and opens Broadcast, every Solo choice (same menu), Set up outputs…. The Live page keeps its own solo picker (same shared menu).
- **Sessions**, **Purpose and sound** (purpose: Service / Stream / Record; profile: Modern Gospel / Worship…), **Favourite mixes**.

## Sheets
Ready (readiness rows: device, inputs, recording, disk… each with ✓/! and Fix action), Check inputs, Mix history, Export, Reset to raw (snapshots first), Recover, Appearance, Getting Started tour (stepped), Reference mix, Tune scope, Mic (speak/sing), Mix Buddy chat (try it / hear it / undo). Esc closes the topmost: menu → sheet → tune → chat.

## Shortcuts (keep all existing ones too)
Space play/stop · Enter RTZ · R record · L loop · B bypass · T tune channel · M marker · [ / ] fold panels · ⌘1–5 pages · ⌃⌘S sidebar · ⌘S save · ⌘Z undo · ⌘E split at playhead · ⌘N new · ⌘O open · ⌘= / ⌘− / ⌘0 zoom · ⇧⌘E export · Esc. Ignore keys while a text field has focus.

## Performance (hard requirement — equal weight to visuals)
The app runs live on a church laptop next to the audio engine. The UI must never cost the audio thread anything, feel instant, and stay small in memory. Treat any regression here as a bug.

**Audio-thread safety**
- UI never locks, allocates, or calls into the audio thread. Read meters/levels/hit events via lock-free single-producer/single-consumer FIFOs or `std::atomic<float>` snapshots that the engine already writes (add them in the engine-facing model layer only if missing — no DSP changes). Parameter writes go through the existing parameter/atomic path; never `MessageManagerLock` from audio code.
- No `juce::String`, `std::vector` growth, logging or `AsyncUpdater::triggerAsyncUpdate` storms originating on the audio thread.

**One clock for everything that moves**
- A single shared `juce::VBlankAttachment` (fallback: one 60 Hz `juce::Timer`) in MainView drives all meters, the sample-hit visualiser, playhead, clocks and countdown rings. No per-component timers. Components register/unregister on visibility (`visibilityChanged`, parent page hidden, window minimised/occluded → zero ticks).
- On each tick a component compares new values to last-painted values and calls `repaint (dirtyRect)` only for the pixels that changed (meter bar delta + peak tick, not the whole strip). Text labels (dB readouts, clocks) update at ≤15 Hz and only when the formatted string changes.
- Idle target: with transport stopped and no signal, the UI does no repaint work at all.

**Painting**
- Static chrome (strip backgrounds, group bars, scale ticks, knob faces, fader tracks, EQ grid) rendered once into cached `juce::Image`s (`setBufferedToImage(true)` or explicit caches) and invalidated only on resize/theme/scale change. Dynamic layers paint on top.
- Mark opaque components `setOpaque(true)`; avoid full-window transparency. Drop shadows/blur: pre-render to an image once; never per-frame `DropShadow` or `ImageEffectFilter`.
- Sample-hit visualiser: ring-buffer of precomputed min/max columns (one per pixel column, computed as hits arrive), blit-scroll the previous frame by the elapsed pixels and draw only the new columns. Glow/flash = pre-rendered sprite scaled/alpha'd, not a live blur. Cap history to what's visible (~3 s).
- Fonts and `juce::GlyphArrangement`s for repeated mono numerals cached; no `Font` construction in `paint()`.
- Try `juce::OpenGLContext` attached to the top-level only if profiling shows CPU-bound painting; keep the software renderer path working.

**Big channel counts (64–128 inputs)**
- Mixer strips, Inputs table, Tracks list, History and Changes lists are virtualised: only visible rows/strips exist as components (`juce::ListBox`/`TableListBox` or a recycled-pool viewport). Scrolling must hold 60 fps at 128 channels.
- Inline editing in the Inputs table uses one shared `TextEditor` moved between cells, not one per row.
- Off-screen pages are not painted or ticked; pages build lazily on first visit and keep their state, but release large caches (waveform images, visualiser buffers) when hidden for >30 s.

**Memory**
- Target: UI adds < 150 MB RSS at 128 channels with every page visited; no growth over a 3-hour session (run with the transport looping and check). No leaks under JUCE's leak detector.
- Image caches sized to on-screen pixels × display scale, never to content length; one shared cache per asset type, purge on scale change.
- Undo/Mix History snapshots store parameter deltas, not full copies; cap the in-memory list and spill older entries to the session file the way history already persists.
- Sample library: decode on import as today; the UI shows names/metadata only and never loads audio to display a list. HEAR IT plays through the existing engine path.

**Latency of interaction**
- Input → visible response within one frame (≤16 ms) for faders, knobs, toggles, menu open and page switch. Fader drags write the parameter immediately and repaint only the fader + readout.
- Anything slow (device rescan, folder import, export, TUNE analysis, waveform builds) runs on a background `juce::ThreadPool` job with progress, never on the message thread. Results are posted back via `MessageManager::callAsync` with a weak reference guard.
- Animations (sidebar collapse, sheet open) use `juce::ComponentAnimator`/`VBlankAttachment` with transforms on cached images; Reduce Motion = instant.

**Prove it**
- Add a debug-only overlay (toggle ⌥⌘P) showing UI frame time, repaints/s, message-thread load and RSS.
- Before/after numbers in `docs/design/v3/PERF.md`: CPU % of the UI process idle and with 64 ch metering, frame time p99 while scrolling the Mixer at 128 ch, RSS after visiting every page, and audio dropouts (must be 0) during a 30-min soak with all pages cycled. Profile with Instruments (Time Profiler + Allocations) and fix the top hotspots.

## Copy
Use the mockup's wording verbatim — plain words, no jargon, every toast says what happened and how to undo.

## Done when
- INVENTORY.md fully mapped, GAPS.md lists every stub.
- Build passes, existing tests pass, no engine/DSP diffs.
- PERF.md targets met: 0 dropouts, idle UI ≈ 0% CPU, 60 fps at 128 ch, < 150 MB UI RSS, no growth over the soak.
- Each page screenshot compared side-by-side with the mockup; note deviations in `docs/design/v3/DEVIATIONS.md`.
