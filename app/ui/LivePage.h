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
    std::unique_ptr<RecordKey> recordButton;
    int health = 0;
    bool liveSafeOn = false;
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
