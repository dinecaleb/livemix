# Milestone 7 — DLIVE becomes a DAW

Date: 2026-09-08. This is the report the brief's §78 asks for (audit, gaps, refactor plan) followed by
what the first DAW milestone (§73) actually delivered.

> DLIVE — the live recording and broadcast DAW.
> Connect. Record. Mix. Tune. Broadcast.

---

## 1. Current state before this milestone

The 2026-09-07 pivot (`docs/ARCHITECTURE-DLIVE.md`) had already turned the plugin engine into a standalone
mixer. What existed and worked:

| Area | Where | State |
|---|---|---|
| Channel DSP, FX DSP, analysis, Tune, profiles, safety | `src/` | 136 engine tests green, allocation-tracked, no JUCE |
| Mix graph (strips -> buses -> FX returns -> master) | `src/Mix/MixEngine`, `RoutingGraph`, `MixParameters` | 64 strips inside 12 % of the callback budget |
| Multi-input listen, planner, macros | `src/Mix/MixCapture`, `MixPlanner`, `MixMacros` | one TUNE MIX lands at -22.6 LUFS on the church multitrack; idempotent |
| Standalone application | `app/` | Device -> Assign -> Purpose -> Mix -> Advanced, JSON session, CoreAudio host |
| Offline verification | `app/Tools/MixStems.cpp`, `dlive_ui_snapshots`, `dlive_device_check` | real stems, headless PNGs, real device |

## 2. Missing DAW foundations (the audit)

| Foundation | Before | Now |
|---|---|---|
| Project | a JSON *setup* (assignments + mix), no folder, no audio | a session **folder** with `Audio Files/` inside, versioned document (v2; v1 still opens) |
| Track | an `InputAssignment` only | `TrackState`: arm, monitoring, height, clips — parallel to the session's inputs |
| AudioClip | none | `AudioClip` (file, start, offset, length, source rate) |
| Transport | none | `Transport`: play / stop / record / locate / loop, sample-exact |
| Timeline | none | the TRACKS workspace: ruler, lanes, waveforms, playhead, zoom |
| Recorder | none | `Recorder`: raw WAV per armed track, lock-free FIFO + writer thread |
| Playback | a stems folder streamed as fake device inputs | `TimelinePlayer`: ring-buffered clip playback into the same graph |
| Mixer | present | plus record arm and input monitoring per strip |
| Monitoring | implicit, always live | `MonitorMode` Off / Input / Auto, one rule in one place |
| Waveform rendering | none | `juce::AudioThumbnail` + cache, drawn per clip |
| Buses / routing | present | unchanged |
| Persistence | setup only | the whole timeline, markers, loop, LIVE SAFE |

## 3. Architectural gaps that were in the way

1. **The mix engine was fed straight from the device.** `AudioHost` handed `inputChannelData` to
   `MixController::process`. Nothing could sit between the converter and the mix, so there was nowhere for a
   recorder or a tape machine to live.
2. **"Playing a recording" was a separate world.** `MultitrackSource` streamed a stems folder as if it were a
   console. It could not be recorded to, edited, exported from, or saved — a second, dead-end audio path.
3. **The session was one file.** Recordings need a folder beside the document.
4. **Nothing owned time.** No playhead meant no arm, no take, no clip, no export range.

## 4. The refactor (the smallest safe path)

One new object sits between the device and the mix, and nothing else moved:

```
CoreAudio device ─ AudioHost ─ DawEngine ─ MixController ─ stereo out
                                  │
                                  ├─ Recorder      (raw device inputs -> WAV per armed track)
                                  ├─ TimelinePlayer (clips -> per-track buffers)
                                  └─ Transport     (the playhead)
```

`DawEngine::processBlock` does four things, in order: capture the raw inputs, read the timeline, build the
**input matrix** (every device channel, with playback swapped in for the tracks that are not on their live
input), and hand that matrix to the mix exactly as the device used to be handed to it. The mix engine, the
planner, the profiles and the DSP were not touched: the same `MixEngine::process` runs, so TUNE MIX works
against live inputs and recorded material alike, and `dlive_mix_stems` still lands at -22.6 LUFS with
`NO CHANGE REQUIRED` on a re-tune.

Removed on the way, because the timeline replaced it: `MultitrackSource` (a folder of stems is now *imported*
as tracks and clips), `AudioHost::openPlayback`, and the stems-folder-only export path. `ClipSource` is the one
place clips become audio; `TimelinePlayer` drives it from its reader thread, the offline bounce drives it directly.

## 5. What Milestone 7 delivers (§73)

All twenty-four items of the first DAW milestone:

| # | Item | Where |
|---|---|---|
| 1 | project creation | File > New Session, Save As (a folder under `~/Music/DLIVE`) |
| 2 | audio device selection | `DevicePage`, plus output-only for playing a recorded session back |
| 3 | 8-16+ tracks | 64 strips; the church multitrack imports as 14 |
| 4 | mono / stereo inputs | `InputAssignment` pairs, stereo clips and waveforms |
| 5 | record arm | `TrackState::armed`, R key on Tracks and Mixer, Track menu |
| 6 | input monitoring | `MonitorMode` Off / Input / Auto (`monitorUsesLiveInput`) |
| 7-11 | mute, solo, faders, pan, channel meters | the existing mixer, now in every workspace |
| 12 | transport | `Transport` + the footer bar; Space, R, Return, L |
| 13 | multitrack recording | `Recorder`, one raw WAV per armed track per take |
| 14 | WAV file creation | `<session>/Audio Files/<Track>_001.wav`, 24-bit |
| 15 | timeline | TRACKS: ruler, lanes, zoom, scroll, playhead |
| 16 | audio clips | `AudioClip`, created by a take or an import |
| 17 | waveform rendering | `juce::AudioThumbnail`, cached, drawn per clip |
| 18 | playback | `TimelinePlayer` -> the mix graph -> the output |
| 19 | basic clip editing | select, move, trim both ends, split at the playhead, delete, undo |
| 20 | Mixer workspace | `MixerPage`, now with arm and monitoring |
| 21 | Channel Inspector | `AdvancedPage`, reachable from any strip |
| 22 | session save / load | `SessionStore` v2: assignments, timeline, macros, kept mix |
| 23 | master output | unchanged: the master chain and its limiter |
| 24 | stereo master export | `MixBounce::renderProject`, streamed to disk, WAV or MP3 |

Plus the shell the brief describes (§6, §7, §35, §36): four workspaces — **TRACKS | MIXER | TUNE | LIVE** —
over one session, a macOS menu bar (File / Edit / Track / Mix / Record / View / Help), and LIVE SAFE, which
locks TUNE MIX, imports, new sessions and timeline edits during a service while leaving mutes, faders and the
transport alone.

## 6. Real-time rules, kept

- The audio callback allocates nothing, locks nothing and touches no file. Recording pushes into a
  lock-free FIFO (`juce::AudioFormatWriter::ThreadedWriter`); playback copies out of a preallocated ring that a
  reader thread keeps full; routing arrives through a `TripleBuffer`.
- The loop wraps to the sample in `Transport::advance` **and** in the player's fill, so the playhead and what is
  heard cannot drift apart.
- A take that the disk cannot keep up with stops recording and says so; it never fails quietly.
- Raw recordings are the device inputs, before any processing (§41). The processed mix is never the only copy.

## 7. Verified

| Check | Result |
|---|---|
| `build/tests/livemix_tests` | 136 cases, 0 failed |
| `build/app/dlive_app_tests` | 19 cases, 0 failed (transport, monitoring, recorder, clip source, player, DAW, documents, import, bounce) |
| `build/app/dlive_mix_stems "<church stems>" 30` | after -22.6 LUFS, re-tune `NO CHANGE REQUIRED`, exit 0 |
| `build/app/dlive_device_check 8 "<church stems>"` | imported as 14 tracks, played through CoreAudio: 6021 blocks, 152 us peak, **0 dropouts, 0 playback underruns**, 12 of 14 strips carrying audio |
| `build/app/dlive_ui_snapshots` | 17 PNGs: every workspace and state, with real waveforms on the timeline |
| plugin suites, `auval` | unchanged |

## 8. Not in this milestone

Fades and crossfades, comping and take folders, moving a clip between tracks, markers in the ruler
(they are stored, not yet drawn), count-in and metronome, processed-stem and stereo-master *live* recording
(only the raw inputs are captured today), House Sound, drum reinforcement, third-party plugin hosting,
and the React bridge spike. §57's undo covers timeline edits only.
