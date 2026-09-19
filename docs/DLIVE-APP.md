# DLIVE: the application

The DAW layer, the mix layer and every workspace - TRACKS, MIXER, TUNE, LIVE, the Inspector, the setup pages - and the rules each one follows. Moved verbatim from the old CLAUDE.md (2026-09-19); the architecture is `docs/ARCHITECTURE-DLIVE.md` and `docs/MILESTONE-7.md`.

- DLIVE is **the live recording and broadcast DAW** (2026-09 DAW milestone; see `docs/MILESTONE-7.md`).
  Four workspaces over one session: TRACKS (timeline, clips, waveforms), MIXER, TUNE, LIVE. The DAW layer is
  `app/native`: `Transport` (the playhead, sample-exact loop), `Recorder` (raw WAV per armed track through
  `ThreadedWriter`), `ClipSource` (the one place clips become audio), `TimelinePlayer` (ring-buffered playback
  on a reader thread), `Project` (tracks, clips, markers, LIVE SAFE) and `DawEngine`, which sits between the
  device and the mix: it records the raw inputs, reads the timeline and builds the **input matrix** that
  `MixController::process` receives, so TUNE MIX works the same on live inputs and on recorded material.
  Monitoring has exactly one rule, `monitorUsesLiveInput` in `Project.h` - do not add a second. A session is a
  folder (`~/Music/DLIVE/<name>/` with `Audio Files/` inside); `SessionStore` is version 3 and still opens
  versions 1 and 2. Import a folder of stems with `MultitrackImport` (it becomes tracks and clips - there is no
  separate "play a recording" audio path any more). Export is `MixBounce::renderProject`, streamed to disk.
  App tests for all of this: `build/app/dlive_app_tests` (`app/Tests/DawTests.cpp`).
- DLIVE standalone (2026-09 pivot; see `docs/ARCHITECTURE-DLIVE.md`): the mix layer lives in `src/Mix`
  (`MixSession`/`RoutingGraph` build buses + returns from assignments; `MixEngine` is the real-time graph, parameters
  arrive whole via `Core/TripleBuffer`; `MixCapture`/`OfflineCapture` listen to every input at once; `MixPlanner` =
  per-strip Tune + input gain + relationships + balance + buses/master; `MixMacros` = the five overview controls, 50 =
  the plan). Mix-level numbers only in `src/Profiles/MixProfileData.cpp`.
  **The group buses are DRUMS, BASS, MUSIC, VOCALS, SPEECH, then MASTER** (`MixBus`, `src/Mix/MixSession.h`).
  A speaking microphone is never mixed in with the singers: `RoleFamily::Speech` routes to its own
  `MixBus::Speech`, which gets its own colour, band, meter, tile and rail section on every workspace
  (`Dine::busTint` in `app/ui/AppTheme` is the one place that colour is decided - do not re-write the
  switch per page) and its own row in the ASSIGN list. Everything that walks the group buses uses
  `b < int (MixBus::Master)`, so a new bus goes in before MASTER. That insertion moved every stored bus
  index above VOCALS, so `SessionStore` is version 3 and remaps a version <= 2 document's five bus slots
  and its output-feed sources (`busFromStoredIndex`) - a session saved before the split opens with its
  master on the master and an empty speech group. A bus chain is only fitted when something feeding it
  was actually heard playing (`busPlayed` in `MixPlanner`): the speech group is silent through most
  songs, and a compressor fitted to silence would crush the sermon the moment it arrives. The plan must stay idempotent on the same
  listen (`MixPlannerTests`); every level decision is absolute from the capture, never "current + delta". Faders and
  the master trim are fitted from levels *predicted under the proposed chain* (`MixPlanner::predictedProcessed{Peak,Rms,ActiveRms}Db`,
  compressor model numbers in `MixProfileData`), so one TUNE MIX lands. A fader is fitted from **loudness while the
  source plays** (`AnalysisResult::activeRmsDb`), never from the sample peak: desk multitrack exports carry isolated
  clicks 25 dB above anything musical, and a peak-fitted fader follows the click. `musicalPeakDb` (the peak capped at
  `hitLevelDb + 12`) is what gain staging reads for the same reason; the raw peak stays the clipping test. A drum close
  microphone hears the whole kit, so the balance lifts one only `maxCloseMicRaiseDb` (gain + fader together) and says to
  turn the preamp up instead. **The effects are timed to the song.** A live console has no host play head, so
  `AnalysisAccumulator` measures a tempo per source (`tempoBpm` / `tempoConfidence`, autocorrelation of the onset
  envelope) and `MixPlanner` takes the consensus across the sources that actually play a rhythm - a held note or an
  open room mic has no onsets and does not vote, or it buries the kick. That tempo rides in `MixParameters::tempoBpm`
  (saved with the session), reaches every return through `MixEngine`'s `FxChain::setTempo`, and also fits the reverb
  tails: `MixProfile::reverbBeats` says how many beats a return may ring for, and the FX profile's own decay stays the
  ceiling, so a quick song shortens the tail and a slow one leaves it alone. Without this every synced delay ran at the
  engine's default 120 BPM regardless of the song. Check with the stems tool's PREDICTION CHECK
  and the `after` LUFS line (target -23, within ~1 LU) when touching gain, fader, bus or master rules. Inputs below
  `faintInputDb` at the device are "faint": flagged, never tuned or raised.
  The app is `app/` (`MixController` no JUCE, `DawEngine` the timeline, `AudioHost` the device, `ui/` pages).
  **The app's look is the v2 desktop design** (see THE V2 DESKTOP above): tokens, icons, widgets and
  look-and-feel in `app/ui/AppTheme.{h,cpp}` (`Dine::`), the chrome in `MainView`, sheets for TUNE MIX and
  its result. Caps only in the product verbs and the section labels (`Dine::caps`); the plug-in keeps
  `Tokens` in `src/UI` and is unaffected.
  MIXER (`app/ui/MixerPage`) is one `Strip` component laid out two ways - STRIPS (a vertical bank at three widths)
  and LIST (a row per source) - with a filter (All / Inputs / Groups), pan and R/A/M/S. The bank is **one console
  surface, not a row of cards**: a column is a flat `#13161c` plane 3 px apart from the next, carries its
  group's colour as a 3 px band along its top (5 px when picked out), and the master is pinned to the right as
  a 150 px column with LUFS-I / Short / True pk / Limiter / Target under its fader. A STRIPS column reads top to bottom the way a console does:
  number and name, the gain-staging chip, INSERTS (the chain stages that are actually on, from `activeChainStages`),
  SENDS (the used FX slots, a readout - sends are edited in the Inspector), PAN, then the fader and meter, the level
  and peak, the keys, and the bus it feeds. **The slots are fixed** - three inserts, two sends, and the gain and pan
  rows are reserved for every channel and bus - so an empty slot holds its place and the sections line up straight
  across the console; `buildColumn` is the one place they are positioned (paint and layout read the same `Col`) and
  a short strip drops whole sections, in a fixed order, rather than squeezing the fader. There is no per-strip dB
  ruler: the one mark a bank is read against is the 0 dB unity line, drawn across the fader and the meter at the
  same height in every strip (the five group meters on TUNE carry the numbers instead). The console fader is
  `dineFader` in `DineLookAndFeel::drawLinearSlider`: a dark milled slot and a moulded cap, never a lit track.
  A fader moves when it is **dragged and at no other time** - a two-finger swipe across a bank of faders is a
  scroll, not twenty-four small changes to the mix - so every `juce::Slider` in `app/ui` goes through
  `Dine::dragOnly` (no wheel; the event passes to the surface underneath) and every scrolling surface through
  `Dine::nativeScrolling`, which sets the step that makes a trackpad swipe travel as far as the fingers do.
  A `Strip` is opaque, cached as an image and repaints only what moved (`Strip::Look` / `showLook`), so
  scrolling a 24-input console is a blit rather than twenty-four columns of text redrawn per frame. The master is pinned to the
  right of the bank (it is a child of the page, not the scrolling `Bank`) and carries the LUFS-I / short-term /
  true-peak readout against the -23 target. A click picks a strip out, a double-click opens it in the Inspector.
  It also opens in its own window (View > Open Mixer in a New Window, or the button on the
  page); the detached page is a second `MixerPage` on the same `MixController`, so both consoles always agree.
  TRACKS (`app/ui/TracksPage`) is drawn and hit-tested by hand: a tool row (row height S/M/L, Snap, Follow,
  Split, Marker, what is selected, the loop and the zoom) and then one 46 px ruler band that holds the loop
  strip along its top (drag it to mark a loop), the marker lane inside it (click to jump, drag to move,
  double-click empty to add, right-click to rename or delete, `M` / Edit menu to add at the playhead) and the
  ticks along its foot. Snap is magnetic to the grid, the markers, the playhead, the loop
  and every other clip edge (`snapSample`). `keyCell` is the one place the R/A/M/S keys are positioned (a 2 x 2
  block beside the meter), so paint and hit-test cannot disagree; a header reads a status dot (accent when the
  chain is doing something), number, source icon and name over its fader and level, and a third line that exists
  only when it has something to say - the balance when it is not centred, the gain-staging chip when there is
  advice. The name carries its own warning glyph when it no longer matches the clips, and a resting R/A/M/S key
  is a hint rather than a boxed button, so the normal row is two lines. Clip and header colours come from
  the same four group tints the mixer bands with, and a clip you cannot hear (muted, or soloed out) is drawn grey.
  A TRACKS header also carries its own volume fader (`faderCell` is the one place it is
  positioned; a tall row gets it under the name with the level beside it, a short row a slim
  bar along the foot), a click on a header picks that channel out (the chain strip along the
  foot reads it) while a *double*-click opens it in the Inspector - the header's own controls
  keep their single clicks - and pinch on the trackpad (or Cmd-wheel) zooms about the pointer
  via `zoomAround`.
  A track and its input are two lists joined by index (`Project::tracks` / `MixSession::inputs`),
  so `Project::syncTracks (previous, next)` remaps the tracks whenever the assignments are
  rebuilt - each track follows its own input by device channel, then by name - and
  `HostServices::reconfigure` hands the DAW engine the new session; without that an input
  dropped on the ASSIGN page left the clips behind and every name below it slid by one.
  **Which input a new input used to be is decided in exactly one place**, `matchInputs`
  (`src/Mix/MixSession.h`): the clips read it (`syncTracks`) and so does the kept mix
  (`carryMix`, `src/Mix/MixParameters.h`), so the timeline and the console can never end up
  disagreeing about which input is which. `carryMix` is what makes rearranging free - every
  channel that survived a rebuild keeps its chain, gain, fader, pan, keys and sends, and only
  an input that became a *different source* goes back to its baseline (a kick's gate is wrong
  on a voice); the buses, the master, the returns and the tempo are not per-input and come
  across whole. One helper, `MixController::carryKept (previousMix, previousSession, tunes)`,
  is what a host calls straight after `prepare()`, and `getPreparedSession()` is the session
  the running mix belongs to (`setSession` replaces the document before the graph is rebuilt,
  so `getSession()` is the wrong thing to read it against).
  **Rearranging the channels is a drag on the TRACKS header** (`TracksPage::moveTrack`, also
  Move up / Move down on the header's menu and in the Track menu): press a header and drag it
  up or down, a line shows where it would land, and letting go moves the *input* - the mixer's
  bank, TUNE's rail, the Inspector's list and the ASSIGN page all read the new order. The row
  only lifts after the pointer has actually travelled (`kOrderGrip`), so a click that wandered
  still just selects; dragging past the edge of the lanes scrolls. Nothing about the sound
  changes, but the graph is rebuilt, so LIVE SAFE locks it like any other re-route.
  A header whose name no longer describes the audio under it is drawn amber with a warning
  glyph (`nameMismatch`: a take suffix is not a mismatch, a different word is), and
  right-clicking a header is the one place to put it right - Rename, Use the clip's name,
  Match every track to its clips, Source, Icon, Fix the assignments..., Open in the Inspector.
  A rename goes through `MixController::setInputName`, which sets the session *and* the graph's
  copy and rebuilds nothing, so TRACKS, MIXER and the Inspector are renamed together and the
  kept mix, the plan and the clips all survive; changing the source rebuilds the routing and
  says so. The icon is the same kind of label: `InputAssignment::icon` (last in the struct,
  so the brace-initialised sessions in the tests still compile) holds a key from
  `Dine::iconChoices()`, empty meaning "whatever the role says"; `Dine::iconFor (key, role)`
  is the one place that decision is made, `MixController::setInputIcon` sets it without a
  rebuild, `RoutingGraph`/`StripRoute` carry it so MIXER and TUNE agree, `AssignPage::commit`
  carries it (it rebuilds every assignment from scratch), and `SessionStore` writes it only
  when it is set.
  Both workspaces end in `app/ui/ChainStrip`: the picked-out channel's chain stage by stage with what each is set
  to, from the same `chainStages` the Inspector's cards follow. Click it to open the Inspector.
  Outputs: the mix can leave by more than one pair at once. `src/Mix/OutputFeeds.h` is the model (up to
  4 feeds, each a device output pair + source (master or a group bus) + level + mono + mute); `MixEngine`
  takes them through their own `TripleBuffer` (`setOutputFeeds`) because monitoring is not mix - the planner,
  the macros and BYPASS never touch them, and an export is unaffected. `AudioHost` opens every output channel
  (`kMaxOutputs`). The UI is `app/ui/OutputsSheet` (toolbar output popup > Set up outputs, View menu, or the
  Device page). CoreAudio opens one device at a time: two devices at once is a macOS Aggregate Device, which
  the sheet explains and can open Audio MIDI Setup for.
  INSPECTOR (`app/ui/AdvancedPage`) is the engineer's drill-down, in three columns. Left, a 206 px rail:
  every channel under its bus (dot, name, a mini level bar, its fader) with the engine's own state - rate,
  buffer, latency - along the foot. Middle, the channel: a 96 px head (colour, what it is, its name, the IN
  and OUT meters either side of the chain, input gain, level, pan and the keys), then `SignalPath` - the
  whole chain as a chip per stage with its lamp, number, icon, setting, a bar for how hard it is working and
  a dot for where the setting came from (accent = tuned by DINE, amber = hand-edited) - and under it the
  stage you picked, opened as a device by `app/ui/ChainEditor`: its name and plain sentence, a badge, IN/OUT
  and `Back to DINE`, then what the stage is doing drawn (an EQ curve with nodes you drag, a compressor's or
  gate's in-out line with the live gain reduction and the last 8 seconds, the bars of a trim with its gain
  staging) beside a knob for every number it owns - band cards on the EQs, a knob grid and switch chips
  elsewhere. Right, a 272 px column: what TUNE MIX did, a line per stage (what it is set to, TUNED /
  EDITED / NOT USED, and the sentence from the report that explains it - matched by the parameter ids the
  stage owns), RE-TUNE and REVERT, and the headroom (the master shows its loudness instead). The chip
  labels and readouts come from `chainStages` in `ChainStrip`, so the path, the mixer's INSERTS and the
  strip along the foot of a workspace can never disagree. The limiter stage appears on the master only and
  the width stage on stereo channels only, because that is where `MixEngine` configures them; the sends
  close the path where the session uses FX. "Hand-edited" is a diff against `getPlan()->proposed`, which is
  also what `Back to DINE` and REVERT put back. Edits go through `MixController::setStripChannel` /
  `setBusChannel` as a whole `ChannelParameters`: they live on the kept mix beside the faders, survive a
  macro, are saved with the session, and the next TUNE MIX replaces them the way it replaces a fader.
  BYPASS disables them.
  Gain staging is the first move in a mix and the app says so before it says anything else:
  `MixController::getInputAdvice (strip)` is the one place that reads the plan and answers what
  one input's level needs (Faint / NotHeard / Low / Hot / Clipping / Digital / Healthy, the
  console move in dB and the sentence). `Digital` is the one that matters in a church - the
  level works, but only because DLIVE raised it more than `digitalGainAdviceDb`
  (`MixProfileData`) digitally, which lifts the preamp's noise with the source. It is surfaced
  as a chip on the TRACKS header and the MIXER strip (both layouts), as the GAIN STAGING card
  at the top of the Inspector's right column, as the plan's *first* note (naming the inputs)
  and in `getMixHealthNotes`; an input that only works on a big digital raise is not counted
  healthy. Advice only: nothing about the mix changes.
  TUNE CHANNEL is one source on its own, on click: `MixController::startTuneChannel (strip)`
  runs the same listen as TUNE MIX (every input is measured, so the channel is still decided
  in mix context) but waits for that channel (`MixCapture::Settings::triggerStrip`) and is
  shorter, and the plan is narrowed by `MixPlanner::channelOnly (plan, strip, profile)`:
  `proposed` is `before` everywhere except that strip, so the buses and the master are left
  alone and what is proposed is exactly what the mix becomes when it is kept - which is also
  what the Inspector reads as "what DINE set". Every other strip keeps what the listen
  measured about it (gain-staging advice and mix health stay fresh) with the moves the plan
  does not make taken back out. BEFORE / AFTER, KEEP and REVERT are the mix's own, and
  `getTuningStrip()` says which channel a listen or a preview is about (-1 = the whole mix).
  It is clicked from the TUNE workspace's input rail, a mixer strip's right-click menu, a
  TRACKS header's menu, the Inspector's right column, the Mix menu or `T`; the sheet
  (`app/ui/ChannelTuneSheet`) drops over whatever workspace you are on, so the console keeps
  playing behind it and MixPage's own sheets stay out of the way.
  Every panel at the edge of the window folds away, so the middle can have the width: the sidebar
  (the toolbar's leftmost button, View menu, `Ctrl-Cmd-S`, `MainView::setSidebarShown` - `sidebarWidth()`
  is the one place the rest of the window reads it), the Inspector's channel rail and its WHAT DINE DID
  column (`AdvancedPage::setRail/TrailShown`) and TUNE's input rail (`MixPage::setRailShown`). A page
  panel keeps a `Dine::Metric::panelTab` gutter with `DinePanelTab` in it - the same handle everywhere,
  chevron pointing the way the click moves the panel, the panel's name down the gutter when it is closed
  - and `[` / `]` toggle the panel on that side of whatever page you are on (`MainView::togglePanel`,
  falling back to the sidebar where a page has no left panel of its own). Nothing about the mix changes.
  State is never left to a shade. `DineKey` (`AppTheme`) is the one console key - M amber, S teal, R red, A blue,
  dark letter on a filled key when it is on - used by the mixer strips, the LIST rows and (drawn the same way by
  hand) the TRACKS headers, so a mute looks like a mute wherever it is pressed; a muted strip darkens, names itself
  in amber and its meter greys out (`DineMeter::setMuted` keeps reading the signal, so "nothing there" and "not
  heard" never look alike). On LIVE, a muted group tile goes amber and says NOT HEARD, a soloed one says SOLO, and
  LIVE SAFE fills and reads "LIVE SAFE ON" with a sentence beside it saying what is locked. LIVE carries a
  tile per group bus, **one for the effects returns and one for the MASTER** (2026-09-18: its fader and mute
  are the master bus's own through `setBusFader` / `setBusMute (Master)`, there is no solo so MUTE has the key
  row, and its integrated LUFS is the readout beside its meter; the "ON" chip yields to the name on a narrow tile): `MixParameters::fxReturnDb` / `fxMute` are the
  FX group's own fader and mute, folded into the return's gain in the one place `MixEngine` already decides
  it (so BYPASS and an unused slot still win) and set through `MixController::setFxReturn` / `setFxMute`.
  0 dB and not muted is "as tuned", which is also what a session saved before they existed reads as. The
  returns solo as one group (`MixController::setFxSoloAll` / `anyFxSolo`, 2026-09-18): S on that tile solos every
  return the session uses, so the engineer hears just the reverbs and delays - in the normal (monitor) solo the
  sources keep feeding the sends and only the returns reach the headphones.
  BYPASS (toolbar, Mix menu, `B`) is `MixController::setBypass`: `compose()` returns `startingPoint()` with
  `bypassProcessing`, carrying only mute and solo across, so you hear the console feed. It never touches the kept
  mix - switch it off and the mix is exactly as it was - and faders are disabled while it is on.
  Verify with
  `build/app/dlive_ui_snapshots <dir>` and the real stems: `build/app/dlive_mix_stems "<stems folder>" 30 <outdir>`
  (writes raw/before/after/after-retuned WAVs, exit 0 = re-plan on the same listen changed nothing). App tests:
  `build/app/dlive_app_tests`; real device: `build/app/dlive_device_check 3`; recording playback through the host: `build/app/dlive_device_check 4 "<stems folder>"`.
  The app is **DLIVE** (renamed from DINELIVE, 2026-09-10): target `DLive`, product `DLIVE`, bundle
  `com.dine.dlive`, tools `dlive_{ui_snapshots,app_tests,mix_stems,device_check}`, sessions in `~/Music/DLIVE/`
  as `<name>.dlive.json`. Sessions written under the old name still open and are still listed - `SessionStore`
  accepts `app: "DINELIVE"`, scans `*.dinelive.json` and reads `~/Music/DINELIVE` (`formerNameFolder`) - and are
  written back under the new extension when they are next saved. The app icon is `app/resources/AppIcon.png`
  (1024 px, generated art: the D with a fader cap), handed to JUCE as `ICON_BIG`. The DINE plug-in family and the
  `Dine::` design tokens keep their name; only DINELIVE became DLIVE.
  **Setting a session up is four screens of one layout** (`app/ui/SetupPages`, 2026-09-12): SESSIONS (the
  library), AUDIO DEVICE, INPUTS and PURPOSE AND SOUND. `SetupLayout::of` is the one place the bands are
  measured - title and a readout, a toolbar, the table beside a 252 px rail of small cards, then a footer
  with a note and Back / Continue - so all four line up and a change to the shape is a change in one
  function; `drawSetupHead` / `drawSetupFooter` draw the ends. The shared parts live in `AppTheme`:
  `DineChip` (a filter chip with its group's dot, in one rounded track), `Dine::drawRadio`,
  `Dine::drawCaption` and `Dine::drawStackedBar` (one bar divided by group, in the bus colours).
  SESSIONS lists every saved session with what it sounds like, what it was for and how its inputs fall
  across the groups - read by `SessionStore::summarise`, which parses the document's header only and never
  walks the audio beside it - and is where the app opens when there is a library to open into.
  INPUTS groups the desk by the bus each input will feed (a clickable group header picks the whole group
  out), and a selection turns the toolbar into the bulk one: set what they are, fill a kit down them in
  order, name them from their role, link them as pairs, drop them. The numbers on PURPOSE AND SOUND are
  `Profiles::targets (profile, masterRoleFor (purpose))`, so what the card promises is what TUNE MIX aims
  at. AUDIO DEVICE and INPUTS draw what is arriving on each device channel from
  `DawEngine::inputPeakDb` / `numInputsCarryingSignal` - a peak per block with a slow release, stored from
  the audio thread with relaxed atomics before anything in the mix touches the signal - so "the console is
  plugged in but channel 9 is dead", and "this unnamed input is carrying signal", are visible before a
  single input has been named.
  In the app, Import a multitrack... on the device page (or File > Import Multitrack Folder...) turns a stems
  folder into tracks and clips, with names and sources guessed from the file names.
  **"Arm" is not a word the app says.** A volunteer does not know it, and the message they meet when they press
  Record is the worst place to teach it. The field stays `Track::armed`, the key stays the red **R** (a console
  key, beside A/M/S), and every sentence around it is plain: "set to record", "N TO RECORD", "No tracks are set
  to record yet - press the red R on each track you want". The engineer's word appears once, in the R key's
  tooltip, as the thing it is called elsewhere. The TRACKS keys are drawn by hand, so `TracksPage::getTooltip`
  is where R, A, M, S and the fader say what they are; the tool row's **All to record** button
  (`setAllToRecord`) is the one click for a whole session, and it mirrors the Track menu.
