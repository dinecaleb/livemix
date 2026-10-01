#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// LIVE: the view for the service itself, built to "06b - Live - decluttered" (161:18761).
// One health strip for the four things that matter while it is happening (is it recording,
// is it going out, is anything clipping, how much room the master has); the groups as tall
// strips with the scene picker over them; and a rail that says what LIVE SAFE and Autopilot
// are doing, in sentences, with the engineer's own listen - and where it goes - at its foot.
class LivePage : public juce::Component
{
public:
    LivePage (MixController&, AppServices&);
    ~LivePage() override;

    std::function<void (const juce::String&)> onToast;
    std::function<void()> onLiveSafeChanged;
    std::function<void()> onToggleRecord;
    std::function<void()> onOpenHistory;      // MIX HISTORY: the mix as it was, by name

    // The sidebar's SCENES row brings you here and points at them: the scenes live on LIVE,
    // where the service is run from, and nowhere else.
    void focusScenes();

    void refresh();                    // 30 Hz
    void rebuild();
    void paint (juce::Graphics&) override;
    void resized() override;

    // The group row opened out to the effect returns, or back to the groups.
    void showEffects (bool open);
    bool effectsShown() const noexcept { return effectsOpen; }

private:
    class GroupTile;
    class Link;
    class LevelLine;
    class SafeDetail;

    struct Layout
    {
        juce::Rectangle<int> health, groupsHeader, sceneCaption, sceneTrack, strips, safe, autopilot, monitor,
                             modesA, modesB, output, speaking, priorityRow, shareRow;
    };
    Layout lay;                        // measured in resized(), and again when a card changes height
    int sceneFlash = 0;                // frames left of the mark the sidebar's SCENES row leaves

    MixController& controller;
    AppServices& services;
    // One strip per group bus in the console's order, then the effects returns.
    std::array<std::unique_ptr<GroupTile>, size_t (MixBus::Master) + 1 + size_t (FxSlot::Count)> tiles;
    // EACH EFFECT: the row opened out to the returns, one fader each, instead of the groups.
    bool effectsOpen = false;
    DineButton effectsButton { "Each effect", DineButton::Style::Standard };
    bool anyEffects() const;

    // SCENES: the picker over the strips. A kept scene comes back in one press; KEEP writes the
    // mix that is running into the one picked.
    std::array<std::unique_ptr<DineButton>, 4> sceneSegments;
    DineButton keepButton { "Keep", DineButton::Style::Standard };
    int sceneSlot = -1;
    void refreshScenes();

    std::unique_ptr<Link> safeLink, autopilotLink;
    // SPEAKING MICS: speech priority and share the mics, the two things that move a level for
    // the spoken word, on LIVE where a service or a show is run - not only in the Mix menu.
    std::unique_ptr<Link> priorityLink, shareLink;
    juce::String priorityText() const;
    juce::String shareText() const;

    // The engineer's listen: MONITOR SOLO / SOLO IN PLACE, AFL / PFL, CLEAR SOLO, DIM, where
    // solo goes and how loud it is there.
    std::array<std::unique_ptr<DineButton>, 4> modes;
    DineButton clearSolo { "CLEAR SOLO", DineButton::Style::Standard };
    DineButton monitorDim { "DIM", DineButton::Style::Toggle };
    DinePopup soloDevice;                                // where solo goes: the device only the engineer hears
    std::unique_ptr<LevelLine> monitorLevel;

    struct Look
    {
        juce::String recording, recordingNote, output, outputNote, clipping, clippingNote, headroom, headroomNote,
                     monitorNote, autopilotSince;
        juce::StringArray autopilotLog;
        bool isRecording = false, safe = false, running = false, anyClip = false, inPlace = false, routed = false,
             autopilotOn = false, autopilotMoved = false, priorityOn = false, shareOn = false;
        int speakingMics = 0;
        int soloCount = -1;
        float headroomDb = 0.0f;
        bool operator== (const Look& o) const
        {
            return recording == o.recording && recordingNote == o.recordingNote && output == o.output && outputNote == o.outputNote
                && clipping == o.clipping && clippingNote == o.clippingNote && headroom == o.headroom && headroomNote == o.headroomNote
                && monitorNote == o.monitorNote && autopilotSince == o.autopilotSince && autopilotLog == o.autopilotLog
                && isRecording == o.isRecording && safe == o.safe && running == o.running && anyClip == o.anyClip
                && inPlace == o.inPlace && routed == o.routed && autopilotOn == o.autopilotOn
                && autopilotMoved == o.autopilotMoved && soloCount == o.soloCount
                && priorityOn == o.priorityOn && shareOn == o.shareOn && speakingMics == o.speakingMics
                && std::abs (headroomDb - o.headroomDb) < 0.05f;
        }
        bool operator!= (const Look& o) const { return ! (*this == o); }
    };
    Look look;
    juce::String safeText() const;
    juce::String autopilotText() const;
    void refreshMonitor();
    void updateDiskNote();
    int diskTicks = 0, adviceTicks = 0;
    double secondsFree = 0.0;
    juce::String clipText, clipNote;
    bool anyClipping = false;
    size_t checkpointsSeen = size_t (-1);
    juce::StringArray autopilotLog;
    juce::String autopilotSince;
};

} // namespace livemix
