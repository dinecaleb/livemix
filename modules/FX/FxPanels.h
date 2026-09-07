#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <memory>
#include "UI/Widgets.h"
#include "UI/MeterComponent.h"
#include "FX/FxParameters.h"
#include "Core/StyleId.h"

namespace livemix
{

// One UI-rate sample of what the FX engine is doing (editor timer, atomics only).
struct FxLiveState
{
    float inPeakDb = -120.0f, inHoldDb = -120.0f, outPeakDb = -120.0f, outHoldDb = -120.0f;
    bool inClipped = false, outClipped = false;
    FxParameters params;
    float duckDb = 0.0f;           // current delay ducking (<= 0)
    float delayMsL = 0.0f, delayMsR = 0.0f;
    double tempo = 120.0;
    bool hostTempo = false;
    float matchDb = 0.0f;
    float activity = 0.0f;         // 0..1 input level envelope
};

enum class FxModule : int { Input = 0, Delay, Reverb, Output, Count };
const char* fxModuleName (FxModule m) noexcept;
juce::String fxModuleSummary (FxModule m, const FxLiveState& live);
bool fxModuleEnabled (FxModule m, const FxParameters& p) noexcept;

// Signal-chain strip at the bottom of Simple mode: INPUT -> DELAY -> REVERB -> OUTPUT.
class FxChainStrip : public juce::Component
{
public:
    static constexpr int kHeight = 92;
    FxChainStrip();
    std::function<void (FxModule)> onModuleClicked;
    void update (const FxLiveState& live);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Item : public juce::Button
    {
    public:
        Item (FxModule m) : juce::Button (fxModuleName (m)), module (m) {}
        void set (const juce::String& summary, bool enabled, float activity, juce::Colour colour);
        void paintButton (juce::Graphics&, bool over, bool down) override;
        FxModule module;
    private:
        juce::String summary;
        bool enabled = true;
        float activity = 0.0f;
        juce::Colour activityColour;
    };
    Item items[int (FxModule::Count)] { FxModule::Input, FxModule::Delay, FxModule::Reverb, FxModule::Output };
};

// Simple mode: input/output meters, SPACE LENGTH WARMTH CLARITY DISTANCE + MIX, chain strip.
class FxSimplePanel : public juce::Component
{
public:
    explicit FxSimplePanel (juce::AudioProcessorValueTreeState&);
    std::function<void (FxModule)> onOpenModule;

    struct Status { FxType type = FxType::VocalPlate; StyleProfileId style = StyleProfileId::ModernGospel; bool liveSafe = false; };
    void update (const FxLiveState& live, const Status& s);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MeterComponent inMeter, outMeter;
    MacroKnob space, length, warmth, clarity, distance, mix;
    FxChainStrip chain;
    Status status;
    juce::String hint, engineLine, inText, outText;
    FxLiveState last;
};

// Advanced mode: module rail (INPUT DELAY REVERB OUTPUT), a live visual per
// module (decay envelope, repeat pattern, meters) and every parameter as a tile.
class FxAdvancedPanel : public juce::Component
{
public:
    explicit FxAdvancedPanel (juce::AudioProcessorValueTreeState&);
    ~FxAdvancedPanel() override;

    void setModule (FxModule m);
    FxModule getModule() const { return module; }
    void update (const FxLiveState& live);
    void setIntent (const juce::String& s) { if (subtitle != s) { subtitle = s; repaint(); } }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class RailButton;
    class Visual;
    void rebuildTiles();

    juce::AudioProcessorValueTreeState& apvts;
    FxModule module = FxModule::Reverb;
    std::vector<std::unique_ptr<RailButton>> rail;
    std::vector<std::unique_ptr<ParamTile>> tiles;
    std::unique_ptr<Visual> visual;
    std::unique_ptr<MeterComponent> inMeter, outMeter;
    juce::String title, subtitle;
    FxLiveState last;
};

} // namespace livemix
