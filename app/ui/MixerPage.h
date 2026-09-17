#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "ChainStrip.h"

namespace livemix
{

// The console: every strip, the group buses and the master, with a meter, fader, pan, the
// record / monitoring / mute / solo keys, the inserts and the sends. The same session as
// TRACKS, TUNE and LIVE - switching views never changes the sound.
//
//   STRIPS  a bank of columns at three widths, the master pinned on the right;
//   LIST    one row per source, so names, levels and keys line up down the page;
//   and a filter (everything / only the inputs / only the groups and the master).
//
// Performance is the design of this page as much as its look. Forty-eight strips at 30 Hz
// cannot rebuild their text, their inserts or their gain advice every tick, so a strip only
// re-reads the things that are atomics every frame (meters, mute, solo, fader), re-reads the
// chain when the parameters actually changed (a hash of them, not a string of them) and
// re-reads the gain advice twice a second. A meter is one gradient fill, whatever its
// height. A column is opaque and cached as an image, so a swipe across the console is a blit.
class MixerPage : public juce::Component
{
public:
    enum class View  { Strips = 0, List };
    enum class Size  { Narrow = 0, Normal, Wide };
    enum class Show  { All = 0, Inputs, Groups };

    MixerPage (MixController&, AppServices&);
    ~MixerPage() override;

    std::function<void (int strip)> onOpenStrip;
    std::function<void (int strip)> onTuneStrip;
    std::function<void (MixBus bus)> onOpenBus;
    std::function<void()> onOpenWindow;
    std::function<void()> onOpenAssign;               // "Fix the assignments..." from a strip's menu
    std::function<void (const juce::String&)> onToast;

    void setView (View);
    View getView() const noexcept { return view; }
    void setStripSize (Size);
    Size getStripSize() const noexcept { return stripSize; }
    void setShow (Show);
    Show getShow() const noexcept { return show; }
    void setSendsVisible (bool);
    bool sendsVisible() const noexcept { return showSends; }

    void selectStrip (int strip);
    void selectBus (MixBus);
    int selectedStrip() const noexcept { return selected; }
    MixBus selectedBus() const noexcept { return selectedBusValue; }

    void setWindowButtonVisible (bool);
    // The chain along the foot belongs to the window now; the detached mixer keeps its own.
    void setFootShown (bool);

    void refresh();                          // 30 Hz
    void rebuild();                          // after the session / graph changed
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Strip;
    class Bank;

    Strip* masterStrip() const;
    void layoutStrips();
    void updateChainStrip();
    void layoutList();
    void updateControls();
    bool visibleInFilter (const Strip&) const;
    int footHeight() const noexcept { return footShown ? ChainStrip::height : 0; }

    MixController& controller;
    AppServices& services;

    juce::Viewport viewport;
    std::unique_ptr<Bank> bank;
    ChainStrip chainStrip;
    std::vector<std::unique_ptr<Strip>> strips;
    int builtForStrips = -1;

    View view = View::Strips;
    Size stripSize = Size::Normal;
    Show show = Show::All;
    bool showSends = true, footShown = true;
    int selected = -1;
    MixBus selectedBusValue = MixBus::Count;
    int tick = 0;                    // so the gain advice is re-read twice a second, not thirty times
    int adviceForTune = -1;

    std::array<std::unique_ptr<DineButton>, 2> viewTabs;
    std::array<std::unique_ptr<DineButton>, 3> sizeTabs;
    std::array<std::unique_ptr<DineButton>, 3> showTabs;
    DineButton sendsButton { "Sends", DineButton::Style::Toggle };
    DineButton clearSolos { "Clear solo", DineButton::Style::Segment };
    DineButton windowButton { "Open in a new window", DineButton::Style::Standard };
    bool windowButtonWanted = true;
};

} // namespace livemix
