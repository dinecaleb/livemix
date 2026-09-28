#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "UI/Widgets.h"

namespace livemix
{

// EXPORT (the v2 design, frame "18").
//
// Bouncing the service out is four questions - what, how much of it, what format, and how
// loud - and until now it was two menu items that asked none of them and exported the whole
// recording in one go. A church that streams needs the part between "Welcome" and "Altar
// call", at the loudness the platform normalises to, and it needs to be told what it is
// about to get before it waits ten minutes for it.
//
// The sheet asks the four and then says, in one sentence, exactly what is about to be
// written. Nothing here is new machinery: `MixBounce::renderProject` already renders a range
// of the timeline through the kept mix, offline, block by block.
//
// GROUP STEMS AND THE RAW MULTITRACK are not offered. They are in the design and they are
// honest work - an offline render per group and per input - but they do not exist yet, and a
// control that does nothing is worse than one that is missing. See
// docs/DESIGN-IMPLEMENTATION.md.
class ExportSheet : public juce::Component
{
public:
    ExportSheet (MixController&, AppServices&);
    ~ExportSheet() override;

    std::function<void()> onClose;
    // Everything chosen: render this range, in this format, to a file the host asks for.
    std::function<void (AppServices::ExportFormat, juce::int64 from, juce::int64 to)> onExport;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::Rectangle<int> cardBounds() const;
    // What the sheet would export right now, in the words it prints. For the test.
    juce::String summary() const;

private:
    struct Range { juce::String name; juce::int64 from = 0, to = 0; };
    std::vector<Range> ranges;      // the whole recording, then every pair of markers
    int range = 0;
    int format = 0;                 // 0 = WAV, 1 = MP3

    void rebuildRanges();
    juce::String lengthText (const Range&) const;

    MixController& controller;
    AppServices& services;

    std::array<std::unique_ptr<DineButton>, 2> formatTabs;
    DinePopup rangePicker;
    DineButton exportButton { "Export", DineButton::Style::Filled };
    DineButton closeButton { "Close", DineButton::Style::Standard };

    juce::Rectangle<int> rangeRow, formatRow, loudnessRow, sentenceRow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportSheet)
};

} // namespace livemix
