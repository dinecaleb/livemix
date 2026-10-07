# DINE v4 — where the build and the mockup differ, and why

The mockup is `docs/design/v4/DINE v4.html`; open it in a browser and compare it side by side with
`build/app-snapshots/*.png` (`scripts/dine.sh --shots`). Every row here is a decision, not an oversight.
`GAPS.md` lists what has no backend; this file lists what was built differently on purpose.

## The language (commit "v4 language")

| What | The mockup | The build | Why |
| --- | --- | --- | --- |
| **The accent, "on" and "fine"** | Apple green `#30D158` (meters, lamps, Running, Healthy) | DINE's teal `#6db8a8` | The owner's call on 2026-10-06: "keep our branding colors the signature green but take everything else". Bass keeps the mockup's green as its group colour - a group's identity, not the brand. |
| **SF Pro / SF Mono** | `-apple-system`, `ui-monospace` | The same faces, asked for by their CoreText family names (`.AppleSystemUIFont`, `.AppleSystemUIFontMonospaced`) | They may not be embedded and need not be; "SF Pro" by its public name falls back to Helvetica. Inter and Plex Mono stay embedded as the fallback and for the plug-ins. |
| **Translucent ink** | `rgba(235,235,245,.62/.34)` | The same values as ARGB tokens (`ink2`, `ink3`) | Exactly the mockup; the planes under them are the mockup's too. |
| **The v3 palette** | Gone | Kept as the built-in theme "Studio v3" | A booth that liked it can go back, and View > Appearance still edits every token. |
| **Long group names on a narrow TUNE tile** | Not drawn narrow | The first three letters ("Amb", "Spe") | The tile is 38 pt at the minimum window; a name is never cut mid-word. |

## The shell (this commit)

| What | The mockup | The build | Why |
| --- | --- | --- | --- |
| **The "Handoff" flag `kUseV3UI`** | Keep the old views compiling behind a flag until the end | No flag; each screen is rebuilt in place, one commit each, and every commit builds, passes `dine_app_tests` and leaves the clipping report empty | The flag exists so the app is never broken half-way. A second copy of every page would double the code that has to stay in step with the engine; one working app per commit does the same job, and `git revert` of one commit is the way back. The old palette is a theme. |
| **Traffic lights** | Inside the sidebar card when it is out; at the toolbar's left when it is hidden | At 25 / 45 / 65, centred on y = 32, in both states | They are the same place in the mockup: the card's top and the toolbar's left coincide. `WindowChrome.mm` puts them there. |
| **Sidebar shadow** | `0 10px 30px rgba(0,0,0,.35)` | A hairline only | On the `#1c1c1e` ground the shadow is all but invisible, and a live drop shadow is what the brief's performance section rules out. A pre-rendered one can be added if it is missed. |
| **Sessions count** | No badge | No badge | v3 counted sessions on that row; v4 does not, and the library says it. |
| **Broadcast readiness row** | No row | A row on "Ready to go live?" while a checklist is under way ("11 of 14 still to confirm by hand", Open checklist); the session menu lists it as "Broadcast Checklist…" | The checklist is an inventory item a person ticks; the Ready sheet only reads its progress, so the pill and the sheet count the same things. |
| **Auto / AUTOPILOT** | "Auto" | "Auto" while it is off, "AUTOPILOT" while it is on | CLAUDE.md: Autopilot is "shown as AUTOPILOT on every workspace while it is on". |
| **LIVE SAFE when on** | The switch | The switch and its word in amber | LIVE SAFE stays the one amber control in the product (CLAUDE.md, DESIGN-V3 §1). |
| **The ⌘ numbers** | ⌘1 Mixer, ⌘2 Tune, ⌘3 Inspector, ⌘4 Live, ⌘5 Tracks | The same | The owner took v4's order on 2026-10-06. |
| **The solo pill** | Click clears | The name goes to what is soloed, the cross clears | The owner's call (2026-10-06): a pill that clears on any click loses the one way to find what is soloed. |
| **The session menu's set-up lines** | Not drawn | Under a "Setup" heading at the foot of the menu: device, inputs, purpose, recording destination, each with its state | They were the v3 setup popover's whole point - how a volunteer finds out what is not done yet. |
| **The output pill** | "Main · solo headphones" | The broadcast device's name and "· solo …"; "Outputs" when the name will not fit | DINE names the device it is sending to; a name is never cut to half. |
| **The pulsing "on air" strip** | Not drawn | Removed | It was v3's, it repainted the window's width every tick while nothing else moved, and the status foot's "On air" cell says the same. |
| **The status foot** | Engine, CPU, Disk "34.2 GB · 9 h 40 m", recording, On air, dropped, Live safe, BPM, Autosaved | The same, plus "Monitor solo · AFL"; Disk says "258 GB - a day+" | The monitor cell is an inventory item. The time is at the armed tracks, or every input before any is armed. |
| **A toolbar piece with no room** | Not drawn narrow | It gives way whole: the clock, the session button, "· solo …", the solo names - never half a word | CLAUDE.md: a fixed word is never ellipsised. The transport's keys and the readiness pill never give way. |

## The Mixer (commit "v4 Mixer")

| What | The mockup | The build | Why |
| --- | --- | --- | --- |
| **The FX key** | Not drawn | At the right of a voice strip's "Sends" caption | Effects on this microphone is an inventory item (one press for the pastor who starts singing). v4 has no third key row, and growing one on every strip would bend the console's line. |
| **Effects filter, Sends toggle, Clear solo** | All / Inputs / Groups; no Sends toggle; no Clear solo | ... and Effects; Sends; Clear solo | All three are inventory items with no v4 home; they sit in the header row where they were. |
| **Effect returns** | Not drawn on the console | A family card of their own, "Effects", after the last group | Each return has its own fader on DINE's console (inventory). |
| **The inserts** | Three slots naming the inserts | Three slots naming the first three stages that are on, in chain order | DINE has a fixed chain, not insert slots; the slots read what the chain is doing, as v3's did. The full chain is on the chain card under the workspace. |
| **The edited dot** | A ring beside the level when a hand edit moved it | Not drawn | No backend says "this fader was moved by hand since the tune" (`GAPS.md`). The quick inspector's provenance line counts hand edits since the last tune. |
| **The gain chip's number** | "Clipping -6", "Digital +12", "Low +6" | The same, from `InputAdvice::consoleMoveDb` | The number is what the preamp should still move. A healthy input, or one not yet measured, says nothing more. |
| **The quick inspector's plain words** | "Clarity", "Steady" | "Clarity" / "Steady" when the EQ / compressor is on, "Off" when not | The rail reads the chain; it does not yet carry the macro words per stage. |
| **The LIST view** | A dense table | The v3 rows, restyled by the shared tokens and keys | The table's columns and behaviour are unchanged inventory; a denser v4 table is a later pass. |
| **Buttons, segments, keys, pan bars** | Pills, white lifts, coloured keys | The shared widgets were restyled (`DineButton`, `Dine::drawSegmentTrack`, `DineKey`, `PanBar`), so every page has them | v4's control language is the whole product's, not the Mixer's. A filled button with its own tint (a mute) keeps that colour; the primary action is the white pill. |
| **The quick inspector** | Always down the right | Folded until asked for: a "Channel" toggle at the right of the header (or `]`) opens it | The owner's call (2026-10-06): the console gets the width by default. Remembered per Mac once UI preferences land (C3). |
| **The white band under the console** | - | Fixed | An opaque viewport over its own unpainted scrollbar strip showed whatever the window held there; it was in the v3 snapshots too. |

## Tune (commit "v4 Tune")

| What | The mockup | The build | Why |
| --- | --- | --- | --- |
| **TUNE on each group tile** | Not drawn | Kept | Per-group TUNE (`startTuneBus`) is an inventory item; the scope sheet's "One group" reaches it too. |
| **The verbs under Voices** | Not drawn | Mix Buddy, Undo / Redo mix, Mix history, Open the Inspector, then the pad card | v3 kept them here for a service (DESIGN-V3 section 5); none is more than one press elsewhere, and they cost one block at the foot of the panel. |
| **The listening sheet** | A 30 s countdown ring and every input, heard or silent with why | Built (C4): the ring counts the seconds down (a sweep while it works or waits), the step lamps stay beside it, and every input says heard, or silent and why - muted, expected during the music (a speaking mic while the band plays), nothing arriving, or not yet - silent ones first | The live run's eight steps and "waiting for the band" are inventory, so the lamps stay. |
| **The master's pickers** | "Target: Broadcast -23 LUFS" and "For: Room and stream" under the numbers | The loudness target and "Sound: as tuned" in the heading row | The owner kept the eight stored master voicings (2026-10-06); v4's four "For:" words describe the purpose, which `MixPurpose` owns. |
| **The pads' corner words** | "Clear voices", "Big and present", "Tight and driving", "Big room" (top only) | The macros' own words (AIRY, PRESENT, ...) on all four corners; a pair that would touch on a small pad is left off | The words are `MixMacros`' and name what each corner does to the sound; renaming them is a profile-data change, not a UI one. |
| **The rail's role line** | Always under the name | Squeezed a little, then left off on a narrow rail | A name is the row; a cut role is the one thing the clipping report forbids. |

## The Inspector (commit "v4 Inspector")

| What | The mockup | The build | Why |
| --- | --- | --- | --- |
| **MUTE, SOLO and TUNE CHANNEL in the head** | Simple / Advanced and RE-TUNE | ... and MUTE, SOLO, TUNE CHANNEL | Inventory items on the head today; they give way first on a narrow window. |
| **Group buses in the channel rail** | Inputs only | Each family's bus at the end of its family, and the master | The buses and the master have to be inspectable somewhere (DESIGN-V3 section 5). |
| **The live hit visualiser on the Sample stage** | A scrolling waveform with fired / held / ghost markers and a velocity pad of the last 16 hits | The threshold line and the last hit, as before | It needs a per-hit event queue out of the engine, which is an engine change (`GAPS.md`, ENGINE); the UI does not change the audio thread to fit a drawing. |
| **Per-family Sample defaults** | Kick band 30-150 Hz; hi-hat blend 0.2, mask 20 ms, band 6-16 kHz | `ProfileData.cpp`'s values | Numbers live in the profile data and nowhere else (CLAUDE.md); the owner kept the profile's (2026-10-06). |
| **"Back to DINE" per control** | A link on the provenance line | Put back per stage (the card's menu), per record (the trail) and per channel | No per-control put-back exists; the stage-level one is one press. |

## Live (commit "v4 Live")

| What | The mockup | The build | Why |
| --- | --- | --- | --- |
| **The setlist, cues and "Up next"** | A cue header, This cue / Groups / All / Alerts, Up next with "Go to ... (Space)", the setlist with Now and Next, cue editing | Built (Part B). The Space hint is on Up next's line ("Cue 4 - Band - Space") rather than inside the white pill | A second ink inside a filled button would be the only one in the product. |
| **The cue editor's "Who's on?"** | Per group: on / off and Softer / Normal / Up front | A cue recalls a scene (one of the four, a kept favourite, or "as it is") and carries two notes, Louder and Softer | The owner's model (2026-10-06): a cue is an ordinary scene recall with its Mix history entry. Per-group levels per cue would be a second scene system. |
| **The health strip on LIVE** | Not drawn | Kept over the strips | Recording, On air, Clipping and Headroom / true peak are inventory; the status foot has the first two only. |
| **The scenes on LIVE** | Not drawn | A row under the cue header: Scene BAND SPEECH WORSHIP CUSTOM and Keep | Recall on click and Keep are inventory; a cue names them. |
| **Each effect** | - | The All view ends with each effect's own fader; the FX strip's name opens All scrolled to them | "Each effect / Back to groups" is inventory; All is where v4 puts every strip. |
| **LIVE SAFE and Autopilot cards on a short rail** | - | Their header line and link only, or left out when not even that fits | The toolbar has both switches; a card is never drawn as a sliver. |
| **Needs attention, short rail** | - | A row shows its whole sentence or only its name; never a cut sentence | Found at the new 1280 x 780 minimum, where the card first has room. "Check inputs" says the rest. |
| **Needs attention** | A card of inputs to fix, each with a button ("Done", "Notch it") | Built: the inputs whose gain the desk should still move, each "<name> is clipping" and the sentence of what to do, and "Check inputs >" in its header | The verdicts are `InputAdvice` - the same as Check inputs, the Mixer's chip and TUNE. A per-row "Done" has nothing to record and "Notch it" is a feedback tool DINE does not have, so the card has the one action that exists. It takes its room before the speaking mics' card, which also lives in the Mix menu. The snapshot set has no frame with both an input flagged and room on the rail, so no PNG shows it yet. |
| **Six effect returns** | Five (the mockup's list) | Vocal Plate, Vocal Delay, BGV Hall, Snare Plate, Drum Room and **Band Hall** on LIVE's effects strip, Tune's "Each effect", the Mixer's Effects card and the Inspector's sends | Band Hall (2026-10-06) is the musicians' own reverb: keys, pads and guitars had none. |
| **Strips per input on LIVE** | The cue's inputs as strips, with Autopilot's "Auto +1.5" chips | All and Alerts are a strip per input (Alerts: those the desk should move); This cue is the groups that are on, Groups every group | Autopilot moves group faders only (CLAUDE.md), so the "Auto" chips stay off the input strips. |

## Tracks (commit "v4 Tracks")

| What | The mockup | The build | Why |
| --- | --- | --- | --- |
| **The header's fader and TUNE** | A level bar under the name and the dB beside it | The horizontal fader, the meter, the dB and TUNE as before | The fader and per-track TUNE are inventory items on the header; v4's header simply has fewer things on it. |
| **The tool row** | S M L, Snap / Follow, Split, Marker, the selection, zoom, All to record | ... and All to input, Loop and Fit | Inventory items; the row's order is v4's. |
| **The header's state lamp** | A group-colour bar | A group-colour bar | The lamp said armed / monitoring / muted, which the R, A and M keys beside it already say. |

## Inputs (commit "C1: Inputs")

| What | The mockup | The build | Why |
| --- | --- | --- | --- |
| **Feeds and Source in the right panel** | Pickers | Read as text: the group the role feeds, "Input 3" | A feed other than the role's and moving an input to another port have no backend (`GAPS.md`: SESSION, host). |
| **The table's port and record columns** | "Mic/Line 3" and a record dot per row | The channel number; Record it is in the right panel | The device's own channel names have no accessor yet (`GAPS.md`). |
| **The table on a narrow window** | Every column | The arriving meter gives way first, then the group's cell narrows, then the name | The role popup and the verdict never shrink; nothing is cut. |
| **A click on a row** | Edits it in the right panel | The same; Cmd-click adds to the selection, Shift-click a range | Before v4 a click toggled the selection; Cmd-click now does. |
| **The verdict** | "Healthy", "Clipping -6" | The same words as the Mixer's chip | It replaced the capitals ("CLIPPING - PREAMP DOWN 10 dB"); the right panel and the banner say the sentence. |
