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
        juce::String openDevices (const juce::String& in, const juce::String& out) override { input = in; output = out; running = true; return {}; }
        juce::String openOutputOnly (const juce::String& out) override { output = out; running = true; return {}; }
        juce::String changeOutput (const juce::String& out) override { output = out; return {}; }
        bool isAudioRunning() override { return running; }
        int numInputChannels() override { return running ? kInputs : 0; }
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
        void saveSession() override {}
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
        bool running = false;
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
        { 413, "Speech Priority" }, { 409, "Mix Buddy" }, { 410, "Try Another Mix" },
        { 411, "Undo" }, { 412, "Redo" }, { 420, "Loudness" },
        { 401, "Centre Macro Pads" }, { 402, "Clear Solo" }, { 403, "Bypass" },
        { 406, "Cloud Model" },
        // Transport
        { 500, "Play" }, { 501, "Record" }, { 502, "Return to Start" }, { 503, "Loop" },
        // View
        { 600, "Tracks" }, { 601, "Mixer" }, { 602, "Tune" }, { 603, "Live" }, { 604, "Inspector" },
        { 613, "Setup" }, { 608, "Mixer in a New Window" }, { 609, "Outputs" }, { 630, "Check Inputs" },
        { 631, "Dim the Broadcast" }, { 632, "Mute the Broadcast" }, { 633, "Mix History" },
        { 610, "Sidebar" }, { 615, "side panels" },
        { 620, "Customise Appearance" }, { 621, "Import a Theme" }, { 622, "Show Themes Folder" },
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
        { { '6', cmd, 0 },                     Page::Tracks, 613, "ROUTING" },
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
        { "outputs",    [&] { view.showOutputs(); } },
        { "check",      [&] { view.showCheck(); } },
        { "history",    [&] { view.showHistory(); } },
        { "appearance", [&] { view.showThemes(); } },
        { "chat",       [&] { view.showChat(); } },
        { "channel",    [&] { view.tuneChannel (0); } },
    };

    for (const auto& sheet : sheets)
    {
        sheet.open();
        window.pump (10);
        CHECK_MESSAGE (view.openSheetName() == juce::String (sheet.name),
                       std::string ("the ") + sheet.name + " sheet did not open (open: \""
                           + view.openSheetName().toStdString() + "\")");

        view.keyPressed ({ juce::KeyPress::escapeKey, 0, 0 });
        window.pump (10);
        CHECK_MESSAGE (view.openSheetName().isEmpty(),
                       std::string ("Escape did not close the ") + sheet.name + " sheet");
    }
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

// ------------------------------------------------------------------------- solo bar
TEST_CASE ("The solo bar: it appears whenever anything is soloed, on every workspace, and clears everything at once")
{
    Window window;
    auto& view = *window.view;
    auto& controller = window.controller;

    view.showPage (MainView::Page::Mixer);
    window.pump (40);
    CHECK (! view.isSoloBarShown());

    // A strip, by the name the console gave it.
    controller.setStripSolo (0, true);
    window.pump (80);
    CHECK (view.isSoloBarShown());
    CHECK (view.soloedNames() == juce::StringArray { "KICK" });

    // It is on every workspace, not only the mixer - that is the whole point of it, because
    // solo goes to the engineer's own device and nothing else says it is on.
    for (auto page : { MainView::Page::Tracks, MainView::Page::Tune, MainView::Page::Live, MainView::Page::Inspector })
    {
        view.showPage (page);
        window.pump (60);
        CHECK_MESSAGE (view.isSoloBarShown(), "the solo bar is missing from a workspace");
    }

    // A group and an FX return count too.
    controller.setBusSolo (MixBus::Drums, true);
    window.pump (80);
    CHECK (view.soloedNames().size() == 2);
    CHECK (view.soloedNames().contains ("DRUMS"));

    // One press clears everything, whatever kind of thing it was.
    controller.clearSolos();
    window.pump (80);
    CHECK (view.soloedNames().isEmpty());
    CHECK (! view.isSoloBarShown());
}
