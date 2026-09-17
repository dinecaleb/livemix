#include "TracksPage.h"
#include "ChainStrip.h"
#include "UI/Widgets.h"
#include <algorithm>

namespace livemix
{

namespace
{
    // The ruler is one band: the loop strip along its top, the marker lane inside it, and the
    // ticks along its foot - so a moment, a loop and a bar line are read in one place.
    // The channel panel's width is now the engineer's, not a constant (see TracksPage.h):
    // these are only what it may be set to. 212 is what it used to be and is still the default.
    constexpr int kMinHeaderWidth = 300;
    constexpr int kMaxHeaderWidth = 640;
    constexpr int kDividerGrip = 4;        // how close the pointer has to be to grab it
    constexpr int kToolbarHeight = 46;
    constexpr int kRulerHeight = 38;
    constexpr int kLoopStrip = 8;          // the top of the ruler: drag here to mark a loop
    constexpr int kMarkerTop = 7;          // the marker lane, inside the ruler
    constexpr int kMarkerHeight = 18;
    constexpr int kMinTrackHeight = 38;
    constexpr int kMaxTrackHeight = 260;
    constexpr int kResizeGrip = 5;
    constexpr int kTrimGrip = 7;
    constexpr int kMaxUndo = 40;
    constexpr int kSnapPixels = 9;
    // Rearranging the channels: how far the pointer travels before a press on a header becomes
    // a reorder rather than a selection, and how the view follows a drag past its own edge.
    constexpr int kOrderGrip = 5;
    constexpr int kOrderEdge = 26;
    constexpr int kOrderScroll = 9;

    // The same group colours the mixer bands with, so one session reads the same way in both
    // workspaces - a speaking microphone included.
    juce::Colour laneColourFor (ChannelRole role)
    {
        const auto bus = mixBusForRole (role);
        return bus == MixBus::Master ? Dine::ink3 : Dine::busTint (bus);
    }

    // The same throw as the mixer's faders (-60 .. +12 dB, the useful half given the room),
    // so a drag in the timeline and a drag in the console feel like the same fader.
    const juce::NormalisableRange<float>& faderRange()
    {
        static const juce::NormalisableRange<float> r = []
        {
            juce::NormalisableRange<float> n (-60.0f, 12.0f);
            n.setSkewForCentre (-12.0f);
            return n;
        }();
        return r;
    }

    // What the last listen says about this input's level, as a chip: gain staging comes
    // before anything else, so the timeline says it without being asked.
    struct GainChip { juce::String text; juce::Colour colour; };

    GainChip gainChipFor (const MixController::InputAdvice& a)
    {
        using Level = MixController::InputAdvice::Level;
        switch (a.level)
        {
            case Level::Clipping: return { "CLIPPING", Dine::crit };
            case Level::Faint:    return { "FAINT", Dine::warn };
            case Level::Low:      return { "LOW", Dine::warn };
            case Level::Hot:      return { "HOT", Dine::hot };
            case Level::Digital:  return { "DIGITAL", Dine::warn };
            case Level::NotHeard: return { "NOT HEARD", Dine::ink3 };
            default:              return {};
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
        rowTabs[size_t (i)]->setFontPx (12.0f);
        rowTabs[size_t (i)]->setPadX (9);
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

    make (zoomOutButton, Glyph::minus(), "Zoom out (Cmd-minus)", [this] { zoom (0.8); });
    make (zoomInButton, "+", "Zoom in (Cmd-plus)", [this] { zoom (1.25); });
    make (zoomFitButton, "Fit", "Zoom to fit (Cmd-0)", [this] { zoomToFit(); });
    make (splitButton, "Split at playhead", "Split the selected clip (Cmd-E)", [this] { splitAtPlayhead(); });
    make (snapButton, "Snap", "Snap clips to the grid, the markers, the playhead and other clips while dragging.",
          [this] { setSnap (! snap); });
    make (followButton, "Follow", "Keep the playhead on screen while it rolls.", [this] { setFollow (! follow); });
    make (markerButton, "Add marker", "Drop a marker at the playhead (M)", [this] { addMarkerAtPlayhead(); });
    make (recordAllButton, "All to record",
          "Set every track to record, and click again to set none. Nothing is captured until you press Record.",
          [this] { setAllToRecord (! allSetToRecord()); });
    for (auto* b : { zoomOutButton.get(), zoomInButton.get(), zoomFitButton.get(), splitButton.get(), markerButton.get(), recordAllButton.get() })
    {
        b->setQuiet (true);
        b->setFontPx (12.0f);
        b->setPadX (11);
    }
    snapButton->setStyle (DineButton::Style::Segment);
    followButton->setStyle (DineButton::Style::Segment);
    for (auto* b : { snapButton.get(), followButton.get() }) { b->setFontPx (12.0f); b->setPadX (10); }

    chainStrip.setEmpty ("Click a clip to read its chain here. Double-click a track header to open that channel in the Inspector.");
    chainStrip.onOpen = [this] { if (selection.track >= 0 && onOpenStrip) onOpenStrip (selection.track); };
    addAndMakeVisible (chainStrip);

    updateToolbar();
    updateChainStrip();
}

TracksPage::~TracksPage() = default;

// The foot of the page: the picked-out track's chain, stage by stage, and the session's clock.
void TracksPage::updateChainStrip()
{
    const auto& project = services.daw().getProject();
    const auto& params = controller.getBase();
    if (selection.track >= 0 && selection.track < numTracks() && selection.track < params.numStrips)
    {
        const auto& input = controller.getSession().inputs[size_t (selection.track)];
        chainStrip.setSource (juce::String (input.name), laneColourFor (input.role),
                              params.strips[size_t (selection.track)].channel, false, input.isStereo());
    }
    else
    {
        chainStrip.setEmpty ("Click a clip to read its chain here. Double-click a track header to open that channel in the Inspector.");
    }

    juce::String note = clockText (services.daw().getTransport().getPosition(), project.sampleRate);
    if (project.hasAudio()) note += "  /  " + clockText (project.lengthSamples(), project.sampleRate);
    if (project.liveSafe) note += "   LIVE SAFE";
    chainStrip.setNote (note);
}

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

int TracksPage::lanesTop() const { return kToolbarHeight + kRulerHeight; }

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

// The slot a dragged row would drop into: 0 is above the first track, numTracks() is below
// the last. A row belongs to the slot above it until the pointer passes its middle, which is
// what makes a short drag feel like "swap with the one next door".
int TracksPage::dropSlotAtY (int y) const
{
    const int n = numTracks();
    for (int i = 0; i < n; ++i)
        if (y < trackTop (i) + trackHeight (i) / 2) return i;
    return n;
}

juce::Rectangle<int> TracksPage::toolbarArea() const
{
    return getLocalBounds().withHeight (kToolbarHeight);
}

juce::Rectangle<int> TracksPage::rulerArea() const
{
    return getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedTop (kToolbarHeight).withHeight (kRulerHeight);
}

juce::Rectangle<int> TracksPage::markerArea() const
{
    return getLocalBounds().withTrimmedLeft (headerWidth)
                           .withTrimmedTop (kToolbarHeight + kMarkerTop).withHeight (kMarkerHeight);
}

juce::Rectangle<int> TracksPage::lanesArea() const
{
    return getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedTop (lanesTop())
                           .withTrimmedBottom (footHeight());
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
    return headerWidth + int (double (sample) / samplesPerPixel() - scrollX);
}

juce::int64 TracksPage::xToSample (int x) const
{
    return juce::jmax ((juce::int64) 0, juce::int64 ((double (x - headerWidth) + scrollX) * samplesPerPixel()));
}

TracksPage::ClipRef TracksPage::clipAt (juce::Point<int> p) const
{
    if (p.x < headerWidth || p.y < lanesTop()) return {};
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

bool TracksPage::compactHeader (int track) const { return trackHeight (track) < 50; }

// ---------------------------------------------------------------------------
// The channel panel's width
//
// One number, dragged once, inherited by every row - the interaction anyone arriving from
// Logic, Pro Tools or Reaper already knows. Everything on this page is measured from
// `headerWidth`, so widening the panel widens the names, moves the keys and the meter with
// them and hands the remaining width to the timeline, with no per-row state anywhere.
// ---------------------------------------------------------------------------
void TracksPage::setPanelWidth (int px)
{
    // The maximum also yields to the window: a panel wider than the timeline it sits beside
    // is a panel that has stopped being a panel.
    const int roomForTimeline = 220;
    const int cap = juce::jmin (kMaxHeaderWidth, juce::jmax (kMinHeaderWidth, getWidth() - roomForTimeline));
    const int want = juce::jlimit (kMinHeaderWidth, cap, px);
    if (want == headerWidth) return;
    headerWidth = want;
    clampScroll();
    repaint();
}

// The grab zone for the divider: a few pixels either side of the panel's edge, everywhere
// below the tool row, so it can be caught against the ruler as well as against the lanes.
bool TracksPage::onDivider (juce::Point<int> p) const
{
    return p.y >= kToolbarHeight
        && p.x >= headerWidth - kDividerGrip && p.x <= headerWidth + kDividerGrip - 1;
}

// The keys are a 2 x 2 block against the meter: M and S on top, record arm and monitoring
// under them. Key 0 is the arm, 1 the monitor, 2 the mute and 3 the solo, whatever row they
// are drawn on, so the paint and the hit-test read the same table.
juce::Rectangle<int> TracksPage::keyCell (int track, int key) const
{
    // Right to left along the header: the level (52), the fader (58), the meter (8), then the
    // keys - a 2 x 2 grid on a tall row (R A over M S), one row of four on a short one.
    const int top = trackTop (track), h = trackHeight (track);
    const bool compact = compactHeader (track);
    const int w = compact ? 19 : 24, cell = compact ? 16 : 22, gap = compact ? 3 : 4;
    const int right = headerWidth - 12 - (compact ? 0 : 52 + 8) - 58 - 8 - 8 - 8;
    if (compact)
    {
        const int left = right - (4 * w + 3 * gap);
        const int y = top + juce::jmax (2, (h - cell) / 2);
        return { left + key * (w + gap), y, w, cell };
    }
    const int left = right - (2 * w + gap);
    const int column = (key == 0 || key == 2) ? 0 : 1;         // R and M on the left
    const int row = key >= 2 ? 1 : 0;                          // R A on top, M S under them
    const int y = top + juce::jmax (2, (h - (2 * cell + gap)) / 2);
    return { left + column * (w + gap), y + row * (cell + gap), w, cell };
}

// The quick fader. A tall row carries it under the name with its level beside it; a short
// row keeps the name legible and gets a slim bar along the foot instead, above the grip.
juce::Rectangle<int> TracksPage::faderCell (int track) const
{
    if (track < 0 || track >= numTracks()) return {};
    const int top = trackTop (track), h = trackHeight (track);
    const bool compact = compactHeader (track);
    const int right = headerWidth - 12 - (compact ? 0 : 52 + 8);
    if (right - 58 < 60) return {};
    return { right - 58, top + (h - 14) / 2, 58, 14 };
}

// A fader is grabbed where it stands and moved from there - it never jumps to the click.
// A live fader that snaps to wherever the mouse landed is how a service gets 12 dB louder
// by accident. Hold Shift for a quarter-speed move, the same as the console's faders.
void TracksPage::dragFader (int track, int x, bool fine)
{
    if (track < 0 || track >= controller.getBase().numStrips) return;
    const auto cell = faderCell (track);
    if (cell.isEmpty()) return;
    const float travel = float (x - dragStartX) / float (juce::jmax (1, cell.getWidth()));
    const float norm = juce::jlimit (0.0f, 1.0f, dragFaderNorm + travel * (fine ? 0.25f : 1.0f));
    controller.setStripFader (track, std::round (faderRange().convertFrom0to1 (norm) * 10.0f) * 0.1f);
    // A fader has to feel immediate, which means repainting the row it is on and the readout
    // that follows it - not the whole timeline, which at 48 channels costs more than a frame.
    repaint (0, trackTop (track), headerWidth, trackHeight (track));
    repaint (toolbarArea());
}

juce::Rectangle<int> TracksPage::markerFlag (int index) const
{
    const auto& markers = services.daw().getProject().markers;
    if (index < 0 || index >= int (markers.size())) return {};
    const auto& m = markers[size_t (index)];
    const int x = sampleToX (m.position);
    const auto font = Dine::text (10.0f);
    const int w = juce::jmin (170, Dine::textWidth (font, m.name) + 12);
    return { x, markerArea().getY(), juce::jmax (22, w), kMarkerHeight };
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

    // The R keys can be changed from a header, from the Track menu or from the toolbar itself,
    // so the button follows the session rather than its own last click.
    if (const bool all = allSetToRecord(); all != recordAllOn) { recordAllOn = all; updateToolbar(); }

    // One meter reading per track per tick: consuming it twice would halve what is shown.
    const int tracks = numTracks();
    if (int (peaks.size()) != tracks) peaks.assign (size_t (tracks), -120.0f);
    if (controller.isPrepared())
        for (int i = 0; i < juce::jmin (tracks, controller.getEngine().getNumStrips()); ++i)
        {
            const float now = controller.getEngine().getStrip (i).getOutputMeter().consumeMaxPeakDb();
            peaks[size_t (i)] = juce::jmax (now, peaks[size_t (i)] - 2.0f);
        }

    if (adviceForTune != controller.getTuneCount() || int (advice.size()) != tracks)
    {
        adviceForTune = controller.getTuneCount();
        advice.clear();
        advice.reserve (size_t (tracks));
        for (int i = 0; i < tracks; ++i) advice.push_back (controller.getInputAdvice (i));
        repaint();
    }

    const auto& transport = services.daw().getTransport();
    const juce::int64 playhead = transport.getPosition();
    const bool moving = transport.isPlaying();

    bool loading = false;
    for (const auto& t : thumbnails) if (t.second != nullptr && ! t.second->isFullyLoaded()) loading = true;

    updateChainStrip();

    // ---- what actually has to be redrawn this tick
    //
    // A timeline is the most expensive surface in the app: names, chips, faders, keys,
    // waveforms and a playhead, times however many channels the church has. Repainting all of
    // it thirty times a second because a meter moved is what made DLIVE feel slower than a
    // DAW should, so nothing here repaints more than it has to.
    //
    //   the playhead moved  -> the ruler and the lanes (the headers have not changed)
    //   the view scrolled   -> everything (the content under the clip is different)
    //   otherwise           -> the meters, and only the ones whose reading actually moved
    const double scrollWas = scrollX;
    if (moving && follow)
    {
        const int x = sampleToX (playhead);
        const auto lanes = lanesArea();
        if (x > lanes.getRight() - 60 || x < lanes.getX())
            scrollX = juce::jmax (0.0, double (playhead) / samplesPerPixel() - lanes.getWidth() * 0.2);
    }
    const bool scrolled = std::fabs (scrollX - scrollWas) > 0.01;
    const bool thumbsChanged = loading != waitingOnThumbnails;

    if (scrolled || thumbsChanged || loading)
    {
        lastPlayhead = playhead;
        waitingOnThumbnails = loading;
        repaint();
    }
    else if (playhead != lastPlayhead)
    {
        lastPlayhead = playhead;
        // The playhead lives over the ruler and the lanes; the channel panel never moves with
        // it, so it is left alone.
        repaint (getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedTop (kToolbarHeight)
                                 .withTrimmedBottom (footHeight()));
    }

    // The meters, one narrow strip per track, and only where the reading really changed. A
    // level that has not moved a tenth of a decibel is not worth a repaint.
    if (int (paintedPeaks.size()) != tracks) paintedPeaks.assign (size_t (tracks), -1000.0f);
    const auto lanes = lanesArea();
    for (int i = 0; i < tracks; ++i)
    {
        if (std::fabs (peaks[size_t (i)] - paintedPeaks[size_t (i)]) < 0.1f) continue;
        paintedPeaks[size_t (i)] = peaks[size_t (i)];
        const auto cell = meterCell (i);
        if (! cell.isEmpty() && cell.getBottom() > lanes.getY() && cell.getY() < lanes.getBottom())
            repaint (cell.expanded (1, 1));
    }
}

// The level meter down the right edge of a header. One place, so the repaint that keeps it
// moving and the paint that draws it can never disagree about where it is.
juce::Rectangle<int> TracksPage::meterCell (int track) const
{
    if (track < 0 || track >= numTracks()) return {};
    const int top = trackTop (track), h = trackHeight (track);
    if (h <= 18) return {};
    const bool compact = compactHeader (track);
    const int right = headerWidth - 12 - (compact ? 0 : 52 + 8) - 58 - 8;
    const int mh = juce::jmin (30, h - 10);
    return { right - 8, top + (h - mh) / 2, 8, mh };
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
    zoomAround (lanesArea().getCentreX(), factor);
}

// Zooming keeps the moment under the pointer where it is, so a pinch feels like pulling the
// timeline apart rather than being thrown somewhere else in the session.
void TracksPage::zoomAround (int x, double factor)
{
    const auto lanes = lanesArea();
    const int anchorX = juce::jlimit (lanes.getX(), juce::jmax (lanes.getX(), lanes.getRight()), x);
    const juce::int64 anchor = xToSample (anchorX);
    const double was = pixelsPerSecond;
    pixelsPerSecond = juce::jlimit (0.2, 800.0, pixelsPerSecond * factor);
    if (std::abs (pixelsPerSecond - was) < 1.0e-9) return;
    scrollX = juce::jmax (0.0, double (anchor) / samplesPerPixel() - double (anchorX - headerWidth));
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
    const int px = h == RowHeight::Small ? 40 : h == RowHeight::Large ? 82 : 58;
    auto& project = services.daw().getProject();
    for (auto& t : project.tracks) t.height = px;
    clampScroll();
    updateToolbar();
    services.saveSession();
    repaint();
}

void TracksPage::setFootShown (bool v)
{
    if (footShown == v) return;
    footShown = v;
    chainStrip.setVisible (v);
    resized();
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

// ------------------------------------------------- putting a track right
// A track and its input drifted apart often enough to be worth saying out loud: what the
// header calls a track and what the audio under it is called are two different pieces of
// the session, and correcting the label used to mean leaving the timeline. So the header
// flags the disagreement and its menu fixes it - one track, every track, or the
// assignments themselves.
juce::String TracksPage::clipName (int track) const
{
    const auto& project = services.daw().getProject();
    if (track < 0 || track >= int (project.tracks.size())) return {};
    for (const auto& clip : project.tracks[size_t (track)].clips)
        if (clip.name.isNotEmpty()) return clip.name;
    return {};
}

bool TracksPage::nameMismatch (int track) const
{
    const auto name = clipName (track);
    if (name.isEmpty() || track >= int (controller.getSession().inputs.size())) return false;
    const juce::String header (controller.getSession().inputs[size_t (track)].name);
    if (header.isEmpty()) return true;
    // A take is the track's own name with something after it ("Kick", "Kick take 2"), and
    // that is not a mismatch. A different word is.
    return ! name.startsWithIgnoreCase (header) && ! header.startsWithIgnoreCase (name);
}

int TracksPage::mismatchCount() const
{
    int n = 0;
    for (int i = 0; i < numTracks(); ++i) if (nameMismatch (i)) ++n;
    return n;
}

void TracksPage::setTrackName (int track, const juce::String& name)
{
    if (track < 0 || track >= numTracks() || name.trim().isEmpty()) return;
    controller.setInputName (track, name.trim().toStdString());
    services.daw().setSession (controller.getSession());
    services.saveSession();
    updateChainStrip();
    if (onSessionChanged) onSessionChanged();
    repaint();
}

void TracksPage::renameTrack (int track)
{
    if (track < 0 || track >= numTracks()) return;
    const juce::String current (controller.getSession().inputs[size_t (track)].name);
    auto* alert = new juce::AlertWindow ("Rename track " + juce::String (track + 1).paddedLeft ('0', 2),
                                         "What is on this track?", juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor ("name", current, "Name");
    alert->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    alert->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, alert, track] (int r)
        {
            std::unique_ptr<juce::AlertWindow> closer (alert);
            if (r != 1) return;
            setTrackName (track, alert->getTextEditorContents ("name"));
        }), true);
}

void TracksPage::matchNamesToClips()
{
    int fixed = 0;
    for (int i = 0; i < numTracks(); ++i)
    {
        if (! nameMismatch (i)) continue;
        controller.setInputName (i, clipName (i).toStdString());
        ++fixed;
    }
    if (fixed == 0) { if (onToast) onToast ("Every track already matches its clips."); return; }

    services.daw().setSession (controller.getSession());
    services.saveSession();
    updateChainStrip();
    if (onSessionChanged) onSessionChanged();
    repaint();
    if (onToast) onToast (juce::String (fixed) + (fixed == 1 ? " track now matches its clip. "
                                                            : " tracks now match their clips. ")
                          + "Check their sources too if the icons look wrong.");
}

// What a track is drawn as. Like a rename this is a label: no rebuild, so the mix, the plan
// and the clips are untouched, and the console and the Inspector are redrawn with it.
void TracksPage::setTrackIcon (int track, const std::string& key)
{
    if (track < 0 || track >= numTracks()) return;
    controller.setInputIcon (track, key);
    services.daw().setSession (controller.getSession());
    services.saveSession();
    if (onSessionChanged) onSessionChanged();
    repaint();
}

// Changing what a source *is* changes the bus it feeds and the chain it starts from, so
// unlike a rename this rebuilds the routing: the mix goes back to the baseline for that
// strip and the session says so.
void TracksPage::setTrackSource (int track, ChannelRole role)
{
    if (track < 0 || track >= numTracks()) return;
    auto session = controller.getSession();
    if (session.inputs[size_t (track)].role == role) return;
    const juce::String name (session.inputs[size_t (track)].name);
    session.inputs[size_t (track)].role = role;
    controller.setSession (session);
    services.daw().setSession (session);
    services.reconfigure();
    services.saveSession();
    rebuild();
    updateChainStrip();
    if (onSessionChanged) onSessionChanged();
    if (onToast) onToast (name + " is now " + Dine::friendlyRoleName (role)
                          + ". It feeds a different bus, so this channel went back to its baseline chain - "
                            "the rest of the mix is where you left it. RE-TUNE while the band plays.");
}

// Rearranging the channels. A track and its input are one thing seen twice, so this moves the
// *input*: the mixer's bank, TUNE's rail, the Inspector's list and the ASSIGN page all read the
// new order too. Nothing about the sound changes - the clips follow their track (syncTracks)
// and every channel's chain, gain, fader and sends follow their input (carryMix) - but the
// graph is rebuilt, which is why LIVE SAFE locks it like any other re-route.
void TracksPage::moveTrack (int from, int to)
{
    const int n = numTracks();
    if (from < 0 || from >= n) return;
    to = juce::jlimit (0, n - 1, to);
    if (to == from) return;
    if (locked()) return;

    auto session = controller.getSession();
    const juce::String name (session.inputs[size_t (from)].name);
    auto moved = session.inputs[size_t (from)];
    session.inputs.erase (session.inputs.begin() + from);
    session.inputs.insert (session.inputs.begin() + to, moved);

    controller.setSession (session);
    services.daw().setSession (session);      // the clips move with their track
    services.reconfigure();                   // rebuilds the graph; the mix follows its input
    services.saveSession();

    selection = { to, -1 };
    rebuild();
    updateChainStrip();
    if (onSessionChanged) onSessionChanged();
    if (onToast) onToast (name + " is now channel " + juce::String (to + 1)
                          + ". The mixer, the Inspector and the assignments read the same order.");
}

void TracksPage::selectTrack (int track)
{
    if (track == selection.track) return;
    selection.track = track;
    selection.index = -1;
    refresh();
    repaint();
}

void TracksPage::moveSelectedTrack (int delta)
{
    if (! canMoveSelectedTrack (delta)) return;
    moveTrack (selection.track, selection.track + delta);
}

bool TracksPage::canMoveSelectedTrack (int delta) const
{
    const int to = selection.track + delta;
    return selection.track >= 0 && selection.track < numTracks() && to >= 0 && to < numTracks();
}

void TracksPage::headerMenu (int track)
{
    if (track < 0 || track >= numTracks()) return;
    const auto& input = controller.getSession().inputs[size_t (track)];
    const juce::String name (input.name);
    const auto clip = clipName (track);
    const int wrong = mismatchCount();

    juce::PopupMenu m;
    m.addSectionHeader (juce::String (track + 1).paddedLeft ('0', 2) + "  " + name);
    m.addItem (1, "Rename" + Glyph::ellip());
    m.addItem (2, "Use the clip's name" + (clip.isEmpty() ? juce::String() : juce::String ("  (") + clip + ")"),
               clip.isNotEmpty() && nameMismatch (track));
    m.addItem (3, "Match every track to its clips" + (wrong > 1 ? "  (" + juce::String (wrong) + ")" : juce::String()),
               wrong > 0);
    m.addSeparator();

    int id = 100;
    std::vector<ChannelRole> byId;
    juce::PopupMenu sources;
    for (const auto& group : Dine::roleGroups())
    {
        juce::PopupMenu sub;
        for (auto r : group.roles)
        {
            sub.addItem (id++, Dine::friendlyRoleName (r), true, input.role == r);
            byId.push_back (r);
        }
        sources.addSubMenu (group.name, sub, true);
    }
    m.addSubMenu ("Source", sources, true);

    // The icon follows the source unless this session says otherwise - a pad running backing
    // tracks, a spare DI carrying talkback - so the picture on the header can be put right
    // without pretending the input is something it is not.
    int iconId = 200;
    std::vector<std::string> iconKeys;
    juce::PopupMenu icons;
    icons.addItem (iconId++, "From the source", ! input.icon.empty(), input.icon.empty());
    iconKeys.push_back ({});
    icons.addSeparator();
    for (const auto& choice : Dine::iconChoices())
    {
        icons.addItem (iconId++, choice.label, true, input.icon == choice.key);
        iconKeys.push_back (choice.key);
    }
    m.addSubMenu ("Icon", icons, true);

    m.addSeparator();
    m.addItem (7, "Move up", track > 0);
    m.addItem (8, "Move down", track < numTracks() - 1);
    m.addSeparator();
    m.addItem (4, "Fix the assignments" + Glyph::ellip(), onOpenAssign != nullptr);
    m.addSeparator();
    m.addItem (6, "TUNE CHANNEL", onTuneStrip != nullptr);
    m.addItem (5, "Open in the Inspector");

    auto header = juce::Rectangle<int> (0, trackTop (track), headerWidth, trackHeight (track));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                         .withTargetScreenArea (localAreaToGlobal (header))
                         .withMinimumWidth (240),
                     [this, track, clip, byId, iconKeys] (int chosen)
                     {
                         if (chosen <= 0 || track >= numTracks()) return;
                         if (chosen >= 200) { setTrackIcon (track, iconKeys[size_t (chosen - 200)]); return; }
                         if (chosen >= 100) { setTrackSource (track, byId[size_t (chosen - 100)]); return; }
                         switch (chosen)
                         {
                             case 1: renameTrack (track); break;
                             case 2: setTrackName (track, clip); break;
                             case 3: matchNamesToClips(); break;
                             case 4: if (onOpenAssign) onOpenAssign(); break;
                             case 5: selection = { track, -1 }; updateChainStrip();
                                     if (onOpenStrip) onOpenStrip (track); break;
                             case 6: selection = { track, -1 }; updateChainStrip(); repaint();
                                     if (onTuneStrip) onTuneStrip (track); break;
                             case 7: moveTrack (track, track - 1); break;
                             case 8: moveTrack (track, track + 1); break;
                             default: break;
                         }
                     });
}

// Every track at once. A volunteer setting up before a service should not have to press R
// twenty-four times, and the one thing this must never read as is "start recording": it is a
// setting, it fills when it is on, and the transport is still the only thing that records.
bool TracksPage::allSetToRecord() const
{
    const auto& tracks = services.daw().getProject().tracks;
    if (tracks.empty()) return false;
    for (const auto& t : tracks) if (! t.armed) return false;
    return true;
}

void TracksPage::setAllToRecord (bool on)
{
    if (locked()) return;
    auto& project = services.daw().getProject();
    if (project.tracks.empty()) return;
    for (auto& t : project.tracks) t.armed = on;
    services.daw().refresh();
    services.saveSession();
    if (onToast) onToast (on ? "Every track will be recorded." : "No tracks will be recorded.");
    updateToolbar();
    repaint();
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
        g.setColour (Dine::window);
        g.fillRect (lanes);

        if (project.loopEnabled && project.loopEnd > project.loopStart)
        {
            const int x1 = sampleToX (project.loopStart), x2 = sampleToX (project.loopEnd);
            g.setColour (Dine::monitor.withAlpha (0.06f));
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
            // the quarter divisions, so a bar has something inside it to read against
            for (int q = 1; q < 4; ++q)
            {
                const int qx = sampleToX (juce::int64 ((s + grid * 0.25 * q) * rate));
                if (qx < lanes.getX() || qx > lanes.getRight()) continue;
                g.setColour (juce::Colours::white.withAlpha (0.035f));
                g.fillRect (float (qx), float (lanes.getY()), 0.5f, float (lanes.getHeight()));
            }
            if (x < lanes.getX()) continue;
            g.setColour (juce::Colours::white.withAlpha (0.09f));
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
            g.drawFittedText ("Nothing recorded yet. Press the red R on each track you want to record, then press "
                              "Record - or import a folder of stems from the File menu.",
                              lanes.reduced (40, 0).withHeight (46).withY (lanes.getY() + 30),
                              juce::Justification::centredTop, 2);
        }

    }

    // ---- headers
    {
        juce::Graphics::ScopedSaveState save (g);
        auto headers = getLocalBounds().withWidth (headerWidth).withTrimmedTop (lanesTop())
                           .withTrimmedBottom (footHeight());
        g.reduceClipRegion (headers);
        g.setColour (Dine::window);
        g.fillRect (headers);
        for (int i = 0; i < tracks; ++i)
        {
            const int top = trackTop (i);
            const int height = trackHeight (i);
            if (top + height < headers.getY() || top > headers.getBottom()) continue;
            paintHeader (g, i, { 0, top, headerWidth, height });
            // The row being rearranged is lifted off the page: it stays where it is, dimmed,
            // while the line below shows where letting go would put it.
            if (dragOrderLifted && i == dragOrderFrom)
            {
                auto row = juce::Rectangle<int> (0, top, headerWidth, height).toFloat();
                g.setColour (Dine::window.withAlpha (0.55f));
                g.fillRect (row);
                g.setColour (Dine::accent.withAlpha (0.55f));
                g.drawRect (row.reduced (1.0f), 1.0f);
            }
        }
        if (tracks == 0)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.5f));
            g.drawText ("No tracks yet", headers.reduced (16, 20), juce::Justification::topLeft);
        }
    }

    // ---- where a dragged channel would land, across the header and the lane alike: the row
    // moves through the whole timeline, not only through the list of names.
    if (dragOrderLifted && dragOrderSlot >= 0 && tracks > 0)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (getLocalBounds().withTrimmedTop (lanesTop()).withTrimmedBottom (footHeight()));
        const int slot = juce::jlimit (0, tracks, dragOrderSlot);
        const float y = float (slot < tracks ? trackTop (slot)
                                             : trackTop (tracks - 1) + trackHeight (tracks - 1));
        g.setColour (Dine::accent);
        g.fillRect (0.0f, y - 1.0f, float (getWidth()), 2.0f);
        g.fillEllipse (2.0f, y - 4.0f, 8.0f, 8.0f);
    }

    paintRuler (g);
    paintMarkers (g);
    // The playhead runs through the ruler and the lanes alike, with a cap in the ruler you
    // can see from the far side of the room.
    {
        const int px = sampleToX (services.daw().getTransport().getPosition());
        const auto colour = services.daw().isRecording() ? Dine::crit : Dine::accent;
        if (px >= lanes.getX() - 1 && px <= lanes.getRight() + 1)
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedTop (kToolbarHeight)
                                    .withTrimmedBottom (footHeight()));
            g.setColour (colour);
            g.fillRect (float (px), float (kToolbarHeight), 1.0f, float (lanes.getBottom() - kToolbarHeight));
        }
    }

    paintToolbar (g);

    // The divider between the channel panel and the timeline. Normally the same hairline the
    // page has always drawn; under the pointer (or while it is being dragged) it lights up and
    // grows a grip, so the one thing on this page that can be dragged sideways says so.
    {
        const bool active = drag == Drag::PanelWidth || dividerHot;
        const float top = float (kToolbarHeight);
        const float height = float (getHeight() - kToolbarHeight - footHeight());
        g.setColour (active ? Dine::accent.withAlpha (0.75f) : Dine::window);
        g.fillRect (float (headerWidth) - (active ? 1.0f : 1.0f), top, active ? 2.0f : 2.0f, height);
        if (active)
        {
            const float cy = top + height * 0.5f;
            g.setColour (Dine::accent.withAlpha (0.9f));
            for (int i = -1; i <= 1; ++i)
                g.fillRoundedRectangle (float (headerWidth) - 1.5f, cy + float (i) * 7.0f - 1.0f, 3.0f, 2.0f, 1.0f);
        }
    }

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
    Dine::drawHeaderBand (g, area);

    if (rowTabs[0] != nullptr && rowTabs[0]->isVisible())
        Dine::drawSegmentTrack (g, rowTabs[0]->getBounds().getUnion (rowTabs[2]->getBounds()).expanded (2, 2));
    if (snapButton != nullptr)
        Dine::drawSegmentTrack (g, snapButton->getBounds().getUnion (followButton->getBounds()).expanded (2, 2));

    // ---- what is picked out, then the zoom, from the right
    if (recordAllButton != nullptr && zoomOutButton != nullptr)
    {
        auto row = toolbarArea().withTrimmedLeft (recordAllButton->getRight() + 14)
                                .withRight (zoomOutButton->getX() - 60);
        if (row.getWidth() > 60)
        {
            juce::String info;
            if (selection.track >= 0 && selection.track < numTracks())
                info = juce::String (controller.getSession().inputs[size_t (selection.track)].name) + " selected";
            else if (services.daw().getProject().liveSafe) info = "LIVE SAFE " + Glyph::dot() + " the timeline is locked";
            else info = "Nothing selected";
            g.setColour (services.daw().getProject().liveSafe && selection.track < 0 ? Dine::warn : Dine::ink3);
            g.setFont (Dine::text (13.0f));
            g.drawText (info, row, juce::Justification::centredRight, true);
        }
        auto zoomCell = toolbarArea().withRight (zoomOutButton->getX() - 6).withTrimmedRight (0);
        zoomCell = zoomCell.removeFromRight (44);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (11.0f));
        g.drawText (juce::String (juce::roundToInt (pixelsPerSecond / 18.0 * 100.0)) + "%", zoomCell, juce::Justification::centredRight);
    }
}

void TracksPage::paintRuler (juce::Graphics& g)
{
    auto area = rulerArea();
    auto all = getLocalBounds().withTrimmedTop (kToolbarHeight).withHeight (kRulerHeight);
    g.setColour (Dine::pageBar);
    g.fillRect (all);

    // The header column of the ruler says what the lane beside it holds.
    {
        auto cell = juce::Rectangle<int> (0, all.getY(), headerWidth, all.getHeight());
        g.setColour (Dine::toolbar);
        g.fillRect (cell);
        const auto& project = services.daw().getProject();
        g.setColour (Dine::ink4);
        g.setFont (Dine::caps (11.0f, 0.08f, 500));
        auto text = cell.reduced (14, 0);
        g.drawText ("MARKERS", text, juce::Justification::centredLeft);
        if (project.numArmed() > 0)
        {
            g.setColour (Dine::crit);
            g.setFont (Dine::mono (10.0f, 500));
            g.drawText (juce::String (project.numArmed()) + " TO RECORD", text, juce::Justification::centredRight, true);
        }
    }

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);

    const auto& project = services.daw().getProject();
    const double rate = juce::jmax (1.0, project.sampleRate);
    const double step = gridSeconds();

    // The loop, as a bar along the top of the ruler.
    if (project.loopEnd > project.loopStart)
    {
        const int x1 = sampleToX (project.loopStart), x2 = sampleToX (project.loopEnd);
        const auto tint = project.loopEnabled ? Dine::monitor : Dine::ink4;
        auto range = juce::Rectangle<float> (float (x1), float (area.getY()), float (juce::jmax (3, x2 - x1)), 5.0f);
        juce::Path p;
        p.addRoundedRectangle (range.getX(), range.getY(), range.getWidth(), range.getHeight(), 3.0f, 3.0f, false, false, true, true);
        g.setColour (tint.withAlpha (project.loopEnabled ? 1.0f : 0.5f));
        g.fillPath (p);
    }

    // The ticks along the foot: a tall one where the number goes, short ones between.
    const double firstSecond = std::floor ((scrollX / pixelsPerSecond) / step) * step;
    for (double sec = firstSecond; ; sec += step)
    {
        const int x = sampleToX (juce::int64 (sec * rate));
        if (x > area.getRight()) break;
        if (x < headerWidth - 60) continue;

        g.setColour (Dine::control);
        g.fillRect (float (x), float (area.getBottom() - 12), 1.0f, 12.0f);
        for (int q = 1; q < 4; ++q)
        {
            const int hx = sampleToX (juce::int64 ((sec + step * 0.25 * q) * rate));
            g.fillRect (float (hx), float (area.getBottom() - 6), 1.0f, 6.0f);
        }

        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (9.0f));
        const int total = int (sec);
        juce::String label = total >= 60 && step >= 60.0 ? juce::String (total / 60) + "m"
                           : juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
        if (step < 1.0) label += "." + juce::String (int ((sec - double (total)) * 10.0 + 0.5));
        g.drawText (label, x + 4, area.getBottom() - 24, 60, 12, juce::Justification::bottomLeft, false);
    }
}

// Markers live inside the ruler: a line at the moment and its name in small caps beside it,
// so a marker is read against the same ticks the clips are read against.
void TracksPage::paintMarkers (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (markerArea());

    const auto& markers = services.daw().getProject().markers;
    for (int i = 0; i < int (markers.size()); ++i)
    {
        auto flag = markerFlag (i);
        if (flag.getRight() < markerArea().getX() || flag.getX() > markerArea().getRight()) continue;
        const bool hot = hoverMarker == i;
        Dine::fillRounded (g, flag.toFloat(), hot ? Dine::controlHot : Dine::control, Dine::Radius::chip);
        g.setColour (hot ? Dine::ink : Dine::ink2);
        g.setFont (Dine::text (10.0f));
        g.drawText (markers[size_t (i)].name, flag.reduced (6, 0), juce::Justification::centredLeft, true);
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
    const bool compact = compactHeader (track);

    // The row: a flat plane with 2 px of the ground between it and the next.
    g.setColour (Dine::window);
    g.fillRect (area);
    auto row = area.withTrimmedBottom (2);
    g.setColour (selected ? Dine::card : Dine::console);
    g.fillRect (row);

    const auto& params = controller.getBase();
    const bool inRange = track < params.numStrips;
    const bool mute = inRange && params.strips[size_t (track)].mute;
    const bool solo = inRange && params.strips[size_t (track)].solo;
    const bool dim = mute;
    const bool monitoring = state.monitor != MonitorMode::Off;

    // ---- the dot, the number, the name and its note
    auto text = row.reduced (12, 0).withRight (keyCell (track, 0).getX() - 8);
    {
        auto dot = text.removeFromLeft (7).withSizeKeepingCentre (7, 7);
        g.setColour (state.armed ? Dine::crit : monitoring ? Dine::monitor : mute ? Dine::warn : tint);
        g.fillEllipse (dot.toFloat());
        text.removeFromLeft (8);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (11.0f));
        g.drawText (juce::String (track + 1).paddedLeft ('0', 2), text.removeFromLeft (20), juce::Justification::centredLeft);
    }

    const auto chip = gainChipFor (track < int (advice.size()) ? advice[size_t (track)] : MixController::InputAdvice {});
    const bool wrongName = nameMismatch (track);
    const float pan = inRange ? params.strips[size_t (track)].pan : 0.0f;
    juce::String note;
    if (chip.text == "DIGITAL") note = "The level works only because DLIVE raised it digitally. Raise the console gain instead.";
    else if (std::fabs (pan) >= 0.005f) note = "Balance " + juce::String (pan < 0.0f ? "L" : "R") + juce::String (juce::roundToInt (std::fabs (pan) * 100.0f));
    else if (wrongName) note = "Named differently from its clips - right-click to fix";

    auto lines = text;
    auto nameLine = compact || note.isEmpty() ? lines.withSizeKeepingCentre (lines.getWidth(), 18)
                                              : lines.withSizeKeepingCentre (lines.getWidth(), 34).removeFromTop (18);
    {
        const auto nameFont = Dine::text (13.0f, 500);
        g.setColour (dim ? Dine::ink3 : chip.text == "DIGITAL" ? Dine::warn : wrongName ? Dine::warn : Dine::ink);
        g.setFont (nameFont);
        auto nameCell = nameLine;
        if (chip.text.isNotEmpty() && chip.text != "HEALTHY" && nameCell.getWidth() > 150)
        {
            auto c = nameCell.removeFromRight (juce::jmin (76, Dine::textWidth (Dine::caps (9.0f, 0.06f, 500), chip.text) + 14));
            Dine::drawStatusChip (g, c.withSizeKeepingCentre (c.getWidth(), 15).toFloat(), chip.text, chip.colour);
            nameCell.removeFromRight (6);
        }
        const int room = nameCell.getWidth() - (wrongName ? 15 : 0);
        g.drawText (juce::String (input.name), nameCell.removeFromLeft (juce::jlimit (0, room, Dine::textWidth (nameFont, input.name) + 1)),
                    juce::Justification::centredLeft, true);
        if (wrongName && nameCell.getWidth() >= 13)
            Dine::drawIcon (g, Dine::Icon::Warn, nameCell.removeFromLeft (13).toFloat().withSizeKeepingCentre (11.0f, 11.0f), Dine::warn);
    }
    if (! compact && note.isNotEmpty())
    {
        auto noteLine = lines.withSizeKeepingCentre (lines.getWidth(), 34).removeFromBottom (14);
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (10.0f));
        g.drawText (note, noteLine, juce::Justification::centredLeft, true);
    }

    // ---- R / A / M / S: the console's own keys
    auto key = [&] (juce::Rectangle<int> cell, const juce::String& label, bool on, juce::Colour colour)
    {
        Dine::fillRounded (g, cell.toFloat(), on ? colour : Dine::control, Dine::Radius::chip);
        g.setColour (on ? Dine::onAccent : Dine::ink2);
        g.setFont (Dine::text (compact ? 10.0f : 11.0f, 600));
        g.drawText (label, cell, juce::Justification::centred);
    };
    key (keyCell (track, 0), "R", state.armed, Dine::keyRec);
    key (keyCell (track, 1), "A", monitoring, Dine::keyMon);
    key (keyCell (track, 2), "M", mute, Dine::keyMute);
    key (keyCell (track, 3), "S", solo, Dine::keySolo);

    // ---- the meter
    {
        auto meter = meterCell (track);
        if (! meter.isEmpty() && track < int (peaks.size()))
            Dine::fillMeter (g, meter.toFloat(), DineMeter::norm (peaks[size_t (track)]), true, dim, 2.0f);
    }

    // ---- the fader and its level
    const float faderDb = inRange ? params.strips[size_t (track)].faderDb : 0.0f;
    if (const auto cell = faderCell (track); ! cell.isEmpty())
    {
        const float norm = faderRange().convertTo0to1 (juce::jlimit (-60.0f, 12.0f, faderDb));
        g.setColour (Dine::hairStrong);
        g.fillRect (float (cell.getX()), float (cell.getCentreY()) - 1.0f, float (cell.getWidth()), 2.0f);
        const float cx = juce::jlimit (float (cell.getX()) + 4.5f, float (cell.getRight()) - 4.5f, cell.getX() + cell.getWidth() * norm);
        g.setColour (controller.isBypassed() || dim ? Dine::ink4 : Dine::ink2);
        g.fillRoundedRectangle (cx - 4.5f, float (cell.getY()), 9.0f, float (cell.getHeight()), 3.0f);
        if (! compact)
        {
            g.setColour (dim ? Dine::ink4 : Dine::ink2);
            g.setFont (Dine::mono (11.0f, 500));
            g.drawText ((faderDb >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (faderDb), 1),
                        juce::Rectangle<int> (cell.getRight() + 8, cell.getY(), 52, cell.getHeight()), juce::Justification::centredRight);
        }
    }

    // the row-height grip
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (float (area.getRight() - 34), float (row.getBottom()) - 2.5f, 22.0f, 1.0f);
}

void TracksPage::paintLane (juce::Graphics& g, int track, juce::Rectangle<int> area)
{
    const auto& session = controller.getSession();
    const auto& project = services.daw().getProject();
    const auto& state = project.tracks[size_t (track)];
    const auto colour = laneColourFor (session.inputs[size_t (track)].role);

    const auto& params = controller.getBase();
    bool anySolo = false;
    for (int i = 0; i < params.numStrips; ++i) anySolo = anySolo || params.strips[size_t (i)].solo;
    const bool inRange = track < params.numStrips;
    const bool dim = inRange && (params.strips[size_t (track)].mute
                                 || (anySolo && ! params.strips[size_t (track)].solo));

    // The lane is the same plane as its header, with the same 2 px of ground under it.
    g.setColour (Dine::window);
    g.fillRect (area);
    g.setColour (selection.track == track ? Dine::card : Dine::console);
    g.fillRect (area.withTrimmedBottom (2));

    for (int i = 0; i < int (state.clips.size()); ++i)
    {
        const auto& clip = state.clips[size_t (i)];
        const int x1 = sampleToX (clip.start);
        const int x2 = sampleToX (clip.end());
        if (x2 < area.getX() - 2 || x1 > area.getRight() + 2) continue;

        auto box = juce::Rectangle<int> (x1, area.getY() + 4, juce::jmax (2, x2 - x1), area.getHeight() - 10);
        const bool selected = selection.track == track && selection.index == i;
        const bool named = box.getHeight() > 22 && box.getWidth() > 44;
        const auto tint = dim ? Dine::ink4 : colour;

        // A clip is a plane of its group's colour, the waveform dark on it, the name dark in
        // the corner - the way the design draws one.
        Dine::fillRounded (g, box.toFloat(), tint.withMultipliedSaturation (dim ? 0.0f : 1.0f).withAlpha (selected ? 1.0f : 0.85f), Dine::Radius::chip);
        auto wave = box.reduced (2, 3).withTrimmedTop (named ? 14 : 0);
        if (auto* thumb = thumbnailFor (clip); thumb != nullptr && wave.getHeight() > 4)
        {
            const double rate = juce::jmax (1.0, clip.fileSampleRate > 0.0 ? clip.fileSampleRate : project.sampleRate);
            const double from = double (clip.offset) / rate;
            const double to = from + double (clip.length) / juce::jmax (1.0, project.sampleRate);
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (wave);
            g.setColour (juce::Colours::black.withAlpha (dim ? 0.25f : 0.42f));
            thumb->drawChannels (g, wave, from, to, 0.95f);
        }
        if (named)
        {
            g.setColour (juce::Colours::black.withAlpha (0.65f));
            g.setFont (Dine::text (10.0f));
            g.drawText (clip.name.isEmpty() ? juce::String (session.inputs[size_t (track)].name) : clip.name,
                        box.reduced (7, 0).withTrimmedTop (2).withHeight (14), juce::Justification::centredLeft, true);
        }
        if (selected)
        {
            g.setColour (Dine::ink);
            g.drawRoundedRectangle (box.toFloat().reduced (0.5f), Dine::Radius::chip, 1.0f);
            g.fillRect (box.getX() + 2, box.getY() + 3, 2, box.getHeight() - 6);
            g.fillRect (box.getRight() - 4, box.getY() + 3, 2, box.getHeight() - 6);
        }
    }

    if (state.clips.empty() && state.armed)
    {
        g.setColour (Dine::crit.withAlpha (0.55f));
        g.setFont (Dine::caps (10.0f, 0.08f));
        g.drawText ("TO RECORD", area.reduced (12, 0).withWidth (96), juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------- layout
void TracksPage::updateToolbar()
{
    for (int i = 0; i < 3; ++i)
        rowTabs[size_t (i)]->setToggleState (int (rowHeight) == i, juce::dontSendNotification);
    snapButton->setToggleState (snap, juce::dontSendNotification);
    followButton->setToggleState (follow, juce::dontSendNotification);
    recordAllButton->setStyle (DineButton::Style::Toggle);
    recordAllButton->setQuiet (true);
    recordAllButton->setToggleState (allSetToRecord(), juce::dontSendNotification);
    repaint (toolbarArea());
}

void TracksPage::resized()
{
    auto row = toolbarArea().reduced (18, 0).withSizeKeepingCentre (juce::jmax (100, getWidth() - 36), Dine::Metric::control);

    auto seg = [&row] (std::initializer_list<DineButton*> buttons, int minW)
    {
        int x = row.getX() + 2;
        for (auto* b : buttons)
        {
            const int w = juce::jmax (minW, b->idealWidth());
            b->setBounds (x, row.getY() + 2, w, row.getHeight() - 4);
            x += w;
        }
        row.removeFromLeft (x + 2 - row.getX() + 10);
    };
    seg ({ rowTabs[0].get(), rowTabs[1].get(), rowTabs[2].get() }, 28);
    seg ({ snapButton.get(), followButton.get() }, 52);
    auto fromLeft = [&row] (DineButton& b, int minWidth)
    {
        b.setBounds (row.removeFromLeft (juce::jmax (minWidth, b.idealWidth())).reduced (0, 1));
        row.removeFromLeft (6);
    };
    fromLeft (*splitButton, 60);
    fromLeft (*markerButton, 60);
    fromLeft (*recordAllButton, 60);

    auto fromRight = [&row] (DineButton& b, int minWidth)
    {
        b.setBounds (row.removeFromRight (juce::jmax (minWidth, b.idealWidth())).reduced (0, 1));
        row.removeFromRight (6);
    };
    fromRight (*zoomFitButton, 40);
    fromRight (*zoomInButton, 30);
    fromRight (*zoomOutButton, 30);

    chainStrip.setBounds (getLocalBounds().removeFromBottom (footHeight()));
    clampScroll();
}

// ---------------------------------------------------------------- mouse
void TracksPage::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (p.y < kToolbarHeight) return;

    // ---- the divider between the channel panel and the timeline
    // The standard DAW gesture: drag it and every row gets wider together, so long channel
    // names become readable without touching a single track. It is grabbed before anything
    // else because it sits on top of both the header's right edge and the lane's left one.
    if (onDivider (p))
    {
        drag = Drag::PanelWidth;
        dragStartX = p.x;
        dragStartHeaderWidth = headerWidth;
        return;
    }

    // ---- the ruler: the loop strip along its top, the marker lane inside it, the ticks below
    if (p.y < lanesTop())
    {
        if (p.x < headerWidth) return;
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

        if (const int hit = markerAt (p); hit >= 0)
        {
            if (e.mods.isPopupMenu()) { markerMenu (hit); return; }
            services.daw().locate (project.markers[size_t (hit)].position);
            drag = Drag::Marker;
            dragMarker = hit;
            undoPushed = false;
            repaint();
            return;
        }

        // While a take is running the playhead is the recording's, not the mouse's.
        if (services.daw().isRecording())
        {
            if (onToast) onToast ("The playhead follows the recording. Stop recording to move it.");
            return;
        }
        drag = Drag::Playhead;
        services.daw().getTransport().setPosition (snapSample (xToSample (p.x), -1, -1));
        repaint();
        return;
    }

    if (p.x < headerWidth)
    {
        const int track = trackAtY (p.y);
        if (track < 0) return;
        // Right-click is the header's own menu: the name, the source and the assignments.
        if (e.mods.isPopupMenu()) { headerMenu (track); return; }
        const int bottom = trackTop (track) + trackHeight (track);
        if (p.y >= bottom - kResizeGrip)
        {
            drag = Drag::TrackHeight;
            dragTrack = track;
            dragStartY = p.y;
            dragStartHeight = trackHeight (track);
            return;
        }

        // The quick fader, before anything else claims the click.
        if (const auto cell = faderCell (track); ! cell.isEmpty() && cell.expanded (0, 4).contains (p))
        {
            if (controller.isBypassed()) { if (onToast) onToast ("BYPASS is on: the faders are the console's while you compare."); return; }
            selection = { track, -1 };
            drag = Drag::Fader;
            dragTrack = track;
            dragStartX = p.x;
            dragFaderNorm = faderRange().convertTo0to1 (
                juce::jlimit (-60.0f, 12.0f, controller.getBase().strips[size_t (track)].faderDb));
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

        // A click on the header picks that channel out - the chain strip along the foot reads
        // it, and the fader, the keys and the menu are all right there. Leaving the timeline
        // is a bigger move than a single click, so the Inspector waits for a double-click.
        //
        // The same press, dragged up or down, rearranges the channels. Nothing happens until
        // the pointer has actually travelled (kOrderGrip), so a click that wanders by a pixel
        // still just selects.
        selection = { track, -1 };
        drag = Drag::TrackOrder;
        dragOrderFrom = track;
        dragOrderSlot = -1;
        dragOrderLifted = false;
        dragStartY = p.y;
        updateChainStrip();
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
        case Drag::PanelWidth:
            // One width for the whole panel, live while dragging. Every row inherits it, the
            // timeline takes whatever is left, and the clips stay where they are in time
            // because the scroll is measured from the panel's edge.
            setPanelWidth (dragStartHeaderWidth + (p.x - dragStartX));
            break;

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

        case Drag::Fader:
            dragFader (dragTrack, p.x, e.mods.isShiftDown());
            break;

        case Drag::TrackOrder:
        {
            if (! dragOrderLifted && std::abs (p.y - dragStartY) < kOrderGrip) break;
            if (! dragOrderLifted && project.liveSafe) { drag = Drag::None; locked(); break; }
            dragOrderLifted = true;
            // Dragging past the top or the bottom of the lanes brings the rest of the session
            // into view, so a channel can travel further than one screenful.
            const auto lanes = lanesArea();
            if (p.y < lanes.getY() + kOrderEdge)          scrollY -= kOrderScroll;
            else if (p.y > lanes.getBottom() - kOrderEdge) scrollY += kOrderScroll;
            clampScroll();
            dragOrderSlot = dropSlotAtY (p.y);
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

    if (drag == Drag::PanelWidth && onPanelWidthChanged) onPanelWidthChanged();

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

    if (drag == Drag::TrackOrder)
    {
        // The slot counts the list as it stands, so dropping below the row it came from lands
        // one place higher once that row has been lifted out.
        const int from = dragOrderFrom, slot = dragOrderSlot;
        const bool lifted = dragOrderLifted;
        drag = Drag::None;
        dragOrderFrom = dragOrderSlot = -1;
        dragOrderLifted = false;
        if (lifted && slot >= 0 && from >= 0) moveTrack (from, slot > from ? slot - 1 : slot);
        repaint();
        return;
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

    // A track header: double-click opens that channel in the Inspector. The controls on the
    // header keep their own single clicks - a double-click on the fader or a key is two of
    // those, not a request to leave the timeline.
    if (p.x < headerWidth && p.y >= lanesTop())
    {
        const int track = trackAtY (p.y);
        if (track < 0) return;
        if (p.y >= trackTop (track) + trackHeight (track) - kResizeGrip) return;
        // Double-click a fader for unity, exactly as the mixer's faders do.
        if (const auto cell = faderCell (track); ! cell.isEmpty() && cell.expanded (0, 4).contains (p))
        {
            if (! controller.isBypassed()) { controller.setStripFader (track, 0.0f); repaint(); }
            return;
        }
        for (int k = 0; k < 4; ++k) if (keyCell (track, k).contains (p)) return;
        selection = { track, -1 };
        updateChainStrip();
        if (onOpenStrip) onOpenStrip (track);
        repaint();
        return;
    }

    if (! markerArea().contains (p)) return;
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

    const bool overDivider = onDivider (p);
    if (overDivider != dividerHot) { dividerHot = overDivider; repaint (headerWidth - 6, kToolbarHeight, 12, getHeight()); }

    if (overDivider)
        cursor = juce::MouseCursor::LeftRightResizeCursor;
    else if (p.y >= kToolbarHeight && p.y < kToolbarHeight + kLoopStrip && p.x >= headerWidth)
        cursor = juce::MouseCursor::LeftRightResizeCursor;
    else if (hoverMarker >= 0)
        cursor = juce::MouseCursor::PointingHandCursor;
    else if (p.x < headerWidth && p.y >= lanesTop())
    {
        const int track = trackAtY (p.y);
        if (track >= 0 && p.y >= trackTop (track) + trackHeight (track) - kResizeGrip)
            cursor = juce::MouseCursor::UpDownResizeCursor;
        else if (track >= 0)
            if (const auto cell = faderCell (track); ! cell.isEmpty() && cell.expanded (0, 4).contains (p))
                cursor = juce::MouseCursor::LeftRightResizeCursor;
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

juce::String TracksPage::getTooltip()
{
    const auto p = getMouseXYRelative();
    const int track = trackAtY (p.y);
    if (p.x >= headerWidth || track < 0 || track >= numTracks()) return {};

    for (int k = 0; k < 4; ++k)
    {
        if (! keyCell (track, k).contains (p)) continue;
        switch (k)
        {
            case 0:  return "R - record this track. Press Record and every track with its R on is captured, raw, "
                            "to its own file. Engineers call this arming.";
            case 1:  return "Monitoring. A: you hear the input unless the timeline is playing this track back. "
                            "I: always the input. " + Glyph::dash() + ": never.";
            case 2:  return "Mute: this source is not heard.";
            default: return "Solo: hear this source alone.";
        }
    }
    if (faderCell (track).contains (p)) return "Level for this track. Double-click for 0.0 dB.";
    // The reorder is a gesture with nothing drawn to advertise it, so the header itself says so.
    if (numTracks() > 1)
        return juce::String (controller.getSession().inputs[size_t (track)].name)
             + " - drag up or down to move this channel. The mixer and the Inspector follow.";
    return {};
}

// Pinch on the trackpad: zoom the timeline, about the fingers.
void TracksPage::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (scaleFactor <= 0.0f) return;
    zoomAround (e.getPosition().x, juce::jlimit (0.5, 2.0, double (scaleFactor)));
}

void TracksPage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isAltDown())
    {
        zoomAround (e.getPosition().x, wheel.deltaY > 0 ? 1.15 : 1.0 / 1.15);
        return;
    }
    scrollY = juce::jmax (0, scrollY - int (wheel.deltaY * 90.0f));
    scrollX = juce::jmax (0.0, scrollX - wheel.deltaX * 140.0);
    clampScroll();
    repaint();
}

} // namespace livemix
