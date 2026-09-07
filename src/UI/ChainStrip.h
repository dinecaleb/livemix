#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include <memory>
#include "LiveState.h"
#include "Core/ProductDefinition.h"

namespace livemix
{

// The stages of the shared chain, as the UI names them.
using ChainModule = ChainStage;

const char* chainModuleName (ChainModule m) noexcept;                       // engineer's name: "GATE", "COMP", "DE-ESS" ...
const char* chainModuleFriendlyName (ChainModule m, Product p) noexcept;    // plain words for Simple: "CLEAN-UP", "LEVEL", "S CONTROL" ...
juce::String chainSummary (ChainModule m, const LiveState& live);           // "−38 dB", "3.2:1", "−14.2 LUFS" ...
bool chainEnabled (ChainModule m, const ChannelParameters& p) noexcept;    // drives the status dot

// Signal-chain strip at the bottom of Simple mode: one item per stage the
// product exposes (INPUT → ... → OUTPUT), each with a status dot, a plain title
// (the engineer's name underneath), a one-line summary and a 3 px activity
// strip. Clicking a module opens it in Advanced.
class ChainStrip : public juce::Component
{
public:
    static constexpr int kHeight = 92;
    explicit ChainStrip (const ProductDefinition& product);
    std::function<void (ChainModule)> onModuleClicked;
    void update (const LiveState& live);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Item : public juce::Button
    {
    public:
        Item (ChainModule m, Product p) : juce::Button (chainModuleFriendlyName (m, p)), module (m), technical (chainModuleName (m)) {}
        void set (const juce::String& summary, bool enabled, float activity, juce::Colour activityColour);
        void paintButton (juce::Graphics&, bool over, bool down) override;
        ChainModule module;
    private:
        juce::String technical, summary;
        bool enabled = true;
        float activity = 0.0f;
        juce::Colour activityColour;
    };
    const ProductDefinition& product;
    std::vector<std::unique_ptr<Item>> items;
};

} // namespace livemix
