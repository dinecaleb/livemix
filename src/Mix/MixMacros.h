#pragma once
#include <array>
#include "MixParameters.h"
#include "RoutingGraph.h"
#include "Core/StyleId.h"

namespace livemix
{

// The five controls a volunteer sees after TUNE MIX. 50 is the plan exactly as Tune
// Mix left it; moving a control makes bounded, musical changes on top of it and never
// touches anything else. Wording is plain: the engineer terms live in Advanced.
enum class MixMacro : int { Vocals = 0, Drums, Bass, Space, Energy, Count };

struct MixMacroValues
{
    std::array<float, int (MixMacro::Count)> v { 50.0f, 50.0f, 50.0f, 50.0f, 50.0f };   // 0..100
    float get (MixMacro m) const noexcept { return v[size_t (m)]; }
    void set (MixMacro m, float value) noexcept { v[size_t (m)] = value < 0.0f ? 0.0f : (value > 100.0f ? 100.0f : value); }
    bool isNeutral() const noexcept { for (float x : v) if (x != 50.0f) return false; return true; }
};

namespace MixMacros
{
    const char* name (MixMacro m) noexcept;        // "VOCALS"
    const char* lowLabel (MixMacro m) noexcept;    // "Warm"
    const char* highLabel (MixMacro m) noexcept;   // "Bright"
    const char* tooltip (MixMacro m) noexcept;     // one or two plain sentences

    // The plan with the macros applied. apply (base, neutral) == base.
    MixParameters apply (const MixParameters& base, const MixMacroValues& values, const RoutingGraph& graph, StyleProfileId profile);

    // The master's voicing on top of that: a bounded tilt for the listener, on the master's
    // tone EQ and saturator only. applyVoicing (base, Neutral) == base.
    MixParameters applyVoicing (const MixParameters& base, MasterVoicing voicing, StyleProfileId profile);
}

} // namespace livemix
