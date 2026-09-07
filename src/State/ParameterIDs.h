#pragma once

// Stable parameter identifiers. Shared by the engine (recommendations, macro
// mapping) and the JUCE parameter layout. Never rename a released ID: DAW
// sessions and automation refer to these strings.
namespace livemix::ParamID
{
    // Identity / mode
    inline constexpr const char* role          = "role";
    inline constexpr const char* profile       = "profile";
    inline constexpr const char* bypass        = "bypass";      // A/B: ORIGINAL vs LIVEMIX
    inline constexpr const char* abMatch       = "abMatch";     // loudness-match the A/B comparison
    inline constexpr const char* liveSafe      = "liveSafe";

    // Simple-mode macros: Dine Drums
    inline constexpr const char* character     = "character";   // 0 clean .. 1 aggressive
    inline constexpr const char* punch         = "punch";
    inline constexpr const char* body          = "body";
    inline constexpr const char* attack        = "attack";
    inline constexpr const char* bleed         = "bleed";
    // Simple-mode macros: Dine Vocals / Keys / Master (0..100, 50 = baseline)
    inline constexpr const char* warmth        = "warmth";
    inline constexpr const char* clarity       = "clarity";
    inline constexpr const char* smooth        = "smooth";      // vocals: de-esser + harshness
    inline constexpr const char* steady        = "steady";      // vocals / keys: compression
    inline constexpr const char* cleanup       = "cleanup";     // vocals: HPF + expander; keys: HPF + mud cut
    inline constexpr const char* shine         = "shine";       // keys: top end
    inline constexpr const char* width         = "width";       // keys / master: stereo width
    inline constexpr const char* glue          = "glue";        // master: bus compression
    inline constexpr const char* loud          = "loud";        // master: drive into the limiter
    inline constexpr const char* grit          = "grit";        // bass: saturation drive

    // Input
    inline constexpr const char* inputTrim     = "inputTrim";
    inline constexpr const char* polarity      = "polarity";
    inline constexpr const char* hpfOn         = "hpfOn";
    inline constexpr const char* hpfFreq       = "hpfFreq";
    inline constexpr const char* hpfSlope      = "hpfSlope";
    inline constexpr const char* lpfOn         = "lpfOn";
    inline constexpr const char* lpfFreq       = "lpfFreq";
    inline constexpr const char* lpfSlope      = "lpfSlope";

    // Gate
    inline constexpr const char* gateOn        = "gateOn";
    inline constexpr const char* gateThreshold = "gateThreshold";
    inline constexpr const char* gateRange     = "gateRange";
    inline constexpr const char* gateAttack    = "gateAttack";
    inline constexpr const char* gateHold      = "gateHold";
    inline constexpr const char* gateRelease   = "gateRelease";
    inline constexpr const char* gateHysteresis= "gateHysteresis";
    inline constexpr const char* gateRatio     = "gateRatio";
    inline constexpr const char* gateScHpf     = "gateScHpf";   // detector high-pass, Hz (0 = off)

    // Corrective EQ (3 bands) -- ids are corrEq{n}{On,Type,Freq,Gain,Q}
    inline constexpr const char* corrEqOn      = "corrEqOn";
    // Tone EQ (4 bands) -- ids are toneEq{n}{On,Type,Freq,Gain,Q}
    inline constexpr const char* toneEqOn      = "toneEqOn";

    // Compressor
    inline constexpr const char* compOn        = "compOn";
    inline constexpr const char* compThreshold = "compThreshold";
    inline constexpr const char* compRatio     = "compRatio";
    inline constexpr const char* compAttack    = "compAttack";
    inline constexpr const char* compRelease   = "compRelease";
    inline constexpr const char* compKnee      = "compKnee";
    inline constexpr const char* compMakeup    = "compMakeup";
    inline constexpr const char* compMix       = "compMix";
    inline constexpr const char* compScHpf     = "compScHpf";   // detector high-pass, Hz (0 = off)

    // Transient
    inline constexpr const char* transOn       = "transOn";
    inline constexpr const char* transAttack   = "transAttack";
    inline constexpr const char* transSustain  = "transSustain";

    // Saturation
    inline constexpr const char* satOn         = "satOn";
    inline constexpr const char* satDrive      = "satDrive";
    inline constexpr const char* satMix        = "satMix";

    // De-esser (vocals)
    inline constexpr const char* deEssOn       = "deEssOn";
    inline constexpr const char* deEssFreq     = "deEssFreq";
    inline constexpr const char* deEssThreshold= "deEssThreshold";
    inline constexpr const char* deEssRange    = "deEssRange";

    // Stereo width (keys, master)
    inline constexpr const char* widthOn       = "widthOn";
    inline constexpr const char* widthAmount   = "widthAmount";  // 0 = mono, 1 = as recorded, 2 = double
    inline constexpr const char* widthMonoBelow= "widthMonoBelow"; // Hz, 0 = off

    // Limiter (master)
    inline constexpr const char* limiterOn     = "limiterOn";
    inline constexpr const char* limiterCeiling= "limiterCeiling";
    inline constexpr const char* limiterRelease= "limiterRelease";

    // Output
    inline constexpr const char* outputTrim    = "outputTrim";

    inline constexpr int kCorrectiveBands = 3;
    inline constexpr int kToneBands = 4;
} // namespace livemix::ParamID
