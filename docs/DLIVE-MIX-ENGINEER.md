# DLIVE: the mix engineer

TUNE LIVE MIX, REFERENCE MIX, what a microphone hears between the sounds, LIVE SAFE, repeatability, MIX BUDDY, delivery loudness, RAISE LOUDNESS and MASTER SOUND, the AMBIENCE bus, input mappings, the six sound profiles and linked faders. Moved verbatim from the old CLAUDE.md (2026-09-19); the architecture is `docs/ARCHITECTURE-DLIVE-AI.md` and `docs/ARCHITECTURE-DLIVE.md`.

- **TUNE LIVE MIX** (the AI Mix Engineer, 2026-09-12; see `docs/ARCHITECTURE-DLIVE-AI.md`) is a reasoning layer
  **above** `MixPlanner`, never instead of it. The deterministic plan is built first and always, so a dead network,
  a timeout or a malformed reply leaves the user with a professional mix and a sentence. Everything is JUCE-free in
  `src/MixAI` (+ `src/Core/Json`): `RelationshipEngine` (measures, never decides - masking, hierarchy, low-end
  ownership, kit balance, what reaches the master), `MixContext` (schema v1: the session as a versioned serialisable
  document, with `MixCaptureAdequacy` refusing a listen that is not worth mixing from), `MixIntent` (schema v1:
  sonic outcomes with a signed strength, never parameters), `DspCapabilityRegistry` (generated from the real
  parameter tables and from how `MixEngine` configures each stage - the limiter is master-only because that is
  where the stage is turned on; an unavailable processor is reported **with its reason**, never silently missing),
  `CapabilityResolver` (intent -> `ProcessingPlan`, deterministic, as deltas on the deterministic plan; every action
  carries EXACT / APPROXIMATED / SUBSTITUTED / UNSUPPORTED - a spring reverb is built from the plate and says what
  it will not have, a gated reverb is refused), `MixSafetyValidator` (refuses rather than reinterprets: capture gain
  is the console's, a gate never goes on a sustained source, the master keeps its headroom; a refused EQ gain takes
  its band with it and every refusal keeps its reason for REVIEW CHANGES) and `TuneLiveCoordinator` (the whole
  state machine, one worker, polled). Numbers live in `MixProfile::aiRanges` / `aiBounds` (`MixProfileData.cpp`),
  versioned. `MixReasoningProvider` is the seam: `LocalMixReasoningProvider` is the default - deterministic,
  offline, reasons from the measured relationships, and is what every test runs against; `OpenAiMixProvider`
  (`app/native`) is opt-in, strict JSON schema, and sends the MixContext and the capability list and **never audio**.
  The result is an ordinary `MixPlan`, so BEFORE / AFTER, KEEP, REVERT, the mixer, the chain strips and the
  Inspector work on it unchanged - one source of truth. `MixController::startTuneLiveMix` drives it; the verify
  listen keeps the applied mix audible (`liveVerifying`); LIVE SAFE blocks it like any re-tune. The session stores
  the run's record under `tuneLive` next to the kept mix, so **reopening a session never contacts a provider**.
  Verify with `build-engine/tests/livemix_tests`, `build/app/dlive_app_tests` and the `08b` / `09b` UI snapshots.
- **REFERENCE MIX** ("make it sound like this") aims the mix at a finished recording. The engine half is
  JUCE-free in `src/Mix/ReferenceMix.{h,cpp}`: `ReferenceProfile` (schema v1 - the tonal balance, crest,
  loudness, correlation and tempo of a record, stored with the session so reopening never re-reads the file),
  `Reference::adequacy` (a four-second clip, a silence or an unreadable file is refused **with its reason**)
  and `Reference::targets`, which is the whole decision: it returns the master's `SourceTargets` aimed at the
  reference instead of at the profile's own. Nothing there writes a parameter - `MasterStrategy` does the work
  it always did - so every bound, every sentence and the idempotency rule come along unchanged. The seam is
  `TuneContext::targetsOverride` (null = the profile), set only by `MixPlanner` and only for the master:
  a reference is a finished stereo record, so the master is the only thing in a plan it can honestly be
  compared with. Band energy is measured relative to the whole, which is why a -9 LUFS master and a -23 LUFS
  live mix are directly comparable and why the plan's fader moves do not disturb the comparison.
  What a reference is **not** allowed to do is the other half of the feature and is reported, never silent:
  it never sets the delivery loudness (that belongs to the broadcast), never moves a source, a fader or a
  group (`MixPlanner` proves this in `ReferenceMixTests`), and never pulls one band target further than
  `MixProfile::referenceBounds` allows (3 dB), nor the master image or glue past their own bounds. Each
  refusal keeps its sentence in `MixPlan::reference` (`ReferenceMatch`), which is what the sheet and the plan
  notes read. The app half: `app/native/ReferenceAudio` decodes and measures the file (up to 4 minutes, on a
  thread of its own), `MixController::setReference` / `clearReference` / `startReferenceMatch` own it, and the
  listen is kept (`getLastListen`) so MATCH TO REFERENCE re-plans from what the band already played instead of
  asking them to play again - the result is an ordinary `MixPlan`, so BEFORE / AFTER, KEEP and REVERT are
  unchanged. The UI is `app/ui/ReferenceSheet` (the Reference button on TUNE, File > Add a Reference Mix...,
  Mix > MATCH TO REFERENCE), which draws the two balances against each other and prints what matching will aim
  for and what it refuses to copy *before* the button is pressed. `SessionStore` stores the measurement under
  `reference`; a document from a schema this build does not know is ignored rather than half-read. Verify with
  `build-engine/tests/livemix_tests` (`Reference*`), `build/app/dlive_app_tests`, the `07c` / `07d` snapshots
  and `build/app/dlive_mix_stems "<stems>" 30 <outdir> gospel -1 broadcast <reference.wav>` (the REFERENCE MIX
  block, and RE-TUNE still saying NO CHANGE REQUIRED).
- **WHAT A MICROPHONE HEARS BETWEEN THE SOUNDS (2026-09-18, from the QUEENSVIEW recording).** Three balance faults
  found on a real 21-input service, all in `MixPlanner` / the analysis, all with the numbers in `MixProfileData`:
  (1) a vocal microphone nobody was really singing into (active -43 dBFS, floor -48) was lifted 30 dB to reach the
  vocal level and became the loudest cymbals in the mix. `Rules::spillBelowTargetDb` (16): a voice or a close drum
  microphone (`isSpillProneMic`) is lifted (gain + fader, absolute) only until its between-the-sounds floor would
  land that far under its mix level; only ever a limit on a lift; the strip is `spillLimited`, says "mostly hears
  the stage", and is never the lead the rest of the band follows down. Keys, pads, DIs, overheads and room
  microphones are exempt: the floor of a held chord is the chord. (2) `AnalysisResult::musicalPeakDb` was capped
  12 dB over the 95th-percentile *frame* level, which on a sparse close mic (a snare on the backbeat) is the decay
  tails - so every real hit looked like an isolated click and the gain staging drove the snare's chain input to
  +3 dBFS. The detected events (`eventLevelDb`, `kSpikeEventsMin` 8) now set the cap too. (3) the close-mic budget
  `maxCloseMicRaiseDb` counted only a positive gain, so a hot hi-hat pulled down 12 dB could not get its fader back:
  it now counts the net lift (gain + fader) whichever way the gain went. Also the master's loudness move per Tune is
  bounded at 18 dB (was 12): a live sum at -22 LUFS asked for a -14 stream is a 15 dB move, and stopping short left
  RE-TUNE with something to say. Measured on four 40 s windows of the recording: the vocal spill mics went from
  +30/+37 dB to +14/+18, the snare's gain from +13 to 0, and the toms from a 5 dB spread to 1 dB. The recording is
  `~/Music/DLIVE/QUEEENSVIEW WIRED/Audio Files` (take `_002`, 534 s, 21 inputs; symlink the files under role names
  for `dlive_mix_stems`, windows at 30 / 120 / 200 / 300 s).
- **LIVE SAFE is a policy, not a tooltip** (`src/Mix/LiveSafe.h`), enforced in `MixController` rather than in a menu
  handler - a guard in `MainView` only covers the menu, and the AI, the chat, a macro and a keyboard shortcut all
  reach the mix without passing one. It never locks the emergency controls (mute, solo, the monitor, the transport,
  recording, UNDO/REDO); it refuses what changes the mix wholesale or interrupts the audio (TUNE / TUNE CHANNEL /
  TUNE LIVE MIX / MATCH TO REFERENCE, KEEP, REVERT, BYPASS, routing, output routing, the device, timeline edits,
  opening a session); and it *limits* what is still allowed - `maxFaderStepDb` 6, `maxMasterStepDb` 3,
  `maxInputGainStepDb` 6, a pan step - so one slip cannot throw a fader across the console. A refusal always carries
  the sentence saying what the risk was. Moving the *monitor* feed stays legal mid-service (`onlyMonitorChanged`).
  `DawEngine::setLiveSafe` is the one place both halves are set (the timeline's lock on `Project`, the mix's policy
  on `MixController`), so they can never disagree.
- **REPEATABILITY** is a requirement of the reasoning layer, not a setting. The same band, the same listen and the
  same settings must produce the same mix. `MixContext::fingerprint()` (FNV-1a over the canonical document, written
  out so it does not move with the toolchain) identifies a listen; it is the model's `seed` and the key of
  `MixReasoningCache`, so a question already answered is answered the same way without a round trip.
  `OpenAiMixProvider` sends `temperature: 0` and `top_p: 1` for the primary mix (a reasoning model takes only the
  seed), and the brief tells every provider to be repeatable. **TRY ANOTHER MIX** is the only way to a different
  reading: `LiveTuneSettings::variation` 1, 2, 3 ... - asked for by name, and itself repeatable -
  and it works from the listen DLIVE already has (`reuseListen`), so two readings are compared against the same
  performance. Verify with the `Repeatability: ...` tests, which pin a deliberately drifting provider.
- **MIX BUDDY** (the user-facing name since 2026-09-17 - "DLIVE's mix engineer, in plain words"; never "chat" or
  "AI chat", so nobody types a request that is not about the mix, and the panel carries a permanent note saying
  what it is for and what it cannot touch; `app/ui/ChatSheet`, `MixController::sendChatRequest`) is a **panel beside the
  workspace**, never over it (`MainView::kRequestsW`; the pages and the chain foot give up its width, so a
  page's own sheet stays whole next to it). It is not a second mixing engine: a sentence
  goes through the same pipeline as TUNE LIVE MIX - intent, `CapabilityResolver`, `MixSafetyValidator` - and comes
  out as an ordinary `MixPlan`, so BEFORE / AFTER, KEEP, REVERT and the Inspector work on it unchanged and nothing
  typed into a chat can reach a parameter by a path the reasoning layer could not. With no cloud model configured
  the request is read by `MixAI/MixRequestParser` - deterministic, offline, and honest: a sentence it cannot read
  comes back with what to try instead rather than a confident change to something nobody asked about, and a capture
  problem ("the singer is off mic") is named as a capture problem. The transcript and the conversation ride in
  `MixReasoningRequest::conversation`. Beside it is mix-level UNDO / REDO (`markMixChange` / `undoMix` / `redoMix`),
  which LIVE SAFE deliberately never locks.
- **HOW LOUD THE FINISHED MIX SHOULD BE** is a setting now (`MixSession::delivery`, `DeliveryLoudness`). It used to
  be a hidden consequence of the purpose: "Church Broadcast" quietly meant EBU R128, which is -23 LUFS - right for a
  television feed and about 9 dB under what a church stream is expected to be - and nothing said so. The target is
  what the whole gain structure is fitted against (`MixPlanner` sets it on the master's `targetsOverride`, after any
  reference and winning over it, because a reference sets the tone and is never allowed to set the delivery
  loudness), not a gain added at the end: at -14 the stems land at -14.7 LUFS, -3.2 dBTP and the same 14.5 dB crest
  as the -23 mix, and RE-TUNE still says NO CHANGE REQUIRED. `MixController::getMasterLoudness()` is the one place
  the master's LUFS-I / short-term / true peak / limiter reduction / target / headroom are read, so no two pages can
  disagree. Check with `dlive_mix_stems "<stems>" 30 <out> gospel -1 broadcast:-14`.
- **RAISE LOUDNESS and MASTER SOUND** (2026-09-17, the MASTER band on TUNE and the Mix menu). *Raise loudness to
  target* (`MixController::raiseLoudnessToTarget`, previewed by `previewLoudnessMove`) gets the master to the delivery
  target (YouTube / Facebook / Spotify = -14 LUFS) in one move without re-tuning: the move is the gap between the
  master's integrated (or short-term) LUFS and the target, clamped by `MixProfile::loudnessLift()` (+12 / -6 dB, and
  capped so the true peak asks the limiter for no more than `maxLimiterGrDb`), written to the master's output trim
  in the kept mix as an undoable mix change, with the master limiter turned on at the delivery ceiling so it cannot
  clip. It refuses with its reason (nothing played, BYPASS, already there), and LIVE SAFE limits it like the master
  fader. The integrated meter is reset afterwards so the readout measures the new level. *Master sound*
  (`MasterVoicing`: As tuned / Warm / Bright / Voice first / Phone speakers / Earbuds / Car / TV-soundbar,
  `MixSession::voicing`, saved as `voicing`) is a compose-time layer like the macros:
  `MixMacros::applyVoicing` (numbers in `MixProfile::voicing (profile, voicing)`, tone shelves + a presence bell +
  a touch of saturation, gains clamped to +/-6 dB) is applied on top of `MixMacros::apply` in `MixController::compose`,
  so it never touches the kept mix, the plan or idempotency, and "As tuned" is exactly what TUNE MIX built.
  Verify with the `MixController: the master's voicing ...` app test and the `07` / `16` snapshots.
- **AMBIENCE is the sixth group bus** (`MixBus::Ambience`, before MASTER) and `SessionStore` is **version 4**, which
  remaps a version <= 3 document's bus slots (`busFromStoredIndex` + `storedBusCount`: the last stored slot has
  always been the master, wherever it sat). `ChannelRole::CrowdMic` / `AmbienceMic` / `AmbienceBus` and
  `RoleFamily::Ambience` / `AmbienceBus` are their own family with their own strategy
  (`src/Tune/AmbienceStrategies.cpp`): **never gated** - on a room microphone the quiet between the sounds is the
  sound - never transient-shaped, high-passed well above a stage source, compressed slowly as a ceiling rather than
  for punch, and aimed well under the band (`busBelowVocalsDb[Ambience]` = -12). A congregation microphone routed
  through the drum-room rules was gated and pushed forward, which is the bug this fixes.
  `ChannelRole::SaxAlto` / `SaxTenor` / `SaxBari` (`RoleFamily::Saxophone`, MUSIC bus, same file) are a horn and not
  a keyboard: the honk between 0.9 and 2.5 kHz is *notched* rather than shelved, the compressor is fitted for the
  range between a held note and a wailed one with an attack slow enough to keep the reed, the high-pass sits under
  the horn's own lowest note, and it is never expanded. Both are in `StemNames`, `Dine::roleGroups`, the ASSIGN
  kits and `Dine::busTint`.
- **INPUT MAPPINGS** (`app/native/InputMapStore.{h,cpp}`, `~/Music/DLIVE/Input Maps/*.dlivemap.json`, File menu).
  A church patches the same desk the same way every week; a map is the patch and nothing else - device channel,
  name, source, stereo link - deliberately not a mix. Save / rename / duplicate / import / export / apply. The one
  rule that matters: applying a map must never route audio to the wrong place, so an input the open device cannot
  provide comes back **switched off and named**, with a sentence saying what is missing, and the dialog says so
  *before* anything is applied. Two inputs wanting one channel is reported the same way.
- **SOUND PROFILES BEYOND THE CHURCH** (2026-09-18). `StyleProfileId` is six: Modern Gospel (default), Modern
  Worship, **Rock Band**, **R&B and Hip-Hop**, **Jazz and Acoustic**, **Talk and Podcast**. Every one is a delta
  on Modern Gospel in the same shape as Worship - `buildRockBand()` etc. in `ProfileData.cpp` (targets and
  baselines), the mix-level deltas beside each rule in `MixProfileData.cpp` (sends, reverb beats, the balance,
  `relationships`, `aiRanges`, `macroRanges`) and the return characters in `FxProfiles.cpp` - so the bounds,
  the sentences, the strategies and idempotency come along unchanged (the per-profile test loops cover them).
  What each one *is*: rock = the kit level with the voices, guitars carry, everything a little denser and driven,
  the room small; R&B = the sub belongs to the kick and the bass (`bassHpf` 30-45), the voice airy and close, the
  delays part of the song, no artificial drum room; jazz = crest +3, no saturation anywhere, **no gates on the
  kit** (`gateAppropriate = false`, the overheads carry it), the piano and the room forward, no delay on a
  voice; talk = the speaking voice is the reference, every voice held steadier (release never under 80 ms) and
  de-essed harder, the band a bed 8 dB under (`busBelowVocalsDb`), a voice dry. Append to the enum, never
  reorder: the index is in sessions, plug-in presets and input maps. The PURPOSE AND SOUND page wraps the
  cards (`soundGridHeight`); `dlive_mix_stems` takes `gospel|worship|rock|rnb|jazz|talk`. The four purposes are
  unchanged (their names are church-flavoured; the numbers are not).
- **LINKED FADERS** (2026-09-18). `StripParameters::linkGroup` (0 = none) and `MixController::linkStrips /
  unlinkStrip / getStripLink / linkedWith / linkedNames`. A link is **relative, about level and solo**:
  `setStripFader (strip, db, withLink)` moves every other member by the same dB (after the LIVE SAFE step limit,
  so no member moves further than the held one could), a member at the end of its travel stops there, and
  Cmd-drag (`withLink = false`) moves one alone; **S on one member solos them all** (`setStripSolo`), from either
  end. Mute, pan and the chain are deliberately not linked. It is
  part of the kept mix - saved (`linkGroup`, absent = none), carried by `carryMix` with the strip, kept through a
  TUNE (the plan sets each fader absolutely; the link keeps the new offsets) - and linking is one undoable mix
  change that LIVE SAFE lets through. **Linking starts the members level**: every member takes the fader of the
  channel the link was made from (the first strip passed), within the LIVE SAFE step, so a pair of overheads is a
  pair from the moment it is linked; a balance is set afterwards with Cmd-drag. Linking to a member joins its
  group; a group of one dissolves. The UI is
  the strip's / header's right-click menu ("Link fader with" / "Linked faders", ticked members, "Unlink this
  fader"), `Dine::drawLinkGlyph` beside the name (MIXER) and before the fader (TRACKS), and the fader tooltip
  names the partners. Tests: `Linked faders: ...` in `dlive_app_tests`.
