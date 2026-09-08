#include "TracksPage.h"
#include "UI/Widgets.h"
#include <algorithm>

namespace livemix
{

namespace
{
    constexpr int kHeaderWidth = 244;
    constexpr int kToolbarHeight = 42;
    constexpr int kRulerHeight = 24;
    constexpr int kMarkerHeight = 20;
    constexpr int kLoopStrip = 7;          // the top of the ruler: drag here to mark a loop
    constexpr int kMinTrackHeight = 38;
    constexpr int kMaxTrackHeight = 260;
    constexpr int kResizeGrip = 5;
    constexpr int kTrimGrip = 7;
    constexpr int kMaxUndo = 40;
    constexpr int kSnapPixels = 9;

    // The same four group colours the mixer bands with, so one session reads the same way
    // in both workspaces.
    juce::Colour laneColourFor (ChannelRole role)
    {
        switch (mixBusForRole (role))
        {
            case MixBus::Drums:  return Dine::warn;
            case MixBus::Bass:   return Dine::accent;
            case MixBus::Music:  return juce::Colour (0xff8fa2d8);
            case MixBus::Vocals: return Dine::ok;
            default:             return Dine::ink3;
        }
    }

    juce::String clockText (juce::int64 samples, double rate)
    {
        const double seconds = double (samples) / juce::jmax (1.0, rate);
        const int total = int (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2)
               + "." + juce::String (int ((seconds - double (total)) * 10.0));
    }
}

TracksPage::TracksPage (MixController& c, AppServices& s) : controller (c), services (s)
{
    formats.registerBasicFormats();
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::NormalCursor);

    const char* rowNames[3] = { "S", "M", "L" };
    const char* rowTips[3] = { "Short rows: the whole session on one screen.",
                               "Normal rows.",
                               "Tall rows: the waveforms in detail." };
    for (int i = 0; i < 3; ++i)
    {
        rowTabs[size_t (i)] = std::make_unique<DineButton> (rowNames[i], DineButton::Style::Segment);
        rowTabs[size_t (i)]->setFontPx (11.5f);
        rowTabs[size_t (i)]->setPadX (8);
        rowTabs[size_t (i)]->setClickingTogglesState (false);
        rowTabs[size_t (i)]->setTooltip (rowTips[i]);
        rowTabs[size_t (i)]->onClick = [this, i] { setRowHeight (RowHeight (i)); };
        addAndMakeVisible (*rowTabs[size_t (i)]);
    }

    auto make = [this] (std::unique_ptr<DineButton>& b, const juce::String& text, const juce::String& tip,
                        std::function<void()> action)
    {
        b = std::make_unique<DineButton> (text, DineButton::Style::Standard);
        b->setFontPx (11.5f);
        b->setPadX (9);
        b->setClickingTogglesState (false);
        b->setTooltip (tip);
        b->onClick = std::move (action);
        addAndMakeVisible (*b);
    };

    make (zoomOutButton, Glyph::minus(), "Zoom out (Cmd -).", [this] { zoom (1.0 / 1.4); });
    make (zoomFitButton, "Fit", "Fit the whole session on screen (Cmd 0).", [this] { zoomToFit(); });
    make (zoomInButton, "+", "Zoom in (Cmd +).", [this] { zoom (1.4); });
    make (snapButton, "Snap", "Snap clips to the grid, the markers, the playhead and other clips while dragging.",
          [this] { setSnap (! snap); });
    make (followButton, "Follow", "Keep the playhead on screen while it rolls.", [this] { setFollow (! follow); });
    make (markerButton, "Marker", "Drop a marker at the playhead (M). Click a marker to jump to it.",
          [this] { addMarkerAtPlayhead(); });

    updateToolbar();
}

TracksPage::~TracksPage() = default;

// ---------------------------------------------------------------- geometry
int TracksPage::numTracks() const
{
    const auto& session = controller.getSession();
    const auto& project = services.daw().getProject();
    return juce::jmin (int (session.inputs.size()), int (project.tracks.size()));
}

int TracksPage::trackHeight (int track) const
{
    const auto& project = services.daw().getProject();
    if (track < 0 || track >= int (project.tracks.size())) return kMinTrackHeight;
    return juce::jlimit (kMinTrackHeight, kMaxTrackHeight, project.tracks[size_t (track)].height);
}

int TracksPage::totalTrackHeight() const
{
    int total = 0;
    for (int i = 0; i < numTracks(); ++i) total += trackHeight (i);
    return total;
}

int TracksPage::lanesTop() const { return kToolbarHeight + kRulerHeight + kMarkerHeight; }

int TracksPage::trackTop (int track) const
{
    int y = lanesTop() - scrollY;
    for (int i = 0; i < track; ++i) y += trackHeight (i);
    return y;
}

int TracksPage::trackAtY (int y) const
{
    for (int i = 0; i < numTracks(); ++i)
    {
        const int top = trackTop (i);
        if (y >= top && y < top + trackHeight (i)) return i;
    }
    return -1;
}

juce::Rectangle<int> TracksPage::toolbarArea() const
{
    return getLocalBounds().withHeight (kToolbarHeight);
}

juce::Rectangle<int> TracksPage::rulerArea() const
{
    return getLocalBounds().withTrimmedLeft (kHeaderWidth).withTrimmedTop (kToolbarHeight).withHeight (kRulerHeight);
}

juce::Rectangle<int> TracksPage::markerArea() const
{
    return getLocalBounds().withTrimmedLeft (kHeaderWidth)
                           .withTrimmedTop (kToolbarHeight + kRulerHeight).withHeight (kMarkerHeight);
}

juce::Rectangle<int> TracksPage::lanesArea() const
{
    return getLocalBounds().withTrimmedLeft (kHeaderWidth).withTrimmedTop (lanesTop());
}

double TracksPage::samplesPerPixel() const
{
    const double rate = juce::jmax (1.0, services.daw().getProject().sampleRate);
    return rate / juce::jmax (1.0, pixelsPerSecond);
}

double TracksPage::gridSeconds() const
{
    static const double steps[] = { 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
    for (double s : steps) if (s * pixelsPerSecond >= 70.0) return s;
    return steps[std::size (steps) - 1];
}

int TracksPage::sampleToX (juce::int64 sample) const
{
    return kHeaderWidth + int (double (sample) / samplesPerPixel() - scrollX);
}

juce::int64 TracksPage::xToSample (int x) const
{
    return juce::jmax ((juce::int64) 0, juce::int64 ((double (x - kHeaderWidth) + scrollX) * samplesPerPixel()));
}

TracksPage::ClipRef TracksPage::clipAt (juce::Point<int> p) const
{
    if (p.x < kHeaderWidth || p.y < lanesTop()) return {};
    const int track = trackAtY (p.y);
    if (track < 0) return {};
    const auto& clips = services.daw().getProject().tracks[size_t (track)].clips;
    for (int i = int (clips.size()) - 1; i >= 0; --i)
    {
        const int x1 = sampleToX (clips[size_t (i)].start);
        const int x2 = sampleToX (clips[size_t (i)].end());
        if (p.x >= x1 && p.x <= x2) return { track, i };
    }
    return {};
}

bool TracksPage::compactHeader (int track) const { return trackHeight (track) < 58; }

juce::Rectangle<int> TracksPage::keyCell (int track, int key) const
{
    const int top = trackTop (track), h = trackHeight (track);
    const int size = 20, gap = 6, span = size * 4 + gap * 3;
    if (compactHeader (track))
    {
        const int right = kHeaderWidth - 10 - 4 - 10;          // the meter and its gutter
        return { right - span + key * (size + gap), top + (h - size) / 2, size, size };
    }
    return { 11 + key * (size + gap), juce::jmax (top + 2, top + h - size - 6), size, size };
}

juce::Rectangle<int> TracksPage::markerFlag (int index) const
{
    const auto& markers = services.daw().getProject().markers;
    if (index < 0 || index >= int (markers.size())) return {};
    const auto& m = markers[size_t (index)];
    const int x = sampleToX (m.position);
    const int w = juce::jmin (170, Dine::textWidth (Dine::text (10.5f, 600), m.name) + 16);
    return { x, markerArea().getY() + 2, juce::jmax (22, w), kMarkerHeight - 5 };
}

int TracksPage::markerAt (juce::Point<int> p) const
{
    if (! markerArea().contains (p)) return -1;
    const int n = int (services.daw().getProject().markers.size());
    for (int i = n - 1; i >= 0; --i)
        if (markerFlag (i).contains (p)) return i;
    return -1;
}

// Magnetism: the grid, the markers, the playhead, the loop and every other clip edge.
juce::int64 TracksPage::snapSample (juce::int64 sample, int ignoreTrack, int ignoreClip) const
{
    if (! snap) return sample;
    const auto& project = services.daw().getProject();
    const juce::int64 threshold = juce::int64 (kSnapPixels * samplesPerPixel());

    juce::int64 best = sample;
    juce::int64 bestDistance = threshold + 1;
    auto consider = [&] (juce::int64 candidate)
    {
        if (candidate < 0) return;
        const juce::int64 d = std::abs (candidate - sample);
        if (d <= threshold && d < bestDistance) { bestDistance = d; best = candidate; }
    };

    consider (0);
    consider (services.daw().getTransport().getPosition());
    if (project.loopEnd > project.loopStart) { consider (project.loopStart); consider (project.loopEnd); }
    for (const auto& m : project.markers) consider (m.position);
    for (int t = 0; t < int (project.tracks.size()); ++t)
        for (int i = 0; i < int (project.tracks[size_t (t)].clips.size()); ++i)
        {
            if (t == ignoreTrack && i == ignoreClip) continue;
            const auto& c = project.tracks[size_t (t)].clips[size_t (i)];
            consider (c.start);
            consider (c.end());
        }

    const double grid = gridSeconds() * juce::jmax (1.0, project.sampleRate);
    if (grid > 1.0) consider (juce::int64 (std::round (double (sample) / grid) * grid));
    return best;
}

void TracksPage::clampScroll()
{
    const double contentWidth = double (services.daw().getProject().lengthSamples()) / samplesPerPixel();
    const double visible = juce::jmax (1, lanesArea().getWidth());
    scrollX = juce::jlimit (0.0, juce::jmax (0.0, contentWidth + visible * 0.25 - visible), scrollX);
    scrollY = juce::jlimit (0, juce::jmax (0, totalTrackHeight() - lanesArea().getHeight()), scrollY);
}

// ---------------------------------------------------------------- lifecycle
void TracksPage::rebuild()
{
    // Thumbnails are keyed by the file's path and takes never reuse a name, so they only
    // need dropping when the session itself changes - not every time this page is opened.
    const auto folder = services.daw().getProject().folder;
    if (folder != builtForFolder) { thumbnails.clear(); builtForFolder = folder; }
    builtForTracks = numTracks();
    clampScroll();
    updateToolbar();
    resized();
    repaint();
}

void TracksPage::refresh()
{
    if (builtForTracks != numTracks()) rebuild();

    // One meter reading per track per tick: consuming it twice would halve what is shown.
    const int tracks = numTracks();
    if (int (peaks.size()) != tracks) peaks.assign (size_t (tracks), -120.0f);
    if (controller.isPrepared())
        for (int i = 0; i < juce::jmin (tracks, controller.getEngine().getNumStrips()); ++i)
        {
            const float now = controller.getEngine().getStrip (i).getOutputMeter().consumeMaxPeakDb();
            peaks[size_t (i)] = juce::jmax (now, peaks[size_t (i)] - 2.0f);
        }

    const auto& transport = services.daw().getTransport();
    const juce::int64 playhead = transport.getPosition();
    const bool moving = transport.isPlaying();

    bool loading = false;
    for (const auto& t : thumbnails) if (t.second != nullptr && ! t.second->isFullyLoaded()) loading = true;

    if (moving || playhead != lastPlayhead || loading != waitingOnThumbnails || loading)
    {
        lastPlayhead = playhead;
        waitingOnThumbnails = loading;
        // Keep the playhead on screen while it rolls.
        if (moving && follow)
        {
            const int x = sampleToX (playhead);
            const auto lanes = lanesArea();
            if (x > lanes.getRight() - 60 || x < lanes.getX())
                scrollX = juce::jmax (0.0, double (playhead) / samplesPerPixel() - lanes.getWidth() * 0.2);
        }
        repaint();
    }
    else
    {
        repaint (0, lanesTop(), kHeaderWidth, getHeight() - lanesTop());   // meters
    }
}

juce::AudioThumbnail* TracksPage::thumbnailFor (const AudioClip& clip)
{
    const auto file = services.daw().getProject().fileFor (clip);
    if (! file.existsAsFile()) return nullptr;
    const auto key = file.getFullPathName();
    auto found = thumbnails.find (key);
    if (found != thumbnails.end()) return found->second.get();

    auto thumb = std::make_unique<juce::AudioThumbnail> (512, formats, thumbnailCache);
    thumb->setSource (new juce::FileInputSource (file));
    auto* raw = thumb.get();
    thumbnails[key] = std::move (thumb);
    return raw;
}

bool TracksPage::locked()
{
    if (! services.daw().getProject().liveSafe) return false;
    if (onToast) onToast ("LIVE SAFE is on: editing the timeline is locked. Turn it off on the Live page.");
    return true;
}

void TracksPage::pushUndo()
{
    undoStack.push_back (services.daw().getProject());
    if (int (undoStack.size()) > kMaxUndo) undoStack.erase (undoStack.begin());
}

void TracksPage::commit()
{
    services.daw().refresh();
    services.saveSession();
    repaint();
}

void TracksPage::undo()
{
    if (locked() || undoStack.empty()) return;
    services.daw().setProject (undoStack.back());
    undoStack.pop_back();
    selection = {};
    commit();
    if (onToast) onToast ("Undone.");
}

// ---------------------------------------------------------------- editing
void TracksPage::splitAtPlayhead()
{
    if (locked()) return;
    auto& project = services.daw().getProject();
    const juce::int64 at = services.daw().getTransport().getPosition();
    int split = 0;
    for (int t = 0; t < numTracks(); ++t)
    {
        auto& clips = project.tracks[size_t (t)].clips;
        for (int i = int (clips.size()) - 1; i >= 0; --i)
        {
            auto& clip = clips[size_t (i)];
            if (! clip.covers (at) || at == clip.start) continue;
            if (split == 0) pushUndo();
            AudioClip right = clip;
            const juce::int64 cut = at - clip.start;
            right.start = at;
            right.offset = clip.offset + cut;
            right.length = clip.length - cut;
            clip.length = cut;
            clips.insert (clips.begin() + i + 1, right);
            ++split;
        }
    }
    if (split == 0) { if (onToast) onToast ("Put the playhead over a clip to split it."); return; }
    selection = {};
    commit();
    if (onToast) onToast (split == 1 ? "Split 1 clip." : "Split " + juce::String (split) + " clips.");
}

void TracksPage::deleteSelection()
{
    if (locked()) return;
    if (! selection.valid()) { if (onToast) onToast ("Select a clip first."); return; }
    auto& project = services.daw().getProject();
    if (selection.track >= int (project.tracks.size())) return;
    auto& clips = project.tracks[size_t (selection.track)].clips;
    if (selection.index >= int (clips.size())) return;
    pushUndo();
    clips.erase (clips.begin() + selection.index);
    selection = {};
    commit();
    if (onToast) onToast ("Clip deleted. The recorded file is still on disk.");
}

void TracksPage::zoom (double factor)
{
    const auto lanes = lanesArea();
    const juce::int64 centre = xToSample (lanes.getCentreX());
    pixelsPerSecond = juce::jlimit (0.2, 800.0, pixelsPerSecond * factor);
    scrollX = juce::jmax (0.0, double (centre) / samplesPerPixel() - lanes.getWidth() * 0.5);
    clampScroll();
    repaint();
}

void TracksPage::zoomToFit()
{
    const auto& project = services.daw().getProject();
    const double seconds = double (project.lengthSamples()) / juce::jmax (1.0, project.sampleRate);
    const int width = juce::jmax (200, lanesArea().getWidth() - 24);
    pixelsPerSecond = seconds > 0.5 ? juce::jlimit (0.2, 800.0, width / seconds) : 18.0;
    scrollX = 0.0;
    repaint();
}

void TracksPage::setRowHeight (RowHeight h)
{
    rowHeight = h;
    const int px = h == RowHeight::Small ? 42 : h == RowHeight::Large ? 112 : 66;
    auto& project = services.daw().getProject();
    for (auto& t : project.tracks) t.height = px;
    clampScroll();
    updateToolbar();
    services.saveSession();
    repaint();
}

void TracksPage::setSnap (bool on)
{
    snap = on;
    updateToolbar();
    if (onToast) onToast (on ? "Snap on: clips line up with the grid, the markers and each other."
                             : "Snap off: clips move freely.");
}

void TracksPage::setFollow (bool on)
{
    follow = on;
    updateToolbar();
}

void TracksPage::addMarkerAtPlayhead()
{
    if (locked()) return;
    auto& project = services.daw().getProject();
    const juce::int64 at = services.daw().getTransport().getPosition();
    for (const auto& m : project.markers)
        if (std::abs (m.position - at) < juce::int64 (samplesPerPixel() * 2.0))
        {
            if (onToast) onToast ("There is already a marker here.");
            return;
        }
    pushUndo();
    project.markers.push_back ({ "Marker " + juce::String (int (project.markers.size()) + 1), at });
    std::stable_sort (project.markers.begin(), project.markers.end(),
                      [] (const Marker& a, const Marker& b) { return a.position < b.position; });
    services.saveSession();
    if (onTimelineChanged) onTimelineChanged();
    repaint();
    if (onToast) onToast ("Marker at " + clockText (at, project.sampleRate) + ".");
}

void TracksPage::markerMenu (int index)
{
    auto& project = services.daw().getProject();
    if (index < 0 || index >= int (project.markers.size())) return;

    juce::PopupMenu m;
    m.addSectionHeader (project.markers[size_t (index)].name);
    m.addItem (1, "Go to this marker");
    m.addItem (2, "Rename" + Glyph::ellip());
    m.addItem (3, "Delete");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                         .withTargetScreenArea (localAreaToGlobal (markerFlag (index))),
                     [this, index] (int result)
                     {
                         auto& p = services.daw().getProject();
                         if (result <= 0 || index >= int (p.markers.size())) return;
                         if (result == 1) { services.daw().locate (p.markers[size_t (index)].position); repaint(); return; }
                         if (locked()) return;
                         if (result == 3)
                         {
                             pushUndo();
                             p.markers.erase (p.markers.begin() + index);
                             services.saveSession();
                             if (onTimelineChanged) onTimelineChanged();
                             repaint();
                             return;
                         }

                         auto* alert = new juce::AlertWindow ("Rename marker", "What is this moment called?",
                                                              juce::MessageBoxIconType::NoIcon);
                         alert->addTextEditor ("name", p.markers[size_t (index)].name, "Name");
                         alert->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
                         alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                         alert->enterModalState (true, juce::ModalCallbackFunction::create (
                             [this, alert, index] (int r)
                             {
                                 std::unique_ptr<juce::AlertWindow> closer (alert);
                                 auto& proj = services.daw().getProject();
                                 if (r != 1 || index >= int (proj.markers.size())) return;
                                 const auto text = alert->getTextEditorContents ("name").trim();
                                 if (text.isEmpty()) return;
                                 pushUndo();
                                 proj.markers[size_t (index)].name = text;
                                 services.saveSession();
                                 if (onTimelineChanged) onTimelineChanged();
                                 repaint();
                             }), true);
                     });
}

void TracksPage::cycleMonitor (int track)
{
    auto& project = services.daw().getProject();
    if (track < 0 || track >= int (project.tracks.size())) return;
    auto& mode = project.tracks[size_t (track)].monitor;
    mode = MonitorMode ((int (mode) + 1) % int (MonitorMode::Count));
    services.daw().refresh();
    services.saveSession();
    repaint();
}

// ---------------------------------------------------------------- painting
void TracksPage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);

    const int tracks = numTracks();
    const auto lanes = lanesArea();
    const auto& project = services.daw().getProject();

    // ---- lanes
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (lanes);
        g.setColour (Dine::desk);
        g.fillRect (lanes);

        if (project.loopEnabled && project.loopEnd > project.loopStart)
        {
            const int x1 = sampleToX (project.loopStart), x2 = sampleToX (project.loopEnd);
            g.setColour (Dine::accent.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<int> (x1, lanes.getY(), juce::jmax (1, x2 - x1), lanes.getHeight()));
        }

        // The ruler's grid, carried down through the lanes so an edit has something to read against.
        const double grid = gridSeconds();
        const double rate = juce::jmax (1.0, project.sampleRate);
        const double first = std::floor ((scrollX / pixelsPerSecond) / grid) * grid;
        for (double s = first; ; s += grid)
        {
            const int x = sampleToX (juce::int64 (s * rate));
            if (x > lanes.getRight()) break;
            if (x < lanes.getX()) continue;
            g.setColour (juce::Colours::white.withAlpha (0.045f));
            g.fillRect (float (x), float (lanes.getY()), 0.5f, float (lanes.getHeight()));
        }

        for (int i = 0; i < tracks; ++i)
        {
            const int top = trackTop (i);
            const int height = trackHeight (i);
            if (top + height < lanes.getY() || top > lanes.getBottom()) continue;
            paintLane (g, i, { lanes.getX(), top, lanes.getWidth(), height });
        }

        // Marker lines, so a moment can be found on every track at once.
        for (const auto& m : project.markers)
        {
            const int x = sampleToX (m.position);
            if (x < lanes.getX() || x > lanes.getRight()) continue;
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.fillRect (float (x), float (lanes.getY()), 0.5f, float (lanes.getHeight()));
        }

        if (tracks > 0 && ! project.hasAudio())
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (13.0f));
            g.drawFittedText ("Nothing recorded yet. Arm the tracks you want and press Record, "
                              "or import a folder of stems from the File menu.",
                              lanes.reduced (40, 0).withHeight (46).withY (lanes.getY() + 30),
                              juce::Justification::centredTop, 2);
        }

        // playhead
        const int px = sampleToX (services.daw().getTransport().getPosition());
        if (px >= lanes.getX() - 1 && px <= lanes.getRight() + 1)
        {
            g.setColour (services.daw().isRecording() ? Dine::crit : Dine::accent);
            g.fillRect (float (px), float (lanes.getY()), 1.5f, float (lanes.getHeight()));
        }
    }

    // ---- headers
    {
        juce::Graphics::ScopedSaveState save (g);
        auto headers = getLocalBounds().withWidth (kHeaderWidth).withTrimmedTop (lanesTop());
        g.reduceClipRegion (headers);
        g.setColour (Dine::window);
        g.fillRect (headers);
        for (int i = 0; i < tracks; ++i)
        {
            const int top = trackTop (i);
            const int height = trackHeight (i);
            if (top + height < headers.getY() || top > headers.getBottom()) continue;
            paintHeader (g, i, { 0, top, kHeaderWidth, height });
        }
        if (tracks == 0)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.5f));
            g.drawText ("No tracks yet", headers.reduced (16, 20), juce::Justification::topLeft);
        }
    }

    paintRuler (g);
    paintMarkers (g);
    paintToolbar (g);

    g.setColour (Dine::hair);
    g.fillRect (float (kHeaderWidth) - 0.5f, float (kToolbarHeight), 0.5f, float (getHeight() - kToolbarHeight));

    // Scroll indicators: how much of the session is on screen, in both directions.
    const int content = totalTrackHeight();
    if (content > lanes.getHeight() && lanes.getHeight() > 0)
    {
        const float span = float (lanes.getHeight()) * float (lanes.getHeight()) / float (content);
        const float at = float (lanes.getHeight()) * float (scrollY) / float (content);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRoundedRectangle (float (getWidth()) - 6.0f, float (lanes.getY()) + at, 3.0f, juce::jmax (24.0f, span), 1.5f);
    }
    const double timelineWidth = double (juce::jmax (project.lengthSamples(),
                                                    juce::int64 (samplesPerPixel() * lanes.getWidth()))) / samplesPerPixel();
    if (timelineWidth > lanes.getWidth() && lanes.getWidth() > 0)
    {
        const float span = float (lanes.getWidth()) * float (lanes.getWidth() / timelineWidth);
        const float at = float (lanes.getWidth()) * float (scrollX / timelineWidth);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRoundedRectangle (float (lanes.getX()) + at, float (getHeight()) - 6.0f, juce::jmax (24.0f, span), 3.0f, 1.5f);
    }
}

void TracksPage::paintToolbar (juce::Graphics& g)
{
    auto area = toolbarArea();
    g.setColour (Dine::window);
    g.fillRect (area);
    Dine::drawRule (g, area.removeFromBottom (1), Dine::hair);

    const auto& project = services.daw().getProject();
    const int tracks = numTracks();
    const int armed = project.numArmed();

    juce::String meta = juce::String (tracks) + (tracks == 1 ? " track" : " tracks");
    if (project.hasAudio()) meta += "   " + Glyph::dot() + "   " + clockText (project.lengthSamples(), project.sampleRate);
    if (armed > 0) meta += "   " + Glyph::dot() + "   " + juce::String (armed) + " armed";
    if (project.liveSafe) meta += "   " + Glyph::dot() + "   LIVE SAFE";

    auto text = juce::Rectangle<int> (16, 0, kHeaderWidth + 220, kToolbarHeight);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (13.0f, 600));
    const int titleW = Dine::textWidth (Dine::text (13.0f, 600), "Timeline");
    g.drawText ("Timeline", text.removeFromLeft (titleW), juce::Justification::centredLeft);
    text.removeFromLeft (12);
    g.setColour (project.liveSafe ? Dine::warn : Dine::ink3);
    g.setFont (Dine::text (11.5f));
    g.drawText (meta, text, juce::Justification::centredLeft, true);

    // the segmented row-height control sits on its own quiet track
    if (rowTabs[0] != nullptr && rowTabs[0]->isVisible())
    {
        auto r = rowTabs[0]->getBounds().getUnion (rowTabs[2]->getBounds());
        Dine::fillRounded (g, r.expanded (2, 2).toFloat(), juce::Colours::white.withAlpha (0.07f), 7.0f);
    }
}

void TracksPage::paintRuler (juce::Graphics& g)
{
    auto area = rulerArea();
    auto all = getLocalBounds().withTrimmedTop (kToolbarHeight).withHeight (kRulerHeight);
    g.setColour (Dine::toolbar);
    g.fillRect (all);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);

    const auto& project = services.daw().getProject();
    const double rate = juce::jmax (1.0, project.sampleRate);
    const double step = gridSeconds();

    // The loop strip along the top of the ruler: drag here to mark the range.
    auto strip = area.withHeight (kLoopStrip);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (strip);
    if (project.loopEnd > project.loopStart)
    {
        const int x1 = sampleToX (project.loopStart), x2 = sampleToX (project.loopEnd);
        const auto tint = project.loopEnabled ? Dine::accent : Dine::ink4;
        g.setColour (tint.withAlpha (project.loopEnabled ? 0.9f : 0.5f));
        g.fillRoundedRectangle (float (x1), float (strip.getY()) + 1.0f,
                                float (juce::jmax (3, x2 - x1)), float (kLoopStrip) - 2.0f, 1.5f);
    }

    const double firstSecond = std::floor ((scrollX / pixelsPerSecond) / step) * step;
    g.setFont (Dine::mono (10.0f));
    for (double s = firstSecond; ; s += step)
    {
        const int x = sampleToX (juce::int64 (s * rate));
        if (x > area.getRight()) break;
        if (x < kHeaderWidth - 40) continue;
        g.setColour (Dine::hairStrong);
        g.fillRect (float (x), float (area.getBottom() - 6), 0.5f, 6.0f);
        // the half-way tick, unlabelled
        const int hx = sampleToX (juce::int64 ((s + step * 0.5) * rate));
        g.setColour (Dine::hair);
        g.fillRect (float (hx), float (area.getBottom() - 3), 0.5f, 3.0f);

        g.setColour (Dine::ink3);
        const int total = int (s);
        juce::String label = juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
        if (step < 1.0) label += "." + juce::String (int ((s - double (total)) * 10.0 + 0.5));
        g.drawText (label, x + 4, area.getY() + kLoopStrip - 2, 60, area.getHeight() - kLoopStrip,
                    juce::Justification::centredLeft, false);
    }

    g.setColour (Dine::hair);
    g.fillRect (float (all.getX()), float (all.getBottom()) - 0.5f, float (all.getWidth()), 0.5f);
}

void TracksPage::paintMarkers (juce::Graphics& g)
{
    auto all = getLocalBounds().withTrimmedTop (kToolbarHeight + kRulerHeight).withHeight (kMarkerHeight);
    g.setColour (juce::Colour (0xff191a1d));
    g.fillRect (all);
    g.setColour (Dine::hair);
    g.fillRect (float (all.getX()), float (all.getBottom()) - 0.5f, float (all.getWidth()), 0.5f);

    // the lane's own label, in the header column
    g.setColour (Dine::ink4);
    g.setFont (Dine::text (10.0f, 600));
    g.drawText ("MARKERS", juce::Rectangle<int> (14, all.getY(), kHeaderWidth - 28, all.getHeight()),
                juce::Justification::centredLeft);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (markerArea());

    const auto& markers = services.daw().getProject().markers;
    for (int i = 0; i < int (markers.size()); ++i)
    {
        auto flag = markerFlag (i);
        if (flag.getRight() < markerArea().getX() || flag.getX() > markerArea().getRight()) continue;
        const bool hot = hoverMarker == i;
        Dine::fillRounded (g, flag.toFloat(), hot ? Dine::accent : juce::Colours::white.withAlpha (0.14f), 3.0f);
        g.setColour (hot ? Dine::onAccent : Dine::ink2);
        g.setFont (Dine::text (10.5f, 600));
        g.drawText (markers[size_t (i)].name, flag.reduced (7, 0), juce::Justification::centredLeft, true);
        g.setColour (hot ? Dine::accent : juce::Colours::white.withAlpha (0.35f));
        g.fillRect (float (flag.getX()), float (flag.getY()), 1.5f, float (flag.getHeight()));
    }
}

void TracksPage::paintHeader (juce::Graphics& g, int track, juce::Rectangle<int> area)
{
    const auto& session = controller.getSession();
    const auto& project = services.daw().getProject();
    const auto& input = session.inputs[size_t (track)];
    const auto& state = project.tracks[size_t (track)];
    const bool selected = selection.track == track;
    const auto tint = laneColourFor (input.role);

    g.setColour (selected ? juce::Colours::white.withAlpha (0.05f) : Dine::window);
    g.fillRect (area);
    g.setColour (Dine::hairSoft);
    g.fillRect (float (area.getX()), float (area.getBottom()) - 0.5f, float (area.getWidth()), 0.5f);

    // Group colour spine, red while the track is armed.
    g.setColour (state.armed ? Dine::crit : tint);
    g.fillRect (area.getX(), area.getY(), 3, area.getHeight());

    auto r = area.reduced (0, 5).withTrimmedLeft (11).withTrimmedRight (10);

    // Meter down the right edge of the header.
    auto meter = r.removeFromRight (4);
    if (track < int (peaks.size()))
    {
        const float db = peaks[size_t (track)];
        const float norm = DineMeter::norm (db);
        Dine::drawWell (g, meter.toFloat(), 2.0f);
        auto lit = meter.toFloat().withTrimmedTop (meter.getHeight() * (1.0f - norm));
        g.setColour (Dine::levelColour (db));
        g.fillRoundedRectangle (lit, 2.0f);
    }
    r.removeFromRight (8);

    const bool compact = compactHeader (track);
    {
        // the track's number, then its name
        auto top = compact ? area.withTrimmedLeft (11).withSizeKeepingCentre (kHeaderWidth - 11, 17)
                                 .withRight (keyCell (track, 0).getX() - 8)
                           : r.removeFromTop (juce::jmin (17, r.getHeight()));
        auto numberCell = top.removeFromLeft (20);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
        g.drawText (juce::String (track + 1), numberCell, juce::Justification::centredLeft);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.5f, 600));
        g.drawText (juce::String (input.name), top, juce::Justification::centredLeft, true);
    }

    if (! compact && area.getHeight() >= 62)
    {
        auto sub = r.removeFromTop (14).withTrimmedLeft (20);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (10.5f));
        juce::String meta = juce::String (channelRoleName (input.role));
        meta += "   " + juce::String (input.isStereo() ? "In " + juce::String (input.inputA + 1) + "-" + juce::String (input.inputB + 1)
                                                       : "In " + juce::String (input.inputA + 1));
        g.drawText (meta, sub, juce::Justification::centredLeft, true);
    }

    // R / A / M / S: under the name on a tall row, beside it on a short one.
    auto key = [&] (juce::Rectangle<int> cell, const juce::String& text, bool on, juce::Colour colour)
    {
        Dine::fillRounded (g, cell.toFloat(), on ? colour : Dine::fill, Dine::Radius::chip);
        if (! on) Dine::hairlineRounded (g, cell.toFloat(), Dine::hair, Dine::Radius::chip);
        g.setColour (on ? Dine::onAccent : Dine::ink2);
        g.setFont (Dine::text (10.0f, 700));
        g.drawText (text, cell, juce::Justification::centred);
    };

    const auto& params = controller.getBase();
    const bool mute = track < params.numStrips && params.strips[size_t (track)].mute;
    const bool solo = track < params.numStrips && params.strips[size_t (track)].solo;

    key (keyCell (track, 0), "R", state.armed, Dine::crit);
    key (keyCell (track, 1), state.monitor == MonitorMode::Off ? Glyph::dash() : (state.monitor == MonitorMode::Input ? "I" : "A"),
         state.monitor != MonitorMode::Off, state.monitor == MonitorMode::Input ? Dine::accent : Dine::accentDeep);
    key (keyCell (track, 2), "M", mute, Dine::crit);
    key (keyCell (track, 3), "S", solo, Dine::warn);

    // the row-height grip, so the affordance is seen rather than discovered
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillRect (float (area.getRight() - 36), float (area.getBottom()) - 2.5f, 22.0f, 1.0f);
}

void TracksPage::paintLane (juce::Graphics& g, int track, juce::Rectangle<int> area)
{
    const auto& session = controller.getSession();
    const auto& project = services.daw().getProject();
    const auto& state = project.tracks[size_t (track)];
    const auto colour = laneColourFor (session.inputs[size_t (track)].role);

    g.setColour (track % 2 == 0 ? Dine::desk : juce::Colour (0xff131417));
    g.fillRect (area);
    if (selection.track == track)
    {
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        g.fillRect (area);
    }
    g.setColour (Dine::hairSoft);
    g.fillRect (float (area.getX()), float (area.getBottom()) - 0.5f, float (area.getWidth()), 0.5f);

    for (int i = 0; i < int (state.clips.size()); ++i)
    {
        const auto& clip = state.clips[size_t (i)];
        const int x1 = sampleToX (clip.start);
        const int x2 = sampleToX (clip.end());
        if (x2 < area.getX() - 2 || x1 > area.getRight() + 2) continue;

        auto box = juce::Rectangle<int> (x1, area.getY() + 3, juce::jmax (2, x2 - x1), area.getHeight() - 7);
        const bool selected = selection.track == track && selection.index == i;
        const bool named = box.getHeight() > 30 && box.getWidth() > 46;

        Dine::fillRounded (g, box.toFloat(), colour.withMultipliedSaturation (0.85f).withAlpha (selected ? 0.24f : 0.13f),
                           Dine::Radius::chip);
        if (named)
        {
            // A title bar the width of the clip: the name never sits on top of the waveform.
            juce::Path head;
            head.addRoundedRectangle (float (box.getX()), float (box.getY()), float (box.getWidth()), 15.0f,
                                      Dine::Radius::chip, Dine::Radius::chip, true, true, false, false);
            g.setColour (colour.withAlpha (selected ? 0.42f : 0.28f));
            g.fillPath (head);
        }
        Dine::hairlineRounded (g, box.toFloat(), selected ? colour : colour.withAlpha (0.5f), Dine::Radius::chip);

        auto wave = box.reduced (2, 3).withTrimmedTop (named ? 13 : 0);
        if (auto* thumb = thumbnailFor (clip); thumb != nullptr && wave.getHeight() > 4)
        {
            const double rate = juce::jmax (1.0, clip.fileSampleRate > 0.0 ? clip.fileSampleRate : project.sampleRate);
            const double from = double (clip.offset) / rate;
            const double to = from + double (clip.length) / juce::jmax (1.0, project.sampleRate);
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (wave);
            g.setColour (colour.brighter (0.45f));
            thumb->drawChannels (g, wave, from, to, 0.95f);
        }

        if (named)
        {
            g.setColour (Dine::ink.withAlpha (0.9f));
            g.setFont (Dine::text (10.5f, 600));
            g.drawText (clip.name.isEmpty() ? juce::String (session.inputs[size_t (track)].name) : clip.name,
                        box.reduced (6, 1).withHeight (13), juce::Justification::centredLeft, true);
        }

        if (selected)
        {
            // trim handles, so the edges say they can be dragged
            g.setColour (colour);
            g.fillRect (box.getX() + 1, box.getY() + 3, 2, box.getHeight() - 6);
            g.fillRect (box.getRight() - 3, box.getY() + 3, 2, box.getHeight() - 6);
        }
    }

    if (state.clips.empty() && state.armed)
    {
        g.setColour (Dine::crit.withAlpha (0.35f));
        g.setFont (Dine::text (10.5f, 600));
        g.drawText ("ARMED", area.reduced (10, 0).withWidth (80), juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------- layout
void TracksPage::updateToolbar()
{
    for (int i = 0; i < 3; ++i)
        rowTabs[size_t (i)]->setToggleState (int (rowHeight) == i, juce::dontSendNotification);
    snapButton->setStyle (snap ? DineButton::Style::Filled : DineButton::Style::Standard);
    followButton->setStyle (follow ? DineButton::Style::Filled : DineButton::Style::Standard);
    repaint (toolbarArea());
}

void TracksPage::resized()
{
    auto row = toolbarArea().reduced (16, 0).withSizeKeepingCentre (juce::jmax (100, getWidth() - 32),
                                                                   Dine::Metric::control);

    auto place = [&row] (DineButton& b, int minWidth)
    {
        const int w = juce::jmax (minWidth, b.idealWidth() + 6);
        b.setBounds (row.removeFromRight (w));
        row.removeFromRight (8);
    };

    place (*markerButton, 74);
    row.removeFromRight (4);
    place (*followButton, 66);
    place (*snapButton, 58);
    row.removeFromRight (4);
    place (*zoomInButton, 34);
    place (*zoomFitButton, 44);
    place (*zoomOutButton, 34);
    row.removeFromRight (4);
    {
        auto seg = row.removeFromRight (90);
        for (int i = 0; i < 3; ++i) rowTabs[size_t (i)]->setBounds (seg.removeFromLeft (30));
    }

    clampScroll();
}

// ---------------------------------------------------------------- mouse
void TracksPage::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (p.y < kToolbarHeight) return;

    // ---- the marker lane
    if (p.y >= kToolbarHeight + kRulerHeight && p.y < lanesTop())
    {
        if (p.x < kHeaderWidth) return;
        const int hit = markerAt (p);
        if (hit >= 0)
        {
            if (e.mods.isPopupMenu()) { markerMenu (hit); return; }
            services.daw().locate (services.daw().getProject().markers[size_t (hit)].position);
            drag = Drag::Marker;
            dragMarker = hit;
            undoPushed = false;
            repaint();
            return;
        }
        drag = Drag::Playhead;
        services.daw().getTransport().setPosition (snapSample (xToSample (p.x), -1, -1));
        repaint();
        return;
    }

    // ---- the ruler
    if (p.y < kToolbarHeight + kRulerHeight)
    {
        if (p.x < kHeaderWidth) return;
        auto& project = services.daw().getProject();
        if (p.y < kToolbarHeight + kLoopStrip)
        {
            if (locked()) return;
            drag = Drag::LoopRange;
            loopAnchor = snapSample (xToSample (p.x), -1, -1);
            project.loopStart = loopAnchor;
            project.loopEnd = loopAnchor;
            repaint();
            return;
        }
        drag = Drag::Playhead;
        services.daw().getTransport().setPosition (snapSample (xToSample (p.x), -1, -1));
        repaint();
        return;
    }

    if (p.x < kHeaderWidth)
    {
        const int track = trackAtY (p.y);
        if (track < 0) return;
        const int bottom = trackTop (track) + trackHeight (track);
        if (p.y >= bottom - kResizeGrip)
        {
            drag = Drag::TrackHeight;
            dragTrack = track;
            dragStartY = p.y;
            dragStartHeight = trackHeight (track);
            return;
        }

        // R / A / M / S
        {
            auto& project = services.daw().getProject();
            for (int k = 0; k < 4; ++k)
            {
                if (! keyCell (track, k).contains (p)) continue;
                if (k == 0)
                {
                    if (locked()) return;
                    project.tracks[size_t (track)].armed = ! project.tracks[size_t (track)].armed;
                    services.daw().refresh();
                    services.saveSession();
                }
                else if (k == 1) cycleMonitor (track);
                else if (k == 2) controller.setStripMute (track, ! controller.getBase().strips[size_t (track)].mute);
                else             controller.setStripSolo (track, ! controller.getBase().strips[size_t (track)].solo);
                repaint();
                return;
            }
        }

        selection = { track, -1 };
        if (e.getNumberOfClicks() >= 2 && onOpenStrip) onOpenStrip (track);
        repaint();
        return;
    }

    // ---- lanes
    const auto hit = clipAt (p);
    selection = hit;
    if (! hit.valid())
    {
        const int track = trackAtY (p.y);
        if (track >= 0) selection = { track, -1 };
        drag = Drag::Scroll;
        dragStartX = p.x;
        dragStartScrollX = scrollX;
        repaint();
        return;
    }

    if (services.daw().getProject().liveSafe) { repaint(); return; }   // selecting is fine; moving is not

    const auto& clip = services.daw().getProject().tracks[size_t (hit.track)].clips[size_t (hit.index)];
    const int x1 = sampleToX (clip.start), x2 = sampleToX (clip.end());
    dragClip = hit;
    dragClipStart = clip.start;
    dragClipOffset = clip.offset;
    dragClipLength = clip.length;
    dragAnchorSample = xToSample (p.x);
    if (p.x - x1 <= kTrimGrip && x2 - x1 > kTrimGrip * 3)      drag = Drag::ClipTrimStart;
    else if (x2 - p.x <= kTrimGrip && x2 - x1 > kTrimGrip * 3) drag = Drag::ClipTrimEnd;
    else                                                        drag = Drag::ClipMove;
    undoPushed = false;                 // a plain click should not fill the undo stack
    repaint();
}

void TracksPage::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    auto& project = services.daw().getProject();

    switch (drag)
    {
        case Drag::Playhead:
            // Move the picture while dragging; the player is only re-primed once, on release.
            services.daw().getTransport().setPosition (snapSample (xToSample (p.x), -1, -1));
            repaint();
            break;

        case Drag::LoopRange:
        {
            const juce::int64 at = snapSample (xToSample (p.x), -1, -1);
            project.loopStart = juce::jmin (loopAnchor, at);
            project.loopEnd = juce::jmax (loopAnchor, at);
            repaint();
            break;
        }

        case Drag::Marker:
        {
            if (dragMarker < 0 || dragMarker >= int (project.markers.size())) break;
            if (project.liveSafe) { drag = Drag::None; break; }
            if (! undoPushed) { pushUndo(); undoPushed = true; }
            project.markers[size_t (dragMarker)].position = snapSample (xToSample (p.x), -1, -1);
            repaint();
            break;
        }

        case Drag::Scroll:
            scrollX = juce::jmax (0.0, dragStartScrollX - (p.x - dragStartX));
            clampScroll();
            repaint();
            break;

        case Drag::TrackHeight:
            if (dragTrack >= 0 && dragTrack < int (project.tracks.size()))
            {
                project.tracks[size_t (dragTrack)].height = juce::jlimit (kMinTrackHeight, kMaxTrackHeight,
                                                                          dragStartHeight + (p.y - dragStartY));
                repaint();
            }
            break;

        case Drag::ClipMove:
        case Drag::ClipTrimStart:
        case Drag::ClipTrimEnd:
        {
            if (! dragClip.valid() || dragClip.track >= int (project.tracks.size())) break;
            auto& clips = project.tracks[size_t (dragClip.track)].clips;
            if (dragClip.index >= int (clips.size())) break;
            if (! undoPushed) { pushUndo(); undoPushed = true; }
            auto& clip = clips[size_t (dragClip.index)];
            const juce::int64 raw = xToSample (p.x) - dragAnchorSample;

            if (drag == Drag::ClipMove)
            {
                // Snap whichever edge lands closest to something, then move the clip as a whole.
                const juce::int64 wanted = juce::jmax ((juce::int64) 0, dragClipStart + raw);
                const juce::int64 fromStart = snapSample (wanted, dragClip.track, dragClip.index) - wanted;
                const juce::int64 endWanted = wanted + dragClipLength;
                const juce::int64 fromEnd = snapSample (endWanted, dragClip.track, dragClip.index) - endWanted;
                const juce::int64 shift = std::abs (fromStart) <= std::abs (fromEnd) ? fromStart : fromEnd;
                clip.start = juce::jmax ((juce::int64) 0, wanted + shift);
            }
            else if (drag == Drag::ClipTrimStart)
            {
                // `offset` counts file samples, `delta` counts project samples.
                const juce::int64 snapped = snapSample (dragClipStart + raw, dragClip.track, dragClip.index);
                const juce::int64 delta = snapped - dragClipStart;
                const double ratio = (clip.fileSampleRate > 0.0 && project.sampleRate > 0.0)
                                         ? clip.fileSampleRate / project.sampleRate : 1.0;
                const juce::int64 headroom = juce::int64 (double (dragClipOffset) / juce::jmax (1.0e-6, ratio));
                const juce::int64 move = juce::jlimit (-headroom, dragClipLength - 1, delta);
                clip.start = juce::jmax ((juce::int64) 0, dragClipStart + move);
                clip.offset = dragClipOffset + juce::int64 (double (move) * ratio);
                clip.length = dragClipLength - move;
            }
            else
            {
                const juce::int64 snapped = snapSample (dragClipStart + dragClipLength + raw, dragClip.track, dragClip.index);
                clip.length = juce::jmax ((juce::int64) 1, snapped - clip.start);
            }
            repaint();
            break;
        }

        default: break;
    }
}

void TracksPage::mouseUp (const juce::MouseEvent&)
{
    auto& project = services.daw().getProject();

    if (drag == Drag::Playhead || drag == Drag::Marker)
        services.daw().locate (services.daw().getTransport().getPosition());

    if (drag == Drag::LoopRange)
    {
        if (project.loopEnd - project.loopStart < juce::int64 (samplesPerPixel() * 4.0))
        {
            services.daw().setLoop (false, 0, 0);
            if (onToast) onToast ("Loop cleared.");
        }
        else
        {
            services.daw().setLoop (true, project.loopStart, project.loopEnd);
            if (onToast) onToast ("Loop " + clockText (project.loopStart, project.sampleRate) + " to "
                                  + clockText (project.loopEnd, project.sampleRate) + ".");
        }
        services.saveSession();
        if (onTimelineChanged) onTimelineChanged();
    }

    if (drag == Drag::Marker && undoPushed)
    {
        std::stable_sort (project.markers.begin(), project.markers.end(),
                          [] (const Marker& a, const Marker& b) { return a.position < b.position; });
        services.saveSession();
        if (onTimelineChanged) onTimelineChanged();
    }

    const bool movedClip = undoPushed && (drag == Drag::ClipMove || drag == Drag::ClipTrimStart || drag == Drag::ClipTrimEnd);
    if (movedClip && dragClip.valid() && dragClip.track < int (project.tracks.size()))
    {
        auto& clips = project.tracks[size_t (dragClip.track)].clips;
        std::stable_sort (clips.begin(), clips.end(), [] (const AudioClip& a, const AudioClip& b) { return a.start < b.start; });
        selection = {};
    }
    if (movedClip || drag == Drag::TrackHeight) commit();
    drag = Drag::None;
    dragClip = {};
    dragMarker = -1;
    undoPushed = false;
}

void TracksPage::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (p.x < kHeaderWidth || p.y < kToolbarHeight + kRulerHeight || p.y >= lanesTop()) return;
    if (markerAt (p) >= 0) return;                    // a double-click on a flag is two jumps
    if (locked()) return;

    auto& project = services.daw().getProject();
    pushUndo();
    project.markers.push_back ({ "Marker " + juce::String (int (project.markers.size()) + 1),
                                snapSample (xToSample (p.x), -1, -1) });
    std::stable_sort (project.markers.begin(), project.markers.end(),
                      [] (const Marker& a, const Marker& b) { return a.position < b.position; });
    services.saveSession();
    if (onTimelineChanged) onTimelineChanged();
    repaint();
}

void TracksPage::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    juce::MouseCursor cursor = juce::MouseCursor::NormalCursor;

    const int wasHover = hoverMarker;
    hoverMarker = markerAt (p);
    if (hoverMarker != wasHover) repaint (markerArea());

    if (p.y >= kToolbarHeight && p.y < kToolbarHeight + kLoopStrip && p.x >= kHeaderWidth)
        cursor = juce::MouseCursor::LeftRightResizeCursor;
    else if (hoverMarker >= 0)
        cursor = juce::MouseCursor::PointingHandCursor;
    else if (p.x < kHeaderWidth && p.y >= lanesTop())
    {
        const int track = trackAtY (p.y);
        if (track >= 0 && p.y >= trackTop (track) + trackHeight (track) - kResizeGrip)
            cursor = juce::MouseCursor::UpDownResizeCursor;
    }
    else if (p.y >= lanesTop())
    {
        if (const auto hit = clipAt (p); hit.valid())
        {
            const auto& clip = services.daw().getProject().tracks[size_t (hit.track)].clips[size_t (hit.index)];
            const int x1 = sampleToX (clip.start), x2 = sampleToX (clip.end());
            if ((p.x - x1 <= kTrimGrip || x2 - p.x <= kTrimGrip) && x2 - x1 > kTrimGrip * 3)
                cursor = juce::MouseCursor::LeftRightResizeCursor;
        }
    }
    setMouseCursor (cursor);
}

void TracksPage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isAltDown())
    {
        zoom (wheel.deltaY > 0 ? 1.15 : 1.0 / 1.15);
        return;
    }
    scrollY = juce::jmax (0, scrollY - int (wheel.deltaY * 90.0f));
    scrollX = juce::jmax (0.0, scrollX - wheel.deltaX * 140.0);
    clampScroll();
    repaint();
}

} // namespace livemix
