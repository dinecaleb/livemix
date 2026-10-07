# DINE v4 — performance

The brief (`HANDOFF.md`, "Performance") treats any regression as a bug. This file records what is measured,
how, and what has not been measured yet and why.

## What is measured, and how

`dine_ui_snapshots --frames <channels>` drives the real window headlessly, on the native renderer, and
reports per workspace: the cost of one `refresh()` (the 30 Hz tick), a whole-window repaint arriving on the
page with every cache dirty ("cold"), the same again ("warm"), and the number of text layouts. Every time is
the fastest of seven rounds, so the fan cannot flatter it. `--paint <channels>` adds the tree of where the
warm paint goes, component by component.

The two binaries were run one after the other on the same Mac (Apple Silicon, 1520 x 960 window, 128
channels), nothing else building. Before = `8b9f4cf`, the last commit before v4.

| Workspace | tick before | tick now | warm repaint before | warm repaint now | cold repaint before | cold repaint now |
| --- | --- | --- | --- | --- | --- | --- |
| Tracks | 1.29 ms | 1.26 ms | 7.17 ms | **3.69 ms** | 7.18 ms | **3.62 ms** |
| Mixer | 1.24 | 1.24 | 2.45 | **2.44** | 9.23 | **5.59** |
| Tune | 1.25 | 1.27 | 9.65 | **3.08** | 9.74 | **9.16** |
| Live | 1.26 | 1.25 | 5.27 | **3.03** | 5.20 | **3.05** |
| Inspector | 1.25 | 1.27 | 8.09 | **3.45** | 8.11 | **3.49** |

The first v4 pass was slower than v3 on every page (Tune 23.8 ms warm). `--paint` found why, and the fixes are
in the commit that adds this file:

- **Measuring text was not cached.** `Dine::textWidth` shaped its string on every call, and the v4 chrome
  measures the same strings every paint - the status line's sentences, the toolbar's pills, a chip row laying
  itself out. It now has the two-generation cache the layout cache uses (bounded at 2 x 6,000 entries,
  cleared with it). The status line went from 1.8 ms to a fraction of that on every page.
- **The Inspector's chip row was quadratic.** Laying chip *i* out walked every chip before it; the rows are
  laid out once and kept until the width or the stages change (5.4 ms to well under 1).
- **TUNE's rail rows and side panel are buffered** to images: they change a few times a minute, and a
  repaint of the page is now a blit of them.

## Watching it live

**Cmd-Option-P** shows an overlay in the corner of the workspace: the display's frame interval (median and
p99), MainView's tick and the share of the message thread it uses, and the process's resident memory. It is
in Debug builds, and in Release only with `DINE_PERF_HUD=1` in the environment, so nobody finds it by accident
at a service. Hidden, its clock is detached and it costs nothing.

## Not measured yet

These need the real application on the booth Mac with audio running; DINE is not launched by the tools here
(`dine_ui_snapshots` never opens a device). Each is a number for this file once taken:

| Target (brief) | How to take it |
| --- | --- |
| UI CPU idle ≈ 0 % | Stopped transport, no signal; Activity Monitor or `top -pid`, with the overlay's msg-thread figure beside it. |
| UI CPU with 64 channels metering | Play a 64-input session (`dine_mix_stems`' QUEENSVIEW folder doubled, or a desk), same tools. |
| p99 frame time scrolling the Mixer at 128 ch | The overlay's p99 while dragging the console's scrollbar end to end. |
| < 150 MB UI RSS after every page | The overlay's RSS after visiting each workspace, minus the RSS at the Sessions page with nothing open. |
| No growth over 3 h; 0 dropouts in a 30-min soak | Loop a take, cycle the pages; the status line's "dropped" count and RSS at the start and the end. |
| Instruments hot spots | Time Profiler and Allocations over the soak; anything on the audio thread from the UI is a bug. |

## The clock and the meters (C5, 2026-10-06)

Measured with `dine_ui_snapshots --frames 128` immediately before and after, same Mac, nothing else
building (ms; fastest of 7 rounds):

| Workspace | tick before | tick after | warm before | warm after | cold before | cold after |
| --- | --- | --- | --- | --- | --- | --- |
| Tracks | 1.45 | **1.24** | 3.70 | 3.99 | 3.98 | 3.97 |
| Mixer | 1.44 | **1.25** | 2.58 | 2.55 | 7.03 | 6.64 |
| Tune | 1.43 | **1.24** | 3.17 | 3.37 | 9.28 | 9.66 |
| Live | 1.45 | **1.24** | 3.11 | 3.09 | 3.31 | 3.17 |
| Inspector | 1.43 | **1.24** | 3.74 | 3.83 | 3.75 | 3.82 |

(The "before" ticks are higher than the first table's because LIVE, Inputs and the Ready sheet grew since;
the repaints move within the run-to-run spread.)

- **One clock for what is drawn.** MainView's `frameClock` is a `juce::VBlankAttachment` throttled to 30 Hz;
  it drives the pages, the sheets, the status foot, the sidebar and the detached Mixer / LIVE / Inspector
  windows (their own 30 Hz timers are gone; each refreshes only while it is on a screen). It stops when the
  window is minimised or off every screen.
- **A clock that never stops.** The 30 Hz `juce::Timer` keeps what must run whether or not anything is drawn:
  `controller.poll()` (Autopilot, the listen, the meter snapshot), the autosave cadence, the microphone
  follow-up, telemetry's notice, the export's state, toast expiry and the device-stopped warning. When the
  VBlank has been silent for 100 ms (no display - the tests and the snapshot tool - or only a detached
  window on screen) it draws too.
- **Meters read once.** `MixController::takeMeterSnapshot()` (from `poll()`) consumes every strip, bus and
  return meter once per tick; the Mixer, TUNE, LIVE, the Inspector, Tracks and the detached windows read
  `stripPeakDb` / `busPeakDb` / `fxPeakDb`. Two readers no longer take each other's peaks (tested). The macro
  pads' 60 Hz settle timers remain, running only while a pad animates.
- **Virtualised strips and rows: measured, not built.** At 128 channels the Mixer's warm repaint is 2.5 ms
  and a tick 1.25 ms of a 33 ms frame; the brief's rule is to measure first. Build it if the real-app scroll
  p99 (the overlay) or RSS says otherwise.

## Known structural work

- Virtualised Mixer strips and Inputs rows, if the real-app numbers under "Not measured yet" ask for it.
- The macro pads' own settle timers could ride the frame clock.
