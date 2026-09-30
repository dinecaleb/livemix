#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// The transport, in the one toolbar (design: `Transport v2`, 117:10151): go to the start,
// stop, play, record, then a divider and the clock. Space plays and stops, R records, Return
// goes back to the start; the same actions the keys perform, so the keyboard and the mouse can
// never disagree. Loop is not one of these keys in v3 - it is a button on the TRACKS tool row,
// beside the loop it sets - but L and Transport > Loop still reach `toggleLoop()`.
//
// It is one well, not a bar: it paints its own plane, sizes itself with idealWidth() and drops
// the session length, then the clock, when the toolbar is too narrow for them.
class TransportBar : public juce::Component
{
public:
    TransportBar (MixController&, AppServices&);
    ~TransportBar() override;

    void refresh();                       // 30 Hz
    std::function<void (const juce::String&)> onToast;
    std::function<void()> onTimelineChanged;   // a take was recorded: the Tracks page should rebuild

    void togglePlay();
    void toggleRecord();
    void returnToStart();
    void toggleLoop();

    int idealWidth() const;               // keys + clock with both cells
    int minimumWidth() const;
    // The keys on their own, with no clock beside them. The toolbar needs this to decide
    // what gives way when the window is narrow: the transport's keys are never dropped,
    // but the clock is, and the document title is entitled to its 96 px before that.
    int keysOnlyWidth() const;             // keys + the timecode alone
    static constexpr int height = 36;   // the pill

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

    juce::Rectangle<int> keysWell, clockWell, timeCell, lengthCell, divider;
    bool showLength = true, showClock = true;
};

} // namespace livemix
