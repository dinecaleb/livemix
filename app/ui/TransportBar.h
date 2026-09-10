#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// The transport, in the unified toolbar: go to the start, stop, play, record, loop, and
// the clock beside them. Space plays and stops, R records, Return goes back to the start;
// the same actions the buttons perform, so the keyboard and the mouse can never disagree.
//
// It is a cluster, not a bar: it paints only its own two wells, sizes itself with
// idealWidth() and drops the LENGTH cell when the toolbar is too narrow for it.
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
    int minimumWidth() const;             // keys + the timecode alone
    static constexpr int height = 38;   // the clock well; the keys well is 32, centred in it

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

    juce::Rectangle<int> keysWell, clockWell, timeCell, lengthCell;
    bool showLength = true;
};

} // namespace livemix
