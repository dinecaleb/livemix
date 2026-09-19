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
  every level decision is absolute from the capture, never "current + delta". Strategies edit a
  `ChannelParameters` inside `TuneDecisions::move()` and changes are recorded by diffing - never hand-write a
  `ParameterChange` list. A high-pass never goes above 0.8 x the measured fundamental; a sustained source never
  gets an expander; a room microphone is never gated.
- **AI is optional, validated and never auto-applied.** Default Off, explicit user action only, a
  `SafetyValidator` / `MixSafetyValidator` on every path, the deterministic result as the fallback on any
  failure, AI output never enters the proposed parameters, nothing AI-related on the audio thread, and a
  reopened session never contacts a provider. The plug-ins' AI assist is switched off on purpose
  (`kAIAssistAvailable = false`); keep the code paths, do not re-enable without being asked. In DLIVE the
  reasoning layer (TUNE LIVE MIX, MIX BUDDY) sits **above** `MixPlanner`, never instead of it: the
  deterministic plan is built first and always.
- **Never rename a released parameter ID**; sessions, presets and automation depend on them. Enums that are
  stored (`StyleProfileId`, `MixBus`, roles) are appended to, never reordered; a stored-layout change bumps
  `SessionStore`'s version and remaps the old one.
- **Latency is reported honestly.** The channel path is sample-synchronous and minimum-phase and adds none;
  the lookahead limiter (`Limiter::kLookaheadMs` = 1.5 ms) exists only where the stage is turned on (Dine
  Master, DLIVE's master bus) and is reported through `setLatencySamples` constantly, on, off or in an A/B.
  Do not change DSP behaviour without meaning to: `tests/reference/*.f32` regression renders must keep passing.
- **Plain words on the surface.** Anything a volunteer sees in Simple view or on a DLIVE workspace is plain
  language (WARMTH, CLARITY, SMOOTH, STEADY, CLEAN-UP, LOUD, "Only I hear it", "set to record"); engineer terms
  (gate, comp, de-ess, AFL, aggregate device, arm) appear once each, in Advanced or a tooltip. The verbs are
  TUNE / RE-TUNE / TUNE KIT / TUNE MIX / TUNE LIVE MIX; "Mix Buddy", never "chat". Tune explains WHAT then WHY
  in sentences, and a refusal always carries the sentence saying why.
- **A take survives a crash** (`app/native/Recorder`): the writer thread keeps the WAV header current and a
  `<take>.wav.recording.json` sidecar beside it; a sidecar found on the next open is repaired and put back on its
  track. Nothing about recording moves to the audio thread.
- **UI changes are verified by looking at the PNGs** (`dlive_ui_snapshots`, the per-product
  `livemix_*_ui_snapshots`), never by reasoning about layout code; regression references change only when a
  baseline changes on purpose. No page repaints itself wholesale from its tick; a page's `paint` and its `Look`
  move together. Colour literals belong in `AppTheme` only, and a new widget re-reads its tokens in
  `lookAndFeelChanged()`.
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
  DSP/ChannelProcessor      the fixed chain: input, filters, gate, corrective EQ, de-esser, comp, transient,
                            tone EQ, saturation, width, output trim, [limiter], [loudness meter]
  State/ParameterSpecs.cpp  every parameter once, per product; visited by forEachDspField (DSP/ChannelParameters.h)
  Tune/                     TuneEngine + one strategy file per family; numbers come from Profiles/
  Mix/                      MixSession, RoutingGraph, MixEngine (the real-time graph), MixPlanner, MixMacros,
                            MonitorBus, OutputFeeds, LiveSafe, ReferenceMix
  MixAI/                    the reasoning layer above MixPlanner (RelationshipEngine ... TuneLiveCoordinator)
modules/Common              the one shared ChannelPluginProcessor/Editor; modules/<Product> names it; modules/FX
app/native                  MixController (no JUCE), DawEngine, Transport, Recorder, TimelinePlayer, AudioHost,
                            MonitorDevice, SessionStore (versioned), InputMapStore, ThemeStore, MixBounce
app/ui                      MainView + the workspaces (TracksPage, MixerPage, MixPage = TUNE, LivePage,
                            AdvancedPage = Inspector), sheets, AppTheme (Dine:: tokens)
app/Tools, app/Tests        dlive_ui_snapshots / dlive_mix_stems / dlive_device_check; dlive_app_tests
tests/                      engine + integration tests, benchmark, reference renders
```

## Where the detail lives

| Topic | File |
| --- | --- |
| Build, test, snapshot, stems and AU commands; the real recordings | `docs/BUILD-AND-VERIFY.md`, `BUILD-RUN-SHARE.md` |
| The engine's rules: products, parameters, Tune, profiles, wording, FX | `docs/DINE-CORE-RULES.md`, `docs/ARCHITECTURE-DINE-CORE.md` |
| DLIVE the application: DAW layer, every workspace, setup pages | `docs/DLIVE-APP.md`, `docs/ARCHITECTURE-DLIVE.md`, `docs/MILESTONE-7.md` |
| The mix engineer: TUNE LIVE MIX, REFERENCE MIX, LIVE SAFE, MIX BUDDY, loudness, profiles, linked faders | `docs/DLIVE-MIX-ENGINEER.md`, `docs/ARCHITECTURE-DLIVE-AI.md` |
| Monitoring: the solo bus, two devices, Dante | `docs/DLIVE-MONITORING.md` |
| The desktop design, macro pads, themes, tutorial, frame budget | `docs/DLIVE-DESIGN.md`, `docs/THEMES.md` |
| RealtimeSanitizer: wiring, findings, suppressions | `docs/REALTIME-SANITIZER.md` |
| Milestone reports and the QA notes | `docs/MILESTONE-1..7.md`, `docs/QA-*.md` |

Read the PRD sections 6-8, 42, 48 and `docs/ARCHITECTURE-DINE-CORE.md` before touching the audio path or
adding a product. Work from `main`; commit each piece of work separately with a descriptive message.
