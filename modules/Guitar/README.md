# Dine Guitar

AU + Standalone (`aufx Lmgt Lvmx`, bundle `com.dine.guitar`, target `LiveMixGuitar`). One guitar channel (mono or stereo),
built from the shared channel-plugin base (`modules/Common`) and Dine Core.

| Piece | Where |
|---|---|
| Sources | Acoustic Guitar, Electric Clean, Electric Drive, Guitar Bus |
| Simple knobs | WARMTH · CLARITY · SMOOTH · STEADY · CLEAN-UP |
| Chain | Input → Clean-up (expander) → Tone (EQ) → Level (compressor) → Width (mid/side, mono below) → Output |
| Profile numbers | `src/Profiles/ProfileData.cpp` (`gospelAcousticGuitar*`, `gospelElectricGuitar*`, `gospelGuitarBus*`, Clean / Drive role refinements) |
| Tune | `src/Tune/GuitarStrategies.cpp` (boom / mud / quack / fizz rules, `rollOffFizz` low-pass for driven amps, expander for amp noise) |
| Measurements | band balance, resonances, crest factor, floor between phrases, `AnalysisResult::stereoCorrelation` |
| Parameters | `channelParameterSpecs (Product::Guitar)`: no transient, de-esser or limiter ids |

Verify with `build/modules/Guitar/livemix_guitar_plugin_tests` and `build/modules/Guitar/livemix_guitar_ui_snapshots <dir>`.
