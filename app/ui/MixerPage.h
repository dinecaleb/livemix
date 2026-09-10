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

// One-screen console: every strip, group bus and the master with a meter, fader, pan,
// record arm, input monitoring, mute and solo. The same session as Tracks, Tune and
// Live — switching views never changes the sound.
//
// The console can be read three ways, because 24 inputs do not fit one shape:
//   STRIPS  the classic vertical bank, at three widths (a narrow bank shows a whole
//           band at once, a wide one shows every label in full);
//   LIST    one row per source, so names, levels and keys line up down the page and
//           nothing is ever off the right-hand edge;
//   and a filter (everything / only the inputs / only the groups and the master).
// The bank is banded by group bus, so you always know what you are looking at.
class MixerPage : public juce::Component
{
public:
    enum class View  { Strips = 0, List };
    enum class Size  { Narrow = 0, Normal, Wide };
    enum class Show  { All = 0, Inputs, Groups };

    MixerPage (MixController&, AppServices&);
    ~MixerPage() override;

    std::function<void (int strip)> onOpenStrip;
    std::function<void (int strip)> onTuneStrip;       // TUNE CHANNEL: listen to this source and tune it on its own
    std::function<void (MixBus bus)> onOpenBus;
    std::function<void()> onOpenWindow;                 // "open the mixer in its own window"
    std::function<void (const juce::String&)> onToast;

    void setView (View);
    View getView() const noexcept { return view; }
    void setStripSize (Size);
    Size getStripSize() const noexcept { return stripSize; }
    void setShow (Show);
    Show getShow() const noexcept { return show; }
    void setSendsVisible (bool);
    bool sendsVisible() const noexcept { return showSends; }

    // Pick a strip out: the foot of the page then reads its chain. This is what a click on a
    // strip does, and what the Inspector uses to keep the two views pointing at one source.
    void selectStrip (int strip);
    void selectBus (MixBus);
    int selectedStrip() const noexcept { return selected; }   // what the foot reads, and what TUNE CHANNEL tunes

    // The detached window is already its own window: it does not offer the button again.
    void setWindowButtonVisible (bool);

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

    MixController& controller;
    AppServices& services;

    juce::Viewport viewport;
    std::unique_ptr<Bank> bank;
    ChainStrip chainStrip;
    std::vector<std::unique_ptr<Strip>> strips;          // inputs, then each group bus, then the master
    int builtForStrips = -1;

    View view = View::Strips;
    Size stripSize = Size::Normal;
    Show show = Show::All;
    bool showSends = true;
    int selected = -1;                                   // the strip whose chain the foot shows
    MixBus selectedBus = MixBus::Count;

    std::array<std::unique_ptr<DineButton>, 2> viewTabs;
    std::array<std::unique_ptr<DineButton>, 3> sizeTabs;
    std::array<std::unique_ptr<DineButton>, 3> showTabs;
    DineButton sendsButton { "Sends", DineButton::Style::Standard };
    DineButton clearSolos { "Clear solos", DineButton::Style::Standard };
    DineButton windowButton { "Open in a window", DineButton::Style::Standard };
    bool windowButtonWanted = true;
};

} // namespace livemix
