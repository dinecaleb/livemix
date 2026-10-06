#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// The transport, in the one toolbar (v4, docs/design/v4): a round-ended pill of five keys -
// back to the start, stop, play, record, loop - and beside it the clock, to the millisecond,
// with the session's length under it. Space plays and stops, R records, Return goes back to
// the start, L loops; the same actions the keys perform, so the keyboard and the mouse can
// never disagree. It sizes itself with idealWidth() and drops the clock when the toolbar is
// too narrow for it; the keys are never dropped.
class TransportBar : public juce::Component
{
public:
    TransportBar (MixController&, AppServices&);
    ~TransportBar() override;

    void refresh();                       // 30 Hz
    std::function<void (const juce::String&)> onToast;
    std::function<void()> onTimelineChanged;   // a take was recorded: the Tracks page should rebuild

    void togglePlay();
    // The last take ended without anybody stopping it - the disk, the device. Stays true until
    // the next take starts, so the status foot can keep saying it after the toast has gone.
    bool takeStoppedByItself() const noexcept { return stoppedByItself; }
    void toggleRecord();
    void returnToStart();
    void toggleLoop();

    int idealWidth() const;               // keys + clock with both cells
    int minimumWidth() const;
    // The keys on their own, with no clock beside them. The toolbar needs this to decide
    // what gives way when the window is narrow: the transport's keys are never dropped,
    // but the clock is, and the document title is entitled to its 96 px before that.
    int keysOnlyWidth() const;             // keys + the timecode alone
    static constexpr int height = 36;   // the pill, and the clock's two lines beside it

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class TransportButton;

    int clockCellWidth() const;
    int lengthCellWidth() const;

    MixController& controller;
    AppServices& services;

    std::unique_ptr<TransportButton> startButton, playButton, stopButton, recordButton, loopButton;
    juce::String timeText { "00:00:00.000" }, lengthText { "0:00.0" };
    bool playing = false, recording = false, looping = false;
    juce::int64 lastPosition = -1, lastLength = -1;
    bool dropSaid = false;                      // this take's "the disk fell behind" has been said
    bool stoppedByItself = false;               // the last take was stopped by DINE (disk, device), not a person
    bool stopPressed = false;                   // a person stopped this take (so its ending is not news)

    juce::Rectangle<int> keysWell, clockWell, timeCell, lengthCell, divider;
    bool showLength = true, showClock = true;
};

} // namespace livemix
