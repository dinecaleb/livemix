#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// TRACKS: the timeline. A tool row along the top (row height, zoom, snap, follow, markers),
// then the ruler with its loop strip and marker lane, the track headers on the left (arm,
// monitor, mute, solo, meter) and the recorded clips on the right with the playhead over them.
//
// The whole surface is drawn and hit-tested by hand rather than built from thousands of
// child components, so 64 tracks scroll and zoom as smoothly as eight. Waveforms come
// from juce::AudioThumbnail, which reads and caches peaks on its own thread.
class TracksPage : public juce::Component
{
public:
    enum class RowHeight { Small = 0, Medium, Large };

    TracksPage (MixController&, AppServices&);
    ~TracksPage() override;

    std::function<void (int strip)> onOpenStrip;      // the Inspector
    std::function<void (const juce::String&)> onToast;
    std::function<void()> onTimelineChanged;          // markers / loop moved: save

    void refresh();                    // 30 Hz
    void rebuild();                    // the session, the timeline or the device changed

    // Editing, also reachable from the menu and the keyboard.
    void splitAtPlayhead();
    void deleteSelection();
    void undo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    void zoom (double factor);
    void zoomToFit();
    void setRowHeight (RowHeight);
    void addMarkerAtPlayhead();
    void setSnap (bool on);
    bool snapEnabled() const noexcept { return snap; }
    void setFollow (bool on);
    bool followEnabled() const noexcept { return follow; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Drag { None, Playhead, ClipMove, ClipTrimStart, ClipTrimEnd, TrackHeight, Scroll,
                      LoopRange, Marker };
    struct ClipRef { int track = -1; int index = -1; bool valid() const noexcept { return track >= 0 && index >= 0; } };

    // Geometry
    int lanesTop() const;
    juce::Rectangle<int> toolbarArea() const;
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> markerArea() const;
    juce::Rectangle<int> lanesArea() const;
    int trackTop (int track) const;
    int trackHeight (int track) const;
    int totalTrackHeight() const;
    int numTracks() const;
    double samplesPerPixel() const;
    double gridSeconds() const;                       // the ruler's current tick, in seconds
    int sampleToX (juce::int64 sample) const;
    juce::int64 xToSample (int x) const;
    int trackAtY (int y) const;
    ClipRef clipAt (juce::Point<int> p) const;
    // The one place the R / A / M / S keys are positioned, so painting and hit-testing can
    // never disagree about where they are - short rows put them beside the name, tall rows under it.
    juce::Rectangle<int> keyCell (int track, int key) const;
    bool compactHeader (int track) const;
    int markerAt (juce::Point<int> p) const;
    juce::Rectangle<int> markerFlag (int index) const;
    juce::int64 snapSample (juce::int64 sample, int ignoreTrack, int ignoreClip) const;

    void paintHeader (juce::Graphics&, int track, juce::Rectangle<int>);
    void paintLane (juce::Graphics&, int track, juce::Rectangle<int>);
    void paintRuler (juce::Graphics&);
    void paintMarkers (juce::Graphics&);
    void paintToolbar (juce::Graphics&);

    juce::AudioThumbnail* thumbnailFor (const AudioClip&);
    bool locked();                     // LIVE SAFE: say so once, then change nothing
    void pushUndo();
    void commit();                     // the project changed: republish and save
    void clampScroll();
    void cycleMonitor (int track);
    void markerMenu (int index);
    void updateToolbar();

    MixController& controller;
    AppServices& services;

    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache { 128 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;

    std::vector<Project> undoStack;
    std::vector<float> peaks;          // one meter reading per track, taken once per refresh
    bool undoPushed = false;
    ClipRef selection;
    Drag drag = Drag::None;
    ClipRef dragClip;
    juce::int64 dragAnchorSample = 0, dragClipStart = 0, dragClipOffset = 0, dragClipLength = 0;
    int dragTrack = -1, dragStartHeight = 0, dragStartY = 0, dragStartX = 0;
    int dragMarker = -1;
    juce::int64 loopAnchor = 0;
    double dragStartScrollX = 0.0;

    double pixelsPerSecond = 18.0;
    double scrollX = 0.0;              // pixels
    int scrollY = 0;                   // pixels
    juce::int64 lastPlayhead = -1;
    bool waitingOnThumbnails = false;
    bool snap = true, follow = true;
    RowHeight rowHeight = RowHeight::Medium;
    int hoverMarker = -1;
    int builtForTracks = -1;
    juce::File builtForFolder;

    std::array<std::unique_ptr<DineButton>, 3> rowTabs;      // S / M / L row height
    std::unique_ptr<DineButton> zoomOutButton, zoomFitButton, zoomInButton;
    std::unique_ptr<DineButton> snapButton, followButton, markerButton;
};

} // namespace livemix
