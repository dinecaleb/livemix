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
