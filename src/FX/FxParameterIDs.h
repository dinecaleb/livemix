#pragma once

// Stable parameter identifiers for Dine FX. Shared shell ids (profile, bypass,
// abMatch, liveSafe) are reused from ParamID so the common UI shell works for
// every product. Never rename a released ID.
namespace livemix::FxParamID
{
    // Identity / mode
    inline constexpr const char* fxType        = "fxType";      // reverb / delay type; loads the profile baseline

    // Simple-mode macros (50 = the profile baseline)
    inline constexpr const char* space         = "space";
    inline constexpr const char* length        = "length";
    inline constexpr const char* warmth        = "warmth";
    inline constexpr const char* clarity       = "clarity";
    inline constexpr const char* distance      = "distance";

    // Input
    inline constexpr const char* inputTrim     = "fxInputTrim";

    // Reverb
    inline constexpr const char* rvOn          = "rvOn";
    inline constexpr const char* rvDecay       = "rvDecay";     // seconds (RT60)
    inline constexpr const char* rvPreDelay    = "rvPreDelay";  // ms
    inline constexpr const char* rvSize        = "rvSize";      // %
    inline constexpr const char* rvDamping     = "rvDamping";   // %
    inline constexpr const char* rvDiffusion   = "rvDiffusion"; // %
    inline constexpr const char* rvLowCut      = "rvLowCut";    // Hz
    inline constexpr const char* rvHighCut     = "rvHighCut";   // Hz
    inline constexpr const char* rvModRate     = "rvModRate";   // Hz
    inline constexpr const char* rvModDepth    = "rvModDepth";  // %
    inline constexpr const char* rvEarly       = "rvEarly";     // early reflections level, %
    inline constexpr const char* rvLevel       = "rvLevel";     // dB

    // Delay
    inline constexpr const char* dlOn          = "dlOn";
    inline constexpr const char* dlMode        = "dlMode";      // Mono / Stereo / Ping-Pong
    inline constexpr const char* dlSync        = "dlSync";
    inline constexpr const char* dlTime        = "dlTime";      // ms (free)
    inline constexpr const char* dlDivision    = "dlDivision";  // note division (sync)
    inline constexpr const char* dlOffset      = "dlOffset";    // right-channel time offset, %
    inline constexpr const char* dlFeedback    = "dlFeedback";  // %
    inline constexpr const char* dlLowCut      = "dlLowCut";    // Hz (in the feedback loop)
    inline constexpr const char* dlHighCut     = "dlHighCut";   // Hz
    inline constexpr const char* dlWidth       = "dlWidth";     // %
    inline constexpr const char* dlDuck        = "dlDuck";      // % (sidechain from the dry signal)
    inline constexpr const char* dlDuckRelease = "dlDuckRelease"; // ms
    inline constexpr const char* dlModRate     = "dlModRate";   // Hz
    inline constexpr const char* dlModDepth    = "dlModDepth";  // %
    inline constexpr const char* dlToReverb    = "dlToReverb";  // % of the delay output fed into the reverb
    inline constexpr const char* dlLevel       = "dlLevel";     // dB

    // Output
    inline constexpr const char* mix           = "fxMix";       // 0 = dry, 1 = wet (aux/send use: 100 %)
    inline constexpr const char* outputTrim    = "fxOutputTrim";
} // namespace livemix::FxParamID
