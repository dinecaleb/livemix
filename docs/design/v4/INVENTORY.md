# DINE v4 — the inventory

Rule zero of the v4 handoff (`HANDOFF.md`): **lose nothing.** This is every feature, command, menu item,
shortcut, setting, sheet, callback, model binding and persisted state the UI has at `8b9f4cf` (2026-10-05),
one line each with `file:symbol`, and where it lives in v4. It was read line by line from `app/ui`,
`app/Main.cpp` and the `app/native` surface the UI calls.

How to read a line: `[kind] what it does — file:symbol — v4: <home>`. "v4: KEEP current — X" means the mockup
draws no home for it and it stays where it is (or under the nearest menu); it is never deleted. A rebuilt
screen is done only when every line for it is ticked off against the new code.

The handoff calls the product DLIVE and the design v3; the product is DINE, and `docs/DESIGN-V3.md` is the
previous (Figma) design, so this one is filed as v4. Stubs and missing backends are in `GAPS.md`.

Sections: 1 Shell (window, toolbar, sidebar, menus, keys, sheets host, status and chain foot, widgets) ·
2 Mixer, Tune, Live, Favourite mixes · 3 Inspector and Tracks · 4 Set-up pages and sheets ·
5 The backend surface the UI binds to (calls, persisted state, meters).

---

## 1. Shell

Scope: `app/Main.cpp`, `app/native/WindowChrome.mm` (window-button calls only), `app/ui/MainView.{h,cpp}`,
`TransportBar.*`, `ChainStrip.*`, `AppServices.h`, `WorkspaceGuide.*`, `Tutorial.*`, `ChoiceSheet.*`, `AppTheme.{h,cpp}`.
Brief: `docs/design/v4/HANDOFF.md`. It still says "DLIVE" and "v3"; it means DINE v4. "v4:" says where each item lives in the v4 design.
Abbreviations: MV = `app/ui/MainView.cpp`, MVh = `app/ui/MainView.h`, Main = `app/Main.cpp`.

### Application and window (Main.cpp)

- `[behaviour]` Single instance only — `Main:DineApplication::moreThanOneInstanceAllowed` — v4: KEEP current (app level)
- `[behaviour]` Folders migrated from DLIVE on launch — `Main:DineApplication::initialise -> AppFolders::migrateFromDlive` — v4: KEEP current (app level)
- `[behaviour]` Telemetry starts first. URL and key come from the build or from env DINE_SUPABASE_URL / DINE_SUPABASE_ANON_KEY — `Main:initialise` — v4: KEEP current (no UI)
- `[behaviour]` Sample library loaded once, then handed to the controller and the services — `Main:initialise, HostServices::setSampleLibrary` — v4: KEEP current
- `[state]` Last-session pointer `~/Music/DINE/.../last-session.txt` reopens the last document on launch — `Main:lastSessionPointer, initialise` — v4: KEEP current
- `[behaviour]` On launch the restored session opens through `openState("launch")`. The page is Assign when there are no inputs, else Tracks, and any recovery note is toasted — `Main:initialise restore lambda` — v4: KEEP current (land on the v4 page equivalents: Routing > Inputs / Tracks)
- `[behaviour]` Toast "saved by a newer DINE, not opened" when the last session came from a newer build — `Main:initialise` — v4: toast
- `[behaviour]` An unreadable document with a readable autosave becomes a recovery offer — `Main:initialise` — v4: Recover sheet
- `[sheet]` Microphone explanation before the macOS prompt on a restored session (Continue / Not now). Not now opens the output only — `Main:initialise askFirst -> MainView::explainMicrophone` — v4: ChoiceSheet "mic" (HANDOFF Sheets: Mic)
- `[sheet]` Recovery offer: Recover / Open last saved / Keep both. Each column lists facts from `SessionStore::summarise`. With no readable document it recovers without asking — `Main:DineApplication::offerRecovery -> MainView::offerRecovery` — v4: Recover sheet
- `[callback]` Recover: load the autosave, `openState("recovery")`, `sessionReplaced`, save, dismiss, then toast. If the save fails, toast and the offer comes back next launch — `Main:offerRecovery onRecover` — v4: Recover sheet
- `[callback]` Keep both: the autosave is saved as "<name> (recovered)" and the original is left untouched — `Main:offerRecovery onKeepBoth` — v4: Recover sheet
- `[callback]` Open last saved: dismiss the autosave and put any unfinished takes back (`recoverTakesNow`) — `Main:offerRecovery onOpenSaved` — v4: Recover sheet
- `[callback]` Device lost: a toast with `deviceLostSentence`, and the wording changes while recording — `Main:host->onDeviceLost` — v4: toast (also the sidebar footer status dot)
- `[callback]` Device returned: a toast with `deviceBackSentence`, checking the channels needed against the channels the device has — `Main:host->onDeviceReturned` — v4: toast
- `[callback]` A different unit with the same name was plugged in: toast, not opened — `Main:host->onDifferentUnitReturned` — v4: toast
- `[callback]` Device list changed: forget the UID cache, `openWantedConsoleIfBack` (toast), refresh the Device page if it is visible — `Main:host->onDeviceListChanged` — v4: Routing > Audio device (Rescan) + toast
- `[callback]` Telemetry milestone toast "Milestone: <title>. <sentence>" — `Main:wireTelemetry onMilestone` — v4: toast
- `[callback]` Usage events forwarded from the controller — `Main:wireTelemetry controller->onUsage` — v4: KEEP current (no UI)
- `[cmd]` Quit while recording: "DINE is recording" alert with Keep Recording (Esc) / Stop and Quit (Return) — `Main:systemRequestedQuit` — v4: KEEP current (AlertWindow; restyle only)
- `[cmd]` Quit while exporting: "DINE is exporting" alert with Keep Exporting / Stop and Quit — `Main:systemRequestedQuit` — v4: KEEP current
- `[behaviour]` Shutdown stops the export (15 s wait), stops the engine, saves for quit, then closes the autosave cleanly (or flushes it if the save failed) — `Main:shutdown, MainView::stopExportAndWait` — v4: KEEP current
- `[behaviour]` Window close button means quit (systemRequestedQuit) — `Main:MainWindow::closeButtonPressed` — v4: KEEP current
- `[pref]` Main window min 1180x760, opens at 1520x960 centred. **The size is not persisted** — `Main:MainWindow ctor` — v4: min 1280x780 - taken 2026-10-06
- `[behaviour]` Native title bar is transparent and the traffic lights sit inside the 52 pt toolbar — `Main:MainWindow ctor -> putWindowButtonsInTheToolbar` (WindowChrome.mm) — v4: the traffic lights move between the sidebar card and the toolbar edge on collapse (HANDOFF Shell). WindowChrome.mm must follow
- `[behaviour]` Dragging the empty toolbar moves the window; double-click follows the System Settings title-bar action — `MV:MainView::mouseDown/mouseDoubleClick -> onToolbarPressed/onToolbarDoubleClicked`, WindowChrome.mm `dragWindowFromToolbar/toolbarDoubleClicked` — v4: toolbar (keep)
- `[menu]` The macOS menu bar is attached by the app (not by the snapshot tool) — `Main:MainWindow ctor setMacMainMenu(view().getMenuModel())`, `MVh:getMenuModel` — v4: KEEP current (macOS menu bar)
- `[behaviour]` No file-open or Finder drop handler at app level (no `anotherInstanceStarted`). File drops only on TRACKS (`TracksPage::filesDropped`) — v4: Tracks (other inventory)

### HostServices (AppServices implementation the UI calls) — Main.cpp

- `[callback]` openDevices / openOutputOnly / changeOutput are all refused under LIVE SAFE (`deviceChangeLocked`) — `Main:HostServices` — v4: Routing > Audio device, toolbar output picker
- `[callback]` setBufferSize is refused while recording or under LIVE SAFE — `Main:HostServices::setBufferSize` — v4: Routing > Audio device (buffer menu)
- `[callback]` Dual-output solo: setSoloOutputDevice handles same device (outs 3-4), another device (built aggregate) and off; headphonesSummary; outputDisplayName ("A  +  B") — `Main:HostServices::setSoloOutputDevice/headphonesSummary/outputDisplayName` — v4: Routing > Outputs Solo picker + toolbar output button ("Main · solo <device>")
- `[callback]` retryHeldInput / inputHeldBack / askForInputPermission (MicPermission) — `Main:HostServices` — v4: Mic sheet + toast
- `[callback]` importSample copies the file into the session's Samples folder; toast text varies for an unsaved session — `Main:HostServices::importSample` — v4: Inspector > Sample stage "Import a sound"
- `[callback]` importAudio plans and applies a multitrack import. A fresh session is named after the folder; opens output-only if no device is open — `Main:HostServices::importAudio` — v4: File/Session menu "Import Multitrack Folder", Routing > Audio device import
- `[callback]` snapshotExport / exportMix run on a worker (MixBounce: stereo / group stems / raw multitrack; WAV/AIFF/MP3; loudness As mixed / -14 / -16) — `Main:HostServices` — v4: Export sheet
- `[callback]` newSession closes the current one cleanly, makes "Untitled N", and keeps the device — `Main:HostServices::newSession` — v4: ⌘N, session menu
- `[callback]` saveSession / saveSessionAs / loadSession, each with its own refusals (recording, name taken, newer build, unreadable, current not savable) — `Main:HostServices` — v4: ⌘S, session menu, Sessions page
- `[state]` trackPanelWidth is persisted in the session document — `Main:HostServices::trackPanelWidth/setTrackPanelWidth`; set by `MV:tracksPage->onPanelWidthChanged` — v4: Tracks
- `[behaviour]` Autosave runs on its own worker. The last-session pointer is written for unsaved docs; failure is reported through `autosaveFailing` — `Main:HostServices::autosaveNow, SessionAutosave` — v4: KEEP current (status foot "Disk not saving")
- `[behaviour]` A stand-in device is never saved over the session's wanted devices — `Main:HostServices::deviceChoice` — v4: KEEP current
- `[behaviour]` Recovery note collects missing audio, unresolved drum sounds, device plan sentence and repaired takes, and is toasted on open — `Main:HostServices::openState/openDevicesFor/takeRecoveryNote` — v4: toast

### Main window layout (MainView)

- `[widget]` MainView is the one window component; it owns every page, sheet, toast and window — `MVh:class MainView` — v4: Shell
- `[state]` Page enum {Sessions, Device, Assign, Purpose, Tracks, Mixer, Tune, Live, Inspector, Outputs, Maps, Routing, Favourites}. Appended to, never reordered: WorkspaceGuide and Tutorial index it by int — `MVh:MainView::Page` — v4: page map (keep the int values or update `WorkspaceGuide::indexForPage` and `Tutorial::steps` page ints)
- `[behaviour]` showPage(Routing) resolves to the section it was last on — `MV:showPage` — v4: sidebar Routing
- `[behaviour]` Leaving Routing resets the LIVE SAFE confirmation ("once per visit") — `MV:showPage -> routingPage->resetConfirmation` — v4: Routing (HANDOFF: keep cover logic)
- `[behaviour]` Leaving Routing with a pending graph change reconfigures and rebuilds Inspector, Mixer, windows and Tracks. This is the 2026-10-05 fix for stereo pairs — `MV:showPage` — v4: KEEP (page-switch logic, not UI)
- `[behaviour]` Telemetry `live_view_opened` when entering Live — `MV:showPage` — v4: Live
- `[behaviour]` Every page refreshes or rebuilds on show (Sessions, Favourites, Device, Assign, Purpose refresh; Tracks applies the stored panel width and rebuilds; Mixer, Live, Inspector rebuild) — `MV:showPage` — v4: each page (HANDOFF wants lazy build; keep the refresh-on-show)
- `[behaviour]` showPage calls maybeShowGuide, updateChrome, resized, then grabKeyboardFocus — `MV:showPage` — v4: Shell
- `[behaviour]` Routing hosts Device and Assign as children; covered under LIVE SAFE, they get empty bounds — `MV:ctor routingPage->host, resized` — v4: Routing
- `[behaviour]` Startup page: Sessions if any are saved, else Device — `MV:ctor` — v4: Sessions / Routing > Audio device
- `[behaviour]` Auto tour on first launch: no sessions, no inputs, not seen — `MV:ctor callAsync showTutorial` — v4: Getting Started tour
- `[state]` contentBounds / columnBounds: toolbar 52, sidebar 208 or rail 52, chain foot 44, status 28, Mix Buddy panel 380 (max half width) — `MV:contentBounds/columnBounds/resized` — v4: Shell (sidebar 214, hides fully)
- `[behaviour]` A sheet covers the workspace column (not the sidebar or toolbar); the chat is a right-hand column that narrows the workspace — `MV:resized` — v4: sheets; Mix Buddy panel
- `[behaviour]` The on-air strip is a 2 px gradient across the top, pulsing teal while live and red while recording — `MV:timerCallback onAir, paint` — v4: KEEP current (no v4 home; keep the 2 px strip)
- `[widget]` "DINE" wordmark in the toolbar after the sidebar switch — `MV:paint` — v4: KEEP current (mockup has no wordmark; keep or drop on purpose)
- `[widget]` Divider between TUNE LIVE MIX and the broadcast keys (1x20) — `MV:paint dividerX` — v4: toolbar
- `[pref]` Look-and-feel set as the default for the whole app and destroyed with the view — `MV:ctor/dtor lookAndFeel` — v4: AppTheme
- `[widget]` TooltipWindow with a 700 ms delay — `MVh:tooltips` — v4: KEEP

### Toolbar controls

- `[control]` Sidebar switch (two-pane glyph). Toggles the sidebar; tooltip "(Ctrl-Cmd-S)" — `MV:SidebarButton, sidebarButton->onClick -> setSidebarShown` — v4: toolbar round sidebar button (Mail-style)
- `[control]` Transport well (RTZ, Stop, Play/Pause, Record, then clock "hh:mm:ss.t / length") — `TransportBar` — v4: transport pill (adds Loop)
- `[control]` SOLO pill: lists soloed names, or "N soloed" when narrow. **Clicking the name jumps to the item on Mixer; the cross clears every solo.** Tooltip explains — `MV:SoloPill, refreshSoloPill, jumpToSoloed` — v4: SOLO pill ("click clears"). KEEP the jump-to-item on the name and the clear on the cross
- `[behaviour]` jumpToSoloed: a strip selects the strip on Mixer, a bus selects the bus, a return gives a toast "returns live on Inspector and LIVE" — `MV:jumpToSoloed` — v4: SOLO pill
- `[control]` TUNE LIVE MIX (verb, TuneNav icon) runs cmd 405. While running it shows "· STOP" and is lit — `MV:tuneLiveButton` — v4: toolbar TUNE LIVE MIX
- `[control]` DIM key (hot tint): `controller.setBroadcastDim` toggles -20 dB on the broadcast and the room, not the engineer's listen; never locked by LIVE SAFE — `MV:dimButton` — v4: toolbar DIM
- `[control]` MUTE key (crit tint): `controller.setBroadcastMute` — `MV:muteButton` — v4: toolbar MUTE
- `[control]` BYPASS key (hot tint): `setBypass(!isBypassed)` — `MV:bypassButton` — v4: toolbar BYPASS
- `[control]` AUTOPILOT key (monitor tint) runs cmd 415. It used to send 626, which did nothing (bug fixed) — `MV:autopilotButton` — v4: toolbar Auto(pilot)
- `[control]` LIVE SAFE primary (amber when on, lock icon, "ON" suffix) runs cmd 614 — `MV:liveSafeButton` — v4: LIVE SAFE switch
- `[control]` Output picker (DinePopup, flat) shows `outputDisplayName` or "No output". Click runs chooseOutput — `MV:outputButton, chooseOutput` — v4: output button "Main · solo <device>"
- `[control]` MIX BUDDY glyph toggles the chat panel (lit while open) — `MV:chatButton` — v4: Mix Buddy toggle
- `[behaviour]` Toolbar visibility: output (running or in a workspace); DIM/MUTE/BYPASS/AUTOPILOT/LIVE SAFE (in a workspace and mixable); TUNE LIVE MIX and Mix Buddy (mixable); transport (in a workspace); chain foot (in a workspace and mixable) — `MV:updateChrome show()` — v4: toolbar (decide per v4; do not show live keys on setup pages without deciding)
- `[behaviour]` Toolbar overflow: nothing crosses kToolbarLeft (224). The four broadcast keys are placed all or none. The transport degrades ideal → minimum → keys-only → hidden. The solo pill takes room before the clock's length cell. A control with no room gets empty bounds — `MV:resized place/key lambdas` — v4: toolbar (keep these priorities)
- `[behaviour]` ToolbarToggle kinds Verb/Key/Primary/Glyph; setSuffix relayouts the parent — `MV:ToolbarToggle` — v4: toolbar key styles (pill / key / switch)
- `[control]` Session popover (readiness rows Audio device / Inputs / Purpose / Recording destination; New, Open, Save, Save as, Import multitrack folder, Add a reference mix, Save input mapping, Export stereo WAV, Export multitrack, Open setup, Rename or fix the inputs, Getting started). **Orphaned: nothing calls `sessionMenu()`/`setupPopover()` today** — `MV:setupPopover, sessionMenu` — v4: toolbar session menu (HANDOFF lists nearly the same items; revive this)

### Transport (TransportBar)

- `[control]` Return to start, tooltip "(Return)" — `TransportBar:startButton -> returnToStart (daw.locate(0))` — v4: transport RTZ
- `[control]` Stop (only acts while playing) — `TransportBar:stopButton` — v4: transport Stop
- `[control]` Play/Pause (paused glyph while playing) — `TransportBar:playButton -> togglePlay` — v4: transport Play
- `[control]` Record — `TransportBar:recordButton -> toggleRecord` — v4: transport Rec
- `[cmd]` toggleLoop: with no loop set it loops the whole project; reached only from L and Transport > Loop. **The `loopButton` member is declared but never created** — `TransportBar::toggleLoop` — v4: transport Loop button (new in pill) + L
- `[behaviour]` togglePlay refuses with "Open an audio device first." when no device is open. Stopping during a recording toasts "Recording stopped." and rebuilds the timeline — `TransportBar::togglePlay` — v4: transport
- `[behaviour]` toggleRecord refuses when armed tracks cannot reach an open input (two sentences). An unsaved session is auto-saved under a unique name before recording. Start and stop toasts give the track counts — `TransportBar::toggleRecord` — v4: transport Rec
- `[behaviour]` A take stopped by DINE (disk or device) is detected and kept true until the next take; the status foot reads it — `TransportBar::takeStoppedByItself, refresh` — v4: status foot
- `[behaviour]` Recorder error stops the take and toasts it; disk-fell-behind toasted once per take; `daw.takeStopNotice()` toasted — `TransportBar::refresh` — v4: toast
- `[behaviour]` Clock is red while recording; length "/ hh:mm:ss.t"; repaints the clock only when values change — `TransportBar::paint/refresh` — v4: transport clock "of total"
- `[behaviour]` Width tiers idealWidth/minimumWidth/keysOnlyWidth; drops length then clock — `TransportBar::resized` — v4: transport pill
- `[callback]` onToast → MainView::showToast; onTimelineChanged → MainView::timelineChanged (Tracks rebuild + touchSession + chrome) — `MV:ctor transportBar->...` — v4: Shell

### Sidebar

- `[control]` Setup › Routing (DeviceNav icon), meta "N in", done tick when mixable and not on Routing; tooltip about LIVE SAFE — `MV:Sidebar rowDefs[0]` — v4: sidebar Set up › Routing
- `[control]` Setup › Audio device (child) — `rowDefs[1]` — v4: sidebar Routing → Audio device
- `[control]` Setup › Inputs (child); the click refreshes AssignPage first — `rowDefs[2]`, `MV:ctor go lambda` — v4: sidebar Routing → Inputs
- `[control]` Setup › Outputs (child) — `rowDefs[3]` — v4: sidebar Routing → Outputs
- `[control]` Setup › Purpose and sound — `rowDefs[4]` — v4: sidebar Set up › Purpose and sound
- `[control]` Workspace › Tracks ⌘1 — `rowDefs[5]` — v4: sidebar Record › Tracks **⌘5**
- `[control]` Workspace › Mixer ⌘2 — `rowDefs[6]` — v4: sidebar Mix › Mixer **⌘1**
- `[control]` Workspace › Tune ⌘3 — `rowDefs[7]` — v4: sidebar Mix › Tune **⌘2**
- `[control]` Workspace › Live ⌘4 — `rowDefs[8]` — v4: sidebar Perform › Live ⌘4
- `[control]` Workspace › Inspector ⌘5 — `rowDefs[9]` — v4: sidebar Mix › Inspector **⌘3**
- `[control]` Safety › Mix history opens the HistorySheet — `rowDefs[10] Action::MixHistory` — v4: sidebar Mix › Mix history
- `[control]` Safety › Scenes goes to Live and calls `livePage->focusScenes()` — `rowDefs[11] Action::Scenes` — v4: sidebar Perform › Setlist
- `[control]` Safety › Broadcast readiness opens the readiness sheet in History mode, meta "checked/applicable". Shown only for broadcast purposes (`broadcastReadinessApplies`) — `rowDefs[12] Action::BroadcastReadiness, updateChrome` — v4: readiness pill / Ready sheet (keep the purpose gating, or decide it)
- `[control]` Library › Sessions, meta = number of sessions; tooltip — `rowDefs[13]` — v4: sidebar Set up › Sessions
- `[control]` Library › Favourite mixes, meta = number of favourites; tooltip — `rowDefs[14]` — v4: sidebar Mix › Favourite mixes (count)
- `[control]` Right-click on Mixer / Live / Inspector rows: "Open in a New Window" / "Show here" — `MV:Sidebar ctor onSecondaryClick` — v4: KEEP current (sidebar context menu)
- `[behaviour]` Rows are disabled until the session is mixable (setup rows always enabled; Inspector only when mixable) — `MV:updateChrome` — v4: sidebar
- `[behaviour]` Selection highlight via indexOf. Routing and Maps light Routing; actions never light — `MV:Sidebar::setSelected/indexOf` — v4: sidebar
- `[widget]` Captions Setup / Workspace / Safety / Library, sentence case; a hairline spine binds the Routing children — `MV:Sidebar::resized/paint` — v4: sections Set up / Mix / Perform / Record
- `[widget]` Footer: device name (input, else output, else "No audio device"), "48 kHz · N dropped buffers" (warn tint when >0), red when recording; repaints only on change — `MV:Sidebar::refresh/paint` — v4: footer status dot, name/inputs, "48 kHz · 128 smp · N dropped buffers" (adds buffer size)
- `[pref]` Folded sidebar is a 52 pt icon rail. Rail items have tooltips with the shortcut; children are hidden; the device lamp shows on the rail. **Fold state is not persisted** — `MV:Sidebar::setCollapsed, setSidebarShown` — v4: hides completely (no rail), animated 420 ms; auto-collapse on Live (new)
- `[control]` Check inputs has no sidebar row today (View menu, Mix Buddy, Tune, readiness sheet only) — `MV:showCheck` — v4: sidebar Set up › Check inputs with issue badge
- `[control]` Export has no sidebar row today (File menu, session popover) — `MV:exportMix` — v4: sidebar Record › Export
- `[control]` Input Mappings (Maps) has no sidebar row; it lights Routing — `Page::Maps` — v4: Routing › Input Mappings

### Menu bar (MainView::Menu) — File

- `[menu]` New Session (100) → newSession — `MV:Menu case 0` — v4: session menu New ⌘N
- `[menu]` Open Session... (101) → openSession, **a popup of saved sessions anchored to the sidebar button, not a file chooser**; toast if none are saved — `MV:openSession` — v4: session menu Open ⌘O
- `[menu]` Save (102) → saveNow (falls back to Save As when unnamed) — v4: session menu Save ⌘S
- `[menu]` Save As... (103) → saveAs AlertWindow "Save session" (name; default "Sunday") — `MV:saveAs` — v4: session menu (⇧⌘S kept)
- `[menu]` Import Audio Files... (104) → importMultitrack chooser (files, folders, multiple) — `MV:importMultitrack` — v4: session menu Import Multitrack Folder
- `[menu]` Add a Reference Mix... (107) goes to Tune and calls `mixPage->openReference()` — `MV:handleCommand 107` — v4: session menu Add Reference Mix; Tune reference
- `[menu]` Save Input Mapping… (108, enabled when there are inputs) → saveInputMapping — v4: session menu Save Input Mapping
- `[menu]` Input Mappings… (109) → Maps page — v4: Routing › Input Mappings
- `[menu]` Export Stereo Mix (WAV)... (105) — `MV:exportMix(Wav)` — v4: Export ⇧⌘E / Export sheet
- `[menu]` Export Stereo Mix (MP3)... (106) — `MV:exportMix(Mp3)` — v4: Export sheet
- `[menu]` Export Multitrack (one file per input)... (110) — `MV:exportMix(Wav, RawMultitrack)` — v4: Export sheet

### Menu bar — Edit

- `[menu]` Undo <label> (200), dynamic label and enablement from undoTarget — `MV:Menu case 1, undoHere` — v4: ⌘Z
- `[menu]` Redo <label> (204) — `MV:redoHere` — v4: ⇧⌘Z (KEEP; not listed in v4)
- `[menu]` Split at Playhead (201), blocked by LIVE SAFE → `tracksPage->splitAtPlayhead` — v4: ⌘E
- `[menu]` Delete Clip (202), blocked by LIVE SAFE → `tracksPage->deleteSelection` — v4: Tracks + Delete/Backspace
- `[menu]` Add Marker at Playhead M (203), blocked by LIVE SAFE; goes to Tracks — v4: M

### Menu bar — Track

- `[menu]` New Track ▸ (ids 3000+, from `TracksPage::fillNewTrackMenu`), disabled under LIVE SAFE; goes to Tracks and calls addTrack(role) — `MV:Menu case 2, handleCommand 3000..3199` — v4: KEEP current (Track menu) + Tracks page
- `[menu]` Set Every Track to Record (300) arms all tracks; toast — `MV:handleCommand 300` — v4: KEEP current (Track menu)
- `[menu]` Set No Tracks to Record (301) — v4: KEEP current (Track menu)
- `[menu]` Move Track Up (305) / Move Track Down (306), enabled by `canMoveSelectedTrack`; toast if nothing is picked — v4: Tracks move up/down + Track menu
- `[menu]` Monitoring: Input / Auto / Off (302-304) sets **every** track; toast — v4: KEEP current (Track menu); per-channel in the Mixer right-click is separate

### Menu bar — Mix

- `[menu]` TUNE LIVE MIX (405): LIVE SAFE gate, then Tune, then `mixPage->pressLiveTune()` — v4: toolbar + Tune
- `[menu]` TUNE MIX… (400): LIVE SAFE gate, then Tune, then `mixPage->pressTune()` (opens the scope sheet) — v4: Tune flow
- `[menu]` TUNE CHANNEL T (404), enabled when a channel is selected; falls back to lastChannel — `MV:handleCommand 404, tuneChannel` — v4: T, Mixer TUNE CHANNEL
- `[menu]` MATCH TO REFERENCE (407), enabled when there is a reference: LIVE SAFE gate, startReferenceMatch, toast — v4: Tune reference / tune flow
- `[menu]` Reference: <name>… / Add a Reference Mix… (408) → Tune openReference — v4: Tune reference sheet
- `[menu]` Speech Priority (413), ticked; `setSpeechPriority` toggle — v4: KEEP current (Mix menu); possibly Live
- `[menu]` Share the Mics (416), ticked; `setAutoMix` toggle — v4: KEEP current (Mix menu); Live speaking-mic tiles
- `[menu]` Mix Buddy… (409) → openChat — v4: Mix Buddy toggle
- `[menu]` Try Another Mix (410), enabled by `canTryAnotherMix`; LIVE SAFE gate; goes to Tune — v4: KEEP current (Mix menu)
- `[menu]` Undo <mix label> (411) / Redo <mix label> (412); mix domain only; toasts when there is nothing to undo or redo — v4: KEEP current (Mix menu) + Mix history
- `[menu]` Raise Loudness to Target (+x dB) (420), disabled under LIVE SAFE or when not possible; toast with the result — `controller.raiseLoudnessToTarget` — v4: KEEP current (Mix menu); Tune loudness
- `[menu]` Loudness Target ▸ (430-436) DeliveryLoudness list with LUFS, ticked → `setDelivery` — v4: Tune loudness target menu
- `[menu]` Master Sound ▸ (450-457) MasterVoicing list, ticked → `setVoicing`; toast — v4: Tune voicing menu
- `[menu]` Autopilot: hold this mix (415), ticked → `setAutopilot` toggle — v4: toolbar Auto + Live autopilot
- `[menu]` Reset Mix to Raw… (414), disabled under LIVE SAFE → resetMixToRaw — v4: session menu "Reset Mix to Raw" (disabled under LIVE SAFE)
- `[menu]` Centre Macro Pads (401) → `mixPage->centreMacroPads`; toast — v4: Tune macro pads (KEEP menu item)
- `[menu]` Clear Solo (N) (402), enabled when anything is soloed — v4: SOLO pill + menu
- `[menu]` Bypass: Hear the Inputs B (403), ticked — v4: BYPASS + B
- `[menu]` Mix Engineer: Use the Cloud Model (406), ticked; enabled only when `OpenAiMixProvider().isAvailable()`. Toggles the reasoning provider and toasts (no API key → refuses). **Session-only state `usingCloudMixEngineer`, not persisted** — v4: KEEP current (Mix menu). AI must stay opt-in

### Menu bar — Transport

- `[menu]` Play / Stop (500): a two-press guard while recording (3 s) — `MV:handleCommand 500` — v4: Space
- `[menu]` Record (501): the same two-press guard to stop a take — v4: R
- `[menu]` Return to Start (502) — v4: Enter/Return
- `[menu]` Loop (503) — v4: L + transport Loop

### Menu bar — View

- `[menu]` Tracks / Mixer / Tune / Live / Inspector (600-604) — v4: sidebar + ⌘1-5 (renumbered)
- `[menu]` Set-up and Routing (613) goes to the Routing section currently showing, or Routing — v4: sidebar Routing
- `[menu]` Saved Input Patches (616) → Maps — v4: Routing › Input Mappings
- `[menu]` Open Mixer in a New Window (608) → openMixerWindow — v4: Mixer "New Window"
- `[menu]` Open Live in a New Window (617) → openLiveWindow — v4: KEEP current (View menu + sidebar right-click)
- `[menu]` Open Inspector in a New Window (618) → openInspectorWindow — v4: KEEP current (View menu + sidebar right-click)
- `[menu]` Outputs… (609) → Routing › Outputs — v4: Routing › Outputs
- `[menu]` Check Inputs… (630) → showCheck — v4: sidebar Check inputs + sheet
- `[menu]` Dim the Broadcast (20 dB) (631), ticked — v4: DIM
- `[menu]` Mute the Broadcast (632), ticked — v4: MUTE
- `[menu]` Hide/Show Sidebar ⌃⌘S (610) — v4: ⌃⌘S
- `[menu]` Show/Hide the two side panels (615): on Inspector toggles both rail and trail; on Tune the rail; elsewhere the sidebar — v4: KEEP current (View menu); `[`/`]`
- `[menu]` Appearance ▸ theme list (640+, built-ins then user themes, ticked) → applyThemeNamed — `MV:Menu case 5, themeMenuNames` — v4: session menu "Appearance" + View menu
- `[menu]` Appearance ▸ Text size ▸ Standard/Large/Larger (660-662) → applyTextSize — v4: Appearance sheet / View menu (KEEP)
- `[menu]` Appearance ▸ Customise Appearance… (620) → showThemes — v4: Appearance sheet
- `[menu]` Appearance ▸ Import a Theme… (621) → showThemes + importTheme — v4: Appearance sheet
- `[menu]` Appearance ▸ Show Themes Folder (622) reveals ThemeStore::folder — v4: KEEP current (View menu)
- `[menu]` Zoom In (605, ×1.25) / Zoom Out (606, ×0.8) / Zoom to Fit (607), all on Tracks — v4: ⌘= / ⌘− / ⌘0

### Menu bar — Help

- `[menu]` Getting started (701) → showTutorial — v4: session menu "Getting Started" + Help
- `[menu]` Show the guides again (702) → Guides::reset + maybeShowGuide; toast — v4: KEEP current (Help menu)
- `[menu]` Share anonymous usage data (703), ticked; only when telemetry is configured; toggles sharing; toast — v4: KEEP current (Help menu)
- `[menu]` About DINE (700): a toast with the tagline — v4: KEEP current (Help menu)

### Keyboard (MainView::commandForKey / keyPressed)

- `[key]` ⌃⌘S → 610 sidebar — v4: ⌃⌘S
- `[key]` ⌘S → 102 Save — v4: ⌘S
- `[key]` ⇧⌘S → 103 Save As — v4: KEEP (not listed)
- `[key]` ⌘Z → 200 Undo; ⇧⌘Z → 204 Redo — v4: ⌘Z (+ keep ⇧⌘Z)
- `[key]` ⌘E → 201 Split at playhead. **Shift is ignored, so ⇧⌘E also splits today** — v4: ⌘E split; ⇧⌘E Export (**conflict: commandForKey must check shift**)
- `[key]` ⌘O → 101 Open; ⌘N → 100 New — v4: same
- `[key]` ⌘= / ⌘+ → 605; ⌘- → 606; ⌘0 → 607 — v4: same
- `[key]` ⌘1..5 → 600-604 = Tracks, Mixer, Tune, Live, Inspector — v4: **⌘1 Mixer, ⌘2 Tune, ⌘3 Inspector, ⌘4 Live, ⌘5 Tracks (conflict; ReachabilityTests asserts the current table)**
- `[key]` Space → 500 Play/Stop (two-press while recording) — v4: Space (HANDOFF: "Space reserved by Live when on that page" — decide whether Live keeps play/stop)
- `[key]` Return → 502 RTZ — v4: Enter RTZ
- `[key]` R → 501 Record (two-press stop) — v4: R
- `[key]` L → 503 Loop — v4: L
- `[key]` B → 403 Bypass — v4: B
- `[key]` T → 404 Tune channel — v4: T
- `[key]` M → 203 Marker — v4: M
- `[key]` [ → 611 fold the left panel (Tune rail / Inspector channels / else sidebar); ] → 612 fold the right panel (Inspector trail / Tune side) — `MV:togglePanel` — v4: [ / ] (v4 Tune left rail toggles with `[`)
- `[key]` Delete / Backspace → 202 Delete clip, only on Tracks — v4: KEEP (Tracks)
- `[key]` Escape closes **every** open sheet at once (scope sheet, check, history, readiness, theme, channel, chat, export, choice); not handled when none is open — `MV:keyPressed, closeSheets` — v4: Esc closes the topmost only (menu → sheet → tune → chat): a behaviour change
- `[key]` A focused text field takes keys before MainView (JUCE focus chain) — v4: "ignore keys while a text field has focus" (already true)
- `[behaviour]` commandForKey is side-effect free and asserted by `app/Tests/ReachabilityTests.cpp` — `MVh:commandForKey` — v4: keep it as a data table; update the tests with the renumbering
- `[key]` Tutorial: Esc skips and marks seen, Right/Return next, Left back — `Tutorial::keyPressed` — v4: tour
- `[key]` ChoiceSheet: Esc closes, Return runs the default action — `ChoiceSheet::keyPressed` — v4: reset/recover/mic sheets
- `[key]` AlertWindows: Return = OK/Save, Esc = Cancel (Save as, Save mapping, Rename mapping, Apply anyway, quit alerts) — v4: KEEP

### Commands with no menu or key (reached by buttons or callbacks)

- `[cmd]` 614 LIVE SAFE toggle (`daw.setLiveSafe`), livePage rebuild, toast with `liveSafe::allowedSummary` — `MV:handleCommand 614` — v4: LIVE SAFE switch
- `[cmd]` 611/612 panel folds (keys only) — v4: [ ]
- `[cmd]` **Gap: no command id exists for "Export…" (v4 ⇧⌘E)**; the menu has three format-specific items instead — v4: add the id; keep the three
- `[cmd]` Unknown ids fall through; theme ids 640+N resolve by name — `MV:handleCommand` — v4: KEEP

### LIVE SAFE, BYPASS, solo, DIM, MUTE, Autopilot behaviour (UI-enforced)

- `[behaviour]` liveSafeBlocks(what) toasts "LIVE SAFE is on, so X is blocked..." with allowedSummary — `MV:liveSafeBlocks` — v4: toast on every blocked action
- `[behaviour]` Blocked under LIVE SAFE: TUNE MIX, TUNE LIVE MIX, TUNE CHANNEL, Try Another Mix, MATCH TO REFERENCE, Reset Mix to Raw, split/delete clip, add marker, new session, import (both paths), apply input mapping, change output device (toolbar), change drum kit; menu items New Track / Raise Loudness / Reset are disabled — `MV:handleCommand, newSession, importMultitrack, importAudio, applyInputMapping, chooseOutput, drumKitMenu` — v4: toolbar/menus/toasts (all must stay gated)
- `[behaviour]` Under LIVE SAFE Cmd+Z on Tracks skips the timeline (mix undo only); a toast explains — `MV:undoTarget/undoHere` — v4: ⌘Z
- `[behaviour]` Not locked by LIVE SAFE: DIM, MUTE, BYPASS, solo, Autopilot, monitor, recording — `MV:dimButton tooltip; liveSafe policy` — v4: same
- `[behaviour]` setBypass refuses with no mix ("Assign your inputs first"). Toasts on and off. Repaints mixer, mixer window, Live/Inspector windows, Live and Tune; chain foot note "BYPASS"; status foot Engine "Bypassed" (warn) — `MV:setBypass, updateChainFoot, StatusBar::update` — v4: BYPASS + white banner under the toolbar + fader-lock toast (new)
- `[behaviour]` TUNE CHANNEL refused while BYPASS is on, while already listening or planning, or with no strips — `MV:tuneChannel` — v4: tune flow
- `[behaviour]` A Mix Buddy SoloStrip action refuses when solo is In Place, because it would change the room — `MV:performBuddyAction` — v4: Mix Buddy
- `[behaviour]` The solo pill follows the controller every 3 ticks as well as on chrome events (an S pressed anywhere) — `MV:timerCallback refreshSoloPill` — v4: SOLO pill
- `[behaviour]` Status-foot first cell priority: muted > recording stopped by itself > dimmed > "running, Autopilot on" — `MV:StatusBar::update` — v4: status foot
- `[behaviour]` Autopilot runs inside `controller.poll()` on MainView's 30 Hz message-thread tick — `MV:timerCallback` — v4: **must keep a message-thread poll even with a VBlank clock**

### Sheets and dialogs hosted by MainView

- `[sheet]` CheckSheet (check inputs): onClose is deferred; refreshed on the tick — `MV:showCheck` — v4: Check inputs sheet
- `[sheet]` HistorySheet (mix history): onToast, onClose — `MV:showHistory` — v4: Mix history sheet
- `[sheet]` BroadcastReadinessSheet (Checklist / History modes); refused for non-broadcast purposes with a toast; onOpenCheck / onOpenHistory / onOpenOutputs swap sheets; refreshed on the tick — `MV:showBroadcastReadiness` — v4: Ready sheet (readiness pill)
- `[sheet]` ThemeSheet (Appearance): onToast, onThemeChanged (menuItemsChanged), onClose; `gUseStoredTheme` passed in — `MV:showThemes` — v4: Appearance sheet
- `[sheet]` ChannelTuneSheet (TUNE CHANNEL): started by controller.startTuneChannel; onOpenInspector; refreshed on the tick — `MV:tuneChannel` — v4: Tune flow sheet (CHANNEL)
- `[sheet]` ChatSheet (Mix Buddy) as a right-hand panel: refuses before inputs are assigned; takeFocus; onAction → performBuddyAction; refreshed on the tick — `MV:openChat` — v4: Mix Buddy chat
- `[sheet]` ExportSheet: guards (no mix, nothing recorded, already exporting); choose(what, format) preselects — `MV:exportMix` — v4: Export sheet
- `[sheet]` ChoiceSheet "Reset the mix to raw?" with Resets / Keeps columns, a checkpoint note, Cancel / Reset to raw (destructive); rebuilds Mixer, windows, Inspector, Live — `MV:resetMixToRaw` — v4: Reset to raw sheet
- `[sheet]` ChoiceSheet "Recover session?" (Keep both / Open last saved / Recover <time>) — `MV:offerRecovery` — v4: Recover sheet
- `[sheet]` ChoiceSheet "macOS is about to ask about the microphone" (Not now / Continue). Esc or any other close counts as Not now (deferred one message) — `MV:explainMicrophone` — v4: Mic sheet
- `[sheet]` Tune scope sheet is owned by MixPage but counted as a sheet for Esc and openSheetName ("tunescope") — `MV:openSheetName, closeSheets` — v4: Tune scope sheet
- `[sheet]` openSheetName names: tunescope, check, history, readiness, appearance, channel, chat, export, choice; used by the tests and snapshots — `MV:openSheetName` — v4: keep the names
- `[sheet]` AlertWindow "Save session" (name) — `MV:saveAs` — v4: KEEP (or the session menu)
- `[sheet]` AlertWindow "Save input mapping" (name defaults to the session name or "Main hall", plus an optional note) → InputMapStore::save, telemetry preset_saved, toast — `MV:saveInputMapping` — v4: Routing › Input Mappings "Save this session's patch"
- `[sheet]` AlertWindow "Apply <map>?" lists problems, Apply anyway / Cancel — `MV:applyInputMapping` — v4: Routing › Input Mappings Apply
- `[sheet]` Input Mappings popup (Apply / Rename… / Duplicate / Export… / Delete per map; Import a patch… / Save this session's patch…). **Orphaned: nothing calls `openInputMappings()`; File > Input Mappings goes to the Maps page instead** — `MV:openInputMappings` — v4: Routing › Input Mappings (⋯ Rename, Delete; Duplicate/Export: KEEP in ⋯)
- `[sheet]` AlertWindow "Rename mapping" — `MV:openInputMappings case 1` — v4: Input Mappings ⋯ Rename
- `[behaviour]` closeSheets resets every sheet including chat and the scope sheet, then updateChrome and resized — `MV:closeSheets` — v4: Esc topmost
- `[behaviour]` A sheet's onClose resets it via callAsync with a SafePointer so a button is not destroyed inside its own callback — `MV:showCheck etc.` — v4: keep the pattern
- `[widget]` ChoiceSheet: title, sentence, two columns (dash or tick, tint), boxed note + quiet note, actions (destructive = red ghost, default = filled + Return). onClose runs before the action — `ChoiceSheet.h/.cpp` — v4: reset / recover / scope / mic sheets

### Separate windows

- `[control]` Mixer window ("Mixer — DINE", min 720x420, opens 1180x700, own 30 Hz timer). onOpenStrip/onOpenBus → inspect; onTuneStrip brings main to front then tunes; onToast; refused before inputs are assigned; toast on open — `MV:MixerWindow, openMixerWindow, closeMixerWindow` — v4: Mixer "New Window"
- `[control]` Live window (PageWindow, min 900x560, opens 1280x760): onOpenHistory, onLiveSafeChanged, onToggleRecord(501), onToast; toast — `MV:openLiveWindow` — v4: KEEP current
- `[control]` Inspector window (PageWindow, min 900x620, opens 1240x820): onRetune(400), onTuneChannel, onImportSample, drumKitName/onDrumKit, onBack closes; refused before inputs are assigned; toast — `MV:openInspectorWindow` — v4: KEEP current
- `[behaviour]` inspectStrip/inspectBus go to the Inspector window when it is open, else the Inspector page — `MV:inspectStrip/inspectBus` — v4: Inspector
- `[behaviour]` rebuildWindows on every session or graph change; windows closed via callAsync — `MV:rebuildWindows, closePageWindow` — v4: KEEP
- `[behaviour]` Text size relayouts every open window — `MV:applyTextSize` — v4: Appearance

### Callbacks wired from MainView to pages (constructor)

- `[callback]` chainFoot->onOpen: Inspector + select(selected or last channel); on Mixer with no strip, the Inspector alone — `MV:ctor` — v4: chain strip click → Inspector
- `[callback]` sidebar go(page) / act(action) / openWindow(page) — `MV:ctor` — v4: sidebar
- `[callback]` statusBar->onExportClicked → exportCellClicked — v4: status foot Export cell
- `[callback]` soloPill->onJump / onClear (clearSolos + chrome) — v4: SOLO pill
- `[callback]` favouritesPage->onToast — v4: Favourite mixes
- `[callback]` sessionsPage->onNew / onOpenFile (popup list) / onImportFolder (import with newSessionFirst) / onOpen(file) (load, rebuild, page, toast with recovery note) — v4: Sessions
- `[callback]` devicePage->onBack (Sessions) / onToast / onContinue (enterSession if inputs, else Assign) / onContinueToAssign / onSetUpOutputs / onImportRecording (importMultitrackFolder) — v4: Routing › Audio device (HANDOFF: keep every DevicePage callback)
- `[callback]` assignPage->onBack (Device) / onContinue (Purpose) / onSaveMapping / onApplyMapping (Maps) — v4: Routing › Inputs
- `[callback]` routingPage->onSection / onCoverChanged (re-show) / onToast / onSaveMap / onApplyMap(file) / onImportMap (chooser *.dinemap.json, load+save, toast, Maps) / onChooseOutputDevice (changeOutput or openOutputOnly, toast) — v4: Routing
- `[callback]` purposePage->onBack (Assign) / onContinue (enterSession: reconfigure, touch, rebuild, Tracks) — v4: Purpose and sound
- `[callback]` tracksPage->onToast / onPanelWidthChanged (persist + touch) / onTimelineChanged / onOpenStrip / onOpenAssign / onTuneStrip / onSessionChanged (rebuild Inspector, Mixer, windows) — v4: Tracks
- `[callback]` mixPage->onOpenAdvanced / onToast / onTuneStrip / onOpenChat / onOpenHistory / onOpenCheck / onOpenFavourites / onSelectStrip (lastChannel + chain foot) / onGraphChanged (reconfigure) — v4: Tune
- `[callback]` mixerPage->onOpenStrip / onOpenBus / onTuneStrip / onOpenWindow / onToast / onOpenAssign — v4: Mixer
- `[callback]` livePage->onToast / onOpenHistory / onLiveSafeChanged / onToggleRecord(501) — v4: Live
- `[callback]` advancedPage->onBack (Tune) / onRetune (400) / onImportSample (chooser wav/aif/aiff/flac → services.importSample, toast, rebuild) / onTuneChannel / drumKitName / onDrumKit (drumKitMenu) — v4: Inspector (+ Sample stage)
- `[callback]` controller.onMessage → showToast (cleared in dtor) — v4: toast
- `[callback]` tracksPage/mixerPage setFootShown(false): the window owns the chain foot — v4: chain strip
- `[control]` Drum kit menu: built-in kits with a sentence each, Custom (disabled) ticked when current; LIVE SAFE gate; applyDrumKit toast. The kit is read from the strips and never stored — `MV:drumKitName/drumKitMenu` — v4: Inspector › Sample stage kit picker
- `[behaviour]` performBuddyAction: ShowPage (11 pages), OpenInspector, SoloStrip (listen only), OpenCheckInputs, OpenHistory, RunTuneMix (go to Tune only), RunTuneChannel, AskForChange (→ proposal) — `MV:performBuddyAction` — v4: Mix Buddy (help, never applies)

### Undo

- `[behaviour]` Two undo domains, Mix (MixController) and Timeline (TracksPage). On Tracks, Cmd+Z takes whichever was edited last (lastEditMs vs undoMixAtMs); elsewhere it is the mix. Redo follows the last undone domain — `MV:undoTarget/redoTarget/undoHere/redoHere, UndoDomain` — v4: ⌘Z
- `[behaviour]` Devices, permissions, recording, autosave, DIM and MUTE are never undoable — `MV comment` — v4: same

### Import, export and session flows

- `[behaviour]` newSession: refused under LIVE SAFE or while recording; **refuses if the current session cannot be saved**; rebuilds; goes to Device; toast "New session." — `MV:newSession` — v4: ⌘N
- `[behaviour]` importAudio: LIVE SAFE and recording gates. newSessionFirst saves and starts fresh only if the current session has audio. Rebuilds everything; lands on Assign if in setup, else Tracks; toast summary — `MV:importAudio` — v4: Import
- `[behaviour]` sessionReplaced: resync revision/milestone, rebuild all, page Assign or Tracks — `MV:sessionReplaced` — v4: KEEP
- `[behaviour]` Export worker: progress stored in the shared ExportProgress; toasts for start / stopped / failed (with "click Export failed") / done (file or folder). One export at a time — `MV:exportMix` — v4: Export sheet + status foot
- `[behaviour]` Export cell click: Running → menu "Stop the export"; Done → reveal in Finder; Failed → re-toast the error and clear — `MV:exportCellClicked` — v4: status foot
- `[behaviour]` Export status text via exportStatusText; tint crit/accent/ink; Done/Cancelled clear after 12 s; a failure stays — `MV:timerCallback` — v4: status foot
- `[behaviour]` Output picker menu: every output device except "DINE Monitoring*" (ticked current), then "Set up outputs…"; LIVE SAFE gate; `flagReadiness(BroadcastDevice)`; toast "Broadcast: X" — `MV:chooseOutput` — v4: toolbar output button (v4 adds the Solo choices from `OutputsSheet::showSoloDeviceMenu`)
- `[behaviour]` showOutputs = Routing › Outputs (not a sheet) — `MV:showOutputs` — v4: Routing › Outputs

### Status foot (MainView::StatusBar)

- `[widget]` Engine cell: running / Not running / Device lost / Bypassed / On air muted / On air dimmed / Recording stopped by itself / running, Autopilot on — `MV:StatusBar::update` — v4: status foot engine status
- `[widget]` Export cell (second, clickable, pointer cursor) plus a progress seam (bar, or a sweep when there is no fraction) across the top edge — `MV:StatusBar::paint` — v4: status foot
- `[widget]` CPU % (warn over 80%) — v4: status foot
- `[widget]` Disk: time left to record (a day+, h m, m), warn under 15 min; "not saving" (crit) when the autosave fails — v4: status foot
- `[widget]` Recording: "N tracks" / "stopped" — v4: status foot
- `[widget]` Broadcast: integrated LUFS, warn when off target — v4: status foot
- `[widget]` Monitor: "in place|solo · PFL|AFL", dim when there is no monitor output — v4: status foot
- `[widget]` Dropped (xruns) — v4: status foot (also the sidebar footer)
- `[widget]` Live safe on/off — v4: status foot
- `[widget]` Right end: "N inputs · M to record" — v4: status foot
- `[behaviour]` Cells drop from the right when narrow; repaints only on a Look change — `MV:StatusBar::cell` — v4: status foot

### Chain foot (ChainStrip)

- `[widget]` ChainStrip: name (hover brightens), chips Input/Filters/Gate/[Sample]/EQ/De-ess/Comp/Transient/Tone/Sat/[Width]/[Limit]/Out with values; inactive chips dimmed; empty message; right-hand note (warn caps). Click opens the Inspector — `ChainStrip.h/.cpp` — v4: chain strip
- `[behaviour]` chainStages / activeChainStages / hasSampleStage are the one chain-order rule shared by Mixer INSERTS and the Inspector — `ChainStrip.h free functions` — v4: Mixer insert slots, Inspector stage list (reuse)
- `[behaviour]` updateChainFoot reads the selected strip, else the selected bus on Mixer/Inspector, else lastChannel; "Set the device up first." when not prepared; note "BYPASS" — `MV:updateChainFoot` — v4: chain strip
- `[behaviour]` lastChannel tracks the last channel picked anywhere (Mixer, Tracks, Inspector, Tune rail) — `MV:selectedChannel, mixPage->onSelectStrip` — v4: KEEP

### Toast

- `[widget]` Toast: bottom-centre of the content (left of the chat panel), up to 640 wide and 44-120 high, drop shadow — `MV:Toast, resized` — v4: toast bottom-centre
- `[behaviour]` Refusal styling is decided by keywords (could not, cannot, is locked, blocked, nowhere, no earlier, first, stopped, "is not "); amber on refuse ground — `MV:Toast::show` — v4: toast (keep the refusal tint)
- `[behaviour]` Toast lasts 5 s (150 ticks at 30 Hz); a new toast replaces the old — `MV:showToast, timerCallback` — v4: ~4 s
- `[behaviour]` Toast sources: controller.onMessage, every page onToast, TransportBar, sheets, Main.cpp device/milestone/recovery, telemetry first-run notice (tick 90), autosave failing (once per failure), device stopped — `MV + Main` — v4: toast

### Timers and ticking

- `[behaviour]` MainView 30 Hz timer: controller.poll(); transport refresh; refresh of the visible page only (Tracks, Mixer, Assign tick, Tune, Live, Inspector); open sheets refresh (channel, check, readiness, chat); status foot; sidebar foot every 10 ticks; followMicrophone each second; chain foot and solo pill every 3 ticks; toast expiry; autosave revision/milestone (milestone immediately, else after 10 quiet ticks); tune-live chrome; device-stopped toast; on-air glow — `MV:timerCallback` — v4: single VBlank clock (keep a message-thread poll and autosave cadence)
- `[behaviour]` Mixer window and PageWindows each run their own 30 Hz timer — `MV:MixerWindow/PageWindow` — v4: shared clock
- `[behaviour]` Telemetry notice is shown once at tick 90 when `needsNotice` — `MV:timerCallback` — v4: toast
- `[behaviour]` followMicrophone: when macOS says yes, retryHeldInput + toast; on AskFirst, once per run, the mic sheet (not over another sheet) — `MV:followMicrophone, micExplained` — v4: Mic sheet
- `[behaviour]` Saving follows the controller revision, never a UI call site (touchSession) — `MV:timerCallback seenRevision/saveTicks` — v4: KEEP (invariant)

### Preferences and persisted UI state

- `[pref]` Theme name → ThemeStore::preferencesFile (`setChosenTheme`), applied before pages are built — `MV:ctor, applyThemeNamed` — v4: Appearance
- `[pref]` Text scale 1.0 / 1.2 / 1.35 → ThemeStore (`setChosenTextSize`) — `MV:applyTextSize` — v4: Appearance
- `[pref]` User themes folder (ThemeStore::folder), import, reveal — `MV:handleCommand 621/622` — v4: Appearance
- `[pref]` Workspace guides: per-workspace seen keys plus a global enable flag in ThemeStore prefs (`Guides::`) — `MV:maybeShowGuide` — v4: KEEP
- `[pref]` Tour seen marker `~/Music/DINE/.getting-started-seen` — `Tutorial::hasBeenSeen/markSeen` — v4: tour
- `[pref]` Telemetry sharing toggle and notice-shown flag — `Telemetry::setSharing/markNoticeShown` — v4: Help menu
- `[state]` TRACKS panel width, per session — see HostServices — v4: Tracks
- `[state]` Sidebar fold, window size/position, cloud-engineer toggle, open windows: **not persisted** — v4: decide (v4 sidebar collapse may want persistence)
- `[state]` Statics for the headless tool: setAutoTutorial, setStoredThemeUsed, setGuidesUsed(prefs file) — `MV:setAutoTutorial/...` — v4: KEEP (tools/tests depend on them)
- `[state]` Snapshot/test hooks: updateChromeForSnapshot, closeSheetsForSnapshot, exportMixForSnapshot, exportMultitrackForSnapshot, showExportProgressForSnapshot, isSoloBarShown, openSheetName, page getters (get*Page, getTransportBar) — `MVh` — v4: KEEP (AppSnapshots / ReachabilityTests)

### Workspace guide and tour

- `[widget]` WorkspaceGuide card, bottom-left of the workspace (340 wide): "Got it" (this one, for good) and "Not these" (all off, toast). Hidden under sheets and the tour; never takes keys or blocks clicks — `WorkspaceGuide, MV:maybeShowGuide/resized` — v4: KEEP current (no v4 home)
- `[behaviour]` Guide texts exist only for Tracks, Mixer, Tune, Live and Inspector (by the Page int) — `WorkspaceGuide.cpp kEntries/indexForPage` — v4: KEEP (update the ints if Page changes)
- `[widget]` Tutorial: 7 steps (Sessions/session, Device/tabs, Tracks/transport, Tune/tabs, Mixer/rail, Inspector/tabs, Live/livesafe), a scrim with a spotlight hole, a card with dots, Back / Next / "Start mixing" / "Skip the tour" — `Tutorial.cpp steps()` — v4: Getting Started tour (stepped)
- `[callback]` Tutorial onStep → showPage(jlimit 0..8); spotFor → MainView::spotlight("session" sidebar button, "transport", "livesafe", "rail" sidebar, "tabs" sidebar Tracks..Inspector rows); onFinished → reset + focus — `MV:showTutorial/spotlight` — v4: tour (the spot names must map onto the v4 chrome; "session" should point at the v4 session menu)

### Reusable widgets (AppTheme.h)

- `[widget]` DineButton (Filled / Standard / Ghost / Segment / Toggle; tint, icon, caps, quiet, padX, idealWidth) — `AppTheme.h:DineButton` — v4: pill buttons / primary white pill
- `[widget]` DinePopup (value + brief value, dot, flat, chevron, idealWidth) — `AppTheme.h:DinePopup` — v4: menus / output picker
- `[widget]` DineNavItem (label, icon, meta, done tick, selected, onSecondaryClick) — `AppTheme.h:DineNavItem` — v4: sidebar rows
- `[widget]` DinePanelTab (left/right fold tab with tooltip) — `AppTheme.h:DinePanelTab` — v4: [ / ] panel folds
- `[widget]` DineKey (M/S/R/A console key with its own tint) — `AppTheme.h:DineKey` — v4: strip keys R/A/M/S (v4 colours)
- `[widget]` DineChip (filter chip with dot; static drawTrack) — `AppTheme.h:DineChip` — v4: group chips / segmented
- `[widget]` DineSegmentRow (a plane for segment rows) — `AppTheme.h:DineSegmentRow` — v4: segmented control
- `[widget]` DineSwitch (28x16 on/off with words) — `AppTheme.h:DineSwitch` — v4: switches (LIVE SAFE switch)
- `[widget]` DineKnob (270° rotary, range with mid skew, default on double-click, caption, format, tint, dial size) — `AppTheme.h:DineKnob` — v4: knobs (Outputs level, Inspector Simple)
- `[widget]` PanBar (Bar / Knob styles, onChange, double-click centre) — `AppTheme.h:PanBar` — v4: pan bar
- `[widget]` DineMeter (Segments / Bar; peak, hold, clip, muted) — `AppTheme.h:DineMeter` — v4: meters (new ballistics in HANDOFF)
- `[widget]` DineLookAndFeel: fonts (Inter / Plex Mono), popup menu, section header, alert box, tooltip, scrollbar, text editor, linear slider/fader cap, setBipolar — `AppTheme.h:DineLookAndFeel` — v4: context menus (dark translucent, check column, shortcut column, blue hover)
- `[widget]` ToolbarToggle, SidebarButton, SoloPill, StatusBar, Sidebar, Toast, MixerWindow, PageWindow (private to MainView) — `MV` — v4: Shell
- `[widget]` TransportButton (Start / Play / Stop / Record / Loop glyphs) — `TransportBar.cpp` — v4: transport pill

### Tokens and drawing helpers (AppTheme.h, by group)

- `[token]` Materials: desk, window, toolbar, title, menubar, sidebar, rail, pageBar, console, tile, card, raised, item, selected, control/controlHot/controlOn, sheet, popover, inset, deep, refuse, recGround, soloGround, editGround, plus the legacy gradient aliases — `AppTheme.h:Dine::` — v4: extend with the v4 palette (bg #1C1C1E, sidebar #2A2A2D...)
- `[token]` Lines: hairSoft, hair, hairStrong, edge, fill/fillHover/fillSoft, well — v4: 0.5 px hairlines
- `[token]` Ink: ink, ink2, ink3, ink4, glyph, panMark — v4: text primary / secondary / tertiary
- `[token]` Accent and status: accent/accentHover/accentDeep/onAccent, ok, hot, warn, crit — v4: status red / orange / yellow / green / blue / link
- `[token]` Keys: keyMute, keySolo, keyRec, keyMon, keyFx, monitor — v4: strip keys R / A / M / S
- `[token]` Buses: busDrums, busBass, busMusic, busVocals (BGV), busLead, busSpeech, busAmbience, busMaster; busTint(MixBus) — v4: group colours
- `[token]` focusRing, disabled 0.38 — v4: keep
- `[token]` Radius: window 12, card 10, well 8, control 6, chip 6, pill 6, key 4 — v4: pill radius h/2, sidebar r16, workspace r14
- `[token]` Metric: toolbar 52, sidebar 208, sidebarRail 52, header 40, status 28, chainFoot 44, onAir 2, chanRail 180, tuneRail 198, trail 280, button/control/row 28, and more — v4: sidebar 214, etc.
- `[token]` Type: text(px, weight), mono, caps; setTextScale/textScale; textWidth — v4: SF Pro / SF Mono (a font change; ThemeStore text size must keep working)
- `[token]` Text drawing: drawText/drawFittedText via a layout cache; textCacheStats/reset/clear; text clip audit (begin/end/scope/report); shortPath — v4: KEEP (CLAUDE.md invariant: all text through Dine::drawText)
- `[token]` Surfaces: fillRounded, hairlineRounded, drawCard, drawWell, drawFilled, drawStandard, drawSheet, drawRule, drawChrome, drawHeaderBand, drawStatusBand, drawPanelGround, drawRaisedCard, drawInsetWell, drawSegmentTrack, sectionCase/drawSection, drawStatusChip, mix(), meterGradient/fillMeter — v4: reuse or extend
- `[token]` Icons: Icon enum (role icons plus the v3 set of 16 nav glyphs, Lock, WindowNav, Close, Headphones), drawIcon, iconForRole, iconChoices, iconFor — v4: icon set
- `[token]` Sources: roleGroups, friendlyRoleName — v4: "What it is" menus
- `[token]` Row helpers: drawSelectedRow, drawCaption, drawRadio, drawStackedBar, pillWidth/drawPill, drawDropChevron, drawLinkGlyph, levelColour — v4: reuse
- `[token]` Themes: themeBindings, applyTheme, setThemeColour, currentColours, currentThemeName, refreshAllWindows, refreshWindow, relayoutTree, styleTextEditor — v4: KEEP (themes must survive v4; new widgets re-read tokens in lookAndFeelChanged)
- `[token]` Gestures: dragOnly(Slider), nativeScrolling(Viewport) — v4: KEEP
- `[token]` AppStyle: kTopBar 56, kMargin 32, kMaxContentWidth 1080, contentArea — `AppServices.h:AppStyle` — v4: setup pages layout

---

## 2. Mixer, Tune, Live, Favourite mixes

Scope: `app/ui/MixerPage.*`, `app/ui/MixPage.*`, `app/ui/MacroPad.*`, `app/ui/LivePage.*`, `app/ui/FavouritesPage.*`, every line read.
The brief is `docs/design/v4/HANDOFF.md`. It still says "DLIVE" and "v3", but the homes below are its v4 pages, sheets, menus and shortcuts.
"MC" means `MixController` (`app/native/MixController.h`). "svc" means `AppServices`. "MainView" is `app/ui/MainView.cpp`, which is out of scope here; it is named only where it wires one of these pages.

---

### Mixer: MixerPage (page) and the toolbar row

- `[state]` View mode Strips/List (`enum View`). It is not persisted: every launch starts on Strips. — `MixerPage.h:View`, `MixerPage::setView` — v4: Mixer segmented "Strips / List"
- `[state]` Strip width S/M/L (`enum Size`: 58 / 74 / 106 pt; the master column is fixed at 150). Not persisted. — `MixerPage.cpp:columnWidthFor`, `MixerPage::setStripSize` — v4: Mixer "strip width S/M/L"
- `[state]` Show filter All/Inputs/Groups/Effects (`enum Show`). Not persisted. — `MixerPage::setShow`, `MixerPage::visibleInFilter` — v4: Mixer "All/Inputs/Groups". **The Effects filter has no v4 slot.** v4: KEEP current — add a 4th segment "Effects"
- `[control]` View tabs "Strips" / "List", with tooltips. — `MixerPage::MixerPage` (viewTabs) — v4: Mixer header segmented
- `[control]` Size tabs "S" "M" "L". Shown only in Strips view. — `MixerPage::MixerPage` (sizeTabs), `updateControls` — v4: Mixer header segmented
- `[control]` Show tabs "All" "Inputs" "Groups" "Effects". — `MixerPage::MixerPage` (showTabs) — v4: Mixer header segmented (+Effects, see above)
- `[control]` "Sends" toggle. It shows or hides the two send rows on every strip, and appears only in Strips view. — `MixerPage::setSendsVisible`, `Strip::setSendsVisible` — v4: KEEP current — Mixer header (no v4 slot; nearest is a View menu item)
- `[control]` "Clear solo" segment. It calls `MC::clearSolos()` and shows the toast "Solo cleared." It is enabled and lit only while `MC::numSoloed()>0`. — `MixerPage::MixerPage` (clearSolos) — v4: toolbar SOLO pill (click clears). Also KEEP it in the Mixer header or Mix menu 402.
- `[control]` "Open in a new window" icon button (WindowNav). It calls `onOpenWindow`, and MainView runs `openMixerWindow()`. It is hidden inside the detached window (`setWindowButtonVisible(false)`). — `MixerPage::windowButton`, `setWindowButtonVisible` — v4: Mixer "New Window" (also View menu 608)
- `[callback]` `onOpenStrip(int)` → MainView `inspectStrip` (opens the Inspector, or the Inspector window if one is detached). — `MixerPage.h` — v4: double-click → Inspector
- `[callback]` `onTuneStrip(int)` → MainView `tuneChannel`. — `MixerPage.h` — v4: TUNE CHANNEL (strip menu, plus the Mixer header button the brief asks for)
- `[callback]` `onOpenBus(MixBus)` → MainView `inspectBus`. — `MixerPage.h` — v4: double-click a bus → Inspector
- `[callback]` `onOpenWindow` → `openMixerWindow`. — `MixerPage.h` — v4: New Window
- `[callback]` `onOpenAssign` → shows the Assign page ("Fix the assignments…"). — `MixerPage.h` — v4: Routing › Inputs
- `[callback]` `onToast`. — `MixerPage.h` — v4: toast
- `[binding]` `selectStrip(int)` / `selectBus(MixBus)` move the selection and update the chain foot. MainView's chain foot and selection-driven commands (T) read `selectedStrip()` and `selectedBus()`. — `MixerPage::selectStrip/selectBus` — v4: Mixer selection → chain strip + right-rail quick inspector
- `[behaviour]` Strip order follows `mixBusInDisplayOrder`: the channels of each group, then that group's bus strip, then the FX returns, then the Master. The groups scroll with their channels and only the Master is pinned on the right. — `MixerPage::rebuild`, `layoutStrips` — v4: Mixer "Group buses after inputs, Master at the end"
- `[behaviour]` A bus strip exists only when `engine.isBusUsed(bus)`. A return strip exists only when `engine.isFxUsed(slot)`. The Master exists only when the master bus is used. — `MixerPage::rebuild` — v4: same
- `[behaviour]` The Master's source text reads "Stereo out N-M" / "Mono out N" / "Not routed", taken from `MC::getOutputFeeds().feeds[0]`. — `MixerPage::rebuild` — v4: Master strip routed-to label
- `[behaviour]` Auto-rebuild: a 30 Hz `refresh()` rebuilds the page when `graph.numStrips()` changes. — `MixerPage::refresh` — v4: same (virtualised)
- `[behaviour]` Tick cadence: meters, mute, solo and faders every tick; gain advice every 15 ticks (twice a second) and on every tune; the chain foot every 3 ticks; header controls every 5 ticks. — `MixerPage::refresh` — v4: the shared clock (Performance)
- `[behaviour]` Empty state: "Assign inputs first. The console fills with a strip for each one…". — `MixerPage::paint` — v4: Mixer empty state
- `[behaviour]` Chain foot (ChainStrip) shows the selected channel's or bus's chain. Its click calls `onOpenStrip` or `onOpenBus`. Empty text: "Click a strip to read its chain here…" or "Set the device up first." It carries a "BYPASS" note while bypassed. Hidden in the main window (`setFootShown(false)`, because the window owns the foot); the detached window shows its own. — `MixerPage::updateChainStrip`, `setFootShown` — v4: shell chain strip (click → Inspector)
- `[mode]` List view: one row per source on a shared `rowGrid`. As the window narrows the cells drop in a fixed order (gain, then pan, then out), then the name shrinks; minimum row 540 pt. Column heads: "#", "Name", "Gain staging", "Level arriving", "Fader", "dB", "Balance", "R·A·M·S·FX", "Group". — `rowGrid`, `Bank::paint`, `MixerPage::layoutList` — v4: Mixer List (dense table)
- `[mode]` Strip column layout drops sections in a fixed order when the window is short: sends, out, peak, inserts, then pan. The FX key row is reserved on every strip so the console's rows stay aligned. — `Strip::buildColumn` — v4: strip layout

### Strip (Channel / Bus / Master / Return)

- `[control]` Fader: −60…+12 dB, skew midpoint −12, double-click returns to 0.0 dB, drag-only. It calls `MC::setStripFader(strip, db, linked)`, `MC::setBusFader(bus, db)` or `MC::setFxSlotReturn(slot, db)`. — `Strip::Strip` fader.onValueChange — v4: strip fader (v4 fader law −60…−20 = 0–40%. **The law differs**; see Notes)
- `[gesture]` Cmd-drag a linked channel fader moves this one alone: `setStripFader(..., !isCommandDown())`. — `Strip::Strip` — v4: KEEP (tooltip says it)
- `[control]` Pan knob (Strips) or pan bar (List), channels only. It calls `MC::setStripPan`. Readout "C", "L23" or "R40". — `Strip::pan`, `panText` — v4: strip pan bar
- `[control]` R key (arm). It toggles `daw.getProject().tracks[strip].armed`, then `svc.daw().refresh()` and `svc.touchSession()`. Channels only. Tooltip: "Set to record (the engineer's word is arm)". — `Strip::armButton` — v4: strip R
- `[control]` A key (monitoring). Each click cycles `tracks[strip].monitor` through Input → Auto → Off, then `touchSession`. The letter shows I or A; the key is unlit when Off. — `Strip::monitorButton` — v4: strip A
- `[control]` M key. It calls `MC::setStripMute`, `setBusMute` or `setFxSlotMute`. Hidden on the Master. — `Strip::muteButton` — v4: strip M
- `[control]` S key. It calls `MC::setStripSolo`, `setBusSolo` or `setFxSolo`. Hidden on the Master. — `Strip::soloButton` — v4: strip S
- `[control]` FX key ("effects on this microphone"). It calls `MC::setStripEffects(strip, !stripEffectsOn)`. Shown only where `MC::stripCanHaveEffects(strip)`. — `Strip::fxButton` — **v4: no slot in R/A/M/S.** KEEP current: a 5th key under M/S
- `[behaviour]` Gain-staging chip from `MC::getInputAdvice(strip)`. Words: CLIPPING / FAINT / LOW / HOT / DIGITAL / NOT HEARD / OK / SPILL. Colours: crit / warn / hot / ink3 / ok. An unlit lamp means unknown. — `gainAdviceChip`, `gainAdviceColour`, `Strip::paintColumn` — v4: gain-health chip (Healthy / Clipping −6 / Digital +12 / Low +6 + tooltip). **v4 adds a dB number and an advice tooltip**; see Gaps
- `[behaviour]` Three insert slots: the lamp and sentence-case name of each active stage, from `activeChainStages(channel, isMaster, stereo, hasSampleStage)`. Re-read only when `chainHash` of `forEachDspField` changes. — `Strip::refresh`, `chainHash` — v4: 3 insert slots
- `[behaviour]` Two send slots: the first two used FX sends above `kSilenceDb` (Plate / Delay / Hall / Snare / Room) with a percentage bar (`sendPercent`, −40…+6 dB). An empty slot is drawn as a recess. — `Strip::refresh`, `paintColumn`, `fxName`, `sendPercent` — v4: 2 send slots (empty = dashed)
- `[behaviour]` Mute look: the name is struck through, the tint drops to 40%, and the meter goes grey (`meter.setMuted`). — `Strip::paintColumn/paintRow` — v4: "NOT HEARD" tag
- `[behaviour]` Linked fader: a link glyph after the name, and the tooltip names the partners (`MC::linkedNames`). — `Strip::refresh`, `drawLinkGlyph` — v4: KEEP (link indicator)
- `[behaviour]` BYPASS: the fader and pan are disabled while `MC::isBypassed()`. — `Strip::refresh` — v4: BYPASS locks faders and shows a toast on drag. **The drag toast is missing today**; see Gaps
- `[behaviour]` Meter: `engine.getStrip/getBus/getFx().getOutputMeter()` with `consumeMaxPeakDb`, `getMaxRmsDb` and `hasClipped`. The Master is stereo: L and R come from `getPeakDb(0/1)`. The peak readout "pk -x.x" refreshes past a 0.15 dB change. — `Strip::refresh` — v4: fader + stereo meter + peak
- `[behaviour]` Unity line across the fader and meter, plus a scale of +6/10/20/40/∞, drawn when the body is taller than 150 pt. — `Strip::paintColumn` — v4: fader scale
- `[behaviour]` Master loudness block: LUFS-I (big; accent colour on target, warn colour off it), a note ("on target" / "over target" / "under target" / "not measured"), Short, True peak (crit when over the ceiling), Limiter GR (warn when over 3 dB) and Target, all from `MC::getMasterLoudness()`. — `Strip::refresh/paintColumn` (Master) — v4: Master strip. KEEP; List view shows "LUFS" in the gain cell
- `[behaviour]` Routed-to label: the channel's group name, or "Master" for a bus or return. — `Strip` outText — v4: routed-to label
- `[behaviour]` Header caption: the number for a channel ("01"), "Group", "Return" or "Output". Tooltip: "name · source" (role · In N-M). — `Strip::paintColumn`, `MixerPage::rebuild` — v4: strip number + name
- `[gesture]` Click selects the strip (`select`). — `Strip::mouseUp` — v4: click selects
- `[gesture]` Double-click opens it (`onOpenStrip` / `onOpenBus`). — `Strip::mouseUp` — v4: double-click → Inspector
- `[gesture]` Right-click (popup) selects the strip, then shows its menu. Returns have no menu. — `Strip::mouseUp`, `Strip::showMenu` — v4: right-click menu

### Strip right-click menu (Channel)

- `[menu]` Header "CHANNEL · name". — `Strip::showMenu` — v4: menu title row
- `[menu]` 1 "TUNE CHANNEL" → `tune()` → `onTuneStrip`. — `Strip::showMenu` — v4: strip menu
- `[menu]` 2 "Open in the Inspector" → `open()`. — v4: strip menu
- `[menu]` Submenu "This microphone is" (voice channels only, via `MC::isVoiceChannel`). Items come from `MC::voiceJobs()`; the current role family is ticked; items are disabled under LIVE SAFE. A choice runs `MC::setInputRole(input, MC::roleForJob(...))`, then `svc.reconfigure()`. — `Strip::showMenu` — **v4 strip menu lists no job submenu.** KEEP current — the strip menu (and Tune › Voices covers speak/sing)
- `[menu]` 3 "Set to record" → `armButton.triggerClick()`. — v4: strip menu "record"
- `[menu]` 4/5/6 "Monitoring: Input / Auto / Off" set `tracks[strip].monitor`, then `touchSession`. — v4: strip menu "monitoring Input/Auto/Off"
- `[menu]` Submenu "Link fader with" or "Linked faders · names": every other input (`300+i`), linked ones ticked, "(linked elsewhere)" marked. A choice runs `MC::linkStrips({strip, other})`, or `MC::unlinkStrip(other)` when that input is already in the group. — v4: "link with next fader". **v4 is narrower**: KEEP the full submenu
- `[menu]` 9 "Unlink this fader" → `MC::unlinkStrip(strip)`. — v4: KEEP in the strip menu
- `[menu]` 7 "Fix the assignments…" → `assign()` → `onOpenAssign`. — v4: strip menu "fix assignments"

### Strip right-click menu (Bus / Master)

- `[menu]` Header "BUS · name". — v4: menu title
- `[menu]` 8 "Clear solo on this bus" → `MC::setBusSolo(bus,false)` (the Master shows it too; it does nothing there). — v4: KEEP in the bus strip menu
- `[menu]` 2 "Open in the Inspector" → `onOpenBus`. — v4: bus strip menu

### Mixer window and tests

- `[behaviour]` A detached MixerPage inside `MixerWindow` (MainView `openMixerWindow`; refuses with a toast when there are no inputs; brings an existing window to front). Shows its own chain foot. — MainView `openMixerWindow`, `MixerPage::setWindowButtonVisible/setFootShown` — v4: Mixer "New Window"
- `[binding]` `busStripCount()` / `returnStripCount()` are used by tests and snapshots. — `MixerPage.h` — v4: KEEP (test API)

---

### Tune: MixPage (the TUNE workspace)

### Layout and panels
- `[mode]` Three columns: the input rail on the left, the middle (groups, master, pads) which never scrolls, and the side panel on the right (296 pt) with its own scroll. — `MixPage::layout`, `MixPage.h` — v4: Tune
- `[control]` Rail tab "Inputs" (DinePanelTab, left). It toggles `setRailShown`. — `MixPage::railTab` — v4: Tune left rail, toggled with `[`
- `[key]` `[` → MainView 611 `togglePanel(true)` → `mixPage->setRailShown(!...)`. 615 "Show/Hide the two side panels" does the same on Tune. — MainView `togglePanel`, `handleCommand 615` — v4: `[`
- `[control]` Side tab "Tune" (right). It toggles `setSideShown`. Folding the panel shows the toast "TUNE MIX and the rest of the verbs are folded away…". — `MixPage::sideTab`, `setSideShown` — v4: right panel. KEEP the toast
- `[key]` `]` → MainView 612 `togglePanel(false)` → `mixPage->setSideShown`. — MainView — v4: `]`
- `[state]` `setRailAvailable(bool)`: the rail can be withdrawn completely (`railWidth` becomes 0). MainView does not call it today. — `MixPage::setRailAvailable` — v4: KEEP (API)
- `[state]` Rail-shown, side-shown, effects-open and the scope segment are not persisted. — `MixPage.h` — v4: same

### Input rail (InputRow)
- `[control]` Row: a lamp in the group's tint (warn colour when muted, crit when faint), the name, and a "FOCUS" mark on the focal input. — `InputRow::paint` — v4: Tune left rail
- `[gesture]` Click a row → `selectRow(i)` → `onSelectStrip` → MainView `lastChannel` + chain foot. The row opens to 68 pt with chips; only one row is open at a time. — `InputRow::mouseUp`, `MixPage::selectRow` — v4: rail selection
- `[control]` Chip "TUNE CHANNEL" (open row) → `selectRow` + `onTuneStrip(i)` → MainView `tuneChannel`. — `InputRow::mouseUp` — v4: rail TUNE CHANNEL
- `[control]` Chip "FOCUS" (open row) → `MC::setFocusInput(input)` (the MixPlanner focal input). — `InputRow::onFocus`, `MixPage::rebuildRail` — **v4: no FOCUS in the Tune brief.** KEEP current — rail chip
- `[behaviour]` Faint / muted marks re-read every 5 ticks: faint = `plan->strips[i].faint`, muted = `kept.strips[i].mute`. — `MixPage::refresh` — v4: rail
- `[behaviour]` "Check this input / these inputs" warning box at the foot of the rail, naming the faint inputs ("never rose above a whisper…"). — `MixPage::paint` — v4: KEEP (rail foot)
- `[behaviour]` Empty rail: "Assign inputs to see them here." — `MixPage::paint` — v4: same

### Groups row (GroupTile)
- `[control]` One tile per group bus in display order, plus an "FX returns" tile. Each has a fader (−60…+12, double-click 0) driving `MC::setBusFader`; the FX tile drives `MC::setFxReturn` (all returns), a return tile drives `MC::setFxSlotReturn`. — `MixPage::GroupTile` — v4: Tune group rows with meters + M/S
- `[control]` M on a tile → `MC::setBusMute`, `setFxMute` (FX tile) or `setFxSlotMute`. — `GroupTile` — v4: group M
- `[control]` S on a tile → `MC::setBusSolo` or `setFxSolo`. The FX tile has no S here. — `GroupTile` — v4: group S
- `[control]` "TUNE" verb on a group tile → `MC::startTuneBus(groupBus(i))`, with the toast "Listening to X alone…". — `GroupTile::mouseUp`, `MixPage::MixPage` — v4: KEEP current (per-group TUNE on the tile; also through the scope sheet "One group")
- `[control]` "Each effect" / "Back" verb on the FX tile → `showEffects(toggle)`. Opening it swaps the row to one tile per used return plus the FX tile. — `GroupTile::onOpen`, `MixPage::showEffects` — v4: KEEP current
- `[control]` "Back to groups" ghost button in the heading → `showEffects(false)`. Groups heading reads "Groups" or "Effects". — `MixPage::backToGroups` — v4: KEEP current
- `[behaviour]` Tile states: "NOT USED"/"OFF", "NOT HEARD"/"MUTED", or the dB value. Grounds: muted = refuse, soloed = soloGround. While listening a lamp shows heard (ok) or not yet (warn), from `MC::busHeard`. — `GroupTile::paint`, `set` — v4: KEEP
- `[behaviour]` Tiles are whole or absent. A narrow window drops trailing tiles rather than squeezing them; minimum tile 52 pt. Names step down in size, then squeeze, then switch to a brief form ("Plate", "FX"). — `MixPage::resized`, `GroupTile::paint` — v4: layout rule
- `[behaviour]` The effects row closes itself when the session loses its effects. — `MixPage::refresh` — v4: same

### Master card
- `[behaviour]` Master card with three cells: Level (`kept.buses[Master].faderDb`), Loudness ("x now · y target", warn when off target), True peak (crit when over the ceiling). Each cell falls back to a brief form. — `MixPage::refreshMaster`, `paint` — v4: KEEP (Tune master row)
- `[control]` Loudness target popup (DinePopup). Menu: "From the purpose (N LUFS)", then every `DeliveryLoudness` (Broadcast −23, BroadcastUS −24, Podcast −18, Streaming −16, StreamingLoud −14, Loud −12) with the current one ticked. A choice runs `MC::setDelivery`. — `MixPage::loudnessTargetButton` — v4: Tune "loudness target menu". Mix menu 430-436 duplicates it
- `[control]` "Raise loudness to target" → `MC::raiseLoudnessToTarget()`, which returns the toast text. Enabled only when `MC::previewLoudnessMove().possible && !isLiveSafe()`. The tooltip carries the sentence or the reason it cannot. — `MixPage::raiseButton`, `refreshMaster` — v4: KEEP current (Tune master); Mix menu 420
- `[control]` Voicing popup "Sound: <name>": every `MasterVoicing` (Neutral "as tuned", Warm, Bright, VoiceFirst, PhoneSpeakers, Earbuds, Car, TvSoundbar) with its hint. A choice runs `MC::setVoicing` and shows a toast. — `MixPage::voicingButton` — v4: Tune "voicing menu". **The v4 items differ** (Room and stream / Stream first / Room first / Recording); see Gaps. Mix menu 450-457

### Macro pads (inside MixPage)
- `[control]` Pad "BODY × VOICE" (across = Bass, up = Vocals; corners AIRY/PRESENT/DARK/THICK; snaps Speech(30,78), Choir(68,58), Plan(50,50)) → `MC::setMacro`. — `MixPage::MixPage` pads[0] — v4: Tune macro pads
- `[control]` Pad "DRIVE × ROOM" (across = Space, up = Drums; corners DRY HIT/BIG HIT/FLAT/WASH; snaps Tight(22,74), Room(76,62), Plan) → `MC::setMacro`. — pads[1] — v4: Tune macro pads
- `[control]` Ribbon "ENERGY" (MixMacro::Energy, stacked) → `MC::setMacro(Energy)`. — `MixPage::ribbon` — v4: Tune ribbon
- `[control]` "Centre both pads" ghost → `centreMacroPads()` → `MC::resetMacros()`, and pads and ribbon ease to 50. — `MixPage::resetMacrosButton` — v4: KEEP (also Mix menu 401 "Centre Macro Pads", with a toast)
- `[behaviour]` Each tick `syncMacros` reads `MC::getMacros()` and sets the LIVE SAFE fence from `MC::macroRange()` with the reason `liveSafe::macroLimitReason(MC::getLiveSafePolicy())`. — `MixPage::syncMacros` — v4: same
- `[behaviour]` Pad card in the side panel: "HOLDING · <pad>", "<pad> MOVED", "BOTH PADS MOVED" or "BOTH PADS AT THE PLAN", with `MixMacros::tooltip` sentences. — `MixPage::updateSide` — v4: Tune "padCard"
- `[binding]` `setMacroValue(MixMacro, float)` exists for tests and snapshots. — `MixPage.h` — v4: KEEP (API)
- `[mode]` Compact layout: pads drop their snap row when the window is short; pad size 150-236. — `MixPage::layout`, `MacroPad::setCompact` — v4: layout rule

### Side panel (SidePanel): the verbs, Voices, Aim at, Mix health
- `[control]` Scope segment "Whole mix / One group / Selected" (`wantedScope` 0/1/2). Disabled while busy. A sentence under it explains the choice. — `MixPage::scopeTabs`, `SidePanel::scopeSentence` — v4: Tune "scope (whole mix / one group / some channels)"
- `[control]` TUNE MIX (filled). Its label reads "Tune mix", "Re-tune" or "Cancel" by state; it is disabled while live-tuning or Planning → `pressTune()`. — `MixPage::tuneButton`, `refreshTuneButton` — v4: Tune primary verb
- `[behaviour]` `pressTune()`: while listening or live-tuning it calls `MC::abortTuneMix()`; when the scope sheet is open it closes it; scope 0 → `MC::startTuneMix()` directly; scope 1/2 → `scopeSheet->open(selectedRow)` + `chooseForSnapshot(wantedScope)`. Also reached from Mix menu 400 (guarded by MainView `liveSafeBlocks("TUNE MIX")`). — `MixPage::pressTune` — v4: Tune flow (modal sheet)
- `[control]` TUNE LIVE MIX. Label "Tune live mix", "Re-tune live" or "Stop" → `pressLiveTune()`: aborts while running, otherwise `MC::startTuneLiveMix()`. — `MixPage::liveTuneButton`, `pressLiveTune` — v4: toolbar TUNE LIVE MIX + Tune flow (Mix menu 405)
- `[control]` "Match to reference" / "Matched ✓" → `openReference()`, which toggles ReferenceSheet (refused while the listen or result sheet is up). — `MixPage::referenceButton`, `openReference` — v4: Tune "reference mix" → Reference sheet (File 107 / Mix 408 open it too; Mix 407 = `MC::startReferenceMatch`)
- `[control]` "Check inputs" → `onOpenCheck` → MainView `showCheck` (CheckSheet). — `MixPage::checkButton` — v4: Check inputs sheet (sidebar)
- `[control]` "Mix Buddy" → `onOpenChat` → MainView `showChat`. — `MixPage::chatButton` — v4: toolbar Mix Buddy toggle
- `[control]` "Undo mix": `MC::canUndoMix` ? `MC::undoMix()` with a toast : the toast "There is no earlier mix…". — `MixPage::undoButton` — v4: ⌘Z (Mix domain) + KEEP current (Mix menu 411)
- `[control]` "Redo mix": `MC::redoMix()` with a toast, or "This is the newest mix…". — `MixPage::redoButton` — v4: Mix menu 412 + KEEP
- `[control]` "Mix history" → `onOpenHistory` → `showHistory`. Enabled only when `MC::getCheckpoints()` is not empty. — `MixPage::historyButton` — v4: Mix history sheet (sidebar)
- `[control]` "Open the Inspector" ghost → `onOpenAdvanced` → `showPage(Inspector)`. — `MixPage::advancedButton` — v4: ⌘3 / sidebar
- `[behaviour]` Stamp: "Tuned N times this session" or "Not tuned yet" (`MC::getTuneCount`). — `SidePanel::paint` — v4: KEEP
- `[control]` Voices: one VoiceRow per voice channel (`MC::isVoiceChannel(input)`), each with a "Speaking" / "Singing" segment. Speaking → `MC::setInputRole(in, MC::roleForJob(in, Speech))`. Singing → keeps BackingVocal or Choir, otherwise LeadVocal. Either then calls `onGraphChanged` → `svc.reconfigure()`. — `MixPage::rebuildVoices`, `VoiceRow` — v4: Tune "voices (speak/sing per vocal mic)"
- `[control]` Aim at: "Pick a favourite mix" / "<name> (favourite)" / "Nothing marked yet" (disabled when there are none) → `onOpenFavourites` → `showPage(Favourites)`. The name is matched against `MC::getReference().name`. — `MixPage::aimButton`, `refreshTuneButton` — v4: Tune "aim-at-favourite"
- `[behaviour]` Mix health score 0-100 (`MC::getMixHealthPercent`), coloured accent ≥80, warn ≥55, otherwise crit. Notes come from `MC::getMixHealthNotes()` and are keyword-coloured (clipping/barely/not heard = crit; preamp/digital = warn). A status line comes from `MC::getStatusText()` unless it duplicates a note. — `SidePanel::layoutFor/paint`, `MixPage::refresh` — v4: Tune "health score"
- `[behaviour]` Repaint gate: `PageLook` holds the status, notes, health, stage, tunes, reference, undo/redo and pad card; the side panel relays out only when one of these changes. — `MixPage::refresh` — v4: perf rule (KEEP)
- `[behaviour]` Readiness gates in `refreshTuneButton`: ready = `isPrepared && numStrips>0`. Reference, chat, check and scope are disabled when not ready or busy. — `MixPage::refreshTuneButton` — v4: same

### Tune flow sheets (all are overlays inside MixPage; desk 88% scrim)
- `[sheet]` ScopeSheet "What should DINE tune?" (620 pt card). Tabs: "The whole mix / One group / Some channels". It opens fresh each time with the rail's selected row pre-ticked and the first used group picked. — `MixPage::ScopeSheet::open` — v4: Tune scope sheet
- `[control]` Scope › group buttons, one per used group bus (up to 4 per row); "NO GROUPS YET" when there are none. — `ScopeSheet` groupButtons — v4: scope sheet
- `[control]` Scope › channel list (scrolling, 8 rows visible; tick, number, name, group) toggles `picked`, with a "Select all" / "Select none" ghost and the caption "CHANNELS · N PICKED". — `ScopeSheet::Row`, allButton — v4: scope sheet
- `[behaviour]` Scope sentence: what one press will do (it names `MC::channelListen().seconds` for one channel, "30 seconds" for the mix). — `ScopeSheet::sentence` — v4: copy
- `[control]` Start: "Tune the mix", "Tune <Group>", "Tune this channel" or "Tune N channels"; disabled until a choice is valid → `MC::startTuneMix()` / `startTuneBus(groupBus(g))` (display order mapped to the enum) / `startTuneStrips(vector)`, each with a toast. — `ScopeSheet::begin` — v4: scope sheet
- `[control]` Cancel closes it. — `ScopeSheet::cancel` — v4: Esc
- `[key]` Esc closes the scope sheet (MainView `openSheetName()=="tunescope"`, `closeSheets` → `closeScopeSheet()`). — MainView, `MixPage::closeScopeSheet` — v4: Esc order (menu → sheet → tune → chat)
- `[binding]` `isScopeSheetOpen`, `setScopeForSnapshot(scope, group)` and `selectRow` are used by tests and snapshots. — `MixPage.h` — v4: KEEP (API)
- `[sheet]` ListenSheet (560×384). It is shown automatically while a mix tune (not a channel tune) is Listening or Planning, or live-tuning. Title "<VERB> is listening / is waiting / is working", where VERB is TUNE MIX / TUNE LIVE MIX / TUNE <BUS> / TUNE N CHANNELS / TUNE CHANNEL. — `ListenSheet::paint`, `tuneVerb`, `MixPage::refresh` — v4: Tune flow "Listening phase"
- `[behaviour]` Listen lines: "waiting for the band" (`MC::isWaitingForBand`), the planning line naming the profile, or for a live tune `MC::getTuneLiveStatus()` + "Keep the band playing." — `ListenSheet::paint` — v4: copy
- `[behaviour]` Step lamps. Live: Listen / Measure / Decide / Resolve / Check / Apply / Verify / Refine, mapped from `TuneLiveCoordinator::State`. Otherwise: Listen / Analyse / Decide. Spacing is computed so no label is ever cut. — `kLiveSteps`, `liveStepFor` — v4: Listening phase. **v4 shows a 30 s countdown ring instead**; KEEP the steps
- `[behaviour]` Progress bar from `MC::getListenProgress()`: a sweep animation while working live, with "N of M seconds" (`getListenSeconds`), "% through" or "Waiting for the band". — `ListenSheet::paint` — v4: countdown ring
- `[behaviour]` Per-group "Heard" / "Not yet" list (`MC::busHeard`). — `ListenSheet::paint` — v4: "list of what's heard/silent with reason". **No reason text today**
- `[control]` "Stop listening" / "Stop" → `MC::abortTuneMix()`. — `ListenSheet::cancel` — v4: Listening cancel
- `[behaviour]` Step timer: `tick()` records when the TuneLive state changes (`stepSeconds()` is not displayed). — `ListenSheet::tick` — v4: n/a
- `[sheet]` ResultSheet. It is shown automatically when `Stage::Preview && hasPlan && !tuningLive`, for mix tunes only. Title "<VERB> is ready", then "On <MC::getLastTuneScope()>. Heard N inputs, proposed X settings and Y levels. <headline>" (or the headline alone when no change is required). — `ResultSheet::paint` — v4: Changes list
- `[behaviour]` A warn line: "The room and the stream hear BEFORE and AFTER as you switch. Nothing is kept until KEEP." — `ResultSheet::paint` — v4: KEEP the copy (A/B is on air)
- `[behaviour]` Bullets, up to 6. After a live tune: `TuneLiveCoordinator::getReview()` lines (what/why; NotPossible and Refused show "NOT DONE"). Otherwise: up to 2 `plan->notes`, then `plan->relationships` (what, why, `formatRecommendationValues`). The value column auto-widens to 230 max; layout is cached per content stamp and width. — `ResultSheet::bullets`, `layoutFor` — v4: Changes list (channel, change, why, **confidence**: no confidence field today; see Gaps)
- `[control]` BEFORE → `MC::setCompare(Before)` + toast; AFTER → `setCompare(After)` + toast. The filled style marks the active one. — `ResultSheet` — v4: Before/After A/B
- `[control]` KEEP SOME group chips, shown when the plan touches more than one group (touched = `MixPlanner::restrictTo(plan, PlanSelection::group(...)).noChangeRequired == false`). A toggle runs `MC::setPlanSelection(sel)` or `clearPlanSelection()`, forces AFTER, and toasts. KEEP then reads "Keep these". — `ResultSheet::rebuildChips/applySelection` — **v4: not in brief.** KEEP current — result sheet
- `[control]` KEEP → `MC::keepPlan()` (refused under LIVE SAFE in MC) + toast "Kept. Every value it set is marked TUNED BY DINE…". — `ResultSheet::keep` — v4: Keep (snapshot into Mix History; MC already checkpoints)
- `[control]` REVERT → `MC::revertPlan()` + toast. — v4: Revert
- `[control]` "Try another mix" → `MC::tryAnotherMix()`, enabled per `MC::canTryAnotherMix()`. — v4: "Tune again" (nearest); also Mix menu 410
- `[control]` "Review in the Inspector" / "Inspector" (dropped when it does not fit) → `onOpenAdvanced`. — v4: KEEP current — result sheet footer
- `[control]` Close (×) → `MC::revertPlan()` + toast "Closed without keeping…". — `ResultSheet::closeButton` — v4: sheet close = revert
- `[behaviour]` The result sheet refreshes every 10 ticks while shown. The reference sheet hides when a listen or preview starts, unless it is measuring. — `MixPage::refresh` — v4: same
- `[sheet]` ReferenceSheet (its own file, out of scope): hosted here; `onClose` / `onToast` wired; `refresh` each tick while visible or measuring. — `MixPage::referenceSheet` — v4: Reference mix sheet
- `[behaviour]` Channel tunes (`MC::isTuningChannel`) never raise the Listen or Result sheets on this page; MainView's ChannelTuneSheet handles them. — `MixPage::refresh` (`mixTune`) — v4: Tune flow shared by CHANNEL. Note the split

### MixPage callbacks (all wired in MainView)
- `[callback]` `onOpenAdvanced` → Inspector · `onTuneStrip` → tuneChannel · `onToast` · `onOpenChat` → showChat · `onOpenHistory` → showHistory · `onOpenCheck` → showCheck · `onOpenFavourites` → Favourites page · `onSelectStrip` → lastChannel + chain foot · `onGraphChanged` → `svc.reconfigure()`. — `MixPage.h` — v4: as listed above

---

### MacroPad / MacroRibbon (widgets)

- `[gesture]` Pad mouseDown anywhere in the square: grabs focus, starts the drag, jumps the puck there (absolute). — `MacroPad::mouseDown` — v4: macro pad
- `[gesture]` Pad drag: absolute, keeps tracking outside the square. — `MacroPad::mouseDrag` — v4: same
- `[gesture]` Pad double-click → `settleTo(50,50)` (the plan) with a 140 ms ease-out. — `MacroPad::mouseDoubleClick` — v4: same
- `[key]` Pad arrows ±1, Shift+arrows ±5 (left/right = across, up/down = up). — `MacroPad::keyPressed` — v4: KEEP
- `[control]` Pad snap buttons (named points, shown as one track). A snap eases the puck there and reads as lit only while the puck sits exactly on it (±0.5). — `MacroPad::refreshSnaps` — v4: KEEP
- `[behaviour]` LIVE SAFE fence: `setLimits(lo,hi,reason)` clamps every value and draws hatched bands, and the reason goes into the tooltip. — `MacroPad::setLimits`, `hatch` — v4: KEEP
- `[behaviour]` Values readout ("BASS 62 · VOCALS 48") shown only when off the plan or while dragging. Dashed ring at the centre, corner words, puck with a glow while dragging, focus ring. — `MacroPad::paint` — v4: KEEP
- `[behaviour]` Accessibility: slider role, a value interface (0-100), `announce()` on change. — `MacroPad::createAccessibilityHandler`, `announce` — v4: KEEP
- `[callback]` `onChange(MixMacro, float)` → `MC::setMacro`; `onActiveChanged` → the side pad card ("HOLDING"). — `MacroPad.h` — v4: KEEP
- `[behaviour]` Easing timer: 60 Hz while settling, then stops. **This is a per-component timer**; v4 wants one shared clock. — `MacroPad::timerCallback`, `MacroRibbon::timerCallback` — v4: port to the shared clock
- `[gesture]` Ribbon mouseDown/drag: absolute on the track. Double-click → `settleTo(50)`. — `MacroRibbon` — v4: ribbon
- `[key]` Ribbon left/right ±1, Shift ±5. — `MacroRibbon::keyPressed` — v4: KEEP
- `[mode]` Ribbon stacked (name and value over the track) or wide (with low/high labels from `MixMacros::lowLabel/highLabel`). Tune uses stacked. — `MacroRibbon::setStacked` — v4: ribbon
- `[behaviour]` Ribbon fence hatch, a fill from the centre, the unity tick, focus ring, and accessibility. — `MacroRibbon::paint` — v4: KEEP

---

### Live: LivePage

- `[behaviour]` Health strip with four stats. Recording ("N tracks · mm:ss" + "X left" from `daw.getRecordingSecondsFree`, re-read every 60 ticks; or "Stopped" + "N set to record"). On air (`svc.outputDisplayName()`, "Off", "No output", + LUFS-I). Clipping (inputs whose `MC::getInputAdvice` is Clipping, first two named, "fix it at the console", re-read every 15 ticks). Headroom (−master peak dB, crit <0.5, warn <3, + "TP x dBTP"). A note is dropped whole when it will not fit. — `LivePage::refresh`, `paint`, `updateDiskNote` — v4: Live "mix health"
- `[control]` Group strips, one per group bus plus "FX returns". Fader (−60…+12, double-click 0, 26×40 cap) → `MC::setBusFader`, `setFxReturn` or `setFxSlotReturn`. The fader is not re-synced while the mouse is down. — `LivePage::GroupTile` — v4: Live group strips
- `[control]` Strip M → `MC::setBusMute`, `setFxMute` or `setFxSlotMute`. — `GroupTile` — v4: Live M
- `[control]` Strip S → `MC::setBusSolo` or `setFxSolo`; **the FX strip's S runs `MC::setFxSoloAll(!anyFxSolo())`** (Tune has no FX solo). — `GroupTile` — v4: Live S
- `[behaviour]` Strip states "Off", "Not heard" (grey meter keeps moving) or "Solo", otherwise "±x.x dB". Unity tick over the slot, hidden when the cap sits on it. The meter shows peak only (peak, peak, clip = peak > −0.2). Fader, M and S are disabled when the group is unused. — `GroupTile::refresh/paint/paintOverChildren` — v4: KEEP
- `[gesture]` Click the FX strip's name area (y < 52) → `showEffects(true)`. — `GroupTile::mouseUp` — v4: KEEP
- `[control]` "Each effect" / "Back to groups" button next to the heading. Visible only when `anyEffects()`. — `LivePage::effectsButton`, `showEffects` — v4: KEEP current
- `[control]` Scene picker: four segments, `defaultSceneName` Band/Speech/Worship/Custom, or the stored `MixScene.name` uppercased. A click picks the slot and, if it is kept, runs `MC::recallScene(i)`; otherwise the toast "Nothing is kept under X yet…". The tooltip names the kept time. — `LivePage::sceneSegments`, `refreshScenes` — v4: Live scenes/setlist
- `[control]` "Keep" → `MC::keepScene(sceneSlot)` + toast. Disabled until a slot is picked. — `LivePage::keepButton` — v4: Live scenes
- `[state]` Scene slot selection (`sceneSlot`) is page-local and not persisted. Scenes are persisted through MC/SessionState. — `LivePage.h` — v4: setlist
- `[behaviour]` `focusScenes()`: the sidebar SCENES row → Live, with an accent ring around the picker fading over 45 frames. — `LivePage::focusScenes`, `paint` — v4: sidebar "Setlist" → Live (ring cue)
- `[control]` LIVE SAFE card link. When off, "Turn on ›" → `svc.daw().setLiveSafe(true)` + `onLiveSafeChanged` + toast. When on, "What's locked ›" → a CallOutBox `SafeDetail` (Locked / Blocked / Allowed, with `policy.maxFaderStepDb` and `maxMasterStepDb`). **It cannot turn LIVE SAFE off from here.** — `LivePage::safeLink`, `SafeDetail` — v4: toolbar LIVE SAFE switch + Live card (KEEP the callout)
- `[behaviour]` LIVE SAFE card copy and colours (refuse ground with a warn hairline when on). Tooltip from `liveSafe::lockedSummary/allowedSummary`. — `LivePage::safeText`, `paint` — v4: KEEP
- `[control]` Autopilot card link. When off, "Turn on ›" → `MC::setAutopilot(true)` (MC explains any refusal). When on, the link is inert and reads "Healthy since hh:mm" / "Holding since hh:mm"; the toolbar is the one off switch. — `LivePage::autopilotLink` — v4: Live autopilot + toolbar "Auto" (Mix menu 415 toggles)
- `[behaviour]` Autopilot log: the two newest `MC::getCheckpoints()` entries starting "Autopilot: ", stopping at "Before Autopilot", reformatted with time. Empty: "Nothing to do: the mix is inside tolerance." Fence text from `MC::getAutopilotLimits().maxTotalDb`. Moved = `getAutopilot().groupsCorrected>0`. — `LivePage::refresh`, `autopilotText` — v4: Live autopilot (apLog)
- `[control]` Speaking mics › "Speech priority" link: "Turn on ›" / "Turn off" → `MC::setSpeechPriority(!get)`. Depth from `MixProfile::speechPriority(profile).depthDb`. — `LivePage::priorityLink`, `priorityText` — v4: Live "speaking-mic tiles" (Mix menu 413)
- `[control]` Speaking mics › "Share the mics" link → `MC::setAutoMix(!get)`. Depth from `MixProfile::autoMix(profile).depthDb`. Reads "On, waiting for a second speaking mic." when fewer than 2 strips sit on Speech. — `LivePage::shareLink`, `shareText` — v4: Live speaking mics (Mix menu 416)
- `[behaviour]` The Speaking mics card goes compact (names only) when short and disappears entirely when nothing fits (the Mix menu still has both). — `LivePage::resized` — v4: KEEP
- `[control]` "What I hear" › MONITOR SOLO → `MC::setSoloMode(Monitor)`. — `LivePage::modes[0]` — v4: Live listen controls
- `[control]` SOLO IN PLACE → `MC::setSoloMode(InPlace)` (MC refuses under LIVE SAFE). — `modes[1]` — v4: Live listen
- `[control]` AFL → `MC::setSoloPoint(AFL)`; PFL → `setSoloPoint(PFL)`. — `modes[2..3]` — v4: Live listen
- `[control]` CLEAR SOLO → `MC::clearSolos()` + toast. Enabled when `numSoloed()>0`. — `LivePage::clearSolo` — v4: Live listen + toolbar SOLO pill
- `[control]` DIM → `MC::setMonitorDim(!dim)` (the engineer's headphones only). — `LivePage::monitorDim` — v4: Live listen. Distinct from the toolbar broadcast DIM −20 dB; do not merge them
- `[control]` Solo device popup → `OutputsSheet::showSoloDeviceMenu(MC, svc, anchor, cb)` (shared menu). Label from `OutputsSheet::soloChoiceLabel`, brief "Pick" / "Here" / device. Disabled when audio is not running. — `LivePage::soloDevice` — v4: "Live page keeps its own solo picker (same shared menu)"
- `[control]` Monitor level line, −40…+12 dB (skew −8, double-click 0) → `MC::setMonitorGain`. Tooltip shows the dB. — `LivePage::LevelLine`, `monitorLevel` — v4: Live listen
- `[behaviour]` Monitor note: "Careful: pressing S is heard by the room…" (in place), "Solo has nowhere to go yet…" (no `MC::hasMonitorOutput`), "N soloed. Only you hear it.", or "Press S on a group…". Headphones icon turns warn when unrouted. — `LivePage::refresh/paint` — v4: KEEP copy
- `[behaviour]` Layout: the rail is 352 pt (min 280, a third of the width). The mode tracks stack when narrow. The card heights are measured in `resized` (`wrapLines`), and the page reflows only when a card's content changes. — `LivePage::resized`, `Look` — v4: Live layout
- `[behaviour]` `rebuild()` refreshes the scenes, tiles and monitor and closes the effects row if effects are gone. `refresh()` runs at 30 Hz. — `LivePage::rebuild/refresh` — v4: same
- `[callback]` `onToast`, `onLiveSafeChanged` (→ MainView `updateChrome`). — `LivePage.h` — v4: toast / toolbar
- `[callback]` `onToggleRecord` and `onOpenHistory` are declared and wired by MainView (501 / showHistory) but **never invoked inside LivePage.cpp**. — `LivePage.h` — v4: KEEP the hooks (nearest homes: toolbar Rec, Mix history)
- `[behaviour]` Live in its own window: `MainView::openLiveWindow` → PageWindow "Live" (900×560 min, 1280×760); the same callbacks are wired. — MainView `openLiveWindow` (View menu 617) — v4: KEEP current — View menu
- `[behaviour]` Scene renaming: `MC::renameScene` exists, but **no UI on LivePage** (the tests call it). — `MixController.h:renameScene` — v4: Setlist "cue editing" (backend exists)

---

### Favourite mixes: FavouritesPage

- `[control]` "Mark current mix as favourite" (filled, check icon). Opens an AlertWindow "Mark this mix as a favourite" with a name field (default `svc.currentSessionName()` or "Mix N+1"), "Mark it" (Return) and "Cancel" (Esc). Then `MC::markFavourite(name)`, then `svc.touchSession()`, and a toast; on failure the toast "There is no mix to keep yet…". Disabled when the session has no inputs. — `FavouritesPage::markCurrent` — v4: Favourite mixes page ("Mark current")
- `[control]` Card "Aim TUNE MIX at this" → `MC::useFavouriteAsReference(i)` (MC explains any refusal), then `touchSession` and the toast "The next TUNE MIX aims at…". It becomes "Aimed at" (disabled) on the aimed card. — `FavouritesPage::rebuild`, `Card::setAimed` — v4: card "Aim at"
- `[control]` Card "Restore mix" → `MC::recallFavourite(i)`, then `touchSession` and a toast; on failure the toast "kept on a different set of inputs…". — `FavouritesPage::rebuild` — v4: card "Restore"
- `[behaviour]` The aimed card is the one whose name equals `MC::getReference().name` (with `ref.valid`): accent border, an "AIMED AT" badge, an accent star. — `FavouritesPage::refresh`, `Card::paint` — v4: KEEP
- `[behaviour]` Card body: name, date · profile (`styleProfileName`), then five fingerprint lines from `f.sound.metric(...)`: Lead over band, Lead over BGV (fallback lead_over_backing_db), Speech over master, Drums against band (fallback kit_against_bass_db), Master LUFS. Unmeasured: "This mix was kept before DINE had listened…". — `Card::paint` — v4: card
- `[behaviour]` Cards (372×440) wrap in a scrolling grid, rebuilt when `numFavourites()` changes. Empty state: "Nothing is marked yet…". Page title and subtitle. — `FavouritesPage::resized/paint/refresh` — v4: Favourite mixes
- `[callback]` `onToast`. — `FavouritesPage.h` — v4: toast
- `[behaviour]` Rename and delete a favourite: `MC::renameFavourite` / `removeFavourite` exist. Delete is reachable only from HistorySheet; **rename has no UI anywhere**. — `MixController.h` — v4: Favourite card ⋯ menu (backend exists)
- `[behaviour]` The sidebar count for "Favourite mixes" is read from `MC::numFavourites()` (by MainView/Sidebar). — v4: sidebar badge

---

---

## 3. Inspector and Tracks

Scope read in full: `app/ui/AdvancedPage.{h,cpp}`, `app/ui/ChainEditor.{h,cpp}`, `app/ui/TracksPage.{h,cpp}`.
Skimmed: `app/native/SampleLibrary.h`, `app/native/DrumKits.h`, `src/DSP/SampleBank.h`, `src/DSP/SampleReplacer.h`,
`src/DSP/ChannelParameters.h` (param IDs), `src/Profiles/ProfileData.cpp` (sample defaults), `docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md`,
and the `MainView.cpp` wiring for these two pages (callbacks, Track/Edit/View menus, `commandForKey`).
Brief: `docs/design/v4/HANDOFF.md` (says "DLIVE v3"; the app is DINE). Parameter IDs are the `ParamID::` strings in
`src/State/ParameterIDs.h` (identical to the field names shown, e.g. `replaceOn`). Released IDs must never be renamed.

v4 page names used below: **Inspector** (⌘3 in v4), **Inspector · Sample stage**, **Tracks** (⌘5 in v4), **Chain strip** (shell foot),
**Mix history** sheet, **Toast**, **Context menu**, **File/Session menu** (toolbar session menu). The v4 brief has no Edit / Track / View
menu bar list, so items that today live only in those menus are marked "v4: nearest menu".

### Inspector — page shell and head (AdvancedPage)

- `[mode]` Inspector page: rail (channels by bus) | channel head + signal path + stage card | trail ("What DINE did") — `AdvancedPage.h:AdvancedPage` — v4: Inspector page
- `[binding]` Page shown/rebuilt on visit; `refresh()` at 30 Hz from MainView tick — `MainView.cpp:showPage / tick → advancedPage->refresh()` — v4: Inspector (must move to the shared VBlank clock per brief)
- `[behaviour]` Repaint-skip cache `InspectorLook` (selection, rowCount, bypassed, prepared, rail/trail, tuneCount) — page paints only when one of these changed — `AdvancedPage.h:InspectorLook`, `AdvancedPage::refresh` — v4: KEEP (perf requirement)
- `[behaviour]` Rebuild when `controller.getGraph().numStrips()` changes; re-entrancy guard `rebuilding` (prevents rebuild→select→refresh loop after a large import) — `AdvancedPage::refresh`, `AdvancedPage::rebuild` — v4: KEEP (easy to lose)
- `[control]` Channel head: name (22 pt), group colour bar, sub-line "IN 4 · Drums · tuned 8:27 PM" (drops tuned clock, then group, on narrow windows) — `AdvancedPage::paintHead`, `deviceInLabel`, `tunedLabel` — v4: Inspector head ("who set this" line)
- `[state]` Head sub-line for buses: "DRUMS · N inputs" / "MASTER · Master bus" — `AdvancedPage::paintHead` — v4: Inspector head
- `[control]` Simple / Advanced segment — `AdvancedPage::simpleTab/advancedTab`, `setSimpleView(bool)` — v4: Inspector Simple / Advanced toggle
- `[behaviour]` Simple is unavailable on a bus/master selection (stays on chain); default view is Advanced; view is not persisted — `AdvancedPage::setSimpleView`, `showSelection` — v4: Inspector (keep rule)
- `[control]` RE-TUNE button → `onRetune` → MainView `handleCommand(400)` (TUNE MIX…) — `AdvancedPage::retuneButton` — v4: Inspector RETUNE button
- `[control]` TUNE CHANNEL button (Advanced view, strip only; shortens to "TUNE" when the head is narrow) → `onTuneChannel(strip)` → `MainView::tuneChannel` — `AdvancedPage::tuneChannelButton`, `resized` — v4: Inspector (primary white pill)
- `[control]` MUTE key in head (strip → `MixController::setStripMute`; group bus → `setBusMute`; hidden for Master; disabled under BYPASS; hidden when it doesn't fit) — `AdvancedPage::muteButton`, `refreshKeys` — v4: KEEP current — Inspector head (brief does not draw it)
- `[control]` SOLO key in head (`setStripSolo` / `setBusSolo`; same rules) — `AdvancedPage::soloButton`, `refreshKeys` — v4: KEEP current — Inspector head
- `[callback]` `onBack` → `showPage(Page::Tune)` (in-window copy: closes window) — `AdvancedPage::onBack`, `MainView.cpp:1354` — v4: KEEP (no visible control today; "Back to DINE" in brief is a different thing — see trail/put back)
- `[callback]` `onImportSample(RoleFamily)` → MainView FileChooser (*.wav;*.aif;*.aiff;*.flac, starts in ~/Music) → `AppServices::importSample` → toast → `rebuild()` — `MainView.cpp:1358` — v4: Inspector · Sample stage "Import a sound"
- `[callback]` `drumKitName()` → `MainView::drumKitName` → `readSampleChoices` + `currentDrumKit` — `AdvancedPage::drumKitName` — v4: Inspector · Sample stage kit picker
- `[callback]` `onDrumKit(anchor)` → `MainView::drumKitMenu` → `applyDrumKit` (LIVE SAFE blocks it; one mix-history entry; toast) — `AdvancedPage::onDrumKit` — v4: Inspector · Sample stage kit picker
- `[binding]` `select(strip)` / `selectBus(bus)` / `selectStage(index)` / `numStages()` / `stageName()` (programmatic; snapshot tool + other pages) — `AdvancedPage.h` — v4: KEEP (API)
- `[binding]` `selectedStrip()` / `selectedBus()` feed the window's "selected channel" (TUNE CHANNEL T, chain strip, Mix menu) — `MainView.cpp:2116, 1642` — v4: KEEP
- `[behaviour]` Entry points into Inspector: double-click mixer strip, Tracks header double-click/menu, channel sheet "Open in Inspector", check sheet, `inspectStrip`/`inspectBus` — `MainView::inspectStrip/inspectBus` — v4: Mixer double-click / chain strip click / menus
- `[mode]` Inspector in its own window (View menu id 618 `openInspectorWindow`, 900×620 min, 1240×820) — `MainView::openInspectorWindow` — v4: KEEP current — View menu → nearest menu (shell has no "New Window" for Inspector; Mixer has one)
- `[control]` `revealHistory()` scrolls trail to top and opens it (menu + snapshot tool) — `AdvancedPage::revealHistory` — v4: Inspector change trail

### Inspector — channel rail (left)

- `[control]` Rail: every channel grouped under its bus (section header: bus dot + "Drums" + count), then the bus row ("Drums bus") under its channels, "Output › Master" last — `AdvancedPage::rebuild`, `SectionHeader`, `Row` — v4: Inspector (the brief's shell already has a channel list; see next line)
- `[state]` Row shows dot (bus tint / mute colour / solo colour), name, struck through when muted, lit plane when selected; peak decays 1.6 dB/tick — `AdvancedPage::Row::set/paintButton` — v4: Inspector rail
- `[behaviour]` Bus row only when `engine.isBusUsed(bus)`; Master row only when the master bus is used — `AdvancedPage::rebuild` — v4: KEEP
- `[control]` Rail fold tab "Channels" (`DinePanelTab`, left) → `setRailShown` — `AdvancedPage::railTab` — v4: `[` folds the left panel
- `[mode]` `setRailAvailable(false)`: rail removed entirely when the window shows its own channel list beside every workspace — `AdvancedPage::setRailAvailable` — v4: KEEP (decide one list, not two)
- `[behaviour]` Auto-select strip 0 (or Master if no strips) after rebuild if selection is out of range — `AdvancedPage::rebuild` — v4: KEEP
- `[key]` `[` toggles rail on Inspector (cmd 611 → `togglePanel(true)`) — `MainView::togglePanel` — v4: `[` / `]` fold panels
- `[key]` `]` toggles trail on Inspector (cmd 612) — `MainView::togglePanel` — v4: `[` / `]`
- `[menu]` View cmd 615: on Inspector folds/unfolds both rail and trail together — `MainView.cpp:2735` — v4: nearest menu (View)

### Inspector — Simple view (SimplePanel)

- `[control]` Five macro knobs (0..100, centre 50 = "as TUNE left it"); labels/tooltips from `productDefinition(product).macros` — voice: WARMTH, CLARITY, SMOOTH, STEADY, CLEAN-UP; drum: PUNCH, BODY, ATTACK, TONE, BLEED (per product) — `SimplePanel::knobs`, `syncKnobs` — v4: Inspector Simple big knobs
- `[binding]` Knob move → `MacroMapping::apply(product, asTuned, values, roleFamily)` → `MixController::setStripChannel` — `SimplePanel::setMacro` — v4: Inspector Simple
- `[gesture]` Knob vertical drag 0.5 units/px; double-click → 50 — `SimplePanel::Knob::mouseDrag/mouseDoubleClick` — v4: Inspector Simple
- `[behaviour]` Re-seed: on channel change, on Simple open, after tune/hand edit, `asTuned = base.strips[s].channel`, knobs reset to `MacroMapping::defaults(product)` — `SimplePanel::reseed` — v4: Inspector Simple ("centred on DINE's tuned values")
- `[control]` Level knob −60..+12 dB (default 0.0 on double-click), writes `MixController::setStripFader` — `SimplePanel::level` — v4: Inspector Simple "level ring"
- `[control]` Speaking / Singing segment (only when `stripCanHaveEffects`) → `MixController::setStripEffects(strip, on)`; reads `stripEffectsOn` — `SimplePanel::speakingTab/singingTab` — v4: Inspector Simple speak/sing
- `[control]` TUNE CHANNEL (Simple) → `onTune` → `onTuneChannel` — `SimplePanel::tuneButton` — v4: Inspector Simple
- `[control]` PUT BACK / "PUT BACK 8:27 PM" (clock of newest TUNE record) → `setStripChannel(strip, asTuned)` + knobs to centre — `SimplePanel::putBack`, `putBackLabel` — v4: Inspector "Back to DINE" (restore tuned value)
- `[state]` "How it sounds" / "How they sound" title (person vs instrument by role family) — `SimplePanel::personal` — v4: Inspector Simple copy
- `[state]` "What DINE did" bullets: up to 5 unique `why` sentences from `plan->strips[s].tune.report.items` then `mixItems`; empty text "Nothing yet. TUNE CHANNEL listens…" — `SimplePanel::sentences` — v4: Inspector Simple
- `[state]` Footnote "The engineer's words — gate, compressor, de-esser — are in Advanced." — `SimplePanel::paint` — v4: Inspector Simple copy
- `[behaviour]` Card height sized to content (`wantedHeight`) — `SimplePanel::wantedHeight` — v4: KEEP
- GAP (brief) gain-health card in Simple — today gain advice is only in the trail (see below) — v4: Inspector Simple → candidate move, binding exists (`getInputAdvice`)

### Inspector — trail ("What DINE did", right)

- `[control]` Trail fold tab "What DINE did" (right `DinePanelTab`) → `setTrailShown` — `AdvancedPage::trailTab` — v4: `]`
- `[state]` Gain-staging card above records when `getInputAdvice(strip).needsAttention()`: headline (crit for Clipping/Faint, warn for Low/Hot/Digital, ink4 NotHeard), "−12.3 dBFS in" / "no signal", "DINE +6.0" digital gain, detail text — `Trail::setGain/paintGain` — v4: Inspector gain-health card
- `[state]` Records newest first: when ("14:02" or "12 Oct 14:02"), what ("TUNE …" accent / "Edited"), headline ("Set by TUNE …", "Put back…", "Mix Buddy…", "Hand-edited"), summary ("3 settings · level −4.0 dB · gain +2.0 dB · pan L20 · 2 sends") — `AdvancedPage::historyViews`, `Trail::List::paint` — v4: Inspector change trail per channel
- `[binding]` Per-change lines "High-pass 80 Hz to 100 Hz" (max 4, "and N more") via `diffParameters`, `findParameterSpec`, `forEachDspParameter` — `AdvancedPage::historyViews` (built; `lines` currently not painted by `Trail::List`) — v4: Inspector change trail (expand row)
- `[control]` PUT BACK chip on every record → `MixController::restoreStripTune(strip, n-1-record)` — `Trail::onRestore`, `Trail::List::mouseUp` — v4: Inspector trail "put back"
- `[state]` Empty-trail copy: no plan → "Nothing yet. This channel runs on the profile's baseline…"; plan → "Nothing on this channel yet. Every tune and every hand edit lands here…" — `AdvancedPage::showSelection` — v4: Inspector trail copy
- `[behaviour]` History rebuilt only when record count / newest `whenMs` / strip changes — `AdvancedPage::refresh` — v4: KEEP (perf)
- `[binding]` Source: `MixController::getStripHistory(strip)` → `StripTuneRecord{what, whenMs, before, after}` (persisted with session's mix history) — `MixController.h:405` — v4: Inspector trail + Mix history sheet

### Inspector — signal path (SignalPath chips)

- `[control]` One chip per stage in audio order: lamp + sentence-case name; selected = lit plane; hover plane — `SignalPath::paint`, `chipName` — v4: Inspector Advanced stage list (with on/off, value, N/A)
- `[state]` Lamp: filled accent = DINE's; filled `Dine::monitor` = hand-edited since last TUNE; empty ring = out of chain — `SignalPath::paint`, `StageView::edited/on` — v4: Inspector stage list state
- `[gesture]` Click chip → `ChainEditor::selectStage` (a chip never toggles) — `SignalPath::mouseUp` — v4: Inspector stage list
- `[menu]` Right-click chip (switchable stages) → "Switch it off / Switch it on" → `ChainEditor::toggleStage` — `SignalPath::showMenu` — v4: Context menu on stage row
- `[gesture]` Horizontal wheel/trackpad scroll of chip row; edge fades; auto-scroll keeps selected chip visible — `SignalPath::mouseWheelMove`, `refresh` — v4: KEEP (stage list may be vertical in v4)
- `[state]` Hover tooltip = stage's `why` (TUNE's sentence) or "X is out of the chain. …" — `SignalPath::mouseMove` — v4: Inspector stage row tooltip
- `[binding]` Chip labels/values from `chainStages(p, isMaster, stereo, hasSample)` — same words as the Chain strip (INPUT, FILTERS, GATE, SAMPLE, EQ, DE-ESS, COMP, TRANSIENT, TONE, SAT, WIDTH, LIMIT, OUT) + SENDS — `ChainStrip.h:chainStages`, `ChainEditor::updateViews` — v4: Inspector stage list + Chain strip

### Inspector — stage card (ChainEditor, common to every stage)

- `[control]` Stage title (17 pt) + macro word (CLEAN-UP Gate, SMOOTH De-esser, STEADY Comp, "WARMTH and CLARITY" Tone EQ, WARMTH Sat, LOUD Limiter) — `ChainEditor::paint`, `macroWordFor` — v4: Inspector stage editor
- `[control]` Off | On segment (switchable stages only; disabled under BYPASS) → `StageSpec::setOn` via `commit` — `ChainEditor::offButton/onButton` — v4: Inspector stage on/off
- `[state]` Provenance line with lamp ("who set this"): "BYPASS is on – nothing in the chain is running." / "Hand-edited" / "Left out of the chain" / "Set by TUNE MIX at 8:27 PM" / "The profile's baseline for this source" — `ChainEditor::refresh`, `lastTuneClock` — v4: Inspector "who set this" line
- `[control]` PUT BACK (per stage; visible only when edited and a plan exists) → `revertStage()`: fields, on/off, bands (or sends) back to `plan->proposed` — `ChainEditor::revertButton`, `revertStage` — v4: Inspector per-stage "Back to DINE"
- `[behaviour]` "Edited" detection per stage vs `plan->proposed` (fields 1e-4, bands freq 0.5 Hz / gain 0.05 dB / Q 0.005, sends 0.05 dB) — `ChainEditor::stageEdited` — v4: KEEP
- `[state]` Foot sentence: TUNE MIX's `why` for any change whose paramId starts with the stage's id prefixes (`s.ids`), else the stage's plain sentence; BYPASS text overrides — `ChainEditor::updateViews whyFor`, `paint` — v4: Inspector stage editor copy
- `[binding]` Every edit = read whole `ChannelParameters` → edit → `MixController::setStripChannel` / `setBusChannel`; refused while `isBypassed()` — `ChainEditor::read/write/commit` — v4: KEEP (UI binds to existing path; goes to Mix history → ⌘Z)
- `[behaviour]` Bus/Master chains editable too (`showBus`): no Sample, no Sends; Limiter only on Master — `ChainEditor::build` — v4: Inspector (bus selection)
- `[behaviour]` Width stage only on stereo strips; Sample only where `hasSampleStage(role)` (kick, snare, tom, hi-hat); Sends only if any `graph.fxUsed[f]` — `ChainEditor::build`, `chainSpecs` — v4: Inspector "N/A states"
- `[behaviour]` Knob default value (double-click) = `ChannelParameters{}` default for that field; knob sweep skewed around `Field::mid` — `ChainEditor::Knob` — v4: KEEP
- `[behaviour]` Controls disabled when stage off or BYPASS; layout wraps knobs, then extras row (popups/buttons), then choice rows; well scrolls below `kGraphMinH` — `ChainEditor::resized` — v4: KEEP
- `[behaviour]` Two-option choices = segment track; ≥3 options = popup menu — `ChainEditor::ChoiceGroup` — v4: segmented controls / menus
- `[behaviour]` Graph history 240 ticks (~8 s at 30 Hz) of GR, level and sample-fire marks; reset on stage change — `ChainEditor::Graph::setStage/update` — v4: KEEP (move to shared clock)

### Inspector — Input stage

- `[control]` Trim knob −24..+24 dB, step 0.1 — `inputTrim` — `chainSpecs StageId::Input` — v4: Inspector · Input
- `[control]` Polarity segment Normal / Flipped — `polarity` — `chainSpecs` — v4: Inspector · Input
- `[control]` Gain knob −24..+24 dB (channels only): the strip's digital preamp, the one TUNE sets → `MixController::setStripInputGain` — `ChainEditor::StripKnob` (2026-10-06) — v4: Inspector · Input
- `[control]` Delay knob 0..100 ms, 0 = "off" (channels only): alignment of a far microphone → `MixController::setStripDelay` — `ChainEditor::StripKnob` (2026-10-06) — v4: Inspector · Input
- `[state]` Drawing: ladders "Arriving" (input meter), "After trim", "Into the fader" (output meter) — `Graph::paintMeters` — v4: Inspector · Input
- `[state]` Not switchable (always in chain) — `chainSpecs` — v4: Inspector · Input

### Inspector — Filters stage

- `[control]` High-pass knob 20..1000 Hz (mid 120) — `hpfFreq` — v4: Inspector · Filters
- `[control]` Low-pass knob 1k..20k Hz (mid 6000) — `lpfFreq` — v4: Inspector · Filters
- `[control]` High-pass Off/On — `hpfOn` — v4: Inspector · Filters
- `[control]` Slope 12 / 24 dB/oct (HP) — `hpfSlope` — v4: Inspector · Filters
- `[control]` Low-pass Off/On — `lpfOn` — v4: Inspector · Filters
- `[control]` Slope 12 / 24 dB/oct (LP) — `lpfSlope` — v4: Inspector · Filters
- `[behaviour]` Stage Off/On: on = HPF on; off = HPF and LPF off — `StageSpec::setOn (Filters)` — v4: KEEP
- `[gesture]` Drag "H"/"L" nodes on the curve (freq only, clamped) — `Graph::nodes/moveNode` — v4: Inspector · Filters curve
- `[state]` Curve = `EqResponse::chainMagnitudeDb` of the stage isolated — `Graph::paintEq`, `isolate` — v4: Inspector · Filters

### Inspector — Gate stage (CLEAN-UP)

- `[control]` Off/On — `gateOn` — v4: Inspector · Gate
- `[control]` Threshold −80..0 dB — `gateThreshold` — v4: Inspector · Gate
- `[control]` Range 0..80 dB — `gateRange` — v4: Inspector · Gate
- `[control]` Attack 0.01..50 ms — `gateAttack` — v4: Inspector · Gate
- `[control]` Hold 0..500 ms — `gateHold` — v4: Inspector · Gate
- `[control]` Release 5..1000 ms — `gateRelease` — v4: Inspector · Gate
- `[control]` Hysteresis 0..12 dB — `gateHysteresis` — v4: Inspector · Gate
- `[control]` Ratio 1..20:1 (expander) — `gateRatio` — v4: Inspector · Gate
- `[control]` Detector HP 0..500 Hz — `gateScHpf` — v4: Inspector · Gate
- `[state]` Drawing: level bars + threshold line; GR from `proc->getGate().getGainReductionDb()` — `Graph::paintThreshold`, `updateViews reduction` — v4: Inspector · Gate

### Inspector — Sample stage (drums; Advanced)

- `[control]` Off/On — `replaceOn` (TUNE never switches it on) — `chainSpecs StageId::Sample` — v4: Inspector · Sample stage
- `[control]` Blend 0..100 % — `replaceBlend` — v4: Inspector · Sample "Blend"
- `[control]` Sensitivity −80..0 dB (threshold) — `replaceThreshold` — v4: Inspector · Sample "Sensitivity"
- `[control]` Level −60..+12 dB — `replaceGain` — v4: Inspector · Sample "Level"
- `[control]` Pitch −5..+5 st ("as recorded" at 0) — `replaceRate` — v4: Inspector · Sample "Pitch"
- `[control]` Align 0..5 ms (sample only, later) — `replaceOffset` — v4: Inspector · Sample "Align"
- `[control]` Rise 0..40 dB — `replaceRise` — v4: Inspector · Sample "Rise"
- `[control]` Mask 1..500 ms — `replaceMask` — v4: Inspector · Sample "Mask"
- `[control]` Listen above 20..2000 Hz — `replaceDetHpf` — v4: Inspector · Sample listen band lo
- `[control]` Listen below 100..20000 Hz — `replaceDetLpf` — v4: Inspector · Sample listen band hi
- `[control]` Sound popup (family's loaded sounds by slot, or "No sounds loaded") — `replaceSound` — v4: Inspector · Sample SOUND list (grouping is a GAP, see notes)
- `[control]` Feel: "Follows the drummer" / "Steady" — `replaceSteady` — v4: Inspector · Sample "Feel"
- `[control]` Tuning: "As recorded" / "Follows the drum" — `replaceFollow` (uses `replaceDrumHz`, written by TUNE, not editable) — v4: Inspector · Sample "Tuning"
- `[control]` Polarity Normal / Flipped — `replacePolarity` — v4: Inspector · Sample "Polarity"
- `[state]` `replaceDrumHz` shown only in summary "pitched to the drum (82 Hz)"; no control — `chainSpecs summary` — v4: Inspector · Sample (read-only readout)
- `[control]` "Drum kit: Church" popup (only when `drumKitName()` non-empty) → `onDrumKit` → `MainView::drumKitMenu` (built-in kits + disabled "Custom") → `applyDrumKit` — `ChainEditor::buildControls`, `updateKitPopup` — v4: Inspector · Sample kit picker
- `[control]` HEAR IT → `MixController::auditionSample(strip)` (plays once to the solo/monitor listen, never the broadcast) — `ChainEditor::buildControls` — v4: Inspector · Sample HEAR IT
- `[control]` "Import a sound..." → `onImportSample(roleFamily(role))` → `SampleLibrary::importSound` (decode first, copy to `<session>/Samples/<kick|snare|toms>/` or `~/Music/DINE/Samples`) — `ChainEditor::buildControls`, `MainView.cpp:1358` — v4: Inspector · Sample "Import a sound"
- `[state]` Drawing: input level bars, threshold line labelled "−30 dB" (crit), orange vertical marks on every tick where `getHitCount()` changed — `Graph::paintThreshold` — v4: Inspector · Sample visualiser (much richer in v4, see GAPS)
- `[state]` Foot sentence prefix "N hits played, M held back as another drum's bleed." from `getSampler().getHitCount()/getVetoCount()` — `ChainEditor::updateViews` — v4: Inspector · Sample counters
- `[state]` Chip bar/GR shows 6 dB while `getSampler().isPlaying()` — `updateViews reduction` — v4: stage list / Chain strip lamp
- `[state]` Summary "40 % · Tight kick · pitched to the drum (82 Hz)" / "off" — `chainSpecs summary` — v4: stage list value
- `[binding]` Sound names: `controller.getSampleBanks()->bank(family, i)->name` for i < `SampleBankTable::kSounds` (24) — `ChainEditor::build` — v4: Sample SOUND list
- `[binding]` `SampleLibrary::sounds(family)` → `Sound{name, user, path, inSession}` (needed for Built in / Your sounds / This session grouping — not used by the UI today) — `SampleLibrary.h` — v4: Sample SOUND list groups
- `[binding]` `SampleLibrary::whatWasLeftOut()` / `familyFull()` (overflow sentence) — `SampleLibrary.h` — v4: KEEP — not surfaced in UI today (candidate toast/inline note)
- `[binding]` `builtInDrumKits()`, `findDrumKit`, `drumKitChoices`, `currentDrumKit` (never stored; "Custom" when strips differ; hi-hat excluded) — `DrumKits.h` — v4: Sample kit picker
- `[state]` Persisted: `ChannelParameters` replace* fields with the strip; chosen sound stored by name as `SampleChoice` (`SessionState.h`), resolved via `SampleLibrary::slotFor` on open — `SampleLibrary.h`, `DrumKits.h` — v4: KEEP (no format change)
- `[behaviour]` LIVE SAFE blocks "changing the drum kit" (`liveSafeBlocks`); other Sample knobs are ordinary chain edits (policy in MixController) — `MainView::drumKitMenu` — v4: KEEP
- `[behaviour]` Sample stage appears on Hi-Hat strips (`sampleReplacementAppropriate` includes HiHat; off by default), but library/import/kits are kick/snare/toms only — hat plays synthesised placeholders — `SampleBank.h`, `SampleLibrary.cpp:familyFolderName` — v4: Inspector · Sample (brief: "Hi-Hat shown but off by default" — matches)

### Inspector — Corrective EQ stage

- `[control]` Off/On — `corrEqOn` — v4: Inspector · EQ
- `[control]` Band 1..3 gain knobs −18..+18 dB (moving a gain enables that band) — `corrEq{n}Gain`, `corrEq{n}On` — v4: Inspector · EQ
- `[control]` Selected band: Frequency 20..20k, Q 0.1..10, Shape popup (`kFilterTypeNames`), Band n Off/On — `corrEq{n}Freq/Q/Type/On` — v4: Inspector · EQ
- `[gesture]` Drag numbered nodes on the curve (freq + gain); picking a node selects that band — `Graph::mouseDown/moveNode` — v4: Inspector · EQ curve
- `[state]` "Drag a node" caption; disabled band nodes dim — `Graph::paintEq` — v4: Inspector · EQ

### Inspector — De-esser stage (SMOOTH)

- `[control]` Off/On — `deEssOn` — v4: Inspector · De-esser
- `[control]` Frequency 2k..12k Hz — `deEssFreq` — v4: Inspector · De-esser
- `[control]` Threshold −60..0 dB — `deEssThreshold` — v4: Inspector · De-esser
- `[control]` Range 0..24 dB — `deEssRange` — v4: Inspector · De-esser
- `[state]` Drawing: "Gain reduction" trace with −6/−12 scale from `getDeEsser().getGainReductionDb()` — `Graph::paintReduction` — v4: Inspector · De-esser

### Inspector — Compressor stage (STEADY)

- `[control]` Off/On — `compOn` — v4: Inspector · Compressor
- `[control]` Threshold −60..0, Ratio 1..20, Attack 0.1..200 ms, Release 5..2000 ms, Knee 0..24 dB, Makeup −12..+24, Blend 0..100 %, Detector HP 0..500 Hz — `compThreshold/compRatio/compAttack/compRelease/compKnee/compMakeup/compMix/compScHpf` — v4: Inspector · Compressor
- `[state]` Drawing: In/out transfer from `Compressor::computeGain` (+makeup, blend) with live input dot, plus GR trace — `Graph::paintCompressor` — v4: Inspector · Compressor

### Inspector — Transient stage

- `[control]` Off/On — `transOn` — v4: Inspector · Transient
- `[control]` Attack −100..+100 — `transAttack` — v4: Inspector · Transient
- `[control]` Sustain −100..+100 — `transSustain` — v4: Inspector · Transient
- `[state]` Drawing: 4 synthetic hits, "Before" vs "After" envelopes — `Graph::paintEnvelope` — v4: Inspector · Transient

### Inspector — Tone EQ stage (WARMTH and CLARITY)

- `[control]` Off/On — `toneEqOn` — v4: Inspector · Tone
- `[control]` Band 1..4 gain knobs; selected band Frequency / Q / Shape / Band Off-On — `toneEq{n}Gain/Freq/Q/Type/On` — v4: Inspector · Tone
- `[gesture]` Node drag on curve — `Graph::moveNode` — v4: Inspector · Tone

### Inspector — Saturation stage (WARMTH)

- `[control]` Off/On — `satOn` — v4: Inspector · Saturation
- `[control]` Drive 0..100 % — `satDrive` — v4: Inspector · Saturation
- `[control]` Blend 0..100 % — `satMix` — v4: Inspector · Saturation
- `[state]` Drawing: tanh transfer curve vs straight line, live input dot — `Graph::paintCurve`, `throughStage` — v4: Inspector · Saturation

### Inspector — Width stage (stereo strips only)

- `[control]` Off/On — `widthOn` — v4: Inspector · Width
- `[control]` Width 0..200 % — `widthAmount` — v4: Inspector · Width
- `[control]` Mono below 0..500 Hz — `widthMonoBelow` — v4: Inspector · Width
- `[state]` Drawing: circle + ellipse, caption "Width 120% · mono below 120 Hz" — `Graph::paintStereo` — v4: Inspector · Width

### Inspector — Limiter stage (Master only; LOUD)

- `[control]` Off/On — `limiterOn` — v4: Inspector · Limiter
- `[control]` Ceiling −12..0 dB(TP) — `limiterCeiling` — v4: Inspector · Limiter
- `[control]` Release 10..1000 ms — `limiterRelease` — v4: Inspector · Limiter
- `[state]` Drawing: output level bars + ceiling line "Ceiling −1.0 dBTP"; GR from `getLimiter()`; sentence "looks 1.5 ms ahead… latency always reported" — `Graph::paintCeiling` — v4: Inspector · Limiter

### Inspector — Output stage

- `[control]` Trim −24..+24 dB — `outputTrim` — v4: Inspector · Output
- `[state]` Drawing: ladders "Into the fader", "Out" — `Graph::paintMeters` — v4: Inspector · Output

### Inspector — Sends stage (strips; only if the session uses an FX return)

- `[control]` One send knob per used return: Plate, Delay, Hall, Snare plate, Drum room (−60 = "off" detent stored as `kSilenceDb`, up to +6 dB) → `MixController::setStripSend(strip, slot, db)` — `ChainEditor::SendKnob` — v4: Inspector · Sends
- `[state]` Drawing: labelled ladder bar per send; "This session has no effects set up yet." — `Graph::paintSends` — v4: Inspector · Sends
- `[state]` Effects off on the mic (Speaking): chip value "off", lamp out, sentence explains levels are kept — `ChainEditor::updateViews` — v4: Inspector · Sends
- `[control]` PUT BACK for sends → `setStripSend` to `plan->proposed.strips[s].sendDb` — `ChainEditor::revertStage` — v4: Inspector · Sends
- `[control]` An effect return's sound, by hand (2026-10-06): double-click a return strip on the Mixer, or its menu "Set how it sounds…" → `EffectSheet`. Reverb: Decay, Pre-delay. Delay: Note popup (eight notes + Free time), Time (free only), Repeats, Tempo (40–240 bpm) and Tap (also the T key) → `MixController::setFxSlotCharacter` / `setTempo` — `MainView::showEffect`, openSheetName "effect" — v4: no slot in the mockup; a sheet like the others

### Inspector — Loudness stage

- GAP (brief) "Loudness" stage in the Inspector stage list — no stage exists in `ChainEditor::chainSpecs` or `chainStages` (no LOUDNESS label). Loudness lives in the master's meter / delivery target (`MixController::setDelivery`, `previewLoudnessMove`, `raiseLoudnessToTarget`) on other pages — v4: Inspector · Loudness → GAP: needs a read-only stage view bound to existing loudness readouts; no DSP change

### Tracks — page shell and toolbar (TracksPage)

- `[mode]` Tracks page: tool row (46 pt) / ruler with loop strip + marker lane (44 pt) / headers (left panel) + lanes / optional foot chain strip — `TracksPage.h` — v4: Tracks (⌘5)
- `[binding]` `refresh()` 30 Hz: meters (one `consumeMaxPeakDb` per track per tick, 2 dB/tick fall), playhead-only repaint, follow-scroll, mix-state hash repaint, per-meter dirty rects — `TracksPage::refresh` — v4: KEEP (perf model matches brief)
- `[control]` Row height S / M / L segment (40 / 58 / 82 px, applied to every track, persisted via track heights) — `TracksPage::rowTabs`, `setRowHeight` — v4: Tracks (keep)
- `[control]` Snap segment toggle (toast on change) — `snapButton`, `setSnap` — v4: KEEP current — Tracks tool row
- `[control]` Follow segment toggle (keep playhead on screen) — `followButton`, `setFollow` — v4: KEEP current — Tracks tool row
- `[control]` "Split at playhead" (⌘E) — `splitButton` → `splitAtPlayhead` — v4: Tracks + shortcut ⌘E
- `[control]` "Add marker" (M) — `markerButton` → `addMarkerAtPlayhead` — v4: Tracks + shortcut M
- `[control]` "All to record" toggle (tracks all armed ↔ none; follows session state) — `recordAllButton`, `setAllToRecord` — v4: Tracks
- `[control]` "All to input" toggle (every track Monitoring=Input ↔ all Auto) — `monitorAllButton`, `setAllToInput` — v4: Tracks
- `[control]` "Loop" toggle (on/off without moving it; tooltip shows range) — `loopButton`, `toggleLoop` — v4: Tracks + transport Loop (L)
- `[control]` Zoom −, +, Fit buttons; zoom % readout (pixelsPerSecond/18) — `zoomOutButton/zoomInButton/zoomFitButton`, `paintToolbar` — v4: Tracks + ⌘− / ⌘= / ⌘0
- `[state]` Selection readout "Kick selected" / "LIVE SAFE · the timeline is locked" (warn) / "Nothing selected" — `paintToolbar` — v4: Tracks tool row
- `[control]` Foot chain strip (page-local; hidden in main window, the window owns the chain strip) — `chainStrip`, `setFootShown`, `updateChainStrip` — v4: shell Chain strip
- `[state]` Foot note: clock "1:23.4 / 45:00.0" + "LIVE SAFE" — `updateChainStrip` — v4: Chain strip / status foot
- `[callback]` Chain strip click → `onOpenStrip(strip)` (Inspector) — `chainStrip.onOpen` — v4: Chain strip click opens Inspector
- `[state]` Empty states: "Nothing recorded yet. Press the red R…", "No tracks yet", "Click a clip to read its chain here…" — `TracksPage::paint`, `updateChainStrip` — v4: Tracks copy
- `[behaviour]` LIVE SAFE: `locked()` toasts "LIVE SAFE is on: editing the timeline is locked. Turn it off on the Live page." and refuses clip/marker/loop/arm/reorder/add/import — `TracksPage::locked` — v4: KEEP (toast)
- `[behaviour]` Pages are drawn/hit-tested by hand (no per-track components) — `TracksPage.h` comment — v4: KEEP (virtualised by construction)

### Tracks — track header (per track)

- `[state]` Dot colour: armed=crit, monitoring=monitor, muted=warn, else group tint; 2-digit number; name; link glyph if linked — `TracksPage::paintHeader` — v4: Tracks header
- `[state]` Gain chip from last listen: CLIPPING / FAINT / LOW / HOT / DIGITAL / NOT HEARD (via `getInputAdvice(stripOf(track))`, rebuilt once per `getTuneCount()`) — `gainChipFor`, `refresh` — v4: Tracks header (same as Mixer gain-health chip)
- `[state]` Note line (tall rows): DIGITAL advice / "Balance L20" (pan) / "Named differently from its clips – right-click to fix", each with a short form — `paintHeader` — v4: Tracks header
- `[state]` Name-mismatch warning icon + warn-coloured name — `nameMismatch`, `paintHeader` — v4: Tracks header
- `[control]` TUNE chip on header (hidden when narrow; dim under LIVE SAFE) → `onTuneStrip(strip)` → `MainView::tuneChannel` — `tuneCell`, `mouseDown` — v4: Tracks header
- `[control]` R key: toggle `project.tracks[t].armed` (LIVE SAFE-locked) → `daw().refresh()`, `touchSession()` — `keyCell(…,0)`, `mouseDown` — v4: Tracks record arm (R, red)
- `[control]` A/I key: cycle `MonitorMode` Auto → Input → Off (label A or I; "—" tooltip) — `cycleMonitor` — v4: Tracks (A key; Mixer menu has Input/Auto/Off)
- `[control]` M key → `MixController::setStripMute` — `keyCell(…,2)` — v4: Tracks header M
- `[control]` S key → `MixController::setStripSolo` — `keyCell(…,3)` — v4: Tracks header S
- `[behaviour]` Keys 2×2 (R A / M S) on tall rows, 1×4 on compact rows (<50 px) — `keyCell`, `compactHeader` — v4: KEEP
- `[control]` Quick fader (−60..+12, skew centre −12; grab-relative, never jumps; Shift = ¼ speed; ⌘-drag = move alone, ignoring fader link) → `setStripFader(strip, db, linked)` ; refused under BYPASS (toast) — `faderCell`, `dragFader` — v4: Tracks header (KEEP)
- `[gesture]` Double-click fader → 0.0 dB — `mouseDoubleClick` — v4: KEEP
- `[state]` dB readout beside fader (tall rows) — `paintHeader` — v4: KEEP
- `[state]` Header meter (8 px, right edge), dims when muted — `meterCell` — v4: Tracks header
- `[gesture]` Click header = select track (always, even when the click lands on a key/fader/grip); updates chain strip — `mouseDown` — v4: Tracks
- `[gesture]` Double-click header (not on key/fader/grip) → Inspector for that strip — `mouseDoubleClick` — v4: Tracks → Inspector
- `[gesture]` Drag header vertically (>5 px) → reorder channels (`moveTrack`), auto-scroll at edges, lifted row dimmed, accent drop line; LIVE SAFE-locked — `Drag::TrackOrder`, `dropSlotAtY` — v4: Tracks move up/down (all workspaces follow)
- `[gesture]` Drag bottom 5 px of a header → per-track height 38..260 px (persisted in project) — `Drag::TrackHeight` — v4: Tracks track heights
- `[gesture]` Drag the header/timeline divider → panel width 340..640 (≤ window − 220), persisted via `onPanelWidthChanged` → `services.setTrackPanelWidth` — `Drag::PanelWidth`, `setPanelWidth` — v4: KEEP current — Tracks
- `[behaviour]` Tooltips for R, A/I, M, S, TUNE, fader (linked text + ⌘-drag hint), reorder hint — `TracksPage::getTooltip` — v4: KEEP (plain-word tooltips)
- `[behaviour]` No-strip track (input without source): M/S/fader/TUNE toast "…is not on the console yet: right-click the header and say what it is." — `mouseDown noStrip` — v4: KEEP
- `[binding]` `stripOf(track)` maps track→console strip skipping inputs with no strip (`inputHasStrip`) — `TracksPage::stripOf` — v4: KEEP (easy-to-lose bug fix)
- `[callback]` `selectTrack(track)` from the window's channel rail — `TracksPage::selectTrack` — v4: KEEP
- `[binding]` `selectedTrack()` is what Mix menu TUNE CHANNEL (T) tunes on Tracks — `MainView.cpp:2115` — v4: KEEP

### Tracks — header right-click menu

- `[menu]` Title "05  Kick" — `headerMenu` — v4: Context menu (Tracks)
- `[menu]` Rename… (AlertWindow "What is on this track?") → `MixController::setInputName` + `daw().setSession` — `renameTrack`, `setTrackName` — v4: Tracks rename
- `[menu]` Use the clip's name (Kick take 2) — enabled only on mismatch — `setTrackName(track, clip)` — v4: Tracks "match tracks to clips"
- `[menu]` Match every track to its clips (N) → `matchNamesToClips` (toast with count) — v4: Tracks "match tracks to clips"
- `[menu]` Source ▸ role groups (`Dine::roleGroups`) → `setTrackSource`: `controller.setSession` + `daw().setSession` + `services.reconfigure()`; channel back to baseline; toast — v4: Tracks source menu
- `[menu]` This microphone is ▸ (voice channels only; `MixController::voiceJobs()`; disabled under LIVE SAFE) → `setInputRole(track, roleForJob(...))` + reconfigure — v4: Tracks source menu (also Mic sheet speak/sing)
- `[menu]` Icon ▸ From the source / `Dine::iconChoices()` → `MixController::setInputIcon` (label only, no rebuild) — `setTrackIcon` — v4: Tracks icon menu
- `[menu]` Move up / Move down → `moveTrack` — v4: Tracks move up/down
- `[menu]` Link fader with ▸ / "Linked faders · OH R, Room" ▸ (tick = member; "(linked elsewhere)") → `linkStrips({track, other})` / `unlinkStrip(other)` — v4: Tracks (Mixer menu has "link with next fader") — KEEP full submenu
- `[menu]` Unlink this fader → `unlinkStrip(stripOf(track))` — v4: Tracks context menu
- `[menu]` Fix the assignments… → `onOpenAssign` → Assign page — v4: Routing › Inputs
- `[menu]` TUNE CHANNEL → `onTuneStrip` — v4: Tracks context menu
- `[menu]` Open in the Inspector → `onOpenStrip` — v4: Tracks context menu
- `[menu]` Right-click below the last track → "New track" ▸ role groups (stereo marked) → `addTrack(role)` — `newTrackMenu`, `fillNewTrackMenu` — v4: Tracks context menu

### Tracks — timeline, ruler, playhead, loop, markers

- `[gesture]` Click/drag ruler tick area → move playhead (snapped); re-primed on release (`daw().locate`); refused while recording (toast "The playhead follows the recording…") — `Drag::Playhead` — v4: Tracks playhead
- `[state]` Playhead line through ruler+lanes, accent; crit while recording — `TracksPage::paint` — v4: Tracks
- `[gesture]` Drag empty loop strip (top 14 px of ruler) → mark new loop (turns on) — `Drag::LoopRange` — v4: Tracks loop
- `[gesture]` Drag loop end grips (±8 px) → resize — `Drag::LoopRange` — v4: Tracks loop
- `[gesture]` Click loop middle → toggle on/off — `Drag::LoopMove`, `mouseUp → toggleLoop` — v4: Tracks loop
- `[gesture]` Drag loop middle → marks a new loop from there; Option-drag middle → move loop keeping length/state — `Drag::LoopMove` — v4: KEEP (easy-to-lose)
- `[behaviour]` Loop shorter than 4 px → cleared ("Loop cleared."); toasts give loop range — `mouseUp` — v4: Toast
- `[state]` Loop bar "LOOP"/"LOOP OFF", wash + edges over lanes — `paintRuler`, `paint` — v4: Tracks
- `[binding]` Loop persisted through `daw().setLoop(on, start, end)` + `touchSession` + `onTimelineChanged` — `toggleLoop`, `mouseUp` — v4: KEEP
- `[gesture]` Click marker flag → locate playhead; drag flag → move marker (snapped, undoable "moving a marker") — `Drag::Marker` — v4: Tracks markers
- `[menu]` Right-click marker → Go to this marker / Rename… / Delete — `markerMenu` — v4: Context menu (Tracks)
- `[gesture]` Double-click empty marker lane → add "Marker N" at that point (snapped, undoable) — `mouseDoubleClick` — v4: KEEP
- `[behaviour]` Add marker refuses within 2 px of an existing one ("There is already a marker here."); markers auto-named "Marker N", kept sorted — `addMarkerAtPlayhead` — v4: KEEP
- `[state]` Marker lines through all lanes; hover highlight on flag — `paint`, `paintMarkers` — v4: Tracks
- `[state]` Ruler labels adapt (0.1 s … 1 h steps; "m:ss", "Nm", tenths) with quarter ticks; grid carried into lanes — `gridSeconds`, `paintRuler` — v4: Tracks
- `[behaviour]` Snap targets: 0, playhead, loop edges, markers, every clip edge, grid; 9 px threshold — `snapSample` — v4: KEEP
- `[state]` Scroll indicators (vertical + horizontal) — `paint` — v4: KEEP

### Tracks — clips

- `[gesture]` Click clip → select (outline + grips); click empty lane → select track — `mouseDown` — v4: Tracks
- `[gesture]` Drag clip → move (snaps either edge; undo "moving a clip") — `Drag::ClipMove` — v4: Tracks clip ops
- `[gesture]` Drag clip left/right 7 px edge → trim start/end (offset in file samples with rate ratio; undo "trimming a clip") — `Drag::ClipTrimStart/End` — v4: Tracks clip ops
- `[gesture]` Drag empty lane → horizontal scroll — `Drag::Scroll` — v4: KEEP
- `[behaviour]` Clips re-sorted by start after edit; selection cleared; `commit()` = `daw().refresh()` + `touchSession()` — `mouseUp`, `commit` — v4: KEEP
- `[behaviour]` LIVE SAFE: selecting allowed, moving not — `mouseDown` — v4: KEEP
- `[control]` Split at playhead (all clips on all tracks under the playhead; toast "Split N clips.") — `splitAtPlayhead` — v4: ⌘E
- `[control]` Delete selected clip (file stays on disk, toast) — `deleteSelection` — v4: Delete/Backspace on Tracks
- `[state]` Clip = group-colour plane, dark waveform (`juce::AudioThumbnail`, 512, cache 128; multichannel file draws its own `fileChannel`), name; dimmed when muted / soloed-out — `paintLane`, `thumbnailFor` — v4: Tracks
- `[state]` "Set to record" label on an armed empty lane — `paintLane` — v4: Tracks
- `[binding]` `primeThumbnails()` / `waveformsReady()` for the snapshot tool — `TracksPage.h` — v4: KEEP (API)
- `[behaviour]` Thumbnails dropped only when the session folder changes — `rebuild` — v4: KEEP (brief asks to release caches after 30 s hidden)
- `[gesture]` Drop audio files / folders from Finder: on a track (at drop time; extra files go down the tracks), on header (time 0), below last track (new tracks); highlight band + "New track" — `isInterestedInFileDrag`, `filesDropped`, `addAudioFiles` → `AppServices::importAudio` (MultitrackImport) — v4: Tracks (KEEP)
- `[behaviour]` Drop is undoable only when it added no tracks; refused while recording / LIVE SAFE — `addAudioFiles` — v4: KEEP

### Tracks — timeline undo, zoom, scroll

- `[behaviour]` Timeline undo/redo stack (40 entries; clips + markers only; epoch-scoped via `daw().getTimelineEpoch()`; refused while recording) with toasts "Undone: moving a clip." — `TracksPage::undo/redo/pushUndo/captureEdit/dropStaleEdits` — v4: ⌘Z / ⇧⌘Z
- `[binding]` MainView picks timeline vs mix undo by most-recent (`lastEditMs` vs `controller.undoMixAtMs`) on Tracks only; LIVE SAFE blocks timeline undo — `MainView::undoTarget` — v4: KEEP
- `[key]` ⌘= / ⌘+ zoom in (cmd 605), ⌘− zoom out (606), ⌘0 fit (607) — `commandForKey` — v4: same
- `[gesture]` ⌘- or ⌥-scroll → zoom about pointer (×1.15); pinch → `zoomAround` — `mouseWheelMove`, `mouseMagnify` — v4: KEEP
- `[gesture]` Scroll wheel → vertical/horizontal scroll — `mouseWheelMove` — v4: KEEP
- `[state]` Zoom range 0.2..800 px/s; default 18 px/s; not persisted. Snap/Follow not persisted (default on) — `TracksPage.h` members — v4: KEEP

### Tracks — app menus and shortcuts (MainView, acting on this page)

- `[key]` ⌘E split at playhead (cmd 201; LIVE SAFE blocks) — `commandForKey` — v4: ⌘E
- `[key]` Delete / Backspace delete clip (Tracks only, cmd 202) — `commandForKey` — v4: KEEP (not in brief list)
- `[key]` M add marker (cmd 203; switches to Tracks) — `commandForKey` — v4: M
- `[key]` T TUNE CHANNEL on selected channel (cmd 404) — v4: T
- `[key]` ⌘1..5 today = Tracks, Mixer, Tune, Live, Inspector (cmd 600–604) — `commandForKey kTabCommand` — v4: CONFLICT — brief remaps to Mixer ⌘1, Tune ⌘2, Inspector ⌘3, Live ⌘4, Tracks ⌘5 (update `app/Tests/ReachabilityTests.cpp`)
- `[menu]` Edit: Split at Playhead / Delete Clip / Add Marker at Playhead M — `MainView.cpp:923` — v4: nearest menu (session menu or Tracks context)
- `[menu]` Track ▸ New Track ▸ (ids 3000+; disabled under LIVE SAFE) → `addTrack` — `MainView.cpp:929` — v4: nearest menu / Tracks context
- `[menu]` Track ▸ Set Every Track to Record / Set No Tracks to Record (300/301) — v4: nearest menu (Tracks "All to record" covers it)
- `[menu]` Track ▸ Move Track Up / Down (305/306, needs a selected track) — v4: Tracks move up/down
- `[menu]` Track ▸ Monitoring: Input / Auto / Off on every track (302–304) — v4: nearest menu (Mixer right-click has per-channel Input/Auto/Off)
- `[menu]` View ▸ Zoom In / Out / Fit (605–607) — v4: ⌘= / ⌘− / ⌘0
- `[menu]` View ▸ Open Inspector in its own window (618) — v4: nearest menu (View)
- `[callback]` `onToast` → shell toast; `onTimelineChanged` → `updateChrome`; `onOpenAssign` → Assign page; `onSessionChanged` → rebuild Inspector & other pages; `onPanelWidthChanged` → persist width — `MainView.cpp:1318–1331` — v4: KEEP

### Tracks — add track

- `[control]` `addTrack(role)`: new input on next free device channel(s), stereo by `defaultsToStereo`, unique name ("Kick 2"), cap `kMaxStrips` / `kMaxInputs` with toasts; reconfigure; select new track — `TracksPage::addTrack` — v4: Tracks (context menu / menu)

### Persisted state touched by these pages

- `[state]` Track: `armed`, `monitor` (MonitorMode), `height`, `clips` (start, offset, length, name, fileChannel, fileSampleRate) — Project via `services.daw()` + `touchSession()` — v4: KEEP (no format change)
- `[state]` Project `markers` (name, position), `loopStart/loopEnd/loopEnabled`, `liveSafe` — v4: KEEP
- `[state]` Session inputs: `name`, `role`, `icon`, order (reorder), `inputA/inputB` — `controller.setSession` / `setInputName` / `setInputIcon` / `setInputRole` — v4: KEEP
- `[state]` Track panel width — `services.setTrackPanelWidth` / `trackPanelWidth()` — v4: KEEP
- `[state]` Strip `channel` (all ChannelParameters incl. replace*), `faderDb`, `mute`, `solo`, `pan`, `sendDb[]`, `effectsOff`, `linkGroup`; strip history records — MixController kept mix — v4: KEEP
- `[state]` Sample choice by name (`SampleChoice`), session `Samples/` folder copies — v4: KEEP
- `[state]` NOT persisted: Simple/Advanced view, rail/trail shown, selected stage/band, zoom, snap, follow, scroll — v4: KEEP or decide (brief suggests pages keep state)

---

## 4. Set-up pages and sheets

Scope: `app/ui/SetupPages.*`, `app/ui/RoutingPage.*`, `app/ui/OutputsSheet.*`, `CheckSheet`, `HistorySheet`, `ExportSheet`, `ChannelTuneSheet`, `ReferenceSheet`, `ChatSheet`, `ThemeSheet`, `BroadcastReadinessSheet`, plus the `MainView.cpp` wiring that opens and answers them (callbacks, menu IDs, Esc, the tick). v4 homes follow `docs/design/v4/HANDOFF.md`. That brief still says "DLIVE/v3"; the app is DINE.

Tags: `[control|menu|gesture|key|callback|binding|state|behaviour|rule]`. "v4: KEEP current" means the brief gives it no home.

---

### Shared set-up shape (SetupPages)

- `[behaviour]` SetupLayout bands (head 34+48, toolbar 30, main, rail `Metric::setupRail` only when width > rail+420, footer button+padY) measured once so all set-up pages line up — `SetupPages.cpp:SetupLayout::of` — v4: KEEP (internal); Routing/Sessions/Purpose pages
- `[behaviour]` Title 22/600 + one sentence 13 ink3 (sentence width capped 760) — `SetupPages.cpp:drawSetupHead` — v4: every set-up page head
- `[behaviour]` Footer note right-aligned left of the buttons — `SetupPages.cpp:drawSetupFooter` — v4: page footers
- `[rule]` Role → short desk name table (Kick, Snare btm, OH L, Lead vox, BV, Pastor, Lapel, Timbs, Pad...) — `SetupPages.cpp:shortRoleName` — v4: Inputs "Name from what it is", typeahead alias seed
- `[rule]` Role groups and friendly role names come from AppTheme so Inputs and the TRACKS header menu match — `Dine::roleGroups` / `Dine::friendlyRoleName` (used in SetupPages.cpp) — v4: Inputs "What it is ▾" / typeahead source list
- `[rule]` Gain-verdict colours: Clipping/Faint=crit, Low/Hot/Digital=warn, NotHeard=ink4, else ok (same words and colours as console + Inspector) — `SetupPages.cpp:gainVerdictColour` — v4: Inputs level verdict + Mixer gain-health chip
- `[rule]` Kits for "Fill in order": Drum kit (9), Overheads and room, Band, Keys in stereo, Singers, Speaking mics, Crowd and room, Horns, Percussion, each with its bus — `SetupPages.cpp:kits()` — v4: Inputs Quick actions / bulk "Fill in order"
- `[behaviour]` Library-style relative time "Just now / N min ago / N hours ago / Yesterday / N days ago / 17 Nov" — `SetupPages.cpp:whenText` — v4: Sessions list date column

### Sessions page (SessionsPage)

- `[callback]` onOpen(File) → `services.loadSession`; error toast; else rebuild Advanced/Mixer/windows/Tracks, go to Assign if no inputs else Tracks, toast `Opened "<name>".` + `services.takeRecoveryNote()` — `SetupPages.h:SessionsPage::onOpen` / `MainView.cpp:1256` — v4: Sessions page (double-click / Return / Open)
- `[callback]` onNew → `MainView::newSession` (refused under LIVE SAFE, refused while recording, then Page::Device) — `SessionsPage::onNew` / `MainView.cpp:2860` — v4: Sessions "New session" + toolbar session menu New ⌘N
- `[callback]` onOpenFile → `MainView::openSession` (Open… from anywhere on disk) — `SessionsPage::onOpenFile` — v4: Sessions "Open…" + ⌘O
- `[callback]` onImportFolder → `MainView::importMultitrack(true)` (new session first) — `SessionsPage::onImportFolder` — v4: Sessions "Import a multitrack folder" + session menu
- `[control]` "New session" filled primary button, tooltip "Start from nothing: pick the device, name the inputs, then tune." — `SessionsPage::newButton` — v4: Sessions page
- `[control]` "Open…" button, tooltip "Open a session from anywhere on this Mac." — `SessionsPage::openButton` — v4: Sessions page
- `[control]` "Import a multitrack folder" button, tooltip: folder of stems becomes a session, one track per file — `SessionsPage::importButton` — v4: Sessions page
- `[control]` Search field "Search sessions" matches the name, the profile name or the purpose name — `SessionsPage::search` / `SessionsPage::rebuild` — v4: Sessions page
- `[control]` Filter chips Recent (modified within 7 days) / Templates (name contains "template"); click toggles, default is none (shows everything) — `SessionsPage::chips`, `filter` — v4: Sessions page
- `[control]` Session row: lamp (accent = the open session, ok = valid, warn = not a DINE session), name, "N inputs · N tracks · purpose · profile" or "Not a DINE session", stacked per-bus bar (only when row > 620 px wide), relative date — `SessionsPage::Row::paintButton` — v4: Sessions list row (keep the per-bus bar, a deliberate deviation)
- `[gesture]` Click row selects it — `SessionsPage::rebuild` (row onClick → select) — v4: Sessions
- `[gesture]` Double-click row opens it — `SessionsPage::Row::mouseDoubleClick` — v4: Sessions
- `[menu]` Right-click row: Open / Show in Finder (`File::revealToUser`) — `SessionsPage::Row::mouseDown` — v4: Sessions row context menu
- `[key]` Return opens the selected session; ↑/↓ move the selection through the filtered list — `SessionsPage::keyPressed` — v4: Sessions (keep)
- `[binding]` `services.listSessions()`, `services.currentSessionName()`, `SessionStore::summarise(file)` (cached by path+mtime) — `SessionsPage::refresh`, `cache` — v4: Sessions
- `[state]` Selection defaults to the open session, else the first row — `SessionsPage::refresh` — v4: Sessions
- `[behaviour]` Empty states: "No sessions saved yet / Start a new session and it is saved into ~/Music/DINE…" and "Nothing matches that / Try a different word, or switch the filter off." — `SessionsPage::paint` — v4: Sessions empty state
- `[behaviour]` Copy: "Sessions live in ~/Music/DINE. The audio stays in its own folder."; "Sorted by when it was last saved"; note under the list "Start from nothing…" — `SessionsPage::paint` — v4: Sessions
- `[behaviour]` Folder text helper (~ path or …/parent/folder), written but not drawn today — `SessionsPage::folderText` — v4: KEEP current (unused helper; candidate for a row tooltip)
- `[behaviour]` Re-styles the search editor on theme change — `SessionsPage::lookAndFeelChanged` — v4: rule (tokens re-read)
- `[binding]` Sidebar Sessions row shows the session count — `MainView::updateChrome` (`sidebar->item(Page::Sessions).setMeta`) — v4: sidebar Set up → Sessions
- `[behaviour]` Launch lands on Sessions when any session exists, else Device — `MainView.cpp:1382` — v4: KEEP rule

### Audio device page (DevicePage)

- `[callback]` onContinue → session has inputs ? `enterSession()` (reconfigure, touch, rebuild, Tracks) : Page::Assign — `DevicePage::onContinue` / `MainView.cpp:1271` — v4: Audio device "Continue → Inputs" (keep the skip to Tracks when already patched)
- `[callback]` onContinueToAssign → Page::Assign (declared for "an import happened: review the guessed assignments"; wired, never fired from DevicePage) — `DevicePage::onContinueToAssign` / `MainView.cpp:1276` — v4: KEEP (brief lists it)
- `[callback]` onImportRecording(folder) → `MainView::importMultitrackFolder` (the same import as File > Import, so Mixer/Inspector rebuild too) — `DevicePage::onImportRecording` / `MainView.cpp:1281` — v4: Audio device "Import a multitrack folder"
- `[callback]` onSetUpOutputs → `showOutputs()` = Page::Outputs — `DevicePage::onSetUpOutputs` — v4: Audio device "Set up outputs…"
- `[callback]` onBack → Page::Sessions — `DevicePage::onBack` — v4: Audio device Back
- `[callback]` onToast(String): input-refused sentence, buffer-change error — `DevicePage::onToast` — v4: toast
- `[control]` Device list rows: lamp (accent = open, ok = has inputs, ink4 = output only), name, "N in · N out · transport", "output only: nothing comes in this way", "In use" on the open one — `DevicePage::DeviceRow` — v4: Audio device "Input device" (the brief draws a menu; keep the list or the menu, with the same rows)
- `[rule]` Auto-select: the device in use, else the device with the most inputs ("the console or Dante, not the built-in mic") — `DevicePage::refresh` — v4: Audio device
- `[rule]` Continue is enabled only when the selected device has more than 0 inputs — `DevicePage::refresh` / `select` — v4: Audio device
- `[control]` "Where it comes out" output rows: lamp, name, "Output 1-2 · N available" or "No stereo pair" — `DevicePage::OutputRow` — v4: Audio device "Output device"
- `[rule]` Default output: same device as the input when it has outputs, else the first output device; `services.outputDisplayName()` wins — `DevicePage::refresh` — v4: Audio device
- `[behaviour]` Choosing an output while audio runs calls `services.changeOutput` at once; only when the pair really changed — `DevicePage::selectOutput` — v4: Audio device output picker
- `[control]` Continue (filled): `services.askForInputPermission` first (the mic prompt belongs here, not at launch), then `openChosenDevice` — `DevicePage::continueButton` — v4: Audio device Continue
- `[behaviour]` `openChosenDevice`: `services.openDevices(input, outputName)`; error stays on the page in crit red; if `deviceState().stage == InputRefused`, toast `device.why` and continue anyway (output-only session still plays/mixes/saves) — `DevicePage::openChosenDevice` — v4: KEEP rule + mic-permission toast
- `[control]` "Rescan devices" (Refresh icon) → `refresh()` re-lists `services.inputDevices()/outputDevices()` — `DevicePage::rescanButton` — v4: Audio device "Rescan devices"
- `[control]` "Set up outputs…" with tooltip (PA on 1-2, headphones/cue on 3-4) — `DevicePage::outputsButton` — v4: Audio device
- `[control]` "Import a multitrack folder" (folder chooser starting in ~/Music, AIFF/WAV/FLAC tooltip) — `DevicePage::recordingButton` — v4: Audio device
- `[control]` Back — `DevicePage::backButton` — v4: Audio device footer
- `[control]` Spec row "Sample rate": "Matched to the console", "48 kHz" or "–" (read-only) — `DevicePage::paint` (`services.sampleRate()`) — v4: Audio device sample-rate menu. Read-only today: see GAPS
- `[gesture]` Click the Buffer spec row (audio running) → menu of `services.bufferSizes()` as "N samples · x.x ms", ticked current → `services.setBufferSize(n)`, error toasted — `DevicePage::mouseUp` — v4: Audio device buffer menu
- `[control]` Spec row "Dropped buffers": `services.xrunCount()`, warn colour and "Raise the buffer if this keeps climbing" when above 0 — `DevicePage::paint` — v4: Audio device + sidebar footer "N dropped buffers"
- `[control]` "What is arriving" meters: two columns of 26 px rows (one when narrower than 440): number, session input name or "In N", `daw().inputPeakDb(c)` bar, "and N more" — `DevicePage::paint` — v4: Audio device "What's arriving" meters
- `[behaviour]` Footer note: `deviceSentence(deviceState, any)` / "Pick a device with inputs to carry on." / "<device> · N inputs ready." — `DevicePage::paint` — v4: Audio device footer
- `[behaviour]` Empty states: "No inputs yet / Connect your interface or console and rescan…", "Nothing to come out of yet.", "Press Continue and the meters fill in - your console is untouched." — `DevicePage::paint` — v4: Audio device
- `[binding]` `services.isAudioRunning`, `numInputChannels`, `currentInputDevice`, `daw().numInputsCarryingSignal()` (liveInputCount) — `DevicePage` — v4: Audio device + sidebar device footer
- `[behaviour]` Refresh when the page is shown — `MainView::showPage` (`devicePage->refresh()`) — v4: rule

### Mic permission (MainView + ChoiceSheet; brief: keep the mic-permission toast)

- `[binding]` `services.askForInputPermission(cb)` on Device Continue — `DevicePage::continueButton` — v4: Audio device Continue
- `[behaviour]` Explain sheet before the macOS prompt: "macOS is about to ask about the microphone", two columns (what DINE listens to / what it does with it), note "Say Not now and DINE still plays, mixes, saves and exports…", actions Not now / Continue; Esc or any other close counts as Not now (deferred) — `MainView::explainMicrophone` — v4: Mic sheet (ChoiceSheet)
- `[behaviour]` `followMicrophone`: if `inputHeldBack() != Listen` and `retryHeldInput()` succeeds, toast "macOS now lets DINE hear <input>. The inputs are open."; if AskFirst and not yet explained and no sheet is open, show the explain sheet then ask — `MainView::followMicrophone` — v4: KEEP rule (toast + Mic sheet)
- `[state]` micExplained: explained once per run — `MainView::micExplained` — v4: KEEP

### Inputs page (AssignPage)

### Callbacks and programmatic API
- `[callback]` onContinue → Page::Purpose (fires only with at least one assigned input; commits first) — `AssignPage::onContinue` / `MainView.cpp:1283` — v4: Inputs footer "Continue → Purpose"
- `[callback]` onBack → Page::Device — `AssignPage::onBack` — v4: Inputs footer Back
- `[callback]` onSaveMapping → `MainView::saveInputMapping` (dialog: name defaults to the session name or "Main hall", optional note; toast `Patch saved as "<n>": N inputs across N channels.`; "There is nothing patched yet." when empty) — `AssignPage::onSaveMapping` / `MainView.cpp:2238` — v4: Inputs Patch ▾ "Save this patch"; Input Mappings "Save this session's patch"
- `[callback]` onApplyMapping → Page::Maps (the Patches section) — `AssignPage::onApplyMapping` — v4: Inputs Patch ▾ "Apply a saved patch" → Input Mappings
- `[binding]` `assign(input, role, name, linkWithNext)` for programmatic edits and the snapshot tool — `AssignPage::assign` — v4: KEEP API
- `[binding]` `selectInputs(vector)` picks rows out and switches to the bulk toolbar — `AssignPage::selectInputs` — v4: KEEP API
- `[binding]` `clearAll()` resets every entry, commits — `AssignPage::clearAll` — v4: Quick actions "Clear all" + footer Clear all
- `[binding]` `tick()` at the window rate: peak-HOLD up instantly, down 0.1 dB/tick (~3 dB/s), repaint only when it moved more than 0.05 dB — `AssignPage::tick` / `MainView::timerCallback` (only while Assign is visible) — v4: Inputs arriving meter with peak-hold tick
- `[binding]` `inputsNeedingGain()` counts assigned, non-right-half inputs whose `controller.liveCaptureAdvice(role, peakHoldDb)` is Clipping/Hot/Low/Faint (NotHeard is deliberately not counted) — `AssignPage::inputsNeedingGain` — v4: "N inputs want the preamp moved" banner
- `[binding]` refresh(): numInputs = max(device channels, every session inputA/B + 1) capped at kMaxInputs; held peaks survive; entries rebuilt from `controller.getSession().inputs` (name, icon, enabled, role, inputB==inputA+1 → linked) — `AssignPage::refresh` — v4: Inputs
- `[binding]` commit(): rows → `MixSession.inputs` (skips unassigned and right halves; an empty name becomes the short role name; inputB = A+1 when linked); keeps the existing TRACKS order (stable sort by previous place, new inputs after in channel order); `controller.setSession(s)` + `services.daw().setSession(s)` immediately — `AssignPage::commit` — v4: KEEP rule (order preservation; stereo link reaches the timeline at once)
- `[behaviour]` Leaving Routing with `controller.needsReconfigure()` runs `services.reconfigure()` + rebuilds (the sidebar now does what Continue did) — `MainView::showPage` — v4: KEEP rule

### Table and row
- `[control]` Row: number (or "5−6" when linked), name editor ("Untitled" placeholder, select-all on focus), suggest-names ▾ button, What it is popup ("Not used" or friendly role), Link switch "L/R / Link", group cell (lamp + Drums/Bass…/"Not used"), held-peak meter with a live tick, verdict ("OK" or `advice.headline` in verdict colour) — `AssignPage::Row` — v4: Inputs table (ch, name, what it is, feeds, pair, arriving meter, level verdict)
- `[gesture]` Name editor: typing writes the entry; Return gives up focus and commits; focus lost commits — `AssignPage::Row` (name.onReturnKey / onFocusLost) — v4: inline Name cell (Tab/Return/Esc rules are new: see GAPS)
- `[menu]` Suggest names ▾: short role name, friendly role name (if different), "Desk NN", separator, "Clear the name" — `AssignPage::showNameMenu` — v4: right panel "suggested names ▾ (desk label / role / clip)"
- `[menu]` What it is ▾: "Not used" (ticked when unassigned), then a submenu per role group with the current role ticked — `AssignPage::showSourceMenu` — v4: What it is ▾ / typeahead
- `[rule]` Picking Overhead, DrumBus, Piano, ElectricPiano or SynthPad auto-links to the next input when that one is unassigned and not already linked — `AssignPage::showSourceMenu` — v4: KEEP rule (applies to the typeahead too)
- `[rule]` setRole: unassigning clears the role and the link (and the next row's linkedFromPrevious); assigning with an empty name fills in the short role name — `AssignPage::setRole` — v4: KEEP rule
- `[control]` Link switch: shown only when this input is assigned, a next input exists, and the next is not someone else's right half; toggles linkedToNext/linkedFromPrevious, commits, rebuilds — `AssignPage::Row::refresh` / link.onClick — v4: Pair ▾ column / right-panel Pair
- `[gesture]` Click a row toggles its selection; Shift-click selects the range from lastClicked (skipping right halves) — `AssignPage::Row::mouseDown` / `toggleSelection` — v4: Inputs table (brief: click = edit in panel, ⌘-click = add, shift-click = range: the click meaning changes)
- `[behaviour]` Linked right halves are hidden rows (`linkedFromPrevious` → not visible) — `AssignPage::visible` — v4: Inputs table
- `[behaviour]` "SIGNAL HERE" (above -30 dB) / "FAINT" (above -54 dB) warn chip on an unassigned input carrying signal — `AssignPage::Row::paint` — v4: "Not used" section flags inputs with signal
- `[behaviour]` Column captions: Input, Name, What it is, Group, "Loudest so far", "At the desk" — `AssignPage::paint` — v4: Inputs table header
- `[control]` Group header (grouped mode only): lamp, name, count, hover "Select them all", "stays out of the mix" on Not used; click selects/deselects the whole group — `AssignPage::GroupHeader` / `selectGroup` — v4: Quick actions group-by-bus view
- `[state]` grouped (flat by default; Quick actions toggles "Group by the bus each input feeds") — `AssignPage::grouped` — v4: Quick actions ▾ group by bus on/off
- `[state]` busFilter: -2 all, -1 not used, else MixBus — `AssignPage::busFilter` — v4: group chips + Not used chip
- `[state]` query (search) — `AssignPage::query` / `visible` (matches name, exact input number, role name or "Not used") — v4: Inputs search. The search editor is hidden today (`updateToolbar` hides it): see GAPS

### Toolbar (no selection)
- `[control]` "All inputs" chip → busFilter -2 — `AssignPage::chips[0]` — v4: group chips
- `[control]` "Not used" chip toggles busFilter -1 — `AssignPage::chips[1]` — v4: Not used chip
- `[menu]` Group/list button ▾: Every input / "Only <Bus>" per group bus / "Group by the bus each input feeds" ↔ "Show one flat list" — `AssignPage::groupButton` — v4: group chips + Quick actions
- `[control]` "Name everything from what it is": every assigned input takes its short desk name — `AssignPage::nameButton` → `quickButtons[0]` — v4: Quick actions ▾
- `[control]` "Pair every L and R": neighbours whose names end in " L"/" R" with the same stem get linked — `AssignPage::linkButton` → `quickButtons[2]` — v4: Quick actions ▾
- `[control]` "Select every input not used": selects every unassigned non-right-half input, filter back to all — `AssignPage::selectAllButton` → `quickButtons[1]` — v4: Quick actions ▾
- `[menu]` Quick actions ▾: Name everything from what it is / Select every input not used / Pair every L and R / (group by bus ↔ one flat list) / Save this patch… / Apply a saved patch… / Clear every assignment — `AssignPage::quickButton` — v4: Quick actions ▾ + Patch ▾ (split as the brief does)

### Toolbar (with a selection: the bulk bar)
- `[control]` Deselect — `AssignPage::deselectButton` → `clearSelection` — v4: bulk bar Deselect
- `[behaviour]` "N inputs selected" label — `AssignPage::paint` — v4: bulk bar
- `[menu]` "Set what it is" ▾: Not used + role-group submenus → setRole on every selected input — `AssignPage::bulkButton` / `showBulkMenu` — v4: bulk "Set what it is"
- `[menu]` "Fill in order" ▾: header "Fill N inputs, in order", each kit with a preview of its short names; lays roles down the selection (the last role repeats); auto-links adjacent OH L+R and Piano+Piano; clears the selection — `AssignPage::kitButton` / `showKitMenu` — v4: bulk + Quick actions "Fill in order"
- `[control]` "Use desk labels": every input still without a name (whole desk, not just the selection) becomes "Desk NN" — `AssignPage::deskLabelsButton` — v4: toolbar "Use desk labels" (show it without a selection too)
- `[control]` "Not used" (ghost): unassigns the selection — `AssignPage::dropButton` / `dropSelection` — v4: bulk "Not used"
- `[binding]` nameFromRole(): selected assigned inputs take their short name — `AssignPage::nameFromRole` (private, no button calls it today) — v4: bulk "Name from what it is"
- `[binding]` linkSelection(): links each selected input whose next input is also selected — `AssignPage::linkSelection` (private, no button calls it today) — v4: bulk "Pair L and R"
- `[behaviour]` Toolbar has two states: whole-desk vs selection; chips and the group button stay in both — `AssignPage::updateToolbar` — v4: KEEP rule

### Footer and hidden members (keep every member)
- `[control]` Continue (enabled only with at least one assigned input) — `AssignPage::continueButton` — v4: Inputs footer
- `[control]` Back — `AssignPage::backButton` — v4: footer
- `[control]` "Clear all" (ghost): built but `setVisible(false)` in resized — `AssignPage::clearButton` — v4: footer Clear all (bring it back)
- `[control]` "Show them" (→ busFilter -1): built but always hidden — `AssignPage::showUnusedButton` — v4: "Not used" section
- `[control]` "Save this patch" / "Apply a saved patch" buttons with tooltips: built, hidden; reached through Quick actions — `AssignPage::patchSaveButton`, `patchApplyButton` — v4: Patch ▾
- `[control]` QuickAction rail cards (3, two-line): built with `addChildComponent` and never shown; their onClick bodies are the live implementations the toolbar calls — `AssignPage::quickButtons`, `AssignPage::QuickAction` — v4: KEEP logic (Quick actions ▾)
- `[behaviour]` Footer status: "Every input placed" (accent) / "Name at least one input to carry on." / "N inputs are still open"; then warn "N inputs want the preamp moved at the desk."; then "N unassigned inputs are carrying signal right now." (above -54 dB) — `AssignPage::paint` — v4: footer counts + preamp banner
- `[behaviour]` Empty states: "No inputs to name yet / Go back and pick a device with inputs, or import a folder of stems." and "Nothing matches that / Switch the filter back to All inputs." — `AssignPage::paint` — v4: Inputs empty state
- `[behaviour]` Head copy "Every assigned input takes its short desk name. Press Continue and the meters fill in - your console is untouched." — `AssignPage::paint` — v4: Inputs head
- `[behaviour]` Re-styles the search editor and the row name editors on theme change — `AssignPage::lookAndFeelChanged`, `Row::lookAndFeelChanged` — v4: rule
- `[binding]` Other entry points to Inputs: Tracks onOpenAssign, Mixer onOpenAssign, setup popover "Inputs" / "Rename or fix the inputs…", Mix Buddy BuddyPage::Assign — `MainView.cpp:1326,1349,1713,1720,2176` — v4: session menu "Rename/Fix Inputs", Mixer right-click "fix assignments"

### Purpose and sound page (PurposePage)

- `[callback]` onContinue ("Tune the mix") → `enterSession()` — `PurposePage::onContinue` / `MainView.cpp:1316` — v4: Purpose and sound footer
- `[callback]` onBack → Page::Assign — `PurposePage::onBack` — v4: footer Back
- `[control]` Purpose cards (one per MixPurpose, 4) with a sentence each and specs "Target LUFS · True peak dBTP · Range Tight/Open/Wide" from `Profiles::targets(profile, masterRoleFor(purpose))` → `controller.setPurpose` — `PurposePage::purposeTiles` / `refresh` — v4: Purpose (brief lists Service/Stream/Record; there are 4 MixPurpose values: keep all 4)
- `[control]` Sound cards (6 StyleProfileIds) with a sentence and a tag (Default, A documented delta, Club festival indie, Studio urban, Jazz folk unplugged, Conference podcast) → `controller.setProfile` — `PurposePage::soundTiles` — v4: Purpose and sound profile cards
- `[rule]` static_assert StyleProfileId::Count == 6 forces a new sentence and tag for any new profile — `PurposePage::PurposePage` — v4: KEEP
- `[menu]` "How loud it should end up" delivery popup: FromPurpose "(N LUFS)" then every DeliveryLoudness with its LUFS → `controller.setDelivery` — `PurposePage::deliveryButton` — v4: Purpose and sound loudness (also the Tune loudness target menu, IDs 430–436)
- `[behaviour]` Delivery hint line `deliveryLoudnessHint(session.delivery)` beside the popup — `PurposePage::paintBody` — v4: Purpose and sound
- `[behaviour]` Closing sentence "DINE will land the mix at X LUFS, never letting it peak past Y dBTP, and tune every group toward <profile>…" (the ceiling becomes -1.0 at -15 LUFS and louder, else -1.5) — `PurposePage::paintBody` — v4: Purpose and sound
- `[rule]` Changing purpose, sound or delivery changes nothing until the next TUNE MIX (head copy + tooltip) — `PurposePage::paint`, deliveryButton tooltip — v4: KEEP copy
- `[behaviour]` Scrolling body; card grid wraps (min 260, max 300 wide, gap 12) — `PurposePage::Body`, `soundColumns` — v4: layout
- `[behaviour]` Footer note "DINE listens for about thirty seconds, then sets the whole mix." — `PurposePage::paint` — v4: footer
- `[binding]` Setup popover "Purpose and sound" row shows purpose + profile; "Open setup" goes to Purpose when prepared, else Device — `MainView::setupPopover` — v4: toolbar session menu "Open Setup"

### Routing workspace (RoutingPage)

- `[state]` Section enum Device / Inputs / Outputs / Maps("Patches") as a segmented control at the top right — `RoutingPage::Section`, `nav`, `segments` — v4: sidebar Routing → Audio device / Inputs / Outputs + Input Mappings
- `[callback]` onSection(s) → `showPage(pageForSection(s))` — `RoutingPage::onSection` — v4: sidebar sub-rows
- `[callback]` onCoverChanged → re-show the current routing page so hosted pages get their bounds — `RoutingPage::onCoverChanged` — v4: KEEP
- `[callback]` onToast — `RoutingPage::onToast` — v4: toast
- `[callback]` onApplyMap(File) → `MainView::applyInputMapping` — `RoutingPage::onApplyMap` — v4: Input Mappings Apply
- `[callback]` onSaveMap → `saveInputMapping` — `RoutingPage::onSaveMap` — v4: Input Mappings "Save this session's patch"
- `[callback]` onImportMap → file chooser `*.dinemap.json` → `InputMapStore::load` + `save`; "That is not a DINE input patch." / "That patch could not be saved." / `Imported "<n>".` then Page::Maps — `MainView.cpp:1293` — v4: Input Mappings "Import a patch"
- `[callback]` onChooseOutputDevice(name) → `changeOutput` or `openOutputOnly`; toast "Output: <n>" or the error (no LIVE SAFE check here, the cover guards it; does not flag readiness, the toolbar picker does) — `MainView.cpp:1308` — v4: Outputs Broadcast picker
- `[binding]` host(page): DevicePage and AssignPage are parented into Routing so the segments stay on top; `contentBounds()` is empty while covered — `RoutingPage::host`, `contentBounds` — v4: KEEP
- `[behaviour]` headHeight: hosted pages draw their own title; Routing draws a title + sentence only over Outputs ("Send the mix, or one group, to another pair… Monitoring only") and Patches ("A patch is the inputs and nothing else - never a mix.") — `RoutingPage::paint` — v4: Outputs / Input Mappings heads
- `[rule]` LIVE SAFE cover: while `daw().isLiveSafe()` and not confirmed, a warn card "LIVE SAFE IS ON / The device, the patch and the output feeds are the three ways to silence a room…" hides every section — `RoutingPage::isCovered`, `paint` — v4: Routing cover (keep)
- `[control]` "I know what I am doing": confirms for this visit only; toast "Routing unlocked for this visit. LIVE SAFE is still on everywhere else." — `RoutingPage::unlockButton` / `confirmForTest` — v4: Routing cover
- `[rule]` Leaving the Routing workspace re-locks it (resetConfirmation) — `MainView::showPage` → `RoutingPage::resetConfirmation` — v4: KEEP
- `[behaviour]` refresh() re-reads LIVE SAFE every tick (it can be switched from toolbar/Live/shortcut), refreshes Outputs while visible, rebuilds Patches when the input device changes — `RoutingPage::refresh` (from `MainView::updateChrome`) — v4: KEEP
- `[control]` Patch row (MapRow): name, "N inputs · needs N channels · device · note", warn "more channels than this device has" when channelsNeeded > available, per-bus stacked bar, tooltip "Saved from <device> on <date>" — `RoutingPage::MapRow` — v4: Input Mappings list row
- `[control]` Patch row "Apply" → onApplyMap — `MapRow::applyButton` — v4: Input Mappings Apply
- `[menu]` Patch row "…": Rename… (dialog Save/Cancel → `InputMapStore::rename`), Duplicate (`duplicate(name, name+" copy")`), Export… (`saveAs` to ~/Documents/<name>.dinemap.json, warns on overwrite), Delete (`InputMapStore::remove`, no confirm); each toasts — `RoutingPage::showMapMenu`, `askForName` — v4: Input Mappings ⋯ (brief lists only Rename/Delete: keep Duplicate and Export)
- `[control]` "Save this session's patch" (filled), "Import a patch" — `RoutingPage::saveMapButton`, `importMapButton` — v4: Input Mappings
- `[behaviour]` Empty Patches copy "No patches saved yet. A church patches the same desk the same way every Sunday…" — `RoutingPage::paint` — v4: Input Mappings empty state
- `[binding]` `InputMapStore::list()`, `services.currentInputDevice()`, `services.numInputChannels()` — `RoutingPage::rebuildMaps` — v4: Input Mappings
- `[behaviour]` applyInputMapping: refused under LIVE SAFE ("changing the routing"); `InputMapStore::apply(map, session, channels, device, map.hasSound)`; problems → warning dialog listing them + "N of M inputs can be restored exactly…" with Apply anyway / Cancel; then `controller.setSession` + `services.reconfigure()` + `touchSession()` + Assign refresh; toast "Applied … N inputs restored, N switched off. Check the INPUTS page." — `MainView::applyInputMapping` — v4: Input Mappings Apply flow
- `[menu]` Legacy "Input Mappings" popup (per-patch submenu Apply/Rename/Duplicate/Export/Delete + Import + Save): defined but no caller (menu 109 now goes to Page::Maps) — `MainView::openInputMappings` — v4: KEEP or retire on purpose (dead code)
- `[menu]` File > Save Input Mapping… (108), File > Input Mappings… (109 → Maps), View > Set-up and Routing (613), View > Saved Input Patches (616) — `MainView::getMenuForIndex`, `handleCommand` — v4: session menu "Save Input Mapping"; Routing
- `[binding]` Sidebar Routing row shows "N in" and a done mark when mixable and not on Routing — `MainView::updateChrome` — v4: sidebar

### Outputs (OutputsSheet, embedded in Routing > Outputs)

- `[state]` Embedded on Routing: no scrim, no Close, card at the top; the same class is a sheet when not embedded — `OutputsSheet::setEmbedded` — v4: Routing > Outputs (the toolbar, View menu 609, Live and Readiness all route to `showOutputs()` = Page::Outputs)
- `[callback]` onClose (sheet mode: Close button or a click off the card) — `OutputsSheet::onClose`, `mouseUp` — v4: n/a embedded; KEEP for sheet mode
- `[callback]` onToast — `OutputsSheet::onToast` — v4: toast
- `[callback]` onChooseDevice(name): the host reopens the device — `OutputsSheet::onChooseDevice` → `RoutingPage::onChooseOutputDevice` — v4: Broadcast picker
- `[menu]` Broadcast device ▾: every output device "Name   N out", current ticked; "No output devices found." toast when none — `OutputsSheet::chooseDevice` — v4: Outputs "Broadcast" picker
- `[behaviour]` Broadcast status: "no device open" / "N output channels · always stereo, on 1-2" (warn under 2) — `OutputsSheet::paint` — v4: Outputs
- `[binding]` Broadcast label shows `services.broadcastOutputDevice()` (not the open combined device) or "None" — `OutputsSheet::refresh` — v4: KEEP rule
- `[menu]` Solo device ▾ = `showSoloDeviceMenu` (shared with Live): — `OutputsSheet::showSoloDeviceMenu` — v4: Outputs "Solo" picker + toolbar output button + Live solo picker
  - `[rule]` Item "Nowhere - I do not need solo", ticked when no solo device and not in place → `setSoloOutputDevice({})`, and in-place falls back to `SoloMode::Monitor` — v4: same
  - `[rule]` Item "Here - everyone hears solo (on <broadcast>, not for a service)" offered only when the broadcast device has fewer than 4 outputs; disabled while `controller.isLiveSafe()`; → clears any solo device, `controller.setSoloMode(SoloMode::InPlace)` (setSoloMode says "Careful: solo is now heard by everyone") — v4: same
  - `[rule]` Every output device with more than 0 outputs, skipping names starting "DINE Monitoring"; the broadcast's own device appears as "<name> (on its outputs 3-4)" and is enabled only with 4 or more outputs; current ticked — v4: same
  - `[rule]` "No output devices found" disabled item when the list is empty — v4: same
  - `[rule]` Choosing a device calls `services.setSoloOutputDevice(name)`, its `MonitorSetup.message` is toasted, and in-place goes back to Monitor — v4: same
- `[binding]` `soloChoiceLabel`: solo device name, "Here - everyone hears" when in place, else the caller's none-text ("Nowhere yet" here, "Choose headphones" on Live) — `OutputsSheet::soloChoiceLabel` — v4: Outputs/toolbar/Live labels
- `[behaviour]` Solo status: "only you hear this" (ok) / "everyone hears solo" (warn) / "solo has nowhere to go yet" (ink4) — `OutputsSheet::paint` — v4: Outputs Solo status
- `[rule]` The solo picker is disabled unless audio is running — `OutputsSheet::refresh` — v4: KEEP
- `[control]` Feed rows (up to kMaxOutputFeeds = 4; at least 1): name Broadcast / Feed 2 / Feed 3 / Feed 4, or "Monitor" when the feed carries the listen; muted text dims — `OutputsSheet::Row::paint` — v4: Outputs feed rows
- `[menu]` Source ▾: "Main mix" / every group bus (disabled unless `engine.isBusUsed`) / section "Just for you" → "My headphones (whatever is soloed)" (monitor = true) — `OutputsSheet::Row::chooseSource` — v4: Source ▾
- `[menu]` Destination ▾: each pair named from `services.outputChannelNames()` "L / R" or "Outputs N-M", then "Not routed" (left/right = -1) — `OutputsSheet::Row::choosePair`, `pairName` — v4: Destination ▾
- `[behaviour]` "not on this device" warn under Destination when the pair no longer exists — `OutputsSheet::Row::paint` / `pairExists` — v4: same
- `[control]` Level knob -60…+12 dB, step 0.1, double-click = 0.0; readout "±x.x dB" — `OutputsSheet::Row::levelKnob` — v4: Level knob
- `[rule]` The main-mix feed (not monitor, source Master) is capped at 0 dB: past unity is level the master limiter never saw — `OutputsSheet::Row::set` — v4: KEEP
- `[control]` MONO toggle; disabled on feed 1 (Broadcast) and on any monitor feed, with the tooltip "Always stereo: the broadcast and your own listen are never summed." — `OutputsSheet::Row::monoButton` — v4: MONO
- `[control]` MUTE: amber (`Dine::keyMute`) filled when on — `OutputsSheet::Row::muteButton` — v4: MUTE
- `[control]` × remove (not on feed 1): shifts the later feeds up, count-1 — `OutputsSheet::Row::removeButton` / `removeFeed` — v4: × remove
- `[control]` "Add a feed": picks the next free pair from pair 1 up; when every pair is in use, toast "Every output on this device is already in use. Move a feed first, or choose a bigger device." (never doubles a pair); else adds Main mix on that pair and toasts "The mix now also goes to <pair>."; disabled at 4 feeds or under 2 channels — `OutputsSheet::addFeed` — v4: Add a feed
- `[binding]` Every edit → `controller.setOutputFeeds(OutputFeeds)` via commit — `OutputsSheet::commit` — v4: KEEP (MixController API)
- `[behaviour]` Foot: "N of 4 feeds in use"; `services.headphonesSummary()` + "While LIVE SAFE is on the monitor bus cannot be re-routed…" or "Pick the device you listen on above. It can be a different box from the broadcast - DINE joins them for you." — `OutputsSheet::paint` — v4: Outputs foot
- `[behaviour]` Table headings Feed / Source / Destination / Level / Mono · Mute — `OutputsSheet::paint` — v4: Outputs
- `[binding]` Toolbar output button: list of output devices (skipping DINE Monitoring), current = broadcast, "Set up outputs…" → Outputs; refused under LIVE SAFE ("changing the output device"); on success `controller.flagReadiness(BroadcastDevice)` + toast "Broadcast: <n>" — `MainView::chooseOutput` — v4: toolbar output button "Main · solo <device>" (add the Solo choices from the shared menu)

### Check inputs sheet (CheckSheet)

- `[callback]` onClose (Done, a click off the card, Esc via MainView) — `CheckSheet::onClose`, `mouseUp` — v4: Check inputs sheet
- `[binding]` Opened by View > Check Inputs… (630), MixPage onOpenCheck, Readiness "Check inputs" link, Mix Buddy OpenCheckInputs; a second open refreshes — `MainView::showCheck` — v4: sidebar "Check inputs" (with issue badge) + sheet
- `[binding]` Rows from `controller.getSession().inputs` (name, `channelRoleName(role)`) — `CheckSheet::CheckSheet` — v4: same
- `[binding]` 30 Hz refresh reads `engine.consumeConverterPeakDb(i)` (at the converter, before DINE's gain) and `engine.converterClipped(i)` — `CheckSheet::refresh` — v4: KEEP (shared clock)
- `[rule]` States: CLIP (converter clipped since open), SILENT (nothing above -60 for 3 s), else from `controller.liveCaptureAdvice(role, holdDb)`: Hot/Clipping → HOT, Low/Faint/NotHeard/Unknown or hold ≤ -100 → LOW, else OK (the same verdict as Inputs and TUNE) — `CheckSheet::refresh` — v4: KEEP rule
- `[behaviour]` Bar = 1 s hold falling 30 dB/s; Peak column = highest since open; colours hot above -6, accent above -18 — `CheckSheet::refresh`, `paint` — v4: same
- `[behaviour]` Headline "N inputs · N OK · N silent · N low · N hot · N clipped" / "No inputs are assigned yet." — `CheckSheet::headline` — v4: sidebar badge count source
- `[control]` "Reset clips": clears row clips/holds, `engine.clearConverterClips()` and drains `consumeConverterPeakDb` — `CheckSheet::resetButton` / `resetClips` — v4: Check inputs sheet
- `[control]` Done — `CheckSheet::doneButton` — v4: same
- `[behaviour]` Copy "Reading only. Nothing here changes the mix." — `CheckSheet::paint` — v4: same
- `[behaviour]` Partial repaint of the bars only when no state changed — `CheckSheet::refresh` — v4: perf rule
- `[binding]` `getRows()` / `headline()` public for tests — `CheckSheet.h` — v4: KEEP

### Mix history sheet (HistorySheet)

- `[callback]` onClose (Close, a click off the card, Esc), onToast — `HistorySheet` — v4: Mix history sheet
- `[binding]` Opened by the sidebar "Mix history" row (Action::MixHistory), MixPage/LivePage/Live window onOpenHistory, Readiness "Mix history" link, Mix Buddy OpenHistory — `MainView::showHistory` — v4: sidebar Mix → Mix history
- `[control]` "Mark this mix as a favourite" (filled) → name dialog (default "Sunday <time>", buttons Mark as favourite / Cancel) → `controller.markFavourite(name)` + `services.touchSession()` — `HistorySheet::askForFavouriteName` — v4: Mix history + Favourite mixes
- `[control]` Favourites section (caption FAVOURITES): when, name, chip "NOT MEASURED" when there is no `fav.sound`, "(different inputs)" suffix — `HistorySheet::rebuild`, `paintRows` — v4: Mix history / Favourite mixes page
- `[control]` Favourite "Bring back" → `controller.recallFavourite(i)` (disabled when the inputs differ) — `Row::restore` — v4: same
- `[control]` Favourite "Aim at this" → `controller.useFavouriteAsReference(i)` (disabled when not measured; tooltip explains) — `Row::aim` — v4: Tune "aim-at-favourite"
- `[control]` Favourite "Remove" → `controller.removeFavourite(i)` (the mix stays in history) — `Row::drop` — v4: same
- `[control]` Checkpoints section (caption "EVERY PLACE TO GO BACK TO" when favourites exist), newest first: time ("9:42 am" or "Sun 9:42 am"), TUNE chip when `fromTune`, what; "Restore" → `controller.restoreCheckpoint(i)` (disabled with different inputs) — `HistorySheet::rebuild` — v4: Mix history list
- `[rule]` Restore keeps the current mix first and UNDO takes it forward again (tooltip) — `Row::restore` tooltip — v4: KEEP copy
- `[behaviour]` A long "what" with a reason is cut at the first ". " instead of mid-word — `HistorySheet::paintRows` — v4: KEEP
- `[behaviour]` Headline "N places to go back to. Where the mix is now is kept before it moves." / "Nothing yet. Every tune, scene and morning of mixing lands here." — `HistorySheet::paint` — v4: same
- `[behaviour]` Rebuilds only when the checkpoint or favourite count changed — `HistorySheet::refresh` — v4: perf rule
- `[binding]` Every change calls `services.touchSession()` (session persistence contract) — `HistorySheet` — v4: KEEP rule

### Export sheet (ExportSheet)

- `[binding]` Opened by File > Export Stereo Mix (WAV) 105 / (MP3) 106 / Export Multitrack 110, setup popover 15/16; `choose(what, format)` preselects — `MainView::exportMix`, `ExportSheet::choose` — v4: Export (⇧⌘E, sidebar Record → Export)
- `[rule]` Refusals before opening: "Finish setup and assign inputs before exporting.", "There is nothing recorded yet. Record a take, or import a multitrack folder.", "An export is already running - the status bar says how far it is." — `MainView::exportMix` — v4: KEEP toasts
- `[control]` What: Stereo mix / Group stems / Raw multitrack, each with a tooltip — `ExportSheet::whatTabs` — v4: Export
- `[control]` Range: Whole session / Loop (enabled only when loopEnd > loopStart) / Between markers (enabled only with 2 or more markers: first → last); falls back to Whole — `ExportSheet::rangeTabs`, `fromSample`, `toSample`, `rangeNote` — v4: Export
- `[control]` Format: WAV / AIFF / MP3 320; MP3 disabled for stems and multitrack — `ExportSheet::formatTabs` — v4: Export
- `[control]` Loudness: As mixed / Stream −14 / Podcast −16 (one gain over the render, no compression); only As mixed for stems and multitrack — `ExportSheet::loudnessTabs` — v4: Export
- `[control]` Save to: path (Dine::shortPath) + "Choose" folder picker; default `<sessionFolder>/Exports` or ~/Music — `ExportSheet::folderButton`, `destination` — v4: Export
- `[rule]` File name = session name (or "DINE mix"), or the first marker's name for Between markers; extension by format; a folder of parts is made beside it ("<name> stems" / "<name> multitrack") — `ExportSheet::destination` / `MainView::exportMix` — v4: KEEP
- `[behaviour]` Notes: "N files, one per input/group", "Parts keep the levels the mix gave them", "A folder is made beside the name above…" — `ExportSheet::whatNote`, `paint` — v4: same
- `[behaviour]` Estimate "mm:ss of audio · about N seconds" (files × 0.65 + 0.35, ×1.4 with a loudness pass) or "There is nothing recorded in that range." — `ExportSheet::paint` — v4: same
- `[control]` Export (filled; enabled only when the range has length) → onExport(Request{dest, format, what, loudness, from, to}) then close; Cancel — `ExportSheet::exportButton`, `cancelButton` — v4: same
- `[key]` Esc closes; Return exports when enabled — `ExportSheet::keyPressed` — v4: KEEP
- `[callback]` onExport → MainView runs `services.snapshotExport()` + `services.exportMix` on a background thread with ExportProgress; the status foot shows progress; toasts on done / cancelled ("Nothing half-written was kept") / failed; "Show in Finder" target recorded — `MainView::exportMix` — v4: Export + status foot (keep the background job)
- `[callback]` onClose, onToast — `ExportSheet` — v4: same

### TUNE CHANNEL sheet (ChannelTuneSheet)

- `[binding]` Opened by `MainView::tuneChannel(strip)` from T (404), the menu "TUNE CHANNEL T", Tracks/Mixer onTuneStrip, Mix Buddy RunTuneChannel — `MainView::tuneChannel` — v4: shared Tune flow sheet (CHANNEL)
- `[rule]` Refusals: LIVE SAFE ("TUNE CHANNEL"), "Assign your inputs first: there is nothing to tune yet.", "BYPASS is on: switch it off to tune a channel.", "DINE is already listening. Let it finish, or cancel it first.", "Pick a channel first: click a strip on the mixer or a track header." — `MainView::tuneChannel`, `handleCommand(404)` — v4: KEEP toasts
- `[binding]` `controller.startTuneChannel(strip, settings)`; the sheet appears only if `isListening()` — `MainView::tuneChannel` — v4: same
- `[behaviour]` Listening state: ring 0–100% "% listened" from `getListenProgress()`, "waiting" when `isWaitingForBand()`, "Building this channel" while Planning, a Heard/waiting pill from `controller.stripHeard(strip)` — `ChannelTuneSheet::paint` — v4: Tune flow Listening phase (30 s ring)
- `[control]` Cancel → `controller.abortTuneMix()` + close — `ChannelTuneSheet::cancel` — v4: same
- `[gesture]` A click off the card cancels a listen but never dismisses a proposal — `ChannelTuneSheet::mouseUp` — v4: KEEP rule
- `[behaviour]` Auto-closes when the listen ends with no plan for this strip (the toast already said why) — `ChannelTuneSheet::refresh` — v4: KEEP
- `[behaviour]` Preview: headline + counts (settings, level ±dB, gain ±dB, sends) + plan notes and relationship what/why lines, capped at 152 px — `ChannelTuneSheet::lines`, `paint` — v4: Changes list
- `[control]` BEFORE / AFTER → `controller.setCompare(Before|After)` (shown only when something changed) — `ChannelTuneSheet::before`, `after` — v4: Before/After A/B
- `[control]` KEEP → `controller.keepPlan()`, toast "<name> is tuned. Everything else in the mix is exactly as it was."; reads "Done" when noChangeRequired — `ChannelTuneSheet::keep` — v4: Keep
- `[control]` REVERT → `controller.revertPlan()`, toast "<name> is back the way it was." — `ChannelTuneSheet::revert` — v4: Revert
- `[control]` "Open in the Inspector" → onOpenInspector (Page::Inspector + `advancedPage->select(strip)`) — `ChannelTuneSheet::inspect` — v4: Tune flow
- `[callback]` onClose, onToast, onOpenInspector — `ChannelTuneSheet.h` — v4: same

### Reference mix sheet (ReferenceSheet, hosted in MixPage)

- `[binding]` Opened by File > Add a Reference Mix… (107), Tune "reference" (408), MATCH TO REFERENCE with no reference (407), setup popover 13 → `mixPage->openReference()` — `MainView::handleCommand`, `MixPage.cpp:2011` — v4: Reference mix sheet (Tune)
- `[state]` Empty / Measuring / Chosen / Refused — `ReferenceSheet::state` — v4: same
- `[control]` "Choose a song…" / "Choose another…" → file chooser (wav/aif/aiff/mp3/m4a/flac in ~/Music) → background `Measurer` thread `ReferenceAudio::measure(file, profile)` — `ReferenceSheet::chooseFile` — v4: same (already a background job)
- `[rule]` A measuring sheet will not close and keeps refreshing while hidden, so a reference picked and then dismissed still arrives — `ReferenceSheet::isMeasuring`, `mouseUp` / `MixPage.cpp:2305` — v4: KEEP
- `[behaviour]` Refused: "That file will not do" (error) or "That is not a mix to aim at" with `adequacy.reason` + `guidance` — `ReferenceSheet::refresh`, `paint` — v4: same
- `[binding]` On success `controller.setReference(profile)`, toast "<name> is the reference. Press MATCH TO REFERENCE…" — `ReferenceSheet::refresh` — v4: same
- `[behaviour]` Chosen: name, "m:ss · N LUFS · stereo/mono · N BPM" (BPM only when confident), two-bar tonal balance per band (reference vs this mix, bounded aim drawn), aims (✓) and limits (⚠) from `Reference::targets` / `plan->reference` — `ReferenceSheet::preview`, `drawBalance` — v4: same
- `[behaviour]` Empty: the three promises (tone/image/density follow; delivery loudness does not; who is loud does not) — `ReferenceSheet::paint` — v4: same copy
- `[control]` "Match to reference" / "Listen, then match" (Target icon) → `controller.startReferenceMatch()`, toast, close; disabled while Listening/Planning/TUNE LIVE — `ReferenceSheet::match` — v4: MATCH REFERENCE through the shared Tune flow
- `[control]` Remove → `controller.clearReference()`, toast "…aimed at the profile again; what the reference already set stays until then." — `ReferenceSheet::remove` — v4: same
- `[control]` Close (Empty/Refused only); a click off the card closes — `ReferenceSheet::close`, `mouseUp` — v4: same
- `[rule]` MATCH TO REFERENCE (407) refused under LIVE SAFE — `MainView::handleCommand` — v4: KEEP

### Mix Buddy (ChatSheet: a right-hand panel, not a modal)

- `[binding]` Toggled by the toolbar chat button and Mix > Mix Buddy… (409); refused "Assign your inputs first: there is nothing to talk about yet." when not prepared; panel width min(kRequestsW, body/2) — `MainView::openChat`, `resized` — v4: toolbar Mix Buddy toggle
- `[control]` Text box (placeholder "Why can't I hear the lead vocal?"), Return or "Ask" → `controller.askBuddy(text)`; the button reads "Working…" while `isChatBusy()` — `ChatSheet::input`, `send`, `ask` — v4: Mix Buddy chat
- `[behaviour]` Intro bubble with `MixBuddy::examples()` when the chat is empty — `ChatSheet::Transcript::layout` — v4: same
- `[behaviour]` Bubbles: engineer (right), DINE, refused (warn); detail lines with an accent rule; auto-scroll to the newest turn — `ChatSheet::Transcript`, `refresh` — v4: same
- `[gesture]` Answer action chips (hover, pointer) → onAction(BuddyAction) → `MainView::performBuddyAction`: ShowPage (Tracks/Mixer/Tune/Live/Inspector/Routing/Assign/Device/Purpose/Outputs/Sessions), OpenInspector(strip), SoloStrip (refused when solo is in place: "…Mix Buddy leaves it to you."), OpenCheckInputs, OpenHistory, RunTuneMix (goes to Tune), RunTuneChannel, AskForChange → `controller.askForChange` — `ChatSheet::Transcript::mouseUp`, `MainView::performBuddyAction` — v4: chat "try it / hear it / undo"
- `[control]` KEEP / REVERT / BEFORE↔AFTER, shown only while `hasBuddyProposal()` → `keepPlan` / `revertPlan` / `setCompare` — `ChatSheet::keepButton`, `revertButton`, `compareButton` — v4: same
- `[rule]` Mix Buddy never changes the mix by itself; a proposal goes to BEFORE/AFTER; the footer says "Built into DINE. Works offline." or the proposal line — `ChatSheet::paint` — v4: KEEP copy
- `[control]` Close (top right) — `ChatSheet::close` — v4: same
- `[key]` Esc closes (the sheet's own handler plus MainView's Esc order) — `ChatSheet::keyPressed` — v4: Esc topmost order
- `[behaviour]` `takeFocus()` puts the caret in the box on open; `lookAndFeelChanged` re-styles it — `ChatSheet` — v4: same

### Appearance (ThemeSheet)

- `[binding]` View > Appearance > Customise Appearance… (620), Import a Theme… (621 opens the sheet + import), Show Themes Folder (622), a theme list (640+), Text size (660–662) — `MainView::handleCommand`, `getMenuForIndex` — v4: session menu "Appearance" + Appearance sheet
- `[control]` Theme rail: DINE's own first, then MINE; row name + note ("DINE's own"/"Yours"); click → `chooseTheme` → `Dine::applyTheme`, `ThemeStore::setChosenTheme` (only when persisting) — `ThemeSheet::ThemeRow`, `select` — v4: Appearance
- `[control]` Swatch grid grouped by `ThemeStore::tokens()` group; click opens a ColourSelector callout; every move → `Dine::setThemeColour` live in every window — `ThemeSheet::Swatch`, `colourEdited` — v4: same
- `[control]` Name box ("Name this theme"; built-ins prefill "<name> (mine)"); Return saves — `ThemeSheet::nameBox` — v4: same
- `[control]` Save → `ThemeStore::saveUser` (only colours that differ from the base; never overwrites a built-in); toast — `ThemeSheet::saveTheme` — v4: same
- `[control]` "Undo changes" (enabled when edited) → reselect — `ThemeSheet::resetTheme` — v4: same
- `[control]` Delete (user themes only) → `ThemeStore::removeUser`, back to the default + toast — `ThemeSheet::deleteTheme` — v4: same
- `[control]` Import… → `ThemeStore::importFile` (a broken file is refused with its reason) — `ThemeSheet::importTheme` — v4: same
- `[control]` Export… → complete standalone file (`ThemeStore::save`, no base) — `ThemeSheet::exportTheme` — v4: same
- `[control]` "Show folder" → reveals ~/Music/DINE/Themes — `ThemeSheet::revealFolder` — v4: same
- `[control]` Close; a click off the card closes — `ThemeSheet::closeButton`, `mouseUp` — v4: same
- `[behaviour]` Live sample strip (M/S/R/A keys, TUNE MIX filled, Keep, pan, meter, HEALTHY/CLIPPING chips, group tints); lighter scrim 0.5 because the console is the preview — `ThemeSheet::resized`, `paint` — v4: same
- `[behaviour]` Status note (built-in unchanged / changed / yours + file + "changed, not yet saved") — `ThemeSheet::paint` — v4: same
- `[callback]` onThemeChanged → updateChrome + `menu->menuItemsChanged()` (the View-menu tick follows) — `MainView::showThemes` — v4: KEEP
- `[rule]` `persisting=false` (snapshot tool) never writes the chosen theme — `ThemeSheet(bool)` — v4: KEEP
- `[behaviour]` applyThemeNamed works with or without the sheet open — `MainView::applyThemeNamed` — v4: KEEP

### Broadcast readiness sheet (BroadcastReadinessSheet)

- `[binding]` Sidebar row "Broadcast readiness" (Action::BroadcastReadiness, meta "checked/applicable") shown only when `broadcastReadinessApplies(purpose)`; otherwise refused with "Broadcast readiness is for Church Broadcast or Livestream…" and the sheet closed — `MainView::showBroadcastReadiness`, `updateChrome` — v4: no sidebar row in the brief → KEEP current sidebar row; nearest is the readiness pill → Ready sheet
- `[behaviour]` Opening creates the active record only if none exists (`editReadiness().ensureActive()` + `touch()`) — `BroadcastReadinessSheet::BroadcastReadinessSheet` — v4: KEEP
- `[control]` Tabs "This service" / "Past services (N)" — `checklistTab`, `historyTab`, `setMode` — v4: KEEP (Ready sheet)
- `[control]` Service name field ("Sunday morning") and "Checked by" field ("Your name"); Return or focus loss commits, Esc reverts; `rememberOperator` — `nameField`, `byField`, `commitMeta` — v4: KEEP
- `[control]` Item rows by group (caption = `readinessGroupName`): lamp (pending ring / done accent / problem warn / skip dash), label, guidance, "Now: <hint>" from `controller.readinessHints()` (warn when concerning, refreshed every 8 ticks), note, "Changed since" chip when `needsReview` — `buildChecklist`, `refresh`, `paintRows` — v4: Ready sheet rows (✓/! + Fix)
- `[control]` Done / Problem / Skip (Skip only when `allowsNotNeeded`); pressing the lit one returns it to Pending → `editReadiness().setItem` (active) or `setReadinessItem` (a past record being corrected) — `BroadcastReadinessSheet::answer` — v4: KEEP
- `[control]` "Add a note" / "Edit note" (on a problem or an existing note) → dialog "What did you hear, or what still needs doing?" Keep the note / Cancel — `askNote` — v4: KEEP
- `[control]` Shortcut links: InputLevels → "Check inputs", BroadcastOutput → "Outputs", RecoverySnapshot → "Mix history" (onOpenCheck/onOpenOutputs/onOpenHistory; MainView closes this sheet first) — `buildChecklist` — v4: the Ready sheet's "Fix action"
- `[control]` "Start over" → `resetActiveStatuses` (notes kept), toast — `startOver` — v4: KEEP
- `[control]` Finish → `finishActive`, toast "Kept in Past services. Nothing about the mix changed." — `finishService` — v4: KEEP
- `[control]` Reopen (finished) → `reopenActive` — `reopenButton` — v4: KEEP
- `[control]` "Start the next service" → `newService("Service <d Mon>", operator)` and focus the name field — `startNextService` — v4: KEEP
- `[control]` Past services list: summary note ("Most often a problem: … Most often left to do: …"), records with "N of M done", when/who/problems/pending, problems + notes, "Open" — `buildHistory` — v4: KEEP
- `[control]` Past record: "All services" back, "Correct it" ↔ "Done" (edits in place, never replaces the active one) — `backButton`, `correctButton` — v4: KEEP
- `[control]` Close (×) — `closeButton` — v4: same
- `[rule]` Confirmations only: marking never changes routing, gain, processing, mutes, solos or recording; "Ticks are what someone heard, not proof the stream is right." — header comment, `paint` — v4: KEEP copy
- `[rule]` The list rebuilds only when the fingerprint changes, and never inside its own button click (`later`) — `fingerprint`, `later` — v4: perf/safety rule
- `[binding]` `controller.flagReadiness(ReadinessChange::BroadcastDevice)` when the broadcast device changes (marks dependent items for review) — `MainView::chooseOutput` — v4: KEEP
- `[binding]` Test hooks `numItemRows`, `rebuildCount`, `setItemForTest` — `BroadcastReadinessSheet.h` — v4: KEEP

### Sheet host rules (MainView)

- `[key]` Esc: when `openSheetName()` is not empty (tunescope, check, history, readiness, appearance, channel, chat, export, choice) → `closeSheets()` closes all of them — `MainView::keyPressed`, `openSheetName`, `closeSheets` — v4: "Esc closes the topmost: menu → sheet → tune → chat" (today Esc closes every sheet at once, the chat included)
- `[behaviour]` Each sheet's onClose resets it through `callAsync` and a SafePointer (never inside its own click) — `MainView::showCheck` etc. — v4: KEEP
- `[behaviour]` The tick refreshes channelSheet, checkSheet, readinessSheet, chatSheet; MixPage refreshes ReferenceSheet — `MainView::timerCallback` — v4: shared clock
- `[behaviour]` Sheets fill the workspace column; chat takes a right column — `MainView::resized` — v4: layout
- `[rule]` No workspace guide card while a sheet or the tour is open — `MainView::maybeShowGuide` — v4: KEEP
- `[menu]` Setup popover (toolbar session button): status rows Audio device / Inputs / Purpose and sound / Recording destination; New, Open, Save, Save as, Import multitrack folder, Add a reference mix, Save input mapping, Export stereo mix (WAV), Export multitrack, Open setup, Rename or fix the inputs, Getting started — `MainView::setupPopover` — v4: toolbar session menu

---

---

## 5. The backend surface the UI binds to

Scope: every backend call app/ui makes today, every persisted field, how live readouts are read, and what the v4
brief (docs/design/v4/HANDOFF.md, written as "DLIVE v3") asks for that has no backend. Generated 2026-10-06 from
`main` @ 8b9f4cf by scanning app/ui for receivers `controller.`, `services.`, `daw()`, `getTransport()`,
`getRecorder()`, `getEngine()` and `Class::` statics; "called from" names the enclosing function (nested
classes shown as `Page::Inner`; a few lambda sites attribute to the enclosing constructor).

Ground rules this section implies for the rebuild:
- The UI owns two handles: `MixController& controller` (no JUCE, message thread only, plain data out) and
  `AppServices& services` (app/ui/AppServices.h; real impl in app/Main.cpp over AudioHost / SessionStore /
  SessionAutosave / SampleLibrary / MonitorDevice; fake impl in app/Tools/AppSnapshots.cpp so every screen renders
  headless). A rebuilt UI must keep talking only through these two; nothing in app/ui touches AudioHost,
  Recorder internals, SessionStore::save or SessionAutosave directly.
- Every mix change goes through a MixController setter (LIVE SAFE is enforced there, not in the UI). Every
  timeline change edits `daw().getProject()` in place, then `daw().refresh()` (republish to audio thread) and
  `services.touchSession()`. The UI never saves on its own: `MainView::timerCallback` follows
  `sessionRevision()/sessionMilestone()` and calls `autosaveNow()` (SESSION-STATE.md 5.3).
- MainView's public contract to app/Main.cpp must survive the rebuild: `MainView (MixController&, AppServices&)`,
  `onToolbarPressed`, `onToolbarDoubleClicked`, `getMenuModel()`, `showToast`, `showPage (Page)`,
  `getDevicePage().refresh()`, `explainMicrophone (MicrophoneAsk)`, `offerRecovery (RecoveryOffer)`,
  `sessionReplaced()`, `isExporting()`, `stopExportAndWait (ms)`; plus `setGuidesUsed`, `setAutoTutorial`,
  `*ForSnapshot` hooks used by dine_ui_snapshots and the reachability test (app/Tests/ReachabilityTests.cpp
  reads `commandForKey`, `openSheetName`, `isSidebarShown`, `isSoloBarShown`).
- Callbacks the controller pushes into the UI: `controller.onMessage` (every refusal/notice -> toast; set in
  MainView ctor) and `controller.onUsage` (telemetry, wired in Main.cpp).

### 1. Backend calls used by the UI

### MixController (app/native/MixController.h) - the UI holds `MixController& controller`
- `MixController::abortTuneMix` — cancel the running listen (mix/channel/group) — app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::ChannelTuneSheet, app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::mouseUp, app/ui/MixPage.cpp:ListenSheet, app/ui/MixPage.cpp:MixPage::pressLiveTune, app/ui/MixPage.cpp:MixPage::pressTune (5)
- `MixController::anyFxSolo` — any FX return soloed — app/ui/LivePage.cpp:LivePage::GroupTile, app/ui/LivePage.cpp:refresh (2)
- `MixController::askBuddy` — Mix Buddy question; never changes the mix — app/ui/ChatSheet.cpp:ChatSheet::ask (1)
- `MixController::askForChange` — Mix Buddy "Propose it" -> TUNE LIVE MIX proposal — app/ui/MainView.cpp:MainView::performBuddyAction (1)
- `MixController::auditionSample` — HEAR IT: play strip sample once to solo path — app/ui/ChainEditor.cpp:ChainEditor::buildControls (1)
- `MixController::busHeard` — listen heard anything on a group — app/ui/MixPage.cpp:MixPage::refresh, app/ui/MixPage.cpp:paint (2)
- `MixController::canRedoMix` — redo available — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:MainView::redoTarget, app/ui/MainView.cpp:getMenuForIndex, app/ui/MixPage.cpp:MixPage::MixPage, app/ui/MixPage.cpp:MixPage::refresh (5)
- `MixController::canTryAnotherMix` — TRY ANOTHER MIX enabled — app/ui/MainView.cpp:getMenuForIndex, app/ui/MixPage.cpp:refresh (2)
- `MixController::canUndoMix` — undo available — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:MainView::undoTarget, app/ui/MainView.cpp:getMenuForIndex, app/ui/MixPage.cpp:MixPage::MixPage, app/ui/MixPage.cpp:MixPage::refresh (5)
- `MixController::clearPlanSelection` — KEEP SOME: clear selection — app/ui/MixPage.cpp:applySelection (1)
- `MixController::clearReference` — drop reference mix — app/ui/ReferenceSheet.cpp:ReferenceSheet::ReferenceSheet (1)
- `MixController::clearSolos` — clear every solo (SOLO pill click) — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::handleCommand, app/ui/MixerPage.cpp:MixerPage::MixerPage (4)
- `MixController::editReadiness` — mutable BroadcastReadiness (UI must touch() after) — app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::answer, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::commitMeta, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::finishService, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::shownMutable, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::startNextService, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::startOver …+1 (7)
- `MixController::flagReadiness` — mark a readiness item for review after a change — app/ui/MainView.cpp:MainView::chooseOutput (1)
- `MixController::getAutoMix` — Share the mics on? — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/LivePage.cpp:LivePage::refresh, app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:getMenuForIndex (4)
- `MixController::getAutopilot` — AutopilotState (on/holding/moved dB/last why) — app/ui/LivePage.cpp:LivePage::refresh (1)
- `MixController::getAutopilotLimits` — Autopilot bounds for the explanation text — app/ui/LivePage.cpp:LivePage::autopilotText (1)
- `MixController::getBase` — MixParameters audible now (kept, or plan before/after while previewing) - the source of every fader/mute/solo/chain value drawn — app/ui/AdvancedPage.cpp:AdvancedPage::AdvancedPage, app/ui/AdvancedPage.cpp:AdvancedPage::refresh, app/ui/AdvancedPage.cpp:AdvancedPage::refreshKeys, app/ui/AdvancedPage.cpp:refresh, app/ui/AdvancedPage.cpp:reseed, app/ui/ChainEditor.cpp:ChainEditor::read …+19 (25)
- `MixController::getChat` — Mix Buddy transcript — app/ui/ChatSheet.cpp:ChatSheet::refresh, app/ui/ChatSheet.cpp:layout (2)
- `MixController::getCheckpoints` — mix-history checkpoints — app/ui/HistorySheet.cpp:HistorySheet::rebuild, app/ui/HistorySheet.cpp:HistorySheet::refresh, app/ui/LivePage.cpp:LivePage::refresh, app/ui/MixPage.cpp:MixPage::refresh (4)
- `MixController::getCompare` — BEFORE/AFTER state — app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::updateControls, app/ui/ChatSheet.cpp:ChatSheet::ChatSheet, app/ui/ChatSheet.cpp:ChatSheet::updateControls, app/ui/MixPage.cpp:applySelection, app/ui/MixPage.cpp:refresh (5)
- `MixController::getDelivery` — loudness target enum — app/ui/MainView.cpp:getMenuForIndex, app/ui/MixPage.cpp:MixPage::MixPage, app/ui/MixPage.cpp:MixPage::refreshMaster, app/ui/SetupPages.cpp:PurposePage::PurposePage (4)
- `MixController::getEngine` — const MixEngine& - meters, strips/buses/fx processors, used-bus flags — app/ui/AdvancedPage.cpp:AdvancedPage::rebuild, app/ui/AdvancedPage.cpp:AdvancedPage::refresh, app/ui/ChainEditor.cpp:ChainEditor::refresh, app/ui/ChainEditor.cpp:ChainEditor::updateViews, app/ui/CheckSheet.cpp:CheckSheet::refresh, app/ui/CheckSheet.cpp:CheckSheet::resetClips …+16 (22)
- `MixController::getFavourite` — favourite MixScene by index — app/ui/FavouritesPage.cpp:FavouritesPage::rebuild, app/ui/FavouritesPage.cpp:FavouritesPage::refresh, app/ui/FavouritesPage.cpp:paint, app/ui/FavouritesPage.cpp:subtitle, app/ui/HistorySheet.cpp:HistorySheet::rebuild, app/ui/MixPage.cpp:MixPage::refreshTuneButton (6)
- `MixController::getFocusInput` — pinned focal input — app/ui/MixPage.cpp:MixPage::refresh (1)
- `MixController::getGraph` — RoutingGraph: strip list (names, roles, buses, inputs), bus/fx usage — app/ui/AdvancedPage.cpp:AdvancedPage::paintHead, app/ui/AdvancedPage.cpp:AdvancedPage::rebuild, app/ui/AdvancedPage.cpp:AdvancedPage::refresh, app/ui/AdvancedPage.cpp:reseed, app/ui/ChainEditor.cpp:ChainEditor::build, app/ui/ChainEditor.cpp:ChainEditor::buildControls …+20 (26)
- `MixController::getInputAdvice` — gain-staging verdict per strip (Level enum incl. Digital/Clipping/Low, consoleMoveDb, headline, detail) — app/ui/AdvancedPage.cpp:AdvancedPage::refresh, app/ui/AdvancedPage.cpp:AdvancedPage::showSelection, app/ui/LivePage.cpp:LivePage::refresh, app/ui/MixerPage.cpp:refresh, app/ui/TracksPage.cpp:TracksPage::refresh (5)
- `MixController::getKept` — kept mix without macros — app/ui/MainView.cpp:MainView::performBuddyAction, app/ui/MixerPage.cpp:showMenu, app/ui/TracksPage.cpp:TracksPage::headerMenu (3)
- `MixController::getLastListen` — last MixCapture::Result — app/ui/ReferenceSheet.cpp:ReferenceSheet::preview (1)
- `MixController::getLastTuneScope` — words for the last tune scope — app/ui/MixPage.cpp:paint (1)
- `MixController::getListenProgress` — 0..1 listen progress (countdown ring) — app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::paint, app/ui/MixPage.cpp:paint (2)
- `MixController::getListenSeconds` — requested listen length — app/ui/MixPage.cpp:paint (1)
- `MixController::getLiveSafePolicy` — LIVE SAFE limits (macro fence, steps) — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/MixPage.cpp:MixPage::syncMacros (2)
- `MixController::getMacros` — five macro values — app/ui/MixPage.cpp:MixPage::syncMacros (1)
- `MixController::getMasterLoudness` — MasterLoudness: integrated/short/momentary LUFS, true peak, limiter GR, target, headroom — app/ui/LivePage.cpp:LivePage::refresh, app/ui/MainView.cpp:update, app/ui/MixPage.cpp:MixPage::refreshMaster, app/ui/MixerPage.cpp:refresh (4)
- `MixController::getMixHealthNotes` — health notes in plain words — app/ui/MixPage.cpp:MixPage::refresh, app/ui/MixPage.cpp:layoutFor (2)
- `MixController::getMixHealthPercent` — health score 0..100 — app/ui/MixPage.cpp:MixPage::refresh (1)
- `MixController::getMonitor` — MonitorState (solo mode/point/gain/dim/mute/source) — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/LivePage.cpp:LivePage::refresh, app/ui/LivePage.cpp:LivePage::refreshMonitor, app/ui/MainView.cpp:update, app/ui/OutputsSheet.cpp:OutputsSheet::paint, app/ui/OutputsSheet.cpp:OutputsSheet::showSoloDeviceMenu …+1 (7)
- `MixController::getOutputFeeds` — OutputFeeds (feed rows) — app/ui/MixerPage.cpp:MixerPage::rebuild, app/ui/OutputsSheet.cpp:OutputsSheet::addFeed, app/ui/OutputsSheet.cpp:OutputsSheet::cardBounds, app/ui/OutputsSheet.cpp:OutputsSheet::paint, app/ui/OutputsSheet.cpp:OutputsSheet::refresh, app/ui/OutputsSheet.cpp:OutputsSheet::removeFeed (6)
- `MixController::getPlan` — MixPlan on preview (per-strip changes, explanations, confidence) — app/ui/AdvancedPage.cpp:AdvancedPage::showSelection, app/ui/AdvancedPage.cpp:AdvancedPage::tunedLabel, app/ui/AdvancedPage.cpp:sentences, app/ui/ChainEditor.cpp:ChainEditor::plannedChannel, app/ui/ChainEditor.cpp:ChainEditor::plannedStrip, app/ui/ChainEditor.cpp:ChainEditor::refresh …+11 (17)
- `MixController::getReadiness` — BroadcastReadiness checklist — app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::buildHistory, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::fingerprint, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::paint, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::rebuild, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::shown, app/ui/BroadcastReadinessSheet.cpp:controller …+1 (7)
- `MixController::getReference` — ReferenceProfile — app/ui/FavouritesPage.cpp:FavouritesPage::refresh, app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:getMenuForIndex, app/ui/MixPage.cpp:MixPage::refresh, app/ui/MixPage.cpp:MixPage::refreshTuneButton, app/ui/ReferenceSheet.cpp:ReferenceSheet::ReferenceSheet …+2 (8)
- `MixController::getSampleBanks` — SampleBankTable (sound list for the Sample stage) — app/ui/ChainEditor.cpp:ChainEditor::build (1)
- `MixController::getSampleRate` — engine sample rate — app/ui/ChainEditor.cpp:ChainEditor::refresh (1)
- `MixController::getScene` — scene slot — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/LivePage.cpp:LivePage::refreshScenes (2)
- `MixController::getSession` — MixSession: name, profile, purpose, delivery, voicing, inputs[] (name/role/icon/inputA/B/enabled/focus), speechPriority, autoMix — app/ui/CheckSheet.cpp:CheckSheet::CheckSheet, app/ui/CheckSheet.cpp:CheckSheet::refresh, app/ui/ExportSheet.cpp:ExportSheet::paint, app/ui/ExportSheet.cpp:ExportSheet::whatNote, app/ui/FavouritesPage.cpp:FavouritesPage::refresh, app/ui/FavouritesPage.cpp:subtitle …+53 (59)
- `MixController::getSoloed` — list of soloed items by name (SOLO pill) — app/ui/MainView.cpp:MainView::refreshSoloPill (1)
- `MixController::getSpeechPriority` — speech priority on? — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/LivePage.cpp:LivePage::refresh, app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:getMenuForIndex (4)
- `MixController::getStage` — Setup/Ready/Listening/Planning/Preview/Mixed — app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::paint, app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::previewing, app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::refresh, app/ui/MainView.cpp:MainView::tuneChannel, app/ui/MixPage.cpp:MixPage::refresh, app/ui/MixPage.cpp:MixPage::refreshTuneButton …+2 (8)
- `MixController::getStatusText` — listen status sentence — app/ui/MixPage.cpp:MixPage::refresh (1)
- `MixController::getStripHistory` — per-strip tune/hand-edit records ("who set this", trail, Back to tuned) — app/ui/AdvancedPage.cpp:AdvancedPage::AdvancedPage, app/ui/AdvancedPage.cpp:AdvancedPage::historyViews, app/ui/AdvancedPage.cpp:AdvancedPage::refresh, app/ui/AdvancedPage.cpp:AdvancedPage::tunedLabel, app/ui/AdvancedPage.cpp:putBackLabel, app/ui/ChainEditor.cpp:ChainEditor::lastTuneClock (6)
- `MixController::getStripLink` — link group of a strip — app/ui/MixerPage.cpp:showMenu, app/ui/TracksPage.cpp:TracksPage::getTooltip, app/ui/TracksPage.cpp:TracksPage::headerMenu (3)
- `MixController::getTuneCount` — how many tunes kept — app/ui/AdvancedPage.cpp:AdvancedPage::refresh, app/ui/MixPage.cpp:MixPage::refresh, app/ui/MixPage.cpp:MixPage::refreshTuneButton, app/ui/MixPage.cpp:contentStamp, app/ui/MixPage.cpp:paint, app/ui/MixPage.cpp:rebuildChips …+2 (8)
- `MixController::getTuneLive` — TuneLiveCoordinator (state machine for TUNE LIVE MIX steps) — app/ui/MixPage.cpp:bullets, app/ui/MixPage.cpp:contentStamp, app/ui/MixPage.cpp:paint, app/ui/MixPage.cpp:tick (4)
- `MixController::getTuneLiveStatus` — TUNE LIVE status sentence — app/ui/MixPage.cpp:paint (1)
- `MixController::getTuningStrip` — strip being tuned (-1 = mix) — app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::previewing, app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::refresh (2)
- `MixController::getVoicing` — master voicing enum — app/ui/MainView.cpp:getMenuForIndex, app/ui/MixPage.cpp:MixPage::MixPage, app/ui/MixPage.cpp:MixPage::refreshMaster (3)
- `MixController::hasBuddyProposal` — Mix Buddy proposal waiting — app/ui/ChatSheet.cpp:ChatSheet::paint, app/ui/ChatSheet.cpp:ChatSheet::refresh, app/ui/ChatSheet.cpp:ChatSheet::updateControls (3)
- `MixController::hasListened` — a listen exists — app/ui/MainView.cpp:MainView::handleCommand, app/ui/ReferenceSheet.cpp:ReferenceSheet::ReferenceSheet, app/ui/ReferenceSheet.cpp:ReferenceSheet::drawBalance, app/ui/ReferenceSheet.cpp:ReferenceSheet::preview, app/ui/ReferenceSheet.cpp:ReferenceSheet::updateControls (5)
- `MixController::hasMonitorOutput` — solo has somewhere to go — app/ui/LivePage.cpp:LivePage::refresh, app/ui/MainView.cpp:update (2)
- `MixController::hasPlan` — plan exists — app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::previewing, app/ui/MixPage.cpp:MixPage::refresh (2)
- `MixController::hasReference` — reference set — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:getMenuForIndex, app/ui/MixPage.cpp:MixPage::refresh, app/ui/MixPage.cpp:MixPage::refreshTuneButton, app/ui/ReferenceSheet.cpp:ReferenceSheet::preview, app/ui/ReferenceSheet.cpp:ReferenceSheet::state (6)
- `MixController::inputOfStrip` — strip -> session input index — app/ui/MixerPage.cpp:showMenu (1)
- `MixController::isAutopilotOn` — Autopilot on — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:MainView::updateChrome, app/ui/MainView.cpp:getMenuForIndex, app/ui/MainView.cpp:update (5)
- `MixController::isBroadcastDimmed` — DIM on — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:MainView::updateChrome, app/ui/MainView.cpp:getMenuForIndex, app/ui/MainView.cpp:update (5)
- `MixController::isBroadcastMuted` — MUTE on — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:MainView::updateChrome, app/ui/MainView.cpp:getMenuForIndex, app/ui/MainView.cpp:update (5)
- `MixController::isBypassed` — BYPASS on (faders lock) — app/ui/AdvancedPage.cpp:AdvancedPage::refresh, app/ui/AdvancedPage.cpp:AdvancedPage::refreshKeys, app/ui/ChainEditor.cpp:ChainEditor::buildControls, app/ui/ChainEditor.cpp:ChainEditor::commit, app/ui/ChainEditor.cpp:ChainEditor::refresh, app/ui/MainView.cpp:MainView::MainView …+13 (19)
- `MixController::isChatBusy` — Mix Buddy request running — app/ui/ChatSheet.cpp:ChatSheet::refresh, app/ui/ChatSheet.cpp:ChatSheet::updateControls (2)
- `MixController::isListening` — listen running — app/ui/MainView.cpp:MainView::tuneChannel, app/ui/MixPage.cpp:MixPage::pressLiveTune, app/ui/MixPage.cpp:MixPage::pressTune, app/ui/MixPage.cpp:MixPage::refresh (4)
- `MixController::isLiveSafe` — LIVE SAFE on — app/ui/MainView.cpp:MainView::redoTarget, app/ui/MainView.cpp:MainView::undoHere, app/ui/MainView.cpp:MainView::undoTarget, app/ui/MainView.cpp:getMenuForIndex, app/ui/MixPage.cpp:MixPage::refreshMaster, app/ui/MixerPage.cpp:showMenu …+2 (8)
- `MixController::isPrepared` — engine graph ready (meters valid) — app/ui/AdvancedPage.cpp:AdvancedPage::rebuild, app/ui/AdvancedPage.cpp:AdvancedPage::refresh, app/ui/AdvancedPage.cpp:refresh, app/ui/AdvancedPage.cpp:reseed, app/ui/ChainEditor.cpp:ChainEditor::refresh, app/ui/ChainEditor.cpp:ChainEditor::updateViews …+33 (39)
- `MixController::isTuningChannel` — TUNE CHANNEL scope — app/ui/MixPage.cpp:MixPage::refresh (1)
- `MixController::isTuningLive` — TUNE LIVE MIX running — app/ui/MainView.cpp:MainView::timerCallback, app/ui/MainView.cpp:MainView::updateChrome, app/ui/MixPage.cpp:MixPage::pressLiveTune, app/ui/MixPage.cpp:MixPage::pressTune, app/ui/MixPage.cpp:MixPage::refresh, app/ui/MixPage.cpp:MixPage::refreshTuneButton …+3 (9)
- `MixController::isVoiceChannel` — strip is a voice mic (speak/sing) — app/ui/MixPage.cpp:MixPage::rebuildVoices, app/ui/MixerPage.cpp:showMenu, app/ui/TracksPage.cpp:TracksPage::headerMenu (3)
- `MixController::isWaitingForBand` — listen armed, waiting for signal — app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::paint, app/ui/MixPage.cpp:paint (2)
- `MixController::keepPlan` — KEEP — app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::ChannelTuneSheet, app/ui/ChatSheet.cpp:ChatSheet::ChatSheet, app/ui/MixPage.cpp:MixPage::ResultSheet (3)
- `MixController::keepScene` — store scene slot — app/ui/LivePage.cpp:LivePage::LivePage (1)
- `MixController::linkStrips` — link faders — app/ui/MixerPage.cpp:showMenu, app/ui/TracksPage.cpp:TracksPage::headerMenu (2)
- `MixController::linkedNames` — names of linked strips — app/ui/MixerPage.cpp:refresh, app/ui/MixerPage.cpp:showMenu, app/ui/TracksPage.cpp:TracksPage::getTooltip, app/ui/TracksPage.cpp:TracksPage::headerMenu (4)
- `MixController::linkedWith` — other members of link — app/ui/TracksPage.cpp:TracksPage::dragFader (1)
- `MixController::liveCaptureAdvice` — gain verdict from role + held peak (no plan needed) - Inputs table/CHECK INPUTS — app/ui/CheckSheet.cpp:CheckSheet::refresh, app/ui/SetupPages.cpp:AssignPage::inputsNeedingGain, app/ui/SetupPages.cpp:paint (3)
- `MixController::macroRange` — LIVE SAFE macro fence — app/ui/MixPage.cpp:MixPage::syncMacros (1)
- `MixController::markFavourite` — save current mix as favourite — app/ui/FavouritesPage.cpp:FavouritesPage::markCurrent, app/ui/HistorySheet.cpp:HistorySheet::askForFavouriteName (2)
- `MixController::needsReconfigure` — graph stale vs document — app/ui/MainView.cpp:MainView::showPage (1)
- `MixController::nowMs` — controller clock (undo ordering) — app/ui/TracksPage.cpp:TracksPage::captureEdit (1)
- `MixController::numFavourites` — favourites count — app/ui/FavouritesPage.cpp:FavouritesPage::markCurrent, app/ui/FavouritesPage.cpp:FavouritesPage::rebuild, app/ui/FavouritesPage.cpp:FavouritesPage::refresh, app/ui/HistorySheet.cpp:HistorySheet::rebuild, app/ui/HistorySheet.cpp:HistorySheet::refresh, app/ui/MainView.cpp:MainView::updateChrome …+1 (7)
- `MixController::numSoloed` — count soloed — app/ui/LivePage.cpp:LivePage::refresh, app/ui/LivePage.cpp:LivePage::refreshMonitor, app/ui/MainView.cpp:getMenuForIndex, app/ui/MixerPage.cpp:MixerPage::updateControls (4)
- `MixController::poll` — message-thread tick (~30 Hz): advances listen/plan, Autopilot, checkpoints — app/ui/MainView.cpp:MainView::timerCallback (1)
- `MixController::previewLoudnessMove` — what Raise-to-target would do — app/ui/MainView.cpp:getMenuForIndex, app/ui/MixPage.cpp:MixPage::refreshMaster (2)
- `MixController::raiseLoudnessToTarget` — raise master to delivery target — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MixPage.cpp:MixPage::MixPage (2)
- `MixController::readinessHints` — live observations beside readiness items — app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::buildChecklist, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::refresh (2)
- `MixController::recallFavourite` — recall favourite — app/ui/FavouritesPage.cpp:FavouritesPage::rebuild, app/ui/HistorySheet.cpp:HistorySheet::rebuild (2)
- `MixController::recallScene` — recall scene — app/ui/LivePage.cpp:LivePage::LivePage (1)
- `MixController::redoMix` — redo — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:MainView::redoHere, app/ui/MixPage.cpp:MixPage::MixPage (3)
- `MixController::redoMixLabel` — redo label — app/ui/MainView.cpp:MainView::redoTarget, app/ui/MainView.cpp:getMenuForIndex (2)
- `MixController::removeFavourite` — delete favourite — app/ui/HistorySheet.cpp:HistorySheet::rebuild (1)
- `MixController::resetMacros` — macros to 50 — app/ui/MixPage.cpp:MixPage::centreMacroPads (1)
- `MixController::resetMixToRaw` — Reset to raw (checkpoint first; refused under LIVE SAFE) — app/ui/MainView.cpp:MainView::resetMixToRaw (1)
- `MixController::restoreCheckpoint` — go back to a checkpoint — app/ui/HistorySheet.cpp:HistorySheet::rebuild (1)
- `MixController::restoreStripTune` — put back an earlier strip record — app/ui/AdvancedPage.cpp:AdvancedPage::AdvancedPage (1)
- `MixController::revertPlan` — REVERT — app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::ChannelTuneSheet, app/ui/ChatSheet.cpp:ChatSheet::ChatSheet, app/ui/MixPage.cpp:MixPage::ResultSheet (3)
- `MixController::roleForJob` — role for speak/sing job on strip — app/ui/MixPage.cpp:MixPage::rebuildVoices, app/ui/MixerPage.cpp:showMenu, app/ui/TracksPage.cpp:TracksPage::headerMenu (3)
- `MixController::setAutoMix` — Share the mics on/off — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/MainView.cpp:MainView::handleCommand (2)
- `MixController::setAutopilot` — engage/disengage Autopilot — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/MainView.cpp:MainView::handleCommand (2)
- `MixController::setBroadcastDim` — DIM key — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::handleCommand (2)
- `MixController::setBroadcastMute` — MUTE key — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::handleCommand (2)
- `MixController::setBusChannel` — group chain edit — app/ui/ChainEditor.cpp:ChainEditor::write (1)
- `MixController::setBusFader` — group fader — app/ui/LivePage.cpp:LivePage::GroupTile, app/ui/MixPage.cpp:MixPage::GroupTile, app/ui/MixerPage.cpp:MixerPage::Strip (3)
- `MixController::setBusMute` — group mute — app/ui/AdvancedPage.cpp:AdvancedPage::AdvancedPage, app/ui/LivePage.cpp:LivePage::GroupTile, app/ui/MixPage.cpp:MixPage::GroupTile, app/ui/MixerPage.cpp:MixerPage::Strip (4)
- `MixController::setBusSolo` — group solo — app/ui/AdvancedPage.cpp:AdvancedPage::AdvancedPage, app/ui/LivePage.cpp:LivePage::GroupTile, app/ui/MixPage.cpp:MixPage::GroupTile, app/ui/MixerPage.cpp:MixerPage::Strip, app/ui/MixerPage.cpp:showMenu (5)
- `MixController::setBypass` — BYPASS — app/ui/MainView.cpp:MainView::setBypass (1)
- `MixController::setCompare` — BEFORE/AFTER — app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::ChannelTuneSheet, app/ui/ChatSheet.cpp:ChatSheet::ChatSheet, app/ui/MixPage.cpp:MixPage::ResultSheet, app/ui/MixPage.cpp:applySelection (4)
- `MixController::setDelivery` — loudness target — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MixPage.cpp:MixPage::MixPage, app/ui/SetupPages.cpp:PurposePage::PurposePage (3)
- `MixController::setFocusInput` — pin focal input — app/ui/MixPage.cpp:MixPage::rebuildRail (1)
- `MixController::setFxMute` — FX returns group mute — app/ui/LivePage.cpp:LivePage::GroupTile, app/ui/MixPage.cpp:MixPage::GroupTile (2)
- `MixController::setFxReturn` — FX returns group fader — app/ui/LivePage.cpp:LivePage::GroupTile, app/ui/MixPage.cpp:MixPage::GroupTile (2)
- `MixController::setFxSlotMute` — one return mute — app/ui/LivePage.cpp:LivePage::GroupTile, app/ui/MixPage.cpp:MixPage::GroupTile, app/ui/MixerPage.cpp:MixerPage::Strip (3)
- `MixController::setFxSlotReturn` — one return level — app/ui/LivePage.cpp:LivePage::GroupTile, app/ui/MixPage.cpp:MixPage::GroupTile, app/ui/MixerPage.cpp:MixerPage::Strip (3)
- `MixController::setFxSolo` — one return solo — app/ui/LivePage.cpp:LivePage::GroupTile, app/ui/MixPage.cpp:MixPage::GroupTile, app/ui/MixerPage.cpp:MixerPage::Strip (3)
- `MixController::setFxSoloAll` — solo all returns — app/ui/LivePage.cpp:LivePage::GroupTile (1)
- `MixController::setInputIcon` — input icon — app/ui/TracksPage.cpp:TracksPage::setTrackIcon (1)
- `MixController::setInputName` — rename input (no rebuild) — app/ui/TracksPage.cpp:TracksPage::matchNamesToClips, app/ui/TracksPage.cpp:TracksPage::setTrackName (2)
- `MixController::setInputRole` — what it is (rebuilds graph; LIVE SAFE refuses) — app/ui/MixPage.cpp:MixPage::rebuildVoices, app/ui/MixerPage.cpp:showMenu, app/ui/TracksPage.cpp:TracksPage::headerMenu (3)
- `MixController::setMacro` — macro value — app/ui/MixPage.cpp:MixPage::MixPage, app/ui/MixPage.cpp:MixPage::setMacroValue (2)
- `MixController::setMonitorDim` — listen dim — app/ui/LivePage.cpp:LivePage::LivePage (1)
- `MixController::setMonitorGain` — listen level — app/ui/LivePage.cpp:LivePage::LivePage (1)
- `MixController::setOutputFeeds` — write feed rows — app/ui/OutputsSheet.cpp:OutputsSheet::addFeed, app/ui/OutputsSheet.cpp:OutputsSheet::commit, app/ui/OutputsSheet.cpp:OutputsSheet::removeFeed (3)
- `MixController::setPlanSelection` — KEEP SOME selection — app/ui/MixPage.cpp:applySelection (1)
- `MixController::setProfile` — style profile — app/ui/SetupPages.cpp:PurposePage::PurposePage (1)
- `MixController::setPurpose` — purpose — app/ui/SetupPages.cpp:PurposePage::PurposePage (1)
- `MixController::setReasoningProvider` — cloud/offline mix engineer (Help menu) — app/ui/MainView.cpp:MainView::handleCommand (1)
- `MixController::setReference` — set reference profile — app/ui/ReferenceSheet.cpp:ReferenceSheet::refresh (1)
- `MixController::setSession` — replace assignments (rebuild; host then reconfigure()) — app/ui/MainView.cpp:MainView::applyInputMapping, app/ui/SetupPages.cpp:AssignPage::commit, app/ui/TracksPage.cpp:TracksPage::addTrack, app/ui/TracksPage.cpp:TracksPage::moveTrack, app/ui/TracksPage.cpp:TracksPage::setTrackSource (5)
- `MixController::setSoloMode` — solo mode — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/OutputsSheet.cpp:OutputsSheet::showSoloDeviceMenu (2)
- `MixController::setSoloPoint` — PFL/AFL point — app/ui/LivePage.cpp:LivePage::LivePage (1)
- `MixController::setSpeechPriority` — speech priority — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/MainView.cpp:MainView::handleCommand (2)
- `MixController::setStripChannel` — whole ChannelParameters for a strip (Inspector stage edits, Simple knobs) — app/ui/AdvancedPage.cpp:SimplePanel, app/ui/AdvancedPage.cpp:setMacro, app/ui/ChainEditor.cpp:ChainEditor::write (3)
- `MixController::setStripEffects` — EFFECTS on/off per mic — app/ui/AdvancedPage.cpp:SimplePanel, app/ui/MixerPage.cpp:MixerPage::Strip (2)
- `MixController::setStripFader` — strip fader (+link; Cmd = alone) — app/ui/AdvancedPage.cpp:SimplePanel, app/ui/MixerPage.cpp:MixerPage::Strip, app/ui/TracksPage.cpp:TracksPage::dragFader, app/ui/TracksPage.cpp:TracksPage::mouseDoubleClick (4)
- `MixController::setStripMute` — strip mute — app/ui/AdvancedPage.cpp:AdvancedPage::AdvancedPage, app/ui/MixerPage.cpp:MixerPage::Strip, app/ui/TracksPage.cpp:TracksPage::mouseDown (3)
- `MixController::setStripPan` — strip pan — app/ui/MixerPage.cpp:MixerPage::Strip (1)
- `MixController::setStripSend` — strip send level — app/ui/ChainEditor.cpp:ChainEditor::buildControls, app/ui/ChainEditor.cpp:ChainEditor::revertStage (2)
- `MixController::setStripSolo` — strip solo — app/ui/AdvancedPage.cpp:AdvancedPage::AdvancedPage, app/ui/MainView.cpp:MainView::performBuddyAction, app/ui/MixerPage.cpp:MixerPage::Strip, app/ui/TracksPage.cpp:TracksPage::mouseDown (4)
- `MixController::setVoicing` — master voicing — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MixPage.cpp:MixPage::MixPage (2)
- `MixController::startReferenceMatch` — MATCH TO REFERENCE — app/ui/MainView.cpp:MainView::handleCommand, app/ui/ReferenceSheet.cpp:ReferenceSheet::ReferenceSheet (2)
- `MixController::startTuneBus` — TUNE <group> — app/ui/MixPage.cpp:MixPage::MixPage, app/ui/MixPage.cpp:begin (2)
- `MixController::startTuneChannel` — TUNE CHANNEL — app/ui/MainView.cpp:MainView::tuneChannel (1)
- `MixController::startTuneLiveMix` — TUNE LIVE MIX — app/ui/MixPage.cpp:MixPage::pressLiveTune (1)
- `MixController::startTuneMix` — TUNE MIX — app/ui/MixPage.cpp:MixPage::pressTune, app/ui/MixPage.cpp:begin (2)
- `MixController::startTuneStrips` — TUNE some channels — app/ui/MixPage.cpp:begin (1)
- `MixController::stripCanHaveEffects` — show EFFECTS switch — app/ui/AdvancedPage.cpp:reseed, app/ui/MixerPage.cpp:MixerPage::Strip, app/ui/MixerPage.cpp:layoutColumn, app/ui/MixerPage.cpp:layoutRow (4)
- `MixController::stripEffectsOn` — EFFECTS state — app/ui/AdvancedPage.cpp:refresh, app/ui/MixerPage.cpp:MixerPage::Strip, app/ui/MixerPage.cpp:refresh (3)
- `MixController::stripHeard` — strip heard in listen — app/ui/ChannelTuneSheet.cpp:ChannelTuneSheet::paint (1)
- `MixController::touch` — bump session revision (autosave) — app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::answer, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::askNote, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::commitMeta, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::finishService, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::startNextService, app/ui/BroadcastReadinessSheet.cpp:BroadcastReadinessSheet::startOver …+1 (7)
- `MixController::tryAnotherMix` — TRY ANOTHER MIX — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MixPage.cpp:MixPage::ResultSheet (2)
- `MixController::undoMix` — undo — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:MainView::undoHere, app/ui/MixPage.cpp:MixPage::MixPage (3)
- `MixController::undoMixAtMs` — time of last mix change (undo arbitration with timeline) — app/ui/MainView.cpp:MainView::undoTarget (1)
- `MixController::undoMixLabel` — undo label — app/ui/MainView.cpp:MainView::undoTarget, app/ui/MainView.cpp:getMenuForIndex (2)
- `MixController::unlinkStrip` — unlink — app/ui/MixerPage.cpp:showMenu, app/ui/TracksPage.cpp:TracksPage::headerMenu (2)
- `MixController::useFavouriteAsReference` — aim at a favourite — app/ui/FavouritesPage.cpp:FavouritesPage::rebuild, app/ui/HistorySheet.cpp:HistorySheet::rebuild (2)

### MixEngine via `controller.getEngine()` (src/Mix/MixEngine.h) - read-only meters
- `MixEngine::clearConverterClips` — reset converter clip latches (CHECK INPUTS) — app/ui/CheckSheet.cpp:CheckSheet::resetClips (1)
- `MixEngine::consumeConverterPeakDb` — pre-gain converter peak since last read (single reader) — app/ui/CheckSheet.cpp:CheckSheet::refresh, app/ui/CheckSheet.cpp:CheckSheet::resetClips (2)
- `MixEngine::converterClipped` — converter hit full scale — app/ui/CheckSheet.cpp:CheckSheet::refresh (1)
- `MixEngine::getBus` — bus ChannelProcessor (meters, GR) — app/ui/AdvancedPage.cpp:AdvancedPage::refresh, app/ui/ChainEditor.cpp:ChainEditor::refresh, app/ui/ChainEditor.cpp:ChainEditor::updateViews, app/ui/LivePage.cpp:LivePage::refresh, app/ui/LivePage.cpp:refresh, app/ui/MixPage.cpp:MixPage::refresh …+1 (7)
- `MixEngine::getFx` — FX return chain (output meter) — app/ui/LivePage.cpp:refresh, app/ui/MixPage.cpp:MixPage::refresh, app/ui/MixerPage.cpp:refresh (3)
- `MixEngine::getGraph` — engine-side graph — app/ui/AdvancedPage.cpp:AdvancedPage::refresh, app/ui/MixPage.cpp:MixPage::refresh (2)
- `MixEngine::getNumStrips` — strips running — app/ui/CheckSheet.cpp:CheckSheet::refresh, app/ui/LivePage.cpp:LivePage::refresh, app/ui/MainView.cpp:MainView::performBuddyAction, app/ui/MainView.cpp:MainView::tuneChannel, app/ui/MixPage.cpp:MixPage::refreshTuneButton, app/ui/TracksPage.cpp:TracksPage::refresh (6)
- `MixEngine::getStrip` — strip ChannelProcessor (input/output meter, gate/comp/de-ess/limiter GR, sampler hit counts) — app/ui/AdvancedPage.cpp:AdvancedPage::refresh, app/ui/ChainEditor.cpp:ChainEditor::refresh, app/ui/ChainEditor.cpp:ChainEditor::updateViews, app/ui/MixerPage.cpp:refresh, app/ui/TracksPage.cpp:TracksPage::refresh (5)
- `MixEngine::isBusUsed` — group in use — app/ui/AdvancedPage.cpp:AdvancedPage::rebuild, app/ui/ExportSheet.cpp:ExportSheet::paint, app/ui/ExportSheet.cpp:ExportSheet::whatNote, app/ui/LivePage.cpp:refresh, app/ui/MixPage.cpp:MixPage::refresh, app/ui/MixPage.cpp:paint …+2 (8)
- `MixEngine::isFxUsed` — return in use — app/ui/LivePage.cpp:LivePage::anyEffects, app/ui/LivePage.cpp:LivePage::resized, app/ui/LivePage.cpp:refresh, app/ui/MixPage.cpp:MixPage::refresh, app/ui/MixPage.cpp:MixPage::resized, app/ui/MixerPage.cpp:MixerPage::rebuild (6)

### ThemeStore (static calls)
- `ThemeStore::all` — list themes — app/ui/MainView.cpp:getMenuForIndex, app/ui/ThemeSheet.cpp:ThemeSheet::refresh (2)
- `ThemeStore::chosenTextSize` — text scale pref — app/ui/MainView.cpp:MainView::MainView (1)
- `ThemeStore::chosenTheme` — theme pref — app/ui/MainView.cpp:MainView::MainView, app/ui/ThemeSheet.cpp:ThemeSheet::ThemeSheet (2)
- `ThemeStore::find` — theme by name — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::applyThemeNamed, app/ui/ThemeSheet.cpp:ThemeSheet::saveTheme (3)
- `ThemeStore::folder` — user themes folder — app/ui/MainView.cpp:MainView::handleCommand, app/ui/ThemeSheet.cpp:ThemeSheet::revealFolder (2)
- `ThemeStore::hex` — colour to hex — app/ui/ThemeSheet.cpp:paintButton (1)
- `ThemeStore::importFile` — import theme file — app/ui/ThemeSheet.cpp:ThemeSheet::importTheme (1)
- `ThemeStore::isBuiltInName` — built-in check — app/ui/ThemeSheet.cpp:ThemeSheet::baseName (1)
- `ThemeStore::preferencesFile` — ~/Music/DINE/preferences.json — app/ui/MainView.cpp:guidePreferences (1)
- `ThemeStore::removeUser` — delete user theme — app/ui/ThemeSheet.cpp:ThemeSheet::deleteTheme (1)
- `ThemeStore::resolve` — theme -> tokens — app/ui/AppTheme.cpp:Dine::applyTheme, app/ui/ThemeSheet.cpp:ThemeSheet::saveTheme (2)
- `ThemeStore::safeFileName` — file name — app/ui/ThemeSheet.cpp:ThemeSheet::exportTheme (1)
- `ThemeStore::save` — export theme — app/ui/ThemeSheet.cpp:ThemeSheet::exportTheme (1)
- `ThemeStore::saveUser` — save user theme — app/ui/ThemeSheet.cpp:ThemeSheet::saveTheme (1)
- `ThemeStore::setChosenTextSize` — write text scale — app/ui/MainView.cpp:MainView::applyTextSize (1)
- `ThemeStore::setChosenTheme` — write theme — app/ui/MainView.cpp:MainView::applyThemeNamed, app/ui/ThemeSheet.cpp:ThemeSheet::select (2)
- `ThemeStore::textSizes` — offered scales — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:getMenuForIndex (2)
- `ThemeStore::tokens` — token list — app/ui/ThemeSheet.cpp:ThemeSheet::rebuildSwatches, app/ui/ThemeSheet.cpp:groupOf (2)

### MixBuddy (static calls)
- `MixBuddy::examples` — example questions — app/ui/ChatSheet.cpp:layout (1)

### AppServices (app/ui/AppServices.h, implemented in app/Main.cpp over AudioHost/SessionStore/SessionAutosave; faked in app/Tools/AppSnapshots.cpp)
- `AppServices::askForInputPermission` — macOS mic prompt — app/ui/MainView.cpp:MainView::followMicrophone, app/ui/SetupPages.cpp:DevicePage::DevicePage (2)
- `AppServices::autosaveFailing` — last autosave failed — app/ui/MainView.cpp:MainView::timerCallback, app/ui/MainView.cpp:update (2)
- `AppServices::autosaveNow` — hand snapshot to SessionAutosave — app/ui/MainView.cpp:MainView::timerCallback (1)
- `AppServices::broadcastOutputDevice` — broadcast device name — app/ui/MainView.cpp:MainView::chooseOutput, app/ui/OutputsSheet.cpp:OutputsSheet::refresh, app/ui/OutputsSheet.cpp:OutputsSheet::showSoloDeviceMenu (3)
- `AppServices::bufferSize` — buffer size — app/ui/SetupPages.cpp:DevicePage::mouseUp, app/ui/SetupPages.cpp:DevicePage::paint (2)
- `AppServices::bufferSizes` — offered buffers — app/ui/SetupPages.cpp:DevicePage::mouseUp (1)
- `AppServices::changeOutput` — switch stereo output — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::chooseOutput, app/ui/SetupPages.cpp:DevicePage::selectOutput (3)
- `AppServices::cpuLoad` — device-manager CPU 0..1 (-1 unknown) — app/ui/MainView.cpp:update (1)
- `AppServices::currentInputDevice` — input device name — app/ui/MainView.cpp:MainView::applyInputMapping, app/ui/MainView.cpp:MainView::saveInputMapping, app/ui/MainView.cpp:MainView::setupPopover, app/ui/MainView.cpp:refresh, app/ui/RoutingPage.cpp:RoutingPage::rebuildMaps, app/ui/RoutingPage.cpp:RoutingPage::refresh …+1 (7)
- `AppServices::currentOutputDevice` — output device name — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:refresh, app/ui/OutputsSheet.cpp:OutputsSheet::chooseDevice (3)
- `AppServices::currentSessionName` — session name — app/ui/ExportSheet.cpp:ExportSheet::destination, app/ui/FavouritesPage.cpp:FavouritesPage::markCurrent, app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::importAudio, app/ui/MainView.cpp:MainView::newSession, app/ui/MainView.cpp:MainView::openSession …+5 (11)
- `AppServices::daw` — DawEngine& — app/ui/ExportSheet.cpp:ExportSheet::destination, app/ui/ExportSheet.cpp:ExportSheet::fromSample, app/ui/ExportSheet.cpp:ExportSheet::paint, app/ui/ExportSheet.cpp:ExportSheet::rangeNote, app/ui/ExportSheet.cpp:ExportSheet::toSample, app/ui/ExportSheet.cpp:ExportSheet::updateControls …+81 (87)
- `AppServices::deviceState` — DeviceState (state + sentence) — app/ui/MainView.cpp:MainView::followMicrophone, app/ui/SetupPages.cpp:DevicePage::openChosenDevice, app/ui/SetupPages.cpp:DevicePage::paint (3)
- `AppServices::deviceStopped` — device vanished — app/ui/MainView.cpp:MainView::timerCallback, app/ui/MainView.cpp:update (2)
- `AppServices::headphonesSummary` — solo summary sentence — app/ui/OutputsSheet.cpp:OutputsSheet::paint (1)
- `AppServices::importAudio` — import files/folder as tracks (synchronous) — app/ui/MainView.cpp:MainView::importAudio, app/ui/TracksPage.cpp:TracksPage::addAudioFiles (2)
- `AppServices::importSample` — copy a sound into session Samples — app/ui/MainView.cpp:MainView::MainView (1)
- `AppServices::inputDevices` — list input devices — app/ui/SetupPages.cpp:DevicePage::refresh (1)
- `AppServices::inputHeldBack` — InputAccess (mic permission state) — app/ui/MainView.cpp:MainView::followMicrophone (1)
- `AppServices::isAudioRunning` — device running — app/ui/LivePage.cpp:LivePage::refresh, app/ui/LivePage.cpp:LivePage::refreshMonitor, app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::chooseOutput, app/ui/MainView.cpp:MainView::setupPopover, app/ui/MainView.cpp:MainView::showPage …+16 (22)
- `AppServices::listSessions` — saved sessions — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::openSession, app/ui/MainView.cpp:MainView::updateChrome, app/ui/SetupPages.cpp:SessionsPage::refresh (4)
- `AppServices::loadSession` — open session — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::openSession (2)
- `AppServices::newSession` — new session — app/ui/MainView.cpp:MainView::importAudio, app/ui/MainView.cpp:MainView::newSession (2)
- `AppServices::numInputChannels` — device inputs — app/ui/MainView.cpp:MainView::applyInputMapping, app/ui/MainView.cpp:MainView::saveInputMapping, app/ui/RoutingPage.cpp:RoutingPage::rebuildMaps, app/ui/SetupPages.cpp:AssignPage::refresh, app/ui/SetupPages.cpp:DevicePage::paint, app/ui/TransportBar.cpp:TransportBar::toggleRecord (6)
- `AppServices::numOutputChannels` — device outputs — app/ui/OutputsSheet.cpp:OutputsSheet::refresh (1)
- `AppServices::openDevices` — open input+output — app/ui/SetupPages.cpp:DevicePage::openChosenDevice (1)
- `AppServices::openOutputOnly` — output only (playback) — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::chooseOutput (2)
- `AppServices::outputChannelNames` — output channel names — app/ui/OutputsSheet.cpp:OutputsSheet::pairName (1)
- `AppServices::outputDevices` — list output devices — app/ui/MainView.cpp:MainView::chooseOutput, app/ui/OutputsSheet.cpp:OutputsSheet::chooseDevice, app/ui/OutputsSheet.cpp:OutputsSheet::showSoloDeviceMenu, app/ui/SetupPages.cpp:DevicePage::refresh (4)
- `AppServices::outputDisplayName` — name to show for output — app/ui/LivePage.cpp:LivePage::refresh, app/ui/MainView.cpp:MainView::updateChrome, app/ui/SetupPages.cpp:DevicePage::refresh (3)
- `AppServices::reconfigure` — rebuild audio graph after assignment change — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::applyInputMapping, app/ui/MainView.cpp:MainView::enterSession, app/ui/MainView.cpp:MainView::showPage, app/ui/MixerPage.cpp:showMenu, app/ui/TracksPage.cpp:TracksPage::addTrack …+3 (9)
- `AppServices::retryHeldInput` — reopen input after permission — app/ui/MainView.cpp:MainView::followMicrophone (1)
- `AppServices::sampleLibrary` — SampleLibrary* (drum kits) — app/ui/MainView.cpp:MainView::drumKitMenu, app/ui/MainView.cpp:MainView::drumKitName (2)
- `AppServices::sampleRate` — device rate — app/ui/MainView.cpp:refresh, app/ui/SetupPages.cpp:DevicePage::mouseUp, app/ui/SetupPages.cpp:DevicePage::paint (3)
- `AppServices::saveSession` — explicit Save — app/ui/MainView.cpp:MainView::importAudio, app/ui/MainView.cpp:MainView::newSession, app/ui/MainView.cpp:MainView::saveNow (3)
- `AppServices::saveSessionAs` — Save As — app/ui/MainView.cpp:MainView::saveAs, app/ui/TransportBar.cpp:TransportBar::toggleRecord (2)
- `AppServices::sessionFolder` — session folder — app/ui/ExportSheet.cpp:ExportSheet::ExportSheet, app/ui/MainView.cpp:MainView::setupPopover, app/ui/TransportBar.cpp:TransportBar::toggleRecord (3)
- `AppServices::sessionMilestone` — milestone counter — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::sessionReplaced, app/ui/MainView.cpp:MainView::timerCallback (3)
- `AppServices::sessionRevision` — revision counter — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::sessionReplaced, app/ui/MainView.cpp:MainView::timerCallback (3)
- `AppServices::setBufferSize` — change buffer — app/ui/SetupPages.cpp:DevicePage::mouseUp (1)
- `AppServices::setSoloOutputDevice` — solo device (builds aggregate) — app/ui/OutputsSheet.cpp:OutputsSheet::showSoloDeviceMenu (1)
- `AppServices::setTrackPanelWidth` — persist TRACKS panel width — app/ui/MainView.cpp:MainView::MainView (1)
- `AppServices::snapshotExport` — copy job for export worker — app/ui/MainView.cpp:MainView::exportMix (1)
- `AppServices::soloOutputDevice` — solo device name — app/ui/LivePage.cpp:LivePage::refreshMonitor, app/ui/OutputsSheet.cpp:OutputsSheet::paint, app/ui/OutputsSheet.cpp:OutputsSheet::refresh, app/ui/OutputsSheet.cpp:OutputsSheet::showSoloDeviceMenu, app/ui/OutputsSheet.cpp:OutputsSheet::soloChoiceLabel (5)
- `AppServices::takeRecoveryNote` — crash-take recovery sentence — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::openSession (2)
- `AppServices::touchSession` — session changed -> autosave follows — app/ui/FavouritesPage.cpp:FavouritesPage::markCurrent, app/ui/FavouritesPage.cpp:FavouritesPage::rebuild, app/ui/HistorySheet.cpp:HistorySheet::askForFavouriteName, app/ui/HistorySheet.cpp:HistorySheet::rebuild, app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::applyInputMapping …+21 (27)
- `AppServices::trackPanelWidth` — stored TRACKS panel width — app/ui/MainView.cpp:MainView::showPage (1)
- `AppServices::xrunCount` — dropped buffers (device-reported, cumulative) — app/ui/MainView.cpp:refresh, app/ui/MainView.cpp:update, app/ui/SetupPages.cpp:DevicePage::paint (3)

### DawEngine via `services.daw()` (app/native/DawEngine.h)
- `DawEngine::getProject` — Project& (tracks, clips, markers, loop, liveSafe; edited in place then refresh()/touch) — app/ui/ExportSheet.cpp:ExportSheet::destination, app/ui/ExportSheet.cpp:ExportSheet::fromSample, app/ui/ExportSheet.cpp:ExportSheet::paint, app/ui/ExportSheet.cpp:ExportSheet::rangeNote, app/ui/ExportSheet.cpp:ExportSheet::toSample, app/ui/ExportSheet.cpp:ExportSheet::updateControls …+56 (62)
- `DawEngine::getRecorder` — Recorder (drops, errors) — app/ui/TransportBar.cpp:TransportBar::refresh (1)
- `DawEngine::getRecordingSeconds` — take length — app/ui/LivePage.cpp:LivePage::refresh (1)
- `DawEngine::getRecordingSecondsFree` — disk time left at current rate — app/ui/LivePage.cpp:LivePage::updateDiskNote, app/ui/MainView.cpp:update (2)
- `DawEngine::getSession` — DAW copy of MixSession — app/ui/TransportBar.cpp:TransportBar::toggleRecord (1)
- `DawEngine::getTimelineEpoch` — timeline undo epoch — app/ui/TracksPage.cpp:TracksPage::captureEdit, app/ui/TracksPage.cpp:TracksPage::dropStaleEdits (2)
- `DawEngine::getTransport` — Transport (atomics) — app/ui/TracksPage.cpp:TracksPage::addMarkerAtPlayhead, app/ui/TracksPage.cpp:TracksPage::mouseDown, app/ui/TracksPage.cpp:TracksPage::mouseDrag, app/ui/TracksPage.cpp:TracksPage::mouseUp, app/ui/TracksPage.cpp:TracksPage::paint, app/ui/TracksPage.cpp:TracksPage::refresh …+6 (12)
- `DawEngine::inputPeakDb` — per device input peak (atomic, slow release) — app/ui/SetupPages.cpp:AssignPage::paint, app/ui/SetupPages.cpp:AssignPage::tick, app/ui/SetupPages.cpp:DevicePage::paint, app/ui/SetupPages.cpp:paint (4)
- `DawEngine::isLiveSafe` — LIVE SAFE (project half) — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/MainView.cpp:MainView::handleCommand, app/ui/RoutingPage.cpp:RoutingPage::isCovered (3)
- `DawEngine::isRecording` — recording — app/ui/LivePage.cpp:LivePage::refresh, app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:MainView::importAudio, app/ui/MainView.cpp:MainView::importMultitrack, app/ui/MainView.cpp:MainView::newSession, app/ui/MainView.cpp:MainView::paint …+11 (17)
- `DawEngine::locate` — move playhead — app/ui/TracksPage.cpp:TracksPage::markerMenu, app/ui/TracksPage.cpp:TracksPage::mouseDown, app/ui/TracksPage.cpp:TracksPage::mouseUp, app/ui/TransportBar.cpp:TransportBar::returnToStart (4)
- `DawEngine::numInputsCarryingSignal` — inputs with signal — app/ui/SetupPages.cpp:DevicePage::liveInputCount (1)
- `DawEngine::play` — play — app/ui/TransportBar.cpp:TransportBar::togglePlay (1)
- `DawEngine::refresh` — republish clips/arming/monitoring/loop — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MixerPage.cpp:MixerPage::Strip, app/ui/MixerPage.cpp:showMenu, app/ui/TracksPage.cpp:TracksPage::commit, app/ui/TracksPage.cpp:TracksPage::cycleMonitor, app/ui/TracksPage.cpp:TracksPage::mouseDown …+2 (8)
- `DawEngine::restoreEdits` — timeline undo/redo — app/ui/TracksPage.cpp:TracksPage::redo, app/ui/TracksPage.cpp:TracksPage::undo (2)
- `DawEngine::setLiveSafe` — LIVE SAFE switch (both halves) — app/ui/LivePage.cpp:LivePage::LivePage, app/ui/MainView.cpp:MainView::handleCommand (2)
- `DawEngine::setLoop` — loop range — app/ui/TracksPage.cpp:TracksPage::mouseUp, app/ui/TracksPage.cpp:TracksPage::toggleLoop, app/ui/TransportBar.cpp:TransportBar::toggleLoop (3)
- `DawEngine::setSession` — tracks follow assignments — app/ui/SetupPages.cpp:AssignPage::commit, app/ui/TracksPage.cpp:TracksPage::addTrack, app/ui/TracksPage.cpp:TracksPage::matchNamesToClips, app/ui/TracksPage.cpp:TracksPage::moveTrack, app/ui/TracksPage.cpp:TracksPage::setTrackIcon, app/ui/TracksPage.cpp:TracksPage::setTrackName …+1 (7)
- `DawEngine::startRecording` — start take — app/ui/TransportBar.cpp:TransportBar::toggleRecord (1)
- `DawEngine::stop` — stop — app/ui/TransportBar.cpp:TransportBar::refresh, app/ui/TransportBar.cpp:TransportBar::togglePlay, app/ui/TransportBar.cpp:TransportBar::toggleRecord (3)
- `DawEngine::stopRecording` — end take -> clips — app/ui/TransportBar.cpp:TransportBar::refresh, app/ui/TransportBar.cpp:TransportBar::toggleRecord (2)
- `DawEngine::takeStopNotice` — why a take stopped itself — app/ui/TransportBar.cpp:TransportBar::refresh (1)

### Transport (static calls)
- `Transport::formatTime` — hh:mm:ss.mmm — app/ui/LivePage.cpp:LivePage::refresh, app/ui/TransportBar.cpp:TransportBar::refresh (2)

### Telemetry (static calls)
- `Telemetry::instance` — telemetry singleton (sharing toggle, notice) — app/ui/MainView.cpp:MainView::handleCommand, app/ui/MainView.cpp:MainView::timerCallback, app/ui/MainView.cpp:getMenuForIndex (3)

### InputMapStore (static calls)
- `InputMapStore::apply` — patch -> session assignments — app/ui/MainView.cpp:MainView::applyInputMapping (1)
- `InputMapStore::duplicate` — copy patch — app/ui/MainView.cpp:MainView::openInputMappings, app/ui/RoutingPage.cpp:RoutingPage::showMapMenu (2)
- `InputMapStore::fromSession` — session -> patch — app/ui/MainView.cpp:MainView::saveInputMapping (1)
- `InputMapStore::list` — saved patches — app/ui/MainView.cpp:MainView::openInputMappings, app/ui/RoutingPage.cpp:RoutingPage::rebuildMaps (2)
- `InputMapStore::load` — read patch file (import) — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::applyInputMapping, app/ui/MainView.cpp:MainView::openInputMappings, app/ui/RoutingPage.cpp:RoutingPage::showMapMenu (4)
- `InputMapStore::remove` — delete patch — app/ui/MainView.cpp:MainView::openInputMappings, app/ui/RoutingPage.cpp:RoutingPage::showMapMenu (2)
- `InputMapStore::rename` — rename patch — app/ui/MainView.cpp:MainView::openInputMappings, app/ui/RoutingPage.cpp:RoutingPage::showMapMenu (2)
- `InputMapStore::save` — save patch — app/ui/MainView.cpp:MainView::MainView, app/ui/MainView.cpp:MainView::openInputMappings, app/ui/MainView.cpp:MainView::saveInputMapping (3)
- `InputMapStore::saveAs` — export patch — app/ui/MainView.cpp:MainView::openInputMappings, app/ui/RoutingPage.cpp:RoutingPage::showMapMenu (2)

### MixController (static calls)
- `MixController::channelListen` — TUNE CHANNEL listen settings — app/ui/MainView.h:isRoutingPage, app/ui/MixPage.cpp:sentence (2)
- `MixController::voiceJobs` — speak/sing jobs — app/ui/MixerPage.cpp:showMenu, app/ui/TracksPage.cpp:TracksPage::headerMenu (2)

### MixPlanner (static calls)
- `MixPlanner::restrictTo` — KEEP SOME preview narrowing — app/ui/MixPage.cpp:rebuildChips (1)

### ReferenceAudio (static calls)
- `ReferenceAudio::measure` — measure a reference file (Measurer thread) — app/ui/ReferenceSheet.cpp:run (1)

### SessionStore (static calls)
- `SessionStore::summarise` — session list card facts — app/ui/SetupPages.cpp:SessionsPage::refresh (1)
- `SessionStore::unusedName` — next free session name (record with no session) — app/ui/TransportBar.cpp:TransportBar::toggleRecord (1)

### Transport via `daw().getTransport()` (app/native/Transport.h) - all atomics
- `Transport::getPosition` — playhead (atomic samples) — app/ui/TracksPage.cpp:TracksPage::addMarkerAtPlayhead, app/ui/TracksPage.cpp:TracksPage::mouseUp, app/ui/TracksPage.cpp:TracksPage::paint, app/ui/TracksPage.cpp:TracksPage::refresh, app/ui/TracksPage.cpp:TracksPage::snapSample, app/ui/TracksPage.cpp:TracksPage::splitAtPlayhead …+2 (8)
- `Transport::getPositionSeconds` — playhead seconds — app/ui/TransportBar.cpp:TransportBar::refresh (1)
- `Transport::getSampleRate` — rate — app/ui/TransportBar.cpp:TransportBar::refresh (1)
- `Transport::isPlaying` — playing — app/ui/TracksPage.cpp:TracksPage::refresh, app/ui/TransportBar.cpp:TransportBar::TransportBar, app/ui/TransportBar.cpp:TransportBar::refresh, app/ui/TransportBar.cpp:TransportBar::togglePlay (4)
- `Transport::setPosition` — scrub playhead — app/ui/TracksPage.cpp:TracksPage::mouseDown, app/ui/TracksPage.cpp:TracksPage::mouseDrag (2)

### Recorder via `daw().getRecorder()` (app/native/Recorder.h)
- `Recorder::getDroppedSeconds` — audio the writer could not keep up with — app/ui/TransportBar.cpp:TransportBar::refresh (1)
- `Recorder::getError` — writer error sentence — app/ui/TransportBar.cpp:TransportBar::refresh (1)

### Free functions / other native statics
- `builtInDrumKits / currentDrumKit / applyDrumKit` — app/native/DrumKits.h — DRUM KIT picker: `MainView::drumKitMenu`, `MainView::drumKitName` (applyDrumKit = one undoable mix change via `chooseSampleSounds`)
- `readSampleChoices` — app/native/SessionState.h — current kit names, `MainView::drumKitName`
- `SampleLibrary` (via `services.sampleLibrary()`) — app/native/SampleLibrary.h — `sounds(family)` with `Sound{name,user,path,inSession}` = Built in / Your sounds / This session grouping already exists; `importSound`; families are kick/snare/toms only
- `Telemetry::instance()->isConfigured / isSharing / setSharing / needsNotice / markNoticeShown` — app/native/Telemetry.h — Help menu toggle, first-run notice in `MainView::timerCallback` (tick 90)
- `OpenAiMixProvider().isAvailable()` + `controller.setReasoningProvider` — app/native/OpenAiMixProvider.h — Help menu cloud engineer toggle (`MainView::handleCommand`)
- `Guides::seen / markSeen / enabled / setEnabled / reset` — app/native/ThemeStore.h — first-visit workspace guide cards, `MainView::maybeShowGuide`
- `ExportProgress` atomics (`getState`, `stage`, `fraction`, `cancel`) — app/native/ExportProgress.h — export runs on `juce::Thread::launch` in `MainView::exportMix`, the status foot reads it on the 30 Hz tick
- `MixBounce::Stage` — app/native/MixBounce.h — export stage words in the status foot
- `MultitrackImport::Destination` — app/native/MultitrackImport.h — `TracksPage::addAudioFiles` (Match / OntoTracks / NewTracks)
- `StemNames::guesses / containsWord / sideOf` — app/native/StemNames.h — name-to-role alias table (hh, oh, bv, vox, ...) included by TracksPage; today used for file names, reusable for the Inputs typeahead
- `DeviceState`, `InputAccess`, `deviceSentence` — app/native/DeviceState.h — DevicePage text, mic-permission flow (`MainView::followMicrophone`)
- `MicPermission::check/request/denied` — app/native/MicPermission.h — only via `services.askForInputPermission`
- `putWindowButtonsInTheToolbar / dragWindowFromToolbar / toolbarDoubleClicked` — app/native/WindowChrome.mm — called by Main.cpp; traffic lights at a fixed `kFirstButtonX`
- `SessionAutosave::Recovery` -> `MainView::RecoveryOffer` — app/Main.cpp `offerRecovery` builds it; UI only shows Recover / Open last saved / Keep both
- `AppFolders::music()` — app/native/AppFolders.h — Tutorial seen marker

### src/ headers app/ui includes directly
- `UI/Widgets.h` (FlatButton, DropdownButton, ParamTile, MacroKnob, `formatRecommendationValues`) — 21 files include it; only `formatRecommendationValues` is materially used
- `UI/LiveMixLookAndFeel.h` — AppTheme.h, AdvancedPage.cpp — `DineLookAndFeel` derives from it
- `Core/DbUtils.h` — AdvancedPage, ChainEditor, MixPage, MixerPage — `kSilenceDb`, dB/gain helpers
- `Core/ChannelRole.h` — AppTheme.h — `ChannelRole` (236 uses), `RoleFamily`, role names/icons
- `Core/StyleId.h` — FavouritesPage — `StyleProfileId` names
- `Core/ProductDefinition.h` — AdvancedPage, SetupPages — `Product::Vocals` spec lookups for parameter display
- `DSP/ChannelParameters.h` — ChainEditor.h, ChainStrip.h, MixerPage — every stage field incl. sample stage (`replaceEnabled/Blend/ThresholdDb/RiseDb/DetHpfHz/DetLpfHz/MaskMs/Steady/OffsetMs/Polarity/RateSemitones/GainDb/Sound/FollowDrum`), `polarityInvert`
- `DSP/ChannelProcessor.h` — AdvancedPage, ChainEditor — `getInputMeter/getOutputMeter/getGate/getCompressor/getDeEsser/getLimiter/getSampler/getOptions`
- `DSP/EqResponse.h` (`chainMagnitudeDb`), `DSP/Biquad.h` — ChainEditor — EQ curve drawing (pure, message thread)
- `DSP/SampleBank.h` — ChainStrip.h — `SampleBankTable` sound names
- `Mix/MixSession.h` — AppTheme.h — `MixBus`, `MixPurpose`, `DeliveryLoudness`, `MasterVoicing`, `mixBusInDisplayOrder`
- `Mix/MixMacros.h` — MacroPad.h, MixPage.h — `MixMacro`, `MixMacroValues`
- `Mix/MixPlanner.h` — ChannelTuneSheet — `MixPlan`, `PlanSelection`, `restrictTo`
- `MixAI/MixBuddy.h` — ChatSheet — `BuddyAction`, `MixBuddy::examples`
- `Profiles/Profile.h` — MixPage, ReferenceSheet, SetupPages — `Profiles::targets(profile, role)` for target LUFS / ranges (read-only; numbers stay in profile data)
- `Profiles/MixProfileData.h` — LivePage — `MixProfile::autoMix(profile).depthDb`, `MixProfile::speechPriority`
- `Profiles/MacroMapping.h` — AdvancedPage — `MacroMapping::apply/defaults` (Simple knobs WARMTH..CLEAN-UP)
- `State/ParameterSpecs.h` — AdvancedPage — `ParameterSpec` ranges/units/types for value text
- Indirectly via MixController.h: `RoutingGraph`, `MixParameters/StripParameters/BusParameters/FxParameters`, `MonitorState`, `OutputFeeds`, `LiveSafePolicy/LiveAction/liveSafe::Verdict`, `MixScene/MixCheckpoint/StripTuneRecord/MixFingerprint` (MixHistory.h), `BroadcastReadiness`, `TuneLiveCoordinator::State`, `Recommendation{confidence}`, `ReferenceProfile`

### 2. Persisted state

### 2a. The session document (`<session>/<name>.dine.json`) — `SessionState` (app/native/SessionState.h), written only via `captureSession`/`applySession`, serialised by `SessionStore` (versioned)
- session.name / profile / purpose / delivery / voicing — SessionState.h:SessionState::session (MixSession) — mix document; UI edits via setSessionName/setProfile/setPurpose/setDelivery/setVoicing
- session.inputs[]: name, role, icon, inputA, inputB, enabled, focus — MixSession::inputs — Inputs table/Tracks; setSession/setInputName/setInputIcon/setInputRole/setFocusInput
- session.speechPriority, session.autoMix — MixSession — LIVE toggles
- project.folder (derived), sampleRate, tempo — Project.h:Project
- project.tracks[]: armed, monitor (Input/Auto/Off), height (px) — Project.h:TrackState — `height` is UI-owned layout (TRACKS row height) stored in the document
- project.tracks[].clips[]: name, file, fileRight, fileChannel, fileRightChannel, start, offset, length, fileSampleRate — Project.h:AudioClip
- project.markers[], loopEnabled/loopStart/loopEnd — Project.h
- project.liveSafe — Project.h — LIVE SAFE on/off authority (DawEngine::setLiveSafe sets both halves)
- devices: consoleInput, broadcastOutput, soloOutput, consoleInputUid, broadcastOutputUid — SessionState.h:DeviceChoice
- hasMix, mix (MixParameters: per strip chain/inputGain/fader/pan/mute/solo/sends/linkGroup; buses; fx slots; fxReturnDb/fxMute; tempo; monitor state) — SessionState::mix
- macros (5), tuneCount — SessionState
- outputs (feeds: left/right/source/gainDb/mute/mono/monitor) — SessionState::outputs (OutputFeeds)
- safety (LiveSafePolicy limits) — SessionState::safety
- reference (measured ReferenceProfile; file never stored) — SessionState::reference
- scenes (4 slots + favourites with MixFingerprint) — SessionState::scenes
- history (StripTuneRecord per channel, max 24 each) — SessionState::history
- checkpoints (MixCheckpoint list) — SessionState::checkpoints
- tuneLive (last TUNE LIVE run record, read-only) — SessionState::tuneLive
- samples[kMaxStrips] (SampleChoice: family, name, user, path) — SessionState::samples
- trackPanelWidth — SessionState::trackPanelWidth — UI-owned (TRACKS channel-panel width; `services.setTrackPanelWidth`, 0 = default)
- readiness (BroadcastReadiness: active ticks, notes, finished-service history) — SessionState::readiness
- NOT persisted by design: BYPASS, DIM/MUTE, plan/preview/compare/selection, Mix Buddy chat, UNDO/REDO stacks (memory only)

### 2b. Beside the document
- `<doc>.autosave.json`, clean-exit marker, `<doc>.recovered.json` — app/native/SessionAutosave.cpp:autosaveFor/markerFor/heldFor — written by worker thread; recovery offered at launch
- `<take>.wav.recording.json` sidecars — app/native/Recorder.h:sidecarFor — crash-safe takes
- `<session>/Samples/<kick|snare|toms>/` — SampleLibrary::setSessionFolder — imported sounds that travel with the session

### 2c. Per-Mac preferences
- theme — ~/Music/DINE/preferences.json "theme" — ThemeStore::chosenTheme/setChosenTheme — UI-owned
- text size (scale) — same file — ThemeStore::chosenTextSize/setChosenTextSize — UI-owned
- workspace guides seen/enabled — same file — Guides::seen/markSeen/enabled/setEnabled/reset (ThemeStore.h) — UI-owned
- user themes — ThemeStore::folder() (*.dinetheme / json) — ThemeStore::saveUser/removeUser/importFile — UI-owned
- Getting Started seen — ~/Music/DINE/.getting-started-seen — Tutorial.cpp:seenMarker — UI-owned
- input patches — ~/Music/DINE/Input Maps/*.dinemap.json (reads *.dlivemap.json) — InputMapStore::save/load/list/rename/duplicate/remove/saveAs
- last session pointer — ~/Library/Application Support/DINE/last-session.txt — Main.cpp:lastSessionPointer
- telemetry install id, sharing switch, notice shown, queue — ~/Library/Application Support/DINE/telemetry.json etc. — Telemetry::setSharing/markNoticeShown
- user sample library — ~/Music/DINE/Samples — SampleLibrary::userFolder

### 2d. UI state that is NOT persisted today (memory only; resets each launch)
- sidebar shown/folded — MainView.h:sidebarShown (View menu 610 / sidebar button)
- MIXER view Strips/List, Show All/Inputs/Groups/Effects, strip Size Narrow/Normal/Wide — MixerPage.h:View/Show/Size
- TUNE left rail — MixPage.h:railShown; INSPECTOR rail/trail — AdvancedPage.h:railShown/trailShown
- TRACKS zoom/scroll — TracksPage; window bounds — Main.cpp `centreWithSize (1520, 960)`, limits 1180x760 (brief asks min 1280x780); no saveWindowState
- If v4 wants these remembered, they belong in preferences.json (per Mac) via a new ThemeStore-style accessor, not in SessionState (which would bump the session format).

### 3. Meter / level / hit readouts (today)

### Clocks
- `MainView::timerCallback` — app/ui/MainView.cpp:1383 `startTimerHz (30)` — THE main clock: `controller.poll()` (listen/plan state machine, Autopilot 400 ms looks, checkpoint beat), `transportBar->refresh()`, the visible page's `refresh()` (Tracks/Mixer/Tune/Live/Inspector; Assign `tick()`), open sheets (`ChannelTuneSheet`, `CheckSheet`, `BroadcastReadinessSheet`, `ChatSheet`), status foot `update(slow)` (slow = every 30 ticks = 1 Hz), sidebar every 10 ticks (3 Hz), chain foot + SOLO pill every 3 ticks (10 Hz), toast countdown, autosave debounce (10 ticks), device-stopped / autosave-failing toasts, and the "on air" top strip pulse.
- `MainView::MixerWindow` — MainView.cpp:828 `startTimerHz (30)` — second timer driving a detached MIXER (`page->refresh()`)
- `MainView::PageWindow` — MainView.cpp:862 `startTimerHz (30)` — detached LIVE / INSPECTOR windows
- `MacroPad` / `MacroRibbon` — MacroPad.cpp:112/408 `startTimerHz (60)` — settle animation only; stops itself when done
- No VBlankAttachment anywhere; no AsyncUpdater; ReferenceSheet::Measurer is a juce::Thread polled from the 30 Hz tick; export is `juce::Thread::launch` + ExportProgress atomics polled from the tick.
- Not idle-zero today: the on-air pulse repaints the top strip every tick while audio runs with inputs (`onAir` sine), and every visible page polls at 30 Hz regardless of signal.

### Sources (all lock-free atomics written by the audio thread; read on the message thread at 30 Hz)
- strip/bus/return level — src/DSP/LevelMeter.h — `getPeakDb(ch)`, `getRmsDb(ch)`, `getMaxPeakDb()`, `getMaxRmsDb()`, `hasClipped()`, `getNumChannels()`; `consumeMaxPeakDb()` = peak since last read via a reader-owned atomic (ONE reader only)
  - read via `controller.getEngine().getStrip(i)/getBus(b)/getFx(s).getOutputMeter()` in MixerPage::Strip::refresh, LivePage::GroupTile::refresh, MixPage::refresh (groups/returns), AdvancedPage::refresh (rail rows), TracksPage::refresh (header meters), ChainEditor::refresh (`getInputMeter().getMaxPeakDb()` + output)
  - Hazard for "one clock": `consumeMaxPeakDb` is consumed by several pages (MixerPage, LivePage, MixPage, AdvancedPage, TracksPage) and by the detached Mixer/Page windows on their own timers; two simultaneous readers of the same meter steal each other's peaks. A single shared tick that reads each meter once per frame into a snapshot the widgets share fixes this without engine changes.
- stage gain reduction — `getGate()/getCompressor()/getDeEsser()/getLimiter().getGainReductionDb()` — ChainEditor.cpp:1729 (stage bars), MixerPage
- master loudness — `controller.getMasterLoudness()` (MixController.h:MasterLoudness: integrated/short-term/momentary LUFS, truePeakDb, limiterReductionDb, target, tolerance, ceiling, headroom; from LoudnessMeter atomics) — MixerPage master strip, LivePage::refresh, MixPage::refreshMaster, MainView status foot `update`
- speech duck — `controller.getSpeechDuckDb()` (MixEngine atomic) — exists, not read by UI
- share-the-mics gains — `MixEngine::getAutoMixGainDb(i)` — exists, not read by UI (speaking-mic tiles can bind to it)
- Autopilot — `getAutopilot()` (movedDb[], released[], lastWhat/Why), `readAutopilotMeters()`, `autopilotDrift(bus)` — LivePage reads the state; drift/meter readers unused
- device inputs before the mix — `daw().inputPeakDb(ch)` (DawEngine.h inputPeak atomics, slow release) + `numInputsCarryingSignal()` — DevicePage "what's arriving", AssignPage::tick (UI-side peak hold falling 0.1 dB/tick)
- converter peaks/clips (pre digital gain) — `getEngine().consumeConverterPeakDb(strip)`, `converterClipped`, `clearConverterClips` — CheckSheet::refresh (single reader)
- gain verdicts — `controller.getInputAdvice(strip)` (after a listen) / `liveCaptureAdvice(role, peakHoldDb)` (live) — MixerPage, LivePage, AdvancedPage, TracksPage, AssignPage, CheckSheet
- sample replacement hits — `getStrip(i).getSampler().getHitCount()` (SampleTrigger atomic int), `getVetoCount()` ("held"), `isPlaying()`; `SampleTrigger::getLastLevelDb()` atomic exists but is not reachable through SampleReplacer — ChainEditor.cpp:1733/1803/1926 (counter text + stage bar lit while a sample plays). No event stream.
- transport — `daw().getTransport().getPosition()/getPositionSeconds()/isPlaying()/isLooping()/getLoopStart/End/getEnd()` (atomics) — TransportBar::refresh (clock via `Transport::formatTime`), TracksPage::refresh/paint (playhead), LivePage (record time)
- recording — `daw().isRecording()`, `getRecordingSeconds()`, `getRecordingSecondsFree()` (disk time left), `getRecorder().getDroppedSeconds()`, `getError()` — TransportBar, LivePage::updateDiskNote, status foot
- engine health — `services.xrunCount()` (device-reported, cumulative), `services.cpuLoad()` (AudioDeviceManager::getCpuUsage), `deviceStopped()`, `deviceState()` — status foot `update`, DevicePage::paint; `MixEngine::getStats()` (last/peak block micros) exists, unused by UI
- listen progress — `getListenProgress()`, `getListenSeconds()`, `isWaitingForBand()`, `stripHeard()`, `busHeard()` — MixPage ListenSheet paint, ChannelTuneSheet::paint
- export — `ExportProgress` atomics — status foot on the tick
- Ballistics live in the widget: `DineMeter::setLevels` (AppTheme.cpp:1361) — hold falls 30 dB/s; the v4 law (35%/100 ms release, 0.25 dB/tick hold, red > -0.5 dB) is UI-only work.
