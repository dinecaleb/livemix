#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "UI/Widgets.h"
#include "Mix/MixMacros.h"

namespace livemix
{

// The screen after setup: mix health and TUNE MIX, the five group strips with their
// meters, the five macros, and every input on a rail down the right. While DINELIVE
// listens, and again when the plan is ready, a sheet drops from under the toolbar.
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
    class ListenSheet;
    class ResultSheet;
    class InputRow;

    struct Layout
    {
        juce::Rectangle<int> health, tune, groups, macros, rail;
    };
    Layout layout() const;
    void refreshTuneButton();
    void rebuildRail();

    MixController& controller;
    std::array<std::unique_ptr<GroupTile>, 5> groups;      // DRUMS BASS MUSIC VOCALS FX
    std::array<std::unique_ptr<MacroSlider>, int (MixMacro::Count)> macros;
    std::unique_ptr<ListenSheet> listenSheet;
    std::unique_ptr<ResultSheet> resultSheet;
    std::vector<std::unique_ptr<InputRow>> inputRows;
    juce::Viewport railView;
    juce::Component railHolder;
    DineButton tuneButton { "TUNE MIX", DineButton::Style::Filled };
    DineButton advancedButton { "Open Advanced", DineButton::Style::Standard };
    DineButton resetMacrosButton { "Reset all", DineButton::Style::Ghost };
    int health = 0;
    int builtRailFor = -1;
    juce::String status;
    MixController::Stage lastStage = MixController::Stage::Setup;
};

} // namespace livemix
