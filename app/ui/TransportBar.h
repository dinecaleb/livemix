#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// The footer that is always there once a session is set up: go to start, play, stop,
// record, the time, the loop, and the state of the machine (rate, buffer, drops, disk).
// Space plays and stops, R records, Return goes back to the start; the same actions the
// buttons perform, so the keyboard and the mouse can never disagree.
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

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class TransportButton;

    MixController& controller;
    AppServices& services;

    std::unique_ptr<TransportButton> startButton, playButton, stopButton, recordButton, loopButton;
    juce::String timeText, barsText;
    bool playing = false, recording = false, looping = false;
    juce::int64 lastPosition = -1;
    juce::String note;
};

} // namespace livemix
