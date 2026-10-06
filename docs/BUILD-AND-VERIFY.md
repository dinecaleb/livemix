# Build, run and verify

Every build, test, snapshot and stems command, and where the real recordings are. Moved verbatim from the old CLAUDE.md (2026-09-19); `BUILD-RUN-SHARE.md` is the tester-facing version.

- Build: `export PATH="$HOME/.local/bin:$PATH"` (cmake/ninja from `uv tool`), then `scripts/build.sh`.
  The app on its own (the fast loop, no plug-ins): `scripts/dine.sh` builds DINE and opens it;
  `--build` builds only, `--tests` runs the app + engine tests, `--shots [dir]` renders the UI snapshots,
  `--debug` uses `build-debug`.
  A build to hand someone else: `scripts/package.sh` (`--universal` for Intel too, `--no-build`, `--out <dir>`) writes
  `dist/DINE-<version>-<date>.zip` + `dist/NOTES.txt` — send both; the signature is ad-hoc, so the tester opens it
  once by right-click > Open. Every build/run/share command, and what to tell the tester: `BUILD-RUN-SHARE.md`.
  **Signing and the microphone:** `dine.sh` signs `DINE.app` with the first "Apple Development" identity in
  the keychain (`-DDINE_SIGN_IDENTITY`, `DINE_SIGN_IDENTITY=-` for ad-hoc), so macOS keeps its microphone
  answer from one build to the next; `package.sh` takes `SIGN_ID` (stable) or `DEVELOPER_ID` (+ notarization).
  An ad-hoc build is a new app to macOS every time and it asks again (`docs/QA-2026-10-05.md` §1).
  Engine-only iteration (fast, no JUCE): `cmake -S . -B build-engine -G Ninja -DLIVEMIX_BUILD_PLUGIN=OFF && cmake --build build-engine && build-engine/tests/livemix_tests`.
- Sample replacement on real drums: `build/app/dine_trigger_check "<folder of takes>" [seconds] [offset] [name filter]`
  (`app/Tools/TriggerCheck.cpp`) fits the stage as TUNE does and runs the detector over every kick, snare and tom take,
  printing hits, a level histogram, gaps and kick / snare coincidences; the QUEENSVIEW takes are the reference set.
- **How close TUNE MIX is to an engineer's own mix:** `scripts/mix_scoreboard.py` runs `build/app/dine_mix_compare`
  over every reference session in `scripts/mix-scoreboard.txt` (sessions an engineer finished by hand) and compares
  each window's SCORE line - channels, groups, lead against the rest, tone; all distances in dB, smaller is closer -
  with `scripts/mix-scoreboard-baseline.txt`. It fails when any figure is more than 0.5 dB further from the hand mix
  than the baseline. Run it after any change to profile numbers, the planner or a strategy; `--update` (and commit)
  only when the move was meant. The audio is on this Mac, so it is a local check, not CI; a missing reference is
  skipped and said. More references are the way to make TUNE better rather than fitted to one recording.
- Real stems for listening/offline checks: `/Users/calebwork/Downloads/stems recording` (church multitracks). Run
  `build/modules/Drums/livemix_tune_stems "<Source>" <file.aif> [seconds] [gospel|worship]` to see measurements + decisions.
- Verify UI changes with `cmake --build build --target livemix_ui_snapshots && build/modules/Drums/livemix_ui_snapshots <dir>`
  (Drums) and `build/modules/<Vocals|Keys|Master|Guitar|Bass>/livemix_<vocals|keys|master|guitar|bass>_ui_snapshots <dir>` and look at the PNGs;
  regression references are regenerated only when baselines change on purpose. Plugin tests per product:
  `build/modules/<P>/livemix_<p>_plugin_tests` (+ `livemix_plugin_tests` for Drums kit/AI-parsing specifics).
  AU ids: Drums `Lmdr`, Vocals `Lmvo`, Keys `Lmky`, Master `Lmma`, Guitar `Lmgt`, Bass `Lmba`, FX `Lmfx` (manufacturer `Lvmx`); `auval -v aufx <code> Lvmx`.
