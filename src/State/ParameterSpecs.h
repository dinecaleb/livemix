#pragma once
#include <string>
#include <vector>
#include "Core/ChannelRole.h"

namespace livemix
{

// Host-agnostic description of every automatable parameter. The JUCE layout is
// generated from this table and the safety validator clamps recommendations
// against it, so bounds are defined exactly once.
struct ParameterSpec
{
    enum class Type { Float, Bool, Choice };

    std::string id;
    std::string name;
    Type type = Type::Float;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    float skewMidpoint = 0.0f;      // > 0: value shown at the middle of the range (log-style), 0 = linear
    std::string unit;
    std::vector<std::string> choices; // for Type::Choice
    bool automatable = true;

    float clamp (float v) const noexcept
    {
        return v < minValue ? minValue : (v > maxValue ? maxValue : v);
    }
};

// One table per channel product: shell parameters (role, profile, A/B, Live
// Safe), that product's five Simple knobs, then the DSP fields the product uses.
// Fields a product does not use are absent from its table and stay at their
// struct defaults (off) inside the plugin.
const std::vector<ParameterSpec>& channelParameterSpecs (Product p);
const std::vector<ParameterSpec>& allParameterSpecs();       // Dine Drums table (older name)
bool productUsesParameter (Product p, const std::string& id);

// Looks a parameter up by id across every product table (channel products first,
// then Dine FX). DSP ids are identical in every table that carries them.
const ParameterSpec* findParameterSpec (const std::string& id);

} // namespace livemix
