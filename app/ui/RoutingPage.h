#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "OutputsSheet.h"
#include "native/InputMapStore.h"

namespace livemix
{

// ROUTING: everything about where the sound comes from and where it goes, in one place, and
// out of the way of the console.
//
// Until now the set-up rows lived in the everyday sidebar beside TRACKS and MIXER - a
// volunteer looking for the fader for the pastor's microphone had "Audio device" and "Inputs"
// in the same list, one click from changing what the console is. The output feeds were a
// sheet in a menu, the saved patches were a menu inside a menu, and nobody could have told
// you where "set-up" ended.
//
// So there is one workspace, reached deliberately from the sidebar's Setup group or the View
// menu, with its sections as a segmented control at the top right (design: `02 - Audio
// device`, 70:9274): the device, the inputs, the outputs and the engineer's listen, and the
// patches this church has saved. The first two are the pages that already existed - this is
// where they are now, not a second copy of them (the window lays each one into
// `contentBounds()`); the last is here because a list of patches deserves a list rather than
// a submenu. What the mix is *for* is not routing, so Purpose and sound is a row of its own.
//
// LIVE SAFE. Nothing on this workspace is small: the device, the patch and the output feeds
// are the three ways to silence a room in the middle of a service. So while LIVE SAFE is on,
// the workspace is covered until somebody says out loud that they mean it, once per visit.
class RoutingPage : public juce::Component
{
public:
    enum class Section { Device = 0, Inputs, Outputs, Maps, Count };

    RoutingPage (MixController&, AppServices&);
    ~RoutingPage() override;

    std::function<void (Section)> onSection;            // the window shows the page for it
    // The cover went up or came down: the window owns the three pages this hosts, so it is
    // the one that can give them their size back (or take it away).
    std::function<void()> onCoverChanged;
    std::function<void (const juce::String&)> onToast;
    std::function<void (const juce::File&)> onApplyMap;  // a saved patch onto this session
    std::function<void()> onSaveMap;                    // this session's patch, saved
    std::function<void()> onImportMap;
    std::function<void (const juce::String& device)> onChooseOutputDevice;

    void setSection (Section);
    Section getSection() const noexcept { return section; }
    static const char* sectionName (Section) noexcept;

    // Where the hosted page goes. The window owns DevicePage and AssignPage but they are
    // children of *this*, so the segmented control at the top right stays over them.
    juce::Rectangle<int> contentBounds() const;
    // The window hands the two pages it owns over to be parented here.
    void host (juce::Component&);
    int headHeight() const noexcept;
    // Does the window have a page of its own to put there, or does ROUTING fill it?
    bool hostsAPage() const noexcept { return section <= Section::Inputs; }

    // LIVE SAFE: the cover is up until somebody says they mean it, and it goes back up every
    // time the workspace is left. Nothing underneath can be reached while it is.
    bool isCovered() const;
    void resetConfirmation();
    void confirmForTest();          // what pressing "I know what I am doing" does

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class MapRow;

    void rebuildMaps();
    void layoutMaps();
    int mapsHeight() const;
    void showMapMenu (const juce::File&, const juce::String& name);
    void askForName (const juce::String& title, const juce::String& current,
                     std::function<void (const juce::String&)> done);

    MixController& controller;
    AppServices& services;
    Section section = Section::Device;
    bool confirmed = false;
    bool coverShown = false;      // what the last layout drew, so refresh() only acts on a change

    DineSegmentRow segments;                                              // the track they sit in
    std::array<std::unique_ptr<DineChip>, size_t (Section::Count)> nav;   // the segmented control, top right
    std::unique_ptr<OutputsSheet> outputs;

    // the patches this church has saved
    juce::Viewport mapsView;
    juce::Component mapsHolder;
    std::vector<std::unique_ptr<MapRow>> mapRows;
    juce::Array<InputMapStore::Listing> maps;
    DineButton saveMapButton { "Save this session's patch", DineButton::Style::Filled };
    DineButton importMapButton { "Import a patch", DineButton::Style::Standard };
    DineButton unlockButton { "I know what I am doing", DineButton::Style::Standard };
    juce::String lastDevice;
    std::unique_ptr<juce::AlertWindow> nameDialog;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace livemix
