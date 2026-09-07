# Dine Core: how one engine becomes a family of plugins

One shared engine (`src/`, target `livemix_engine`, no JUCE) and one product target per plugin
(`modules/<Product>/`). Products compose; they never duplicate DSP, analysis, tuning, profiles or metering.

```
Dine Core (src/)
├── Core/            block view, dB/fast math, smoothers, envelope followers, FFT
├── DSP/             Biquad, FilterProcessor, ParametricEQ, GateExpander, Compressor (detector HPF),
│                    TransientProcessor, Saturator, LevelMeter, ChannelProcessor (the drum chain)
├── Analysis/        AnalysisFifo, AnalysisEngine (signal-triggered capture, measurements), KitAnalysis
├── Tune/            TuneEngine, SourceStrategy + TuneDecisions, StrategyToolkit, DrumStrategies
├── Profiles/        Profile.h (SourceTargets, ProfileDefinition), ProfileData.cpp (Modern Gospel, Modern Worship), MacroMapping
├── Recommendations/ Recommendation (items, sections), RecommendationEngine facade
├── Intelligence/    SafetyValidator, AnalyzeCoordinator, IIntelligenceProvider (optional AI)
├── Communication/   InstanceRegistry, IKitEndpoint, KitController (process-local; future inter-plugin bus)
├── FX/              DelayLine, Allpass/DampedComb, LFO, TempoSync; ReverbAlgorithm, DelayAlgorithm, FxChain,
│                    FxParameters/Specs, FxMacroMapping, FxProfiles (the Dine FX engine)
└── State/           ParameterSpecs (one table), ParameterIDs; JUCE-side Layout/Bridge/PresetManager
```

## What a product supplies

| Piece | Drums / Vocals / Keys / Master / Guitar / Bass (one shared channel plugin) | FX |
|---|---|---|
| Product definition | `ProductDefinition` (`Core/ProductDefinition.h`, data `Profiles/ProductData.cpp`): sources, five knobs (id, plain label, tooltip), chain stages, wording, kit / loudness flags | `FxType` families |
| Parameter struct + visitor | one `ChannelParameters` + `forEachDspField`; one spec table per product (`channelParameterSpecs (Product)`, `usesStage` hides stages a product does not need; hidden fields stay at defaults) | `FxParameters`, `forEachFxField`, `fxParameterSpecs` |
| Chain | `ChannelProcessor`: filters, gate, corrective EQ, de-esser, comp, transient, tone EQ, saturation, width, trim, [limiter], [loudness meter] (`Options` per product) | `FxChain` = `DelayAlgorithm` + `ReverbAlgorithm` |
| Roles | `ChannelRole` (35 sources, 24 families, `productOf`) | `FxType` (14 types, 2 families) |
| Strategies | `DrumStrategies`, `VocalStrategies`, `KeysStrategies`, `MasterStrategy`, `GuitarStrategies`, `BassStrategies`, all on the `tune::` toolkit (+ `controlSibilance`, `setWidth`, `setLoudness`) | none yet (FX Tune is the next step) |
| Profile data | `ProfileData.cpp`: `SourceTargets` + baseline per family, Worship deltas, per-role refinements (delivery loudness) | `FxProfiles.cpp` |
| Plugin | `modules/Common/ChannelPlugin{Processor,Editor}` + `DineChannelProduct.cmake`; `modules/<Product>/` is a thin subclass | `modules/FX` |
| UI | shared shell; `SimplePanel` builds the knobs from the definition, `ChainStrip` / `AdvancedPanel` show the product's stages with plain names (engineer term as subtitle) | `FxSimplePanel`, `FxAdvancedPanel` |

## The five cases, as built (Milestone 5)

- **Drums (mono close mic)**: unchanged behaviour, now a thin subclass of the shared channel plugin.
- **Vocals**: `DeEsser` (LR4 split, high band ducked only while an S is loud) sits between corrective EQ and the compressor;
  `AnalysisResult::sibilanceDb` (95th percentile of the 5 kHz+ band vs the full band on loud frames) feeds
  `tune::controlSibilance`. Lead / Backing / Choir / Speech / Vocal Bus are distinct strategies with distinct targets.
  Lessons from the church stems: the high-pass never goes above 0.8 x the measured fundamental (male voices), and a
  continuous source never gets an expander.
- **Stereo keys**: `StereoWidth` (mid/side, side high-passed below `monoBelowHz`, correlation meter);
  `AnalysisResult::stereoCorrelation` feeds `tune::setWidth` (narrow when out of phase, a little width when mono-ish).
- **Reverb / delay (FX)**: Milestone 4, unchanged. FX Tune is still not written.
- **Master bus**: `Limiter` (1.5 ms lookahead, sliding-minimum gain, clip safety; latency reported constantly and kept
  during A/B) and `LoudnessMeter` (BS.1770 K-weighting, momentary / short-term / gated integrated, interpolated true
  peak). The "source" menu is the delivery (Livestream -14, Broadcast -23 / -2 dBTP, Recording -18, Room PA safety
  only); `tune::setLoudness` fits the output level to it and waits for a healthy input level first.

- **Guitar (Milestone 6)**: no new DSP. Acoustic / Electric Clean / Electric Drive / Guitar Bus reuse filters, expander,
  corrective + tone EQ, compressor, saturation and `StereoWidth` (stereo modelers). `GuitarStrategies` adds one rule of its
  own, `rollOffFizz`: the low-pass is fitted from the profile template (never the current value) when brilliance and air sit
  well above the profile, so re-tuning the same capture lands on the same frequency. The Clean / Drive split is a role
  refinement (Clean: no template low-pass or drive, more air; Drive: lower crest range, lighter compression).
- **Bass (Milestone 6)**: no new DSP either. Bass DI / Bass Amp / Synth Bass / Bass Bus. `BassStrategies` uses the
  fundamental tracker (30–130 Hz) the way the vocal strategy does: the high-pass never goes above 0.8 x the lowest measured
  note and the expander detector follows 0.7 x the note; `addGrit` adds bounded saturation to a spiky DI. The GRIT knob
  (`ParamID::grit`) is a new macro id, forbidden for Tune and AI like every other knob.

`TuneEngine`, `SafetyValidator`, `AnalysisEngine`, the parameter table mechanism and the coordinator did not change
shape. `ChannelRole` / `RoleFamily` grew instead of being replaced: the released drum indices stay first, and each product
stores its own compact source index in its `role` parameter (`roleIndexInProduct`).

## Inter-plugin communication (future)

`InstanceRegistry` + `IKitEndpoint` are process-local and message-thread only. A cross-product session bus
should keep that shape: endpoints publish measurements and receive bounded trim/tone hints; nothing crosses
the audio thread, nothing is auto-applied without an explicit user action.

## Rules that must hold for every product

- Audio thread: no allocation, locks, logging, string building, file/network/AI work (allocation tracker tests).
- Tune runs off the audio thread and reaches the DSP only through host parameters.
- AI is optional, off by default, explicit action only, `SafetyValidator` on every path, Standard fallback.
- Profile numbers live in profile data files; strategies contain decision logic, not targets.
- Never rename a released parameter ID.
