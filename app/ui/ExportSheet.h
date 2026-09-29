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
// WHAT IT DOES NOT OFFER, and why. The design also draws Group stems and Raw multitrack, and
// a loudness choice on the way out. DLIVE renders the master bus, so those are not written
// yet; they are named in docs/DESIGN-V3.md as the gap rather than drawn here as controls that
// do nothing. The loudness the mix lands at is set once, in Purpose and sound, and the export
// carries it - which is the same answer, made in one place.
class ExportSheet : public juce::Component
{
public:
    ExportSheet (MixController&, AppServices&);
    ~ExportSheet() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onToast;
    // The window owns the render: it outlives this sheet, and it is the thing that knows
    // whether one is already running.
    std::function<void (const juce::File&, AppServices::ExportFormat, juce::int64 from, juce::int64 to)> onExport;

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
    juce::File destination() const;
    void updateControls();

    MixController& controller;
    AppServices& services;

    Range range = Range::Whole;
    int format = 0;                         // 0 WAV, 1 MP3 320
    std::array<std::unique_ptr<DineButton>, 3> rangeTabs;
    std::array<std::unique_ptr<DineButton>, 2> formatTabs;
    DineSegmentRow rangeTrack, formatTrack;
    DineButton exportButton { "Export", DineButton::Style::Filled };
    DineButton cancelButton { "Cancel", DineButton::Style::Standard };
    DineButton folderButton { "Choose…", DineButton::Style::Standard };
    juce::File folder;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace livemix
