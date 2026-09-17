#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppTheme.h"
#include "native/MixController.h"

namespace livemix
{

// The channel list, beside every workspace.
//
// Before the 2026-09 revamp the window carried two navigations: a 224 px sidebar of setup
// steps that was permanently in the way once setup was done, and a row of workspace tabs in
// the toolbar. The tabs won. What takes the sidebar's place is this - not navigation at all,
// but the session itself: every channel under its group, searchable, filterable, with a live
// meter and whatever DLIVE has to say about its level. It is *session-scoped*, so it reads
// the same on TRACKS, MIXER, TUNE and CHANNEL, and picking a channel here picks it out on
// whichever workspace is up.
//
// It folds to a 17 px handle with its name written down it, the way every other panel in the
// app does, so the middle of the window can have the width.
//
// Performance: the rows are painted, not built. A console of forty-eight channels is one
// component and one image, so a 30 Hz tick costs a fill of the meter cells that actually
// moved (`Row::look`), never a wall of child components laying themselves out.
class ChannelRail : public juce::Component
{
public:
    explicit ChannelRail (MixController&);
    ~ChannelRail() override;

    // What a click does. The host decides - a click picks the channel out on the workspace
    // that is up, a double-click opens it in the Inspector.
    std::function<void (int strip)> onSelect;
    std::function<void (int strip)> onOpen;
    std::function<void (MixBus)> onSelectBus;
    std::function<void (MixBus)> onOpenBus;
    std::function<void (int strip)> onTune;       // TUNE CHANNEL, from the row's menu
    std::function<void()> onCollapsedChanged;

    void rebuild();                               // the graph changed
    void refresh();                               // 30 Hz: meters and level advice
    void setSelected (int strip);                 // -1 = nothing picked out
    void setSelectedBus (MixBus);
    int getSelected() const noexcept { return selected; }

    void setCollapsed (bool);
    bool isCollapsed() const noexcept { return collapsed; }
    static int widthFor (bool collapsed) noexcept
    {
        return collapsed ? Dine::Metric::railHandle : Dine::Metric::chanRail;
    }
    int width() const noexcept { return widthFor (collapsed); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class List;
    class Sticky;
    friend class List;

    enum class Filter { All, Inputs, Groups };

    struct Entry
    {
        bool header = false;          // a group heading rather than a channel
        bool isBus = false;           // the row is a group bus, not an input
        int strip = -1;
        MixBus bus = MixBus::Master;
        juce::String name, sub, group;
        Dine::Icon icon = Dine::Icon::Mic;
        juce::Colour tint { Dine::ink2 };
        int count = 0;                // headers only
        // What the last tick measured. Compared before a repaint, so a still console is free.
        float peak = -120.0f;
        int attention = 0;            // 0 none, 1 worth a look, 2 wrong now
        char flag = ' ';
        juce::Colour flagColour { Dine::ink4 };
    };

    void buildEntries();
    void applyFilter();
    int rowHeight (const Entry& e) const noexcept { return e.header ? 24 : 32; }
    int indexAt (juce::Point<int> inList) const;
    void clicked (int index, bool doubleClick);
    void rowMenu (int index);

    MixController& controller;
    std::vector<Entry> entries;       // everything
    std::vector<int> shown;           // indices into `entries` after search + filter

    std::unique_ptr<List> list;
    std::unique_ptr<Sticky> sticky;
    juce::Viewport view;
    juce::TextEditor search;
    std::unique_ptr<DineChip> filters[3];
    std::unique_ptr<DineButton> collapseButton;
    std::unique_ptr<DinePanelTab> handle;

    Filter filter = Filter::All;
    int selected = -1;
    MixBus selectedBus = MixBus::Master;
    bool busSelected = false;
    bool collapsed = false;
    int builtFor = -1;
    int adviceTicks = 0, adviceFor = -1;
};

} // namespace livemix
