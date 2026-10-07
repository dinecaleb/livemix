# DINE v4 — proposals for the owner's sign-off (C6)

Described, not built. Each is a v4 control that needs more than UI, so each waits for a yes.

## 1. The Sample stage's hit visualiser

**What v4 draws.** On a kick, snare or tom's Sample stage: a scrolling waveform of the detector band
(about 3 s), the threshold as a band across it, a marker on every hit - fired (a sample played), held
(masked: inside the mask window after a fire) and ghost (crossed a lower line but not the threshold) -
a velocity pad of the last 16 hits, and the counters "hits · held · under the line".

**What exists.** `SampleTrigger` counts hits and vetoes (`getHitCount`, `getVetoCount`,
`getLastHitLevelDb`, `isPlaying`). Per-hit times are audio-thread only; nothing counts sub-threshold
crossings; there is no detector-band waveform feed.

**What it takes.**
- **Engine (audio thread, allocation-free):** a fixed-size single-producer / single-consumer ring in
  `SampleTrigger` (256 events of `{ sampleTime, levelDb, kind }`, `kind` = fired / held / ghost), pushed
  where the trigger decides; a ghost is a local peak above `threshold - ghostRangeDb` that did not fire
  (`ghostRangeDb` in `ProfileData.cpp`, 12 dB). Beside it a second ring of min/max pairs of the detector
  band, one pair per 256 samples (≈ 190 pairs a second at 48 kHz). Both are written with relaxed atomics
  only; `LIVEMIX_NONBLOCKING` on the push; covered by `tests/AllocationTracker` and RTSan.
- **Model (message thread):** the frame clock drains both rings for the strip the Inspector shows into a
  column buffer sized to the visualiser's width (never to the length of the service).
- **UI:** a cached image of the waveform columns, blit-scrolled by the elapsed columns each frame with
  only the new ones drawn; markers and the threshold band painted over it; the velocity pad as 16
  pre-sized bars. No timer of its own; hidden, it drains nothing.
- **Tests:** a known click train through `SampleTrigger` gives the expected fired / held / ghost sequence;
  RTSan clean; the reference renders unchanged (the rings observe, they do not change the sound).
- **Cost:** about two days; the only audio-thread change is the two pushes.

## 2. The edited-fader dot

**What v4 draws.** A small ring beside a strip's level on the Mixer when a person has moved the fader
since TUNE set it.

**What exists.** The kept mix, and the Mix history's checkpoints, each knowing whether it came from a tune
(`MixCheckpoint::fromTune`). The Inspector compares against the plan only while a plan is open.

**What it takes.**
- **Model, read-only:** `MixController::faderEditedSinceTune (strip)` - the kept fader against the newest
  checkpoint with `fromTune`, more than 0.05 dB apart. The reference is cached and found again only when the
  checkpoint list changes, so the per-frame cost is one float compare per strip.
- **What counts as an edit:** a person's move. A scene or cue recall, Autopilot (group faders only, so never
  a strip), a linked partner moving with it, and LIVE SAFE's step limit are not a person's move; a recall
  takes the reference forward. That is the one decision to sign off: *after a cue or scene recall, should
  the dot clear?* The proposal is yes - the recalled mix is the one somebody chose.
- **UI:** the ring on the strip (the Mixer, the console window), on the Tracks header, and "edited" in the
  quick inspector's provenance line, which already counts hand edits.
- **Cost:** half a day; no engine or session-format change.

## 3. The traffic lights move with the sidebar

**What v4 draws.** When the sidebar hides, the three window buttons slide into the toolbar's left edge
with it; when it shows, they slide back into the sidebar card. With Reduce Motion, they jump.

**What exists.** `WindowChrome.mm` places the buttons once, at a fixed `kFirstButtonX`; the sidebar
animation (`MainView::stepSidebar`) moves only the views. There is no Reduce Motion accessor.

**What it takes.**
- **Host (app/native, Objective-C++):** `setWindowButtonsOffset (window, x)` moving the three
  `NSWindowButton`s' frames horizontally (no new window style), and `prefersReducedMotion()` over
  `NSWorkspace.accessibilityDisplayShouldReduceMotion`.
- **UI:** `stepSidebar` calls the offset each VBlank step; Reduce Motion makes the sidebar jump as well
  (today it always animates).
- **Risk:** macOS re-lays the buttons on resize and full-screen changes; the offset is re-applied in
  `MainWindow::resized` and on `windowDidExitFullScreen`. Checked by eye on the real app only - the snapshot
  tool has no title bar.
- **Cost:** half a day.
