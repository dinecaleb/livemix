# DINE: the application

The DAW layer, the mix layer and every workspace - TRACKS, MIXER, TUNE, LIVE, the Inspector, the setup pages - and the rules each one follows. Moved verbatim from the old CLAUDE.md (2026-09-19); the architecture is `docs/ARCHITECTURE-DINE.md` and `docs/MILESTONE-7.md`.

- DINE is **the live recording and broadcast DAW** (2026-09 DAW milestone; see `docs/MILESTONE-7.md`).
  **ROUTING (2026-09-28, `app/ui/RoutingPage`).** Everything about where the sound comes from and where it goes
  is one workspace now, reached deliberately from the sidebar's single SET-UP row or from View > Set-up and
  Routing. Five sections down its left: the audio device, the inputs, what the mix is for, the outputs and the
  engineer's listen, and the patches this church has saved. The first three are the pages that already existed
  - this is where they are now, not a second copy: `MainView` still owns them and lays each one into
  `RoutingPage::contentBounds()`, and the head is only drawn over the two sections ROUTING owns, because a
  shell that repeats the title of the thing inside it does not trust it. Outputs stopped being a sheet
  (`OutputsSheet::setEmbedded`) and the saved patches stopped being a submenu inside a submenu - they are a list
  with the row's own Apply and a menu for the rest. The everyday sidebar lost its three set-up rows and gained
  one, so a volunteer looking for the fader for the pastor's microphone is no longer one click from changing
  what the console is.
  **Under LIVE SAFE the workspace is covered.** The device, the patch and the output feeds are the three ways
  to silence a room in the middle of a service, so nothing there can be reached until somebody presses "I know
  what I am doing" - and `contentBounds()` is empty while it is covered, so the page underneath has no size
  rather than being greyed and hoped about. The confirmation is for one visit: leaving the workspace locks it
  again, and LIVE SAFE itself is never touched by it.

  Four workspaces over one session: TRACKS (timeline, clips, waveforms), MIXER, TUNE, LIVE. The DAW layer is
  `app/native`: `Transport` (the playhead, sample-exact loop), `Recorder` (raw WAV per armed track through
  `ThreadedWriter`), `ClipSource` (the one place clips become audio), `TimelinePlayer` (ring-buffered playback
  on a reader thread), `Project` (tracks, clips, markers, LIVE SAFE) and `DawEngine`, which sits between the
  device and the mix: it records the raw inputs, reads the timeline and builds the **input matrix** that
  `MixController::process` receives, so TUNE MIX works the same on live inputs and on recorded material.
  Monitoring has exactly one rule, `monitorUsesLiveInput` in `Project.h` - do not add a second. A session is a
  folder (`~/Music/DINE/<name>/` with `Audio Files/` inside); `SessionStore` is version 3 and still opens
  versions 1 and 2. Import audio with `MultitrackImport` (files and folders become tracks and clips - there is no
  separate "play a recording" audio path any more). **Every import - File > Import Audio Files, the
  launcher, the device page, a drop on TRACKS - goes through `MultitrackImport::plan` / `apply`**, and
  its rules are in the header: every file is a channel (one the name says nothing about plays in Music
  until it is set); folders are searched, hidden "._" files and bounce folders are not; numbers are
  counted (2 before 10), a leading number is the channel and a take number or a desk's time stamp is
  not part of the name; files that ran at the same time (broadcast-WAV stamp, else the file's birth
  time) are one pass and later passes follow on the same tracks, so DINE's own `LEAD_001..006` is
  three tracks, not six; a desk's multichannel card file is one track per channel
  (`AudioClip::fileChannel`) with its chunks chained; split stereo of one length is paired, two
  toms never are; an import *adds* to the session, and an empty track already set
  up takes the file with its name (or the only file of its source, or - for a card file - its console
  channel). From the launcher an import is a new session. `dine_device_check --import <paths>` prints
  what an import would make of them without opening a device. **Track > New Track** (and a right-click
  below the last track) makes an empty track of a chosen source on the next free device channel. Export is `MixBounce::renderProject`, streamed to disk.
  App tests for all of this: `build/app/dine_app_tests` (`app/Tests/DawTests.cpp`).
- **A stereo source is two channels, linked** (2026-10-07). Nothing shows a pair as one channel: each side
  is its own strip on the Mixer and its own track on Tracks, with its own meter, recorded to its own mono
  file. The two are marked `InputAssignment::stereoSide` (-1 left, +1 right, the right straight after its
  left), linked (`MixParameters::linkGroup`: fader and solo together; mute and pan each its own), panned hard
  left and right, and armed together (`DawEngine::toggleArmed`). `splitStereoInputs` turns any one-strip
  stereo input (`inputB`) into that pair wherever it arrives - `MixController::setSession`,
  `DawEngine::setSession`, an import, a saved patch, a session saved before version 11
  (`Project::splitStereo` in `applySession`, the right side taking the pair's chain and fader through
  `pairStereoSides`, and a two-channel take reading its left and right channel). The Inputs page still
  draws a pair as one row with its link. Tests: the `Stereo:` cases in `app/Tests/ReliabilityPassTests.cpp`.
- **Before the service (2026-09-24).** Three things an engineer reaches for during a service, each one press:
  - The emergency keys, DIM and MUTE, on the toolbar beside BYPASS (and under View): the broadcast and the room
    pulled down 20 dB, or silenced, on every feed but the engineer's listen (`MixController::setBroadcastDim /
    Mute`, an overlay in `compose()` that the engine ramps per block on the non-monitor feeds). Lit while on; never
    kept, saved or undone - a session must not open muted; LIVE SAFE never locks them.
  - SCENES, a row on LIVE between the group tiles and the monitor card: four pads (Band, Speech, Worship, Custom)
    with a KEEP chip each. KEEP writes the kept mix and the macros into the slot under the inputs' names; a press
    on the pad brings the whole mix back as one undoable change (LIVE SAFE lets it through; solo and the monitor
    are the engineer's and stay), and is refused with a sentence on a different set of inputs. `MixScene` in
    `app/native/MixHistory.h`; saved in the session document (`scenes`); each recall is a record in every
    changed channel's history ("Scene: Band").
  - GAIN STAGING, ON THE PATCH ITSELF (`AssignPage`, 2026-09-29). ROUTING > Inputs is the one screen somebody is
    on with a hand on the preamps, and it now says what each of them should do: a held peak per input ("Loudest so
    far" - up instantly, down at 3 dB a second, so a hit can be walked from the stage to the desk), a live mark for
    where the level is this instant, and the verdict beside it ("At the desk": OK, PREAMP UP 8 dB, PREAMP DOWN 6 dB,
    CLIPPING - PREAMP DOWN 10 dB, CHECK THIS INPUT, NO SIGNAL). The foot counts the ones with a move to make, before
    Continue. The verdict is `MixController::liveCaptureAdvice`, which reads the held peak against **the same
    capture range Tune uses for that source**, out of the same profile - a soundcheck and a tune must never disagree
    about whether an input is hot - and needs no plan, no engine and no strip. This page is also the one set-up page
    the window ticks (`MainView::timerCallback`), because a page of live numbers that only moves when somebody types
    is worse than no numbers.
  - CHECK INPUTS (View > Check Inputs..., `app/ui/CheckSheet`): every assigned input with its level now, its peak
    since the sheet opened, and one word - OK, SILENT (nothing above -60 dBFS for three seconds), LOW (never above
    -30), HOT (over -6), CLIP - under a headline that counts them ("13 inputs · 11 OK · 2 silent"). Reading only.
- A session opens whether or not the console it was recorded on is plugged in (2026-09-24, `app/native/DevicePlan.h`):
  its own devices when they are here; otherwise whatever is open already; otherwise an output alone so the
  recording still plays; otherwise no device and the Audio device page. Every case but the first is a sentence on
  the "Opened" toast naming the missing device and where to fix it - never a refusal. The same plan runs at launch
  for the last session. `HostServices::openDevicesFor` is the one place that opens devices for a document.
- **The microphone prompt is explained before macOS puts it up** (2026-09-29, `app/ui/MainView::explainMicrophone`,
  `app/Main.cpp`). macOS gates every audio input behind one switch and cannot tell a thirty-two channel desk from
  the built-in mic, and it puts its prompt up the moment a process *starts* an input stream - which, on a restored
  session, is a second after launch, before anybody has asked for anything and with nothing on screen to explain
  it. (Enumerating devices costs nothing: JUCE creates the CoreAudio IOProc in `start()`, not when a device is
  listed.) So on the one launch where macOS has never been answered and the session is about to open a console,
  DINE says what it wants it for first: what it listens to, what it does with it, and that saying no costs
  nothing but the meters. **The answer is honoured.** *Not now* - including Escape, or the sheet dismissed any
  other way - opens the output alone (`openState (state, source, allowInputs = false)`), and the toast is
  `inputsNotAskedSentence`, which names the device and points at Audio device; it does not read like a failure,
  because nothing failed. A recovery offer outranks it: one sheet at a time, and a recovery is the bigger
  question. `MicPermission::request` is still called from exactly one other place - Continue on the Audio device
  page - and `check()` never prompts.
  *A dev build asks every launch and that is not a bug:* TCC stores the grant against the code signature, and a
  local build is ad-hoc, linker-signed, so the signature changes every time. `scripts/package.sh` (Developer ID +
  notarized) is asked once, ever.
- DINE standalone (2026-09 pivot; see `docs/ARCHITECTURE-DINE.md`): the mix layer lives in `src/Mix`
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
  a 150 px column with LUFS-I / Short / True pk / Limiter / Target under its fader.
  **The group buses are pinned beside it (2026-09-28).** A group is not one more channel: it is what an
  engineer reaches for when something is wrong with a whole section, and on a thirty-two channel console it
  used to be seven screens to the right of wherever the pointer was. So every used group bus sits in a fixed
  rail left of the master, at the narrow width whatever width the *channels* are set to, with a gutter and a
  hairline for a seam. They are the same `Strip` objects moved between the bank and the page rather than drawn
  twice - a bus meter is consumed when it is read (`consumeMaxPeakDb`), so two widgets on one bus would each
  get half its peaks. The All / Inputs filter is about the bank and never takes the rail away; only GROUPS
  ("show me the groups and the master") puts them back in the bank at full width, and LIST has no rail at all.
  `MixerPage::pinnedGroupCount()` is what `dine_ui_tests` asks; LIVE has carried the same four controls per
  group as tiles since Milestone 7. A STRIPS column reads top to bottom the way a console does:
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
  Split, Marker, Loop, what is selected and the zoom) and then one 44 px ruler band that holds the loop
  strip along its top, the marker lane under it (click to jump, drag to move,
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
  **The loop** (2026-09-21): drag the empty strip along the top of the ruler to mark one; the bar it draws has
  a grip at each end (drag to change it), its middle moves it, a click on it switches it on or off, and so does
  the Loop button in the tool row. The part that goes round is drawn over the lanes too - an edge line either
  way, a wash while it is on - *after* the lanes, which are opaque planes and hid it before. `snapSample` takes
  `ignoreLoop` for the loop's own drags: with the loop's edges as snap targets the edge under the pointer snapped
  back to where it was a moment ago and a slow drag never got anywhere, which is what "I can't extend it" was.
  **TUNE on the header** (`tuneCell`): the chip between the name and the keys, calling `onTuneStrip` - the same
  TUNE CHANNEL the header's menu offers; a panel too narrow for it keeps the name and drops the chip.
  **Audio files from the Finder** (`FileDragAndDropTarget`, `addAudioFiles`): dropped on a track they become
  clips on it at the drop moment (snapped; on the channel panel means the start), the files after the first
  going down the tracks below; dropped below the last track each file is a new track - a new *input* on device
  channels past every assigned one, named and source-guessed from the file name by the same `MultitrackImport`
  as File > Import (a folder can be dropped too), an unrecognised source playing in Music for the header's
  menu to put right - and the session is rebuilt the way the
  ASSIGN page rebuilds it. Clips-only drops are undoable; a drop that made tracks is not (the undo stack holds
  projects, not sessions).
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
  stage owns), RE-TUNE and REVERT, then HISTORY - the channel's own record, newest first: every tune that
  landed on it (TUNE MIX, TUNE CHANNEL, TUNE LIVE MIX, a Mix Buddy request) and every hand edit of its
  chain, each with the clock, a count line (settings, level, gain, pan, sends) and the changes in words
  ("High-pass  80 Hz to 100 Hz"), and a PUT BACK chip that restores that setting on this channel alone
  (`MixController::restoreStripTune`; `docs/DINE-MIX-ENGINEER.md`) - and the headroom (the master shows
  its loudness instead). `AdvancedPage::revealHistory` scrolls the column to the section. The chip
  labels and readouts come from `chainStages` in `ChainStrip`, so the path, the mixer's INSERTS and the
  strip along the foot of a workspace can never disagree. The limiter stage appears on the master only and
  the width stage on stereo channels only, because that is where `MixEngine` configures them; a kick, snare or
  tom strip carries the SAMPLE stage between GATE and EQ (`hasSampleStage` in `ChainStrip.h` is the one rule every
  list reads): its lamp switches the sample in, and its device has BLEND, SENSITIVITY, LEVEL, PITCH, ALIGN, RISE,
  MASK, the two LISTEN bands, SOUND (the names the `SampleLibrary` loaded), FEEL (follows the drummer / steady),
  TUNING (as recorded / follows the drum), POLARITY and HEAR IT (the chosen sound once, into the engineer's listen
  where solo goes - `MixController::auditionSample` / `MixEngine::auditionSample`; the broadcast never hears it, and
  with no solo output the toast says so); its sentence carries "N hits played, M held back" and its chip's bar lights
  while a sample plays - `docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md`; the sends
  close the path where the session uses FX. "Hand-edited" is a diff against `getPlan()->proposed`, which is
  also what `Back to DINE` and REVERT put back. Edits go through `MixController::setStripChannel` /
  `setBusChannel` as a whole `ChannelParameters`: they live on the kept mix beside the faders, survive a
  macro, are saved with the session, and the next TUNE MIX replaces them the way it replaces a fader.
  BYPASS disables them.
  Gain staging is the first move in a mix and the app says so before it says anything else:
  `MixController::getInputAdvice (strip)` is the one place that reads the plan and answers what
  one input's level needs (Faint / NotHeard / Low / Hot / Clipping / Digital / Healthy, the
  console move in dB and the sentence). `Digital` is the one that matters in a church - the
  level works, but only because DINE raised it more than `digitalGainAdviceDb`
  (`MixProfileData`) digitally, which lifts the preamp's noise with the source. It is surfaced
  as a chip on the TRACKS header and the MIXER strip (both layouts), as the GAIN STAGING card
  at the top of the Inspector's right column, as the plan's *first* note (naming the inputs)
  and in `getMixHealthNotes`; an input that only works on a big digital raise is not counted
  healthy. Advice only: nothing about the mix changes.
  TUNE opens with **the scope picker** (2026-09-28): the whole mix, one group by name, or
  some channels by name. It is `MixPage::ScopeSheet`, Escape closes it like any sheet, and
  the result card names what it ran on. `docs/DINE-MIX-ENGINEER.md` has the whole of it.
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
  **Each return on its own (2026-10-01).** MIXER has a Return strip per effect the session uses (Vocal Plate, Vocal
  Delay, BGV Hall, Snare Plate, Drum Room), after the last group and before the master; GROUPS shows them too. Its
  fader is `FxSlotParameters::returnDb` - the level TUNE MIX set for that return - through
  `MixController::setFxSlotReturn` (LIVE SAFE's fader step applies); M is `FxSlotParameters::mute` through
  `setFxSlotMute`, kept through BYPASS and reset like any engineer's mute, saved with the session ("mute" in each fx
  entry; absent = not muted); S is the existing per-return solo. The FX group fader and mute still ride every return
  on top.
  BYPASS (toolbar, Mix menu, `B`) is `MixController::setBypass`: `compose()` returns `startingPoint()` with
  `bypassProcessing`, carrying only mute and solo across, so you hear the console feed. It never touches the kept
  mix - switch it off and the mix is exactly as it was - and faders are disabled while it is on.
  Verify with
  `build/app/dine_ui_snapshots <dir>` and the real stems: `build/app/dine_mix_stems "<stems folder>" 30 <outdir>`
  (writes raw/before/after/after-retuned WAVs, exit 0 = re-plan on the same listen changed nothing). App tests:
  `build/app/dine_app_tests`; real device: `build/app/dine_device_check 3`; recording playback through the host: `build/app/dine_device_check 4 "<stems folder>"`.
  The app is **DINE** (DINELIVE until 2026-09-10, DLIVE until 2026-10-01): target `DineApp`, product `DINE`, bundle
  `com.dine.app`, tools `dine_{ui_snapshots,app_tests,mix_stems,device_check}`, sessions in `~/Music/DINE/`
  as `<name>.dine.json`. Sessions written under the old name still open and are still listed - `SessionStore`
  accepts `app: "DINELIVE"`, scans `*.dinelive.json` and reads `~/Music/DINELIVE` (`formerNameFolder`) - and are
  written back under the new extension when they are next saved. The app icon is `app/resources/AppIcon.png`
  (1024 px, generated art: the D with a fader cap), handed to JUCE as `ICON_BIG`. The DINE plug-in family and the
  `Dine::` design tokens keep their name; only DINELIVE became DINE.
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
  In the app, Import a multitrack... on the device page (or File > Import Audio Files...) turns a stems
  folder into tracks and clips, with names and sources guessed from the file names.
  **"Arm" is not a word the app says.** A volunteer does not know it, and the message they meet when they press
  Record is the worst place to teach it. The field stays `Track::armed`, the key stays the red **R** (a console
  key, beside A/M/S), and every sentence around it is plain: "set to record", "N TO RECORD", "No tracks are set
  to record yet - press the red R on each track you want". The engineer's word appears once, in the R key's
  tooltip, as the thing it is called elsewhere. The TRACKS keys are drawn by hand, so `TracksPage::getTooltip`
  is where R, A, M, S and the fader say what they are; the tool row's **All to record** button
  (`setAllToRecord`) is the one click for a whole session, and it mirrors the Track menu.
