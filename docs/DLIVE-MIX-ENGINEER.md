# DLIVE: the mix engineer

TUNE LIVE MIX, REFERENCE MIX, what a microphone hears between the sounds, LIVE SAFE, repeatability, MIX BUDDY, delivery loudness, RAISE LOUDNESS and MASTER SOUND, the AMBIENCE bus, input mappings, the six sound profiles and linked faders. Moved verbatim from the old CLAUDE.md (2026-09-19); the architecture is `docs/ARCHITECTURE-DLIVE-AI.md` and `docs/ARCHITECTURE-DLIVE.md`.

- **WHAT THE MIX IS BUILT AROUND (2026-09-25).** Every rule that said "the lead" used to find one for
  itself - the vocal pocket took the loudest, the follow-down rule the first in the list, the backing
  hierarchy a third answer. There is one now: `MixPlanner`'s `focal`, meaning the source the mix is built
  around. Unpinned it is the lead microphone somebody is really singing into (the widest gap between its
  own level and what it hears between phrases, not the loudest - a spare open on a wedge is louder than a
  singer two feet off a capsule). Pinned, it is whatever the engineer says: `InputAssignment::focus`,
  FOCUS on the TUNE input rail, `MixController::setFocusInput`, stored with the session, at most one.
  Beside it: several lead microphones sung into *together* are held so their sum is one lead's level
  (singers taking a verse each - over half the listen silent - are not a duet and are left alone), and the
  **group balance** finally reads `busBelowVocalsDb` (DRUMS -1, BASS -3, MUSIC -5, SPEECH level,
  AMBIENCE -12 in gospel). That table settles what per-source numbers cannot: how loud a group ends up
  also depends on how many microphones are in it. Group faders are fitted from each bus's predicted
  output, bounded to `maxBusFaderMoveDb` 6, computed from the listen alone so a re-plan lands identically.
  Masking is measured from **fitted** levels now, not from the profile's targets: the balance is fitted
  once before the relationships and again over the chains they change, so the keys are never cut to make
  room for a voice that never reached the mix.
- **A SERMON IS NOT A SONG (2026-09-25).** A service is a sequence of performances and the listen only
  hears the one that is happening, but the master was re-fitted to whatever that was - so the band came
  back through a master built for one voice, and the next TUNE MIX moved it back. A listen where the only
  source playing is the spoken word (`sermonListen`) now leaves the master exactly as the band set it and
  sets the speech group by **what leaves the mix**: the delivery target read through the master already
  there, so the words land where the song lands. Where a fader cannot carry it, the plan names the
  microphone and says to turn its preamp up. The microphones open across a stage during a sermon - the
  overheads, the drum room - are spill then, the way a speech microphone is spill during a song; a
  congregation microphone deliberately is not. Measured on the planner's song → sermon → song sequence:
  the band comes back within 0.5 LU of where it left instead of 2.3 dB hotter.
- **A LISTEN WITH NO PERFORMANCE IN IT IS REFUSED (2026-09-25).** Two ways of asking, both in
  `MixPlanner`: every input DLIVE could hear was steady (never quiet, never far above its own average,
  `stuckSourceCrestDb` 4), or what arrived at the mix never moved at all (`minMasterCrestDb` 4). QUEENSVIEW
  take 002 at 450 s is the case - a kick channel stuck at -2.5 dBFS with a 2.5 dB crest was the only thing
  "playing" and the master went 7.5 dB up to meet it. One held chord measures the same way, which is why a
  single input never decides it. Headline: `MIX: THAT WAS NOT A PERFORMANCE`, and nothing is changed.
- **A BALANCE THAT HOLDS STILL (2026-09-25).** Three things, because two tunes of the same band one verse
  apart were moving faders six and eight dB. (1) The level statistics a balance is fitted from are read
  again over **each third** of the listen and the middle answer taken (`AnalysisAccumulator::levelsOverWindows`,
  one byte per 10 ms frame); a source that played in only one third keeps the whole-capture answer, which
  is right for a solo. (2) A `faderDeadbandDb` of 1: half a dB is inside what two listens disagree about.
  (3) On a mix that has already been tuned (`MixPlanContext::retune`), one fader moves at most
  `maxRetuneFaderStepDb` 4, the plan says what it stopped short of, and RE-TUNE carries the rest. The flag
  belongs to the *listen*, not the moment - MATCH TO REFERENCE and TRY ANOTHER MIX re-plan listens from
  before the last KEEP, and a listen has to plan the same way every time. Every plan now also carries one
  line naming what it is about to move and by how much, against the mix that is running.
- **THE LOUDNESS IS GATED, AND THE CEILING IS A TRUE PEAK (2026-09-25).** `AnalysisResult::loudnessGatedLufs`
  gates the way a delivery meter gates (400 ms blocks on a 100 ms hop, absolute -70, relative -10), which is
  what the whole gain structure is fitted against: ungated, every pause counted as programme and a sermon or
  a quiet song asked to be pushed louder than it was. And `Limiter`'s detector is 4x oversampled (a 48-tap
  polyphase interpolator, 12 taps per phase), so the number the readouts have always called dBTP is one.
  A session now starts on **Livestream** (-14 LUFS, -1 dBTP); Church Broadcast is the television spec and
  says so on its card.
- **SPEECH PRIORITY (2026-09-25)** is the only thing in DLIVE that moves a level on its own, and it is off
  until somebody turns it on (Mix menu, `MixSession::speechPriority`). While the speech group carries
  somebody speaking, DRUMS, BASS and MUSIC step back into the master - 4 dB at 150 ms with a 250 ms hold
  and an 800 ms release, 6 dB under Talk and Podcast, numbers in `MixProfile::speechPriority`. Not the
  voices (a singer under a preacher is a duet), not the room (a congregation answering is the service),
  not the returns, and **never the engineer's listen**: the duck is applied where a group is summed into
  the master, which is after the listen has taken its copy, so a solo is never a lie. Nothing about it
  allocates or computes a coefficient on the audio thread.
- **TUNE <GROUP> and KEEP SOME (2026-09-25).** `MixPlanner::busOnly` / `restrictTo`, `MixController::startTuneBus`
  / `setPlanSelection` / `keepPlanSelection`, the TUNE verb on each group tile and a chip per touched group
  on the result sheet. A group tune is the same listen and the same planner narrowed to one bus, so the band
  can be tuned during the song and the pastor during the sermon without either moving the other or the
  master. KEEP applies exactly what AFTER was playing: `getBase()` is the one place that decides what is
  heard and `compose()` reads it.
- **LEAD AND BGV ARE TWO GROUPS (2026-09-28).** `MixBus` gained `Lead` before `Master` (stored layout 8 slots,
  `SessionStore::kVersion` 6; an older file remaps by the bus count it actually has, as it did for SPEECH and
  AMBIENCE, and nothing but the master's index moves). A lead microphone routes to LEAD, everything else that
  sings - the backing voices, the choir, a vocal subgroup off the desk - to VOCALS, which is **called BGV on
  screen**: the enum name and the stored index are untouched, and "VOCALS" beside "LEAD" read as though one of
  them contained the other. The console shows the groups in `mixBusInDisplayOrder` (DRUMS BASS MUSIC LEAD BGV
  SPEECH AMBIENCE) rather than in enum order, because the enum is the storage and the display is not.

  **What it buys is the group balance.** The groups are set against a reference and the reference is now the
  lead - the thing the mix is *built around* - instead of the voices summed together. Before, adding two
  backing singers changed the VOCALS sum and moved the drums, the bass and the band with it, which is not a
  thing a mix engineer would ever do; now the band is the same mix in both rooms and the only thing that moves
  is the backing group, held under the lead by `busBelowVocalsDb[Vocals]` (-3 dB in Modern Gospel, per profile).
  `MixPlannerTests` asserts exactly that, in both directions. With nobody singing lead the backing group is the
  reference, and a sermon has only speech - the fallback chain is Lead, then BGV, then SPEECH.

  LEAD is glued like a vocal group (`busRole` gives it `VocalBus`), not processed like a vocal channel: the
  de-essing, the boom cut and the presence lift were already done on the microphone itself. The VOICE macro
  moves both vocal groups together, because a pad that brightened the lead and left the backing voices where
  they were would change the blend rather than the tone - and the blend is what the hierarchy rules own. Mix
  Buddy hears "the vocals", "the voices" and "the singers" as the lead, and "BGV", "backing" and "choir" as the
  backing group.
- **AUTOPILOT (2026-09-28, `src/Mix/Autopilot.h`, `MixController::setAutopilot`).** The second thing in DLIVE
  allowed to move a level by itself, and the rules it lives under are in `CLAUDE.md` because they are not
  negotiable: deterministic (there is no AI anywhere in `Autopilot.cpp`), off by default, engaged only on
  purpose, group faders only, bounded to `maxTotalDb` of the mix it was engaged on, every move a Mix history
  entry with its reason, and never the audio thread. It never touches a channel, a chain, the master fader, the
  returns, the room or the engineer's listen.
  **What it is for**: a volunteer sets a mix at 9:30 and then has a camera to run and a door to answer. By
  10:20 the band is louder, the lead has stepped back from the microphone and the backing voices have found
  their confidence, and nobody is at the desk. Autopilot is not a mixing engine - it is the operator's own mix,
  held where they left it, by the smallest move that will do it.
  **It works in relationships**, exactly as the planner does: where each group sat *against the master* when it
  was engaged. Never an absolute level, because "the drums at -6" means nothing an hour later - what the
  engineer set was the drums against the rest of it.
  **Within tolerance it does nothing**, which is the usual answer and the thing the tests spend most of their
  time on (`tests/Mix/AutopilotTests.cpp`). Past the tolerance it moves one `maxStepDb` step, in the direction
  that helps, never past `maxTotalDb` from the engaged mix, and it keeps holding a group until the error is
  well inside the tolerance again (`hysteresisDb`) so a group on the boundary is not nudged all morning. It
  stands down entirely while a listen, a plan, a live run or BYPASS is in the way, and while nothing is
  playing. **An engineer's own move on a group it had been correcting hands that group straight back** - the
  person at the desk outranks the machine standing in for them - and says so once.
  The decision is a pure function of a target, a reading and what has already been moved, so it is tested with
  no engine, no audio and no clock; `MixController` reads the meters off `poll()`, calls it, and applies what
  comes back through `setBusFader`, which is the same path a hand uses and already has LIVE SAFE in it. The
  window says AUTOPILOT on every workspace while it is on (`MainView::StateBar`, shared with the solo band).
- **FAVOURITE MIXES (2026-09-28, `MixController::markFavourite` and friends).** The mixes somebody said worked,
  kept by name in a list - **and not a third store beside SCENES and REFERENCE MIX.** A favourite *is* a scene:
  the scene list grows past its four fixed service slots, so saving, restoring, refusing onto a different
  console and recording a recall are the same code for both, and LIVE's four pads mean exactly what they meant.
  A favourite carries two things a scene does not: a name the engineer gave it, and a `MixFingerprint`.
  **The fingerprint is the point.** A mix is fader positions and chains, and neither of those is what anybody
  means when they say they liked it: what they liked is where things *landed* - the lead over the band, the
  backing under the lead, the kit against the bass, how loud the master was and how much of it was peaks. So it
  is measured from the listen - `RelationshipEngine`'s metrics under their own stable names, where each group
  landed against the master, and the master's own loudness, crest, true peak, correlation and band balance -
  and never read off a fader, because a fader at -6 dB means nothing without knowing what arrived at it. A
  favourite marked before anything has been heard keeps the mix and says out loud that the sound is not
  measured, rather than pretending.
  **And it is aimable at.** `useFavouriteAsReference` builds a `ReferenceProfile` out of the fingerprint's
  master measurements and hands it to `setReference`, so TUNE aims at a mix this church liked through exactly
  the path it aims at a record: same profile bounds, same MATCH TO REFERENCE, no second target system. The
  surface is the Mix history sheet, which is where a list of mixes by name already lived.
- **WHAT THIS MICROPHONE IS DOING (2026-09-28, `MixController::setInputRole` / `voiceJobs`).** A church has
  three or four microphones that do two jobs: the handheld is the pastor's in the sermon and the worship
  leader's in the last song, the lapel is a host and then an MC, the spare at the back is a guest nobody can
  classify until they open their mouth. Those are not the same channel - a preaching microphone is levelled to
  a spoken target, gated, de-essed hard and cut under the boom; a lead vocal is levelled to a sung target,
  never gated, and given a pocket in the band. So a voice channel carries "This microphone is" on the MIXER
  strip's menu and the TRACKS header's menu, in four plain words: SPEAKING / SINGING LEAD / SINGING BACKING /
  CHOIR. One press moves the input to the right group and gives that one strip the profile's own starting
  point for the job. Every other channel is exactly where it was.
  **They are starting points, not presets**: what lands is `startingPoint (session, graph)` for the new role -
  the same table TUNE plans from - so everything stays editable and the next TUNE MIX or RE-TUNE plans the
  channel as what it now is. The graph is rebuilt, so the host calls `reconfigure()` afterwards and LIVE SAFE
  refuses it (`LiveAction::Routing`) with the sentence that says why. A rebuild clears UNDO, so the way back is
  a Mix history checkpoint named "Before <name> became <role>", taken first.
- **EFFECTS ON THIS MICROPHONE (2026-09-29, `MixController::setStripEffects` / `stripEffectsOn` /
  `stripCanHaveEffects`).** The other half of the same problem, and the half a service actually hits: the
  pastor's handheld is the same microphone preaching and singing, and between the two the only thing that has
  to change is whether it is in the plate. So it is one press - an `FX` key on the MIXER strip under M and S,
  and an `EFFECTS ON` / `EFFECTS OFF` button in the Inspector's channel header beside MUTE and SOLO.
  **It is a gate, not a set of numbers.** `StripParameters::effectsOff` holds every send at silence and
  `sendDb` keeps the levels it had, so the press back is exact rather than approximate, a TUNE MIX in between
  can re-plan the send levels and the switch still means the same thing, and - because the sends are
  post-fader - what is already in the plate rings out instead of being chopped. The engine applies it in the
  one place a send is set (`MixEngine`, the smoothed send target), so it costs nothing and cannot click.
  **It is not `setInputRole`.** Nothing is re-routed, no graph is rebuilt, the audio never stops, the chain
  and the tuning are untouched, and the input is still whatever it was on whatever group it was on. That is
  why **LIVE SAFE allows it** where it refuses a role change: it is exactly the move a service needs, and one
  press is the way back. It is saved with the session as an optional `effectsOff` property, so a session
  written before it existed reads as "the effects are on", which is how every one of them sounded.
  **Turning it on for a channel that has never had a send** - a speaking microphone is dry by profile, so
  there is nothing to un-gate - seeds the sends from what the profile gives a lead vocal, the same table TUNE
  plans from. After that the levels are the engineer's and the switch only ever gates them.
  **The plate has to exist before anybody can be put in it**, so `RoutingGraph::build` now marks the two vocal
  returns used wherever the session has a voice microphone, *speech included*. A sermon-only session used to
  build no vocal returns at all, which left nothing to switch on. Nothing sends to them until somebody asks,
  and a session with no voice in it - a drums-only capture - still builds neither.
  The Inspector's SENDS stage says `off` with its lamp out while the switch is off, because a level that is
  being read but not heard has to look like one.
- **RESET MIX TO RAW (2026-09-28, `MixController::resetMixToRaw`).** Everything DLIVE has decided about the
  sound, taken back: every strip's chain, gain, fader, pan and sends, every group's chain and fader, the
  returns, the master, the macros and the sample replacement, all the way to `startingPoint (session, graph)` -
  the mix a service starts from before anything has been listened to. **It is not BYPASS.** Bypass is a way of
  *listening*: it leaves the kept mix alone and switching it off puts everything back. This throws the kept mix
  away and is meant to, which is why it is asked for out loud.
  What it keeps is everything that is not a mix decision: the audio on disk, the clips, the names, the
  assignments, the routing, the scenes, the reference, the per-channel records and the whole mix history - plus
  a "Before reset to raw" checkpoint taken first, and an UNDO entry, so it is never a one-way door. Mutes and
  solos survive because they are the engineer's listening state, exactly as they do through BYPASS. Refused
  under LIVE SAFE (`LiveAction::ResetMix`) with the sentence that says why.
- **THE SCOPE PICKER (2026-09-28).** TUNE always asked the same question and never asked it out loud: the verb
  meant the whole mix, `startTuneBus` was reachable only from a small word inside a group tile, and nothing at
  all offered "these three microphones". Pressing TUNE (the button, the Mix menu, the keyboard) now opens
  `MixPage::ScopeSheet` first - **the whole mix / one group / some channels**, the groups listed by name and
  only the ones this console uses, the channels listed by name with their group beside them, and the sentence
  that says exactly what one press is about to do. The result card names the scope it ran on
  (`MixController::getLastTuneScope`), the whole mix included, so a card can never say "is ready" without
  saying what it is a card about.

  **Some channels** is a third scope on the controller, not a mode in the UI: `startTuneStrips (strips)` runs
  the same listen as TUNE MIX - every input is measured, so the choice is still made in mix context - waits for
  any of the picked strips (`MixCapture::Settings::triggerStrips`, the same field a group tune uses), and
  narrows the plan through `MixPlanner::restrictTo` with a `PlanSelection` of exactly those strips: the buses,
  the master and every unpicked channel are `before`, so what is proposed is what the mix becomes when it is
  kept. One picked channel *is* TUNE CHANNEL, with its shorter listen, and the picker never has to say so.
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
- **THE CYMBALS ARE THE SUM, NOT A CHANNEL (2026-09-30, from the QUEENSVIEW recording).** "Sometimes the cymbals are
  a lot" after TUNE: no channel was too bright on its own, but the overheads and hi-hat were ~60 % of the mix's
  6-12 kHz and ~94 % above 12 kHz, and every microphone near the kit hears them. Nothing summed the top end.
  `MixPlanner` now measures, after the balance and before the group faders, every balanced strip's Brilliance + Air
  where it lands (processed level + fader + that band's share + the shelf its own Tune *aims* at +
  its group's template shelf) against the same sum for Upper-Mid, and holds it
  `Relationships::topEndBelowUpperMidDb` under (8 Gospel, 7 Rock, 6.5 Jazz). Past it, in order: a `spillLimited`
  microphone that is not a cymbal mic loses its top-end lift; the overhead / hi-hat / room high shelf goes to the
  profile's own shelf minus up to `cymbalShelfMaxCutDb` (3); then the overhead and hi-hat faders come down by up to
  `cymbalFaderMaxCutDb` (3). The measurement uses `tune::airShelfAimDb` (what `shapeAir` aims at from the capture
  and the profile), never the shelf the strip runs, so the rule never measures its own earlier cut and a re-tune
  says NO CHANGE REQUIRED. `shapeAir` itself no longer leaves a lift on a source measured *brighter* than its
  profile: the drum bus used to be "smoothed" to +0.5 dB. Measured (6-12 kHz vs 1-3 kHz of the `after` render,
  `dlive_mix_stems ... gospel <t> livestream --check`): 30 s -5.0 -> -6.5, 120 s -7.4 -> -7.7, 200 s -7.6 -> -8.0,
  300 s -4.6 -> -6.9 dB; the bright windows move most, the balanced ones barely, all four pass `--check`. What is
  left above 6 kHz in the bright windows is the voices' own air and the spill-limited microphones' level, not the
  cymbal mics. Levels are still unweighted RMS; a K-weighted balance would read the cymbals ~4 dB louder, but it
  retunes every level number and is a separate piece of work.
- **MANY MICROPHONES, ONE INSTRUMENT (2026-09-30, from the engineer's hand mix of the Praise stems).** Diffing the
  last TUNE MIX checkpoint against the mix the engineer finished by hand showed one systematic fault: every
  microphone was fitted to a whole instrument's level. Net of the group faders they raised, they put Snare Bottom
  12 dB under TUNE, the hi-hat 11, the overheads 20, the playback 14, Kick In and Snare Top 8.5 *over*, and spread
  every L/R pair by hand. `MixPlanner` now: treats a linked group of one kind as one source (strongest member's
  chain and gain, one fader for the sum); blends Kick Out / Snare Bottom under the main mic
  (`MixProfile::blendBelowPrimaryDb`); lets several sources of one kind playing at once share its level
  (`familyShareMaxDb` 6); and the kit's colour targets moved down (hi-hat -33, overheads -31, toms -26). Linking two
  mono channels of one kind in the console pans them as a pair (`stereoPairWidth`). The engineer's taste beyond
  this (which keyboard leads, the lead's 1 kHz cut) is arrangement, not a rule. Evaluate with a 30-mono-channel cut
  of 13:00-15:00 (the only stretch with band and lead together).
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
- **MIX BUDDY** is help, not a second mixing engine (rebuilt 2026-09-30; `src/MixAI/MixBuddy`, `app/ui/ChatSheet`,
  `MixController::askBuddy`). TUNE MIX improves a mix, Autopilot holds one, and Mix Buddy explains, diagnoses and
  shows you where. A question is answered deterministically and offline from `MixController::buddySnapshot()` - a
  copy of the session as it is: each input's level, fader, mute, group, compressor and gate reduction, speech
  priority's duck, the emergency keys, BYPASS, the master's loudness, true peak and limiter, the output guards. "Why
  can't I hear channel 14" walks that path from the outside in and says the first thing that explains it (MUTE,
  BYPASS, no signal at the input, a muted channel or group, a solo in place, a fader or group down, speech priority,
  a weak preamp, a squashing compressor, else masking and TUNE MIX). "How do I" answers name only controls that
  exist, checked against `app/ui`, and say so where DLIVE does not do a thing (a new bus, a shortcut for mix undo).
  **Asking never changes the mix**: no parameter, no history entry, no undo step (`AppTests`: "Mix Buddy: a
  question never changes the mix"). Answers end in buttons (`BuddyAction`, performed by
  `MainView::performBuddyAction`): show a page, open a channel in the Inspector, solo it in the engineer's own
  listen (never under SOLO IN PLACE), CHECK INPUTS, the Mix history, TUNE a channel, or **Propose it** - the one
  button that can lead to a change, `askForChange`, which hands the request to TUNE LIVE MIX (`userRequest`,
  `reuseListen`) and lands on BEFORE / AFTER; it is refused under LIVE SAFE, refused while any other proposal is
  waiting, and kept only by KEEP, as one undo step named "Mix Buddy: <request>". The conversation no longer
  reaches TUNE LIVE MIX. Nothing is kept by starting something else: a TUNE MIX proposal waiting on BEFORE / AFTER
  makes TUNE LIVE MIX refuse, and TRY ANOTHER MIX replaces the reading on preview rather than keeping it.
- **SAMPLE REPLACEMENT** (2026-09-24, `docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md`): TUNE fits the stage's detector on
  the kick-in, snare-top and tom strips from the listen - `tune::setSampleReplacement`, an item of
  `Recommendation::Kind::Sample` in the Bleed section: the threshold between `bleedLevelDb` and the hits (never
  within 6 dB of the bleed; halfway between the floor and the hits when the listen found no separable bleed; and
  never more than 12 dB under the microphone's musical peak on a tom, 18 dB on a kick or snare - QUEENSVIEW showed
  a tom's "events" are mostly the rest of the kit), the sample's level at the microphone's musical peak, the drum's
  fundamental (`replaceDrumHz`, for a sample that follows the drum), the profile's band (its low edge under that
  fundamental), mask and rise - and never the sound or the tuning, which are the engineer's. It **does** switch the
  stage on (with the profile's blend) on a kick, snare or tom whose hits it can tell from the bleed
  (`willSample`, `StrategyToolkit.cpp`); that is a proposal like every other, seen on BEFORE / AFTER and kept only
  by KEEP. (Earlier text here said TUNE never sets the switch; the code has done so since the kit work of
  2026-09-25. Whether it should, for toms especially, is an open product question in `docs/AUDIT-2026-09-30.md`.)
  Kick-out and snare-bottom are never fitted, so two samples never land on one hit. Every number is absolute from
  the capture: a re-tune on the same listen fits the same numbers (tested). `dlive_trigger_check` runs the fit and
  the detector over real takes (`docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md`).
  **A sampled kit is tuned as one** (`TuneContext::sampled` / `kitSampled`, filled by `MixPlanner` from the strips'
  own switches and from the ones this TUNE turns on, so the same settings and listen still give the same plan). A
  microphone whose sample is on is gated far harder (`setGate`: threshold at 0.6 of the floor-to-hit gap, range
  15 dB past the profile's, hold and release at 0.4 / 0.5 of the decay, ratio 10:1) because the sample carries the
  body and the microphone only supplies the attack. Once any kick, snare or tom is sampled, what the other drum
  microphones hear of those drums is the dirt in a clean kit: the hi-hat gets a gentle 10 dB expander it never
  gets on its own, a drum microphone without a sample (kick-out, snare-bottom, a tom on its own) gets its expander
  at half the bleed and 6 dB deeper, and the hi-hat, overheads and room take their high-pass to the top of the
  profile's range. A room microphone is still never gated.
- **TRACK HISTORY** (`app/native/MixHistory.h`, `MixController::getStripHistory` / `restoreStripTune`, 2026-09-24)
  is the other way back: one channel, oldest first, a `StripTuneRecord` for every tune that landed on it and every
  hand edit of its chain - what did it (`what`), which TUNE, the clock, and the strip before and after. It is
  written in exactly two places: `keepPlan` (every strip the plan moved; TUNE MIX / TUNE CHANNEL / TUNE LIVE MIX /
  "Mix Buddy: <request>") and `setStripChannel` ("Inspector edit"). Fader, gain, pan and send moves by hand are
  not tuning and are not recorded; mute, solo and the link are keys, never part of a record. PUT BACK restores a
  record's chain, input gain, level, pan and sends on that channel alone: it is an ordinary mix change
  (`markMixChange ("putting <name> back")`, so UNDO takes it back), LIVE SAFE lets the chain through and bounds
  the level and gain to one step as it does an Inspector edit (the sentence says so), and it is remembered as a
  record of its own ("Put back: TUNE MIX"). 24 records per channel. The history is saved in the session document
  (`SessionStore::Document::history`, absent in older files) and carried across a rearrangement by the same
  `matchInputs` identity as the kept mix (`carryStripHistory`), dropping the records of an input that became a
  different source. The Inspector's trail shows it under the stages (`docs/DLIVE-APP.md`).
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
