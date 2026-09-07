#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include <memory>
#include "Widgets.h"
#include "MeterComponent.h"
#include "Communication/KitController.h"

namespace livemix
{

// Kit view: one row per LiveMix instance in this group (name, mini meter,
// dBFS, status chip, recommendation summary), TUNE KIT, and after a
// kit tune the RE-APPLY TUNE / APPLY BALANCE footer. Console-side
// preamp items are listed but never applied.
class KitPanel : public juce::Component
{
public:
    KitPanel();
    ~KitPanel() override;

    std::function<void()> onAnalyzeKit, onApplyAllSafe, onApplyBalance, onCancel;

    void setMembers (const std::vector<InstanceInfo>& members, uint64_t selfId);
    void setResult (const KitRecommendationResult& result, KitController::State state, bool liveSafe);
    void setStatus (const juce::String& text) { if (status != text) { status = text; repaint(); } }
    void setSelfLevel (float peakDb, float holdDb, bool clipped);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    void rebuildRows();

    FlatButton analyzeButton { "TUNE KIT", FlatButton::Style::Solid };
    FlatButton cancelButton { "CANCEL", FlatButton::Style::Outline };
    FlatButton applySafeButton { "RE-APPLY TUNE ON ALL", FlatButton::Style::Solid };
    FlatButton applyBalanceButton { "APPLY BALANCE", FlatButton::Style::Accent };
    juce::Viewport viewport;
    juce::Component rowHost;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<InstanceInfo> members;
    uint64_t selfId = 0;
    KitRecommendationResult result;
    KitController::State state = KitController::State::Idle;
    bool liveSafe = false, safeApplied = false, balanceApplied = false;
    juce::String status, footerText;
};

} // namespace livemix
