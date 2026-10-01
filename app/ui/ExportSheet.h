#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// EXPORT (design: `27 - Export`, 88:23516).
//
// Bounced offline from the kept mix. What the room and the stream hear is not touched: the
// export is a render of the document, not a recording of the output, so it can run while the
// service is still going and it can run faster than real time.
//
// What is asked, in the order the design asks it: what to write, how much of it, in which
// format, and where to put it - with how long the render will take printed under them, because
// "about twenty seconds" is the difference between waiting and thinking it has hung.
//
// WHAT: the stereo mix as one file, a group stem per group bus, or the raw multitrack - one
// file per input, straight off the disk with nothing in the way. Stems and a multitrack are a
// folder rather than a file, so the sheet says so before it writes one.
//
// LOUDNESS is a stereo mix's alone, and it is one gain over the whole render measured from the
// render itself: a set of parts whose levels moved apart from each other would stop being
// parts of the same thing. "As mixed" is the default and the honest answer - the mix was built
// to a target once, in Purpose and sound, and it already carries it.
class ExportSheet : public juce::Component
{
public:
    ExportSheet (MixController&, AppServices&);
    ~ExportSheet() override;

    // Opens on what the menu item said: File > Export Multitrack is the multitrack, not a
    // stereo mix with the multitrack one tab away.
    void choose (AppServices::ExportWhat, AppServices::ExportFormat);

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onToast;
    // The window owns the render: it outlives this sheet, and it is the thing that knows
    // whether one is already running.
    struct Request
    {
        juce::File dest;
        AppServices::ExportFormat format = AppServices::ExportFormat::Wav;
        AppServices::ExportWhat what = AppServices::ExportWhat::StereoMix;
        AppServices::ExportLoudness loudness = AppServices::ExportLoudness::AsMixed;
        juce::int64 from = 0, to = 0;
    };
    std::function<void (const Request&)> onExport;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Range { Whole = 0, Loop, BetweenMarkers };

    juce::Rectangle<int> cardBounds() const;
    juce::int64 fromSample() const;
    juce::int64 toSample() const;
    juce::String rangeNote() const;
    juce::String whatNote() const;           // what the export will actually leave on the disk
    juce::File destination() const;
    bool writesFolder() const noexcept { return what != 0; }
    void updateControls();

    MixController& controller;
    AppServices& services;

    Range range = Range::Whole;
    int what = 0;                           // 0 stereo mix, 1 group stems, 2 raw multitrack
    int format = 0;                         // 0 WAV, 1 AIFF, 2 MP3 320
    int loudness = 0;                       // 0 as mixed, 1 stream -14, 2 podcast -16
    std::array<std::unique_ptr<DineButton>, 3> whatTabs;
    std::array<std::unique_ptr<DineButton>, 3> rangeTabs;
    std::array<std::unique_ptr<DineButton>, 3> formatTabs;
    std::array<std::unique_ptr<DineButton>, 3> loudnessTabs;
    DineSegmentRow whatTrack, rangeTrack, formatTrack, loudnessTrack;
    DineButton exportButton { "Export", DineButton::Style::Filled };
    DineButton cancelButton { "Cancel", DineButton::Style::Standard };
    DineButton folderButton { "Choose", DineButton::Style::Standard };
    juce::File folder;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace livemix
