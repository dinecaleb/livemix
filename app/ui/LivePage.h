#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// LIVE: the view for the service itself. Four cards for the four things that matter while
// it is happening (is it recording, is it going out, is anything clipping, how much room the
// master has), a tile per group bus and one for the effects returns with a meter, a fader
// and the two keys anyone might touch, the engineer's own listen, and LIVE SAFE with what it
// locks, blocks and allows printed rather than implied.
class LivePage : public juce::Component
{
public:
    LivePage (MixController&, AppServices&);
    ~LivePage() override;

    std::function<void (const juce::String&)> onToast;
    std::function<void()> onLiveSafeChanged;
    std::function<void()> onToggleRecord;

    void refresh();                    // 30 Hz
    void rebuild();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class GroupTile;
    class RecordKey;
    class Chip;

    struct Layout { juce::Rectangle<int> status, tiles, monitor, safe; };
    Layout layout() const;

    MixController& controller;
    AppServices& services;
    // One tile per group bus, then the effects returns: DRUMS BASS MUSIC VOCALS SPEECH AMBIENCE FX.
    std::array<std::unique_ptr<GroupTile>, size_t (MixBus::Master) + 1> tiles;
    DineButton liveSafeButton { "LIVE SAFE OFF", DineButton::Style::Standard };
    std::array<std::unique_ptr<DineButton>, 6> chips;   // MONITOR SOLO / SOLO IN PLACE / AFL / PFL / Dim / Clear solo
    juce::Slider monitorLevel { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::unique_ptr<RecordKey> recordButton;

    struct Look
    {
        juce::String recording, recordingNote, output, outputNote, clipping, clippingNote, headroom, headroomNote, monitorNote;
        bool isRecording = false, safe = false, running = false, anyClip = false, inPlace = false, routed = false;
        int soloCount = -1;
        float monitorDb = 0.0f;
        bool operator== (const Look& o) const
        {
            return recording == o.recording && recordingNote == o.recordingNote && output == o.output && outputNote == o.outputNote
                && clipping == o.clipping && clippingNote == o.clippingNote && headroom == o.headroom && headroomNote == o.headroomNote
                && monitorNote == o.monitorNote && isRecording == o.isRecording && safe == o.safe && running == o.running
                && anyClip == o.anyClip && inPlace == o.inPlace && routed == o.routed && soloCount == o.soloCount
                && std::abs (monitorDb - o.monitorDb) < 0.05f;
        }
        bool operator!= (const Look& o) const { return ! (*this == o); }
    };
    Look look;
    void refreshMonitor();
    void updateDiskNote();
    int diskTicks = 0;
    double secondsFree = 0.0;
    float headroomDb = 0.0f;
};

} // namespace livemix
