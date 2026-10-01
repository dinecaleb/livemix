# Would a small local model make DINE's plain-English layer better?

**Research only. Nothing in this document is in the product, and nothing in it should be until the
measurement in §7 has actually been taken.** Written 2026-09-28 as Phase 4 item 2 of
`docs/ROADMAP-RELIABILITY.md`.

The question is narrow on purpose. It is **not** "should DINE use AI" - it already can, optionally, through
`MixReasoningProvider`, and the rules for that are in `CLAUDE.md`. It is: **would a ~1-4 B model, quantized and
shipped in the bundle, read what an engineer types better than `MixRequestParser` does today, by enough to
justify what it costs?**

The short answer is in §8. The rest is how it was reached.

---

## 1. What the offline layer is today

`src/MixAI/MixRequestParser.cpp` - 380 lines, no dependencies, deterministic. It lowercases the sentence and
matches it against three tables: about 25 objective phrases (`harsh|harshness|piercing|shrill|...` →
Brightness, negative), the session's own channel names and roles, and about 20 bus words. Two modifiers scale
the strength (`a little` × 0.5, `a lot` × 1.4). What comes out is a `MixIntent`, which goes through exactly the
same resolver, `MixSafetyValidator` and `MixPlan` as anything a cloud model returns. A sentence it cannot read
comes back `understood == false` with a sentence saying what to try instead.

That last property is the one that matters for this study: **the parser's failure mode is a refusal, and a
refusal is safe.** It is also why the question is not "how many sentences does it understand" but "what does it
do with the ones it half-understands".

## 2. What it actually does with hard sentences — measured

Twelve phrases through `readMixRequest` on the standard test band (the probe was temporary and is not in the
tree; the table is its output, verbatim in substance).

| Typed | What DINE did | Verdict |
| --- | --- | --- |
| `Bring the lead vocal forward` | Lead **and** LEAD, presence +0.60 each | works, but see §3 |
| `Don't make the vocals harsh` | LEAD, brightness −0.55 | accidentally right |
| `The vocals are not harsh, leave them alone` | LEAD, brightness −0.55 | **acts when told not to** |
| `Everything except the pastor needs to come down` | refused | safe |
| `Vocals up, drums down` | refused | safe, and a common request |
| `Make the keys quieter than the guitars` | Keys, level −0.55 | comparative ignored; roughly right |
| `A bit more` | refused | safe |
| `He sounds like he is singing from the car park` | refused | safe |
| `The snare sounds like a wet cardboard box` | Snare, **space +0.55** | **wrong direction** ("wet" matched) |
| `Can you make it sound like last week` | refused | safe - and answered by favourites since 2026-09-28 |
| `Take the reverb off the pastor but leave it on the singers` | Pastor **+space**, LEAD **+space** | **wrong on both counts** |
| `The drums are fine but the bass is too much` | refused | safe |

**Eight of twelve are safe.** Seven refuse, one is right. The four that are not safe fall into two kinds:

- **Negation and scoping** (`not harsh, leave them alone`; `take the reverb off X but leave it on Y`). The
  matcher is substring-based, so a word inside a negation, a contrast or an exclusion reads the same as the
  word on its own. This is the damaging class: DINE acts, confidently, in a direction nobody asked for.
- **Metaphor colliding with the vocabulary** (`wet cardboard box` → "wet" → more reverb). Rare, but the same
  shape: a confident wrong move rather than a refusal.

## 3. A bug this study found, which is not about models at all

`Bring the lead vocal forward` produced **two** targets - the strip `Lead` and the bus `LEAD` - each with
presence +0.60. The safety validator bounds each of them, so nothing unsafe happens, but the request is
answered twice, which is not what the engineer asked for and is more movement than they expect. This is
`findTargets` matching the word "lead" as a channel name *and* as a bus word; it became more likely on
2026-09-28 when LEAD became a bus of its own. **It is worth fixing whatever this study concludes**, and it
needs no model.

## 4. So what would a model actually buy?

Honestly: **the four rows above, and very little else.** Every other failure in the table is a refusal, and a
refusal costs the engineer one retype in DINE's own vocabulary - which the chat already shows them.

That is the answer the roadmap asked to be said out loud if it was the answer. **It is few.**

Two things temper it:

- The four are not evenly weighted. "Take the reverb off the pastor but leave it on the singers" is a *normal
  sentence a volunteer would type*, and DINE doing the opposite of it on a Sunday is a real failure, not a
  cosmetic one. Frequency, not count, is the number that matters, and nobody has counted it - see §7.
- Anaphora (`a bit more`) is the one capability a model adds that no table can: the chat has a history and the
  parser has no memory of it. It refuses safely today, so this is comfort rather than correctness.

## 5. What a model would cost

**Not measured on this Mac.** No model was downloaded, no inference was run, and nothing below is a figure from
this repository. These are the published/typical orders of magnitude for planning, and §7 says what would have
to be measured before any of them is relied on.

| | Order of magnitude |
| --- | --- |
| Candidates | Qwen2.5-1.5B-Instruct, Llama-3.2-1B/3B-Instruct, Phi-3.5-mini (3.8 B), Gemma-2-2B |
| Runtime on Apple Silicon | llama.cpp (Metal) or MLX; both are C++/Objective-C and link cleanly |
| Bundle size at Q4 | ~0.7 GB (1 B), ~1.6 GB (3 B), ~2.2 GB (3.8 B) |
| Resident RAM while loaded | roughly the weights plus a small KV cache: ~1-2.5 GB |
| Cold start (load + first token) | seconds, dominated by reading the weights off disk |
| Per request | a short structured reply is tens of tokens, so sub-second once warm |

The sizes are the number that should stop the conversation. DINE's own bundle is tens of megabytes. A 1.6 GB
model is **two orders of magnitude** more than the application it is helping, shipped to a church booth Mac to
improve four sentences in twelve.

**Dropouts.** This is the part that is not a guess, because it is architecture rather than performance: nothing
proposed here would go near the audio thread. Inference would run on a worker, exactly as `TuneLiveCoordinator`
already does, and reach the engine only as a whole `MixParameters` through the same `publish()` a fader move
uses. The real risk is not the audio thread - it is **memory pressure**: 2 GB resident on an 8 GB M1 that is
also running Dante Virtual Soundcard, a browser for the stream and the recorder is how a booth Mac starts
swapping, and swapping is how a take gets a hole in it. That risk is real, it is not mitigable by being careful
on the audio thread, and it is the strongest argument in this document against shipping a model.

## 6. How it would have to run, if it ever did

Non-negotiable, and all of it already true of the cloud provider:

- **On demand, never continuously.** Loaded on the first chat request of a session, unloaded after N minutes
  idle. Nothing loads at launch; a session that never opens the chat never pays for it.
- **Never on the audio thread**, and never holding a lock anything on the audio thread takes.
- **Never the only path.** It produces a `MixIntent` and nothing else. Every existing rule stands: the
  resolver, `MixSafetyValidator`, the deterministic plan as the fallback, and nothing AI-shaped entering the
  proposed parameters. A model that is slow, absent, corrupted or talking nonsense falls back to
  `MixRequestParser`, which is what is there today - so the worst case is exactly the present.
- **Off by default and visible**, like every other reasoning path (`CLAUDE.md`).
- **macOS only.** Metal, and the Apple Silicon assumption throughout.

## 7. What would have to be measured before this is decided

Nothing in §5 is from this machine, and §4's "few" is a count rather than a frequency. Three measurements
would settle it, in this order:

1. **How often it actually happens.** Log - locally, with consent - the sentences typed into Mix Buddy across
   a dozen real services, and count how many fall in the two unsafe classes in §2. If it is under a few per
   cent, the rest of this document is moot.
2. **Whether a cheaper fix closes it.** A negation/contrast guard in `MixRequestParser` - refuse rather than
   guess when the sentence contains `not`, `don't`, `except`, `but leave`, `rather than` near a matched word -
   would turn every unsafe row in §2 into a refusal. That is perhaps fifty lines and no bundle size at all.
   **Measure the residue after that fix**, not before it: it is the residue a model would be bought for.
3. **Only then**, the model: cold start, warm per-request latency and peak RSS on a base M1 with 8 GB,
   *while a 21-channel session is recording*, against `scripts/benchmark-baseline.txt`'s conditions - and the
   dropped-buffer count over a 90-minute run with and without it.

## 8. Recommendation: **DEFER**, and do §7.2 now

Not "build": the measured benefit is four sentences in twelve, and the cost is a bundle two orders of magnitude
larger plus a memory-pressure risk on exactly the machine DINE is meant to be reliable on. Nothing in §4
justifies that today.

Not "drop": the one class that a model genuinely fixes - negation, contrast and exclusion - contains at least
one sentence a volunteer would plausibly type, and DINE currently does the opposite of it. That is worth
keeping on the list.

**Defer, and take the cheap half now.** The negation/contrast guard in §7.2 turns every measured unsafe case
into DINE's own safe refusal, for fifty lines and no megabytes. The duplicate-target bug in §3 is worth fixing
the same afternoon. If, after both, the logged residue in §7.1 is still material, this document has a real
number to reopen on - and if it is not, then the honest answer was that the deterministic parser plus a
refusal was always good enough, which is the answer this study currently expects.
