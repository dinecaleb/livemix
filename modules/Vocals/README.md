# Dine Vocals

AU + Standalone (`aufx Lmvo Lvmx`, bundle `com.dine.vocals`, target `LiveMixVocals`). One voice channel, built from the
shared channel-plugin base (`modules/Common`) and Dine Core.

| Piece | Where |
|---|---|
| Sources | Lead Vocal, Backing Vocal, Choir, Speech, Vocal Bus (`ChannelRole`, families in `RoleFamily`) |
| Simple knobs | WARMTH · CLARITY · SMOOTH · STEADY · CLEAN-UP (`ProductData.cpp`, mapping in `MacroMapping.cpp`) |
| Chain | Input → Clean-up (expander) → Tone (EQ) → S control (de-esser) → Level (compressor) → Output |
| Profile numbers | `src/Profiles/ProfileData.cpp` (`gospel*Vocal*`, `gospelChoir*`, `gospelSpeech*`, `gospelVocalBus*`) |
| Tune | `src/Tune/VocalStrategies.cpp` + `tune::controlSibilance` (`StrategyToolkit.cpp`) |
| Measurements | sibilance (95th percentile of the 5 kHz+ band vs full band on loud frames), `AnalysisResult::sibilanceDb` |
| Parameters | `channelParameterSpecs (Product::Vocals)`: no transient shaper, width or limiter ids |

Plain-language rules: every knob label is a word a volunteer knows; the chain strip shows the friendly name with the
engineer's name underneath; Tune explains WHAT and WHY in sentences. Verify with
`build/modules/Vocals/livemix_vocals_plugin_tests` and `build/modules/Vocals/livemix_vocals_ui_snapshots <dir>`.
