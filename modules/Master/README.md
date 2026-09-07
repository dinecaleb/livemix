# Dine Master

AU + Standalone (`aufx Lmma Lvmx`, bundle `com.dine.master`, target `LiveMixMaster`). The final mix for one delivery,
built from the shared channel-plugin base (`modules/Common`) and Dine Core. The only Dine product with latency: the
limiter's 1.5 ms lookahead (72 samples at 48 kHz), reported constantly, on or off, and kept during A/B.

| Piece | Where |
|---|---|
| Outputs (the "source" menu) | Livestream (−14 LUFS), Broadcast (−23 LUFS, −2 dBTP), Recording (−18 LUFS, gentle), Room PA (safety limiting only) |
| Simple knobs | WARMTH · CLARITY · GLUE · LOUD · WIDTH (LOUD drives the limiter; the ceiling never moves) |
| Chain | Input → Tone (EQ) → Glue (bus compressor) → Width → Safety (limiter) → Output, with a BS.1770 loudness meter |
| Profile numbers | `src/Profiles/ProfileData.cpp` (`gospelMaster*`, per-output loudness in `applyRoleTargetRefinements`) |
| Tune | `src/Tune/MasterStrategy.cpp` + `tune::setLoudness` / `tune::setWidth` |
| DSP | `src/DSP/Limiter` (sliding-minimum lookahead, hard-clip safety), `src/DSP/LoudnessMeter` (K-weighting, momentary / short-term / gated integrated, interpolated true peak) |
| Parameters | `channelParameterSpecs (Product::Master)`: no gate, transient or de-esser ids |

The Simple view shows short-term loudness against the output's target ("−15.2 LUFS · target −14 · 1.2 dB quiet · peak −1.0").
Verify with `build/modules/Master/livemix_master_plugin_tests` and `build/modules/Master/livemix_master_ui_snapshots <dir>`.
