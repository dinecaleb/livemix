# Dine Bass

AU + Standalone (`aufx Lmba Lvmx`, bundle `com.dine.bass`, target `LiveMixBass`). One bass channel (mono; a stereo input is
processed as two channels), built from the shared channel-plugin base (`modules/Common`) and Dine Core.

| Piece | Where |
|---|---|
| Sources | Bass DI, Bass Amp, Synth Bass, Bass Bus |
| Simple knobs | WARMTH · CLARITY · GRIT · STEADY · CLEAN-UP |
| Chain | Input → Clean-up (expander) → Tone (EQ + drive) → Level (compressor) → Output |
| Profile numbers | `src/Profiles/ProfileData.cpp` (`gospelBass*`, `gospelSynthBass*`, `gospelBassBus*`, Bass Amp role refinements) |
| Tune | `src/Tune/BassStrategies.cpp` (high-pass capped at 0.8 x the measured fundamental, mud / boxy note / clank rules, `addGrit`, expander with the detector under the note) |
| Measurements | fundamental (30–130 Hz), band balance, resonances, crest factor, floor between notes |
| Parameters | `channelParameterSpecs (Product::Bass)`: no transient, de-esser, width or limiter ids; `grit` macro id |

Verify with `build/modules/Bass/livemix_bass_plugin_tests`, `build/modules/Bass/livemix_bass_ui_snapshots <dir>` and, on the
real stem, `build/modules/Drums/livemix_tune_stems "Bass DI" "<stems>/Bass#10.aif"`.
