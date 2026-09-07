#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include <memory>
#include "Widgets.h"
#include "LiveState.h"
#include "Tune/TuneTypes.h"
#include "Core/ProductDefinition.h"

namespace livemix
{

// Tune workflow overlay: dims the content area (top bars stay visible) and
// shows the Waiting card (play normally, waiting for signal), the Listening
// card (live waveform, seconds remaining, progress) or the Tune card: headline
// ("TOM 1 TUNED"), one line per section (INPUT / TONE / DYNAMICS / ATTACK /
// BLEED / MIX), the detailed decisions, and BEFORE | AFTER, KEEP, REVIEW, REVERT.
class AnalyzeOverlay : public juce::Component, private juce::Timer
{
public:
    enum class Phase { Hidden, Setup, Listening, Processing, Results };

    AnalyzeOverlay();
    ~AnalyzeOverlay() override;

    std::function<void()> onCancel, onKeep, onRevert, onReview;
    std::function<void (bool)> onCompare; // true = AFTER (tuned), false = BEFORE

    void setPhase (Phase p);
    Phase getPhase() const { return phase; }
    void setSourceName (const juce::String& s);
    void setProduct (const ProductDefinition* p) { product = p; }
    void setAIOn (bool on);
    void setListening (float secondsRemaining, float progress, const LevelHistory& history, float inputDb);
    void setTuneResult (const TuneResult& result, const juce::String& aiInterpretation, const juce::String& profileName,
                        bool previewActive, bool showingAfter, float capturedSeconds);
    void setCompareState (bool showingAfter);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {} // swallow clicks on the dim layer

private:
    class Card;
    class ResultRow;
    void timerCallback() override;
    void layoutResults();
    void paintCard (juce::Graphics&, Card&);
    int sectionsHeight() const;

    Phase phase = Phase::Hidden;
    const ProductDefinition* product = nullptr;
    juce::String sourceName;
    bool aiOn = false;
    float remaining = 0.0f, progress = 0.0f, inputDb = -120.0f, pulse = 0.0f;
    const LevelHistory* history = nullptr;

    std::unique_ptr<Card> setupCard, listenCard, resultsCard;
    FlatButton cancelSetup { "CANCEL", FlatButton::Style::Outline }, cancelListen { "CANCEL", FlatButton::Style::Outline };
    FlatButton beforeButton { "BEFORE", FlatButton::Style::Segment }, afterButton { "AFTER", FlatButton::Style::Segment };
    FlatButton keepButton { "KEEP", FlatButton::Style::Solid }, reviewButton { "REVIEW", FlatButton::Style::Accent }, revertButton { "REVERT", FlatButton::Style::Ghost };
    juce::Viewport resultsViewport;
    juce::Component resultsHost;
    std::vector<std::unique_ptr<ResultRow>> rows;
    TuneResult result;
    bool previewActive = false, showingAfter = true;
    std::vector<int> visibleSections; // TuneSection indices shown in the summary block
    juce::String aiText, resultMeta;
};

} // namespace livemix
