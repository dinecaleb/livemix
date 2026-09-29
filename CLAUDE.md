# Dine / DLIVE (repo LiveMix / Calive) — notes for Claude Code

One engine (`src/`, C++20, JUCE-free), six Dine channel plug-ins + Dine FX (`modules/`), and DLIVE, the live
recording and broadcast DAW (`app/`). This file holds the invariants and the map. Everything else is a topic
file under `docs/` — read the one for the area you are touching before changing it.

## Hard invariants

- **The audio thread never allocates, locks, logs, builds strings or does I/O.** That is `process()`,
  `processBlock()`, `MixEngine::process`, `Recorder::write`, the CoreAudio callback and everything they call.
  Enforced by `tests/AllocationTracker` and by RealtimeSanitizer over the entry points marked
  `LIVEMIX_NONBLOCKING` (`src/Core/Realtime.h`; `-DLIVEMIX_RTSAN=ON`, `scripts/rtsan.sh`,
  `docs/REALTIME-SANITIZER.md`). A violation is fixed or documented in `scripts/rtsan.supp` with its reason.
- **`src/` stays JUCE-free.** JUCE lives in `src/State/ParameterLayout|Bridge`, `src/UI`, `modules/`, `app/`.
- **Numbers live in the profile data, nowhere else.** Per-source targets and safe ranges in
  `src/Profiles/ProfileData.cpp`; mix-level rules in `src/Profiles/MixProfileData.cpp`; FX characters in
  `src/FX/FxProfiles.cpp`. A strategy holds decision logic, never a target. Modern Gospel is the default; every
  other profile is a documented delta on it. Never add a profile that cannot be tuned by listening.
- **Tune is deterministic, repeatable and idempotent.** Analysis + profile targets + source strategy give a
  bounded starting point; the same listen and settings give the same mix; a re-tune on the same capture says
  NO CHANGE REQUIRED (tested). Cut rules compute from the profile template, never from the current value;
  every level decision is absolute from the capture, never "current + delta" - a bound measured from
  `atCapture` (the mix the listen ran through) is still absolute, which is how the re-tune step limit and the
  group balance stay idempotent. Strategies edit a `ChannelParameters` inside `TuneDecisions::move()` and
  changes are recorded by diffing - never hand-write a `ParameterChange` list. A high-pass never goes above
  0.8 x the measured fundamental (`capHighPassToFundamental`, which outranks the profile's own minimum); a
  sustained source never gets an expander; a room microphone is never gated; a boost and a cut never land
  inside the same octave in one pass.
- **A mix has one thing it is built around.** `MixPlanner`'s `focal` - the pinned input, else the lead
  microphone somebody is really singing into - is what every hierarchy rule means by "the lead". Levels are
  measured from where a source will actually land, never from where the profile wishes it were. The master is
  fitted to a band, so a sermon-only listen leaves it alone and sets the speech group by what leaves the mix.
  A listen with no performance in it is refused rather than mixed.
- **Two things move a level by themselves, and both are off by default.** *Speech priority* ducks DRUMS, BASS
  and MUSIC into the master while the speech group is open. Never the voices, never the room, never the
  returns, and never the engineer's listen - it is applied where a group is summed into the master, after the
  monitor has taken its copy.
  *Autopilot* is the second, added 2026-09-28. It is **deterministic - no AI anywhere in it** - off by default,
  engaged only by explicit action, shown as AUTOPILOT on every workspace while it is on, bounded by
  `MixSafetyValidator`, limited to **group faders only** and to +/- `maxTotalDb` of the mix it was engaged on,
  and every move it makes is a Mix history entry with the sentence that says why. It never touches EQ,
  dynamics, a channel, the room, the returns, the master fader or the engineer's listen. It runs on the
  message thread off `MixController::poll` and never on the audio thread. **Within tolerance it does nothing,
  and that is the default outcome** (`tests/Mix/AutopilotTests.cpp` asserts it). One press turns it off, and an
  engineer's own move on a fader it had been correcting hands that fader straight back.
- **AI is optional, validated and never auto-applied.** Default Off, explicit user action only, a
  `SafetyValidator` / `MixSafetyValidator` on every path, the deterministic result as the fallback on any
  failure, AI output never enters the proposed parameters, nothing AI-related on the audio thread, and a
  reopened session never contacts a provider. The plug-ins' AI assist is switched off on purpose
  (`kAIAssistAvailable = false`); keep the code paths, do not re-enable without being asked. In DLIVE the
  reasoning layer (TUNE LIVE MIX, MIX BUDDY) sits **above** `MixPlanner`, never instead of it: the
  deterministic plan is built first and always.
- **Never rename a released parameter ID**; sessions, presets and automation depend on them. Enums that are
  stored (`StyleProfileId`, `MixBus`, roles) are appended to, never reordered; a stored-layout change bumps
  `SessionStore`'s version and remaps the old one. A group bus goes in immediately before `MASTER`, so
  `b < int (MixBus::Master)` keeps meaning "the groups"; what a person *sees* follows
  `mixBusInDisplayOrder`, never the enum, because the enum is the storage.
- **Latency is reported honestly, and so is the ceiling.** The channel path is sample-synchronous and
  minimum-phase and adds none; the lookahead limiter (`Limiter::kLookaheadMs` = 1.5 ms) exists only where the
  stage is turned on (Dine Master, DLIVE's master bus) and is reported through `setLatencySamples` constantly,
  on, off or in an A/B. Its ceiling is a **true** peak: the detector is 4x oversampled, because a number
  printed as dBTP has to be one. Do not change DSP behaviour without meaning to: `tests/reference/*.f32`
  regression renders must keep passing.
- **Plain words on the surface.** Anything a volunteer sees in Simple view or on a DLIVE workspace is plain
  language (WARMTH, CLARITY, SMOOTH, STEADY, CLEAN-UP, LOUD, "Only I hear it", "set to record"); engineer terms
  (gate, comp, de-ess, AFL, aggregate device, arm) appear once each, in Advanced or a tooltip. The verbs are
  TUNE / RE-TUNE / TUNE KIT / TUNE MIX / TUNE LIVE MIX; "Mix Buddy", never "chat". Tune explains WHAT then WHY
  in sentences, and a refusal always carries the sentence saying why.
- **A take survives a crash** (`app/native/Recorder`): the writer thread keeps the WAV header current and a
  `<take>.wav.recording.json` sidecar beside it; a sidecar found on the next open is repaired and put back on its
  track. Nothing about recording moves to the audio thread.
- **The session is a document, not a side effect of an open device.** `SessionState` (`app/native/SessionState.h`)
  is the one owned model and the only thing `SessionStore` serialises; `captureSession` / `applySession` are the
  only ways in and out, and both are compiled into `dlive_app_tests` - nothing assembles a session out of getters,
  anywhere. `MixController::rebuild()` builds the graph and carries the mix across it with no device and no sample
  rate; `prepare()` builds the audio graph and never decides whether the state exists. Saving follows
  `MixController::touch()`'s revision, never a UI call site. A session survives a crash the way a take does
  (`app/native/SessionAutosave`): an autosave and a clean-exit marker sit beside the document, and one found on the
  next launch offers Recover / Open last saved / Keep both. `docs/SESSION-STATE.md` is the audit and the contract.
- **The design is the Figma file, and `docs/DESIGN-V3.md` is the map to it.** The DLIVE
  application is built to "DLIVE - Full UX Mockup" (v3, 2026-09-29): Inter and IBM Plex Mono,
  flat neutral planes, one 52 pt toolbar with the window's own buttons inside it, a 208 pt
  sidebar in sentence case that folds to a 52 pt icon rail, and a 28 pt status foot. **The only
  capitals in the product are its verbs** - `Dine::drawSection` puts a caption back into
  sentence case, so a caption written in capitals is corrected rather than shouted. Read
  `docs/DESIGN-V3.md` before moving anything in `app/ui`; it also lists, with the reason, every
  place the code and the design disagree.
- **UI changes are verified by looking at the PNGs** (`dlive_ui_snapshots`, the per-product
  `livemix_*_ui_snapshots`), never by reasoning about layout code; regression references change only when a
  baseline changes on purpose. No page repaints itself wholesale from its tick; a page's `paint` and its `Look`
  move together. Colour literals belong in `AppTheme` only, and a new widget re-reads its tokens in
  `lookAndFeelChanged()`. **Text is drawn through `Dine::drawText` / `Dine::drawFittedText`, never through
  `g.drawText`**: JUCE's own layout cache holds 128 strings for the whole window and is an LRU, which a
  workspace cycles straight through, so every paint re-shapes every string. Same arguments, same pixels
  (`app/Tests/TextCacheTests.cpp` asserts they are identical); `dlive_ui_snapshots --frames` prints the
  layout count per workspace, and that count is the figure a frame-budget regression shows up in.
  **A fixed word is never ellipsised.** A name is data and is cut when it will not fit; CLOSE drawn as
  "Clo..." is a cell that is too small. `dlive_ui_snapshots <dir>` ends with a TEXT CLIPPING report naming
  every string that lost characters and the screen it was on, and **that report is expected to be empty** -
  widen the cell, or let the control say the same thing in fewer words (`DinePopup::setBriefValue`, a brief
  form beside the long one, `Dine::shortPath` for a file path). `Dine::drawFittedText` squeezes instead of
  cutting and is the right call for a name on a narrow strip.
- **LIVE SAFE is a policy in `MixController`**, not a menu guard; **BYPASS never touches the kept mix**;
  **solo never changes what the room hears**.


## Build and check (the short form; all of it in `docs/BUILD-AND-VERIFY.md`)

```sh
export PATH="$HOME/.local/bin:$PATH"     # cmake / ninja from uv tool
scripts/build.sh                         # everything, Release, into build/
scripts/dlive.sh [--build|--tests|--shots|--debug]      # the app alone: the fast loop
cmake -S . -B build-engine -G Ninja -DLIVEMIX_BUILD_PLUGIN=OFF && cmake --build build-engine   # engine only
scripts/test.sh && build/app/dlive_app_tests            # ctest + benchmark, then the DAW/app suite
build/app/dlive_mix_stems "<stems>" 30 <out> gospel     # TUNE MIX on a real multitrack; exit 0 = idempotent
scripts/package.sh                       # a zip for a tester (Developer ID + notarization via env vars)
```

One build at a time, `--parallel 4`, targeted targets rather than the whole project when iterating. CI
(`.github/workflows/ci.yml`) runs bootstrap, the build, ctest, the benchmark against
`scripts/benchmark-baseline.txt` (fails over 15 % slower) and `auval`, plus the RTSan job.

## Map

```
src/Core, DSP, Analysis, Tune, Profiles, Recommendations, Intelligence, FX, Communication, State, MixAI, Mix
  DSP/ChannelProcessor      the fixed chain: input, filters, gate, [sample], corrective EQ, de-esser, comp, transient,
                            tone EQ, saturation, width, output trim, [limiter], [loudness meter]
  State/ParameterSpecs.cpp  every parameter once, per product; visited by forEachDspField (DSP/ChannelParameters.h)
  Tune/                     TuneEngine + one strategy file per family; numbers come from Profiles/
  Mix/                      MixSession, RoutingGraph, MixEngine (the real-time graph), MixPlanner, MixMacros,
                            MonitorBus, OutputFeeds, LiveSafe, ReferenceMix
  MixAI/                    the reasoning layer above MixPlanner (RelationshipEngine ... TuneLiveCoordinator)
modules/Common              the one shared ChannelPluginProcessor/Editor; modules/<Product> names it; modules/FX
app/native                  MixController (no JUCE), DawEngine, Transport, Recorder, TimelinePlayer, AudioHost,
                            MonitorDevice, SessionState (the one model) + SessionStore (versioned) +
                            SessionAutosave (worker, atomic, recovery), InputMapStore, ThemeStore, MixBounce
app/ui                      MainView + the workspaces (TracksPage, MixerPage, MixPage = TUNE, LivePage,
                            AdvancedPage = Inspector), sheets (Outputs, Check, History, Chat, Theme, ChannelTune),
                            AppTheme (Dine:: tokens)
app/Tools, app/Tests        dlive_ui_snapshots / dlive_mix_stems / dlive_device_check; dlive_app_tests
tests/                      engine + integration tests, benchmark, reference renders
```

## Where the detail lives

| Topic | File |
| --- | --- |
| Build, test, snapshot, stems and AU commands; the real recordings | `docs/BUILD-AND-VERIFY.md`, `BUILD-RUN-SHARE.md` |
| What a session holds, who owns it, autosave, recovery and the mix history | `docs/SESSION-STATE.md` |
| The four-phase plan and what phase the work is in | `docs/ROADMAP-RELIABILITY.md` |
| The engine's rules: products, parameters, Tune, profiles, wording, FX | `docs/DINE-CORE-RULES.md`, `docs/ARCHITECTURE-DINE-CORE.md` |
| DLIVE the application: DAW layer, every workspace, setup pages | `docs/DLIVE-APP.md`, `docs/ARCHITECTURE-DLIVE.md`, `docs/MILESTONE-7.md` |
| The mix engineer: TUNE LIVE MIX, REFERENCE MIX, LIVE SAFE, MIX BUDDY, loudness, profiles, linked faders | `docs/DLIVE-MIX-ENGINEER.md`, `docs/ARCHITECTURE-DLIVE-AI.md` |
| Monitoring: the solo bus, two devices, Dante | `docs/DLIVE-MONITORING.md` |
| The v3 design: the Figma file, every frame, and where each one landed | `docs/DESIGN-V3.md` |
| The desktop design, macro pads, themes, tutorial, frame budget | `docs/DLIVE-DESIGN.md`, `docs/THEMES.md` |
| RealtimeSanitizer: wiring, findings, suppressions | `docs/REALTIME-SANITIZER.md` |
| Drum sample replacement: the scope and what was built | `docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md` |
| Milestone reports and the QA notes | `docs/MILESTONE-1..7.md`, `docs/QA-*.md` |

Read the PRD sections 6-8, 42, 48 and `docs/ARCHITECTURE-DINE-CORE.md` before touching the audio path or
adding a product. Work from `main`; commit each piece of work separately with a descriptive message.
