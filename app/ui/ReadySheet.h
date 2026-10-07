#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// READY TO GO LIVE? (v4). Nine things (ten with a broadcast checklist under way), each read from what DINE already knows - never a box
// somebody ticks, which is what the broadcast checklist is (BroadcastReadiness, kept apart):
// the device, the inputs, recording, the disk, on air, loudness, BYPASS, LIVE SAFE and the
// autosave. Each says how it is in a sentence and, where there is one, offers the one press
// that fixes it.
struct ReadyCheck
{
    enum class State { Ok, Warn, Crit, Neutral };
    enum class Fix { None, AudioDevice, CheckInputs, ArmAll, Outputs, RaiseLoudness, BypassOff, LiveSafeOn, SaveNow, OpenChecklist };
    struct Row
    {
        juce::String title, sentence;
        State state = State::Neutral;
        Fix fix = Fix::None;
        juce::String fixLabel;
    };
    // Read-only: asks the controller and the host, changes nothing.
    static std::vector<Row> gather (MixController&, AppServices&);
    // How many rows want somebody: the readiness pill's number.
    static int problems (const std::vector<Row>&);
    // The disk in the status line's words: "34.2 GB", with "· 9 h 40 m" when it can say.
    static juce::String diskText (juce::int64 bytesFree, double secondsFree);
};

class ReadySheet : public juce::Component
{
public:
    ReadySheet (MixController&, AppServices&);

    std::function<void()> onClose, onGoToLive, onOpenChecklist;
    std::function<void (ReadyCheck::Fix)> onFix;

    void refresh();                       // MainView's slow tick: the rows follow what changes
    const std::vector<ReadyCheck::Row>& getRows() const noexcept { return rows; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<int> cardBounds() const;
    int rowHeight() const;                // kRowH, less on a short window so every row shows
    void rebuildButtons();

    MixController& controller;
    AppServices& services;
    std::vector<ReadyCheck::Row> rows;
    std::vector<std::unique_ptr<DineButton>> fixes;
    DineButton close { "", DineButton::Style::Standard };
    DineButton live { "Go to Live", DineButton::Style::Standard };
    DineButton checklist { "Broadcast checklist", DineButton::Style::Ghost };
    DineButton done { "Done", DineButton::Style::Filled };
    static constexpr int kCardW = 640, kRowH = 62, kHeadH = 74, kFootH = 64;
};

} // namespace livemix
