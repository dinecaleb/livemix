#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "UI/Widgets.h"

namespace livemix
{

// The four screens a session is set up on - SESSIONS, AUDIO DEVICE, INPUTS, PURPOSE AND
// SOUND - are one layout with four contents, so the volunteer learns the shape once:
//
//   title + one sentence .................................. a readout of where you are
//   a toolbar: search, filters, the actions for what is selected
//   +------------------------------------------+  +------------------+
//   | the list, the table, the cards           |  | small cards:     |
//   |                                          |  | what it adds up  |
//   |                                          |  | to, what you can |
//   |                                          |  | do in one go     |
//   +------------------------------------------+  +------------------+
//   a note .......................................... Back      Continue
//
// SetupLayout is the one place those bands are measured, so all four pages line up
// exactly and a change to the shape is a change in one function.
struct SetupLayout
{
    juce::Rectangle<int> head;      // title, subtitle and the page's readout
    juce::Rectangle<int> toolbar;   // search / filters / bulk actions ({} when the page has none)
    juce::Rectangle<int> main;      // the table or the cards
    juce::Rectangle<int> rail;      // the column of small cards ({} when the page has none)
    juce::Rectangle<int> footer;    // the note and the buttons

    static SetupLayout of (juce::Rectangle<int> page, bool withToolbar, bool withRail);
};

// Title, one sentence under it, and the page's own readout on the right.
void drawSetupHead (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, const juce::String& sentence);
// The band along the foot: a hairline, the ground, and a quiet note left of the buttons.
void drawSetupFooter (juce::Graphics&, juce::Rectangle<int> page, const juce::String& note, int reservedRight);

// STEP 0: the library. Every saved session, what it sounds like, what it was for and when
// it was last opened; opening one puts its inputs, groups and tune back as they were.
class SessionsPage : public juce::Component
{
public:
    SessionsPage (MixController&, AppServices&);
    ~SessionsPage() override;

    std::function<void (const juce::File&)> onOpen;    // open this saved session
    std::function<void()> onNew;                       // start a new one: straight to the device

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    struct Item
    {
        SessionStore::Listing listing;
        SessionStore::Summary summary;
        juce::String when;          // "2 hours ago", "Yesterday", "17 Nov"
        bool open = false;          // this is the session on screen
    };

    void rebuild();
    void select (int index);
    const Item* selectedItem() const;
    // Where a session lives, written the way a person would say it: under the home folder
    // it is a ~ path; anywhere else, the last two folders - so the row never fills with a
    // path nobody reads.
    static juce::String folderText (const juce::File&);

    MixController& controller;
    AppServices& services;
    std::vector<Item> items;        // every session, newest first
    std::vector<int> shown;         // indices into items, after the search and the filter
    std::vector<std::unique_ptr<Row>> rows;
    std::map<juce::String, SessionStore::Summary> cache;   // by path: a listing is re-read, a document is not
    int selected = -1;
    int filter = 0;                 // 0 All, 1 Recent, 2 Templates
    juce::Viewport viewport;
    juce::Component listHolder;
    juce::TextEditor search;
    std::array<std::unique_ptr<DineChip>, 3> chips;
    DineButton newButton { "New session", DineButton::Style::Filled };
    DineButton openButton { "Open session", DineButton::Style::Filled };
    DineButton revealButton { "Show in Finder", DineButton::Style::Standard };
};

// STEP 1: which device brings the inputs in. The list, what it is running at, where the
// sound comes out, and what is actually arriving on each channel -> Continue.
class DevicePage : public juce::Component
{
public:
    DevicePage (MixController&, AppServices&);
    ~DevicePage() override;
    std::function<void()> onContinue;
    std::function<void()> onContinueToAssign;                  // an import happened: review the guessed assignments
    std::function<void (const juce::File&)> onImportRecording; // a folder of stems becomes tracks and clips
    std::function<void()> onSetUpOutputs;                      // the Outputs sheet: more than one pair at once
    std::function<void()> onBack;                              // back to the library
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class DeviceRow;
    class OutputRow;
    // The three bands of the device column, measured once so paint and resized cannot
    // disagree: the list takes the height, the spec sits under it, and the "no band in the
    // room" card is pinned to the foot.
    struct Column { juce::Rectangle<int> listCaption, list, specCaption, spec, importCard; };
    Column column() const;
    void select (int index);
    void selectOutput (int index);
    SetupLayout layout() const;
    int liveInputCount() const;

    MixController& controller;
    AppServices& services;
    juce::Array<AppServices::Device> inputs, outputs;
    int selected = -1;
    juce::String outputName;
    juce::String error;
    std::vector<std::unique_ptr<DeviceRow>> rows;
    std::vector<std::unique_ptr<OutputRow>> outputRows;
    juce::Viewport viewport;
    juce::Component listHolder;
    DineButton continueButton { "Continue", DineButton::Style::Filled };
    DineButton backButton { "Back", DineButton::Style::Standard };
    DineButton rescanButton { "Rescan devices", DineButton::Style::Standard };
    DineButton outputsButton { "Set up outputs...", DineButton::Style::Standard };
    DineButton recordingButton { "Import stems...", DineButton::Style::Standard };
    std::unique_ptr<juce::FileChooser> chooser;
};

// STEP 2: every device input on one row - number, icon, name, what is arriving, what it
// is, and whether it is half of a stereo pair - grouped by the bus it will feed.
class AssignPage : public juce::Component
{
public:
    AssignPage (MixController&, AppServices&);
    ~AssignPage() override;
    std::function<void()> onContinue, onBack;
    void refresh();                       // rebuild rows from the controller's session and the device's channel count
    void paint (juce::Graphics&) override;
    void resized() override;

    // Programmatic equivalents of the user's edits (also used by the snapshot tool).
    void assign (int input, ChannelRole role, const juce::String& name, bool linkWithNext = false);
    void selectInputs (const std::vector<int>&);   // pick these out; the toolbar becomes the bulk one
    void clearAll();

private:
    class Row;
    // The band a group of inputs sits under: its colour, its name, how many are in it, and
    // a click that picks the whole group out at once.
    class GroupHeader;
    // A rail action: what it does on one line, what that means on the next. Two lines,
    // left-aligned, because "Name everything from what it is" needs the sentence under it
    // more than it needs to be centred.
    class QuickAction;
    struct Entry
    {
        juce::String name;
        std::string icon;
        bool assigned = false;
        ChannelRole role = ChannelRole::KickIn;
        bool linkedToNext = false, linkedFromPrevious = false;
        bool selected = false;
    };
    struct Group { juce::String name; juce::Colour colour; int bus = -1; std::vector<int> inputs; };

    void rebuild();
    void commit();                        // rows -> controller session
    void showSourceMenu (int input, juce::Component& anchor);
    void showNameMenu (int input, juce::Component& anchor);
    void showBulkMenu (juce::Component& anchor);
    void showKitMenu (juce::Component& anchor);
    void setRole (int input, ChannelRole role, bool assigned);
    void toggleSelection (int input, bool extend);
    void selectGroup (const Group&);
    void clearSelection();
    int selectionCount() const;
    std::vector<int> selectedInputs() const;
    void nameFromRole();
    void linkSelection();
    void dropSelection();
    bool visible (int input) const;       // passes the search and the bus filter
    int assignedCount() const;
    int unusedCount() const;
    std::vector<Group> buildGroups() const;
    SetupLayout layout() const;
    void updateToolbar();

    MixController& controller;
    AppServices& services;
    int numInputs = 0;
    std::vector<Entry> entries;
    std::vector<Group> groups;            // what the list is showing, rebuilt with the rows
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<std::unique_ptr<GroupHeader>> headers;
    juce::String query;
    int busFilter = -2;                   // -2 all, -1 not used, else MixBus
    int lastClicked = -1;                 // for shift-click
    bool grouped = true;
    juce::Viewport viewport;
    juce::Component listHolder;
    juce::TextEditor search;
    std::vector<std::unique_ptr<DineChip>> chips;
    DineButton selectAllButton { "Select all", DineButton::Style::Standard };
    DineButton deskLabelsButton { "Use desk labels", DineButton::Style::Standard };
    DineButton groupButton { "", DineButton::Style::Standard };
    DineButton bulkButton { "Set what it is", DineButton::Style::Standard };
    DineButton kitButton { "Fill in order", DineButton::Style::Standard };
    DineButton nameButton { "Name from role", DineButton::Style::Standard };
    DineButton linkButton { "Link as pair", DineButton::Style::Standard };
    DineButton dropButton { "Not used", DineButton::Style::Ghost };
    DineButton deselectButton { "Deselect", DineButton::Style::Ghost };
    DineButton continueButton { "Continue", DineButton::Style::Filled };
    DineButton backButton { "Back", DineButton::Style::Standard };
    DineButton clearButton { "Clear all", DineButton::Style::Ghost };
    std::array<std::unique_ptr<QuickAction>, 3> quickButtons;
    DineButton showUnusedButton { "Show them", DineButton::Style::Standard };
};

// STEP 3: purpose (how loud it lands and how hard it may peak) and sound (the character
// every group is tuned toward), as picker cards, with what DINE will do beside them.
class PurposePage : public juce::Component
{
public:
    PurposePage (MixController&);
    ~PurposePage() override;
    std::function<void()> onContinue, onBack;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Tile;
    SetupLayout layout() const;

    MixController& controller;
    std::vector<std::unique_ptr<Tile>> purposeTiles, soundTiles;
    // How loud the finished mix should end up. Until this was a control it was a hidden
    // consequence of the purpose - "Church Broadcast" quietly meant -23 LUFS, which is right
    // for a television feed and about 9 dB under what a church stream is expected to be, and
    // nothing in the app said so. It is the number the whole gain structure is fitted against.
    DinePopup deliveryButton;
    DineButton continueButton { "Tune the mix", DineButton::Style::Filled };
    DineButton backButton { "Back", DineButton::Style::Standard };
};

} // namespace livemix
