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
            { "Speaking mics", MixBus::Speech, { ChannelRole::Speech } }
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

    // The shape of an empty state or an advisory card: an icon, a bold line and a sentence.
    void drawNoteCard (juce::Graphics& g, juce::Rectangle<int> r, Dine::Icon icon, juce::Colour tint,
                       const juce::String& title, const juce::String& body, bool warning)
    {
        if (warning)
        {
            Dine::fillRounded (g, r.toFloat(), tint.withAlpha (0.10f), Dine::Radius::card);
            Dine::hairlineRounded (g, r.toFloat(), tint.withAlpha (0.30f), Dine::Radius::card);
        }
        else
        {
            Dine::drawCard (g, r.toFloat());
        }
        auto inner = r.reduced (12, 11);
        auto line = inner.removeFromTop (15);
        Dine::drawIcon (g, icon, line.removeFromLeft (14).toFloat().withSizeKeepingCentre (14.0f, 14.0f), tint);
        line.removeFromLeft (7);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.5f, 600));
        g.drawText (title, line, juce::Justification::centredLeft, true);
        inner.removeFromTop (5);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (11.5f));
        g.drawFittedText (body, inner, juce::Justification::topLeft, 4);
    }

    // A key / value line in a small card: "Groups   5 of 5 in use".
    void drawStat (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& k, const juce::String& v)
    {
        auto key = r.removeFromLeft (66);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f));
        g.drawText (k, key, juce::Justification::centredLeft, true);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.0f));
        g.drawText (v, r, juce::Justification::centredLeft, true);
    }

    constexpr int kHead = 52, kToolbar = 46, kGutter = 18, kCardGap = 16;
}

// ============================================================================ the shared shape
SetupLayout SetupLayout::of (juce::Rectangle<int> page, bool withToolbar, bool withRail)
{
    SetupLayout L;
    L.footer = page.removeFromBottom (Dine::Metric::footer);
    auto r = page.reduced (Dine::Metric::padX, 0);
    r.removeFromTop (20);
    L.head = r.removeFromTop (kHead);
    L.toolbar = withToolbar ? r.removeFromTop (kToolbar) : juce::Rectangle<int>();
    if (! withToolbar) r.removeFromTop (18);
    r.removeFromBottom (14);
    if (withRail && r.getWidth() > Dine::Metric::setupRail + 360)
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
    g.setFont (Dine::text (20.0f, 640));
    g.drawText (title, r.removeFromTop (26), juce::Justification::centredLeft);
    r.removeFromTop (2);
    g.setColour (Dine::ink2);
    g.setFont (Dine::text (13.0f));
    g.drawFittedText (sentence, r.removeFromTop (18), juce::Justification::topLeft, 1);
}

void drawSetupFooter (juce::Graphics& g, juce::Rectangle<int> page, const juce::String& note, int reservedRight)
{
    auto footer = page.removeFromBottom (Dine::Metric::footer);
    g.setColour (Dine::window.brighter (0.02f));
    g.fillRect (footer);
    Dine::drawRule (g, footer.withHeight (1), Dine::hair);
    if (note.isEmpty()) return;
    auto r = footer.reduced (Dine::Metric::padX, 0);
    r.removeFromRight (reservedRight + 12);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.0f));
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

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& it = page.items[size_t (index)];
        const bool on = getToggleState();
        auto b = getLocalBounds();
        g.setColour (on ? Dine::accentDeep : over ? juce::Colours::white.withAlpha (0.05f) : juce::Colours::transparentBlack);
        g.fillRect (b);
        Dine::drawRule (g, b.removeFromBottom (1), Dine::hairSoft);

        const juce::Colour body = on ? juce::Colours::white.withAlpha (0.9f) : Dine::ink2;
        auto r = getLocalBounds().reduced (12, 0);
        auto when = r.removeFromRight (kWhenW);
        auto count = r.removeFromRight (kCountW);
        auto purpose = r.removeFromRight (kPurposeW);
        auto sound = r.removeFromRight (kSoundW);

        Dine::drawIcon (g, it.summary.tracks > 0 ? Dine::Icon::Waveform : Dine::Icon::List,
                        r.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f),
                        on ? Dine::onAccent : Dine::ink4);
        r.removeFromLeft (8);

        auto name = r.removeFromTop (r.getHeight() / 2 + 2).withTrimmedTop (3);
        auto path = r;
        const juce::String tag = it.open ? "Open" : it.listing.file.getFileName().endsWithIgnoreCase (".dinelive.json") ? "Older name" : juce::String();
        if (tag.isNotEmpty())
        {
            const int w = Dine::textWidth (Dine::text (10.5f, 500), tag) + 14;
            auto pill = name.removeFromLeft (juce::jmin (w, name.getWidth()));
            // The name comes first, so the pill is placed after it is measured.
            const int nameW = juce::jmin (Dine::textWidth (Dine::text (13.0f, 500), it.listing.name), name.getWidth());
            pill = name.withX (name.getX() + nameW + 8).withWidth (w).withSizeKeepingCentre (w, 15);
            Dine::fillRounded (g, pill.toFloat(), on ? juce::Colours::white.withAlpha (0.22f)
                                                     : juce::Colour (0xff8fa2d8).withAlpha (0.16f), Dine::Radius::pill);
            g.setColour (on ? Dine::onAccent : juce::Colour (0xff8fa2d8));
            g.setFont (Dine::text (10.5f, 500));
            g.drawText (tag, pill, juce::Justification::centred);
            name = name.withWidth (nameW);
        }
        g.setColour (on ? Dine::onAccent : Dine::ink);
        g.setFont (Dine::text (13.0f, 500));
        g.drawText (it.listing.name, name, juce::Justification::centredLeft, true);
        g.setColour (on ? juce::Colours::white.withAlpha (0.7f) : Dine::ink4);
        g.setFont (Dine::text (11.0f));
        g.drawText (page.folderText (it.listing.file), path, juce::Justification::centredLeft, true);

        g.setColour (body);
        g.setFont (Dine::text (12.5f));
        g.drawText (it.summary.valid ? juce::String (styleProfileName (it.summary.profile)) : juce::String (Glyph::dash()),
                    sound, juce::Justification::centredLeft, true);
        g.drawText (it.summary.valid ? juce::String (mixPurposeName (it.summary.purpose)) : juce::String (Glyph::dash()),
                    purpose, juce::Justification::centredLeft, true);
        g.setFont (Dine::mono (12.0f));
        g.drawText (it.summary.valid ? juce::String (it.summary.inputs) : juce::String (Glyph::dash()),
                    count, juce::Justification::centredLeft);
        g.setFont (Dine::text (12.0f));
        g.drawText (it.when, when, juce::Justification::centredLeft, true);
    }

    static constexpr int kSoundW = 140, kPurposeW = 130, kCountW = 56, kWhenW = 96;

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
    search.setColour (juce::TextEditor::backgroundColourId, Dine::well);
    search.setColour (juce::TextEditor::outlineColourId, Dine::hair);
    search.setColour (juce::TextEditor::focusedOutlineColourId, Dine::accent);
    search.setColour (juce::TextEditor::textColourId, Dine::ink);
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
    const auto L = SetupLayout::of (getLocalBounds(), true, true);

    auto head = L.head;
    head.removeFromRight (juce::jmax (0, getWidth() - newButton.getX()) + 10);
    drawSetupHead (g, head, "Sessions",
                   "Opening one puts its inputs, groups and tune back exactly as they were.");

    auto toolbar = L.toolbar;
    toolbar.removeFromTop (10);
    auto chipRow = toolbar.withHeight (Dine::Metric::control);
    if (chips[0] != nullptr)
    {
        auto track = chips[0]->getBounds();
        for (int i = 1; i < 3; ++i) track = track.getUnion (chips[size_t (i)]->getBounds());
        DineChip::drawTrack (g, track.expanded (2, 2));
    }
    g.setColour (Dine::ink4);
    g.setFont (Dine::text (12.0f));
    g.drawText ("Sorted by when it was last saved", chipRow.withTrimmedRight (2), juce::Justification::centredRight, true);

    // The search field's magnifier, drawn into its indent.
    Dine::drawIcon (g, Dine::Icon::Search, search.getBounds().toFloat().withWidth (24.0f).reduced (6.0f, 5.0f), Dine::ink4, 1.2f);

    // ---- the table
    auto table = L.main;
    Dine::drawCard (g, table.toFloat());
    auto header = table.removeFromTop (26);
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRect (header.reduced (1, 0).withTrimmedTop (1));
    Dine::drawRule (g, header.removeFromBottom (1), Dine::hair);
    auto hr = header.reduced (12, 0);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.0f, 500));
    g.drawText ("Last saved", hr.removeFromRight (Row::kWhenW), juce::Justification::centredLeft);
    g.drawText ("Inputs", hr.removeFromRight (Row::kCountW), juce::Justification::centredLeft);
    g.drawText ("Purpose", hr.removeFromRight (Row::kPurposeW), juce::Justification::centredLeft);
    g.drawText ("Sound", hr.removeFromRight (Row::kSoundW), juce::Justification::centredLeft);
    g.drawText ("Session", hr, juce::Justification::centredLeft);

    if (shown.empty())
    {
        auto empty = table.reduced (30, 40).removeFromTop (90);
        Dine::drawIcon (g, Dine::Icon::List, empty.removeFromTop (28).withSizeKeepingCentre (28, 28).toFloat(), Dine::ink4);
        empty.removeFromTop (12);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (14.0f, 600));
        g.drawText (items.empty() ? "No sessions saved yet" : "Nothing matches that",
                    empty.removeFromTop (18), juce::Justification::centred);
        empty.removeFromTop (4);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText (items.empty() ? "Start a new session and it is saved into ~/Music/DLIVE as you work."
                                        : "Try a different word, or switch the filter back to All.",
                          empty.removeFromTop (34), juce::Justification::centredTop, 2);
    }

    // ---- the rail
    if (! L.rail.isEmpty())
    {
        auto rail = L.rail;
        Dine::drawCaption (g, rail.removeFromTop (16), "Selected session");
        rail.removeFromTop (4);
        auto card = rail.removeFromTop (openButton.getBottom() + 6 + 24 + 8 - rail.getY());
        if (const auto* it = selectedItem())
        {
            Dine::drawCard (g, card.toFloat());
            auto inner = card.reduced (13, 12);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (15.0f, 600));
            g.drawText (it->listing.name, inner.removeFromTop (19), juce::Justification::centredLeft, true);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.0f));
            g.drawText (it->summary.valid
                            ? juce::String (styleProfileName (it->summary.profile)) + "  " + Glyph::dot() + "  "
                                  + juce::String (mixPurposeName (it->summary.purpose))
                            : juce::String ("Not a DLIVE session"),
                        inner.removeFromTop (16), juce::Justification::centredLeft, true);
            inner.removeFromTop (9);

            std::vector<Dine::BarSlice> slices;
            const float total = juce::jmax (1.0f, float (it->summary.inputs));
            for (int b = 0; b < int (MixBus::Master); ++b)
                slices.push_back ({ float (it->summary.perBus[size_t (b)]) / total, busColour (MixBus (b)) });
            Dine::drawStackedBar (g, inner.removeFromTop (5), slices);
            inner.removeFromTop (11);

            int groupsUsed = 0;
            for (int b = 0; b < int (MixBus::Master); ++b) if (it->summary.perBus[size_t (b)] > 0) ++groupsUsed;
            drawStat (g, inner.removeFromTop (17), "Inputs", juce::String (it->summary.inputs) + " assigned");
            drawStat (g, inner.removeFromTop (17), "Groups", juce::String (groupsUsed) + " of 5 in use");
            drawStat (g, inner.removeFromTop (17), "Tuned",
                      it->summary.tuneCount > 0 ? juce::String (it->summary.tuneCount) + (it->summary.tuneCount == 1 ? " time" : " times")
                                                : juce::String ("Not yet"));
            drawStat (g, inner.removeFromTop (17), "Takes",
                      it->summary.tracks > 0 ? juce::String (it->summary.tracks) + " tracks recorded" : juce::String ("None on the timeline"));
            drawStat (g, inner.removeFromTop (17), "Device", it->summary.inputDevice.isNotEmpty() ? it->summary.inputDevice : juce::String (Glyph::dash()));
        }
        rail.removeFromTop (kCardGap);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f));
        g.drawFittedText ("Sessions live in ~/Music/DLIVE. The audio stays in its own folder - nothing is copied into a library.",
                          rail.removeFromTop (48).reduced (2, 0), juce::Justification::topLeft, 4);
    }

    drawSetupFooter (g, getLocalBounds(), items.empty() ? juce::String()
                                                        : juce::String (items.size()) + (items.size() == 1 ? " session" : " sessions")
                                                              + " in your library.", 0);
}

void SessionsPage::resized()
{
    const auto L = SetupLayout::of (getLocalBounds(), true, true);

    auto head = L.head.withHeight (Dine::Metric::button);
    const int nw = juce::jmax (110, newButton.idealWidth());
    newButton.setBounds (head.removeFromRight (nw));
    head.removeFromRight (10);
    search.setBounds (head.removeFromRight (190).withHeight (Dine::Metric::control));

    auto toolbar = L.toolbar;
    toolbar.removeFromTop (10);
    auto chipRow = toolbar.withHeight (Dine::Metric::control);
    chipRow.removeFromTop (0);
    for (int i = 0; i < 3; ++i)
    {
        const int w = juce::jmax (58, chips[size_t (i)]->idealWidth());
        chips[size_t (i)]->setBounds (chipRow.removeFromLeft (w).withSizeKeepingCentre (w, 21));
    }

    auto list = L.main.withTrimmedTop (26).reduced (1);
    viewport.setBounds (list);
    const int total = int (rows.size()) * 40;
    listHolder.setSize (list.getWidth() - (total > list.getHeight() ? 10 : 0), juce::jmax (total, list.getHeight()));
    int y = 0;
    for (auto& r : rows) { r->setBounds (0, y, listHolder.getWidth(), 40); y += 40; }

    if (! L.rail.isEmpty())
    {
        auto rail = L.rail;
        rail.removeFromTop (16 + 4);
        auto inner = rail.reduced (13, 0).withTrimmedTop (12);
        inner.removeFromTop (19 + 16 + 9 + 5 + 11 + 17 * 5 + 8);
        openButton.setBounds (inner.removeFromTop (Dine::Metric::button));
        inner.removeFromTop (6);
        revealButton.setBounds (inner.removeFromTop (Dine::Metric::control));
    }
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
        auto b = getLocalBounds();
        g.setColour (on ? Dine::accent.withAlpha (0.13f) : over ? juce::Colours::white.withAlpha (0.05f) : juce::Colours::transparentBlack);
        g.fillRect (b);
        if (! first) Dine::drawRule (g, b.withHeight (1), Dine::hairSoft);

        auto r = b.reduced (12, 0);
        Dine::drawRadio (g, r.removeFromLeft (15).toFloat(), on);
        r.removeFromLeft (11);
        Dine::drawIcon (g, Dine::Icon::Device, r.removeFromLeft (18).toFloat().withSizeKeepingCentre (18.0f, 18.0f),
                        on ? Dine::accent : Dine::glyph);
        r.removeFromLeft (11);

        // The state pill first, so the name always gets what is left.
        const bool usable = inputChannels > 0;
        const juce::String state = usable ? "Ready" : "Output only";
        const juce::Colour tint = usable ? Dine::ok : Dine::warn;
        const int pw = int (Dine::pillWidth (state, false));
        Dine::drawPill (g, r.removeFromRight (pw).toFloat().withSizeKeepingCentre (float (pw), 18.0f), state, tint);
        r.removeFromRight (12);
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (12.0f));
        g.drawText (juce::String (inputChannels) + " in / " + juce::String (outputChannels) + " out",
                    r.removeFromRight (104), juce::Justification::centredRight);
        r.removeFromRight (10);

        // A second line only when there is something to say; otherwise the name sits centred.
        const juce::String note = ! usable ? juce::String ("It can play the mix out, but nothing comes in this way")
                                : open     ? juce::String ("Open now - DLIVE is listening to it")
                                           : juce::String();
        if (note.isNotEmpty())
        {
            auto name = r.removeFromTop (r.getHeight() / 2 + 2).withTrimmedTop (5);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (13.0f, on ? 600 : 400));
            g.drawText (deviceName, name, juce::Justification::centredLeft, true);
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (11.5f));
            g.drawText (note, r, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (Dine::ink);
            g.setFont (Dine::text (13.0f, on ? 600 : 400));
            g.drawText (deviceName, r, juce::Justification::centredLeft, true);
        }
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
        auto b = getLocalBounds();
        if (on || over)
            Dine::fillRounded (g, b.toFloat(), on ? Dine::accent.withAlpha (0.13f) : juce::Colours::white.withAlpha (0.06f), Dine::Radius::chip);
        auto r = b.reduced (8, 0);
        Dine::drawRadio (g, r.removeFromLeft (15).toFloat(), on);
        r.removeFromLeft (9);
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
    recordingButton.setIcon (Dine::Icon::Waveform);
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
    outputName = services.currentOutputDevice();
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
    c.listCaption = main.removeFromTop (16);
    main.removeFromTop (5);
    const int specH = 4 * 34;
    const int below = 20 + 16 + 5 + specH + 18 + 56;
    // The list grows with the window, but only so far: six devices is already more than any
    // church has, and a list card taller than that is a lot of empty pane. What is left goes
    // between the list and the two blocks pinned to the foot.
    c.list = main.removeFromTop (juce::jlimit (104, juce::jmax (104, 6 * 52),
                                               juce::jmax (104, main.getHeight() - below)));
    main.removeFromTop (juce::jmax (20, main.getHeight() - below));
    c.specCaption = main.removeFromTop (16);
    main.removeFromTop (5);
    c.spec = main.removeFromTop (specH);
    main.removeFromTop (18);
    c.importCard = main.removeFromTop (56);
    return c;
}

void DevicePage::paint (juce::Graphics& g)
{
    const auto L = layout();
    const int channels = services.isAudioRunning() ? services.numInputChannels()
                       : selected >= 0 && selected < inputs.size() ? inputs[selected].inputChannels : 0;

    // ---- head, with what is arriving at the device drawn across it
    auto head = L.head;
    auto activity = head.removeFromRight (juce::jmin (340, juce::jmax (0, head.getWidth() - 420)));
    drawSetupHead (g, head, "Audio device",
                   "Choose where the band comes in. DLIVE listens only - it never changes a setting on your console.");
    if (activity.getWidth() > 180 && channels > 0 && services.isAudioRunning())
    {
        const int live = liveInputCount();
        auto line = activity.removeFromTop (17);
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (11.0f));
        g.drawText ("live at the device", line, juce::Justification::centredRight, true);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f));
        const juce::String head1 = juce::String (live) + " of " + juce::String (channels) + " carrying signal";
        g.drawText (head1, line, juce::Justification::centredLeft, true);
        activity.removeFromTop (8);

        // One column per device channel: the desk at a glance, before a single input is named.
        auto bars = activity.removeFromTop (18);
        const int n = juce::jmin (channels, 64);
        const float w = float (bars.getWidth()) / float (juce::jmax (1, n));
        for (int c = 0; c < n; ++c)
        {
            auto cell = juce::Rectangle<float> (bars.getX() + c * w, float (bars.getY()), juce::jmax (2.0f, w - 2.0f), float (bars.getHeight()));
            Dine::fillRounded (g, cell, juce::Colours::white.withAlpha (0.08f), 1.5f);
            const float db = services.isAudioRunning() ? services.daw().inputPeakDb (c) : -120.0f;
            const float level = DineMeter::norm (db);
            if (level <= 0.0f) continue;
            auto filled = cell.withTop (cell.getBottom() - cell.getHeight() * level);
            Dine::fillRounded (g, filled, Dine::levelColour (db), 1.5f);
        }
    }

    // ---- the device list
    const auto col = column();
    Dine::drawCaption (g, col.listCaption, "Devices on this machine");
    Dine::drawCard (g, col.list.toFloat());
    if (rows.empty())
    {
        auto r = juce::Rectangle<int> (col.list).withSizeKeepingCentre (col.list.getWidth() - 60, 154);
        Dine::drawIcon (g, Dine::Icon::Device, r.removeFromTop (30).withSizeKeepingCentre (30, 30).toFloat(), Dine::ink4);
        r.removeFromTop (12);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (14.0f, 600));
        g.drawText ("No inputs yet", r.removeFromTop (18), juce::Justification::centred);
        r.removeFromTop (4);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText ("Connect your interface or console and rescan. You can also work from a folder of stems while nothing is plugged in.",
                          r.removeFromTop (36), juce::Justification::centredTop, 2);
    }

    // ---- what it is running at
    Dine::drawCaption (g, col.specCaption, "What it is running at");
    auto spec = col.spec;
    Dine::drawCard (g, spec.toFloat());
    const bool running = services.isAudioRunning();
    struct Line { juce::String key, value, note; juce::Colour colour; };
    const Line lines[4] = {
        { "Sample rate", running ? juce::String (services.sampleRate() / 1000.0, 1) + " kHz" : juce::String ("48.0 kHz"),
          running ? "Matched to the console" : "Set when the device opens", Dine::ink },
        { "Buffer", running ? juce::String (services.bufferSize()) + " samples" : juce::String (Glyph::dash()),
          running ? juce::String (1000.0 * services.bufferSize() / juce::jmax (1.0, services.sampleRate()), 1) + " ms each way"
                  : juce::String ("The lower it is, the sooner you hear it"), Dine::ink },
        { "Inputs seen", juce::String (channels), "Channels DLIVE can listen to", channels > 0 ? Dine::accent : Dine::ink3 },
        { "Drops", running ? juce::String (services.xrunCount()) : juce::String (Glyph::dash()),
          services.xrunCount() > 0 ? "Raise the buffer if this keeps climbing" : "Nothing missed",
          services.xrunCount() > 0 ? Dine::warn : Dine::ink }
    };
    for (int i = 0; i < 4; ++i)
    {
        auto row = spec.removeFromTop (34);
        if (i < 3) Dine::drawRule (g, row.removeFromBottom (1), Dine::hairSoft);
        auto r = row.reduced (13, 0);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawText (lines[i].key, r.removeFromLeft (130), juce::Justification::centredLeft, true);
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (11.5f));
        g.drawText (lines[i].note, r.removeFromRight (juce::jmin (250, r.getWidth() / 2)), juce::Justification::centredRight, true);
        g.setColour (lines[i].colour);
        g.setFont (Dine::mono (13.0f));
        g.drawText (lines[i].value, r, juce::Justification::centredLeft, true);
    }

    // ---- no band in the room
    {
        auto card = col.importCard;
        Dine::drawCard (g, card.toFloat());
        auto r = card.reduced (13, 0);
        Dine::drawIcon (g, Dine::Icon::Waveform, r.removeFromLeft (19).toFloat().withSizeKeepingCentre (19.0f, 19.0f), juce::Colour (0xff8fa2d8));
        r.removeFromLeft (13);
        r.removeFromRight (recordingButton.getWidth() + 12);
        auto title = r.removeFromTop (r.getHeight() / 2 + 2).withTrimmedTop (8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 500));
        g.drawText ("No band in the room?", title, juce::Justification::centredLeft, true);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (11.5f));
        g.drawText ("Import a folder of stems. They become tracks and clips, and everything from here works exactly as it does live.",
                    r.withTrimmedBottom (6), juce::Justification::topLeft, true);
    }


    if (error.isNotEmpty())
    {
        g.setColour (Dine::crit);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText (error, juce::Rectangle<int> (L.main).removeFromBottom (30), juce::Justification::centredLeft, 2);
    }

    // ---- the rail: where it comes out, and what the device is saying
    if (! L.rail.isEmpty())
    {
        auto rail = L.rail;
        Dine::drawCaption (g, rail.removeFromTop (16), "Where it comes out");
        rail.removeFromTop (4);
        auto card = rail.removeFromTop (juce::jmax (34, int (outputRows.size()) * 34 + 8));
        Dine::drawCard (g, card.toFloat());
        rail.removeFromTop (8);
        outputsButton.getBounds();     // placed in resized(); the caption above belongs to it
        rail.removeFromTop (Dine::Metric::control + kCardGap);

        const bool usable = selected >= 0 && selected < inputs.size() && inputs[selected].inputChannels > 0;
        auto note = rail.removeFromTop (92);
        if (! usable)
            drawNoteCard (g, note, Dine::Icon::Warn, Dine::warn, "Output only, for now",
                          "This device has no inputs. Pick the console or interface above and the meters fill in.", true);
        else if (! services.isAudioRunning())
            drawNoteCard (g, note, Dine::Icon::Device, Dine::accent, inputs[selected].name + " is ready",
                          juce::String (inputs[selected].inputChannels) + " inputs. Press Continue and DLIVE starts listening to them - your console is untouched.", false);
        else
            drawNoteCard (g, note, Dine::Icon::Check, Dine::ok, inputs[selected].name + " is reading",
                          juce::String (liveInputCount()) + " of the " + juce::String (channels)
                              + " channels are carrying signal. The quiet ones may just be an idle mic - you can leave them out on the next step.", false);
        rail.removeFromTop (kCardGap);
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

    viewport.setBounds (col.list.reduced (1));
    viewport.setVisible (! rows.empty());
    const int total = int (rows.size()) * 52;
    listHolder.setSize (viewport.getWidth() - (total > viewport.getHeight() ? 10 : 0), juce::jmax (total, viewport.getHeight()));
    int y = 0;
    for (auto& r : rows) { r->setBounds (0, y, listHolder.getWidth(), 52); y += 52; }

    {
        const int w = juce::jmax (140, recordingButton.idealWidth());
        recordingButton.setBounds (juce::Rectangle<int> (col.importCard).reduced (13, 0)
                                       .removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::button));
    }


    if (! L.rail.isEmpty())
    {
        auto rail = L.rail;
        rail.removeFromTop (16 + 4);
        auto card = rail.removeFromTop (juce::jmax (34, int (outputRows.size()) * 34 + 8)).reduced (4);
        for (auto& r : outputRows) r->setBounds (card.removeFromTop (34));
        rail.removeFromTop (8);
        outputsButton.setBounds (rail.removeFromTop (Dine::Metric::control));
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
        const auto tint = e.assigned ? busColour (mixBusForRole (e.role)) : juce::Colours::white.withAlpha (0.2f);
        auto b = getLocalBounds();
        g.setColour (e.selected ? Dine::accentDeep : isMouseOver (true) ? juce::Colours::white.withAlpha (0.045f)
                                                                        : juce::Colours::transparentBlack);
        g.fillRect (b);
        Dine::drawRule (g, b.removeFromBottom (1), Dine::hairSoft);

        auto r = getLocalBounds().reduced (12, 0);
        g.setColour (e.selected ? juce::Colours::white.withAlpha (0.9f) : e.assigned ? Dine::ink3 : Dine::ink4);
        g.setFont (Dine::mono (12.0f));
        g.drawText (e.linkedToNext ? juce::String (input + 1) + Glyph::minus() + juce::String (input + 2) : juce::String (input + 1),
                    r.removeFromLeft (kNumW), juce::Justification::centredLeft);
        r.removeFromLeft (kGap);

        Dine::drawIcon (g, e.assigned ? Dine::iconFor (e.icon, e.role) : Dine::Icon::Dash,
                        r.removeFromLeft (kIconW).toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                        e.selected ? Dine::onAccent : e.assigned ? tint : Dine::ink4);

        // The signal column: what is arriving on this channel right now.
        auto meter = getLocalBounds().reduced (12, 0);
        meter.removeFromRight (kPairW + kGap + kSourceW + kGap);
        meter = meter.removeFromRight (kSignalW).withSizeKeepingCentre (kSignalW, 4);
        const float db = page.services.isAudioRunning() ? page.services.daw().inputPeakDb (input) : -120.0f;
        Dine::fillRounded (g, meter.toFloat(), juce::Colours::white.withAlpha (0.10f), 2.0f);
        if (const float level = DineMeter::norm (db); level > 0.0f)
            Dine::fillRounded (g, meter.toFloat().withWidth (meter.getWidth() * level),
                               e.assigned ? tint : Dine::warn, 2.0f);

        // An unassigned input that is carrying signal is the one thing on this page worth
        // saying out loud: it is about to be left out of the mix by accident.
        if (! e.assigned && db > -54.0f)
        {
            auto flag = getLocalBounds().reduced (12, 0);
            flag.removeFromRight (kPairW + kGap + kSourceW + kGap + kSignalW + kGap);
            const juce::String text = db > -30.0f ? "signal here" : "faint";
            const int w = int (Dine::pillWidth (text, true));
            if (flag.getWidth() > w + 120)
                Dine::drawPill (g, flag.removeFromRight (w).toFloat().withSizeKeepingCentre (float (w), 17.0f),
                                text, Dine::warn, Dine::Icon::Warn);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 0);
        r.removeFromLeft (kNumW + kGap + kIconW);
        auto pair = r.removeFromRight (kPairW);
        link.setBounds (pair.withSizeKeepingCentre (kPairW, 20));
        r.removeFromRight (kGap);
        source.setBounds (r.removeFromRight (kSourceW).withSizeKeepingCentre (kSourceW, 22));
        r.removeFromRight (kGap);
        r.removeFromRight (kSignalW + kGap);
        // A name is a name, not a paragraph: the field keeps a sensible width and the slack
        // goes between it and the signal, so the columns line up down the page.
        r = r.removeFromLeft (juce::jmin (r.getWidth(), kNameW));
        suggest.setBounds (r.removeFromRight (20).withSizeKeepingCentre (20, 20));
        name.setBounds (r.withSizeKeepingCentre (r.getWidth(), 21));
    }

    static constexpr int kNumW = 44, kGap = 12, kIconW = 24, kNameW = 260, kSignalW = 78, kSourceW = 200, kPairW = 62;

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
        auto b = getLocalBounds();
        g.setColour (over ? juce::Colour (0xff32353a) : juce::Colour (0xff2a2c30));
        g.fillRect (b);
        Dine::drawRule (g, b.removeFromBottom (1), Dine::hairSoft);
        auto r = getLocalBounds().reduced (12, 0);
        g.setColour (colour);
        g.fillEllipse (r.removeFromLeft (7).toFloat().withSizeKeepingCentre (7.0f, 7.0f));
        r.removeFromLeft (8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.0f, 600));
        const int w = Dine::textWidth (Dine::text (12.0f, 600), name);
        g.drawText (name, r.removeFromLeft (w), juce::Justification::centredLeft);
        r.removeFromLeft (8);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (11.0f));
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
    search.setColour (juce::TextEditor::backgroundColourId, Dine::well);
    search.setColour (juce::TextEditor::outlineColourId, Dine::hair);
    search.setColour (juce::TextEditor::focusedOutlineColourId, Dine::accent);
    search.setColour (juce::TextEditor::textColourId, Dine::ink);
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

    // The three things worth doing to a whole session at once.
    const char* quickLabels[3] = { "Name everything from what it is", "Select every input not used", "Pair every L and R" };
    const char* quickLines[3] = { "Every assigned input takes its short desk name.",
                                  "Then set them all at once, or fill a kit down them.",
                                  "Neighbours named L and R become one stereo row." };
    for (int i = 0; i < 3; ++i)
    {
        quickButtons[size_t (i)] = std::make_unique<QuickAction> (quickLabels[i], quickLines[i]);
        addAndMakeVisible (*quickButtons[size_t (i)]);
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

SetupLayout AssignPage::layout() const { return SetupLayout::of (getLocalBounds(), true, true); }

void AssignPage::paint (juce::Graphics& g)
{
    const auto L = layout();
    const int assigned = assignedCount(), unused = unusedCount();
    const int total = assigned + unused;

    // ---- head: the title, and how far through the desk you are
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
        Dine::drawStackedBar (g, progress.removeFromTop (5), slices);
        progress.removeFromTop (5);
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (11.0f));
        g.drawText (unused > 0 ? juce::String (unused) + " still open" : "Every input placed",
                    progress.removeFromTop (14), juce::Justification::centredRight, true);
    }

    // ---- toolbar
    if (selectionCount() == 0)
    {
        auto track = juce::Rectangle<int>();
        for (auto& c : chips) track = track.isEmpty() ? c->getBounds() : track.getUnion (c->getBounds());
        if (! track.isEmpty()) DineChip::drawTrack (g, track.expanded (2, 2));
        Dine::drawIcon (g, Dine::Icon::Search, search.getBounds().toFloat().withWidth (24.0f).reduced (6.0f, 5.0f), Dine::ink4, 1.2f);
    }
    else
    {
        auto label = L.toolbar.withHeight (Dine::Metric::control).withWidth (bulkButton.getX() - L.toolbar.getX() - 9);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawText (juce::String (selectionCount()) + (selectionCount() == 1 ? " input" : " inputs") + " picked out",
                    label, juce::Justification::centredLeft, true);
    }

    // ---- the table
    auto table = L.main;
    Dine::drawCard (g, table.toFloat());
    auto header = table.removeFromTop (26);
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRect (header.reduced (1, 0).withTrimmedTop (1));
    Dine::drawRule (g, header.removeFromBottom (1), Dine::hair);
    auto hr = header.reduced (12, 0);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.0f, 500));
    g.drawText ("Pair", hr.removeFromRight (Row::kPairW), juce::Justification::centredLeft);
    hr.removeFromRight (Row::kGap);
    g.drawText ("What it is", hr.removeFromRight (Row::kSourceW), juce::Justification::centredLeft);
    hr.removeFromRight (Row::kGap);
    g.drawText ("Signal", hr.removeFromRight (Row::kSignalW), juce::Justification::centredLeft);
    hr.removeFromRight (Row::kGap);
    g.drawText ("Ch", hr.removeFromLeft (Row::kNumW), juce::Justification::centredLeft);
    hr.removeFromLeft (Row::kGap + Row::kIconW);
    g.drawText ("Name", hr, juce::Justification::centredLeft);

    if (rows.empty())
    {
        auto empty = table.reduced (30, 40).removeFromTop (72);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (14.0f, 600));
        g.drawText (numInputs == 0 ? "No inputs to name yet" : "Nothing matches that",
                    empty.removeFromTop (18), juce::Justification::centred);
        empty.removeFromTop (4);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText (numInputs == 0 ? "Go back and pick a device with inputs, or import a folder of stems."
                                         : "Clear the search, or switch the filter back to All.",
                          empty.removeFromTop (34), juce::Justification::centredTop, 2);
    }

    // ---- the rail
    if (! L.rail.isEmpty())
    {
        auto rail = L.rail;
        Dine::drawCaption (g, rail.removeFromTop (16), "Where it all goes");
        rail.removeFromTop (4);
        auto card = rail.removeFromTop (int (MixBus::Master) * 26 + 8);
        Dine::drawCard (g, card.toFloat());
        auto inner = card.reduced (4);
        int most = 1;
        for (int b = 0; b < int (MixBus::Master); ++b)
        {
            int n = 0;
            for (int i = 0; i < numInputs; ++i)
                if (entries[size_t (i)].assigned && ! entries[size_t (i)].linkedFromPrevious
                    && int (mixBusForRole (entries[size_t (i)].role)) == b) ++n;
            most = juce::jmax (most, n);
        }
        for (int b = 0; b < int (MixBus::Master); ++b)
        {
            int n = 0;
            for (int i = 0; i < numInputs; ++i)
                if (entries[size_t (i)].assigned && ! entries[size_t (i)].linkedFromPrevious
                    && int (mixBusForRole (entries[size_t (i)].role)) == b) ++n;
            auto row = inner.removeFromTop (26).reduced (8, 0);
            g.setColour (busColour (MixBus (b)));
            g.fillEllipse (row.removeFromLeft (7).toFloat().withSizeKeepingCentre (7.0f, 7.0f));
            row.removeFromLeft (9);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.5f));
            g.drawText (busLabel (MixBus (b)), row.removeFromLeft (54), juce::Justification::centredLeft, true);
            auto count = row.removeFromRight (18);
            g.setColour (n > 0 ? Dine::ink2 : Dine::ink4);
            g.setFont (Dine::mono (11.5f));
            g.drawText (juce::String (n), count, juce::Justification::centredRight);
            row.removeFromRight (6);
            auto bar = row.withSizeKeepingCentre (row.getWidth(), 4);
            Dine::drawStackedBar (g, bar, { { float (n) / float (most), busColour (MixBus (b)) } });
        }
        rail.removeFromTop (kCardGap);

        Dine::drawCaption (g, rail.removeFromTop (16), "Do it all at once");
        rail.removeFromTop (4);
        auto quick = rail.removeFromTop (3 * 38 + 8);
        Dine::drawCard (g, quick.toFloat());
        rail.removeFromTop (kCardGap);

        if (unused > 0 && rail.getHeight() > 60)
        {
            int live = 0;
            for (int i = 0; i < numInputs; ++i)
                if (! entries[size_t (i)].assigned && ! entries[size_t (i)].linkedFromPrevious
                    && services.isAudioRunning() && services.daw().inputPeakDb (i) > -54.0f) ++live;
            auto note = rail.removeFromTop (juce::jmin (rail.getHeight(), 108));
            drawNoteCard (g, note, Dine::Icon::Warn, Dine::warn,
                          juce::String (unused) + (unused == 1 ? " input is not used" : " inputs are not used"),
                          live > 0 ? juce::String (live) + (live == 1 ? " of them is carrying signal right now - worth a listen before you move on."
                                                                      : " of them are carrying signal right now - worth a listen before you move on.")
                                   : juce::String ("They stay out of the mix. That is fine if nothing is patched to them."),
                          true);
        }
    }

    drawSetupFooter (g, getLocalBounds(),
                     assigned > 0 ? juce::String (unused) + " unassigned inputs stay out of the mix."
                                  : juce::String ("Name at least one input to carry on."),
                     continueButton.getWidth() + backButton.getWidth() + 10);
}

void AssignPage::resized()
{
    const auto L = layout();

    // ---- toolbar
    auto bar = L.toolbar.withHeight (Dine::Metric::control);
    if (selectionCount() == 0)
    {
        search.setBounds (bar.removeFromLeft (184));
        bar.removeFromLeft (10);
        auto track = bar;
        for (auto& c : chips)
        {
            const int w = juce::jmax (54, c->idealWidth());
            c->setBounds (track.removeFromLeft (w).withSizeKeepingCentre (w, 21));
        }
        auto right = L.toolbar.withHeight (Dine::Metric::control);
        const int gw = 30;
        groupButton.setBounds (right.removeFromRight (gw));
        right.removeFromRight (8);
        const int dw = juce::jmax (120, deskLabelsButton.idealWidth());
        deskLabelsButton.setBounds (right.removeFromRight (dw));
        right.removeFromRight (8);
        const int sw = juce::jmax (86, selectAllButton.idealWidth());
        selectAllButton.setBounds (right.removeFromRight (sw));
    }
    else
    {
        bar.removeFromLeft (juce::jmin (140, bar.getWidth() / 4));
        for (auto* b : { &bulkButton, &kitButton, &nameButton, &linkButton, &dropButton })
        {
            const int w = juce::jmax (86, b->idealWidth());
            b->setBounds (bar.removeFromLeft (w));
            bar.removeFromLeft (8);
        }
        const int dw = juce::jmax (78, deselectButton.idealWidth());
        deselectButton.setBounds (bar.removeFromRight (dw));
    }

    // ---- the list, and where the group headers land inside it
    auto list = L.main.withTrimmedTop (26).reduced (1);
    viewport.setBounds (list);
    const int rowH = 32, groupH = 25;
    int total = 0;
    for (const auto& g : groups) total += groupH + int (g.inputs.size()) * rowH;
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
    }

    // ---- the rail
    if (! L.rail.isEmpty())
    {
        auto rail = L.rail;
        rail.removeFromTop (16 + 4 + int (MixBus::Master) * 26 + 8 + kCardGap + 16 + 4);
        auto quick = rail.removeFromTop (3 * 38 + 8).reduced (4);
        for (auto& b : quickButtons) b->setBounds (quick.removeFromTop (38));
        rail.removeFromTop (kCardGap);
        if (unusedCount() > 0 && rail.getHeight() > 60)
        {
            auto note = rail.removeFromTop (juce::jmin (rail.getHeight(), 108));
            const int w = juce::jmax (92, showUnusedButton.idealWidth());
            showUnusedButton.setBounds (note.removeFromBottom (32).removeFromLeft (w + 12).withTrimmedLeft (12)
                                            .withSizeKeepingCentre (w, 22));
            showUnusedButton.setVisible (true);
        }
        else
        {
            showUnusedButton.setVisible (false);
        }
    }
    else
    {
        showUnusedButton.setVisible (false);
        for (auto& b : quickButtons) b->setVisible (false);
    }

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
        Dine::fillRounded (g, b, on ? Dine::accent.withAlpha (0.13f) : over ? juce::Colour (0xff2a2c30) : Dine::card, Dine::Radius::card);
        Dine::hairlineRounded (g, b, on ? Dine::accent.withAlpha (0.75f) : Dine::hair, Dine::Radius::card);

        auto r = getLocalBounds().reduced (13, 12);
        auto top = r.removeFromTop (18);
        Dine::drawRadio (g, top.removeFromLeft (15).toFloat(), on);
        top.removeFromLeft (9);
        if (tag.isNotEmpty())
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (10.5f));
            const int w = Dine::textWidth (Dine::text (10.5f), tag);
            g.drawText (tag, top.removeFromRight (w), juce::Justification::centredRight);
        }
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.5f, 600));
        g.drawText (heading, top, juce::Justification::centredLeft, true);

        r.removeFromLeft (24);
        r.removeFromTop (6);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.0f));
        g.drawFittedText (detail, r.removeFromTop (specs.empty() ? r.getHeight() : 34), juce::Justification::topLeft, 3);

        if (specs.empty()) return;
        r.removeFromTop (7);
        auto line = r.removeFromTop (30);
        for (const auto& s : specs)
        {
            auto cell = line.removeFromLeft (juce::jmin (86, line.getWidth() / juce::jmax (1, int (specs.size()))));
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (10.5f));
            g.drawText (s.first, cell.removeFromTop (13), juce::Justification::topLeft);
            g.setColour (on ? Dine::accent : Dine::ink2);
            g.setFont (Dine::mono (12.5f));
            g.drawText (s.second, cell, juce::Justification::topLeft);
            line.removeFromLeft (10);
        }
    }

    juce::String heading, detail, tag;
    std::vector<std::pair<juce::String, juce::String>> specs;
};

namespace
{
    // Two rows of purpose cards, each tall enough for its name, its sentence and the three
    // numbers it promises.
    constexpr int kPurposeGrid = 234;

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
    repaint();
}

SetupLayout PurposePage::layout() const { return SetupLayout::of (getLocalBounds(), false, true); }

void PurposePage::paint (juce::Graphics& g)
{
    const auto L = layout();
    const auto& session = controller.getSession();
    const auto target = masterTargets (session.profile, session.purpose);

    auto head = L.head;
    auto plan = head.removeFromRight (juce::jmin (330, juce::jmax (0, head.getWidth() - 470)));
    drawSetupHead (g, head, "Purpose and sound",
                   "Purpose sets how loud it lands and how hard it may peak. Sound sets the character DLIVE tunes toward.");
    if (plan.getWidth() > 200)
    {
        const std::pair<juce::String, juce::String> readouts[3] = {
            { "Target", lufs (target.targetLufs) },
            { "True peak", dbtp (target.truePeakCeilingDb) },
            { "Sound", styleProfileName (session.profile) }
        };
        // Each readout takes what its own value needs, right to left, so "Modern Worship"
        // never runs into the peak ceiling beside it.
        for (int i = 2; i >= 0; --i)
        {
            const auto valueFont = i == 2 ? Dine::text (14.0f) : Dine::mono (14.0f);
            const int w = juce::jmin (plan.getWidth(),
                                      juce::jmax (Dine::textWidth (valueFont, readouts[i].second),
                                                  Dine::textWidth (Dine::text (11.0f), readouts[i].first)) + 18);
            auto cell = plan.removeFromRight (w);
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (11.0f));
            g.drawText (readouts[i].first, cell.removeFromTop (14), juce::Justification::centredRight);
            g.setColour (i == 0 ? Dine::accent : Dine::ink);
            g.setFont (valueFont);
            g.drawText (readouts[i].second, cell.removeFromTop (20), juce::Justification::centredRight, true);
        }
    }

    auto main = L.main;
    Dine::drawCaption (g, main.removeFromTop (16), "Purpose - where it is going");
    main.removeFromTop (5 + kPurposeGrid + 22);
    Dine::drawCaption (g, main.removeFromTop (16), "Sound - what it should feel like");

    if (! L.rail.isEmpty())
    {
        auto rail = L.rail;
        Dine::drawCaption (g, rail.removeFromTop (16), "What DLIVE will do");
        rail.removeFromTop (4);
        auto card = rail.removeFromTop (170);
        Dine::drawCard (g, card.toFloat());
        auto inner = card.reduced (13, 12);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText ("DLIVE will land the mix at " + lufs (target.targetLufs) + ", never letting it peak past "
                              + dbtp (target.truePeakCeilingDb) + ", and tune every group toward "
                              + juce::String (styleProfileName (session.profile)) + ".",
                          inner.removeFromTop (64), juce::Justification::topLeft, 5);
        inner.removeFromTop (8);
        drawStat (g, inner.removeFromTop (17), "Purpose", mixPurposeName (session.purpose));
        drawStat (g, inner.removeFromTop (17), "Sound", styleProfileName (session.profile));
        drawStat (g, inner.removeFromTop (17), "Groups", "5 groups and a master");
        drawStat (g, inner.removeFromTop (17), "Listens to",
                  juce::String (int (session.inputs.size())) + (session.inputs.size() == 1 ? " input" : " inputs"));
        rail.removeFromTop (kCardGap);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f));
        g.drawFittedText ("Purpose and sound can be switched mid-service. The next tune follows the new one; anything you moved by hand is kept.",
                          rail.removeFromTop (60).reduced (2, 0), juce::Justification::topLeft, 4);
    }

    drawSetupFooter (g, getLocalBounds(), "DLIVE listens for about twenty seconds, then sets the whole mix.",
                     continueButton.getWidth() + backButton.getWidth() + 10);
}

void PurposePage::resized()
{
    const auto L = layout();
    auto main = L.main;
    main.removeFromTop (16 + 5);

    const int gap = 10;
    auto purposeArea = main.removeFromTop (kPurposeGrid);
    const int cols = purposeArea.getWidth() > 620 ? 2 : 1;
    const int rowsN = (int (purposeTiles.size()) + cols - 1) / cols;
    const int tileH = (kPurposeGrid - gap * (rowsN - 1)) / juce::jmax (1, rowsN);
    const int tileW = (purposeArea.getWidth() - gap * (cols - 1)) / cols;
    for (int i = 0; i < int (purposeTiles.size()); ++i)
    {
        const int c = i % cols, r = i / cols;
        purposeTiles[size_t (i)]->setBounds (purposeArea.getX() + c * (tileW + gap),
                                             purposeArea.getY() + r * (tileH + gap), tileW, tileH);
    }
    main.removeFromTop (22 + 16 + 5);
    auto soundArea = main.removeFromTop (juce::jmin (main.getHeight(), 86));
    const int sw = (soundArea.getWidth() - gap * (int (soundTiles.size()) - 1)) / juce::jmax (1, int (soundTiles.size()));
    for (auto& t : soundTiles) { t->setBounds (soundArea.removeFromLeft (sw)); soundArea.removeFromLeft (gap); }

    auto footer = L.footer.reduced (Dine::Metric::padX, 0);
    const int cw = juce::jmax (130, continueButton.idealWidth());
    continueButton.setBounds (footer.removeFromRight (cw).withSizeKeepingCentre (cw, Dine::Metric::button));
    footer.removeFromRight (10);
    const int bw = juce::jmax (72, backButton.idealWidth());
    backButton.setBounds (footer.removeFromRight (bw).withSizeKeepingCentre (bw, Dine::Metric::button));
}

} // namespace livemix
