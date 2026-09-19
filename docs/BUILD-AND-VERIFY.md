# Build, run and verify

Every build, test, snapshot and stems command, and where the real recordings are. Moved verbatim from the old CLAUDE.md (2026-09-19); `BUILD-RUN-SHARE.md` is the tester-facing version.

- Build: `export PATH="$HOME/.local/bin:$PATH"` (cmake/ninja from `uv tool`), then `scripts/build.sh`.
  The app on its own (the fast loop, no plug-ins): `scripts/dlive.sh` builds DLIVE and opens it;
  `--build` builds only, `--tests` runs the app + engine tests, `--shots [dir]` renders the UI snapshots,
  `--debug` uses `build-debug`.
  A build to hand someone else: `scripts/package.sh` (`--universal` for Intel too, `--no-build`, `--out <dir>`) writes
  `dist/DLIVE-<version>-<date>.zip` + `dist/NOTES.txt` — send both; the signature is ad-hoc, so the tester opens it
  once by right-click > Open. Every build/run/share command, and what to tell the tester: `BUILD-RUN-SHARE.md`.
  Engine-only iteration (fast, no JUCE): `cmake -S . -B build-engine -G Ninja -DLIVEMIX_BUILD_PLUGIN=OFF && cmake --build build-engine && build-engine/tests/livemix_tests`.
- Real stems for listening/offline checks: `/Users/calebwork/Downloads/stems recording` (church multitracks). Run
  `build/modules/Drums/livemix_tune_stems "<Source>" <file.aif> [seconds] [gospel|worship]` to see measurements + decisions.
- Verify UI changes with `cmake --build build --target livemix_ui_snapshots && build/modules/Drums/livemix_ui_snapshots <dir>`
  (Drums) and `build/modules/<Vocals|Keys|Master|Guitar|Bass>/livemix_<vocals|keys|master|guitar|bass>_ui_snapshots <dir>` and look at the PNGs;
  regression references are regenerated only when baselines change on purpose. Plugin tests per product:
  `build/modules/<P>/livemix_<p>_plugin_tests` (+ `livemix_plugin_tests` for Drums kit/AI-parsing specifics).
  AU ids: Drums `Lmdr`, Vocals `Lmvo`, Keys `Lmky`, Master `Lmma`, Guitar `Lmgt`, Bass `Lmba`, FX `Lmfx` (manufacturer `Lvmx`); `auval -v aufx <code> Lvmx`.
