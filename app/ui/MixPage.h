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

class ReferenceSheet;

// TUNE: the mix's health and the verbs that build it, the group buses with their faders and
// meters, the five macros, and every input on a rail down the left. While DLIVE listens, and
// again when the plan is ready, a sheet drops over the workspace.
class MixPage : public juce::Component
{
public:
    explicit MixPage (MixController&);
    ~MixPage() override;

    std::function<void()> onOpenAdvanced;
    std::function<void (int strip)> onTuneStrip;      // TUNE CHANNEL, from the input rail
    std::function<void (const juce::String&)> onToast;
    std::function<void()> onOpenChat;                 // Mix Buddy, from the action column
    std::function<void (int strip)> onSelectStrip;    // a row on the rail was picked out
    int selectedStrip() const noexcept { return selectedRow; }

    void refresh();                    // 30 Hz: meters, stage, health

    void setRailShown (bool);
    bool isRailShown() const noexcept { return railShown; }
    void setRailAvailable (bool);
    bool isRailAvailable() const noexcept { return railAvailable; }

    void paint (juce::Graphics&) override;
    void resized() override;

    void openReference();
    void pressTune();
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
        juce::Rectangle<int> health, actions, groupsCaption, groups, master, macrosCaption, macros, rail, railTab;
    };
    Layout layout() const;
    int railWidth() const noexcept
    {
        return ! railAvailable ? 0 : railShown ? Dine::Metric::tuneRail : Dine::Metric::panelTab;
    }
    void refreshTuneButton();
    void refreshMaster();              // the loudness readout and the two pickers, a few times a second
    void rebuildRail();
    void selectRow (int strip);

    MixController& controller;
    // One tile per group bus, then the FX returns: DRUMS BASS MUSIC VOCALS SPEECH AMBIENCE FX.
    std::array<std::unique_ptr<GroupTile>, size_t (MixBus::Master) + 1> groups;
    std::array<std::unique_ptr<MacroSlider>, int (MixMacro::Count)> macros;
    std::unique_ptr<ListenSheet> listenSheet;
    std::unique_ptr<ResultSheet> resultSheet;
    std::unique_ptr<ReferenceSheet> referenceSheet;
    std::vector<std::unique_ptr<InputRow>> inputRows;
    juce::Viewport railView;
    juce::Component railHolder;
    std::unique_ptr<DinePanelTab> railTab;
    bool railShown = true, railAvailable = true;
    DineButton tuneButton { "TUNE MIX", DineButton::Style::Filled };
    DineButton liveTuneButton { "TUNE LIVE MIX", DineButton::Style::Standard };
    DineButton referenceButton { "Reference", DineButton::Style::Standard };
    DineButton chatButton { "Mix Buddy", DineButton::Style::Standard };
    DineButton undoButton { "Undo mix", DineButton::Style::Standard };
    DineButton redoButton { "Redo mix", DineButton::Style::Standard };
    DineButton advancedButton { "Open the Inspector", DineButton::Style::Ghost };
    DineButton resetMacrosButton { "Reset macros", DineButton::Style::Ghost };
    // MASTER: how loud the finished mix should be, one press to get there, and who it is for.
    DinePopup loudnessTargetButton;
    DineButton raiseButton { "Raise loudness to target", DineButton::Style::Standard };
    DinePopup voicingButton;
    juce::String masterNote;
    bool raisePossible = false;
    int health = 0;
    struct PageLook
    {
        juce::String status, notes;
        int health = -1;
        MixController::Stage stage = MixController::Stage::Setup;
        int tunes = -1;
        bool hasReference = false, canUndo = false, canRedo = false;
        juce::String referenceName, masterNote;
        bool operator== (const PageLook& o) const
        {
            return status == o.status && notes == o.notes && health == o.health && stage == o.stage && tunes == o.tunes
                && hasReference == o.hasReference && referenceName == o.referenceName && canUndo == o.canUndo && canRedo == o.canRedo
                && masterNote == o.masterNote;
        }
        bool operator!= (const PageLook& o) const { return ! (*this == o); }
    };
    PageLook painted;
    int builtRailFor = -1;
    int selectedRow = -1;
    juce::String status;
    MixController::Stage lastStage = MixController::Stage::Setup;
    bool lastLiveRun = false;
    int adviceTicks = 0;
};

} // namespace livemix
