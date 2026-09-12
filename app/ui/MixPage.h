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
// meters, the five macros, and every input on a rail down the right. While DLIVE
// listens, and again when the plan is ready, a sheet drops from under the toolbar.
class MixPage : public juce::Component
{
public:
    explicit MixPage (MixController&);
    ~MixPage() override;

    std::function<void()> onOpenAdvanced;
    std::function<void (int strip)> onTuneStrip;      // TUNE CHANNEL, from the input rail
    std::function<void (const juce::String&)> onToast;

    void refresh();                    // 30 Hz: meters, stage, health

    // The input rail folds away when the mix, not the list, is what you are working on.
    void setRailShown (bool);
    bool isRailShown() const noexcept { return railShown; }

    void paint (juce::Graphics&) override;
    void resized() override;

    // Programmatic equivalents of the user's actions (also used by the snapshot tool).
    void pressTune();
    // TUNE LIVE MIX: the same listen with a mix engineer's reasoning on top, then a second
    // listen to check what it did. The deterministic TUNE MIX beside it is unchanged.
    void pressLiveTune();
    void setMacroValue (MixMacro m, float v);

private:
    class GroupTile;
    class MacroSlider;
    class ListenSheet;
    class ResultSheet;
    class InputRow;

    struct Layout
    {
        juce::Rectangle<int> health, tune, liveTune, groups, macros, rail, railTab;
    };
    Layout layout() const;
    int railWidth() const noexcept { return railShown ? Dine::Metric::rail : Dine::Metric::panelTab; }
    void refreshTuneButton();
    void rebuildRail();

    MixController& controller;
    // One tile per group bus, then the FX returns: DRUMS BASS MUSIC VOCALS SPEECH FX.
    std::array<std::unique_ptr<GroupTile>, size_t (MixBus::Master) + 1> groups;
    std::array<std::unique_ptr<MacroSlider>, int (MixMacro::Count)> macros;
    std::unique_ptr<ListenSheet> listenSheet;
    std::unique_ptr<ResultSheet> resultSheet;
    std::vector<std::unique_ptr<InputRow>> inputRows;
    juce::Viewport railView;
    juce::Component railHolder;
    std::unique_ptr<DinePanelTab> railTab;
    bool railShown = true;
    DineButton tuneButton { "TUNE MIX", DineButton::Style::Standard };
    DineButton liveTuneButton { "TUNE LIVE MIX", DineButton::Style::Filled };
    DineButton advancedButton { "Open Advanced", DineButton::Style::Standard };
    DineButton resetMacrosButton { "Reset all", DineButton::Style::Ghost };
    int health = 0;
    int builtRailFor = -1;
    juce::String status;
    MixController::Stage lastStage = MixController::Stage::Setup;
    bool lastLiveRun = false;
};

} // namespace livemix
