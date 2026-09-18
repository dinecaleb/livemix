#include "SetupPages.h"
#include "AppTheme.h"
#include "Core/ProductDefinition.h"
#include "Profiles/Profile.h"
#include <algorithm>

namespace livemix
{

namespace
{
    // The sources and their plain names live in AppTheme (Dine::roleGroups /
    // Dine::friendlyRoleName), so this page and the TRACKS header menu offer the same list.
    using Dine::roleGroups;
    using Dine::friendlyRoleName;

    juce::Colour busColour (MixBus b) noexcept { return Dine::busTint (b); }

    // A bus name as it is written in a sentence rather than shouted on a fader: DRUMS -> Drums.
    juce::String busLabel (MixBus b)
    {
        const juce::String n (mixBusName (b));
        return n.substring (0, 1) + n.substring (1).toLowerCase();
    }

    // A short desk name for a source, the name a volunteer would write on tape.
    juce::String shortRoleName (ChannelRole r)
    {
        switch (r)
        {
            case ChannelRole::KickIn:               return "Kick";
            case ChannelRole::KickOut:              return "Kick out";
            case ChannelRole::SnareTop:             return "Snare";
            case ChannelRole::SnareBottom:          return "Snare btm";
            case ChannelRole::HiHat:                return "Hi-hat";
            case ChannelRole::RackTom:              return "Tom";
            case ChannelRole::FloorTom:             return "Floor tom";
            case ChannelRole::Overhead:             return "OH";
            case ChannelRole::OverheadLeft:         return "OH L";
            case ChannelRole::OverheadRight:        return "OH R";
            case ChannelRole::Room:                 return "Room";
            case ChannelRole::DrumBus:              return "Drum mix";
            case ChannelRole::BassDI:               return "Bass DI";
            case ChannelRole::BassAmp:              return "Bass amp";
            case ChannelRole::SynthBass:            return "Sub bass";
            case ChannelRole::Piano:                return "Keys";
            case ChannelRole::ElectricPiano:        return "E piano";
            case ChannelRole::Organ:                return "Organ";
            case ChannelRole::SynthPad:             return "Tracks";
            case ChannelRole::SynthLead:            return "Lead synth";
            case ChannelRole::AcousticGuitar:       return "Ac gtr";
            case ChannelRole::ElectricGuitarClean:  return "El gtr";
            case ChannelRole::ElectricGuitarDrive:  return "El gtr dr";
            case ChannelRole::LeadVocal:            return "Lead vox";
            case ChannelRole::BackingVocal:         return "BV";
            case ChannelRole::Choir:                return "Choir";
            case ChannelRole::Speech:               return "Pastor";
            case ChannelRole::CrowdMic:             return "Crowd";
            case ChannelRole::AmbienceMic:          return "Ambience";
            case ChannelRole::SaxAlto:              return "Alto sax";
            case ChannelRole::SaxTenor:             return "Tenor sax";
            case ChannelRole::SaxBari:              return "Bari sax";
            default:                                return channelRoleName (r);
        }
    }

    // A kit is a run of sources laid down a selection in order, so a drum kit patched
    // 1-9 is named and placed in one click rather than nine.
    struct Kit { const char* label; MixBus bus; std::vector<ChannelRole> roles; };

    const std::vector<Kit>& kits()
    {
        static const std::vector<Kit> k {
            { "Drum kit", MixBus::Drums, { ChannelRole::KickIn, ChannelRole::KickOut, ChannelRole::SnareTop, ChannelRole::SnareBottom,
                                           ChannelRole::HiHat, ChannelRole::RackTom, ChannelRole::FloorTom,
                                           ChannelRole::OverheadLeft, ChannelRole::OverheadRight } },
            { "Overheads and room", MixBus::Drums, { ChannelRole::OverheadLeft, ChannelRole::OverheadRight, ChannelRole::Room } },
            { "Band", MixBus::Music, { ChannelRole::BassDI, ChannelRole::ElectricGuitarClean, ChannelRole::ElectricGuitarDrive,
                                       ChannelRole::AcousticGuitar, ChannelRole::Piano, ChannelRole::SynthPad } },
            { "Keys in stereo", MixBus::Music, { ChannelRole::Piano, ChannelRole::Piano } },
            { "Singers", MixBus::Vocals, { ChannelRole::LeadVocal, ChannelRole::BackingVocal } },
            { "Speaking mics", MixBus::Speech, { ChannelRole::Speech } },
            // The building. Two of these across the room is what makes a stream sound like a
            // service rather than a studio recording of a band.
            { "Crowd and room", MixBus::Ambience, { ChannelRole::CrowdMic, ChannelRole::CrowdMic, ChannelRole::AmbienceMic } },
            { "Horns", MixBus::Music, { ChannelRole::SaxAlto, ChannelRole::SaxTenor, ChannelRole::SaxBari } }
        };
        return k;
    }

    // "2 hours ago", "Yesterday", "17 Nov": the way a library says when, not a timestamp.
    juce::String whenText (juce::Time t)
    {
        const auto now = juce::Time::getCurrentTime();
        const double minutes = (now.toMilliseconds() - t.toMilliseconds()) / 60000.0;
        if (minutes < 1.0)   return "Just now";
        if (minutes < 60.0)  return juce::String (int (minutes)) + " min ago";
        if (minutes < 24 * 60.0)
        {
            const int hours = int (minutes / 60.0);
            return juce::String (hours) + (hours == 1 ? " hour ago" : " hours ago");
        }
        if (minutes < 48 * 60.0) return "Yesterday";
        if (minutes < 8 * 24 * 60.0) return juce::String (int (minutes / (24 * 60.0))) + " days ago";
        return t.formatted ("%d %b");
    }

    constexpr int kHead = 58, kToolbar = 46, kGutter = 24;
}

// ============================================================================ the shared shape
SetupLayout SetupLayout::of (juce::Rectangle<int> page, bool withToolbar, bool withRail)
{
    SetupLayout L;
    L.footer = page.removeFromBottom (Dine::Metric::footer);
    auto r = page.reduced (Dine::Metric::padX, 0);
    r.removeFromTop (22);
    L.head = r.removeFromTop (kHead);
    L.toolbar = withToolbar ? r.removeFromTop (kToolbar) : juce::Rectangle<int>();
    if (! withToolbar) r.removeFromTop (14);
    r.removeFromBottom (14);
    if (withRail && r.getWidth() > Dine::Metric::setupRail + 420)
    {
        L.rail = r.removeFromRight (Dine::Metric::setupRail);
        r.removeFromRight (kGutter);
    }
    L.main = r;
    return L;
}

void drawSetupHead (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& sentence)
{
    g.setColour (Dine::ink);
    g.setFont (Dine::text (20.0f, 600));
    g.drawText (title, r.removeFromTop (26), juce::Justification::centredLeft);
    r.removeFromTop (6);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (13.0f));
    g.drawFittedText (sentence, r.removeFromTop (20).withWidth (juce::jmin (r.getWidth(), 620)), juce::Justification::topLeft, 1);
}

void drawSetupFooter (juce::Graphics& g, juce::Rectangle<int> page, const juce::String& note, int reservedRight)
{
    auto footer = page.removeFromBottom (Dine::Metric::footer);
    if (note.isEmpty()) return;
    auto r = footer.reduced (Dine::Metric::padX, 0);
    r.removeFromRight (reservedRight + 12);
    g.setColour (Dine::ink4);
    g.setFont (Dine::text (12.5f));
    g.drawText (note, r, juce::Justification::centredRight, true);
}

// ============================================================================ SessionsPage

class SessionsPage::Row : public juce::Button
{
public:
    Row (SessionsPage& owner, int itemIndex) : juce::Button ("session"), page (owner), index (itemIndex)
    {
        setClickingTogglesState (false);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        page.select (index);
        if (const auto* it = page.selectedItem(); it != nullptr && page.onOpen) page.onOpen (it->listing.file);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& it = page.items[size_t (index)];
        const bool on = getToggleState();
        auto b = getLocalBounds().withTrimmedBottom (8);
        Dine::fillRounded (g, b.toFloat(), on ? Dine::selected : over ? Dine::raised : Dine::card, Dine::Radius::card);

        auto r = b.reduced (16, 0);
        auto when = r.removeFromRight (kWhenW);
        r.removeFromRight (20);
        auto bars = r.removeFromRight (260);
        r.removeFromRight (20);

        // the name and what it was for
        auto text = r.withSizeKeepingCentre (r.getWidth(), 40);
        auto name = text.removeFromTop (18);
        const juce::String tag = it.open ? "Open" : it.listing.file.getFileName().endsWithIgnoreCase (".dinelive.json") ? "Older name" : juce::String();
        g.setColour (Dine::ink);
        g.setFont (Dine::text (15.0f));
        const int nameW = juce::jmin (Dine::textWidth (Dine::text (15.0f), it.listing.name), name.getWidth() - (tag.isEmpty() ? 0 : 70));
        g.drawText (it.listing.name, name.removeFromLeft (nameW), juce::Justification::centredLeft, true);
        if (tag.isNotEmpty())
        {
            name.removeFromLeft (8);
            const int w = Dine::textWidth (Dine::caps (9.0f, 0.06f, 500), tag.toUpperCase()) + 14;
            Dine::drawStatusChip (g, name.removeFromLeft (w).withSizeKeepingCentre (w, 16).toFloat(), tag.toUpperCase(),
                                  tag == "Open" ? Dine::accent : Dine::monitor);
        }
        text.removeFromTop (5);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.5f));
        g.drawText (it.summary.valid ? juce::String (styleProfileName (it.summary.profile)) + "  " + Glyph::dot() + "  "
                                           + juce::String (mixPurposeName (it.summary.purpose)) + "  " + Glyph::dot() + "  "
                                           + page.folderText (it.listing.file)
                                     : juce::String ("Not a DLIVE session"),
                    text, juce::Justification::centredLeft, true);

        // how its inputs fall across the groups
        auto barArea = bars.withSizeKeepingCentre (bars.getWidth(), 8 + 7 + 14);
        std::vector<Dine::BarSlice> slices;
        const float total = juce::jmax (1.0f, float (it.summary.inputs));
        for (int bus = 0; bus < int (MixBus::Master); ++bus)
            slices.push_back ({ float (it.summary.perBus[size_t (bus)]) / total, busColour (MixBus (bus)) });
        Dine::drawStackedBar (g, barArea.removeFromTop (8), slices);
        barArea.removeFromTop (7);
        int groupsUsed = 0;
        for (int bus = 0; bus < int (MixBus::Master); ++bus) if (it.summary.perBus[size_t (bus)] > 0) ++groupsUsed;
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (12.0f));
        g.drawText (it.summary.valid ? juce::String (it.summary.inputs) + " inputs across " + juce::String (groupsUsed) + " groups"
                                     + (it.summary.tracks > 0 ? "  " + Glyph::dot() + "  " + juce::String (it.summary.tracks) + " tracks recorded" : juce::String())
                                     : juce::String (Glyph::dash()),
                    barArea, juce::Justification::centredLeft, true);

        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        g.drawText (it.when, when, juce::Justification::centredRight, true);
    }

    static constexpr int kSoundW = 140, kPurposeW = 130, kCountW = 56, kWhenW = 120;

    SessionsPage& page;
    int index;
};

SessionsPage::SessionsPage (MixController& c, AppServices& s) : controller (c), services (s)
{
    viewport.setViewedComponent (&listHolder, false);
    Dine::nativeScrolling (viewport);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    search.setFont (Dine::text (12.5f));
    search.setTextToShowWhenEmpty ("Search", Dine::ink4);
    search.setIndents (24, 0);
    search.setBorder (juce::BorderSize<int> (0));
    search.setColour (juce::TextEditor::backgroundColourId, Dine::control);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, Dine::accent);
    search.setColour (juce::TextEditor::textColourId, Dine::ink);
    search.setTextToShowWhenEmpty ("Search sessions", Dine::ink4);
    search.setIndents (12, 0);
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (search);

    const char* names[3] = { "All", "Recent", "Templates" };
    for (int i = 0; i < 3; ++i)
    {
        chips[size_t (i)] = std::make_unique<DineChip> (names[i]);
        chips[size_t (i)]->onClick = [this, i] { filter = i; rebuild(); };
        addAndMakeVisible (*chips[size_t (i)]);
    }

    addAndMakeVisible (newButton);
    addAndMakeVisible (openButton);
    addAndMakeVisible (revealButton);
    newButton.setCaps (false);
    openButton.setCaps (false);
    newButton.setTooltip ("Start from nothing: pick the device, name the inputs, then tune.");
    newButton.onClick = [this] { if (onNew) onNew(); };
    openButton.onClick = [this]
    {
        if (const auto* it = selectedItem(); it != nullptr && onOpen) onOpen (it->listing.file);
    };
    revealButton.onClick = [this]
    {
        if (const auto* it = selectedItem()) it->listing.file.revealToUser();
    };
    refresh();
}

SessionsPage::~SessionsPage() = default;

void SessionsPage::refresh()
{
    const auto listed = services.listSessions();
    const auto openName = services.currentSessionName();
    items.clear();
    for (const auto& L : listed)
    {
        Item it;
        it.listing = L;
        const auto key = L.file.getFullPathName() + "|" + juce::String (L.modified.toMilliseconds());
        auto found = cache.find (key);
        if (found == cache.end()) found = cache.emplace (key, SessionStore::summarise (L.file)).first;
        it.summary = found->second;
        it.when = whenText (L.modified);
        it.open = openName.isNotEmpty() && L.name == openName;
        items.push_back (it);
    }
    if (selected < 0)
        for (int i = 0; i < int (items.size()); ++i)
            if (items[size_t (i)].open) selected = i;
    if (selected < 0 && ! items.empty()) selected = 0;
    rebuild();
}

void SessionsPage::rebuild()
{
    const auto q = search.getText().trim().toLowerCase();
    shown.clear();
    for (int i = 0; i < int (items.size()); ++i)
    {
        const auto& it = items[size_t (i)];
        if (filter == 1 && juce::Time::getCurrentTime().toMilliseconds() - it.listing.modified.toMilliseconds() > 7LL * 24 * 3600 * 1000) continue;
        if (filter == 2 && ! it.listing.name.containsIgnoreCase ("template")) continue;
        if (q.isNotEmpty()
            && ! it.listing.name.toLowerCase().contains (q)
            && ! juce::String (styleProfileName (it.summary.profile)).toLowerCase().contains (q)
            && ! juce::String (mixPurposeName (it.summary.purpose)).toLowerCase().contains (q))
            continue;
        shown.push_back (i);
    }

    rows.clear();
    listHolder.removeAllChildren();
    for (int index : shown)
    {
        auto row = std::make_unique<Row> (*this, index);
        row->onClick = [this, index] { select (index); };
        row->onStateChange = [] {};
        row->setToggleState (index == selected, juce::dontSendNotification);
        listHolder.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
    for (int i = 0; i < 3; ++i) chips[size_t (i)]->setToggleState (filter == i, juce::dontSendNotification);
    const bool any = selectedItem() != nullptr;
    openButton.setEnabled (any);
    revealButton.setEnabled (any);
    resized();
    repaint();
}

void SessionsPage::select (int index)
{
    selected = index;
    for (auto& r : rows) r->setToggleState (r->index == selected, juce::dontSendNotification);
    openButton.setEnabled (selectedItem() != nullptr);
    revealButton.setEnabled (selectedItem() != nullptr);
    repaint();
}

juce::String SessionsPage::folderText (const juce::File& file)
{
    const auto folder = file.getParentDirectory();
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    const auto full = folder.getFullPathName();
    if (full.startsWith (home)) return "~" + full.substring (home.length());
    return Glyph::ellip() + "/" + folder.getParentDirectory().getFileName() + "/" + folder.getFileName();
}

const SessionsPage::Item* SessionsPage::selectedItem() const
{
    if (selected < 0 || selected >= int (items.size())) return nullptr;
    return &items[size_t (selected)];
}

void SessionsPage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto L = SetupLayout::of (getLocalBounds(), true, false);

    auto head = L.head;
    head.removeFromRight (juce::jmax (0, getWidth() - search.getX()) + 10);
    drawSetupHead (g, head, "Library", "Opening a session puts its inputs, groups and tune back exactly as they were.");

    if (chips[0] != nullptr)
    {
        auto track = chips[0]->getBounds();
        for (int i = 1; i < 3; ++i) track = track.getUnion (chips[size_t (i)]->getBounds());
        Dine::drawSegmentTrack (g, track.expanded (2, 2));
    }
    g.setColour (Dine::ink4);
    g.setFont (Dine::text (12.5f));
    g.drawText ("Sorted by when it was last saved", L.toolbar.withTrimmedRight (2), juce::Justification::centredRight, true);

    if (shown.empty())
    {
        auto empty = L.main.reduced (30, 40).removeFromTop (90);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (15.0f, 600));
        g.drawText (items.empty() ? "No sessions saved yet" : "Nothing matches that",
                    empty.removeFromTop (20), juce::Justification::centredTop);
        empty.removeFromTop (6);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        g.drawFittedText (items.empty() ? "Start a new session and it is saved into ~/Music/DLIVE as you work."
                                        : "Try a different word, or switch the filter back to All.",
                          empty.removeFromTop (34), juce::Justification::centredTop, 2);
    }

    drawSetupFooter (g, getLocalBounds(), items.empty() ? juce::String ("Sessions live in ~/Music/DLIVE. The audio stays in its own folder.")
                                                        : juce::String (items.size()) + (items.size() == 1 ? " session" : " sessions")
                                                              + " in your library  " + Glyph::dot() + "  ~/Music/DLIVE",
                     openButton.getWidth() + revealButton.getWidth() + 10);
}

void SessionsPage::resized()
{
    const auto L = SetupLayout::of (getLocalBounds(), true, false);

    auto head = L.head.withHeight (Dine::Metric::button);
    const int nw = juce::jmax (110, newButton.idealWidth());
    newButton.setBounds (head.removeFromRight (nw));
    head.removeFromRight (10);
    search.setBounds (head.removeFromRight (200).withHeight (Dine::Metric::button));

    auto chipRow = L.toolbar.withSizeKeepingCentre (L.toolbar.getWidth(), Dine::Metric::control);
    int x = chipRow.getX() + 2;
    for (int i = 0; i < 3; ++i)
    {
        const int w = juce::jmax (58, chips[size_t (i)]->idealWidth());
        chips[size_t (i)]->setBounds (x, chipRow.getY() + 2, w, chipRow.getHeight() - 4);
        x += w;
    }

    auto list = L.main;
    viewport.setBounds (list);
    const int rowH = 74;
    const int total = int (rows.size()) * rowH;
    listHolder.setSize (list.getWidth() - (total > list.getHeight() ? 10 : 0), juce::jmax (total, list.getHeight()));
    int y = 0;
    for (auto& r : rows) { r->setBounds (0, y, listHolder.getWidth(), rowH); y += rowH; }

    auto footer = L.footer.reduced (Dine::Metric::padX, 0);
    const int ow = juce::jmax (120, openButton.idealWidth());
    openButton.setBounds (footer.removeFromRight (ow).withSizeKeepingCentre (ow, Dine::Metric::button));
    footer.removeFromRight (10);
    const int rw = juce::jmax (120, revealButton.idealWidth());
    revealButton.setBounds (footer.removeFromRight (rw).withSizeKeepingCentre (rw, Dine::Metric::button));
}

// ============================================================================ DevicePage

class DevicePage::DeviceRow : public juce::Button
{
public:
    DeviceRow (const juce::String& name, int in, int out, bool firstRow, bool isOpen)
        : juce::Button (name), deviceName (name), inputChannels (in), outputChannels (out), first (firstRow), open (isOpen) {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto b = getLocalBounds().withTrimmedBottom (8);
        Dine::fillRounded (g, b.toFloat(), on ? Dine::selected : over ? Dine::raised : Dine::card, Dine::Radius::card);

        auto r = b.reduced (14, 0);
        Dine::drawRadio (g, r.removeFromLeft (14).toFloat(), on);
        r.removeFromLeft (14);

        const bool usable = inputChannels > 0;
        auto text = r.withSizeKeepingCentre (r.getWidth(), 38);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.5f));
        g.drawText (deviceName, text.removeFromTop (18), juce::Justification::centredLeft, true);
        text.removeFromTop (4);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.5f));
        juce::String meta = juce::String (inputChannels) + " in  " + Glyph::dot() + "  " + juce::String (outputChannels) + " out";
        if (! usable)  meta += "  " + Glyph::dot() + "  output only: nothing comes in this way";
        else if (open) meta += "  " + Glyph::dot() + "  open now";
        g.drawText (meta, text, juce::Justification::centredLeft, true);
    }

    juce::String deviceName;
    int inputChannels, outputChannels;
    bool first, open;
};

// The output pair the mix is monitored through: a radio row in the rail.
class DevicePage::OutputRow : public juce::Button
{
public:
    OutputRow (const juce::String& name, int channels) : juce::Button (name), deviceName (name), outputChannels (channels) {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto b = getLocalBounds().withTrimmedBottom (4);
        Dine::fillRounded (g, b.toFloat(), on ? Dine::selected : over ? Dine::raised : Dine::card, Dine::Radius::control);
        auto r = b.reduced (10, 0);
        Dine::drawRadio (g, r.removeFromLeft (14).toFloat(), on);
        r.removeFromLeft (10);
        auto name = r.removeFromTop (r.getHeight() / 2 + 1).withTrimmedTop (4);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.5f));
        g.drawText (deviceName, name, juce::Justification::centredLeft, true);
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (11.0f));
        g.drawText (outputChannels >= 2 ? "Output 1-2  " + Glyph::dot() + "  " + juce::String (outputChannels) + " available"
                                        : "No stereo pair",
                    r, juce::Justification::centredLeft, true);
    }

    juce::String deviceName;
    int outputChannels;
};

DevicePage::DevicePage (MixController& c, AppServices& s) : controller (c), services (s)
{
    viewport.setViewedComponent (&listHolder, false);
    Dine::nativeScrolling (viewport);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    addAndMakeVisible (continueButton);
    addAndMakeVisible (backButton);
    addAndMakeVisible (rescanButton);
    addAndMakeVisible (recordingButton);
    addAndMakeVisible (outputsButton);
    continueButton.setCaps (false);
    rescanButton.setIcon (Dine::Icon::Refresh);
    recordingButton.setButtonText ("Import a multitrack folder");
    outputsButton.setTooltip ("Send the mix to more than one pair of outputs at once: the PA on 1-2, headphones or a "
                              "cue on 3-4, each with its own level.");
    outputsButton.onClick = [this] { if (onSetUpOutputs) onSetUpOutputs(); };
    backButton.onClick = [this] { if (onBack) onBack(); };
    rescanButton.onClick = [this] { refresh(); };
    recordingButton.setTooltip ("Turn a folder of recorded stems (AIFF / WAV / FLAC) into tracks, so you can mix, tune and export without a band in the room.");
    recordingButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder of multitrack stems", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            const auto folder = fc.getResult();
            if (folder.isDirectory() && onImportRecording) onImportRecording (folder);
        });
    };
    continueButton.onClick = [this]
    {
        if (selected < 0 || selected >= inputs.size()) return;
        error = services.openDevices (inputs[selected].name, outputName);
        if (error.isNotEmpty()) { repaint(); return; }
        if (onContinue) onContinue();
    };
    refresh();
}

DevicePage::~DevicePage() = default;

void DevicePage::refresh()
{
    inputs = services.inputDevices();
    outputs = services.outputDevices();
    rows.clear();
    listHolder.removeAllChildren();
    const juce::String current = services.currentInputDevice();
    selected = -1;
    for (int i = 0; i < inputs.size(); ++i)
    {
        auto row = std::make_unique<DeviceRow> (inputs[i].name, inputs[i].inputChannels, inputs[i].outputChannels,
                                                i == 0, services.isAudioRunning() && inputs[i].name == current);
        row->setClickingTogglesState (false);
        row->onClick = [this, i] { select (i); };
        listHolder.addAndMakeVisible (*row);
        if (inputs[i].name == current) selected = i;
        rows.push_back (std::move (row));
    }
    if (selected < 0 && ! inputs.isEmpty())
    {
        // Prefer the device with the most inputs: that is the console or Dante, not the built-in mic.
        int best = 0;
        for (int i = 1; i < inputs.size(); ++i) if (inputs[i].inputChannels > inputs[best].inputChannels) best = i;
        selected = best;
    }
    outputName = services.outputDisplayName();
    if (outputName.isEmpty() && selected >= 0)
    {
        // Same device when it has outputs, else the first output device.
        for (const auto& o : outputs) if (o.name == inputs[selected].name) outputName = o.name;
        if (outputName.isEmpty() && ! outputs.isEmpty()) outputName = outputs[0].name;
    }

    outputRows.clear();
    for (int i = 0; i < outputs.size(); ++i)
    {
        auto row = std::make_unique<OutputRow> (outputs[i].name, outputs[i].outputChannels);
        row->setClickingTogglesState (false);
        row->onClick = [this, i] { selectOutput (i); };
        row->setToggleState (outputs[i].name == outputName, juce::dontSendNotification);
        addAndMakeVisible (*row);
        outputRows.push_back (std::move (row));
    }

    for (int i = 0; i < int (rows.size()); ++i) rows[size_t (i)]->setToggleState (i == selected, juce::dontSendNotification);
    continueButton.setEnabled (selected >= 0 && inputs[selected].inputChannels > 0);
    resized();
    repaint();
}

void DevicePage::select (int index)
{
    selected = index;
    for (int i = 0; i < int (rows.size()); ++i) rows[size_t (i)]->setToggleState (i == selected, juce::dontSendNotification);
    continueButton.setEnabled (selected >= 0 && inputs[selected].inputChannels > 0);
    repaint();
}

void DevicePage::selectOutput (int index)
{
    if (index < 0 || index >= outputs.size() || outputs[index].name == outputName) return;
    outputName = outputs[index].name;
    for (int i = 0; i < int (outputRows.size()); ++i)
        outputRows[size_t (i)]->setToggleState (i == index, juce::dontSendNotification);
    // Already listening: change where it comes out without walking setup again. Re-opening a
    // device is not free, so this happens only when the pair really changed.
    if (services.isAudioRunning()) error = services.changeOutput (outputName);
    repaint();
}

int DevicePage::liveInputCount() const
{
    return services.isAudioRunning() ? services.daw().numInputsCarryingSignal() : 0;
}

SetupLayout DevicePage::layout() const { return SetupLayout::of (getLocalBounds(), false, true); }

DevicePage::Column DevicePage::column() const
{
    Column c;
    auto main = layout().main;
    c.listCaption = main.removeFromTop (14);
    main.removeFromTop (4);
    const int specH = 4 * 34 + 12;
    const int below = 20 + 14 + 8 + specH + 18 + 44;
    c.list = main.removeFromTop (juce::jlimit (74, juce::jmax (74, 6 * 74), juce::jmax (74, main.getHeight() - below)));
    main.removeFromTop (juce::jmax (20, main.getHeight() - below));
    c.specCaption = main.removeFromTop (14);
    main.removeFromTop (8);
    c.spec = main.removeFromTop (specH);
    main.removeFromTop (18);
    c.importCard = main.removeFromTop (44);
    return c;
}

void DevicePage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto L = layout();
    const int channels = services.isAudioRunning() ? services.numInputChannels()
                       : selected >= 0 && selected < inputs.size() ? inputs[selected].inputChannels : 0;

    drawSetupHead (g, L.head, "Audio device",
                   "macOS opens one device at a time. Two at once means an Aggregate Device - DLIVE builds one for solo when it needs to.");

    // ---- the device list
    const auto col = column();
    Dine::drawSection (g, col.listCaption, "DEVICES ON THIS MACHINE");
    if (rows.empty())
    {
        auto r = juce::Rectangle<int> (col.list).withSizeKeepingCentre (col.list.getWidth() - 60, 74);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (14.0f, 600));
        g.drawText ("No inputs yet", r.removeFromTop (18), juce::Justification::centredLeft);
        r.removeFromTop (4);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText ("Connect your interface or console and rescan. You can also work from a folder of stems while nothing is plugged in.",
                          r.removeFromTop (36), juce::Justification::topLeft, 2);
    }

    // ---- what it is running at
    Dine::drawSection (g, col.specCaption, "WHAT IT IS RUNNING AT");
    auto spec = col.spec;
    Dine::fillRounded (g, spec.toFloat(), Dine::card, Dine::Radius::card);
    spec = spec.reduced (0, 6);
    const bool running = services.isAudioRunning();
    struct Line { juce::String key, value, note; juce::Colour colour; };
    const Line lines[4] = {
        { "Sample rate", running ? juce::String (services.sampleRate() / 1000.0, 1) + " kHz" : juce::String ("48.0 kHz"),
          running ? "Matched to the console" : "Set when the device opens", Dine::ink },
        { "Buffer", running ? juce::String (services.bufferSize()) + " samples" : juce::String (Glyph::dash()),
          running ? juce::String (1000.0 * services.bufferSize() / juce::jmax (1.0, services.sampleRate()), 1) + " ms each way"
                  : juce::String ("The lower it is, the sooner you hear it"), Dine::ink },
        { "Inputs seen", juce::String (channels), "Channels DLIVE can listen to", channels > 0 ? Dine::accent : Dine::ink3 },
        { "Dropped buffers", running ? juce::String (services.xrunCount()) : juce::String (Glyph::dash()),
          services.xrunCount() > 0 ? "Raise the buffer if this keeps climbing" : "Nothing missed",
          services.xrunCount() > 0 ? Dine::warn : Dine::ink }
    };
    for (int i = 0; i < 4; ++i)
    {
        auto row = spec.removeFromTop (34);
        auto r = row.reduced (16, 0);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawText (lines[i].key, r.removeFromLeft (130), juce::Justification::centredLeft, true);
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (12.0f));
        g.drawText (lines[i].note, r.removeFromRight (juce::jmin (260, r.getWidth() / 2)), juce::Justification::centredRight, true);
        g.setColour (lines[i].colour);
        g.setFont (Dine::mono (12.5f, 500));
        g.drawText (lines[i].value, r, juce::Justification::centredLeft, true);
    }

    // ---- no band in the room
    {
        auto r = col.importCard;
        r.removeFromLeft (recordingButton.getWidth() + 14);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.5f));
        g.drawText ("No band in the room? A folder of stems becomes tracks and clips, and everything from here works exactly as it does live.",
                    r, juce::Justification::centredLeft, true);
    }

    if (error.isNotEmpty())
    {
        g.setColour (Dine::crit);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText (error, juce::Rectangle<int> (L.main).removeFromBottom (30), juce::Justification::centredLeft, 2);
    }

    // ---- the right column: what is arriving, then where it comes out
    if (! L.rail.isEmpty())
    {
        auto rail = L.rail;
        Dine::drawSection (g, rail.removeFromTop (14), "WHAT IS ARRIVING");
        rail.removeFromTop (6);
        const int n = juce::jmin (channels, 16);
        const int outsH = int (outputRows.size()) * 38 + 14 + 6 + 8 + Dine::Metric::control;
        auto meters = rail.removeFromTop (juce::jmax (0, rail.getHeight() - outsH - 18));
        if (n == 0)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (12.5f));
            g.drawFittedText (running ? "No inputs on this device." : "Press Continue and the meters fill in - your console is untouched.",
                              meters.removeFromTop (40), juce::Justification::topLeft, 2);
        }
        for (int c = 0; c < n; ++c)
        {
            if (meters.getHeight() < 22) break;
            auto row = meters.removeFromTop (22);
            g.setColour (Dine::ink4);
            g.setFont (Dine::mono (11.0f));
            g.drawText (juce::String (c + 1).paddedLeft ('0', 2), row.removeFromLeft (24), juce::Justification::centredLeft);
            row.removeFromLeft (10);
            const float db = running ? services.daw().inputPeakDb (c) : -120.0f;
            auto note = row.removeFromRight (110);
            row.removeFromRight (10);
            Dine::fillMeter (g, row.withSizeKeepingCentre (row.getWidth(), 6).toFloat(), DineMeter::norm (db), false, false, 2.0f);
            const bool faint = running && db <= -54.0f;
            const bool clip = db > -0.2f;
            g.setColour (clip ? Dine::crit : Dine::warn);
            g.setFont (Dine::text (11.0f));
            g.drawText (clip ? "clipping" : faint ? "nothing arriving" : juce::String(), note, juce::Justification::centredLeft);
        }
        if (channels > 16)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (11.0f));
            g.drawText ("and " + juce::String (channels - 16) + " more", meters.removeFromTop (16), juce::Justification::centredLeft);
        }

        auto outs = rail.removeFromBottom (outsH);
        Dine::drawSection (g, outs.removeFromTop (14), "WHERE IT COMES OUT");
    }

    const juce::String note = selected < 0 || selected >= inputs.size() ? juce::String ("No audio device found.")
                            : inputs[selected].inputChannels <= 0 ? juce::String ("Pick a device with inputs to carry on.")
                            : inputs[selected].name + "  " + Glyph::dot() + "  " + juce::String (inputs[selected].inputChannels) + " inputs ready.";
    drawSetupFooter (g, getLocalBounds(), note, continueButton.getWidth() + backButton.getWidth() + 10);
}

void DevicePage::resized()
{
    const auto L = layout();
    const auto col = column();

    viewport.setBounds (col.list);
    viewport.setVisible (! rows.empty());
    const int rowH = 74;
    const int total = int (rows.size()) * rowH;
    listHolder.setSize (viewport.getWidth() - (total > viewport.getHeight() ? 10 : 0), juce::jmax (total, viewport.getHeight()));
    int y = 0;
    for (auto& r : rows) { r->setBounds (0, y, listHolder.getWidth(), rowH); y += rowH; }

    {
        const int w = juce::jmax (140, recordingButton.idealWidth());
        recordingButton.setBounds (juce::Rectangle<int> (col.importCard).removeFromLeft (w).withSizeKeepingCentre (w, Dine::Metric::button));
    }

    if (! L.rail.isEmpty())
    {
        auto rail = L.rail;
        const int outsH = int (outputRows.size()) * 38 + 14 + 6 + 8 + Dine::Metric::control;
        auto outs = rail.removeFromBottom (outsH);
        outs.removeFromTop (14 + 6);
        for (auto& r : outputRows) r->setBounds (outs.removeFromTop (38));
        outs.removeFromTop (8);
        outputsButton.setBounds (outs.removeFromTop (Dine::Metric::control));
    }
    else
    {
        for (auto& r : outputRows) r->setBounds (0, 0, 0, 0);
        outputsButton.setBounds (0, 0, 0, 0);
    }

    auto footer = L.footer.reduced (Dine::Metric::padX, 0);
    const int cw = juce::jmax (110, continueButton.idealWidth());
    continueButton.setBounds (footer.removeFromRight (cw).withSizeKeepingCentre (cw, Dine::Metric::button));
    footer.removeFromRight (10);
    const int bw = juce::jmax (72, backButton.idealWidth());
    backButton.setBounds (footer.removeFromRight (bw).withSizeKeepingCentre (bw, Dine::Metric::button));
    const int rw = juce::jmax (130, rescanButton.idealWidth());
    rescanButton.setBounds (footer.removeFromLeft (rw).withSizeKeepingCentre (rw, Dine::Metric::button));
}

// ============================================================================ AssignPage

class AssignPage::Row : public juce::Component
{
public:
    Row (AssignPage& owner, int index) : page (owner), input (index)
    {
        addAndMakeVisible (name);
        name.setFont (Dine::text (13.0f));
        name.setIndents (6, 0);
        name.setBorder (juce::BorderSize<int> (0));
        name.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        name.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        name.setColour (juce::TextEditor::focusedOutlineColourId, Dine::accent);
        name.setColour (juce::TextEditor::textColourId, Dine::ink);
        name.setColour (juce::TextEditor::highlightedTextColourId, Dine::ink);
        name.setTextToShowWhenEmpty ("Untitled", Dine::ink4);
        name.setSelectAllWhenFocused (true);
        name.onTextChange = [this] { page.entries[size_t (input)].name = name.getText(); };
        name.onReturnKey = [this] { name.giveAwayKeyboardFocus(); page.commit(); };
        name.onFocusLost = [this] { page.commit(); };

        addAndMakeVisible (suggest);
        suggest.setIcon (Dine::Icon::UpDown);
        suggest.setPadX (3);
        suggest.setTooltip ("Names DLIVE can suggest for this input.");
        suggest.onClick = [this] { page.showNameMenu (input, suggest); };

        addAndMakeVisible (source);
        source.onClick = [this] { page.showSourceMenu (input, source); };

        addAndMakeVisible (link);
        link.setClickingTogglesState (false);
        link.onClick = [this]
        {
            auto& e = page.entries[size_t (input)];
            if (input + 1 >= int (page.entries.size())) return;
            e.linkedToNext = ! e.linkedToNext;
            page.entries[size_t (input) + 1].linkedFromPrevious = e.linkedToNext;
            page.commit();
            page.rebuild();
        };
    }

    void refresh()
    {
        const auto& e = page.entries[size_t (input)];
        name.setText (e.name, false);
        source.setValue (e.assigned ? friendlyRoleName (e.role) : "Not used");
        link.setToggleState (e.linkedToNext, juce::dontSendNotification);
        // Linking is offered where it can happen: this input is something, the next input
        // exists, and it is not already the right half of somebody else's pair.
        link.setVisible (e.assigned && input + 1 < int (page.entries.size())
                         && (e.linkedToNext || ! page.entries[size_t (input) + 1].linkedFromPrevious));
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // The row itself picks the input out; the controls on it keep their own clicks.
        page.toggleSelection (input, e.mods.isShiftDown());
    }

    void paint (juce::Graphics& g) override
    {
        const auto& e = page.entries[size_t (input)];
        const auto bus = e.assigned ? mixBusForRole (e.role) : MixBus::Master;
        const auto tint = e.assigned ? busColour (bus) : Dine::ink4;
        auto b = getLocalBounds().withTrimmedBottom (3);
        Dine::fillRounded (g, b.toFloat(), e.selected ? Dine::selected : isMouseOver (true) ? Dine::selected.withAlpha (0.5f) : Dine::raised, Dine::Radius::control);

        auto r = b.reduced (12, 0);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (11.0f));
        g.drawText (e.linkedToNext ? juce::String (input + 1) + Glyph::minus() + juce::String (input + 2) : juce::String (input + 1),
                    r.removeFromLeft (kNumW), juce::Justification::centredLeft);

        // the signal column: what is arriving on this channel right now
        const float db = page.services.isAudioRunning() ? page.services.daw().inputPeakDb (input) : -120.0f;
        auto meter = r.removeFromLeft (kSignalW).withSizeKeepingCentre (kSignalW, 6);
        Dine::fillMeter (g, meter.toFloat(), DineMeter::norm (db), false, ! e.assigned, 2.0f);
        r.removeFromLeft (kGap);
        r.removeFromLeft (kIconW);
        Dine::drawIcon (g, e.assigned ? Dine::iconFor (e.icon, e.role) : Dine::Icon::Dash,
                        juce::Rectangle<int> (r.getX() - kIconW, r.getY(), kIconW - 6, r.getHeight()).toFloat().withSizeKeepingCentre (15.0f, 15.0f), tint);

        // the bus, in its colour, right of the source popup
        auto right = b.reduced (12, 0);
        right.removeFromRight (kPairW + kGap);
        auto busCell = right.removeFromRight (kBusW);
        g.setColour (tint);
        g.setFont (Dine::caps (11.0f, 0.06f, 500));
        g.drawText (e.assigned ? juce::String (mixBusName (bus)).toUpperCase() : "NOT USED", busCell, juce::Justification::centredLeft, true);

        // an unassigned input that is carrying signal is worth saying out loud
        if (! e.assigned && db > -54.0f)
        {
            auto flag = right;
            flag.removeFromRight (kSourceW + kGap);
            const juce::String text = db > -30.0f ? "SIGNAL HERE" : "FAINT";
            const int w = Dine::textWidth (Dine::caps (9.0f, 0.06f, 500), text) + 14;
            if (flag.getWidth() > w + 220)
                Dine::drawStatusChip (g, flag.removeFromRight (w).withSizeKeepingCentre (w, 16).toFloat(), text, Dine::warn);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedBottom (3).reduced (12, 0);
        r.removeFromLeft (kNumW + kSignalW + kGap + kIconW);
        auto pair = r.removeFromRight (kPairW);
        link.setBounds (pair.withSizeKeepingCentre (kPairW, 20));
        r.removeFromRight (kGap);
        r.removeFromRight (kBusW);
        source.setBounds (r.removeFromRight (kSourceW).withSizeKeepingCentre (kSourceW, Dine::Metric::control));
        r.removeFromRight (kGap);
        r = r.removeFromLeft (juce::jmin (r.getWidth(), kNameW));
        suggest.setBounds (r.removeFromRight (20).withSizeKeepingCentre (20, 20));
        name.setBounds (r.withSizeKeepingCentre (r.getWidth(), 22));
    }

    static constexpr int kNumW = 34, kGap = 12, kIconW = 24, kNameW = 230, kSignalW = 70, kSourceW = 200, kPairW = 80, kBusW = 110;

    AssignPage& page;
    int input;
    juce::TextEditor name;
    DineButton suggest { "", DineButton::Style::Ghost };
    DinePopup source;
    DineSwitch link { "L/R", "Link" };
};

class AssignPage::GroupHeader : public juce::Button
{
public:
    GroupHeader (const juce::String& groupName, juce::Colour c, int count, bool unusedGroup)
        : juce::Button (groupName), name (groupName), colour (c), n (count), unused (unusedGroup)
    {
        setClickingTogglesState (false);
        setTooltip (unused ? "Pick out every input that is not used, and set them in one go."
                           : "Pick out every input in " + groupName + ".");
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().reduced (12, 0);
        g.setColour (colour);
        g.fillEllipse (r.removeFromLeft (7).toFloat().withSizeKeepingCentre (7.0f, 7.0f));
        r.removeFromLeft (8);
        g.setColour (over ? Dine::ink : Dine::ink4);
        g.setFont (Dine::caps (9.5f, 0.12f));
        const int w = Dine::textWidth (Dine::caps (9.5f, 0.12f), name.toUpperCase());
        g.drawText (name.toUpperCase(), r.removeFromLeft (w), juce::Justification::centredLeft);
        r.removeFromLeft (8);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
        g.drawText (juce::String (n), r.removeFromLeft (24), juce::Justification::centredLeft);
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (11.0f));
        g.drawText (over ? "Select them all" : unused ? "stays out of the mix" : juce::String(),
                    r, juce::Justification::centredRight, true);
    }

    juce::String name;
    juce::Colour colour;
    int n;
    bool unused;
};

class AssignPage::QuickAction : public juce::Button
{
public:
    QuickAction (const juce::String& title, const juce::String& line)
        : juce::Button (title), heading (title), detail (line) { setClickingTogglesState (false); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds();
        if (over || down)
            Dine::fillRounded (g, b.toFloat(), juce::Colours::white.withAlpha (down ? 0.10f : 0.06f), Dine::Radius::chip);
        auto r = b.reduced (8, 0);
        Dine::drawIcon (g, Dine::Icon::Chevron, r.removeFromRight (11).toFloat().withSizeKeepingCentre (11.0f, 11.0f), Dine::ink4);
        r.removeFromRight (4);
        auto title = r.removeFromTop (r.getHeight() / 2 + 1).withTrimmedTop (5);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.5f));
        g.drawText (heading, title, juce::Justification::centredLeft, true);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f));
        g.drawFittedText (detail, r.withTrimmedBottom (4), juce::Justification::topLeft, 2);
    }

    juce::String heading, detail;
};

AssignPage::AssignPage (MixController& c, AppServices& s) : controller (c), services (s)
{
    viewport.setViewedComponent (&listHolder, false);
    Dine::nativeScrolling (viewport);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    search.setFont (Dine::text (12.5f));
    search.setTextToShowWhenEmpty ("Search", Dine::ink4);
    search.setIndents (24, 0);
    search.setBorder (juce::BorderSize<int> (0));
    search.setColour (juce::TextEditor::backgroundColourId, Dine::control);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, Dine::accent);
    search.setColour (juce::TextEditor::textColourId, Dine::ink);
    search.setIndents (12, 0);
    search.onTextChange = [this] { query = search.getText(); rebuild(); };
    addAndMakeVisible (search);

    // The bus filter: All, then one chip per group in its own colour, then Not used.
    chips.push_back (std::make_unique<DineChip> ("All"));
    chips.back()->onClick = [this] { busFilter = -2; rebuild(); };
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        chips.push_back (std::make_unique<DineChip> (busLabel (MixBus (b)), busColour (MixBus (b))));
        chips.back()->onClick = [this, b] { busFilter = busFilter == b ? -2 : b; rebuild(); };
    }
    chips.push_back (std::make_unique<DineChip> ("Not used", juce::Colours::white.withAlpha (0.25f)));
    chips.back()->onClick = [this] { busFilter = busFilter == -1 ? -2 : -1; rebuild(); };
    for (auto& c : chips) addAndMakeVisible (*c);

    for (auto* b : { &selectAllButton, &deskLabelsButton, &groupButton, &bulkButton, &kitButton, &nameButton,
                     &linkButton, &dropButton, &deselectButton, &continueButton, &backButton, &clearButton,
                     &showUnusedButton })
        addAndMakeVisible (*b);
    continueButton.setCaps (false);
    groupButton.setIcon (Dine::Icon::List);
    groupButton.setPadX (5);
    groupButton.setTooltip ("Group the inputs by the bus they feed, or list them in channel order.");
    bulkButton.setIcon (Dine::Icon::UpDown);
    kitButton.setIcon (Dine::Icon::UpDown);
    kitButton.setTooltip ("Lay a whole kit down the selection, in order: the first input gets the first source, the next the next.");
    deskLabelsButton.setTooltip ("Every input still without a name takes its channel number, the way it is written on the desk.");

    selectAllButton.onClick = [this]
    {
        const bool all = [this]
        {
            for (int i = 0; i < numInputs; ++i)
                if (visible (i) && ! entries[size_t (i)].linkedFromPrevious && ! entries[size_t (i)].selected) return false;
            return true;
        }();
        for (int i = 0; i < numInputs; ++i)
            if (visible (i) && ! entries[size_t (i)].linkedFromPrevious) entries[size_t (i)].selected = ! all;
        rebuild();
    };
    deskLabelsButton.onClick = [this]
    {
        for (int i = 0; i < numInputs; ++i)
            if (entries[size_t (i)].name.trim().isEmpty())
                entries[size_t (i)].name = "Desk " + juce::String (i + 1).paddedLeft ('0', 2);
        commit();
        rebuild();
    };
    groupButton.onClick = [this] { grouped = ! grouped; rebuild(); };
    bulkButton.onClick = [this] { showBulkMenu (bulkButton); };
    kitButton.onClick = [this] { showKitMenu (kitButton); };
    nameButton.onClick = [this] { nameFromRole(); };
    linkButton.onClick = [this] { linkSelection(); };
    dropButton.onClick = [this] { dropSelection(); };
    deselectButton.onClick = [this] { clearSelection(); };
    clearButton.onClick = [this] { clearAll(); };
    continueButton.onClick = [this] { commit(); if (assignedCount() > 0 && onContinue) onContinue(); };
    backButton.onClick = [this] { if (onBack) onBack(); };
    showUnusedButton.onClick = [this] { busFilter = -1; rebuild(); };

    patchSaveButton.onClick = [this] { if (onSaveMapping) onSaveMapping(); };
    patchApplyButton.onClick = [this] { if (onApplyMapping) onApplyMapping(); };
    patchSaveButton.setTooltip ("Save this patch - device channel, name, source, stereo link - so next Sunday is one click.");
    patchApplyButton.setTooltip ("Apply a saved patch. An input this device cannot provide comes back switched off and named.");
    addAndMakeVisible (patchSaveButton);
    addAndMakeVisible (patchApplyButton);
    quickButton.setTooltip ("The three things worth doing to a whole session at once.");
    quickButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Name everything from what it is");
        m.addItem (2, "Select every input not used");
        m.addItem (3, "Pair every L and R");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&quickButton).withMinimumWidth (260),
                         [this] (int r) { if (r >= 1 && r <= 3 && quickButtons[size_t (r - 1)]->onClick) quickButtons[size_t (r - 1)]->onClick(); });
    };
    addAndMakeVisible (quickButton);

    // The three things worth doing to a whole session at once.
    const char* quickLabels[3] = { "Name everything from what it is", "Select every input not used", "Pair every L and R" };
    const char* quickLines[3] = { "Every assigned input takes its short desk name.",
                                  "Then set them all at once, or fill a kit down them.",
                                  "Neighbours named L and R become one stereo row." };
    for (int i = 0; i < 3; ++i)
    {
        quickButtons[size_t (i)] = std::make_unique<QuickAction> (quickLabels[i], quickLines[i]);
        addChildComponent (*quickButtons[size_t (i)]);
    }
    quickButtons[0]->setTooltip ("Every assigned input takes its short desk name: Kick, Snare, OH L.");
    quickButtons[0]->onClick = [this]
    {
        for (int i = 0; i < numInputs; ++i)
            if (entries[size_t (i)].assigned) entries[size_t (i)].name = shortRoleName (entries[size_t (i)].role);
        commit();
        rebuild();
    };
    quickButtons[1]->setTooltip ("Then set them all at once, or fill a kit down them in order.");
    quickButtons[1]->onClick = [this]
    {
        for (int i = 0; i < numInputs; ++i)
            entries[size_t (i)].selected = ! entries[size_t (i)].assigned && ! entries[size_t (i)].linkedFromPrevious;
        busFilter = -2;
        rebuild();
    };
    quickButtons[2]->setTooltip ("Neighbours whose names end in L and R become one stereo row.");
    quickButtons[2]->onClick = [this]
    {
        for (int i = 0; i + 1 < numInputs; ++i)
        {
            auto& a = entries[size_t (i)];
            const auto& b = entries[size_t (i) + 1];
            if (a.linkedToNext || a.linkedFromPrevious || a.name.isEmpty() || b.name.isEmpty()) continue;
            const auto an = a.name.trim().toUpperCase(), bn = b.name.trim().toUpperCase();
            if (an.endsWith (" L") && bn.endsWith (" R") && an.dropLastCharacters (2) == bn.dropLastCharacters (2))
            {
                a.linkedToNext = true;
                entries[size_t (i) + 1].linkedFromPrevious = true;
            }
        }
        commit();
        rebuild();
    };
}

AssignPage::~AssignPage() = default;

void AssignPage::refresh()
{
    // Every channel the device brings in, and never fewer than the session already uses: an
    // imported multitrack, or a session opened without its console, still shows all its inputs.
    numInputs = juce::jlimit (0, kMaxInputs, services.numInputChannels());
    for (const auto& in : controller.getSession().inputs)
        numInputs = juce::jlimit (0, kMaxInputs, juce::jmax (numInputs, in.inputA + 1, in.inputB + 1));
    entries.assign (size_t (numInputs), Entry {});
    for (const auto& in : controller.getSession().inputs)
    {
        if (in.inputA < 0 || in.inputA >= numInputs) continue;
        auto& e = entries[size_t (in.inputA)];
        e.name = in.name;
        e.icon = in.icon;
        e.assigned = in.enabled;
        e.role = in.role;
        if (in.inputB >= 0 && in.inputB < numInputs) { e.linkedToNext = in.inputB == in.inputA + 1; entries[size_t (in.inputB)].linkedFromPrevious = e.linkedToNext; }
    }
    rebuild();
}

bool AssignPage::visible (int input) const
{
    const auto& e = entries[size_t (input)];
    if (e.linkedFromPrevious) return false;
    if (busFilter == -1 && e.assigned) return false;
    if (busFilter >= 0 && (! e.assigned || int (mixBusForRole (e.role)) != busFilter)) return false;
    const auto q = query.trim().toLowerCase();
    if (q.isEmpty()) return true;
    if (e.name.toLowerCase().contains (q)) return true;
    if (juce::String (input + 1) == q) return true;
    return (e.assigned ? friendlyRoleName (e.role) : juce::String ("Not used")).toLowerCase().contains (q);
}

std::vector<AssignPage::Group> AssignPage::buildGroups() const
{
    std::vector<Group> out;
    if (! grouped)
    {
        Group all { "All inputs", Dine::accent, -1, {} };
        for (int i = 0; i < numInputs; ++i) if (visible (i)) all.inputs.push_back (i);
        if (! all.inputs.empty()) out.push_back (std::move (all));
        return out;
    }
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        Group g { busLabel (MixBus (b)), busColour (MixBus (b)), b, {} };
        for (int i = 0; i < numInputs; ++i)
            if (visible (i) && entries[size_t (i)].assigned && int (mixBusForRole (entries[size_t (i)].role)) == b)
                g.inputs.push_back (i);
        if (! g.inputs.empty()) out.push_back (std::move (g));
    }
    Group unused { "Not used", juce::Colours::white.withAlpha (0.22f), -1, {} };
    for (int i = 0; i < numInputs; ++i)
        if (visible (i) && ! entries[size_t (i)].assigned) unused.inputs.push_back (i);
    if (! unused.inputs.empty()) out.push_back (std::move (unused));
    return out;
}

void AssignPage::rebuild()
{
    groups = buildGroups();
    rows.clear();
    headers.clear();
    listHolder.removeAllChildren();
    for (size_t gi = 0; gi < groups.size(); ++gi)
    {
        const auto& g = groups[gi];
        auto header = std::make_unique<GroupHeader> (g.name, g.colour, int (g.inputs.size()), g.bus < 0 && g.name == "Not used");
        header->onClick = [this, gi] { if (gi < groups.size()) selectGroup (groups[gi]); };
        listHolder.addAndMakeVisible (*header);
        headers.push_back (std::move (header));
        for (int i : g.inputs)
        {
            auto row = std::make_unique<Row> (*this, i);
            listHolder.addAndMakeVisible (*row);
            row->refresh();
            rows.push_back (std::move (row));
        }
    }
    for (size_t i = 0; i < chips.size(); ++i)
    {
        const int key = i == 0 ? -2 : i == chips.size() - 1 ? -1 : int (i) - 1;
        chips[i]->setToggleState (busFilter == key, juce::dontSendNotification);
    }
    groupButton.setToggleState (grouped, juce::dontSendNotification);
    continueButton.setEnabled (assignedCount() > 0);
    updateToolbar();
    resized();
    repaint();
}

void AssignPage::updateToolbar()
{
    const bool any = selectionCount() > 0;
    for (auto* b : { &bulkButton, &kitButton, &nameButton, &linkButton, &dropButton, &deselectButton })
        b->setVisible (any);
    for (auto* b : { &selectAllButton, &deskLabelsButton })
        b->setVisible (! any);
    search.setVisible (! any);
    for (auto& c : chips) c->setVisible (! any);
    groupButton.setVisible (! any);
}

void AssignPage::commit()
{
    MixSession s = controller.getSession();
    s.inputs.clear();
    for (int i = 0; i < numInputs; ++i)
    {
        const auto& e = entries[size_t (i)];
        if (! e.assigned || e.linkedFromPrevious) continue;
        InputAssignment a;
        a.name = e.name.isEmpty() ? juce::String (shortRoleName (e.role)).toStdString() : e.name.toStdString();
        a.icon = e.icon;
        a.role = e.role;
        a.inputA = i;
        a.inputB = e.linkedToNext && i + 1 < numInputs ? i + 1 : -1;
        a.enabled = true;
        s.inputs.push_back (a);
    }
    controller.setSession (s);
    continueButton.setEnabled (assignedCount() > 0);
    repaint();
}

int AssignPage::assignedCount() const
{
    int n = 0;
    for (const auto& e : entries) if (e.assigned && ! e.linkedFromPrevious) ++n;
    return n;
}

int AssignPage::unusedCount() const
{
    int n = 0;
    for (const auto& e : entries) if (! e.assigned && ! e.linkedFromPrevious) ++n;
    return n;
}

int AssignPage::selectionCount() const
{
    int n = 0;
    for (const auto& e : entries) if (e.selected) ++n;
    return n;
}

std::vector<int> AssignPage::selectedInputs() const
{
    std::vector<int> out;
    for (int i = 0; i < numInputs; ++i) if (entries[size_t (i)].selected) out.push_back (i);
    return out;
}

void AssignPage::toggleSelection (int input, bool extend)
{
    if (input < 0 || input >= numInputs) return;
    if (extend && lastClicked >= 0)
    {
        const int a = juce::jmin (lastClicked, input), b = juce::jmax (lastClicked, input);
        for (int i = a; i <= b; ++i) if (! entries[size_t (i)].linkedFromPrevious) entries[size_t (i)].selected = true;
    }
    else
    {
        entries[size_t (input)].selected = ! entries[size_t (input)].selected;
    }
    lastClicked = input;
    updateToolbar();
    resized();
    repaint();
}

void AssignPage::selectGroup (const Group& g)
{
    const bool all = std::all_of (g.inputs.begin(), g.inputs.end(), [this] (int i) { return entries[size_t (i)].selected; });
    for (int i : g.inputs) entries[size_t (i)].selected = ! all;
    updateToolbar();
    resized();
    repaint();
}

void AssignPage::clearSelection()
{
    for (auto& e : entries) e.selected = false;
    updateToolbar();
    resized();
    repaint();
}

void AssignPage::setRole (int input, ChannelRole role, bool assigned)
{
    auto& e = entries[size_t (input)];
    e.assigned = assigned;
    if (! assigned) { e.role = ChannelRole::KickIn; e.linkedToNext = false; if (input + 1 < numInputs) entries[size_t (input) + 1].linkedFromPrevious = false; return; }
    e.role = role;
    if (e.name.trim().isEmpty()) e.name = shortRoleName (role);
}

void AssignPage::nameFromRole()
{
    for (int i : selectedInputs())
        if (entries[size_t (i)].assigned) entries[size_t (i)].name = shortRoleName (entries[size_t (i)].role);
    commit();
    rebuild();
}

void AssignPage::linkSelection()
{
    const auto sel = selectedInputs();
    for (int i : sel)
    {
        if (i + 1 >= numInputs) continue;
        if (std::find (sel.begin(), sel.end(), i + 1) == sel.end()) continue;
        if (entries[size_t (i)].linkedFromPrevious) continue;
        entries[size_t (i)].linkedToNext = true;
        entries[size_t (i) + 1].linkedFromPrevious = true;
    }
    clearSelection();
    commit();
    rebuild();
}

void AssignPage::dropSelection()
{
    for (int i : selectedInputs()) setRole (i, ChannelRole::KickIn, false);
    clearSelection();
    commit();
    rebuild();
}

void AssignPage::assign (int input, ChannelRole role, const juce::String& name, bool linkWithNext)
{
    if (input < 0 || input >= numInputs) return;
    auto& e = entries[size_t (input)];
    e.assigned = true;
    e.role = role;
    e.name = name;
    e.linkedToNext = linkWithNext && input + 1 < numInputs;
    if (e.linkedToNext) entries[size_t (input) + 1].linkedFromPrevious = true;
    commit();
    rebuild();
}

void AssignPage::selectInputs (const std::vector<int>& which)
{
    for (auto& e : entries) e.selected = false;
    for (int i : which) if (i >= 0 && i < numInputs) entries[size_t (i)].selected = true;
    if (! which.empty()) lastClicked = which.back();
    rebuild();
}

void AssignPage::clearAll()
{
    for (auto& e : entries) e = Entry {};
    commit();
    rebuild();
}

void AssignPage::showNameMenu (int input, juce::Component& anchor)
{
    const auto& e = entries[size_t (input)];
    juce::PopupMenu m;
    juce::StringArray options;
    if (e.assigned)
    {
        options.add (shortRoleName (e.role));
        if (friendlyRoleName (e.role) != options[0]) options.add (friendlyRoleName (e.role));
    }
    options.add ("Desk " + juce::String (input + 1).paddedLeft ('0', 2));
    for (int i = 0; i < options.size(); ++i) m.addItem (i + 1, options[i]);
    m.addSeparator();
    m.addItem (90, "Clear the name");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor).withMinimumWidth (200),
                     [this, input, options] (int chosen)
                     {
                         if (chosen <= 0) return;
                         entries[size_t (input)].name = chosen == 90 ? juce::String() : options[chosen - 1];
                         commit();
                         rebuild();
                     });
}

void AssignPage::showSourceMenu (int input, juce::Component& anchor)
{
    juce::PopupMenu m;
    m.addItem (1, "Not used", true, ! entries[size_t (input)].assigned);
    m.addSeparator();

    int id = 100;
    std::vector<ChannelRole> byId;
    // Hierarchical menus read as native macOS - not one endless flat list.
    for (const auto& group : roleGroups())
    {
        juce::PopupMenu sub;
        for (auto r : group.roles)
        {
            const bool on = entries[size_t (input)].assigned && entries[size_t (input)].role == r;
            sub.addItem (id++, friendlyRoleName (r), true, on);
            byId.push_back (r);
        }
        m.addSubMenu (group.name, sub, true);
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor).withMinimumWidth (220),
                     [this, input, byId] (int chosen)
                     {
                         if (chosen <= 0 || input >= int (entries.size())) return;
                         if (chosen == 1) { setRole (input, ChannelRole::KickIn, false); }
                         else
                         {
                             const auto role = byId[size_t (chosen - 100)];
                             setRole (input, role, true);
                             auto& e = entries[size_t (input)];
                             if ((role == ChannelRole::Overhead || role == ChannelRole::DrumBus || role == ChannelRole::Piano
                                  || role == ChannelRole::ElectricPiano || role == ChannelRole::SynthPad)
                                 && input + 1 < numInputs && ! entries[size_t (input) + 1].assigned && ! e.linkedToNext)
                             {
                                 e.linkedToNext = true;
                                 entries[size_t (input) + 1].linkedFromPrevious = true;
                             }
                         }
                         commit();
                         rebuild();
                     });
}

void AssignPage::showBulkMenu (juce::Component& anchor)
{
    juce::PopupMenu m;
    m.addItem (1, "Not used");
    m.addSeparator();
    int id = 100;
    std::vector<ChannelRole> byId;
    for (const auto& group : roleGroups())
    {
        juce::PopupMenu sub;
        for (auto r : group.roles) { sub.addItem (id++, friendlyRoleName (r)); byId.push_back (r); }
        m.addSubMenu (group.name, sub, true);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor).withMinimumWidth (220),
                     [this, byId] (int chosen)
                     {
                         if (chosen <= 0) return;
                         for (int i : selectedInputs())
                             setRole (i, chosen == 1 ? ChannelRole::KickIn : byId[size_t (chosen - 100)], chosen != 1);
                         commit();
                         rebuild();
                     });
}

void AssignPage::showKitMenu (juce::Component& anchor)
{
    const auto sel = selectedInputs();
    juce::PopupMenu m;
    m.addSectionHeader ("Fill " + juce::String (sel.size()) + (sel.size() == 1 ? " input" : " inputs") + ", in order");
    for (int i = 0; i < int (kits().size()); ++i)
    {
        juce::String preview;
        for (auto r : kits()[size_t (i)].roles) preview += (preview.isEmpty() ? "" : ", ") + shortRoleName (r);
        m.addItem (i + 1, juce::String (kits()[size_t (i)].label) + "   " + Glyph::dash() + "  " + preview);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor).withMinimumWidth (300),
                     [this, sel] (int chosen)
                     {
                         if (chosen <= 0 || chosen > int (kits().size())) return;
                         const auto& kit = kits()[size_t (chosen - 1)];
                         for (size_t n = 0; n < sel.size(); ++n)
                             setRole (sel[n], kit.roles[juce::jmin (n, kit.roles.size() - 1)], true);
                         // A kit that ends in an L and an R leaves them linked, the way they are patched.
                         for (size_t n = 0; n + 1 < sel.size(); ++n)
                         {
                             const int a = sel[n], b = sel[n + 1];
                             if (b != a + 1) continue;
                             const auto ra = entries[size_t (a)].role, rb = entries[size_t (b)].role;
                             if ((ra == ChannelRole::OverheadLeft && rb == ChannelRole::OverheadRight)
                                 || (ra == ChannelRole::Piano && rb == ChannelRole::Piano))
                             {
                                 entries[size_t (a)].linkedToNext = true;
                                 entries[size_t (b)].linkedFromPrevious = true;
                             }
                         }
                         clearSelection();
                         commit();
                         rebuild();
                     });
}

SetupLayout AssignPage::layout() const { return SetupLayout::of (getLocalBounds(), true, false); }

void AssignPage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto L = layout();
    const int assigned = assignedCount(), unused = unusedCount();
    const int total = assigned + unused;

    auto head = L.head;
    auto progress = head.removeFromRight (juce::jmin (250, juce::jmax (0, head.getWidth() - 430)));
    drawSetupHead (g, head, "Inputs", "Name each input and say what it is. DLIVE sends it to the right group.");
    if (progress.getWidth() > 150)
    {
        auto line = progress.removeFromTop (17);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f));
        g.drawText (juce::String (assigned) + " of " + juce::String (total) + " assigned", line, juce::Justification::centredRight, true);
        progress.removeFromTop (6);
        std::vector<Dine::BarSlice> slices;
        for (int b = 0; b < int (MixBus::Master); ++b)
        {
            int n = 0;
            for (int i = 0; i < numInputs; ++i)
                if (entries[size_t (i)].assigned && ! entries[size_t (i)].linkedFromPrevious
                    && int (mixBusForRole (entries[size_t (i)].role)) == b) ++n;
            slices.push_back ({ float (n) / float (juce::jmax (1, total)), busColour (MixBus (b)) });
        }
        Dine::drawStackedBar (g, progress.removeFromTop (6), slices);
        progress.removeFromTop (5);
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (11.5f));
        g.drawText (unused > 0 ? juce::String (unused) + " still open" : "Every input placed",
                    progress.removeFromTop (14), juce::Justification::centredRight, true);
    }

    // ---- toolbar
    Dine::drawHeaderBand (g, L.toolbar.expanded (Dine::Metric::padX, 0));
    if (selectionCount() == 0)
    {
        auto track = juce::Rectangle<int>();
        for (auto& c : chips) track = track.isEmpty() ? c->getBounds() : track.getUnion (c->getBounds());
        if (! track.isEmpty()) Dine::drawSegmentTrack (g, track.expanded (2, 2));
    }
    else
    {
        auto label = L.toolbar.withHeight (Dine::Metric::control).withWidth (bulkButton.getX() - L.toolbar.getX() - 9)
                        .withSizeKeepingCentre (bulkButton.getX() - L.toolbar.getX() - 9, L.toolbar.getHeight());
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f));
        g.drawText (juce::String (selectionCount()) + (selectionCount() == 1 ? " input selected" : " inputs selected"),
                    label, juce::Justification::centredLeft, true);
    }

    if (rows.empty())
    {
        auto empty = L.main.reduced (30, 40).removeFromTop (72);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (14.0f, 600));
        g.drawText (numInputs == 0 ? "No inputs to name yet" : "Nothing matches that",
                    empty.removeFromTop (18), juce::Justification::centredLeft);
        empty.removeFromTop (4);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText (numInputs == 0 ? "Go back and pick a device with inputs, or import a folder of stems."
                                         : "Clear the search, or switch the filter back to All.",
                          empty.removeFromTop (34), juce::Justification::topLeft, 2);
    }

    int live = 0;
    for (int i = 0; i < numInputs; ++i)
        if (! entries[size_t (i)].assigned && ! entries[size_t (i)].linkedFromPrevious
            && services.isAudioRunning() && services.daw().inputPeakDb (i) > -54.0f) ++live;
    drawSetupFooter (g, getLocalBounds(),
                     assigned > 0 ? juce::String (unused) + " unassigned inputs stay out of the mix"
                                        + (live > 0 ? " - " + juce::String (live) + " of them " + (live == 1 ? "is" : "are") + " carrying signal right now." : ".")
                                  : juce::String ("Name at least one input to carry on."),
                     continueButton.getWidth() + backButton.getWidth() + 10);
}

void AssignPage::resized()
{
    const auto L = layout();

    // ---- toolbar
    auto bar = L.toolbar.withSizeKeepingCentre (L.toolbar.getWidth(), Dine::Metric::control);
    if (selectionCount() == 0)
    {
        auto right = bar;
        const int aw = juce::jmax (120, patchApplyButton.idealWidth());
        patchApplyButton.setBounds (right.removeFromRight (aw));
        right.removeFromRight (8);
        const int sw = juce::jmax (110, patchSaveButton.idealWidth());
        patchSaveButton.setBounds (right.removeFromRight (sw));
        right.removeFromRight (8);
        const int qw = juce::jmax (100, quickButton.idealWidth());
        quickButton.setBounds (right.removeFromRight (qw));
        right.removeFromRight (8);
        const int gw = 30;
        groupButton.setBounds (right.removeFromRight (gw));
        right.removeFromRight (8);
        const int dw = juce::jmax (120, deskLabelsButton.idealWidth());
        deskLabelsButton.setBounds (right.removeFromRight (dw));
        right.removeFromRight (8);
        const int saw = juce::jmax (86, selectAllButton.idealWidth());
        selectAllButton.setBounds (right.removeFromRight (saw));

        int x = bar.getX() + 2;
        for (auto& c : chips)
        {
            const int w = juce::jmax (48, c->idealWidth());
            c->setBounds (x, bar.getY() + 2, w, bar.getHeight() - 4);
            x += w;
        }
        search.setBounds (juce::Rectangle<int> (x + 12, bar.getY(), juce::jmax (0, juce::jmin (184, selectAllButton.getX() - 10 - (x + 12))), bar.getHeight()));
    }
    else
    {
        bar.removeFromLeft (juce::jmin (150, bar.getWidth() / 4));
        for (auto* b : { &bulkButton, &kitButton, &nameButton, &linkButton, &dropButton })
        {
            const int w = juce::jmax (86, b->idealWidth());
            b->setBounds (bar.removeFromLeft (w));
            bar.removeFromLeft (8);
        }
        const int dw = juce::jmax (78, deselectButton.idealWidth());
        deselectButton.setBounds (bar.removeFromRight (dw));
    }
    for (auto* b : { &patchSaveButton, &patchApplyButton, &quickButton })
        b->setVisible (selectionCount() == 0);

    // ---- the list, and where the group headers land inside it
    auto list = L.main.withTrimmedTop (8);
    viewport.setBounds (list);
    const int rowH = 44, groupH = 28;
    int total = 0;
    for (const auto& g : groups) total += groupH + int (g.inputs.size()) * rowH + 6;
    listHolder.setSize (list.getWidth() - (total > list.getHeight() ? 10 : 0), juce::jmax (total + 8, list.getHeight()));
    int y = 0;
    size_t r = 0;
    for (size_t gi = 0; gi < groups.size(); ++gi)
    {
        if (gi < headers.size()) headers[gi]->setBounds (0, y, listHolder.getWidth(), groupH);
        y += groupH;
        for (size_t n = 0; n < groups[gi].inputs.size() && r < rows.size(); ++n, ++r)
        {
            rows[r]->setBounds (0, y, listHolder.getWidth(), rowH);
            y += rowH;
        }
        y += 6;
    }
    showUnusedButton.setVisible (false);

    auto footer = L.footer.reduced (Dine::Metric::padX, 0);
    const int cw = juce::jmax (110, continueButton.idealWidth());
    continueButton.setBounds (footer.removeFromRight (cw).withSizeKeepingCentre (cw, Dine::Metric::button));
    footer.removeFromRight (10);
    const int bw = juce::jmax (72, backButton.idealWidth());
    backButton.setBounds (footer.removeFromRight (bw).withSizeKeepingCentre (bw, Dine::Metric::button));
    const int clw = juce::jmax (80, clearButton.idealWidth());
    clearButton.setBounds (footer.removeFromLeft (clw).withSizeKeepingCentre (clw, Dine::Metric::button));
}

// ============================================================================ PurposePage

class PurposePage::Tile : public juce::Button
{
public:
    Tile (const juce::String& title, const juce::String& line) : juce::Button (title), heading (title), detail (line) {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto b = getLocalBounds().toFloat();
        Dine::fillRounded (g, b, on ? Dine::selected : over ? Dine::raised : Dine::card, Dine::Radius::card);

        auto r = getLocalBounds().reduced (18, 18);
        auto top = r.removeFromTop (18);
        Dine::drawRadio (g, top.removeFromLeft (14).toFloat(), on);
        top.removeFromLeft (10);
        if (tag.isNotEmpty())
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (11.0f));
            const int w = Dine::textWidth (Dine::text (11.0f), tag);
            g.drawText (tag, top.removeFromRight (w), juce::Justification::centredRight);
        }
        g.setColour (Dine::ink);
        g.setFont (Dine::text (15.0f));
        g.drawText (heading, top, juce::Justification::centredLeft, true);

        r.removeFromTop (10);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText (detail, r.removeFromTop (specs.empty() ? r.getHeight() : 40), juce::Justification::topLeft, 3, 1.0f);

        if (specs.empty()) return;
        r.removeFromTop (10);
        juce::String line;
        for (const auto& s : specs) line += (line.isEmpty() ? "" : "  " + juce::String (Glyph::dot()) + "  ") + s.second;
        g.setColour (Dine::accent);
        g.setFont (Dine::mono (11.0f));
        g.drawText (line, r.removeFromTop (14), juce::Justification::centredLeft, true);
    }

    juce::String heading, detail, tag;
    std::vector<std::pair<juce::String, juce::String>> specs;
};

namespace
{
    // Two rows of purpose cards, each tall enough for its name, its sentence and the three
    // numbers it promises.
    constexpr int kPurposeGrid = 150;

    // Every number on this page is the profile's own: the loudness a delivery wants, the
    // ceiling its true peaks must stay under, and how much the loudness may wander. They
    // live in ProfileData, so what the card promises is what TUNE MIX actually aims at.
    SourceTargets masterTargets (StyleProfileId profile, MixPurpose purpose)
    {
        return Profiles::targets (profile, masterRoleFor (purpose));
    }

    juce::String lufs (float v)   { return juce::String (v, 0) + " LUFS"; }
    juce::String dbtp (float v)   { return juce::String (v, 1) + " dBTP"; }
    juce::String window (float lu) { return lu <= 1.0f ? "Tight" : lu <= 2.0f ? "Open" : "Wide"; }
}

PurposePage::PurposePage (MixController& c) : controller (c)
{
    const char* purposeLines[] = {
        "For television and broadcast. Steady loudness matters more than dynamics, and the peaks stay well down.",
        "YouTube, Facebook, the church platform. A little louder, with the headroom the encoder needs.",
        "Captured to keep. Headroom is left for whoever edits it later.",
        "Rehearsal, a listening feed, the room itself. The band keeps its dynamics."
    };
    for (int i = 0; i < int (MixPurpose::Count); ++i)
    {
        auto t = std::make_unique<Tile> (mixPurposeName (MixPurpose (i)), purposeLines[i]);
        t->setClickingTogglesState (false);
        t->onClick = [this, i] { controller.setPurpose (MixPurpose (i)); refresh(); };
        addAndMakeVisible (*t);
        purposeTiles.push_back (std::move (t));
    }
    const char* soundLines[] = {
        "Full low end, a forward vocal and drums that push. Organ and keys sit wide behind them.",
        "Guitars and pads carry it. The vocal is warm rather than bright and the kick stays tight."
    };
    for (int i = 0; i < int (StyleProfileId::Count); ++i)
    {
        auto t = std::make_unique<Tile> (styleProfileName (StyleProfileId (i)), soundLines[i]);
        t->tag = i == 0 ? "Default" : "A documented delta";
        t->setClickingTogglesState (false);
        t->onClick = [this, i] { controller.setProfile (StyleProfileId (i)); refresh(); };
        addAndMakeVisible (*t);
        soundTiles.push_back (std::move (t));
    }
    addAndMakeVisible (deliveryButton);
    deliveryButton.setTooltip ("How loud the finished mix should end up. This is what the whole gain structure is "
                               "fitted against - not a gain added at the end - so changing it changes nothing until "
                               "the next TUNE MIX, and then every fader, group and the master follow it.");
    deliveryButton.onClick = [this]
    {
        juce::PopupMenu m;
        const auto current = controller.getDelivery();
        const auto purposeTarget = masterTargets (controller.getSession().profile, controller.getSession().purpose);
        m.addItem (1, juce::String (deliveryLoudnessName (DeliveryLoudness::FromPurpose)) + "   ("
                       + lufs (purposeTarget.targetLufs) + ")", true, current == DeliveryLoudness::FromPurpose);
        m.addSeparator();
        for (int i = 1; i < int (DeliveryLoudness::Count); ++i)
        {
            const auto d = DeliveryLoudness (i);
            m.addItem (i + 1, juce::String (deliveryLoudnessName (d)) + "   " + lufs (deliveryLoudnessLufs (d)),
                       true, current == d);
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (deliveryButton).withMinimumWidth (330),
                         [this] (int id)
                         {
                             if (id <= 0) return;
                             controller.setDelivery (DeliveryLoudness (id - 1));
                             refresh();
                         });
    };

    addAndMakeVisible (continueButton);
    addAndMakeVisible (backButton);
    continueButton.setCaps (false);
    continueButton.onClick = [this] { if (onContinue) onContinue(); };
    backButton.onClick = [this] { if (onBack) onBack(); };
    refresh();
}

PurposePage::~PurposePage() = default;

void PurposePage::refresh()
{
    const auto& session = controller.getSession();
    for (int i = 0; i < int (purposeTiles.size()); ++i)
    {
        const auto t = masterTargets (session.profile, MixPurpose (i));
        purposeTiles[size_t (i)]->specs = { { "Target", lufs (t.targetLufs) },
                                            { "True peak", dbtp (t.truePeakCeilingDb) },
                                            { "Range", window (t.loudnessToleranceLu) } };
        purposeTiles[size_t (i)]->setToggleState (int (session.purpose) == i, juce::dontSendNotification);
    }
    for (int i = 0; i < int (soundTiles.size()); ++i)
        soundTiles[size_t (i)]->setToggleState (int (session.profile) == i, juce::dontSendNotification);
    const auto purposeTarget = masterTargets (session.profile, session.purpose);
    deliveryButton.setValue (session.delivery == DeliveryLoudness::FromPurpose
                                 ? juce::String (deliveryLoudnessName (session.delivery)) + "  (" + lufs (purposeTarget.targetLufs) + ")"
                                 : juce::String (deliveryLoudnessName (session.delivery)) + "  " + lufs (session.deliveryTargetLufs()));
    repaint();
}

SetupLayout PurposePage::layout() const { return SetupLayout::of (getLocalBounds(), false, false); }

void PurposePage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto L = layout();
    const auto& session = controller.getSession();
    auto target = masterTargets (session.profile, session.purpose);
    if (const float wanted = session.deliveryTargetLufs(); wanted < 0.0f && target.loudnessTargetAppropriate)
    {
        target.targetLufs = wanted;
        target.truePeakCeilingDb = juce::jmin (target.truePeakCeilingDb, wanted >= -15.0f ? -1.0f : -1.5f);
    }

    drawSetupHead (g, L.head, "Purpose and sound",
                   "Each card is a commitment: the numbers on it are what DLIVE will mix to.");

    auto main = L.main;
    Dine::drawSection (g, main.removeFromTop (14), "PURPOSE  " + juce::String (Glyph::dot()) + "  WHERE IT IS GOING");
    main.removeFromTop (10 + kPurposeGrid + 22);
    Dine::drawSection (g, main.removeFromTop (14), "SOUND  " + juce::String (Glyph::dot()) + "  WHAT IT SHOULD FEEL LIKE");
    main.removeFromTop (10 + 110 + 22);
    Dine::drawSection (g, main.removeFromTop (14), "HOW LOUD IT SHOULD END UP");
    main.removeFromTop (10);
    auto row = main.removeFromTop (Dine::Metric::button);
    row.removeFromLeft (deliveryButton.getWidth() + 14);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.5f));
    g.drawText (deliveryLoudnessHint (session.delivery), row, juce::Justification::centredLeft, true);
    main.removeFromTop (18);
    g.setColour (Dine::ink2);
    g.setFont (Dine::text (13.0f));
    g.drawFittedText ("DLIVE will land the mix at " + lufs (target.targetLufs) + ", never letting it peak past "
                          + dbtp (target.truePeakCeilingDb) + ", and tune every group toward "
                          + juce::String (styleProfileName (session.profile)) + ". Purpose and sound can be switched mid-service: "
                          "the next tune follows the new one, and anything you moved by hand is kept.",
                      main.removeFromTop (44).withWidth (juce::jmin (main.getWidth(), 720)), juce::Justification::topLeft, 3, 1.0f);

    drawSetupFooter (g, getLocalBounds(), "DLIVE listens for about thirty seconds, then sets the whole mix.",
                     continueButton.getWidth() + backButton.getWidth() + 10);
}

void PurposePage::resized()
{
    const auto L = layout();
    auto main = L.main;
    main.removeFromTop (14 + 10);

    const int gap = 12;
    auto purposeArea = main.removeFromTop (kPurposeGrid);
    const int cols = juce::jlimit (1, int (purposeTiles.size()), (purposeArea.getWidth() + gap) / (300 + gap));
    const int rowsN = (int (purposeTiles.size()) + cols - 1) / cols;
    const int tileH = juce::jmin (kPurposeGrid, (kPurposeGrid - gap * (rowsN - 1)) / juce::jmax (1, rowsN));
    const int tileW = juce::jmin (300, (purposeArea.getWidth() - gap * (cols - 1)) / cols);
    for (int i = 0; i < int (purposeTiles.size()); ++i)
    {
        const int c = i % cols, r = i / cols;
        purposeTiles[size_t (i)]->setBounds (purposeArea.getX() + c * (tileW + gap),
                                             purposeArea.getY() + r * (tileH + gap), tileW, tileH);
    }
    main.removeFromTop (22 + 14 + 10);
    auto soundArea = main.removeFromTop (110);
    for (auto& t : soundTiles) { t->setBounds (soundArea.removeFromLeft (300)); soundArea.removeFromLeft (gap); }

    main.removeFromTop (22 + 14 + 10);
    auto loudRow = main.removeFromTop (Dine::Metric::button);
    deliveryButton.setBounds (loudRow.removeFromLeft (juce::jmin (300, juce::jmax (220, deliveryButton.idealWidth()))));

    auto footer = L.footer.reduced (Dine::Metric::padX, 0);
    const int cw = juce::jmax (130, continueButton.idealWidth());
    continueButton.setBounds (footer.removeFromRight (cw).withSizeKeepingCentre (cw, Dine::Metric::button));
    footer.removeFromRight (10);
    const int bw = juce::jmax (72, backButton.idealWidth());
    backButton.setBounds (footer.removeFromRight (bw).withSizeKeepingCentre (bw, Dine::Metric::button));
}

} // namespace livemix
