# DLIVE AI Mix Engineer — TUNE LIVE MIX

*2026-09-12. Read `docs/ARCHITECTURE-DLIVE.md` first: this layer sits above the mix engine
and the deterministic planner, and changes neither.*

---

## 1. What this is

DLIVE already contains a professional mix engineer: `MixPlanner`. It listens to the band,
measures every input at once, tunes each source, resolves the relationships between them,
fits the faders from loudness, tunes the buses and the master, and lands one pass. It is
deterministic and it is tested to be idempotent.

The AI Mix Engineer does not replace that. It **refines it**:

```
                                   the reasoning layer
                                            |
  listen -> AnalysisEngine -> RelationshipEngine -> MixContext
                |                                       |
                |                                       v
                |                            MixReasoningProvider
                |                                       |
                v                                       v
          MixPlanner  ------- the baseline --------> MixIntent
         (deterministic)                                |
                |                          DspCapabilityRegistry
                |                                       |
                |                             CapabilityResolver
                |                                       |
                |                               ProcessingPlan
                |                                       |
                |                             MixSafetyValidator
                |                                       |
                +----------> MixParameters <------------+
                                    |
                              MixEngine (audio thread)
```

The deterministic plan is built **first, always**. Whatever happens to the reasoning pass -
no network, a timeout, a malformed reply, a provider that throws - the user is left with a
professional mix and a sentence saying what happened. The audio never stops for any of it.

## 2. The separation that makes it work

| Question | Who answers it |
|---|---|
| What is happening in the audio? | `AnalysisEngine` / `AnalysisAccumulator` (unchanged) |
| How are the sources interacting? | `RelationshipEngine` (`src/MixAI`) |
| What should this mix sound like? | `MixReasoningProvider` -> `MixIntent` |
| What tools do we actually have? | `DspCapabilityRegistry` |
| How can those tools achieve it? | `CapabilityResolver` -> `ProcessingPlan` |
| Is it safe and valid? | `MixSafetyValidator` |
| Execute it | `MixEngine`, through the same `publish()` a fader uses |

The reasoning layer is creative at the **intent** level. DLIVE is authoritative at the
**capability** level. The safety layer is authoritative at the **execution** level.

## 3. The pieces

All of it is JUCE-free and lives in `src/MixAI` (plus `src/Core/Json` for serialisation),
so it is testable with no host, no device and no network.

- **`RelationshipEngine`** measures and never decides. Kick/bass sub overlap, presence
  masking against the lead, the lead/backing hierarchy, close mics against the overheads,
  room mics against the artificial room, a speech mic open during the song, channel and bus
  processing stacking, the vocal's depth against its intelligibility, and what arrives at the
  master. Every fact carries its value, the profile's tolerance, whether it is a concern and a
  plain sentence.
- **`MixContext`** (schema v1) is the whole session as a versioned, serialisable, provider-
  independent document: session, every track with the measurements that support a decision,
  the buses, the master, the relationships, what the baseline already decided, and
  `MixCaptureAdequacy` - whether the listen is worth mixing from at all.
- **`MixIntent`** (schema v1) describes desired sonic outcomes, never parameters. Objectives
  carry a signed strength (-1..1, the sign is the direction), an optional `against` target for
  separation, an optional `character` for a return, and the two constraints that matter in a
  live mix: `preserveArticulation` and `preserveTransients`.
- **`DspCapabilityRegistry`** (schema v1) is generated from the real parameter tables and from
  how `MixEngine` actually configures each processor. The limiter is on the master because
  that is the only place the stage is turned on; width is offered where the signal is really
  stereo. A processor a source does not carry is reported as unavailable **with the reason**,
  never silently missing.
- **`CapabilityResolver`** turns intent into a `ProcessingPlan`, deterministically, as deltas
  on the deterministic plan. It is where honesty lives: every action carries `EXACT`,
  `APPROXIMATED`, `SUBSTITUTED` or `UNSUPPORTED`. A spring reverb is built from the plate
  engine - short, band-limited, modulated - and says what it will not have. A gated reverb
  needs a gate across the return, which DLIVE has not got, so nothing is applied and the
  reason reaches the user. Separation is a narrow static cut because DLIVE has no dynamic EQ,
  and the plan says so.
- **`MixSafetyValidator`** checks every action against the registry's own ranges and against
  `MixProfile::aiBounds()`. It refuses rather than reinterprets: capture gain (that belongs to
  the console preamp and to gain staging), a gate on a sustained source, a target that is not
  in the mix, a value that is not finite, a control that does not exist, anything that would
  take the master's remaining headroom. A refused EQ gain takes its whole band with it. Every
  refusal keeps its reason and is shown in REVIEW CHANGES.
- **`MixReasoningProvider`** is the provider seam. `LocalMixReasoningProvider` is the default:
  deterministic, offline, reasons from the measured relationships, and is what every test runs
  against. `OpenAiMixProvider` (`app/native`) is the cloud one, opt-in, strict JSON schema.
- **`TuneLiveCoordinator`** is the state machine: `Idle → CapturingInitial → AnalyzingInitial →
  WaitingForReasoning → Resolving → Validating → Applying → CapturingVerify → AnalyzingVerify →
  WaitingForRefinement → ValidatingRefinement → ApplyingRefinement → Ready`, plus `Failed` and
  `Cancelled`. The lifecycle is here and nowhere else.

## 4. Real-time safety

Nothing in `src/MixAI` is reachable from `process()`. The captures are the existing
`MixCapture` (wait-free FIFOs, one worker). The reasoning runs on `TuneLiveCoordinator`'s own
worker and is polled from the message thread. A plan reaches the engine only as one whole
`MixParameters` snapshot through the `TripleBuffer` that every fader move already uses, so
there is no half-applied state to recover from - cancelling is dropping a preview.

## 5. Numbers

Every number the reasoning layer works to is in `src/Profiles/MixProfileData.cpp`, versioned:

- `MixProfile::aiRanges (profile)` - how far a full-strength objective moves one control.
  Deliberately small: these are deltas on a mix that is already good.
- `MixProfile::aiBounds()` - what the layer is never allowed to do. A bound is a safety
  limit, not a sound, so it is the same for every profile.

## 6. The workflow

`MixController::startTuneLiveMix()`. The listen, the deterministic plan (audible immediately,
while the reasoning happens), the intent, the resolution, the bounds, the apply, a second
listen that hears **what was applied**, one conservative refinement, then `Ready`. The result
is an ordinary `MixPlan`, so BEFORE / AFTER, KEEP, REVERT, the mixer, the chain strips and the
Inspector's "what DINE set" all work on it without knowing a reasoning layer was involved.
One source of truth: if it changed a compressor threshold, the compressor shows that threshold.

LIVE SAFE blocks it, like every other re-tune. `MixController::abortTuneMix()` cancels it at
any point; cancelling after the first pass leaves the proposal on BEFORE / AFTER so the user
chooses.

## 7. Privacy

The offline engineer sends nothing anywhere. The cloud provider sends the `MixContext`
document and the capability list - measurements, source names, roles, what DLIVE can do - and
**never audio, never a recording, never a session file**. `sendsDataExternally()` is what the
listen sheet reads to say so while the run is going. Keys live in `AISettings`
(`~/Library/Application Support/LiveMix/ai-settings.xml` or `OPENAI_API_KEY`), never in a
session file and never in a log. Before release this needs a backend so customers are not
shipped a master key.

## 8. Persistence

The mix that runs is the mix that is stored: the kept `MixParameters`, as always. The run's
record (intent, plan, validation counts, review lines) is stored beside it under `tuneLive`
for REVIEW CHANGES and for the record. **Opening yesterday's session never contacts a
provider** and sounds exactly as it did.

## 9. Verifying

```
build-engine/tests/livemix_tests          # RelationshipEngine, MixContext, MixIntent,
                                          # the registry, the resolver, the validator,
                                          # and the whole slice end to end, offline
build/app/dlive_app_tests                 # the controller's live run, a dead provider,
                                          # and the cloud provider's request/response
build/app/dlive_ui_snapshots <dir>         # 08b-tune-live-listening, 09b-tune-live-result
build/app/dlive_mix_stems "<stems>" 30 <out>   # the deterministic path is unchanged
```

## 10. What is deliberately not here

No Studio Mix yet (the architecture is shared, the workflow is not written). No House Sound.
No learning, no telemetry, no data collection. No third-party plug-in hosting - but
`DspCapabilityRegistry` is shaped so AU/VST3 processors can join it later without the
reasoning layer ever learning a brand name.
