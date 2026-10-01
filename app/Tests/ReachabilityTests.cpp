// REACHABILITY: everything a person can reach today, written down and asserted.
//
// This is the no-regression guard for the v2 design work. The design moves things - the
// set-up rows leave the sidebar for a ROUTING workspace, Purpose becomes a sheet, the tabs
// gain a sixth - and moving is allowed. Removing is not. So every menu item and its command
// id, every keyboard shortcut, every workspace and set-up page, and every sheet and the call
// that opens it is listed below, and this test says each one is still there and still does
// the same thing.
//
// The lists are deliberately literal. A test that asked the window what it had would agree
// with the window however much the window lost; these tables were read off the application as
// it stood at the end of Phase 1 (commit d1d96ce), and a later phase that wants to change one
// has to come here and say so.
//
// Nothing here runs a command. `commandForKey` is a pure lookup and the menu model only
// builds a juce::PopupMenu, so no file chooser and no modal alert can open in a test run.
#include "TestFramework.h"
#include "native/DawEngine.h"
#include "native/MixController.h"
#include "native/SampleLibrary.h"
#include "ui/MainView.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <set>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 128;
    constexpr int kInputs = 16;

    // The outside world, faked: no device, no disk, no chooser. The snapshot tool has a
    // fuller one; this needs only enough for the window to build and draw.
    class FakeServices : public AppServices
    {
    public:
        FakeServices (MixController& c, DawEngine& d) : controller (c), dawEngine (d) {}

        DawEngine& daw() override { return dawEngine; }
        juce::Array<Device> inputDevices() override { juce::Array<Device> a; a.add ({ "Console", 32, 32 }); return a; }
        juce::Array<Device> outputDevices() override { juce::Array<Device> a; a.add ({ "Console", 32, 32 }); return a; }
        juce::String openDevices (const juce::String& in, const juce::String& out) override { input = in; output = out; running = true; outputOnly = false; return {}; }
        juce::String openOutputOnly (const juce::String& out) override { input = {}; output = out; running = true; outputOnly = true; return {}; }
        juce::String changeOutput (const juce::String& out) override { output = out; return {}; }
        bool isAudioRunning() override { return running; }
        int numInputChannels() override { return running && ! outputOnly ? kInputs : 0; }
        int numOutputChannels() override { return running ? 8 : 0; }
        double sampleRate() override { return kSr; }
        int bufferSize() override { return kBlock; }
        int xrunCount() override { return 0; }
        void reconfigure() override
        {
            controller.prepare (kSr, kBlock);
            dawEngine.setSession (controller.getSession());
            dawEngine.prepare (kSr, kBlock);
        }
        // The two halves of how a session is written down, counted rather than done. A UI edit
        // must move the document's revision (touchSession); writing the file itself belongs to
        // File > Save and to replacing the document, and to nothing else.
        void touchSession() override { ++touches; }
        unsigned long long sessionRevision() override { return touches; }
        bool saveSession() override { ++saves; return true; }
        unsigned long long touches = 0, saves = 0;
        void newSession() override {}
        juce::String saveSessionAs (const juce::String& name) override { sessionName = name; return {}; }
        juce::String loadSession (const juce::File&) override { return {}; }
        juce::Array<SessionStore::Listing> listSessions() override { return {}; }
        juce::String currentInputDevice() override { return input; }
        juce::String currentOutputDevice() override { return output; }
        juce::String currentSessionName() override { return sessionName; }
        juce::File sessionFolder() override { return dawEngine.getProject().folder; }
        juce::String importMultitrack (const juce::File&) override { return "not here"; }
        std::shared_ptr<const ExportJob> snapshotExport() override { return {}; }
        juce::String exportMix (std::shared_ptr<const ExportJob>, const juce::File&, ExportFormat,
                                std::function<bool (float)>) override { return "not here"; }

    private:
        MixController& controller;
        DawEngine& dawEngine;
        bool running = false, outputOnly = false;
        juce::String input, output, sessionName { "Reachability" };
    };

    // A window, with a band on it so the menu items that depend on a channel being there are
    // built in the state a user would meet them in.
    struct Window
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        SampleLibrary samples;
        MixController controller;
        DawEngine dawEngine { controller };
        FakeServices services { controller, dawEngine };
        std::unique_ptr<MainView> view;

        Window()
        {
            MainView::setAutoTutorial (false);
            MainView::setStoredThemeUsed (false);
            samples.load();
            controller.setSampleBanks (samples.table());
            view = std::make_unique<MainView> (controller, services);
            view->setSize (1520, 960);
            view->setVisible (true);

            services.openDevices ("Console", "Console");
            view->showPage (MainView::Page::Assign);
            auto& assign = view->getAssignPage();
            assign.assign (0, ChannelRole::KickIn, "Kick");
            assign.assign (1, ChannelRole::SnareTop, "Snare");
            assign.assign (7, ChannelRole::BassDI, "Bass");
            assign.assign (10, ChannelRole::LeadVocal, "Lead");
            assign.assign (14, ChannelRole::Speech, "Pastor");
            view->showPage (MainView::Page::Purpose);
            view->getPurposePage().onContinue();
            pump (40);
        }

        void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

        // Every item in every menu, by command id, with the text it carried.
        std::map<int, juce::String> menuItems()
        {
            std::map<int, juce::String> found;
            auto* model = view->getMenuModel();
            const auto names = model->getMenuBarNames();
            for (int i = 0; i < names.size(); ++i)
                collect (model->getMenuForIndex (i, names[i]), found);
            return found;
        }

    private:
        static void collect (juce::PopupMenu menu, std::map<int, juce::String>& into)
        {
            juce::PopupMenu::MenuItemIterator it (menu);
            while (it.next())
            {
                const auto& item = it.getItem();
                if (item.subMenu != nullptr) collect (*item.subMenu, into);
                if (item.itemID != 0) into[item.itemID] = item.text;
            }
        }
    };
}

// ---------------------------------------------------------------------------- menus
TEST_CASE ("Reachability: every menu item is still in a menu, under the same command id")
{
    Window window;
    const auto items = window.menuItems();

    // id, and a word from the item that says which item it is. The text is allowed to change
    // around the word - several of these items rewrite themselves for what is going on.
    struct Item { int id; const char* says; };
    static const Item expected[] = {
        // File
        { 100, "New Session" }, { 101, "Open Session" }, { 102, "Save" }, { 103, "Save As" },
        { 104, "Import Multitrack" }, { 107, "Reference Mix" }, { 108, "Save Input Mapping" },
        { 109, "Input Mappings" }, { 105, "Export Stereo Mix (WAV)" }, { 106, "Export Stereo Mix (MP3)" },
        // Edit
        { 200, "Undo" }, { 201, "Split at Playhead" }, { 202, "Delete Clip" }, { 203, "Marker" },
        // Track
        { 300, "Set Every Track to Record" }, { 301, "Set No Tracks to Record" },
        { 305, "Move Track Up" }, { 306, "Move Track Down" },
        { 302, "Monitoring: Input" }, { 303, "Monitoring: Auto" }, { 304, "Monitoring: Off" },
        // Mix
        { 405, "TUNE LIVE MIX" }, { 400, "TUNE MIX" }, { 404, "TUNE CHANNEL" },
        { 407, "MATCH TO REFERENCE" }, { 408, "Reference" },
        { 413, "Speech Priority" }, { 416, "Share the Mics" }, { 409, "Mix Buddy" }, { 410, "Try Another Mix" },
        { 411, "Undo" }, { 412, "Redo" }, { 420, "Loudness" },
        { 415, "Autopilot" }, { 414, "Reset Mix to Raw" },
        { 401, "Centre Macro Pads" }, { 402, "Clear Solo" }, { 403, "Bypass" },
        { 406, "Cloud Model" },
        // Transport
        { 500, "Play" }, { 501, "Record" }, { 502, "Return to Start" }, { 503, "Loop" },
        // View
        { 600, "Tracks" }, { 601, "Mixer" }, { 602, "Tune" }, { 603, "Live" }, { 604, "Inspector" },
        // MOVED, 2026-09-28 (Phase 2 item 4): the set-up rows left the everyday sidebar for one
        // ROUTING workspace, and Outputs left its sheet for a section of it. Both commands are
        // still here and still ask for the same thing; 616 is the saved patches, which were a
        // submenu inside a submenu and are now a section of their own.
        { 613, "Routing" }, { 608, "Mixer in a New Window" }, { 609, "Outputs" }, { 630, "Check Inputs" },
        { 616, "Saved Input Patches" },
        { 631, "Dim the Broadcast" }, { 632, "Mute the Broadcast" },
        { 610, "Sidebar" }, { 615, "side panels" },
        { 620, "Customise Appearance" }, { 621, "Import a Theme" }, { 622, "Show Themes Folder" },
        { 660, "Standard" }, { 661, "Large" }, { 662, "Larger" },     // View > Appearance > Text size
        { 605, "Zoom In" }, { 606, "Zoom Out" }, { 607, "Zoom to Fit" },
        // Help
        { 701, "Getting started" }, { 700, "About DLIVE" },
    };

    for (const auto& item : expected)
    {
        const auto found = items.find (item.id);
        CHECK_MESSAGE (found != items.end(),
                       "command " + std::to_string (item.id) + " (" + item.says + ") is in no menu any more");
        if (found == items.end()) continue;
        CHECK_MESSAGE (found->second.containsIgnoreCase (item.says),
                       "command " + std::to_string (item.id) + " should still say \"" + item.says
                           + "\", says \"" + found->second.toStdString() + "\"");
    }

    // The loudness targets and the master sounds are a row each, built from their enums.
    for (int i = 0; i < int (DeliveryLoudness::Count); ++i)
        CHECK_MESSAGE (items.count (430 + i) == 1, "loudness target " + std::to_string (i) + " left the Mix menu");
    for (int i = 0; i < int (MasterVoicing::Count); ++i)
        CHECK_MESSAGE (items.count (450 + i) == 1, "master sound " + std::to_string (i) + " left the Mix menu");

    // At least one theme is always offered (the built-ins), at 640 and up.
    CHECK (items.count (640) == 1);
}

TEST_CASE ("Reachability: the menu bar still has every menu")
{
    Window window;
    const auto names = window.view->getMenuModel()->getMenuBarNames();
    for (const char* wanted : { "File", "Edit", "Track", "Mix", "Transport", "View", "Help" })
        CHECK_MESSAGE (names.contains (wanted), std::string ("the ") + wanted + " menu is gone");
}

// ------------------------------------------------------------------------ shortcuts
TEST_CASE ("Reachability: every keyboard shortcut still asks for the same command")
{
    using Page = MainView::Page;
    const int cmd = juce::ModifierKeys::commandModifier;
    const int shift = juce::ModifierKeys::shiftModifier;
    const int ctrl = juce::ModifierKeys::ctrlModifier;

    struct Binding { juce::KeyPress key; Page page; int command; const char* what; };
    const Binding bindings[] = {
        { { 'S', cmd | ctrl, 0 },              Page::Mixer,  610, "hide the sidebar" },
        { { 'S', cmd, 0 },                     Page::Mixer,  102, "save" },
        { { 'S', cmd | shift, 0 },             Page::Mixer,  103, "save as" },
        { { 'Z', cmd, 0 },                     Page::Tracks, 200, "undo" },
        { { 'E', cmd, 0 },                     Page::Tracks, 201, "split at playhead" },
        { { 'O', cmd, 0 },                     Page::Mixer,  101, "open a session" },
        { { 'N', cmd, 0 },                     Page::Mixer,  100, "a new session" },
        { { '=', cmd, 0 },                     Page::Tracks, 605, "zoom in" },
        { { '-', cmd, 0 },                     Page::Tracks, 606, "zoom out" },
        { { '0', cmd, 0 },                     Page::Tracks, 607, "zoom to fit" },
        { { '1', cmd, 0 },                     Page::Mixer,  600, "TRACKS" },
        { { '2', cmd, 0 },                     Page::Tracks, 601, "MIXER" },
        { { '3', cmd, 0 },                     Page::Tracks, 602, "TUNE" },
        { { '4', cmd, 0 },                     Page::Tracks, 603, "LIVE" },
        { { '5', cmd, 0 },                     Page::Tracks, 604, "INSPECTOR" },
        { { juce::KeyPress::spaceKey, 0, 0 },  Page::Tracks, 500, "play / stop" },
        { { juce::KeyPress::returnKey, 0, 0 }, Page::Tracks, 502, "return to start" },
        { { 'R', 0, 0 },                       Page::Tracks, 501, "record" },
        { { 'L', 0, 0 },                       Page::Tracks, 503, "loop" },
        { { 'B', 0, 0 },                       Page::Mixer,  403, "bypass" },
        { { 'T', 0, 0 },                       Page::Mixer,  404, "TUNE CHANNEL" },
        { { 'M', 0, 0 },                       Page::Tracks, 203, "add a marker" },
        { { '[', 0, 0 },                       Page::Tune,   611, "fold the left panel" },
        { { ']', 0, 0 },                       Page::Tune,   612, "fold the right panel" },
        { { juce::KeyPress::deleteKey, 0, 0 }, Page::Tracks, 202, "delete a clip" },
        { { juce::KeyPress::backspaceKey, 0, 0 }, Page::Tracks, 202, "delete a clip" },
    };

    for (const auto& b : bindings)
        CHECK_MESSAGE (MainView::commandForKey (b.key, b.page) == b.command,
                       std::string ("the shortcut for ") + b.what + " no longer asks for command "
                           + std::to_string (b.command) + " (it asks for "
                           + std::to_string (MainView::commandForKey (b.key, b.page)) + ")");

    // A clip is only deleted where there are clips - everywhere else the key is free.
    CHECK (MainView::commandForKey ({ juce::KeyPress::deleteKey, 0, 0 }, Page::Mixer) == 0);
}

// ----------------------------------------------------------------------------- pages
TEST_CASE ("Reachability: every workspace and every set-up page still opens")
{
    Window window;
    using Page = MainView::Page;
    struct Row { Page page; const char* name; };
    const Row pages[] = {
        { Page::Sessions,  "Sessions" },  { Page::Device, "Audio device" }, { Page::Assign, "Inputs" },
        { Page::Purpose,   "Purpose and sound" },
        // The two sections ROUTING owns. Outputs was a sheet until 2026-09-28; the saved
        // patches were a submenu inside a submenu.
        { Page::Outputs,   "Outputs and monitoring" }, { Page::Maps, "Saved input patches" },
        { Page::Tracks,    "Tracks" },    { Page::Mixer,  "Mixer" },        { Page::Tune,   "Tune" },
        { Page::Live,      "Live" },      { Page::Inspector, "Inspector" },
    };

    for (const auto& row : pages)
    {
        window.view->showPage (row.page);
        window.pump (10);
        CHECK_MESSAGE (window.view->getPage() == row.page,
                       std::string ("the ") + row.name + " page does not open any more");
    }

    // ...and the four of them are one workspace, reached deliberately. `Routing` is not a page
    // a window is ever on: it means the workspace at whatever section it was left on.
    window.view->showPage (Page::Live);
    window.pump (10);
    window.view->showPage (Page::Routing);
    window.pump (10);
    CHECK (window.view->getPage() == Page::Maps);          // where the loop above left it
    CHECK (MainView::isRoutingPage (window.view->getPage()));
    CHECK (! MainView::isRoutingPage (Page::Sessions));    // the library is not routing
    CHECK (! MainView::isRoutingPage (Page::Mixer));
}

// ---------------------------------------------------------------------------- sheets
TEST_CASE ("Reachability: every sheet still opens, and Escape still closes it")
{
    Window window;
    auto& view = *window.view;
    view.showPage (MainView::Page::Mixer);
    window.pump (10);

    struct Sheet { const char* name; std::function<void()> open; };
    const Sheet sheets[] = {
        // "outputs" left this list on 2026-09-28: it is a section of the ROUTING workspace now,
        // asserted in the pages test above, and view.showOutputs() goes there.
        { "check",      [&] { view.showCheck(); } },
        { "history",    [&] { view.showHistory(); } },
        { "appearance", [&] { view.showThemes(); } },
        { "chat",       [&] { view.showChat(); } },
        { "channel",    [&] { view.tuneChannel (0); } },
        // TUNE asks *which* group or channels before it tunes them - the whole mix needs
        // nothing more said about it, so that one starts. It is the workspace's own sheet
        // rather than the window's, and Escape means the same thing over it.
        { "tunescope",  [&] { view.showPage (MainView::Page::Tune);
                              view.getMixPage().setScopeForSnapshot (1, -1);
                              view.getMixPage().pressTune(); } },
    };

    for (const auto& sheet : sheets)
    {
        // Opening the channel sheet starts a listen, and a listen makes TUNE mean "stop" - so
        // each entry starts from a console that is not busy rather than from the last one's
        // leftovers.
        window.controller.abortTuneMix();
        sheet.open();
        window.pump (10);
        CHECK_MESSAGE (view.openSheetName() == juce::String (sheet.name),
                       std::string ("the ") + sheet.name + " sheet did not open (open: \""
                           + view.openSheetName().toStdString() + "\")");

        // ...AND IT HAS A SIZE. Mix history was created, added to the window and left out of
        // the list that gives the sheets their bounds, so it was a sheet with no size that
        // nobody had ever seen (found 2026-09-28 by photographing it). Opening is half of it.
        bool sized = false;
        for (int i = 0; i < view.getNumChildComponents(); ++i)
            if (auto* child = view.getChildComponent (i))
                if (child->isVisible() && child->getWidth() > 200 && child->getHeight() > 100
                    && child->getWidth() >= view.getWidth() / 3)
                    sized = true;
        CHECK_MESSAGE (sized, std::string ("the ") + sheet.name + " sheet has no bounds, so nobody can see it");

        view.keyPressed ({ juce::KeyPress::escapeKey, 0, 0 });
        window.pump (10);
        CHECK_MESSAGE (view.openSheetName().isEmpty(),
                       std::string ("Escape did not close the ") + sheet.name + " sheet");
    }
}

// ------------------------------------------------------------------- the document
// EVERY EDIT REACHES THE DOCUMENT.
//
// This is the guard for the bug class that keeps coming back: a page changes something the
// session holds - a track's name, a marker, a row height, what is set to record - and nothing
// tells the document, so the work is there on screen and gone after a crash or a reopen.
//
// It is asserted here rather than in the engine tests because the engine cannot see it. The
// revision is moved by the *call site* in the page, so a test that drives MixController
// directly passes whatever the pages happen to do. Restoring app/ui to an older state on
// 2026-09-28 put twenty of these call sites back to calling saveSession() - which still wrote
// the file, so nothing looked broken on screen, but never moved the revision the autosave
// watches, and a crash would have taken every timeline edit with it.
//
// The second half matters as much as the first: an edit must NOT write the document itself. A
// 32-channel session with a morning of history behind it is a real serialise, and doing that
// inside a 30 Hz tick is what made dragging a clip stutter.
TEST_CASE ("Every edit a page makes reaches the document, and none of them writes the file")
{
    Window window;
    auto& view = *window.view;
    view.showPage (MainView::Page::Tracks);
    window.pump (30);

    auto& tracks = view.getTracksPage();

    // The loop key refuses when nothing is marked to loop ("drag along the top of the ruler
    // first"), so the range is marked on the project directly - a refusal is not an edit, and
    // this test is about edits.
    {
        auto& project = window.services.daw().getProject();
        project.loopStart = 0;
        project.loopEnd = juce::int64 (kSr * 4);
    }

    const auto savesBefore = window.services.saves;

    struct Edit { const char* what; std::function<void()> go; };
    // The edit paths TracksPage offers publicly. The rest - renaming a track, its icon, its
    // source, arming, monitoring - are reached through the row rather than through the class,
    // and they all sit in the same file beside these; widening TracksPage's API so a test can
    // call them would be testing the test. These four are enough to catch the regression,
    // because a phase that breaks one call site has broken all of them the same way.
    const Edit edits[] = {
        { "adding a marker",         [&] { tracks.addMarkerAtPlayhead(); } },
        { "changing the row height", [&] { tracks.setRowHeight (TracksPage::RowHeight::Large); } },
        { "moving a track",          [&] { tracks.moveTrack (0, 2); } },
        { "setting the loop",        [&] { tracks.toggleLoop(); } },
    };

    for (const auto& edit : edits)
    {
        const auto before = window.services.touches;
        edit.go();
        window.pump (4);
        CHECK_MESSAGE (window.services.touches > before,
                       std::string (edit.what) + " did not tell the document anything had changed");
    }

    CHECK_MESSAGE (window.services.saves == savesBefore,
                   "editing wrote the session file " + std::to_string (window.services.saves - savesBefore)
                   + " times; the document is written by File > Save, not by an edit");
}

TEST_CASE ("Reachability: the sidebar folds, the side panels fold, and the tutorial still opens")
{
    Window window;
    auto& view = *window.view;

    CHECK (view.isSidebarShown());
    view.setSidebarShown (false);
    CHECK (! view.isSidebarShown());
    view.setSidebarShown (true);
    CHECK (view.isSidebarShown());

    view.showPage (MainView::Page::Tune);
    window.pump (10);
    view.togglePanel (true);    // `[`
    view.togglePanel (false);   // `]`

    view.showTutorial();
    window.pump (10);
    view.closeTutorial();
    window.pump (10);
}

// -------------------------------------------------------------------- the solo band
// SOLO YOU CANNOT MISS. An S left down on MIXER used to be invisible from every other
// workspace, and a service mixed through one microphone's worth of headphones is what that
// costs. The band is the answer, so this asserts the two things it has to be: there whenever
// anything is soloed, from anywhere, and gone the moment nothing is.
TEST_CASE ("Reachability: anything soloed says so from every workspace, and one press clears it")
{
    Window window;
    auto& view = *window.view;
    auto& controller = window.controller;

    using Page = MainView::Page;
    const Page workspaces[] = { Page::Tracks, Page::Mixer, Page::Tune, Page::Live, Page::Inspector };

    // Nothing soloed, nothing said.
    for (const Page p : workspaces)
    {
        view.showPage (p);
        window.pump (10);
        CHECK_MESSAGE (! view.isSoloBarShown(), "the solo band is there with nothing soloed");
    }

    // A channel, a group and a return: all three kinds reach the band, by name.
    controller.setStripSolo (0, true);
    controller.setBusSolo (MixBus::Drums, true);
    controller.setFxSolo (FxSlot::VocalPlate, true);
    const auto soloed = controller.getSoloed();
    CHECK (soloed.size() == 3);
    CHECK (soloed[0].kind == MixController::SoloedItem::Kind::Strip);
    CHECK (soloed[0].name == controller.getSession().inputs[0].name);
    CHECK (soloed[1].kind == MixController::SoloedItem::Kind::Bus);
    CHECK (soloed[2].kind == MixController::SoloedItem::Kind::Fx);

    for (const Page p : workspaces)
    {
        view.showPage (p);
        window.pump (10);
        CHECK_MESSAGE (view.isSoloBarShown(),
                       "the solo band is not on this workspace, so a solo is invisible from it");
    }

    // ...and the room never heard any of it: solo is the monitor's business and nothing else's.
    CHECK (! controller.getKept().strips[0].mute);
    CHECK (controller.numSoloed() == 3);

    controller.clearSolos();
    window.pump (10);
    view.showPage (Page::Mixer);
    window.pump (10);
    CHECK (! view.isSoloBarShown());
    CHECK (controller.getSoloed().empty());
}

// ------------------------------------------------------------------- the group buses
// A group bus is what an engineer reaches for when something is wrong with a whole section,
// so it has to be on the console however the console is set up. It sits at the end of the
// family that feeds it and scrolls with the channels - there is no fixed rail of groups - and
// this says every used group has a strip, on a console big enough for it to matter, at the
// smallest window the application allows, at every width and under every filter.
TEST_CASE ("Reachability: every group bus has a strip on the console, at any size")
{
    Window window;
    auto& view = *window.view;
    auto& controller = window.controller;

    // Thirty-two sources across every group, which is the case the rail exists for.
    MixSession session;
    session.name = "Group rail";
    const ChannelRole roles[] = { ChannelRole::KickIn, ChannelRole::SnareTop, ChannelRole::BassDI,
                                  ChannelRole::Piano, ChannelRole::ElectricGuitarClean, ChannelRole::LeadVocal,
                                  ChannelRole::BackingVocal, ChannelRole::Speech, ChannelRole::Room };
    for (int i = 0; i < 32; ++i)
    {
        InputAssignment a;
        a.role = roles[size_t (i) % (sizeof (roles) / sizeof (roles[0]))];
        a.name = (juce::String (channelRoleName (a.role)) + " " + juce::String (i + 1)).toStdString();
        a.inputA = i;
        session.inputs.push_back (a);
    }
    controller.setSession (session);
    window.services.reconfigure();
    view.setSize (1180, 760);            // app/Main.cpp's smallest allowed window
    view.showPage (MainView::Page::Mixer);
    window.pump (30);

    auto& mixer = view.getMixerPage();
    int used = 0;
    for (int b = 0; b < int (MixBus::Master); ++b)
        if (controller.getEngine().isBusUsed (MixBus (b))) ++used;
    REQUIRE (used >= 4);
    CHECK_MESSAGE (mixer.busStripCount() == used,
                   "the console shows " + std::to_string (mixer.busStripCount()) + " of "
                       + std::to_string (used) + " group buses");

    // Folding the sidebar, resizing, and the narrow and wide strip widths do not lose them.
    view.setSidebarShown (false);
    window.pump (10);
    CHECK (mixer.busStripCount() == used);
    mixer.setStripSize (MixerPage::Size::Wide);
    window.pump (10);
    CHECK (mixer.busStripCount() == used);
    mixer.setStripSize (MixerPage::Size::Narrow);
    view.setSidebarShown (true);
    window.pump (10);
    CHECK (mixer.busStripCount() == used);

    // "Only the inputs" hides them; "only the groups and the master" is a request to look at
    // them on their own; the list shows them among the channels the same way the console does.
    mixer.setShow (MixerPage::Show::Inputs);
    window.pump (10);
    CHECK (mixer.busStripCount() == 0);
    mixer.setShow (MixerPage::Show::Groups);
    window.pump (10);
    CHECK (mixer.busStripCount() == used);
    mixer.setShow (MixerPage::Show::All);
    mixer.setView (MixerPage::View::List);
    window.pump (10);
    CHECK (mixer.busStripCount() == used);
    mixer.setView (MixerPage::View::Strips);
    window.pump (10);
    CHECK (mixer.busStripCount() == used);
}

// ---------------------------------------------------------------------- routing
// SET-UP IS ONE WORKSPACE, REACHED ON PURPOSE. The device, the patch and the output feeds are
// the three ways to silence a room in the middle of a service, and until now they sat in the
// everyday sidebar beside TRACKS and MIXER. This asserts the two halves of the answer: the
// sections are all there, and under LIVE SAFE nothing on them can be reached until somebody
// says they mean it - once per visit, and not a moment longer.
TEST_CASE ("Reachability: ROUTING gathers the set-up, and LIVE SAFE covers it until you say so")
{
    Window window;
    auto& view = *window.view;
    auto& routing = view.getRoutingPage();
    using Page = MainView::Page;
    using Section = RoutingPage::Section;

    // Every section is reachable and names itself.
    const Section sections[] = { Section::Device, Section::Inputs, Section::Outputs, Section::Maps };
    for (const auto s : sections)
    {
        view.showPage (MainView::pageForSection (s));
        window.pump (10);
        CHECK (routing.getSection() == s);
        CHECK (MainView::sectionForPage (view.getPage()) == s);
        CHECK (juce::String (RoutingPage::sectionName (s)).isNotEmpty());
    }

    // LIVE SAFE off: the workspace is open and the page it hosts has room to be on.
    CHECK (! window.services.daw().isLiveSafe());
    view.showPage (Page::Device);
    window.pump (10);
    CHECK (! routing.isCovered());
    CHECK (! routing.contentBounds().isEmpty());
    CHECK (view.getDevicePage().isVisible());

    // LIVE SAFE on: covered, and nothing underneath has a size to be clicked in.
    window.services.daw().setLiveSafe (true);
    view.showPage (Page::Device);
    window.pump (10);
    CHECK (routing.isCovered());
    CHECK (routing.contentBounds().isEmpty());
    CHECK (! view.getDevicePage().isVisible());

    // Saying you mean it uncovers this visit, and LIVE SAFE itself is untouched: the lock is
    // still on everywhere the mix can be changed.
    routing.confirmForTest();
    window.pump (10);
    CHECK (! routing.isCovered());
    CHECK (window.services.daw().isLiveSafe());
    CHECK (! routing.contentBounds().isEmpty());
    CHECK_MESSAGE (view.getDevicePage().isVisible(),
                   "the page under the cover did not get its size back when the cover came down");

    // Moving between sections keeps it; leaving the workspace locks it again.
    view.showPage (Page::Outputs);
    window.pump (10);
    CHECK (! routing.isCovered());
    view.showPage (Page::Mixer);
    window.pump (10);
    view.showPage (Page::Outputs);
    window.pump (10);
    CHECK_MESSAGE (routing.isCovered(), "a confirmation outlived the visit it was given for");

    window.services.daw().setLiveSafe (false);
}

TEST_CASE ("Recording: REC with nothing to record from says so and records nothing")
{
    // An output-only device - the microphone refused, "Not now", the console unplugged - is
    // running. REC used to light up over it and write hours of silence.
    Window w;
    REQUIRE (! w.dawEngine.getProject().tracks.empty());
    w.dawEngine.getProject().tracks[0].armed = true;
    w.dawEngine.getProject().folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dlive-rec-refused");
    w.services.openOutputOnly ("Console");
    w.view->getTransportBar().toggleRecord();
    CHECK (! w.dawEngine.isRecording());
}

TEST_CASE ("Recording: one stray key never stops a service take")
{
    Window w;
    REQUIRE (! w.dawEngine.getProject().tracks.empty());
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dlive-rec-keys");
    folder.deleteRecursively();
    folder.createDirectory();
    w.dawEngine.getProject().folder = folder;
    w.dawEngine.getProject().tracks[0].armed = true;
    w.services.reconfigure();
    w.view->getTransportBar().toggleRecord();
    REQUIRE (w.dawEngine.isRecording());

    auto& view = static_cast<juce::Component&> (*w.view);
    view.keyPressed (juce::KeyPress ('R', 0, 0));
    CHECK (w.dawEngine.isRecording());                        // asked, not stopped
    view.keyPressed (juce::KeyPress ('R', 0, 0));
    CHECK (! w.dawEngine.isRecording());                      // the second press means it
    folder.deleteRecursively();
}

// ---------------------------------------------------------------------------- the Inspector
TEST_CASE ("Inspector: a session the engine has not caught up with yet is shown, not rebuilt forever")
{
    // An import hands the controller its new inputs at once, and the engine a moment later (a
    // large folder, the device re-opening). The Inspector used to rebuild from one list and check
    // itself against the other, and on a 31-file import it recursed until the stack ran out.
    Window window;
    auto session = window.controller.getSession();
    for (int i = int (session.inputs.size()); i < 12; ++i)
        session.inputs.push_back ({ "Extra " + std::to_string (i), ChannelRole::BackingVocal, 20 + i, -1 });
    window.controller.setSession (session);
    REQUIRE (window.controller.getGraph().numStrips() != window.controller.getEngine().getGraph().numStrips());

    auto& inspector = window.view->getAdvancedPage();
    inspector.rebuild();
    inspector.refresh();
    inspector.refresh();
    window.pump (40);
    CHECK (true);   // reaching here is the test
}
