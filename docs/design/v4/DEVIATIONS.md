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
| **Broadcast readiness row** | No row | The readiness pill opens it when the purpose is Church Broadcast or Livestream; the session menu lists it as "Broadcast Checklist…" | The checklist is an inventory item with no v4 home of its own; the pill is where v4 puts "is everything ready". |
| **Auto / AUTOPILOT** | "Auto" | "Auto" while it is off, "AUTOPILOT" while it is on | CLAUDE.md: Autopilot is "shown as AUTOPILOT on every workspace while it is on". |
| **LIVE SAFE when on** | The switch | The switch and its word in amber | LIVE SAFE stays the one amber control in the product (CLAUDE.md, DESIGN-V3 §1). |
| **The ⌘ numbers** | ⌘1 Mixer, ⌘2 Tune, ⌘3 Inspector, ⌘4 Live, ⌘5 Tracks | The sidebar is in v4's order; the keys are still ⌘1 Tracks … ⌘5 Inspector, and the tooltips say so | A decision for the owner (`GAPS.md`); `ReachabilityTests` asserts today's table. |
| **The solo pill** | Click clears | The name goes to what is soloed, the cross clears | A decision for the owner (`GAPS.md`); today's rule is kept until then. |
| **The session menu's set-up lines** | Not drawn | Under a "Setup" heading at the foot of the menu: device, inputs, purpose, recording destination, each with its state | They were the v3 setup popover's whole point - how a volunteer finds out what is not done yet. |
| **The output pill** | "Main · solo headphones" | The broadcast device's name and "· solo …"; "Outputs" when the name will not fit | DINE names the device it is sending to; a name is never cut to half. |
| **The pulsing "on air" strip** | Not drawn | Removed | It was v3's, it repainted the window's width every tick while nothing else moved, and the status foot's "On air" cell says the same. |
| **The status foot** | Engine, CPU, Disk "34.2 GB · 9 h 40 m", recording, On air, dropped, Live safe, BPM, Autosaved | The same, plus "Monitor solo · AFL"; Disk says the hours left, not the gigabytes | The monitor cell is an inventory item. Bytes free has no accessor yet (`GAPS.md`). |
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
| **The white band under the console** | - | Fixed | An opaque viewport over its own unpainted scrollbar strip showed whatever the window held there; it was in the v3 snapshots too. |

## Tune (commit "v4 Tune")

| What | The mockup | The build | Why |
| --- | --- | --- | --- |
| **TUNE on each group tile** | Not drawn | Kept | Per-group TUNE (`startTuneBus`) is an inventory item; the scope sheet's "One group" reaches it too. |
| **The verbs under Voices** | Not drawn | Mix Buddy, Undo / Redo mix, Mix history, Open the Inspector, then the pad card | v3 kept them here for a service (DESIGN-V3 section 5); none is more than one press elsewhere, and they cost one block at the foot of the panel. |
| **The listening sheet** | A 30 s countdown ring | The step lamps and the progress line it had | The live run's eight steps (`TuneLiveCoordinator`) and the "waiting for the band" state are inventory; a ring alone loses them. The sheet takes v4's buttons and type. |
| **The master's pickers** | "Target: Broadcast -23 LUFS" and "For: Room and stream" under the numbers | The loudness target and "Sound: as tuned" in the heading row | The voicing list is the owner's decision (`GAPS.md`): v4's four "For:" choices are not the eight master voicings DINE has. |
| **The pads' corner words** | "Clear voices", "Big and present", "Tight and driving", "Big room" (top only) | The macros' own words (AIRY, PRESENT, ...) on all four corners; a pair that would touch on a small pad is left off | The words are `MixMacros`' and name what each corner does to the sound; renaming them is a profile-data change, not a UI one. |
| **The rail's role line** | Always under the name | Squeezed a little, then left off on a narrow rail | A name is the row; a cut role is the one thing the clipping report forbids. |
