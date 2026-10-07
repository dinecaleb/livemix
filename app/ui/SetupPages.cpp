#include "SetupPages.h"
#include "AboutSheet.h"
#include "AppTheme.h"
#include "Core/ProductDefinition.h"
#include "Profiles/Profile.h"
#include "native/StemNames.h"
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

    // A bus name as it is written in a sentence rather than shouted on a fader: DRUMS -> Drums,
    // and BGV, an initialism, as it is.
    juce::String busLabel (MixBus b)
    {
        const juce::String n (mixBusName (b));
        return n.length() <= 3 ? n.toUpperCase() : n.substring (0, 1) + n.substring (1).toLowerCase();
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

    if (shown.empty() && items.empty())
    {
        // THE FIRST LAUNCH is where the product introduces itself (DINE Identity v1): the
        // wordmark in Paper, the line under it in Steel, then what to do - centred in the room
        // the list will take.
        auto area = viewport.getBounds().withTrimmedTop (40).withHeight (40 + 18 + 18 + 30 + 20 + 6 + 34);
        Dine::drawWordmark (g, area.removeFromTop (40).toFloat(), Dine::brandPaper);
        area.removeFromTop (18);
        g.setColour (Dine::brandSteel);
        g.setFont (Dine::text (14.0f));
        Dine::drawText (g, AboutSheet::kTagline, area.removeFromTop (18), juce::Justification::centred, false);
        area.removeFromTop (30);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (14.0f, 600));
        Dine::drawText (g, "No sessions saved yet", area.removeFromTop (20), juce::Justification::centred);
        area.removeFromTop (6);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        Dine::drawFittedText (g, "Start a new session and it is saved into ~/Music/DINE as you work.",
                              area.removeFromTop (34).withSizeKeepingCentre (juce::jmin (620, area.getWidth()), 34),
                              juce::Justification::centredTop, 2);
        return;
    }
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
    addAndMakeVisible (midiSetupButton);
    midiSetupButton.setTooltip ("macOS's own audio settings: build an aggregate device, or set a device's clock and rate.");
    midiSetupButton.onClick = [this] { services.openAudioMidiSetup(); };
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
        auto line = outs.removeFromTop (Dine::Metric::button);
        outputsButton.setBounds (line.removeFromLeft (w));
        line.removeFromLeft (8);
        const int mw = midiSetupButton.idealWidth();
        midiSetupButton.setVisible (mw <= line.getWidth());
        midiSetupButton.setBounds (line.removeFromLeft (juce::jmin (mw, line.getWidth())));
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

namespace
{
    // The gain verdict in the v4 chip's words - the Mixer's chip says the same: "Healthy",
    // "Clipping -6", "Digital +12", "Low +6". The number is what the preamp should still move.
    juce::String gainChipText (const MixController::InputAdvice& a)
    {
        using Level = MixController::InputAdvice::Level;
        juce::String word;
        switch (a.level)
        {
            case Level::Clipping: word = "Clipping"; break;
            case Level::Faint:    word = "Faint"; break;
            case Level::Low:      word = "Low"; break;
            case Level::Hot:      word = "Hot"; break;
            case Level::Digital:  word = "Digital"; break;
            case Level::NotHeard: return "Not heard";
            case Level::Healthy:  return "Healthy";
            case Level::Bleed:    return "Spill";
            default:              return {};
        }
        const int move = juce::roundToInt (a.consoleMoveDb);
        if (move == 0) return word;
        return word + " " + (move > 0 ? juce::String ("+") : Glyph::minus()) + juce::String (std::abs (move));
    }
}

class AssignPage::Row : public juce::Component, private juce::KeyListener
{
public:
    void lookAndFeelChanged() override { Dine::styleTextEditor (name, Dine::control); }

    // The name cell's keys (v4 fast entry). Return and down go to the next input's name, up to
    // the one before, Tab to what this one is (Shift-Tab to the name before), Esc puts the name
    // back as it was, Cmd-D makes it the same as the input above.
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            page.entries[size_t (input)].name = nameBefore;
            name.setText (nameBefore, false);
            name.giveAwayKeyboardFocus();
            return true;
        }
        if (key == juce::KeyPress::downKey || key == juce::KeyPress::returnKey)
        {
            page.commit();
            page.moveFrom (input, 1, false);
            return true;
        }
        if (key == juce::KeyPress::upKey) { page.commit(); page.moveFrom (input, -1, false); return true; }
        if (key.getKeyCode() == juce::KeyPress::tabKey)
        {
            page.commit();
            if (key.getModifiers().isShiftDown()) page.moveFrom (input, -1, true);
            else page.openRoleEditor (input);
            return true;
        }
        if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0)) { page.sameAsAbove (input); return true; }
        return false;
    }

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
        name.onTextChange = [this]
        {
            // A list pasted into one name fills the column down from here.
            const auto text = name.getText();
            if (text.containsChar ('\n') || text.containsChar ('\r'))
            {
                name.setText (nameBefore, false);
                page.fillDown (input, text);
                return;
            }
            page.entries[size_t (input)].name = text;
        };
        name.onFocusLost = [this] { page.commit(); };
        name.addKeyListener (this);
        name.setMultiLine (false);
        name.setTabKeyUsedAsCharacter (false);

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
        nameBefore = e.name;
        // An input not used offers itself: "Use it" opens the same list of what it could be.
        source.setValue (e.assigned ? friendlyRoleName (e.role) : juce::String ("Use it"));
        link.setToggleState (e.linkedToNext, juce::dontSendNotification);
        // Linking is offered where it can happen: this input is something, the next input
        // exists, and it is not already the right half of somebody else's pair.
        link.setVisible (e.assigned && input + 1 < int (page.entries.size())
                         && (e.linkedToNext || ! page.entries[size_t (input) + 1].linkedFromPrevious));
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // v4: a click shows the input in the right panel, Cmd-click adds it to the selection,
        // Shift-click selects the range. The controls on the row keep their own clicks.
        if (e.mods.isCommandDown())    page.toggleSelection (input, false);
        else if (e.mods.isShiftDown()) page.toggleSelection (input, true);
        else                           page.pickInput (input);
        page.grabKeyboardFocus();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (const int s = page.stripOf (input); s >= 0 && page.onOpenInspector) page.onOpenInspector (s);
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
        if (input == page.picked)     Dine::hairlineRounded (g, b.toFloat().reduced (0.5f), Dine::hairStrong, Dine::Radius::control);

        const auto c = AssignPage::colsFor (getWidth());
        auto r = b.reduced (10, 0);
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.0f, 500));
        Dine::drawText (g, e.linkedToNext ? juce::String (input + 1) + Glyph::minus() + juce::String (input + 2) : juce::String (input + 1),
                        r.removeFromLeft (c.num), juce::Justification::centredLeft);

        // THE SIGNAL COLUMN, AND WHAT TO DO ABOUT IT AT THE DESK.
        //
        // The needle is where it is at this instant; the gain stage is about the loudest moment
        // of a service, so the bar is the held peak and the verdict is read from it. The move
        // is named in decibels because the preamp it is about has a number on it.
        const float db = page.services.isAudioRunning() ? page.services.daw().inputPeakDb (input) : -120.0f;
        auto column = r.removeFromRight (c.meter + c.verdict);
        auto verdict = column.removeFromRight (c.verdict);
        auto meter = column.withTrimmedRight (c.gap).withSizeKeepingCentre (juce::jmax (0, column.getWidth() - c.gap), 4);
        if (! meter.isEmpty()) Dine::fillMeter (g, meter.toFloat(), DineMeter::norm (e.peakHoldDb), false, ! e.assigned, 2.0f);
        // Where it is right now, as a mark on the held bar: the hold is the decision, the mark
        // is the reassurance that something is still arriving.
        if (db > -70.0f && ! meter.isEmpty())
        {
            const float x = meter.getX() + meter.getWidth() * DineMeter::norm (db);
            g.setColour (Dine::ink.withAlpha (0.7f));
            g.fillRect (x - 0.5f, float (meter.getY()) - 2.0f, 1.0f, float (meter.getHeight()) + 4.0f);
        }
        if (e.assigned && ! e.linkedFromPrevious)
        {
            // v4's chip words: "Healthy", "Clipping -6" - the same as the Mixer's chip.
            const auto advice = page.controller.liveCaptureAdvice (e.role, e.peakHoldDb);
            const auto text = gainChipText (advice);
            const auto colour = gainVerdictColour (advice.level);
            const auto font = Dine::text (11.5f, 600);
            if (advice.level == MixController::InputAdvice::Level::Healthy)
            {
                g.setColour (Dine::ink3);
                g.setFont (font);
                Dine::drawText (g, text, verdict, juce::Justification::centredLeft, true);
            }
            else if (text.isNotEmpty())
            {
                const int w = juce::jmin (verdict.getWidth(), Dine::textWidth (font, text) + 16);
                Dine::drawStatusChip (g, verdict.withWidth (w).withSizeKeepingCentre (w, 20).toFloat(), text, colour);
            }
        }
        r.removeFromRight (c.gap);
        r.removeFromRight (pairWidth() + c.gap);

        // the group, in its colour, with a lamp before it
        auto busCell = r.removeFromRight (c.bus);
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
    int pairWidth() const { return juce::jmax (AssignPage::colsFor (getWidth()).pair, link.idealWidth()); }

    int sourceWidth() const
    {
        const auto c = AssignPage::colsFor (getWidth());
        auto r = getLocalBounds().reduced (10, 0);
        const int taken = c.num + c.name + 22 + (c.gap - 6) + c.bus + pairWidth() + c.meter + c.verdict + 3 * c.gap;
        return juce::jmax (120, r.getWidth() - taken);
    }

    void resized() override
    {
        const auto c = AssignPage::colsFor (getWidth());
        auto r = getLocalBounds().withTrimmedBottom (1).reduced (10, 0);
        r.removeFromLeft (c.num);
        name.setBounds (r.removeFromLeft (c.name).withSizeKeepingCentre (c.name, 30));
        suggest.setBounds (r.removeFromLeft (22).withSizeKeepingCentre (20, 20));
        r.removeFromLeft (c.gap - 6);
        const int sw = sourceWidth();
        source.setBounds (r.removeFromLeft (sw).withSizeKeepingCentre (sw, 30));
        r.removeFromRight (c.meter + c.verdict + c.gap);
        const int pw = pairWidth();
        link.setBounds (r.removeFromRight (pw).withSizeKeepingCentre (pw, 20));
    }


    AssignPage& page;
    int input;
    juce::String nameBefore;            // what Esc puts back
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


// THE RIGHT PANEL (v4): everything about the one input picked, without a dialog. Which port
// it is and the arrows that step through them; its name with suggestions; what is arriving,
// the held peak and what to do at the desk; what it is, which group it feeds, its pair and
// its port; set to record, flip polarity, listen in headphones; TUNE CHANNEL, the Inspector
// and Not used.
class AssignPage::Detail : public juce::Component
{
public:
    explicit Detail (AssignPage& p) : page (p)
    {
        for (auto* b : { &prev, &next }) { b->setFontPx (12.0f); b->setPadX (8); addAndMakeVisible (*b); }
        prev.setTooltip ("The input before this one (up arrow, with no cell being typed in)");
        next.setTooltip ("The next input (down arrow)");
        prev.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };

        name.setFont (Dine::text (15.0f, 600));
        name.setIndents (10, 0);
        name.setBorder (juce::BorderSize<int> (0));
        name.setJustification (juce::Justification::centredLeft);
        name.setTextToShowWhenEmpty ("Untitled", Dine::ink4);
        name.onReturnKey = [this] { commitName(); };
        name.onFocusLost = [this] { commitName(); };
        name.onEscapeKey = [this] { load(); page.grabKeyboardFocus(); };
        addAndMakeVisible (name);
        suggest.setIcon (Dine::Icon::UpDown);
        suggest.setPadX (6);
        suggest.setTooltip ("Names DINE can suggest: the role, the desk's number, the clip.");
        suggest.onClick = [this] { if (input >= 0) page.showNameMenu (input, suggest); };
        addAndMakeVisible (suggest);

        checkButton.setFontPx (12.0f);
        checkButton.setTooltip ("Forget the loudest so far, so the next hit is the reading. Move the preamp, play, read again.");
        checkButton.onClick = [this] { page.checkAgain(); };
        addAndMakeVisible (checkButton);

        what.onClick = [this] { if (input >= 0) page.showSourceMenu (input, what); };
        addAndMakeVisible (what);
        pair.onClick = [this] { togglePair(); };
        pair.setTooltip ("Mono, or this input and the next as one stereo pair.");
        addAndMakeVisible (pair);

        for (auto* t : { &record, &polarity, &listen }) { t->setClickingTogglesState (false); addAndMakeVisible (*t); }
        record.onClick = [this] { toggleRecord(); };
        polarity.onClick = [this] { togglePolarity(); };
        listen.onClick = [this] { toggleListen(); };

        tune.setFontPx (12.0f);
        tune.setCaps (true);
        tune.onClick = [this] { if (const int s = page.stripOf (input); s >= 0 && page.onTuneChannel) page.onTuneChannel (s); };
        inspect.setFontPx (12.0f);
        inspect.onClick = [this] { if (const int s = page.stripOf (input); s >= 0 && page.onOpenInspector) page.onOpenInspector (s); };
        notUsed.setFontPx (12.0f);
        notUsed.setTint (Dine::crit);
        notUsed.setTooltip ("This input is not part of the session: no track, no strip. Use it again from What it is.");
        notUsed.onClick = [this]
        {
            if (input < 0) return;
            page.setRole (input, page.entries[size_t (input)].role, false);
            page.commit();
            page.rebuild();
            load();
        };
        for (auto* b : { &tune, &inspect, &notUsed }) addAndMakeVisible (*b);
    }

    void show (int which) { input = which; load(); }

    // Everything but the meter, from the page's entries and the session.
    void load()
    {
        const bool any = input >= 0 && input < int (page.entries.size());
        setVisible (true);
        for (auto* c : std::initializer_list<juce::Component*> { &name, &suggest, &what, &pair, &record, &polarity, &listen,
                                                                 &tune, &inspect, &notUsed, &prev, &next, &checkButton })
            c->setVisible (any);
        if (! any) { repaint(); return; }
        const auto& e = page.entries[size_t (input)];
        if (! name.hasKeyboardFocus (true)) name.setText (e.name, false);
        what.setValue (e.assigned ? friendlyRoleName (e.role) : juce::String ("Not used"));
        pair.setValue (e.linkedToNext ? "With " + juce::String (input + 2) : juce::String ("Mono"));
        pair.setEnabled (e.assigned && input + 1 < int (page.entries.size()));
        const int strip = page.stripOf (input), track = page.sessionIndexOf (input);
        const auto& project = page.services.daw().getProject();
        record.setToggleState (track >= 0 && track < int (project.tracks.size()) && project.tracks[size_t (track)].armed, juce::dontSendNotification);
        const auto& base = page.controller.getBase();
        const bool hasStrip = strip >= 0 && strip < base.numStrips;
        polarity.setToggleState (hasStrip && base.strips[size_t (strip)].channel.polarityInvert, juce::dontSendNotification);
        listen.setToggleState (hasStrip && base.strips[size_t (strip)].solo, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &record, &polarity, &listen, &tune, &inspect })
            c->setEnabled (hasStrip && e.assigned);
        record.setEnabled (track >= 0);
        notUsed.setEnabled (e.assigned);
        prev.setEnabled (page.neighbour (input, -1) >= 0);
        next.setEnabled (page.neighbour (input, 1) >= 0);
        resized();
        repaint();
    }

    void tick() { if (isVisible() && input >= 0) repaint (meterArea.expanded (2, 24)); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 14);
        auto head = r.removeFromTop (30);
        next.setBounds (head.removeFromRight (30).withSizeKeepingCentre (30, 26));
        head.removeFromRight (6);
        prev.setBounds (head.removeFromRight (30).withSizeKeepingCentre (30, 26));
        headArea = head;
        r.removeFromTop (8);
        auto nameRow = r.removeFromTop (36);
        suggest.setBounds (nameRow.removeFromRight (36));
        nameRow.removeFromRight (6);
        name.setBounds (nameRow);
        r.removeFromTop (12);

        // the arriving card: caption + dBFS + chip, the meter, the sentence, Check again
        arriving = r.removeFromTop (juce::jmin (r.getHeight(), 168));
        auto a = arriving.reduced (12, 10);
        a.removeFromTop (20);
        meterArea = a.removeFromTop (10);
        a.removeFromTop (16);
        auto checkRow = a.removeFromBottom (Dine::Metric::button);
        checkButton.setBounds (checkRow.withWidth (juce::jmin (checkRow.getWidth(), checkButton.idealWidth() + 8)));
        adviceArea = a.withTrimmedBottom (6);
        r.removeFromTop (14);

        auto field = [&r] (juce::Rectangle<int>& caption, juce::Component* c)
        {
            auto row = r.removeFromTop (32);
            caption = row.removeFromLeft (96);
            if (c != nullptr) c->setBounds (row.withSizeKeepingCentre (row.getWidth(), 30));
            r.removeFromTop (6);
        };
        field (whatCap, &what);
        field (feedsCap, nullptr);
        feedsArea = feedsCap.withX (feedsCap.getRight()).withWidth (r.getWidth() - 96);
        field (pairCap, &pair);
        field (sourceCap, nullptr);
        sourceArea = sourceCap.withX (sourceCap.getRight()).withWidth (r.getWidth() - 96);
        r.removeFromTop (8);
        auto toggle = [&r] (juce::Rectangle<int>& text, juce::Component& sw)
        {
            auto row = r.removeFromTop (38);
            sw.setBounds (row.removeFromRight (52).withSizeKeepingCentre (44, 24));
            text = row;
        };
        toggle (recordText, record);
        toggle (polarityText, polarity);
        toggle (listenText, listen);
        r.removeFromTop (12);
        auto verbs = r.removeFromTop (Dine::Metric::button);
        const int tw = juce::jmax (110, tune.idealWidth());
        tune.setBounds (verbs.removeFromLeft (juce::jmin (tw, verbs.getWidth())));
        verbs.removeFromLeft (8);
        inspect.setBounds (verbs.withWidth (juce::jmin (verbs.getWidth(), inspect.idealWidth() + 8)));
        r.removeFromTop (10);
        notUsed.setBounds (r.removeFromTop (Dine::Metric::button).withWidth (juce::jmax (84, notUsed.idealWidth())));
    }

    void paint (juce::Graphics& g) override
    {
        Dine::fillRounded (g, getLocalBounds().toFloat(), Dine::card, 12.0f);
        if (input < 0 || input >= int (page.entries.size()))
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            Dine::drawFittedText (g, "Click an input to see everything about it here.", getLocalBounds().reduced (16).removeFromTop (40),
                                  juce::Justification::topLeft, 2, 1.0f);
            return;
        }
        const auto& e = page.entries[size_t (input)];
        {
            auto h = headArea;
            g.setColour (Dine::ink);
            g.setFont (Dine::text (12.0f, 600));
            const juce::String title = "Input " + juce::String (input + 1).paddedLeft ('0', 2);
            Dine::drawText (g, title, h.removeFromTop (16), juce::Justification::centredLeft, false);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f, 500));
            const auto device = page.services.deviceState().input;
            Dine::drawFittedText (g, device.isNotEmpty() ? device : juce::String ("No device open"), h, juce::Justification::centredLeft, 1, 0.85f);
        }

        // ARRIVING
        Dine::fillRounded (g, arriving.toFloat(), Dine::well, 10.0f);
        {
            auto a = arriving.reduced (12, 10);
            auto top = a.removeFromTop (20);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f, 600));
            Dine::drawText (g, "Arriving", top.removeFromLeft (70), juce::Justification::centredLeft, false);
            const auto advice = page.controller.liveCaptureAdvice (e.role, e.peakHoldDb);
            const auto chip = e.assigned ? gainChipText (advice) : juce::String();
            if (chip.isNotEmpty())
            {
                const int cw = Dine::textWidth (Dine::text (11.0f, 600), chip) + 16;
                Dine::drawStatusChip (g, top.removeFromRight (cw).withSizeKeepingCentre (cw, 18).toFloat(), chip, gainVerdictColour (advice.level));
                top.removeFromRight (8);
            }
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (12.0f, 500));
            const juce::String level = e.peakHoldDb > -100.0f ? juce::String (e.peakHoldDb, 1).replace ("-", Glyph::minus()) + " dBFS"
                                                              : juce::String ("Nothing yet");
            Dine::drawText (g, level, top, juce::Justification::centredRight, false);

            // the bar is the held peak; the tick is where it is right now
            Dine::fillMeter (g, meterArea.toFloat(), DineMeter::norm (e.peakHoldDb), false, ! e.assigned, 3.0f);
            const float now = page.services.isAudioRunning() ? page.services.daw().inputPeakDb (input) : -120.0f;
            if (now > -70.0f)
            {
                const float x = meterArea.getX() + meterArea.getWidth() * DineMeter::norm (now);
                g.setColour (Dine::ink.withAlpha (0.75f));
                g.fillRect (x - 0.5f, float (meterArea.getY()) - 3.0f, 1.0f, float (meterArea.getHeight()) + 6.0f);
            }
            g.setColour (Dine::ink4);
            g.setFont (Dine::mono (9.5f, 500));
            for (const float mark : { -60.0f, -20.0f, -10.0f, 0.0f })
            {
                const int x = meterArea.getX() + juce::roundToInt (meterArea.getWidth() * DineMeter::norm (mark));
                const juce::String t = mark == 0.0f ? juce::String ("0") : Glyph::minus() + juce::String (int (-mark));
                const auto cell = juce::Rectangle<int> (x - 16, meterArea.getBottom() + 2, 32, 12)
                                      .constrainedWithin (meterArea.withHeight (12).withY (meterArea.getBottom() + 2));
                Dine::drawText (g, t, cell, mark == 0.0f ? juce::Justification::centredRight
                                          : mark == -60.0f ? juce::Justification::centredLeft : juce::Justification::centred, false);
            }
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.0f));
            const juce::String say = ! e.assigned ? juce::String ("Not used. Pick what it is to use it.")
                                   : juce::String (advice.detail).isNotEmpty() ? juce::String (advice.detail)
                                   : juce::String ("Play it, and the loudest moment says whether the preamp is right.");
            Dine::drawFittedText (g, say, adviceArea, juce::Justification::topLeft, juce::jmax (1, adviceArea.getHeight() / 15), 1.0f);
        }

        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f, 500));
        for (auto* cap : { &whatCap, &feedsCap, &pairCap, &sourceCap })
            Dine::drawText (g, cap == &whatCap ? "What it is" : cap == &feedsCap ? "Feeds" : cap == &pairCap ? "Pair" : "Source",
                            *cap, juce::Justification::centredLeft, false);
        {
            auto f = feedsArea;
            const auto bus = mixBusForRole (e.role);
            g.setColour (e.assigned ? busColour (bus) : Dine::ink4);
            g.fillEllipse (f.removeFromLeft (8).withSizeKeepingCentre (7, 7).toFloat());
            f.removeFromLeft (8);
            g.setColour (e.assigned ? Dine::ink : Dine::ink3);
            g.setFont (Dine::text (13.0f));
            Dine::drawText (g, e.assigned ? juce::String (busLabel (bus)) : juce::String ("Nothing"), f, juce::Justification::centredLeft, true);
        }
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f));
        Dine::drawText (g, "Input " + juce::String (input + 1) + (e.linkedToNext ? " and " + juce::String (input + 2) : juce::String()),
                        sourceArea, juce::Justification::centredLeft, true);

        auto toggleText = [&g] (juce::Rectangle<int> r, const juce::String& title, const juce::String& line)
        {
            g.setColour (Dine::ink);
            g.setFont (Dine::text (13.0f, 500));
            Dine::drawText (g, title, r.removeFromTop (19), juce::Justification::bottomLeft, false);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f));
            Dine::drawFittedText (g, line, r, juce::Justification::topLeft, 1, 0.85f);
        };
        toggleText (recordText, "Record it", "Goes to its own track on disk");
        toggleText (polarityText, "Flip polarity", "For a mic facing the other way");
        toggleText (listenText, "Listen in headphones", "Only you hear it");
    }

private:
    void step (int delta)
    {
        if (const int n = page.neighbour (input, delta); n >= 0) page.pickInput (n);
    }
    void commitName()
    {
        if (input < 0) return;
        const auto t = name.getText().trim();
        if (t == page.entries[size_t (input)].name) return;
        page.entries[size_t (input)].name = t;
        page.commit();
        page.rebuild();
    }
    void togglePair()
    {
        if (input < 0 || input + 1 >= int (page.entries.size())) return;
        auto& e = page.entries[size_t (input)];
        e.linkedToNext = ! e.linkedToNext;
        page.entries[size_t (input) + 1].linkedFromPrevious = e.linkedToNext;
        page.commit();
        page.rebuild();
        load();
    }
    void toggleRecord()
    {
        const int track = page.sessionIndexOf (input);
        auto& daw = page.services.daw();
        auto& project = daw.getProject();
        if (track < 0 || track >= int (project.tracks.size())) return;
        if (daw.isRecording()) { if (page.onToast) page.onToast ("Recording is running. Stop it first, then choose what records."); return; }
        project.tracks[size_t (track)].armed = ! project.tracks[size_t (track)].armed;
        daw.refresh();
        page.services.touchSession();
        load();
    }
    void togglePolarity()
    {
        const int strip = page.stripOf (input);
        if (strip < 0 || strip >= page.controller.getBase().numStrips) return;
        auto ch = page.controller.getBase().strips[size_t (strip)].channel;
        ch.polarityInvert = ! ch.polarityInvert;
        page.controller.setStripChannel (strip, ch);
        load();
    }
    void toggleListen()
    {
        const int strip = page.stripOf (input);
        if (strip < 0 || strip >= page.controller.getBase().numStrips) return;
        page.controller.setStripSolo (strip, ! page.controller.getBase().strips[size_t (strip)].solo);
        load();
    }

    AssignPage& page;
    int input = -1;
    DineButton prev { juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x91")), DineButton::Style::Standard };
    DineButton next { juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x93")), DineButton::Style::Standard };
    juce::TextEditor name;
    DineButton suggest { "", DineButton::Style::Standard };
    DineButton checkButton { juce::String (juce::CharPointer_UTF8 ("I\xe2\x80\x99ve changed it, check again")), DineButton::Style::Filled };
    DinePopup what, pair;
    DineSwitch record { "", "" }, polarity { "", "" }, listen { "", "" };
    DineButton tune { "Tune channel", DineButton::Style::Filled };
    DineButton inspect { "Open in Inspector", DineButton::Style::Standard };
    DineButton notUsed { "Not used", DineButton::Style::Ghost };
    juce::Rectangle<int> headArea, arriving, meterArea, adviceArea, whatCap, feedsCap, feedsArea, pairCap, sourceCap, sourceArea,
                         recordText, polarityText, listenText;
};

// CMD-V OUTSIDE A CELL (v4): the list on the clipboard, where it will land before it lands -
// "14 names -> inputs 05-18" - and Fill names to do it. A name per line; a tab-separated
// second column says what each is, as a list pasted into a name cell does.
class AssignPage::PastePanel : public juce::Component
{
public:
    explicit PastePanel (AssignPage& p) : page (p)
    {
        text.setMultiLine (true, false);
        text.setReturnKeyStartsNewLine (true);
        text.setFont (Dine::mono (12.5f, 500));
        Dine::styleTextEditor (text, Dine::control);
        text.onTextChange = [this] { repaint(); };
        addAndMakeVisible (text);
        start.onClick = [this] { pickStart(); };
        start.setTooltip ("The input the first name goes to. The rest follow down the list as it is shown.");
        addAndMakeVisible (start);
        cancel.setFontPx (12.0f);
        fill.setFontPx (12.0f);
        cancel.onClick = [this] { page.paste.reset(); page.grabKeyboardFocus(); };
        fill.onClick = [this] { page.applyPaste(); };
        addAndMakeVisible (cancel);
        addAndMakeVisible (fill);
    }

    void set (const juce::String& t, int from) { text.setText (t, false); first = from; update(); }
    int lineCount() const
    {
        juce::StringArray lines;
        lines.addLines (text.getText());
        lines.removeEmptyStrings (true);
        return lines.size();
    }
    juce::String preview() const
    {
        const int n = lineCount();
        if (n == 0 || first < 0) return "Nothing to paste yet: one name per line.";
        const auto targets = page.fillTargets (first, n);
        if (targets.empty()) return "There is no input there to fill.";
        const auto arrow = juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 "));
        const auto dash = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
        juce::String s = juce::String (int (targets.size())) + (targets.size() == 1 ? " name" : " names") + arrow
                       + (targets.size() == 1 ? "input " + juce::String (targets.front() + 1).paddedLeft ('0', 2)
                                              : "inputs " + juce::String (targets.front() + 1).paddedLeft ('0', 2) + dash
                                                    + juce::String (targets.back() + 1).paddedLeft ('0', 2));
        if (int (targets.size()) < n) s += ". " + juce::String (n - int (targets.size())) + " more than there are inputs left";
        return s;
    }
    const juce::String getText() const { return text.getText(); }
    int startInput() const noexcept { return first; }

    void update()
    {
        start.setValue ("Input " + juce::String (first + 1).paddedLeft ('0', 2));
        fill.setEnabled (lineCount() > 0 && first >= 0);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Dine::desk.withAlpha (0.7f));
        Dine::drawSheet (g, card().toFloat(), 14.0f);
        auto r = card().reduced (20, 16);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (15.0f, 700));
        Dine::drawText (g, "Paste a list of names", r.removeFromTop (22), juce::Justification::centredLeft, false);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawText (g, "One name per line. A tab and a second column says what each one is.", r.removeFromTop (18),
                        juce::Justification::centredLeft, true);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f, 500));
        Dine::drawText (g, "Starting at", startCaption, juce::Justification::centredLeft, false);
        g.setColour (Dine::accent);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawFittedText (g, preview(), previewArea, juce::Justification::centredLeft, 1, 0.85f);
    }

    void resized() override
    {
        auto r = card().reduced (20, 16);
        r.removeFromTop (22 + 18 + 12);
        auto buttons = r.removeFromBottom (Dine::Metric::button);
        fill.setBounds (buttons.removeFromRight (juce::jmax (96, fill.idealWidth())));
        buttons.removeFromRight (8);
        cancel.setBounds (buttons.removeFromRight (juce::jmax (76, cancel.idealWidth())));
        r.removeFromBottom (12);
        previewArea = r.removeFromBottom (20);
        r.removeFromBottom (8);
        auto startRow = r.removeFromBottom (30);
        startCaption = startRow.removeFromLeft (84);
        start.setBounds (startRow.withWidth (juce::jmin (startRow.getWidth(), 140)));
        r.removeFromBottom (12);
        text.setBounds (r);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! card().contains (e.getPosition())) { page.paste.reset(); page.grabKeyboardFocus(); }
    }

private:
    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre (juce::jmin (520, getWidth() - 40), juce::jmin (420, getHeight() - 40)); }
    void pickStart()
    {
        juce::PopupMenu m;
        for (int i : page.visibleOrder())
            m.addItem (1 + i, "Input " + juce::String (i + 1).paddedLeft ('0', 2) + "   " + page.entries[size_t (i)].name, true, i == first);
        juce::Component::SafePointer<PastePanel> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&start), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            safe->first = r - 1;
            safe->update();
        });
    }

    AssignPage& page;
    juce::TextEditor text;
    DinePopup start;
    DineButton cancel { "Cancel", DineButton::Style::Standard };
    DineButton fill { "Fill names", DineButton::Style::Filled };
    juce::Rectangle<int> startCaption, previewArea;
    int first = -1;
};

AssignPage::AssignPage (MixController& c, AppServices& s) : controller (c), services (s)
{
    // The typeahead over a row's "what it is" cell (v4 fast entry). One editor for the whole
    // table, moved to the row being typed in.
    listHolder.addChildComponent (roleEditor);
    listHolder.addChildComponent (roleHint);
    roleEditor.setFont (Dine::text (13.0f));
    roleEditor.setIndents (10, 0);
    roleEditor.setBorder (juce::BorderSize<int> (0));
    roleEditor.setJustification (juce::Justification::centredLeft);
    Dine::styleTextEditor (roleEditor, Dine::controlOn);
    roleHint.setFont (Dine::text (12.0f));
    roleHint.setColour (juce::Label::textColourId, Dine::accent);
    roleHint.setJustificationType (juce::Justification::centredRight);
    roleHint.setInterceptsMouseClicks (false, false);
    roleEditor.onTextChange = [this]
    {
        ChannelRole role {};
        roleHint.setText (roleFor (roleEditor.getText(), role) ? friendlyRoleName (role) : juce::String(), juce::dontSendNotification);
    };
    roleEditor.onEscapeKey = [this] { closeRoleEditor (false); };
    roleEditor.onFocusLost = [this] { closeRoleEditor (true); };
    roleKeys.owner = this;
    roleEditor.addKeyListener (&roleKeys);
    roleEditor.setTabKeyUsedAsCharacter (false);

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
    // v4's group chips: All, each group in its colour, Not used. chips[0] is All, chips.back()
    // Not used, the ones between are the groups in the order a person reads them.
    chips.push_back (std::make_unique<DineChip> ("All"));
    chips.back()->onClick = [this] { setBusFilter (-2); };
    for (int t = 0; t < int (MixBus::Master); ++t)
    {
        const auto b = mixBusInDisplayOrder (t);
        chips.push_back (std::make_unique<DineChip> (busLabel (b), busColour (b)));
        chips.back()->onClick = [this, b] { setBusFilter (busFilter == int (b) ? -2 : int (b)); };
    }
    chips.push_back (std::make_unique<DineChip> ("Not used", Dine::ink4));
    chips.back()->onClick = [this] { setBusFilter (busFilter == -1 ? -2 : -1); };
    for (auto& c : chips) addAndMakeVisible (*c);
    search.setTextToShowWhenEmpty ("Find an input", Dine::ink4);

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

    // ---- v4: the right panel, the selection's own verbs, the patch menu, the preamp banner
    detail = std::make_unique<Detail> (*this);
    addAndMakeVisible (*detail);
    numberButton.setTooltip ("Name the picked-out inputs in order: BV 1, BV 2, BV 3.");
    numberButton.onClick = [this] { numberSelection(); };
    nameSelButton.setTooltip ("The picked-out inputs take the short name of what they are.");
    nameSelButton.onClick = [this]
    {
        for (int i : selectedInputs()) if (entries[size_t (i)].assigned) entries[size_t (i)].name = shortRoleName (entries[size_t (i)].role);
        commit();
        rebuild();
    };
    pairSelButton.setTooltip ("Pair each picked-out input with the next one as a stereo row.");
    pairSelButton.onClick = [this] { linkSelection(); };
    patchButton.setIcon (Dine::Icon::UpDown);
    patchButton.setTooltip ("Save this patch, or apply a saved one.");
    patchButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Save this patch" + juce::String (Glyph::ellip()));
        m.addItem (2, "Apply a saved patch" + juce::String (Glyph::ellip()));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patchButton), [this] (int r)
        {
            if (r == 1 && onSaveMapping) onSaveMapping();
            if (r == 2 && onApplyMapping) onApplyMapping();
        });
    };
    quickButton.setIcon (Dine::Icon::UpDown);
    checkAgainButton.setFontPx (12.0f);
    checkAgainButton.setTooltip ("Forget the loudest so far on every input, so the next hit is the reading.");
    checkAgainButton.onClick = [this] { checkAgain(); };
    for (auto* b : { &numberButton, &nameSelButton, &pairSelButton, &patchButton, &checkAgainButton }) addChildComponent (*b);
    setWantsKeyboardFocus (true);

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
    if (detail != nullptr) detail->tick();
    // The banner comes and goes with what the preamps need, and moves the table when it does.
    if ((inputsNeedingGain() > 0) == bannerArea.isEmpty()) resized();
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
    chips.front()->setToggleState (busFilter == -2, juce::dontSendNotification);
    chips.back()->setToggleState (busFilter == -1, juce::dontSendNotification);
    for (int t = 0; t < int (MixBus::Master); ++t)
        chips[size_t (1 + t)]->setToggleState (busFilter == int (mixBusInDisplayOrder (t)), juce::dontSendNotification);
    if (picked >= numInputs) picked = -1;
    // The right panel always has an input in it (v4): the first one shown, until one is picked.
    if (picked < 0 && ! rows.empty()) picked = rows.front()->input;
    if (detail != nullptr) detail->show (picked);
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
    // v4: the whole desk's three (Use desk labels, Quick actions, Patch) until inputs are
    // picked out; then the selection's own verbs. The named whole-desk actions live in Quick
    // actions; the group chips, search and the list button stay in both states.
    const bool any = selectionCount() > 0;
    for (auto* b : { &bulkButton, &kitButton, &nameSelButton, &pairSelButton, &numberButton, &dropButton, &deselectButton })
        b->setVisible (any);
    for (auto* b : { &deskLabelsButton, &quickButton, &patchButton })
        b->setVisible (! any);
    for (auto* b : { &nameButton, &linkButton, &selectAllButton, &patchSaveButton, &patchApplyButton })
        b->setVisible (false);
    search.setVisible (true);
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
    // What this page does not edit travels with the input: the pinned focal source, and what
    // the microphone was on the other side of singing / speaking.
    for (auto& a : s.inputs)
        for (const auto& b : before)
            if (b.inputA == a.inputA) { a.focus = b.focus; a.otherVoiceRole = b.otherVoiceRole; break; }
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

// What it is, chosen from the menu or typed: the role, and - for a source that comes in a
// pair (overheads, keys, a drum bus, a pad) - the next input linked as its other side when
// that input is free.
void AssignPage::chooseRole (int input, ChannelRole role)
{
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

// ---------------------------------------------------------------- fast entry (v4)
// A desk of 64 inputs is named from the keyboard, not with the mouse: Return / down / up move
// through the name column, Tab goes on to what it is (a typeahead that knows the desk's
// shorthand - bv, oh, hh, vox, keys, amb), a list pasted into a name fills down, and Cmd-D
// makes an input the same as the one above it.
int AssignPage::rowIndexOf (int input) const
{
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i]->input == input) return int (i);
    return -1;
}

void AssignPage::focusName (int input)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<AssignPage> (this), input]
    {
        if (safe == nullptr) return;
        const int r = safe->rowIndexOf (input);
        if (r < 0) return;
        auto& row = *safe->rows[size_t (r)];
        safe->viewport.setViewPosition (0, juce::jlimit (0, juce::jmax (0, safe->listHolder.getHeight() - safe->viewport.getHeight()),
                                                         juce::jmin (safe->viewport.getViewPositionY(), row.getY() - 8)
                                                         + juce::jmax (0, row.getBottom() + 8 - (safe->viewport.getViewPositionY() + safe->viewport.getHeight()))));
        row.name.grabKeyboardFocus();
    });
}

void AssignPage::moveFrom (int input, int delta, bool toRole)
{
    const int r = rowIndexOf (input);
    if (r < 0) return;
    const int next = r + delta;
    if (next < 0 || next >= int (rows.size())) return;
    if (toRole) openRoleEditor (rows[size_t (next)]->input);
    else        focusName (rows[size_t (next)]->input);
}

// The best role for what has been typed: a role whose name starts with it, then one whose name
// contains it, then the desk shorthand StemNames knows.
bool AssignPage::roleFor (const juce::String& typed, ChannelRole& role)
{
    const auto t = typed.trim().toLowerCase();
    if (t.isEmpty()) return false;
    const ChannelRole* contains = nullptr;
    ChannelRole found {};
    for (const auto& group : Dine::roleGroups())
        for (auto r : group.roles)
        {
            const auto n = friendlyRoleName (r).toLowerCase();
            if (n.startsWith (t)) { role = r; return true; }
            if (contains == nullptr && n.contains (t)) { found = r; contains = &found; }
        }
    if (contains != nullptr) { role = *contains; return true; }
    return StemNames::guessRole (typed, role);
}

void AssignPage::openRoleEditor (int input)
{
    const int r = rowIndexOf (input);
    if (r < 0) return;
    roleInput = input;
    auto& row = *rows[size_t (r)];
    const auto cell = row.source.getBounds().translated (row.getX(), row.getY());
    roleEditor.setBounds (cell);
    roleEditor.setText ({}, false);
    roleEditor.setTextToShowWhenEmpty (entries[size_t (input)].assigned ? friendlyRoleName (entries[size_t (input)].role)
                                                                        : juce::String ("Type what it is"), Dine::ink4);
    roleHint.setBounds (cell.withTrimmedLeft (cell.getWidth() / 2).withTrimmedRight (36));
    roleHint.setText ({}, juce::dontSendNotification);
    roleEditor.setVisible (true);
    roleHint.setVisible (true);
    roleEditor.toFront (false);
    roleHint.toFront (false);
    roleEditor.grabKeyboardFocus();
}

void AssignPage::closeRoleEditor (bool accept)
{
    if (roleInput < 0) return;
    const int input = roleInput;
    roleInput = -1;
    ChannelRole role {};
    const bool chosen = accept && roleFor (roleEditor.getText(), role);
    roleEditor.setVisible (false);
    roleHint.setVisible (false);
    if (chosen && input < numInputs)
    {
        chooseRole (input, role);
        commit();
        rebuild();
    }
}

// A list pasted into a name: one name per line, from this input down the column in the order
// the table shows them; a second, tab-separated column on a line is what that input is.
void AssignPage::fillDown (int input, const juce::String& text)
{
    juce::StringArray lines;
    lines.addLines (text);
    lines.removeEmptyStrings (true);
    if (lines.isEmpty()) return;
    const int start = rowIndexOf (input);
    if (start < 0) return;
    int filled = 0, last = input;
    for (int k = 0; k < lines.size() && start + k < int (rows.size()); ++k)
    {
        const int in = rows[size_t (start + k)]->input;
        const auto nameText = lines[k].upToFirstOccurrenceOf ("\t", false, false).trim();
        const auto roleText = lines[k].fromFirstOccurrenceOf ("\t", false, false).trim();
        entries[size_t (in)].name = nameText;
        ChannelRole role {};
        if (roleText.isNotEmpty() && roleFor (roleText, role)) chooseRole (in, role);
        else if (! entries[size_t (in)].assigned && roleFor (nameText, role)) chooseRole (in, role);
        ++filled;
        last = in;
    }
    commit();
    rebuild();
    if (onToast) onToast (juce::String (filled) + (filled == 1 ? " name" : " names") + " " + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92"))
                          + " inputs " + juce::String (input + 1).paddedLeft ('0', 2) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))
                          + juce::String (last + 1).paddedLeft ('0', 2) + ".");
}

// Cmd-D: this input becomes what the one above it is, with the next number on its name when
// that one ends in a number ("BV 1" -> "BV 2").
void AssignPage::sameAsAbove (int input)
{
    const int r = rowIndexOf (input);
    if (r <= 0) return;
    const auto& above = entries[size_t (rows[size_t (r - 1)]->input)];
    auto& e = entries[size_t (input)];
    if (above.assigned) chooseRole (input, above.role);
    const auto trail = above.name.retainCharacters ("0123456789");
    const bool numbered = above.name.isNotEmpty() && juce::CharacterFunctions::isDigit (above.name.getLastCharacter());
    if (numbered)
    {
        const auto digits = above.name.substring (above.name.trimCharactersAtEnd ("0123456789").length());
        e.name = above.name.trimCharactersAtEnd ("0123456789") + juce::String (digits.getIntValue() + 1);
    }
    else e.name = above.name;
    juce::ignoreUnused (trail);
    commit();
    rebuild();
    focusName (input);
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
                         else chooseRole (input, byId[size_t (chosen - 100)]);
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

SetupLayout AssignPage::layout() const { return SetupLayout::of (getLocalBounds(), true, true); }

// The table's columns at a width: the meter gives way first, then the group's cell, then the
// name, so the role popup keeps its room and nothing is ever cut.
AssignPage::Cols AssignPage::colsFor (int width)
{
    Cols c;
    const auto source = [&c, width]
    {
        return width - 20 - (c.num + c.name + 22 + (c.gap - 6) + c.bus + c.pair + c.meter + c.verdict + 3 * c.gap);
    };
    if (source() < 150) c.meter = 60;
    if (source() < 150) c.bus = 104;
    if (source() < 150) c.name = 150;
    if (source() < 150) c.meter = 0;
    if (source() < 150) c.num = 40;
    return c;
}

std::vector<int> AssignPage::visibleOrder() const
{
    std::vector<int> out;
    for (const auto& r : rows) out.push_back (r->input);
    return out;
}

int AssignPage::neighbour (int input, int delta) const
{
    const auto order = visibleOrder();
    const auto it = std::find (order.begin(), order.end(), input);
    if (it == order.end()) return order.empty() ? -1 : (delta > 0 ? order.front() : order.back());
    const long at = long (it - order.begin()) + delta;
    return at >= 0 && at < long (order.size()) ? order[size_t (at)] : -1;
}

int AssignPage::sessionIndexOf (int input) const
{
    const auto& inputs = controller.getSession().inputs;
    for (size_t k = 0; k < inputs.size(); ++k) if (inputs[k].inputA == input) return int (k);
    return -1;
}

int AssignPage::stripOf (int input) const
{
    const int k = sessionIndexOf (input);
    if (k < 0) return -1;
    const auto& g = controller.getGraph();
    for (int s = 0; s < g.numStrips(); ++s) if (g.strips[size_t (s)].input == k) return s;
    return -1;
}

void AssignPage::pickInput (int input)
{
    if (input < 0 || input >= numInputs) return;
    picked = input;
    lastClicked = input;
    if (detail != nullptr) detail->show (input);
    // keep the row in view
    for (const auto& r : rows)
        if (r->input == input)
        {
            const auto b = r->getBounds();
            auto view = viewport.getViewArea();
            if (b.getY() < view.getY()) viewport.setViewPosition (0, b.getY());
            else if (b.getBottom() > view.getBottom()) viewport.setViewPosition (0, b.getBottom() - view.getHeight());
        }
    listHolder.repaint();
}

// THE TABLE FROM THE KEYBOARD, with no cell being typed in (v4): up and down move the
// picked input, Shift extends the selection, Space picks it out, Return types its name,
// Cmd-A picks out everything shown, Cmd-V pastes a list, Escape lets go of the selection.
bool AssignPage::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (paste != nullptr)
    {
        if (code == juce::KeyPress::escapeKey) { paste.reset(); grabKeyboardFocus(); return true; }
        return false;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const int delta = code == juce::KeyPress::upKey ? -1 : 1;
        const int from = picked;
        const int to = neighbour (picked, delta);
        if (to < 0) return true;
        if (mods.isShiftDown())
        {
            if (from >= 0) entries[size_t (from)].selected = true;
            entries[size_t (to)].selected = true;
            updateToolbar();
            resized();
        }
        pickInput (to);
        repaint();
        return true;
    }
    if (code == juce::KeyPress::spaceKey && ! mods.isAnyModifierKeyDown())
    {
        if (picked >= 0) toggleSelection (picked, false);
        return true;
    }
    if (code == juce::KeyPress::returnKey && ! mods.isAnyModifierKeyDown())
    {
        if (picked >= 0) focusName (picked);
        return true;
    }
    if (mods.isCommandDown() && (code == 'A' || code == 'a'))
    {
        for (int i : visibleOrder()) entries[size_t (i)].selected = true;
        updateToolbar();
        resized();
        repaint();
        return true;
    }
    if (mods.isCommandDown() && (code == 'V' || code == 'v'))
    {
        openPaste (juce::SystemClipboard::getTextFromClipboard());
        return true;
    }
    if (code == juce::KeyPress::escapeKey && selectionCount() > 0) { clearSelection(); return true; }
    return false;
}

std::vector<int> AssignPage::fillTargets (int fromInput, int count) const
{
    std::vector<int> out;
    const int start = rowIndexOf (fromInput);
    if (start < 0) return out;
    for (int k = 0; k < count && start + k < int (rows.size()); ++k) out.push_back (rows[size_t (start + k)]->input);
    return out;
}

void AssignPage::openPaste (const juce::String& text)
{
    int from = picked;
    if (from < 0) for (int i : visibleOrder()) if (entries[size_t (i)].selected) { from = i; break; }
    if (from < 0) for (int i : visibleOrder()) if (! entries[size_t (i)].assigned) { from = i; break; }
    if (from < 0 && ! rows.empty()) from = rows.front()->input;
    paste = std::make_unique<PastePanel> (*this);
    addAndMakeVisible (*paste);
    paste->set (text.trim(), from);
    resized();
    paste->toFront (true);
}

bool AssignPage::isPasteOpen() const noexcept { return paste != nullptr; }
juce::String AssignPage::pastePreview() const { return paste != nullptr ? paste->preview() : juce::String(); }

void AssignPage::applyPaste()
{
    if (paste == nullptr) return;
    const auto text = paste->getText();
    const int from = paste->startInput();
    juce::Component::SafePointer<AssignPage> safe (this);
    // The panel's own button is running: it goes after this message, never under itself.
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->paste.reset(); safe->grabKeyboardFocus(); } });
    paste->setVisible (false);
    fillDown (from, text);
}

// "Number them": the picked-out inputs, in the order they are shown, become one name with a
// number - the name they share without its number, or what the first one is.
void AssignPage::numberSelection()
{
    std::vector<int> sel;
    for (int i : visibleOrder()) if (entries[size_t (i)].selected) sel.push_back (i);
    if (sel.empty()) return;
    auto stem = [] (const juce::String& n) { return n.trim().trimCharactersAtEnd ("0123456789").trim(); };
    juce::String base = stem (entries[size_t (sel.front())].name);
    for (int i : sel) if (stem (entries[size_t (i)].name) != base) { base = {}; break; }
    if (base.isEmpty()) base = entries[size_t (sel.front())].assigned ? juce::String (shortRoleName (entries[size_t (sel.front())].role))
                                                                     : juce::String ("Input");
    for (size_t k = 0; k < sel.size(); ++k) entries[size_t (sel[k])].name = base + " " + juce::String (int (k) + 1);
    commit();
    rebuild();
    if (onToast) onToast ("Numbered " + juce::String (int (sel.size())) + " inputs: " + base + " 1 to " + base + " " + juce::String (int (sel.size())) + ".");
}

void AssignPage::checkAgain()
{
    for (auto& e : entries) e.peakHoldDb = -120.0f;
    if (services.isAudioRunning()) for (int i = 0; i < numInputs; ++i) (void) services.daw().inputPeakDb (i);
    resized();
    repaint();
    if (onToast) onToast ("Listening again. Play each source, and the loudest moment is the new reading.");
}

void AssignPage::setBusFilter (int filter)
{
    busFilter = filter;
    rebuild();
}

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
        // v4: the fast-entry keys, in one line over the table.
        Dine::drawText (g, "Type to rename " + Glyph::dot() + " Tab next cell " + Glyph::dot() + " Return next input "
                           + Glyph::dot() + " paste a list to fill down " + Glyph::dot() + " Cmd-D same as above "
                           + Glyph::dot() + " Shift-click a range",
                        r.removeFromTop (18).withWidth (juce::jmin (r.getWidth(), 760)), juce::Justification::centredLeft, true);
    }

    // ---- "N inputs want the preamp moved": a card per input, what to do at the desk
    if (! bannerArea.isEmpty())
    {
        Dine::fillRounded (g, bannerArea.toFloat(), Dine::refuse, 12.0f);
        Dine::hairlineRounded (g, bannerArea.toFloat().reduced (0.5f), Dine::warn.withAlpha (0.45f), 12.0f);
        auto r = bannerArea.reduced (14, 10);
        auto head = r.removeFromTop (28).withTrimmedRight (checkAgainButton.getWidth() + 12);
        const int n = inputsNeedingGain();
        g.setColour (Dine::warn);
        g.fillEllipse (head.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        head.removeFromLeft (10);
        const juce::String title = juce::String (n) + (n == 1 ? " input wants" : " inputs want") + " the preamp moved";
        const auto titleFont = Dine::text (13.0f, 600);
        g.setColour (Dine::ink);
        g.setFont (titleFont);
        Dine::drawText (g, title, head.removeFromLeft (Dine::textWidth (titleFont, title) + 2), juce::Justification::centredLeft, false);
        const juce::String why = juce::String ("Fix these at the console ") + Glyph::dash() + " DINE can't undo a clipped preamp.";
        if (Dine::textWidth (Dine::text (12.0f), why) + 12 <= head.getWidth())
        {
            head.removeFromLeft (12);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            Dine::drawText (g, why, head, juce::Justification::centredLeft, false);
        }
        r.removeFromTop (8);
        // As many cards as fit at 220 pt or more, worst first.
        std::vector<std::pair<int, MixController::InputAdvice>> want;
        for (int i = 0; i < numInputs; ++i)
        {
            const auto& e = entries[size_t (i)];
            if (! e.assigned || e.linkedFromPrevious) continue;
            const auto a = controller.liveCaptureAdvice (e.role, e.peakHoldDb);
            using Level = MixController::InputAdvice::Level;
            if (a.level == Level::Clipping || a.level == Level::Hot || a.level == Level::Low || a.level == Level::Faint)
                want.push_back ({ i, a });
        }
        std::stable_sort (want.begin(), want.end(), [] (const auto& x, const auto& y)
        {
            return (x.second.level == MixController::InputAdvice::Level::Clipping) > (y.second.level == MixController::InputAdvice::Level::Clipping);
        });
        const int cards = juce::jlimit (1, 4, juce::jmin (int (want.size()), (r.getWidth() + 10) / 230));
        const int cw = (r.getWidth() - 10 * (cards - 1)) / cards;
        for (int k = 0; k < cards && k < int (want.size()); ++k)
        {
            auto card = r.removeFromLeft (cw);
            r.removeFromLeft (10);
            Dine::fillRounded (g, card.toFloat(), Dine::card, 8.0f);
            auto c = card.reduced (12, 8);
            auto top = c.removeFromTop (18);
            const auto chip = gainChipText (want[size_t (k)].second);
            const auto chipFont = Dine::text (11.5f, 600);
            g.setColour (gainVerdictColour (want[size_t (k)].second.level));
            g.setFont (chipFont);
            Dine::drawText (g, chip, top.removeFromRight (Dine::textWidth (chipFont, chip) + 2), juce::Justification::centredRight, false);
            top.removeFromRight (8);
            const int in = want[size_t (k)].first;
            g.setColour (Dine::ink3);
            g.setFont (Dine::mono (11.0f, 500));
            Dine::drawText (g, juce::String (in + 1).paddedLeft ('0', 2), top.removeFromLeft (24), juce::Justification::centredLeft, false);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (13.0f, 600));
            Dine::drawFittedText (g, entries[size_t (in)].name.isNotEmpty() ? entries[size_t (in)].name : juce::String (friendlyRoleName (entries[size_t (in)].role)),
                                  top, juce::Justification::centredLeft, 1, 0.85f);
            c.removeFromTop (4);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (11.5f));
            Dine::drawFittedText (g, juce::String (want[size_t (k)].second.detail), c, juce::Justification::topLeft, juce::jmax (1, c.getHeight() / 15), 1.0f);
        }
    }

    // ---- the actions row: with inputs picked out, how many
    if (selectionCount() > 0)
    {
        auto label = juce::Rectangle<int> (deselectButton.getRight() + 12, actionsArea.getY(), juce::jmin (130, actionsArea.getWidth() / 5) - 12,
                                           actionsArea.getHeight());
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.0f, 500));
        Dine::drawFittedText (g, juce::String (selectionCount()) + " selected", label, juce::Justification::centredLeft, 1, 0.85f);
    }
    else
    {
        // the keys, after the three buttons, as room allows
        auto hint = actionsArea.withTrimmedLeft (juce::jmax (deskLabelsButton.getRight(), juce::jmax (quickButton.getRight(), patchButton.getRight())) - actionsArea.getX() + 16);
        const juce::String keys = juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x91\xe2\x86\x93")) + " move " + Glyph::dot() + " Space select "
                                + Glyph::dot() + " Return name " + Glyph::dot() + " Cmd-V a list fills down " + Glyph::dot() + " Cmd-A all";
        if (Dine::textWidth (Dine::text (12.0f), keys) <= hint.getWidth())
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            Dine::drawText (g, keys, hint, juce::Justification::centredLeft, false);
        }
    }

    // ---- the column captions, over the table
    {
        const auto c = colsFor (viewport.getWidth() - 10);
        auto head = juce::Rectangle<int> (L.main.getX(), captionTop, L.main.getWidth(), 18).reduced (10, 0);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        auto cell = [&g, &head] (int w, const juce::String& text)
        {
            Dine::drawText (g, text, head.removeFromLeft (w), juce::Justification::centredLeft, false);
        };
        cell (c.num, "Ch");
        cell (c.name + 22, "Name");
        head.removeFromLeft (c.gap - 6);
        auto signal = head.removeFromRight (c.meter + c.verdict);
        auto verdict = signal.removeFromRight (c.verdict);
        head.removeFromRight (c.gap);
        auto pairCell = head.removeFromRight (c.pair);
        head.removeFromRight (c.gap);
        auto group = head.removeFromRight (c.bus);
        Dine::drawText (g, "What it is", head, juce::Justification::centredLeft, false);
        Dine::drawText (g, "Feeds", group, juce::Justification::centredLeft, false);
        Dine::drawText (g, "Pair", pairCell, juce::Justification::centredLeft, false);
        if (signal.getWidth() >= Dine::textWidth (Dine::text (11.0f, 500), "Arriving")) Dine::drawText (g, "Arriving", signal, juce::Justification::centredLeft, false);
        Dine::drawText (g, "Level", verdict, juce::Justification::centredLeft, false);
    }

    if (rows.empty())
    {
        auto empty = L.main.withY (captionTop).withTrimmedTop (30).removeFromTop (72).withWidth (juce::jmin (620, L.main.getWidth()));
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

    // ---- the tool row: the group chips at the left, search and the list button at the right.
    // A chip with no room steps out whole (All and Not used never do; every group is also
    // under the list button).
    auto bar = L.toolbar.withSizeKeepingCentre (L.toolbar.getWidth(), Dine::Metric::control);
    {
        const int sw = juce::jmin (220, bar.getWidth() / 4);
        search.setBounds (bar.removeFromRight (sw));
        bar.removeFromRight (8);
        groupButton.setBounds (bar.removeFromRight (30));
        bar.removeFromRight (12);
        const int allW = juce::jmax (44, chips.front()->idealWidth()), unusedW = juce::jmax (80, chips.back()->idealWidth());
        int room = bar.getWidth() - allW - unusedW - 2 * 6;
        int x = bar.getX();
        chips.front()->setBounds (x, bar.getY(), allW, bar.getHeight());
        x += allW + 6;
        for (size_t k = 1; k + 1 < chips.size(); ++k)
        {
            const int w = juce::jmax (60, chips[k]->idealWidth());
            const bool fits = w + 6 <= room;
            chips[k]->setVisible (fits);
            if (! fits) { room = 0; continue; }
            chips[k]->setBounds (x, bar.getY(), w, bar.getHeight());
            x += w + 6;
            room -= w + 6;
        }
        chips.back()->setBounds (x, bar.getY(), unusedW, bar.getHeight());
    }

    // ---- the right panel: the picked input
    if (detail != nullptr)
    {
        detail->setVisible (! L.rail.isEmpty());
        detail->setBounds (L.rail.withTrimmedBottom (8));
    }

    auto main = L.main;
    // ---- "N inputs want the preamp moved": the banner over the table while any do
    bannerArea = {};
    checkAgainButton.setVisible (false);
    if (inputsNeedingGain() > 0)
    {
        bannerArea = main.removeFromTop (118);
        main.removeFromTop (12);
        const int cw = juce::jmax (96, checkAgainButton.idealWidth() + 8);
        checkAgainButton.setBounds (bannerArea.reduced (14, 10).removeFromTop (28).removeFromRight (cw));
        checkAgainButton.setVisible (true);
    }

    // ---- the actions row: the whole desk's, or the selection's
    actionsArea = main.removeFromTop (Dine::Metric::control);
    main.removeFromTop (10);
    {
        auto left = actionsArea;
        auto place = [&left] (DineButton& b, int minW)
        {
            const int w = juce::jmax (minW, b.idealWidth());
            if (w > left.getWidth()) { b.setBounds ({}); return; }
            b.setBounds (left.removeFromLeft (w));
            left.removeFromLeft (8);
        };
        if (selectionCount() == 0)
        {
            place (deskLabelsButton, 110);
            place (quickButton, 110);
            place (patchButton, 80);
        }
        else
        {
            place (deselectButton, 78);
            left.removeFromLeft (juce::jmin (130, left.getWidth() / 5));      // the count, painted
            for (auto* b : { &bulkButton, &kitButton, &nameSelButton, &pairSelButton, &numberButton, &dropButton })
                place (*b, 80);
        }
    }

    // ---- the list. The column captions are drawn over it, so it starts under them; a flat
    // list has no band over it at all, because "All inputs" over every input says nothing.
    const bool banded = grouped && groups.size() > 1;
    captionTop = main.getY();
    auto list = main.withTrimmedTop (18 + 6);
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
    if (paste != nullptr) paste->setBounds (getLocalBounds());
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
                          + juce::String (styleProfileName (session.profile)) + ". Purpose and sound can be switched at any time: "
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
