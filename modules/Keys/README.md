# Dine Keys

AU + Standalone (`aufx Lmky Lvmx`, bundle `com.dine.keys`, target `LiveMixKeys`). One keys channel (mono or stereo),
built from the shared channel-plugin base (`modules/Common`) and Dine Core.

| Piece | Where |
|---|---|
| Sources | Piano, Electric Piano, Organ, Synth Pad, Synth Lead, Keys Bus |
| Simple knobs | WARMTH · SHINE · CLEAN-UP · STEADY · WIDTH |
| Chain | Input → Tone (EQ) → Level (compressor) → Width (mid/side, mono below) → Output |
| Profile numbers | `src/Profiles/ProfileData.cpp` (`gospelPiano*`, `gospelElectricPiano*`, `gospelOrgan*`, `gospelSynth*`, `gospelKeysBus*`) |
| Tune | `src/Tune/KeysStrategies.cpp` + `tune::setWidth` (stereo correlation, mono-below) |
| Measurements | `AnalysisResult::stereoCorrelation`, band balance, resonances |
| Parameters | `channelParameterSpecs (Product::Keys)`: no gate, transient, de-esser or limiter ids |

Verify with `build/modules/Keys/livemix_keys_plugin_tests` and `build/modules/Keys/livemix_keys_ui_snapshots <dir>`.
