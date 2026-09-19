# DLIVE: the monitor (solo) bus and the second device

How solo never changes what the room hears, and how the broadcast and the engineer's headphones share one CoreAudio device. Moved verbatim from the old CLAUDE.md (2026-09-19).

- **THE MONITOR (SOLO) BUS** (2026-09-16, `src/Mix/MonitorBus.h`). Pressing S never changes what the room and the
  stream hear. Solo feeds a stereo accumulator that sits beside the master and never into it (`MixEngine::Monitor`),
  and it leaves by an output feed with `monitor = true` (`OutputFeeds.h`) - headphones, a pair of nearfields, an
  Aggregate Device. `MonitorState` (in `MixParameters`, so solo flags and the monitor are applied in one breath)
  carries the mode, the tap point, the level, dim, mute and what the monitor follows when nothing is soloed.
  `SoloMode::Monitor` is the default and the whole point; `SoloMode::InPlace` is the old destructive behaviour, kept
  because it is right for mixing a recording, never the default, and said out loud on the LIVE page when it is on.
  `SoloPoint::PFL` taps before the fader (a muted channel is still audible - that is what a pre-fade listen is for),
  `AFL` after it. Strips, group buses and FX returns can all be soloed (`FxSlotParameters::solo`). The whole monitor
  path is skipped when no feed carries it, so a session that never uses it costs nothing; solo with nowhere to go is
  said once rather than silently doing nothing (`MixController::hasMonitorOutput`). Verify with the `MixEngine: solo
  ...` / `MixEngine: PFL ...` tests and `Monitor: solo is monitoring ...` in `dlive_app_tests`.
  **From the user's side the whole feature is two pickers** on the Outputs sheet - "Broadcast"
  and "Solo" - and everything under them is DLIVE's problem, because macOS opens exactly one
  audio device at a time. Two different devices makes `app/native/MonitorDevice` build the
  combined CoreAudio device itself: **unstacked** (a *stacked* aggregate is a Multi-Output
  Device, which mirrors one bus to every device in it - which is precisely why a private solo
  was impossible before), with the broadcast as clock master and **drift correction on the
  solo device**, because Dante and a USB interface do not share a clock. The same device with
  four or more outputs needs no aggregate at all: solo takes its outputs 3-4. Choosing
  "nowhere" removes what DLIVE made and puts the Mac back. The words "Aggregate Device" never
  reach the user; a device the *user* built is never touched (only ours carries our UID). The
  picking rules live apart from CoreAudio in `MonitorDevicePick.cpp` so they are tested
  without a device - the one that matters is "never suggest the laptop speaker when a real
  interface is plugged in".
  **The broadcast and the engineer's listen are always a real stereo pair**, enforced by
  `normaliseOutputs` at the single chokepoint every routing passes through
  (`MixController::setOutputFeeds`), never at the call sites: a mix that reaches the stream
  summed to mono, or on one leg because a pair was half-chosen, is the kind of fault nobody
  notices until it is on the recording. The optional extra feeds keep their mono switch,
  because that is what it is for (one fill speaker, a feed to a phone).
  **The words are the volunteer's, not the engineer's**: WHAT I HEAR, "Only I hear it" /
  "Everyone hears it", "In the mix" / "On its own", "My headphones". The engineer's terms
  (monitor bus, solo in place, AFL, PFL, aggregate device) survive once each, in tooltips -
  the same plain-language-on-the-surface rule the rest of the app follows.
- **SOLO ON A SECOND DEVICE, WITH DANTE (2026-09-18, the QUEENSVIEW session).** Broadcast on Dante Virtual Soundcard
  and solo on a Scarlett went silent. Two causes, both in the machinery under the two pickers: the built device put
  the Scarlett's pair *after* sixty-four Dante outputs (channel 64-65) and `AudioHost` opened the first `kMaxOutputs`
  (16) channels, so the solo pair was never open; and the console was opened twice - as the input device, and again
  inside the built device for its outputs - glued together by JUCE's own `AudioIODeviceCombiner`. Now
  `MonitorDevice::layoutFor (broadcast, headphones, input)` (JUCE-core, `MonitorDevicePick.cpp`, tested in
  `DawTests`) decides the pieces of the built device in channel order - **the console's input device first** so
  channel 1 stays channel 1, then the broadcast, then the headphones, each once - and `combine` builds exactly that
  and reports `carriesInput`; the host then opens the built device **once, for both directions**, with
  `MonitorDevice::outputChannelsToOpen` choosing the pairs (the solo pair always, as much of the broadcast device as
  fits beside it) passed to `AudioHost::open (..., outputChannels)`. The engine and the feeds address the *open*
  channels packed in device order (`slotForOutputChannel`, and `getOutputChannelNames()` lists them the same way),
  so a pair at 64-65 is slot 14-15 and never falls off the engine. `HostServices::consoleInput()` is the console's
  device while the built one carries it; nothing outside `Main.cpp` ever sees "DLIVE Monitoring" as an input. The
  session stores `inputDevice` / `outputDevice` as the devices the user chose plus `soloDevice`, and
  `restoreSolo` rebuilds the pairing on opening, so a Mac that lost the built device comes back right. A failed
  join waits for the console device to be republished before reopening it and says if that failed too, instead
  of leaving the desk silent.
