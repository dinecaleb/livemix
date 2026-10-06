#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "UI/Widgets.h"
#include "native/MixController.h"

namespace livemix
{

// BROADCAST READINESS: the optional before-the-stream checklist, and the services it was run for.
//
// Confirmations only. Marking an item never changes routing, gain, processing, mutes, solos or
// recording, and DINE's observations beside an item are never a tick (BroadcastReadiness.h).
//
// Built the way every v3 sheet is (HistorySheet): a header with the title, one line saying
// where things stand and the close mark; List Rows - lamp, what, why, and the answer on the
// right; and a footer with the default action last. Every answer is a visible control - DONE,
// PROBLEM, SKIP - never a click somewhere on a row that cycles through states nobody can see.
//
// Two views. THIS SERVICE is the active record. PAST SERVICES lists the finished ones; opening
// one shows it read-only, and "Correct it" edits that record in place - it never replaces the
// service in progress.
//
// The list is rebuilt only when what it shows has changed (a fingerprint compared on the tick),
// and never from inside one of its own buttons, so a click always lands on a live control.
class BroadcastReadinessSheet : public juce::Component
{
public:
    enum class Mode { Checklist = 0, History };

    BroadcastReadinessSheet (MixController&, AppServices&, Mode = Mode::Checklist);
    ~BroadcastReadinessSheet() override;

    std::function<void()> onClose;
    std::function<void()> onOpenHistory;          // Mix history (save a favourite)
    std::function<void()> onOpenCheck;            // CHECK INPUTS
    std::function<void()> onOpenOutputs;          // ROUTING > Outputs
    std::function<void (const juce::String&)> onToast;

    void setMode (Mode);
    Mode getMode() const noexcept { return mode; }
    void refresh();                               // the window's tick: rebuilds only on a change
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void lookAndFeelChanged() override;

    // The snapshot tool and the tests.
    int numItemRows() const noexcept;
    int rebuildCount() const noexcept { return rebuilds; }
    void setItemForTest (ReadinessItemId id, ReadinessStatus st) { answer (id, st); }

private:
    struct Row
    {
        enum class Kind { Caption, Item, Record, Note };
        Kind kind = Kind::Item;
        ReadinessItemId item = ReadinessItemId::SourcesMapped;
        std::string recordId;
        ReadinessStatus status = ReadinessStatus::Pending;
        bool needsReview = false, allowsSkip = false, concerning = false, editable = true;
        juce::String title, why, seen, note, value;
        juce::StringArray whyLines, seenLines, noteLines;   // wrapped to the row's width
        int height = 0;
        std::unique_ptr<DineButton> done, problem, skip, shortcut, noteButton, open;
    };

    // The record the list is showing: the active one, or a past one opened from history.
    const ReadinessRecord* shown() const;
    ReadinessRecord* shownMutable();
    bool shownEditable() const;

    juce::Rectangle<int> cardBounds() const;
    std::string fingerprint() const;
    void rebuild();
    void buildChecklist();
    void buildHistory();
    void layoutRows();
    void paintRows (juce::Graphics&);
    void later (std::function<void()>);          // after the click that asked for it has finished

    void answer (ReadinessItemId, ReadinessStatus);
    void askNote (ReadinessItemId);
    void finishService();
    void startNextService();
    void startOver();
    void commitMeta();

    struct ListBody : juce::Component
    {
        explicit ListBody (BroadcastReadinessSheet& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintRows (g); }
        BroadcastReadinessSheet& owner;
    };

    MixController& controller;
    AppServices& services;
    Mode mode = Mode::Checklist;
    std::string openRecord;          // History: the past record shown in full ("" = the list)
    bool correcting = false;         // ...and being corrected
    std::string builtFor;
    int rebuilds = 0, ticks = 0;

    std::vector<std::unique_ptr<Row>> rows;
    juce::Viewport viewport;
    ListBody list { *this };

    DineButton closeButton { juce::String(), DineButton::Style::Ghost };
    DineButton checklistTab { "This service", DineButton::Style::Segment };
    DineButton historyTab { "Past services", DineButton::Style::Segment };
    juce::TextEditor nameField, byField;
    DineButton backButton { "All services", DineButton::Style::Ghost };
    DineButton startOverButton { "Start over", DineButton::Style::Standard };
    DineButton reopenButton { "Reopen", DineButton::Style::Standard };
    DineButton correctButton { "Correct it", DineButton::Style::Standard };
    DineButton finishButton { "Finish", DineButton::Style::Filled };
    DineButton nextButton { "Start the next service", DineButton::Style::Filled };
    std::unique_ptr<juce::AlertWindow> dialog;
    juce::Rectangle<int> headerLine;   // where the one-line summary is drawn
    juce::Rectangle<int> metaArea;     // the name / checked-by captions
    int contentH = 600;                // the list's own height, which the card fits to

    static constexpr int kCardW = 780, kPad = 26;
};

} // namespace livemix
