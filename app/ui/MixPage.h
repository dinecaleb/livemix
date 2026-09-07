#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "UI/Widgets.h"
#include "UI/MeterComponent.h"
#include "Mix/MixMacros.h"

namespace livemix
{

// The main screen after setup: MIX HEALTH, the five groups with their meters, TUNE MIX,
// the five macros, and the plan card (BEFORE / AFTER, KEEP, REVERT) while a plan is
// previewed. While listening an overlay shows "LISTENING..." with a tick per group.
// Nothing here looks like a console; Advanced is one click away for engineers.
class MixPage : public juce::Component
{
public:
    explicit MixPage (MixController&);
    ~MixPage() override;

    std::function<void()> onOpenAdvanced;
    std::function<void (const juce::String&)> onToast;

    void refresh();                    // 30 Hz: meters, stage, health
    void paint (juce::Graphics&) override;
    void resized() override;

    // Programmatic equivalents of the user's actions (also used by the snapshot tool).
    void pressTune();
    void setMacroValue (MixMacro m, float v);

private:
    class GroupTile;
    class MacroSlider;
    class ListenOverlay;
    class PlanCard;

    void refreshTuneButton();

    MixController& controller;
    std::array<std::unique_ptr<GroupTile>, 5> groups;      // DRUMS BASS MUSIC VOCALS FX
    std::array<std::unique_ptr<MacroSlider>, int (MixMacro::Count)> macros;
    std::unique_ptr<ListenOverlay> overlay;
    std::unique_ptr<PlanCard> card;
    FlatButton tuneButton { "TUNE MIX", FlatButton::Style::Solid };
    FlatButton advancedButton { "ADVANCED", FlatButton::Style::Outline };
    FlatButton resetMacrosButton { "RESET", FlatButton::Style::Ghost };
    int health = 0;
    juce::String status;
    MixController::Stage lastStage = MixController::Stage::Setup;
};

} // namespace livemix
