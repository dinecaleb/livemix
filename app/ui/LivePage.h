#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// LIVE: the view for the service itself. Once the mix is built, nobody should have to
// watch forty processors — only what matters while it is happening: is it recording, is
// the output going out, is anything clipping, and the group faders anyone might touch.
// LIVE SAFE locks away everything that could change the sound by accident.
class LivePage : public juce::Component
{
public:
    LivePage (MixController&, AppServices&);
    ~LivePage() override;

    std::function<void (const juce::String&)> onToast;
    std::function<void()> onLiveSafeChanged;
    std::function<void()> onToggleRecord;      // the transport's own Record, so there is one code path

    void refresh();                    // 30 Hz
    void rebuild();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class GroupFader;
    class RecordKey;

    juce::Rectangle<int> body() const;

    MixController& controller;
    AppServices& services;
    // One tile per group bus, then the effects returns as one more: DRUMS BASS MUSIC VOCALS
    // SPEECH MASTER FX.
    std::array<std::unique_ptr<GroupFader>, int (MixBus::Count) + 1> faders;
    DineButton liveSafeButton { "LIVE SAFE", DineButton::Style::Standard };
    // The engineer's own listen. Everything here is monitoring: none of it can change what
    // the room and the stream hear, which is the entire point of the monitor bus.
    DinePopup soloModeButton;              // MONITOR SOLO / SOLO IN PLACE
    DineButton soloPointButton { "AFL", DineButton::Style::Standard };
    DineButton dimButton { "Dim", DineButton::Style::Standard };
    DineButton clearSoloButton { "Clear solo", DineButton::Style::Standard };
    juce::Slider monitorLevel { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::unique_ptr<RecordKey> recordButton;
    // What the page is currently showing. A frame that would draw the same thing is skipped:
    // the group tiles keep their own meters moving, and this is only the text around them.
    struct Look
    {
        juce::String clock, state, stateNote, output, notes;
        int health = -1;
        int headroomTenths = 0;
        bool recording = false, safe = false, running = false;
        int soloCount = -1, xruns = -1, tunes = -1;
        bool monitorRouted = false, inPlace = false;
        bool operator== (const Look& o) const
        {
            return clock == o.clock && state == o.state && stateNote == o.stateNote && output == o.output
                && notes == o.notes && health == o.health && headroomTenths == o.headroomTenths
                && recording == o.recording && safe == o.safe && running == o.running
                && soloCount == o.soloCount && xruns == o.xruns && tunes == o.tunes
                && monitorRouted == o.monitorRouted && inPlace == o.inPlace;
        }
        bool operator!= (const Look& o) const { return ! (*this == o); }
    };
    Look look;
    int health = 0;
    bool liveSafeOn = false;
    int soloCount = 0;
    bool monitorRouted = false;
    void refreshMonitor();
    bool recordingOn = false;
    float headroomDb = 0.0f;
    juce::String clock, state, stateNote;
    juce::Colour stateNoteColour;
    // The disk is a syscall, so it is asked a few times a minute rather than 30 times a second.
    void updateDiskNote();
    int diskTicks = 0;
    double secondsFree = 0.0;
};

} // namespace livemix
