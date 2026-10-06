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
            case ChannelRole::SpeechLapel:          return "Lapel";
            case ChannelRole::SpeechHeadset:        return "Headset";
            case ChannelRole::SpeechHandheld:       return "Handheld";
            case ChannelRole::SpeechLectern:        return "Lectern";
            case ChannelRole::CrowdMic:             return "Crowd";
            case ChannelRole::AmbienceMic:          return "Ambience";
            case ChannelRole::SaxAlto:              return "Alto sax";
            case ChannelRole::SaxTenor:             return "Tenor sax";
            case ChannelRole::SaxBari:              return "Bari sax";
            case ChannelRole::BrassSection:         return "Horns";
            case ChannelRole::Timbales:             return "Timbs";
            case ChannelRole::DrumPad:              return "Pad";
            default:                                return channelRoleName (r);
        }
    }

    // What a gain-staging verdict looks like. The same three colours the console and the
    // Inspector use for the same words, so an input that is hot is hot everywhere.
    juce::Colour gainVerdictColour (MixController::InputAdvice::Level level) noexcept
    {
        using Level = MixController::InputAdvice::Level;
        switch (level)
        {
            case Level::Clipping:
            case Level::Faint:    return Dine::crit;
            case Level::Low:
            case Level::Hot:
            case Level::Digital:  return Dine::warn;
            case Level::NotHeard: return Dine::ink4;
            default:              return Dine::ok;
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
            { "Speaking mics", MixBus::Speech, { ChannelRole::SpeechLapel, ChannelRole::SpeechHandheld,
                                                 ChannelRole::SpeechLectern } },
            // The building. Two of these across the room is what makes a stream sound like a
            // service rather than a studio recording of a band.
            { "Crowd and room", MixBus::Ambience, { ChannelRole::CrowdMic, ChannelRole::CrowdMic, ChannelRole::AmbienceMic } },
            { "Horns", MixBus::Music, { ChannelRole::SaxAlto, ChannelRole::SaxTenor, ChannelRole::SaxBari,
                                        ChannelRole::Trumpet, ChannelRole::Trombone } },
            // Percussion sits with the kit, so it is laid down beside it and balanced with it.
            { "Percussion", MixBus::Drums, { ChannelRole::Congas, ChannelRole::Bongos, ChannelRole::Shaker } }
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

    // The v3 rhythm every set-up page shares: 34 above the title, the title 28, 2, the one
    // sentence 18, then 31 to the tool row and 21 from it to whatever the page is about.
    constexpr int kTopPad = 34, kHead = 48, kToolbar = 30, kHeadGap = 31, kToolGap = 21, kGutter = 24;
    constexpr int kFooterH = Dine::Metric::button + Dine::Metric::padY;
}

// ============================================================================ the shared shape
SetupLayout SetupLayout::of (juce::Rectangle<int> page, bool withToolbar, bool withRail)
{
    SetupLayout L;
    L.footer = page.removeFromBottom (kFooterH);
    auto r = page.reduced (Dine::Metric::padX, 0);
    r.removeFromTop (kTopPad);
    L.head = r.removeFromTop (kHead);
    r.removeFromTop (kHeadGap);
    L.toolbar = withToolbar ? r.removeFromTop (kToolbar) : juce::Rectangle<int>();
    r.removeFromTop (withToolbar ? kToolGap : 0);
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
    g.setFont (Dine::text (22.0f, 600));
    Dine::drawText (g, title, r.removeFromTop (28), juce::Justification::centredLeft, true);
    r.removeFromTop (2);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (13.0f));
    Dine::drawFittedText (g, sentence, r.removeFromTop (18).withWidth (juce::jmin (r.getWidth(), 760)), juce::Justification::topLeft, 1);
}

void drawSetupFooter (juce::Graphics& g, juce::Rectangle<int> page, const juce::String& note, int reservedRight)
{
    auto footer = page.removeFromBottom (kFooterH).removeFromTop (Dine::Metric::button);
    if (note.isEmpty()) return;
    auto r = footer.reduced (Dine::Metric::padX, 0);
    r.removeFromRight (reservedRight + 12);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.0f));
    Dine::drawText (g, note, r, juce::Justification::centredRight, true);
}

// ============================================================================ SessionsPage

// A session, as the design draws it (`01 - Sessions`, 70:9051): a lamp, the name, then one
// line saying what it is and what it was for, with when it was last saved at the right. The
// stacked bar of how its inputs fall across the groups sits before that date - the design left
// it out, and it is the one thing on this screen that says at a glance what kind of service a
// session was, so it stays.
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

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) { juce::Button::mouseDown (e); return; }
        page.select (index);
        const auto file = page.items[size_t (index)].listing.file;
        juce::PopupMenu m;
        m.addItem (1, "Open");
        m.addItem (2, "Show in Finder");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this, file] (int r)
                         {
                             if (r == 1 && page.onOpen) page.onOpen (file);
                             else if (r == 2) file.revealToUser();
                         });
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& it = page.items[size_t (index)];
        const bool on = getToggleState();
        auto b = getLocalBounds().withTrimmedBottom (4);
        if (on)        Dine::fillRounded (g, b.toFloat(), Dine::selected, Dine::Radius::control);
        else if (over) Dine::fillRounded (g, b.toFloat(), Dine::item, Dine::Radius::control);

        auto r = b.reduced (12, 0);
        auto when = r.removeFromRight (kWhenW);
        r.removeFromRight (16);
        auto bars = r.getWidth() > 620 ? r.removeFromRight (200) : juce::Rectangle<int>();
        if (! bars.isEmpty()) r.removeFromRight (20);

        // the lamp: green while this is the session on screen, quiet otherwise
        auto lamp = r.removeFromLeft (8).withSizeKeepingCentre (7, 7);
        g.setColour (it.open ? Dine::accent : it.summary.valid ? Dine::ok : Dine::warn);
        g.fillEllipse (lamp.toFloat());
        r.removeFromLeft (12);

        auto text = r.withSizeKeepingCentre (r.getWidth(), 38);
        auto name = text.removeFromTop (18);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (14.0f, 600));
        Dine::drawText (g, it.listing.name, name, juce::Justification::centredLeft, true);
        text.removeFromTop (2);

        // what it is, in one line: inputs, takes, purpose, sound
        juce::StringArray parts;
        if (it.summary.valid)
        {
            parts.add (juce::String (it.summary.inputs) + (it.summary.inputs == 1 ? " input" : " inputs"));
            if (it.summary.tracks > 0) parts.add (juce::String (it.summary.tracks) + (it.summary.tracks == 1 ? " track" : " tracks"));
            parts.add (mixPurposeName (it.summary.purpose));
            parts.add (styleProfileName (it.summary.profile));
        }
        else parts.add ("Not a DINE session");
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawText (g, parts.joinIntoString ("  " + juce::String (Glyph::dot()) + "  "), text,
                        juce::Justification::centredLeft, true);

        // how its inputs fall across the groups
        if (! bars.isEmpty() && it.summary.valid)
        {
            std::vector<Dine::BarSlice> slices;
            const float total = juce::jmax (1.0f, float (it.summary.inputs));
            for (int bus = 0; bus < int (MixBus::Master); ++bus)
                slices.push_back ({ float (it.summary.perBus[size_t (bus)]) / total, busColour (MixBus (bus)) });
            Dine::drawStackedBar (g, bars.withSizeKeepingCentre (bars.getWidth(), 5), slices);
        }

        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.0f, 500));
        Dine::drawText (g, it.when, when, juce::Justification::centredRight, true);
    }

    static constexpr int kWhenW = 96;

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
    search.setJustification (juce::Justification::centredLeft);   // one line, centred in its box
    search.setTextToShowWhenEmpty ("Search", Dine::ink4);
    search.setIndents (24, 0);
    search.setBorder (juce::BorderSize<int> (0));
    Dine::styleTextEditor (search, Dine::control);
    search.setTextToShowWhenEmpty ("Search sessions", Dine::ink3);
    search.setIndents (34, 0);
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (search);

    const char* names[2] = { "Recent", "Templates" };
    for (int i = 0; i < 2; ++i)
    {
        chips[size_t (i)] = std::make_unique<DineChip> (names[i]);
        chips[size_t (i)]->onClick = [this, i] { filter = (filter == i ? -1 : i); rebuild(); };
        addAndMakeVisible (*chips[size_t (i)]);
    }
    filter = -1;   // everything, until a chip is pressed

    addAndMakeVisible (newButton);
    addAndMakeVisible (openButton);
    addAndMakeVisible (importButton);
    newButton.setCaps (false);
    openButton.setCaps (false);
    importButton.setCaps (false);
    newButton.setTooltip ("Start from nothing: pick the device, name the inputs, then tune.");
    newButton.onClick = [this] { if (onNew) onNew(); };
    openButton.setButtonText ("Open" + juce::String (Glyph::ellip()));
    openButton.setTooltip ("Open a session from anywhere on this Mac.");
    openButton.onClick = [this] { if (onOpenFile) onOpenFile(); };
    importButton.setTooltip ("A folder of stems becomes a session: one track per file, named and assigned.");
    importButton.onClick = [this] { if (onImportFolder) onImportFolder(); };
    setWantsKeyboardFocus (true);
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
        if (filter == 0 && juce::Time::getCurrentTime().toMilliseconds() - it.listing.modified.toMilliseconds() > 7LL * 24 * 3600 * 1000) continue;
        if (filter == 1 && ! it.listing.name.containsIgnoreCase ("template")) continue;
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
    for (int i = 0; i < 2; ++i) chips[size_t (i)]->setToggleState (filter == i, juce::dontSendNotification);
    resized();
    repaint();
}

void SessionsPage::select (int index)
{
    selected = index;
    for (auto& r : rows) r->setToggleState (r->index == selected, juce::dontSendNotification);
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

// The library, as the design lays it out (`01 - Sessions`, 70:9051): the title and its one
// sentence, the three actions at the right, a filter and a search under them, then the rows.
// There is no bar along the foot: a session is opened by double-clicking it, by Return, or
// from its own right-click menu, and the note under the list says where to start from nothing.
void SessionsPage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    auto r = getLocalBounds().reduced (Dine::Metric::padX, 0);
    r.removeFromTop (34);

    auto head = r.removeFromTop (28);
    head.removeFromRight (juce::jmax (0, getRight() - Dine::Metric::padX - openButton.getX()) + 16);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (22.0f, 600));
    Dine::drawText (g, "Sessions", head, juce::Justification::centredLeft, true);
    r.removeFromTop (2);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (13.0f));
    Dine::drawText (g, "Sessions live in ~/Music/DINE. The audio stays in its own folder.",
                    r.removeFromTop (18), juce::Justification::centredLeft, true);

    // the filter track behind the two chips
    if (chips[0] != nullptr)
    {
        auto track = chips[0]->getBounds();
        for (int i = 1; i < 2; ++i) track = track.getUnion (chips[size_t (i)]->getBounds());
        Dine::drawSegmentTrack (g, track.expanded (2, 2));
    }

    // the glyph inside the search field
    Dine::drawIcon (g, Dine::Icon::Sessions,
                    juce::Rectangle<int> (search.getX() + 10, search.getY(), 16, search.getHeight())
                        .toFloat().withSizeKeepingCentre (16.0f, 16.0f), Dine::ink3);

    auto sorted = juce::Rectangle<int> (r.getX(), search.getBottom() + 16, r.getWidth(), 16);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.0f, 500));
    Dine::drawText (g, "Sorted by when it was last saved", sorted, juce::Justification::centredLeft, true);

    if (shown.empty())
    {
        auto empty = juce::Rectangle<int> (r.getX(), viewport.getY() + 30, juce::jmin (620, r.getWidth()), 70);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (14.0f, 600));
        Dine::drawText (g, items.empty() ? "No sessions saved yet" : "Nothing matches that",
                        empty.removeFromTop (20), juce::Justification::topLeft);
        empty.removeFromTop (6);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        Dine::drawFittedText (g, items.empty() ? "Start a new session and it is saved into ~/Music/DINE as you work."
                                               : "Try a different word, or switch the filter off.",
                              empty.removeFromTop (34), juce::Justification::topLeft, 2);
        return;
    }

    // The note sits under the last row rather than at the foot of the page: it is about
    // starting a new session, and it belongs beside the list it is about.
    const int listEnd = juce::jmin (viewport.getBottom(), viewport.getY() + int (rows.size()) * 56);
    auto note = juce::Rectangle<int> (r.getX(), listEnd + 14, r.getWidth(), 18);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (13.0f));
    Dine::drawText (g, "Start from nothing: pick the device, name the inputs, then tune.", note,
                    juce::Justification::centredLeft, true);
}

bool SessionsPage::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey)
    {
        if (const auto* it = selectedItem(); it != nullptr && onOpen) { onOpen (it->listing.file); return true; }
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        if (shown.empty()) return false;
        int at = 0;
        for (int i = 0; i < int (shown.size()); ++i) if (shown[size_t (i)] == selected) at = i;
        at = juce::jlimit (0, int (shown.size()) - 1, at + (key == juce::KeyPress::downKey ? 1 : -1));
        select (shown[size_t (at)]);
        return true;
    }
    return false;
}

void SessionsPage::resized()
{
    auto r = getLocalBounds().reduced (Dine::Metric::padX, 0);
    r.removeFromTop (34);

    // the three actions, right to left, level with the title
    auto head = r.removeFromTop (28);
    const int nw = juce::jmax (104, newButton.idealWidth());
    newButton.setBounds (head.removeFromRight (nw));
    head.removeFromRight (8);
    const int iw = importButton.idealWidth();
    importButton.setBounds (head.removeFromRight (iw));
    head.removeFromRight (8);
    const int ow = juce::jmax (72, openButton.idealWidth());
    openButton.setBounds (head.removeFromRight (ow));

    r.removeFromTop (2 + 18);
    r.removeFromTop (27);

    // the filter and the search share one row
    auto row = r.removeFromTop (30);
    search.setBounds (row.removeFromRight (juce::jmin (280, row.getWidth() / 3)));
    auto chipRow = row.withSizeKeepingCentre (row.getWidth(), Dine::Metric::control);
    int x = chipRow.getX() + 2;
    for (int i = 0; i < 2; ++i)
    {
        const int w = juce::jmax (66, chips[size_t (i)]->idealWidth());
        chips[size_t (i)]->setBounds (x, chipRow.getY() + 2, w, chipRow.getHeight() - 4);
        x += w;
    }

    r.removeFromTop (16 + 16 + 8);
    auto list = r.withTrimmedBottom (shown.empty() ? Dine::Metric::padY : 32 + Dine::Metric::padY);
    // the rows are inset 12 so a lifted plane has room to be a plane
    viewport.setBounds (list.expanded (12, 0));
    const int rowH = 56;
    const int total = int (rows.size()) * rowH;
    listHolder.setSize (viewport.getWidth() - (total > list.getHeight() ? 10 : 0), juce::jmax (total, list.getHeight()));
    int y = 0;
    for (auto& rw : rows) { rw->setBounds (0, y, listHolder.getWidth(), rowH); y += rowH; }
}

// ============================================================================ DevicePage

// A device, as the design draws it (`02 - Audio device`, 70:9274): a lamp, the name, what it
// carries under it, and "In use" at the right of the one that is open. Chosen lifts to a plane.
class DevicePage::DeviceRow : public juce::Button
{
public:
    DeviceRow (const juce::String& name, int in, int out, bool firstRow, bool isOpen)
        : juce::Button (name), deviceName (name), inputChannels (in), outputChannels (out), first (firstRow), open (isOpen) {}

    void setTransport (const juce::String& t) { transport = t; }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto b = getLocalBounds().withTrimmedBottom (4);
        if (on)        Dine::fillRounded (g, b.toFloat(), Dine::selected, Dine::Radius::control);
        else if (over) Dine::fillRounded (g, b.toFloat(), Dine::item, Dine::Radius::control);

        auto r = b.reduced (12, 0);
        const bool usable = inputChannels > 0;
        auto lamp = r.removeFromLeft (8).withSizeKeepingCentre (7, 7);
        g.setColour (open ? Dine::accent : usable ? Dine::ok : Dine::ink4);
        g.fillEllipse (lamp.toFloat());
        r.removeFromLeft (12);

        if (open)
        {
            const auto font = Dine::mono (11.0f, 500);
            auto cell = r.removeFromRight (Dine::textWidth (font, "In use"));
            g.setColour (Dine::ink3);
            g.setFont (font);
            Dine::drawText (g, "In use", cell, juce::Justification::centredRight);
            r.removeFromRight (12);
        }

        auto text = r.withSizeKeepingCentre (r.getWidth(), 36);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, deviceName, text.removeFromTop (18), juce::Justification::centredLeft, true);
        juce::StringArray parts;
        parts.add (juce::String (inputChannels) + " in");
        parts.add (juce::String (outputChannels) + " out");
        if (transport.isNotEmpty()) parts.add (transport);
        if (! usable) parts.add ("output only: nothing comes in this way");
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        Dine::drawText (g, parts.joinIntoString ("  " + juce::String (Glyph::dot()) + "  "), text,
                        juce::Justification::centredLeft, true);
    }

    juce::String deviceName, transport;
    int inputChannels, outputChannels;
    bool first, open;
};

// The output pair the mix is monitored through.
class DevicePage::OutputRow : public juce::Button
{
public:
    OutputRow (const juce::String& name, int channels) : juce::Button (name), deviceName (name), outputChannels (channels) {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto b = getLocalBounds().withTrimmedBottom (2);
        if (on)        Dine::fillRounded (g, b.toFloat(), Dine::selected, Dine::Radius::control);
        else if (over) Dine::fillRounded (g, b.toFloat(), Dine::item, Dine::Radius::control);
        auto r = b.reduced (12, 0);
        auto lamp = r.removeFromLeft (7).withSizeKeepingCentre (7, 7);
        g.setColour (on ? Dine::accent : Dine::ink4);
        g.fillEllipse (lamp.toFloat());
        r.removeFromLeft (11);
        auto name = r.removeFromTop (r.getHeight() / 2 + 1).withTrimmedTop (3);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, on ? 600 : 500));
        Dine::drawText (g, deviceName, name, juce::Justification::centredLeft, true);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        Dine::drawText (g, outputChannels >= 2 ? "Output 1-2  " + Glyph::dot() + "  " + juce::String (outputChannels) + " available"
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
        // The microphone prompt belongs here, at the moment somebody asks for an input device,
        // and not at launch when they have not said what they want yet. macOS puts it up once
        // ever; after that this is an immediate answer either way.
        juce::Component::SafePointer<DevicePage> safe (this);
        services.askForInputPermission ([safe] (bool)
        {
            if (safe == nullptr) return;
            safe->openChosenDevice();
        });
    };
    refresh();
}

DevicePage::~DevicePage() = default;

// Opening is never refused because the inputs would not come: the output opens on its own so
// the session still plays, still mixes and still saves, and the sentence says why the meters
// are still. A device that will not open at all is a different thing, and stays on the page.
void DevicePage::openChosenDevice()
{
    if (selected < 0 || selected >= inputs.size()) return;
    error = services.openDevices (inputs[selected].name, outputName);
    if (error.isNotEmpty()) { repaint(); return; }
    const auto device = services.deviceState();
    if (device.stage == DeviceStage::InputRefused && onToast) onToast (device.why);
    if (onContinue) onContinue();
}

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

// The two columns the design lays this page out in (`02 - Audio device`, 70:9274): the
// devices and where the sound comes out down the left, what it is running at and what is
// arriving down the right. Measured once, so paint and resized cannot disagree.
DevicePage::Column DevicePage::column() const
{
    Column c;
    auto page = getLocalBounds().reduced (Dine::Metric::padX, 0);
    page.removeFromTop (34 + 28 + 2 + 18 + 26);
    auto foot = page.removeFromBottom (Dine::Metric::button + Dine::Metric::padY);
    c.footer = foot.removeFromTop (Dine::Metric::button);

    auto left = page.removeFromLeft ((page.getWidth() - 52) / 2);
    page.removeFromLeft (52);
    auto right = page;

    // left: the devices, then where it comes out directly under them
    c.listCaption = left.removeFromTop (18);
    left.removeFromTop (8);
    const int outsH = 18 + 8 + int (outputRows.size()) * 44 + 12 + Dine::Metric::button;
    const int wanted = juce::jmax (56, int (rows.size()) * 52);
    c.list = left.removeFromTop (juce::jmin (wanted, juce::jmax (56, left.getHeight() - outsH - 30)));
    left.removeFromTop (30);
    c.outCaption = left.removeFromTop (18);
    left.removeFromTop (8);
    c.outputs = left.withTrimmedBottom (12);

    // right: what it is running at, then what is arriving
    c.specCaption = right.removeFromTop (18);
    right.removeFromTop (8);
    c.spec = right.removeFromTop (3 * 64);
    right.removeFromTop (28);
    c.arrivingCaption = right.removeFromTop (18);
    right.removeFromTop (8);
    c.arriving = right;
    return c;
}

namespace
{
    // A caption over a column of this page: sentence case, quiet, the design's `Headline`.
    void columnCaption (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, text, r, juce::Justification::centredLeft, true);
    }
}

void DevicePage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto col = column();
    const bool running = services.isAudioRunning();
    const int channels = running ? services.numInputChannels()
                       : selected >= 0 && selected < inputs.size() ? inputs[selected].inputChannels : 0;

    // ---- the head. The sections live at the top right of the workspace, so the sentence
    // stops well short of them.
    {
        auto r = getLocalBounds().reduced (Dine::Metric::padX, 0);
        r.removeFromTop (34);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (22.0f, 600));
        Dine::drawText (g, "Audio device", r.removeFromTop (28), juce::Justification::centredLeft, true);
        r.removeFromTop (2);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        Dine::drawText (g, "Pick the device your console reaches DINE through. Nothing about the mix changes here.",
                        r.removeFromTop (18).withWidth (juce::jmin (r.getWidth(), 700)), juce::Justification::centredLeft, true);
    }

    // ---- the devices
    columnCaption (g, col.listCaption, "Devices on this machine");
    if (rows.empty())
    {
        auto r = juce::Rectangle<int> (col.list).removeFromTop (60);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, "No inputs yet", r.removeFromTop (18), juce::Justification::centredLeft);
        r.removeFromTop (4);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawFittedText (g, "Connect your interface or console and rescan. You can also work from a folder of stems "
                                 "while nothing is plugged in.",
                              r.removeFromTop (36), juce::Justification::topLeft, 2);
    }

    // ---- where it comes out
    columnCaption (g, col.outCaption, "Where it comes out");
    if (outputRows.empty())
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawText (g, "Nothing to come out of yet.", juce::Rectangle<int> (col.outputs).removeFromTop (18),
                        juce::Justification::centredLeft, true);
    }

    // ---- what it is running at: three rows, each a line and its note, with the value at
    // the right, separated by a hairline rather than boxed in a card.
    columnCaption (g, col.specCaption, "What it is running at");
    struct Line { juce::String key, note, value; juce::Colour colour; };
    const int drops = services.xrunCount();
    const Line lines[3] = {
        { "Sample rate", running ? "Matched to the console" : "Set when the device opens",
          running ? juce::String (services.sampleRate() / 1000.0, 0) + " kHz" : juce::String (Glyph::dash()), Dine::ink },
        { "Buffer", running ? "Lower is sooner. Click to change" : "The lower it is, the sooner you hear it",
          running ? juce::String (services.bufferSize()) + " samples  " + Glyph::dot() + "  "
                        + juce::String (1000.0 * services.bufferSize() / juce::jmax (1.0, services.sampleRate()), 1) + " ms"
                  : juce::String (Glyph::dash()), Dine::ink },
        { "Dropped buffers", drops > 0 ? "Raise the buffer if this keeps climbing" : "Nothing missed",
          running ? juce::String (drops) : juce::String (Glyph::dash()), drops > 0 ? Dine::warn : Dine::ink },
    };
    {
        auto spec = col.spec;
        for (const auto& line : lines)
        {
            auto row = spec.removeFromTop (64);
            g.setColour (Dine::hair);
            g.fillRect (row.removeFromBottom (1));
            auto r = row.withSizeKeepingCentre (row.getWidth(), 36);
            const auto valueFont = Dine::mono (11.0f, 500);
            auto value = r.removeFromRight (Dine::textWidth (valueFont, line.value));
            r.removeFromRight (16);
            g.setColour (line.colour);
            g.setFont (valueFont);
            Dine::drawText (g, line.value, value, juce::Justification::centredRight);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (13.0f, 500));
            Dine::drawText (g, line.key, r.removeFromTop (18), juce::Justification::centredLeft, true);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            Dine::drawText (g, line.note, r, juce::Justification::centredLeft, true);
        }
    }

    // ---- what is arriving: two columns of number, name and a bar, the way the design draws
    // it, so "the console is plugged in but channel 9 is dead" is visible before anything is
    // named.
    columnCaption (g, col.arrivingCaption, "What is arriving");
    {
        auto area = col.arriving;
        const int perRow = area.getWidth() >= 440 ? 2 : 1;
        const int rowH = 26;
        const int maxRows = juce::jmax (0, (area.getHeight() - 18) / rowH);
        const int shownMax = maxRows * perRow;
        const int n = juce::jmin (channels, shownMax);
        if (n <= 0)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            Dine::drawFittedText (g, running ? "No inputs on this device."
                                             : "Press Continue and the meters fill in - your console is untouched.",
                                  area.removeFromTop (40), juce::Justification::topLeft, 2);
        }
        const int colW = perRow == 2 ? (area.getWidth() - 20) / 2 : area.getWidth();
        const auto& session = controller.getSession();
        for (int c = 0; c < n; ++c)
        {
            const int cx = area.getX() + (c % perRow) * (colW + 20);
            const int cy = area.getY() + (c / perRow) * rowH;
            auto row = juce::Rectangle<int> (cx, cy, colW, rowH);
            g.setColour (Dine::ink3);
            g.setFont (Dine::mono (10.0f, 500));
            Dine::drawText (g, juce::String (c + 1).paddedLeft ('0', 2), row.removeFromLeft (22), juce::Justification::centredLeft);
            row.removeFromLeft (8);
            const juce::String name = c < int (session.inputs.size()) && ! session.inputs[size_t (c)].name.empty()
                                          ? juce::String (session.inputs[size_t (c)].name)
                                          : "In " + juce::String (c + 1);
            const bool named = c < int (session.inputs.size()) && ! session.inputs[size_t (c)].name.empty();
            g.setColour (named ? Dine::ink2 : Dine::ink4);
            g.setFont (Dine::text (12.0f, 500));
            Dine::drawText (g, name, row.removeFromLeft (juce::jmin (78, row.getWidth() / 3)), juce::Justification::centredLeft, true);
            row.removeFromLeft (10);
            const float db = running ? services.daw().inputPeakDb (c) : -120.0f;
            Dine::fillMeter (g, row.withSizeKeepingCentre (row.getWidth(), 4).toFloat(), DineMeter::norm (db), false, false, 2.0f);
        }
        if (channels > n && n > 0)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (11.0f));
            Dine::drawText (g, "and " + juce::String (channels - n) + " more",
                            juce::Rectangle<int> (area.getX(), area.getY() + (n + perRow - 1) / perRow * rowH, area.getWidth(), 16),
                            juce::Justification::centredLeft);
        }
    }

    if (error.isNotEmpty())
    {
        g.setColour (Dine::crit);
        g.setFont (Dine::text (12.0f));
        Dine::drawFittedText (g, error, juce::Rectangle<int> (col.footer).withTrimmedRight (280).translated (0, -22),
                              juce::Justification::centredLeft, 2);
    }

    // the note along the foot, left of the buttons
    const juce::String note = selected < 0 || selected >= inputs.size()
                                ? deviceSentence (services.deviceState(), ! inputs.isEmpty())
                            : inputs[selected].inputChannels <= 0 ? juce::String ("Pick a device with inputs to carry on.")
                            : inputs[selected].name + "  " + Glyph::dot() + "  " + juce::String (inputs[selected].inputChannels) + " inputs ready.";
    {
        auto r = col.footer;
        r.removeFromLeft (rescanButton.getWidth() + recordingButton.getWidth() + 20);
        r.removeFromRight (continueButton.getWidth() + backButton.getWidth() + 22);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawText (g, note, r, juce::Justification::centredRight, true);
    }
}

// The Buffer row is the second of the three spec rows (64 pt each, see paint).
void DevicePage::mouseUp (const juce::MouseEvent& e)
{
    const auto spec = column().spec;
    const juce::Rectangle<int> bufferRow (spec.getX(), spec.getY() + 64, spec.getWidth(), 64);
    if (! bufferRow.contains (e.getPosition()) || ! services.isAudioRunning()) return;
    const auto sizes = services.bufferSizes();
    if (sizes.isEmpty()) return;
    juce::PopupMenu m;
    const double rate = juce::jmax (1.0, services.sampleRate());
    for (int n : sizes)
        m.addItem (n, juce::String (n) + " samples  " + Glyph::dot() + "  " + juce::String (1000.0 * n / rate, 1) + " ms",
                   true, n == services.bufferSize());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (bufferRow)),
                     [safe = juce::Component::SafePointer<DevicePage> (this)] (int chosen)
                     {
                         if (safe == nullptr || chosen <= 0) return;
                         const auto err = safe->services.setBufferSize (chosen);
                         if (err.isNotEmpty() && safe->onToast) safe->onToast (err);
                         safe->repaint();
                     });
}

void DevicePage::resized()
{
    const auto col = column();

    viewport.setBounds (col.list.expanded (12, 0));
    viewport.setVisible (! rows.empty());
    const int rowH = 52;
    const int total = int (rows.size()) * rowH;
    listHolder.setSize (viewport.getWidth() - (total > viewport.getHeight() ? 10 : 0), juce::jmax (total, viewport.getHeight()));
    int y = 0;
    for (auto& r : rows) { r->setBounds (0, y, listHolder.getWidth(), rowH); y += rowH; }

    {
        auto outs = col.outputs;
        for (auto& r : outputRows) r->setBounds (outs.removeFromTop (44).expanded (12, 0));
        outs.removeFromTop (10);
        const int w = juce::jmax (110, outputsButton.idealWidth());
        outputsButton.setBounds (outs.removeFromTop (Dine::Metric::button).removeFromLeft (w));
    }

    auto footer = col.footer;
    const int cw = juce::jmax (100, continueButton.idealWidth());
    continueButton.setBounds (footer.removeFromRight (cw));
    footer.removeFromRight (10);
    const int bw = juce::jmax (68, backButton.idealWidth());
    backButton.setBounds (footer.removeFromRight (bw));
    const int rw = juce::jmax (120, rescanButton.idealWidth());
    rescanButton.setBounds (footer.removeFromLeft (rw));
    footer.removeFromLeft (10);
    const int iw = juce::jmax (140, recordingButton.idealWidth());
    recordingButton.setBounds (footer.removeFromLeft (iw));
}

// ============================================================================ AssignPage

class AssignPage::Row : public juce::Component
{
public:
    void lookAndFeelChanged() override { Dine::styleTextEditor (name, Dine::control); }

    Row (AssignPage& owner, int index) : page (owner), input (index)
    {
        addAndMakeVisible (name);
        name.setFont (Dine::text (13.0f));
        name.setJustification (juce::Justification::centredLeft);
        name.setIndents (10, 0);
        name.setBorder (juce::BorderSize<int> (0));
        Dine::styleTextEditor (name, Dine::control);
        name.setTextToShowWhenEmpty ("Untitled", Dine::ink4);
        name.setSelectAllWhenFocused (true);
        name.onTextChange = [this] { page.entries[size_t (input)].name = name.getText(); };
        name.onReturnKey = [this] { name.giveAwayKeyboardFocus(); page.commit(); };
        name.onFocusLost = [this] { page.commit(); };

        addAndMakeVisible (suggest);
        suggest.setIcon (Dine::Icon::UpDown);
        suggest.setPadX (3);
        suggest.setTooltip ("Names DINE can suggest for this input.");
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

    // Input | Name | What it is | Group | Signal here, in the design's order (`03 - Inputs`,
    // 70:9496). Rows are separated by a hairline rather than boxed; a picked-out row lifts.
    void paint (juce::Graphics& g) override
    {
        const auto& e = page.entries[size_t (input)];
        const auto bus = e.assigned ? mixBusForRole (e.role) : MixBus::Master;
        const auto tint = e.assigned ? busColour (bus) : Dine::ink4;
        auto b = getLocalBounds();
        g.setColour (Dine::hair);
        g.fillRect (b.removeFromBottom (1));
        if (e.selected)               Dine::fillRounded (g, b.toFloat(), Dine::selected, Dine::Radius::control);
        else if (isMouseOver (true))  Dine::fillRounded (g, b.toFloat(), Dine::item, Dine::Radius::control);

        auto r = b.reduced (10, 0);
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.0f, 500));
        Dine::drawText (g, e.linkedToNext ? juce::String (input + 1) + Glyph::minus() + juce::String (input + 2) : juce::String (input + 1),
                        r.removeFromLeft (kNumW), juce::Justification::centredLeft);

        // THE SIGNAL COLUMN, AND WHAT TO DO ABOUT IT AT THE DESK.
        //
        // The needle is where it is at this instant; the gain stage is about the loudest moment
        // of a service, so the bar is the held peak and the verdict is read from it. The move
        // is named in decibels because the preamp it is about has a number on it.
        const float db = page.services.isAudioRunning() ? page.services.daw().inputPeakDb (input) : -120.0f;
        auto column = r.removeFromRight (kSignalW);
        auto verdict = column.removeFromRight (kVerdictW);
        auto meter = column.withTrimmedRight (kGap).withSizeKeepingCentre (column.getWidth() - kGap, 4);
        Dine::fillMeter (g, meter.toFloat(), DineMeter::norm (e.peakHoldDb), false, ! e.assigned, 2.0f);
        // Where it is right now, as a mark on the held bar: the hold is the decision, the mark
        // is the reassurance that something is still arriving.
        if (db > -70.0f)
        {
            const float x = meter.getX() + meter.getWidth() * DineMeter::norm (db);
            g.setColour (Dine::ink.withAlpha (0.7f));
            g.fillRect (x - 0.5f, float (meter.getY()) - 2.0f, 1.0f, float (meter.getHeight()) + 4.0f);
        }
        if (e.assigned && ! e.linkedFromPrevious)
        {
            const auto advice = page.controller.liveCaptureAdvice (e.role, e.peakHoldDb);
            const juce::String text = advice.level == MixController::InputAdvice::Level::Healthy
                                          ? juce::String ("OK")
                                          : juce::String (advice.headline);
            const auto colour = gainVerdictColour (advice.level);
            g.setColour (colour);
            g.setFont (Dine::caps (9.5f, 0.04f, 600));
            Dine::drawText (g, text, verdict, juce::Justification::centredLeft, true);
        }
        r.removeFromRight (kGap);
        r.removeFromRight (pairWidth() + kGap);

        // the group, in its colour, with a lamp before it
        auto busCell = r.removeFromRight (kBusW);
        auto lamp = busCell.removeFromLeft (7).withSizeKeepingCentre (7, 7);
        g.setColour (tint);
        g.fillEllipse (lamp.toFloat());
        busCell.removeFromLeft (8);
        g.setColour (e.assigned ? Dine::ink2 : Dine::ink4);
        g.setFont (Dine::text (12.0f, 500));
        Dine::drawText (g, e.assigned ? juce::String (busLabel (bus)) : "Not used", busCell, juce::Justification::centredLeft, true);

        // an unassigned input that is carrying signal is worth saying out loud
        if (! e.assigned && db > -54.0f)
        {
            const juce::String text = db > -30.0f ? "SIGNAL HERE" : "FAINT";
            const int w = Dine::textWidth (Dine::caps (9.0f, 0.04f, 600), text) + 14;
            auto flag = juce::Rectangle<int> (meter.getX(), b.getY(), w, b.getHeight());
            if (flag.getRight() < b.getRight())
                Dine::drawStatusChip (g, flag.withSizeKeepingCentre (w, 16).toFloat(), text, Dine::warn);
        }
    }

    // What it is, and what group it lands in, are read together - so the source popup takes the
    // whole run between the name and the group rather than stopping at a fixed width and
    // leaving a hand's width of nothing in the middle of the row.
    int pairWidth() const { return juce::jmax (kPairW, link.idealWidth()); }

    int sourceWidth() const
    {
        auto r = getLocalBounds().reduced (10, 0);
        const int taken = kNumW + kNameW + 22 + (kGap - 6) + kBusW + pairWidth() + kSignalW + 3 * kGap;
        return juce::jmax (120, r.getWidth() - taken);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedBottom (1).reduced (10, 0);
        r.removeFromLeft (kNumW);
        name.setBounds (r.removeFromLeft (kNameW).withSizeKeepingCentre (kNameW, 30));
        suggest.setBounds (r.removeFromLeft (22).withSizeKeepingCentre (20, 20));
        r.removeFromLeft (kGap - 6);
        const int sw = sourceWidth();
        source.setBounds (r.removeFromLeft (sw).withSizeKeepingCentre (sw, 30));
        r.removeFromRight (kSignalW + kGap);
        const int pw = pairWidth();
        link.setBounds (r.removeFromRight (pw).withSizeKeepingCentre (pw, 20));
    }

    static constexpr int kNumW = 52, kGap = 20, kNameW = 200, kSignalW = 260, kPairW = 56, kBusW = 140;
    // Wide enough for the longest thing the verdict says: "CLIPPING - PREAMP DOWN 10 dB".
    static constexpr int kVerdictW = 168;

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
        g.setColour (over ? Dine::ink : Dine::ink2);
        g.setFont (Dine::text (12.0f, 600));
        const int w = Dine::textWidth (Dine::text (12.0f, 600), name);
        Dine::drawText (g, name, r.removeFromLeft (w), juce::Justification::centredLeft);
        r.removeFromLeft (8);
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (10.0f, 500));
        Dine::drawText (g, juce::String (n), r.removeFromLeft (24), juce::Justification::centredLeft);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f));
        Dine::drawText (g, over ? "Select them all" : unused ? "stays out of the mix" : juce::String(),
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
        Dine::drawText (g, heading, title, juce::Justification::centredLeft, true);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f));
        Dine::drawFittedText (g, detail, r.withTrimmedBottom (4), juce::Justification::topLeft, 2);
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
    search.setJustification (juce::Justification::centredLeft);   // one line, centred in its box
    search.setTextToShowWhenEmpty ("Search", Dine::ink4);
    search.setIndents (24, 0);
    search.setBorder (juce::BorderSize<int> (0));
    Dine::styleTextEditor (search, Dine::control);
    search.setIndents (12, 0);
    search.onTextChange = [this] { query = search.getText(); rebuild(); };
    addAndMakeVisible (search);

    // The filter the design draws: everything, or only what is still open. One group at a
    // time is a choice under the list button beside it, so the row keeps its room for the
    // three things a volunteer actually presses.
    chips.push_back (std::make_unique<DineChip> ("All inputs"));
    chips.back()->onClick = [this] { busFilter = -2; rebuild(); };
    chips.push_back (std::make_unique<DineChip> ("Not used"));
    chips.back()->onClick = [this] { busFilter = busFilter == -1 ? -2 : -1; rebuild(); };
    for (auto& c : chips) addAndMakeVisible (*c);

    for (auto* b : { &selectAllButton, &deskLabelsButton, &groupButton, &bulkButton, &kitButton, &nameButton,
                     &linkButton, &dropButton, &deselectButton, &continueButton, &backButton, &clearButton,
                     &showUnusedButton })
        addAndMakeVisible (*b);
    continueButton.setCaps (false);
    groupButton.setIcon (Dine::Icon::List);
    groupButton.setPadX (5);
    groupButton.setTooltip ("Show one group at a time, or group the list by the bus each input feeds.");
    bulkButton.setIcon (Dine::Icon::UpDown);
    kitButton.setIcon (Dine::Icon::UpDown);
    kitButton.setTooltip ("Lay a whole kit down the selection, in order: the first input gets the first source, the next the next.");
    deskLabelsButton.setTooltip ("Every input still without a name takes its channel number, the way it is written on the desk.");

    selectAllButton.onClick = [this] { if (quickButtons[1]->onClick) quickButtons[1]->onClick(); };
    deskLabelsButton.onClick = [this]
    {
        for (int i = 0; i < numInputs; ++i)
            if (entries[size_t (i)].name.trim().isEmpty())
                entries[size_t (i)].name = "Desk " + juce::String (i + 1).paddedLeft ('0', 2);
        commit();
        rebuild();
    };
    groupButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Every input", true, busFilter == -2);
        for (int b = 0; b < int (MixBus::Master); ++b)
            m.addItem (10 + b, juce::String ("Only ") + busLabel (MixBus (b)), true, busFilter == b);
        m.addSeparator();
        m.addItem (2, grouped ? "Show one flat list" : "Group by the bus each input feeds", true, grouped);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&groupButton).withMinimumWidth (220),
                         [this] (int r)
                         {
                             if (r == 1) busFilter = -2;
                             else if (r == 2) grouped = ! grouped;
                             else if (r >= 10) busFilter = r - 10;
                             else return;
                             rebuild();
                         });
    };
    bulkButton.onClick = [this] { showBulkMenu (bulkButton); };
    kitButton.onClick = [this] { showKitMenu (kitButton); };
    // The three named buttons act on the whole desk, because that is what the words say and
    // because they are on screen whether anything is selected or not.
    nameButton.onClick = [this] { if (quickButtons[0]->onClick) quickButtons[0]->onClick(); };
    linkButton.onClick = [this] { if (quickButtons[2]->onClick) quickButtons[2]->onClick(); };
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
    quickButton.setTooltip ("Everything else a whole desk can have done to it at once.");
    quickButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Name everything from what it is");
        m.addItem (2, "Select every input not used");
        m.addItem (3, "Pair every L and R");
        m.addSeparator();
        m.addItem (4, grouped ? "Show one flat list" : "Group by the bus each input feeds");
        m.addItem (5, "Save this patch" + juce::String (Glyph::ellip()));
        m.addItem (6, "Apply a saved patch" + juce::String (Glyph::ellip()));
        m.addSeparator();
        m.addItem (7, "Clear every assignment");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&quickButton).withMinimumWidth (280),
                         [this] (int r)
                         {
                             if (r >= 1 && r <= 3) { if (quickButtons[size_t (r - 1)]->onClick) quickButtons[size_t (r - 1)]->onClick(); return; }
                             if (r == 4) { grouped = ! grouped; rebuild(); return; }
                             if (r == 5) { if (onSaveMapping) onSaveMapping(); return; }
                             if (r == 6) { if (onApplyMapping) onApplyMapping(); return; }
                             if (r == 7) clearAll();
                         });
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

// THE LEVELS MOVE WHILE THE BAND PLAYS. This page had no tick at all: its meters only
// redrew when somebody typed, which is the opposite of what a soundcheck needs from the one
// screen where the preamps are being set.
void AssignPage::tick()
{
    if (entries.empty()) return;
    const bool running = services.isAudioRunning();
    bool moved = false;
    for (int i = 0; i < numInputs && i < int (entries.size()); ++i)
    {
        const float now = running ? services.daw().inputPeakDb (i) : -120.0f;
        auto& hold = entries[size_t (i)].peakHoldDb;
        // Up instantly, down at 3 dB a second: the loudest moment stays on screen long enough
        // to walk from the stage to the desk and read it.
        const float next = juce::jmax (now, hold - 0.1f);
        if (std::fabs (next - hold) > 0.05f) { hold = next; moved = true; }
        else hold = next;
    }
    if (moved) repaint();
}

// How many assigned inputs want the preamp moved right now.
int AssignPage::inputsNeedingGain() const
{
    int n = 0;
    for (int i = 0; i < numInputs && i < int (entries.size()); ++i)
    {
        const auto& e = entries[size_t (i)];
        if (! e.assigned || e.linkedFromPrevious) continue;
        // Only the ones with a move to make. "Nothing has arrived here yet" is true of every
        // input before the band plays, and a page that says twelve inputs need the preamp
        // when nothing is plugged in has taught somebody to ignore it by the second Sunday.
        using Level = MixController::InputAdvice::Level;
        switch (controller.liveCaptureAdvice (e.role, e.peakHoldDb).level)
        {
            case Level::Clipping: case Level::Hot: case Level::Low: case Level::Faint: ++n; break;
            default: break;
        }
    }
    return n;
}

void AssignPage::refresh()
{
    // Every channel the device brings in, and never fewer than the session already uses: an
    // imported multitrack, or a session opened without its console, still shows all its inputs.
    numInputs = juce::jlimit (0, kMaxInputs, services.numInputChannels());
    for (const auto& in : controller.getSession().inputs)
        numInputs = juce::jlimit (0, kMaxInputs, juce::jmax (numInputs, in.inputA + 1, in.inputB + 1));
    // The held peaks survive a refresh: renaming an input is not a reason to forget how loud
    // it has been, and refresh() runs on every edit.
    std::vector<float> heldPeaks (size_t (numInputs), -120.0f);
    for (size_t i = 0; i < heldPeaks.size() && i < entries.size(); ++i) heldPeaks[i] = entries[i].peakHoldDb;
    entries.assign (size_t (numInputs), Entry {});
    for (size_t i = 0; i < heldPeaks.size(); ++i) entries[i].peakHoldDb = heldPeaks[i];
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
    chips[0]->setToggleState (busFilter == -2, juce::dontSendNotification);
    chips[1]->setToggleState (busFilter == -1, juce::dontSendNotification);
    groupButton.setToggleState (grouped, juce::dontSendNotification);
    continueButton.setEnabled (assignedCount() > 0);
    updateToolbar();
    resized();
    repaint();
}

void AssignPage::updateToolbar()
{
    // The tool row has two states: what a whole desk can have done to it, and - once inputs are
    // picked out - what those inputs can. The filter and the grouping stay in both, because a
    // selection made under a filter is still a selection.
    const bool any = selectionCount() > 0;
    for (auto* b : { &bulkButton, &kitButton, &deskLabelsButton, &dropButton, &deselectButton })
        b->setVisible (any);
    for (auto* b : { &nameButton, &linkButton, &selectAllButton, &quickButton })
        b->setVisible (! any);
    search.setVisible (false);
    patchSaveButton.setVisible (false);
    patchApplyButton.setVisible (false);
    for (auto& c : chips) c->setVisible (true);
    groupButton.setVisible (true);
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
    // The order is the one the tracks were arranged in, not the desk's: an input that was
    // already assigned keeps its place, and a new one goes after them in channel order.
    // Rebuilding by channel undid every rearrangement made on TRACKS the moment this page
    // was opened again.
    const auto& before = controller.getSession().inputs;
    auto placeOf = [&before] (const InputAssignment& a)
    {
        for (size_t i = 0; i < before.size(); ++i) if (before[i].inputA == a.inputA) return int (i);
        return int (before.size()) + a.inputA;
    };
    std::stable_sort (s.inputs.begin(), s.inputs.end(),
                      [&] (const InputAssignment& x, const InputAssignment& y) { return placeOf (x) < placeOf (y); });
    controller.setSession (s);
    // The timeline follows at once: a pair linked here is one stereo track from this moment,
    // so a take started before the graph is rebuilt still records both of its channels.
    services.daw().setSession (s);
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

    // ---- the head. The ROUTING sections sit at the top right of the workspace, so the
    // sentence stops short of them and the progress readout has moved to the foot.
    {
        auto r = getLocalBounds().reduced (Dine::Metric::padX, 0);
        r.removeFromTop (34);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (22.0f, 600));
        Dine::drawText (g, "Inputs", r.removeFromTop (28), juce::Justification::centredLeft, true);
        r.removeFromTop (2);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        Dine::drawText (g, "Every assigned input takes its short desk name. Press Continue and the meters fill in "
                           + juce::String (Glyph::dash()) + " your console is untouched.",
                        r.removeFromTop (18).withWidth (juce::jmin (r.getWidth(), 760)), juce::Justification::centredLeft, true);
    }

    // ---- the tool row: what to do to a selection at the left, the filter at the right
    if (selectionCount() == 0)
    {
        auto track = juce::Rectangle<int>();
        for (auto& c : chips) track = track.isEmpty() ? c->getBounds() : track.getUnion (c->getBounds());
        if (! track.isEmpty()) Dine::drawSegmentTrack (g, track.expanded (2, 2));
    }
    else
    {
        auto label = juce::Rectangle<int> (deselectButton.getRight() + 12, L.toolbar.getY(),
                                           juce::jmax (0, chips.empty() ? 0 : chips.front()->getX() - deselectButton.getRight() - 24),
                                           L.toolbar.getHeight());
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.0f, 500));
        Dine::drawText (g, juce::String (selectionCount()) + (selectionCount() == 1 ? " input selected" : " inputs selected"),
                        label, juce::Justification::centredLeft, true);
    }

    // ---- the column captions, over the table
    {
        auto head = juce::Rectangle<int> (L.main.getX(), L.main.getY(), L.main.getWidth(), 18).reduced (10, 0);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        auto cell = [&g, &head] (int w, const juce::String& text, bool fromRight = false)
        {
            auto c = fromRight ? head.removeFromRight (w) : head.removeFromLeft (w);
            Dine::drawText (g, text, c, juce::Justification::centredLeft, true);
        };
        cell (Row::kNumW, "Input");
        cell (Row::kNameW + 22, "Name");
        head.removeFromLeft (Row::kGap - 6);
        auto signal = head.removeFromRight (Row::kSignalW);
        auto verdict = signal.removeFromRight (Row::kVerdictW);
        head.removeFromRight (Row::kGap);
        head.removeFromRight (Row::kPairW + Row::kGap);
        auto group = head.removeFromRight (Row::kBusW);
        Dine::drawText (g, "What it is", head, juce::Justification::centredLeft, true);
        Dine::drawText (g, "Group", group, juce::Justification::centredLeft, true);
        Dine::drawText (g, "Loudest so far", signal, juce::Justification::centredLeft, true);
        Dine::drawText (g, "At the desk", verdict, juce::Justification::centredLeft, true);
    }

    if (rows.empty())
    {
        auto empty = L.main.withTrimmedTop (30).removeFromTop (72).withWidth (juce::jmin (620, L.main.getWidth()));
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, numInputs == 0 ? "No inputs to name yet" : "Nothing matches that",
                        empty.removeFromTop (18), juce::Justification::centredLeft);
        empty.removeFromTop (4);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawFittedText (g, numInputs == 0 ? "Go back and pick a device with inputs, or import a folder of stems."
                                                : "Switch the filter back to All inputs.",
                              empty.removeFromTop (34), juce::Justification::topLeft, 2);
    }

    // ---- the foot: where the patch stands, then Back and Continue
    int live = 0;
    for (int i = 0; i < numInputs; ++i)
        if (! entries[size_t (i)].assigned && ! entries[size_t (i)].linkedFromPrevious
            && services.isAudioRunning() && services.daw().inputPeakDb (i) > -54.0f) ++live;
    {
        auto foot = L.footer.reduced (Dine::Metric::padX, 0);
        foot.removeFromRight (continueButton.getWidth() + backButton.getWidth() + 22);
        const juce::String left = unused == 0 && assigned > 0 ? juce::String ("Every input placed")
                                : assigned == 0 ? juce::String ("Name at least one input to carry on.")
                                : juce::String (unused) + (unused == 1 ? " input is" : " inputs are") + " still open";
        g.setColour (unused == 0 && assigned > 0 ? Dine::accent : assigned == 0 ? Dine::ink3 : Dine::ink3);
        g.setFont (Dine::text (12.0f, 500));
        const int w = Dine::textWidth (Dine::text (12.0f, 500), left);
        Dine::drawText (g, left, foot.removeFromLeft (w), juce::Justification::centredLeft);
        // THE PREAMPS COME FIRST. A gain stage that is wrong is the one thing no amount of
        // tuning can put right, and this is the screen somebody is on with a hand on the desk -
        // so the count of inputs that want a preamp move is said here, before Continue.
        if (const int gain = inputsNeedingGain(); gain > 0)
        {
            foot.removeFromLeft (16);
            g.setColour (Dine::warn);
            g.setFont (Dine::text (12.0f, 500));
            const juce::String note = juce::String (gain) + (gain == 1 ? " input wants" : " inputs want")
                                    + " the preamp moved at the desk.";
            const int gw = Dine::textWidth (Dine::text (12.0f, 500), note);
            Dine::drawText (g, note, foot.removeFromLeft (juce::jmin (foot.getWidth(), gw)),
                        juce::Justification::centredLeft, true);
        }
        if (live > 0)
        {
            foot.removeFromLeft (16);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            Dine::drawText (g, juce::String (live) + " unassigned " + (live == 1 ? "input is" : "inputs are")
                                + " carrying signal right now.",
                            foot, juce::Justification::centredLeft, true);
        }
    }
}

void AssignPage::resized()
{
    const auto L = layout();

    // ---- the tool row. The three things a volunteer does to a whole desk at once are named
    // in full at the left (design `03 - Inputs`, 70:9496); everything else a selection can
    // have done to it is one press away under Quick actions.
    auto bar = L.toolbar.withSizeKeepingCentre (L.toolbar.getWidth(), Dine::Metric::control);
    {
        // the filter, right-aligned
        auto right = bar;
        int x = right.getRight();
        for (int i = int (chips.size()) - 1; i >= 0; --i)
        {
            const int w = juce::jmax (84, chips[size_t (i)]->idealWidth());
            x -= w;
            chips[size_t (i)]->setBounds (x, bar.getY() + 2, w, bar.getHeight() - 4);
        }
        const int gw = 30;
        groupButton.setBounds (juce::Rectangle<int> (x - 8 - gw, bar.getY(), gw, bar.getHeight()));
        bar = bar.withRight (groupButton.getX() - 12);
    }

    if (selectionCount() == 0)
    {
        auto left = bar;
        for (auto* b : { &nameButton, &linkButton, &selectAllButton })
        {
            const int w = juce::jmax (110, b->idealWidth());
            b->setBounds (left.removeFromLeft (juce::jmin (w, juce::jmax (0, left.getWidth()))));
            left.removeFromLeft (8);
        }
        const int qw = juce::jmax (100, quickButton.idealWidth());
        quickButton.setBounds (left.removeFromLeft (juce::jmin (qw, juce::jmax (0, left.getWidth()))));
    }
    else
    {
        auto left = bar;
        const int dw = juce::jmax (78, deselectButton.idealWidth());
        deselectButton.setBounds (left.removeFromLeft (dw));
        left.removeFromLeft (12);
        left.removeFromLeft (juce::jmin (160, left.getWidth() / 3));   // the count, painted
        for (auto* b : { &bulkButton, &kitButton, &deskLabelsButton, &dropButton })
        {
            const int w = juce::jmax (86, b->idealWidth());
            b->setBounds (left.removeFromLeft (juce::jmin (w, juce::jmax (0, left.getWidth()))));
            left.removeFromLeft (8);
        }
    }

    // ---- the list. The column captions are drawn over it, so it starts under them; a flat
    // list has no band over it at all, because "All inputs" over every input says nothing.
    const bool banded = grouped && groups.size() > 1;
    auto list = L.main.withTrimmedTop (18 + 6);
    viewport.setBounds (list.expanded (10, 0));
    const int rowH = 46, groupH = banded ? 30 : 0;
    int total = 0;
    for (const auto& g : groups) total += groupH + int (g.inputs.size()) * rowH + (banded ? 8 : 0);
    listHolder.setSize (viewport.getWidth() - (total > list.getHeight() ? 10 : 0), juce::jmax (total + 8, list.getHeight()));
    int y = 0;
    size_t r = 0;
    for (size_t gi = 0; gi < groups.size(); ++gi)
    {
        if (gi < headers.size())
        {
            headers[gi]->setVisible (banded);
            if (banded) headers[gi]->setBounds (0, y, listHolder.getWidth(), groupH);
        }
        y += groupH;
        for (size_t n = 0; n < groups[gi].inputs.size() && r < rows.size(); ++n, ++r)
        {
            rows[r]->setBounds (0, y, listHolder.getWidth(), rowH);
            y += rowH;
        }
        if (banded) y += 8;
    }
    showUnusedButton.setVisible (false);

    auto footer = juce::Rectangle<int> (L.footer).removeFromTop (Dine::Metric::button).reduced (Dine::Metric::padX, 0);
    const int cw = juce::jmax (100, continueButton.idealWidth());
    continueButton.setBounds (footer.removeFromRight (cw).withSizeKeepingCentre (cw, Dine::Metric::button));
    footer.removeFromRight (10);
    const int bw = juce::jmax (68, backButton.idealWidth());
    backButton.setBounds (footer.removeFromRight (bw).withSizeKeepingCentre (bw, Dine::Metric::button));
    const int clw = juce::jmax (80, clearButton.idealWidth());
    clearButton.setBounds (footer.removeFromRight (clw + 10).removeFromLeft (clw).withSizeKeepingCentre (clw, Dine::Metric::button));
    clearButton.setVisible (false);
}

// ============================================================================ PurposePage

namespace
{
    // A section caption on a set-up page: the design's Headline, sentence case, never capitals.
    void sectionCaption (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, text, r, juce::Justification::centredLeft, true);
    }
}

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
        // The heading is the card; the tag is a hint and gives way to it, then disappears.
        const int headingW = Dine::textWidth (Dine::text (15.0f), heading);
        if (tag.isNotEmpty())
        {
            // Whole, or not at all: half a hint is not a hint, and the card's own sentence
            // below says the same thing in full.
            const int w = Dine::textWidth (Dine::text (11.0f), tag);
            if (w <= top.getWidth() - headingW - 12)
            {
                g.setColour (Dine::ink4);
                g.setFont (Dine::text (11.0f));
                Dine::drawText (g, tag, top.removeFromRight (w), juce::Justification::centredRight, true);
            }
        }
        g.setColour (Dine::ink);
        g.setFont (Dine::text (15.0f));
        Dine::drawText (g, heading, top, juce::Justification::centredLeft, true);

        r.removeFromTop (10);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.5f));
        Dine::drawFittedText (g, detail, r.removeFromTop (specs.empty() ? r.getHeight() : 40), juce::Justification::topLeft, 3, 1.0f);

        if (specs.empty()) return;
        r.removeFromTop (10);
        juce::String line;
        for (const auto& s : specs) line += (line.isEmpty() ? "" : "  " + juce::String (Glyph::dot()) + "  ") + s.second;
        g.setColour (Dine::accent);
        g.setFont (Dine::mono (11.0f));
        Dine::drawText (g, line, r.removeFromTop (14), juce::Justification::centredLeft, true);
    }

    juce::String heading, detail, tag;
    std::vector<std::pair<juce::String, juce::String>> specs;
};

namespace
{
    // A purpose card is tall enough for its name, its sentence and the three numbers it
    // promises; the grid wraps at its natural height, the page scrolls if it must.
    constexpr int kPurposeTileH = 136;

    // Every number on this page is the profile's own: the loudness a delivery wants, the
    // ceiling its true peaks must stay under, and how much the loudness may wander. They
    // live in ProfileData, so what the card promises is what TUNE MIX actually aims at.
    SourceTargets masterTargets (StyleProfileId profile, MixPurpose purpose)
    {
        return Profiles::targets (profile, masterRoleFor (purpose));
    }

    // The SOUND cards wrap like the PURPOSE cards do: three across on a 1280 desk, six on a
    // 1920 one. One function decides the rows, so paint() and resized() cannot disagree.
    // A card is 300 wide when there is room and never narrower than 260: four purpose cards
    // fit one row on a 1280 desk that way instead of wrapping a lone fourth under the others.
    constexpr int kSoundTileH = 110, kTileW = 300, kTileMinW = 260, kTileGap = 12;
    int soundColumns (int width, int count) { return juce::jlimit (1, juce::jmax (1, count), (width + kTileGap) / (kTileMinW + kTileGap)); }
    int gridHeight (int width, int count, int tileH)
    {
        const int rows = (count + soundColumns (width, count) - 1) / soundColumns (width, count);
        return rows * tileH + (rows - 1) * kTileGap;
    }
    int soundGridHeight (int width, int count)   { return gridHeight (width, count, kSoundTileH); }
    int purposeGridHeight (int width, int count) { return gridHeight (width, count, kPurposeTileH); }

    juce::String lufs (float v)   { return juce::String (v, 0) + " LUFS"; }
    juce::String dbtp (float v)   { return juce::String (v, 1) + " dBTP"; }
    juce::String window (float lu) { return lu <= 1.0f ? "Tight" : lu <= 2.0f ? "Open" : "Wide"; }
}

PurposePage::PurposePage (MixController& c) : controller (c)
{
    const char* purposeLines[] = {
        "Only for a feed with a spec to meet: a television or radio desk. -23 LUFS is that spec, and 9 dB under a stream.",
        "YouTube, Facebook, the church platform. What a stream is normalised to, with the headroom the encoder needs.",
        "Captured to keep. Headroom is left for whoever edits it later.",
        "Rehearsal, a listening feed, the room itself. The band keeps its dynamics."
    };
    for (int i = 0; i < int (MixPurpose::Count); ++i)
    {
        auto t = std::make_unique<Tile> (mixPurposeName (MixPurpose (i)), purposeLines[i]);
        t->setClickingTogglesState (false);
        t->onClick = [this, i] { controller.setPurpose (MixPurpose (i)); refresh(); };
        body.addAndMakeVisible (*t);
        purposeTiles.push_back (std::move (t));
    }
    // One sentence per sound, in the profile's order. The first two are the church; the rest
    // are the other rooms the same desk ends up in. Every card is a real profile with its own
    // numbers (ProfileData.cpp / MixProfileData.cpp), never a rename of another.
    static_assert (int (StyleProfileId::Count) == 6, "add a sentence and a tag for the new profile");
    const char* soundLines[] = {
        "Full low end, a forward vocal and drums that push. Organ and keys sit wide behind them.",
        "Guitars and pads carry it. The vocal is warm rather than bright and the kick stays tight.",
        "A kit that hits, guitars that carry the song and a voice that cuts through them. Dense, and the room kept small.",
        "The sub owns the low end, the snare cracks, the voice is smooth and close over wide keys. The delays are part of the song.",
        "Open dynamics and nothing driven. The kit is one instrument through the overheads, the piano is forward, the room is welcome.",
        "Every voice held steady, close and clear. The music is a bed under the words, never a mix of its own."
    };
    const char* soundTags[] = { "Default", "A documented delta", "Club, festival, indie", "Studio, urban",
                                "Jazz, folk, unplugged", "Conference, podcast" };
    for (int i = 0; i < int (StyleProfileId::Count); ++i)
    {
        auto t = std::make_unique<Tile> (styleProfileName (StyleProfileId (i)), soundLines[i]);
        t->tag = soundTags[i];
        t->setClickingTogglesState (false);
        t->onClick = [this, i] { controller.setProfile (StyleProfileId (i)); refresh(); };
        body.addAndMakeVisible (*t);
        soundTiles.push_back (std::move (t));
    }
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&body, false);
    viewport.setScrollBarsShown (true, false);
    Dine::nativeScrolling (viewport);
    body.addAndMakeVisible (deliveryButton);
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
    body.repaint();
    repaint();
}

SetupLayout PurposePage::layout() const { return SetupLayout::of (getLocalBounds(), false, false); }

namespace
{
    // The body's bands, top to bottom. One place, read by bodyHeight(), paintBody() and resized().
    constexpr int kSectionCaption = 14, kCaptionGap = 10, kSectionGap = 22, kClosing = 44;
}

int PurposePage::bodyHeight (int width) const
{
    return kSectionCaption + kCaptionGap + purposeGridHeight (width, int (purposeTiles.size())) + kSectionGap
         + kSectionCaption + kCaptionGap + soundGridHeight (width, int (soundTiles.size())) + kSectionGap
         + kSectionCaption + kCaptionGap + Dine::Metric::button + 18 + kClosing + 12;
}

void PurposePage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto L = layout();
    drawSetupHead (g, L.head, "Purpose and sound",
                   "What the mix is for and what it should sound like. Changing either changes nothing until the next TUNE MIX.");
    drawSetupFooter (g, getLocalBounds(), "DINE listens for about thirty seconds, then sets the whole mix.",
                     continueButton.getWidth() + backButton.getWidth() + 10);
}

void PurposePage::paintBody (juce::Graphics& g)
{
    const auto& session = controller.getSession();
    auto target = masterTargets (session.profile, session.purpose);
    if (const float wanted = session.deliveryTargetLufs(); wanted < 0.0f && target.loudnessTargetAppropriate)
    {
        target.targetLufs = wanted;
        target.truePeakCeilingDb = juce::jmin (target.truePeakCeilingDb, wanted >= -15.0f ? -1.0f : -1.5f);
    }

    auto main = body.getLocalBounds();
    sectionCaption (g, main.removeFromTop (kSectionCaption), "Purpose");
    main.removeFromTop (kCaptionGap + purposeGridHeight (main.getWidth(), int (purposeTiles.size())) + kSectionGap);
    sectionCaption (g, main.removeFromTop (kSectionCaption), "Sound");
    main.removeFromTop (kCaptionGap + soundGridHeight (main.getWidth(), int (soundTiles.size())) + kSectionGap);
    sectionCaption (g, main.removeFromTop (kSectionCaption), "How loud it should end up");
    main.removeFromTop (kCaptionGap);
    auto row = main.removeFromTop (Dine::Metric::button);
    row.removeFromLeft (deliveryButton.getWidth() + 14);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.5f));
    Dine::drawText (g, deliveryLoudnessHint (session.delivery), row, juce::Justification::centredLeft, true);
    main.removeFromTop (18);
    g.setColour (Dine::ink2);
    g.setFont (Dine::text (13.0f));
    Dine::drawFittedText (g, "DINE will land the mix at " + lufs (target.targetLufs) + ", never letting it peak past "
                          + dbtp (target.truePeakCeilingDb) + ", and tune every group toward "
                          + juce::String (styleProfileName (session.profile)) + ". Purpose and sound can be switched mid-service: "
                          "the next tune follows the new one, and anything you moved by hand is kept.",
                      main.removeFromTop (kClosing).withWidth (juce::jmin (main.getWidth(), 720)), juce::Justification::topLeft, 3, 1.0f);
}

void PurposePage::resized()
{
    const auto L = layout();
    viewport.setBounds (L.main);
    const int width = viewport.getMaximumVisibleWidth();
    body.setSize (width, juce::jmax (viewport.getMaximumVisibleHeight(), bodyHeight (width)));

    auto main = body.getLocalBounds();
    main.removeFromTop (kSectionCaption + kCaptionGap);

    auto purposeArea = main.removeFromTop (purposeGridHeight (main.getWidth(), int (purposeTiles.size())));
    const int cols = soundColumns (purposeArea.getWidth(), int (purposeTiles.size()));
    const int tileH = kPurposeTileH;
    const int tileW = juce::jmin (kTileW, (purposeArea.getWidth() - kTileGap * (cols - 1)) / cols);
    for (int i = 0; i < int (purposeTiles.size()); ++i)
    {
        const int c = i % cols, r = i / cols;
        purposeTiles[size_t (i)]->setBounds (purposeArea.getX() + c * (tileW + kTileGap),
                                             purposeArea.getY() + r * (tileH + kTileGap), tileW, tileH);
    }
    main.removeFromTop (kSectionGap + kSectionCaption + kCaptionGap);
    auto soundArea = main.removeFromTop (soundGridHeight (main.getWidth(), int (soundTiles.size())));
    {
        const int scols = soundColumns (soundArea.getWidth(), int (soundTiles.size()));
        const int stileW = juce::jmin (kTileW, (soundArea.getWidth() - kTileGap * (scols - 1)) / scols);
        for (int i = 0; i < int (soundTiles.size()); ++i)
        {
            const int c = i % scols, r = i / scols;
            soundTiles[size_t (i)]->setBounds (soundArea.getX() + c * (stileW + kTileGap),
                                               soundArea.getY() + r * (kSoundTileH + kTileGap), stileW, kSoundTileH);
        }
    }

    main.removeFromTop (kSectionGap + kSectionCaption + kCaptionGap);
    auto loudRow = main.removeFromTop (Dine::Metric::button);
    deliveryButton.setBounds (loudRow.removeFromLeft (juce::jmin (300, juce::jmax (220, deliveryButton.idealWidth()))));

    auto footer = juce::Rectangle<int> (L.footer).removeFromTop (Dine::Metric::button).reduced (Dine::Metric::padX, 0);
    const int cw = juce::jmax (130, continueButton.idealWidth());
    continueButton.setBounds (footer.removeFromRight (cw).withSizeKeepingCentre (cw, Dine::Metric::button));
    footer.removeFromRight (10);
    const int bw = juce::jmax (72, backButton.idealWidth());
    backButton.setBounds (footer.removeFromRight (bw).withSizeKeepingCentre (bw, Dine::Metric::button));
}

} // namespace livemix

// A theme change: the editors' colours are set on them, so they are set again.
void livemix::SessionsPage::lookAndFeelChanged() { Dine::styleTextEditor (search, Dine::control); }
void livemix::AssignPage::lookAndFeelChanged()   { Dine::styleTextEditor (search, Dine::control); }
