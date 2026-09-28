#include "RoutingPage.h"
#include "UI/Widgets.h"

namespace livemix
{

namespace
{
    constexpr int kNavW    = Dine::Metric::setupNav;   // the section list down the left
    constexpr int kPadX    = 26;
    constexpr int kPadY    = 22;
    constexpr int kHeadH   = 58;
    constexpr int kMapRowH = 66;
}

// ------------------------------------------------------------------ MapRow
// One saved patch: what it is called, what it was made on, how many inputs it wants and how
// they fall across the groups. The stacked bar is the same one the library uses for a
// session, because a patch and a session answer the same question - what is on this desk.
class RoutingPage::MapRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    MapRow (const InputMapStore::Listing& l, int availableChannels)
        : listing (l), available (availableChannels)
    {
        apply.setFontPx (12.0f);
        more.setFontPx (12.0f);
        addAndMakeVisible (apply);
        addAndMakeVisible (more);
        setTooltip (l.name + (l.note.isEmpty() ? juce::String() : "  " + Glyph::dash() + "  " + l.note)
                    + "\nSaved from " + (l.deviceName.isEmpty() ? juce::String ("an unnamed device") : l.deviceName)
                    + " on " + l.modified.toString (true, false));
    }

    const InputMapStore::Listing& get() const noexcept { return listing; }
    DineButton& applyButton() { return apply; }
    DineButton& moreButton() { return more; }

    // A patch that wants more channels than this device has cannot be honoured in full, and
    // the row says so rather than the dialog saying it afterwards.
    bool tooBig() const noexcept { return available > 0 && listing.channelsNeeded > available; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        Dine::drawCard (g, r.toFloat(), Dine::item);
        r = r.reduced (16, 12);

        auto head = r.removeFromTop (18);
        head.removeFromRight (apply.getWidth() + more.getWidth() + 20);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (14.0f, 600));
        Dine::drawText (g, listing.name, head, juce::Justification::centredLeft, true);

        r.removeFromTop (4);
        auto line = r.removeFromTop (16);
        line.removeFromRight (apply.getWidth() + more.getWidth() + 20);
        juce::String words = juce::String (listing.inputCount) + (listing.inputCount == 1 ? " input" : " inputs")
                           + "   " + Glyph::dot() + "   needs " + juce::String (listing.channelsNeeded) + " channels";
        if (listing.deviceName.isNotEmpty()) words += "   " + Glyph::dot() + "   " + listing.deviceName;
        if (listing.note.isNotEmpty()) words += "   " + Glyph::dot() + "   " + listing.note;
        g.setColour (tooBig() ? Dine::warn : Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawText (g, tooBig() ? words + "   " + Glyph::dot() + "   more channels than this device has"
                                    : words,
                        line, juce::Justification::centredLeft, true);

        r.removeFromTop (6);
        std::vector<Dine::BarSlice> slices;
        const float total = juce::jmax (1.0f, float (listing.inputCount));
        for (int b = 0; b < int (MixBus::Count); ++b)
            if (listing.perBus[size_t (b)] > 0)
                slices.push_back ({ float (listing.perBus[size_t (b)]) / total, Dine::busTint (MixBus (b)) });
        if (! slices.empty()) Dine::drawStackedBar (g, r.removeFromTop (5), slices);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 0);
        auto row = r.withSizeKeepingCentre (r.getWidth(), Dine::Metric::control);
        more.setBounds (row.removeFromRight (juce::jmax (40, more.idealWidth())));
        row.removeFromRight (8);
        apply.setBounds (row.removeFromRight (juce::jmax (72, apply.idealWidth())));
    }

private:
    InputMapStore::Listing listing;
    int available = 0;
    DineButton apply { "Apply", DineButton::Style::Standard };
    DineButton more { juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")), DineButton::Style::Ghost };
};

// ------------------------------------------------------------------ RoutingPage
RoutingPage::RoutingPage (MixController& c, AppServices& s) : controller (c), services (s)
{
    for (int i = 0; i < int (Section::Count); ++i)
    {
        nav[size_t (i)] = std::make_unique<DineNavItem> (sectionName (Section (i)));
        nav[size_t (i)]->onClick = [this, i] { setSection (Section (i)); if (onSection) onSection (Section (i)); };
        addAndMakeVisible (*nav[size_t (i)]);
    }

    outputs = std::make_unique<OutputsSheet> (controller, services);
    outputs->setEmbedded (true);
    outputs->onToast = [this] (const juce::String& t) { if (onToast) onToast (t); };
    outputs->onChooseDevice = [this] (const juce::String& d) { if (onChooseOutputDevice) onChooseOutputDevice (d); };
    addChildComponent (*outputs);

    mapsView.setViewedComponent (&mapsHolder, false);
    Dine::nativeScrolling (mapsView);
    mapsView.setScrollBarsShown (true, false);
    addChildComponent (mapsView);

    saveMapButton.setFontPx (12.5f);
    saveMapButton.onClick = [this] { if (onSaveMap) onSaveMap(); };
    importMapButton.setFontPx (12.5f);
    importMapButton.onClick = [this] { if (onImportMap) onImportMap(); };
    addChildComponent (saveMapButton);
    addChildComponent (importMapButton);

    unlockButton.setFontPx (12.5f);
    unlockButton.setTooltip ("LIVE SAFE stays on. This only unlocks this screen, and only until you leave it.");
    unlockButton.onClick = [this]
    {
        confirmForTest();
        if (onToast) onToast ("Routing unlocked for this visit. LIVE SAFE is still on everywhere else.");
    };
    addChildComponent (unlockButton);

    setSection (Section::Device);
}

RoutingPage::~RoutingPage() = default;

const char* RoutingPage::sectionName (Section s) noexcept
{
    switch (s)
    {
        case Section::Device:  return "Audio device";
        case Section::Inputs:  return "Inputs";
        case Section::Purpose: return "Purpose and sound";
        case Section::Outputs: return "Outputs and monitoring";
        case Section::Maps:    return "Saved input patches";
        case Section::Count:
        default:               return "?";
    }
}

void RoutingPage::setSection (Section s)
{
    section = s;
    coverShown = isCovered();
    for (int i = 0; i < int (Section::Count); ++i)
        nav[size_t (i)]->setSelected (i == int (s));

    const bool covered = isCovered();
    outputs->setVisible (s == Section::Outputs && ! covered);
    mapsView.setVisible (s == Section::Maps && ! covered);
    saveMapButton.setVisible (s == Section::Maps && ! covered);
    importMapButton.setVisible (s == Section::Maps && ! covered);
    unlockButton.setVisible (covered);
    if (s == Section::Maps) rebuildMaps();
    if (s == Section::Outputs) outputs->refresh();
    resized();
    repaint();
}

bool RoutingPage::isCovered() const { return services.daw().isLiveSafe() && ! confirmed; }

void RoutingPage::confirmForTest()
{
    confirmed = true;
    setSection (section);
    if (onCoverChanged) onCoverChanged();
}

void RoutingPage::resetConfirmation()
{
    if (! confirmed) return;
    confirmed = false;
    setSection (section);
}

void RoutingPage::refresh()
{
    // The cover follows LIVE SAFE, which can be switched on from anywhere - the toolbar, the
    // LIVE page, a keyboard shortcut - so it is re-read rather than set once on the way in.
    if (const bool covered = isCovered(); covered != coverShown)
    {
        coverShown = covered;
        setSection (section);
        if (onCoverChanged) onCoverChanged();
    }

    if (section == Section::Outputs && outputs->isVisible()) outputs->refresh();
    if (section == Section::Maps && mapsView.isVisible())
    {
        const auto device = services.currentInputDevice();
        if (device != lastDevice) { lastDevice = device; rebuildMaps(); }
    }
    for (int i = 0; i < int (Section::Count); ++i)
    {
        juce::String meta;
        switch (Section (i))
        {
            case Section::Device:  meta = services.isAudioRunning() ? juce::String (Glyph::check()) : juce::String(); break;
            case Section::Inputs:  meta = controller.getSession().inputs.empty()
                                             ? juce::String() : juce::String (int (controller.getSession().inputs.size())); break;
            case Section::Outputs: meta = juce::String (juce::jmax (1, controller.getOutputFeeds().count)); break;
            case Section::Maps:    meta = maps.isEmpty() ? juce::String() : juce::String (maps.size()); break;
            default: break;
        }
        nav[size_t (i)]->setMeta (meta);
    }
}

// The three hosted pages carry their own title and their own sentence - saying "Audio device"
// twice, once above the other, is what a shell does when it does not trust the thing inside
// it. So the head is only drawn over the two sections ROUTING owns.
int RoutingPage::headHeight() const noexcept { return hostsAPage() ? 0 : kHeadH; }

juce::Rectangle<int> RoutingPage::contentBounds() const
{
    if (isCovered()) return {};        // nothing is reachable until somebody says they mean it
    return getLocalBounds().withTrimmedLeft (kNavW).withTrimmedTop (headHeight());
}

void RoutingPage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);

    // the section list
    auto navArea = getLocalBounds().removeFromLeft (kNavW);
    Dine::drawPanelGround (g, navArea);
    g.setColour (Dine::hair);
    g.fillRect (navArea.removeFromRight (1));
    Dine::drawCaption (g, { kPadX / 2 + 8, kPadY, kNavW - 24, 14 }, "SET-UP AND ROUTING");

    // the head over the two sections ROUTING owns; the hosted pages carry their own
    if (headHeight() > 0)
    {
        auto head = getLocalBounds().withTrimmedLeft (kNavW).removeFromTop (headHeight()).reduced (kPadX, 0);
        auto title = head.withSizeKeepingCentre (head.getWidth(), 40);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (18.0f, 600));
        const juce::String name (sectionName (section));
        const int nameW = Dine::textWidth (Dine::text (18.0f, 600), name);
        Dine::drawText (g, name, title.removeFromLeft (nameW), juce::Justification::centredLeft);
        title.removeFromLeft (16);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.5f));
        Dine::drawText (g, section == Section::Outputs
                               ? "Where the mix leaves this Mac, and where the engineer listens. Monitoring only: "
                                 "nothing here changes the mix."
                               : "The patches this church has saved. A patch is the inputs and nothing else - never a mix.",
                        title, juce::Justification::centredLeft, true);
        g.setColour (Dine::hair);
        g.fillRect (getLocalBounds().withTrimmedLeft (kNavW).removeFromTop (headHeight()).removeFromBottom (1));
    }

    if (isCovered())
    {
        auto r = getLocalBounds().withTrimmedLeft (kNavW).withTrimmedTop (headHeight()).reduced (kPadX, kPadY);
        auto card = r.withHeight (juce::jmin (r.getHeight(), 168));
        Dine::drawCard (g, card.toFloat(), Dine::refuse, Dine::warn);
        auto in = card.reduced (24, 22);
        g.setColour (Dine::warn);
        g.setFont (Dine::caps (11.5f, 0.10f));
        Dine::drawText (g, "LIVE SAFE IS ON", in.removeFromTop (14), juce::Justification::centredLeft);
        in.removeFromTop (10);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (14.0f));
        Dine::drawFittedText (g, "The device, the patch and the output feeds are the three ways to silence a room in the "
                                 "middle of a service. Nothing on this screen can be changed until you say you mean it, "
                                 "and it locks itself again when you leave.",
                              in.removeFromTop (juce::jmax (20, in.getHeight() - Dine::Metric::button - 14)),
                              juce::Justification::topLeft, 3, 1.0f);
        return;
    }

    if (section == Section::Maps && maps.isEmpty())
    {
        auto r = getLocalBounds().withTrimmedLeft (kNavW).withTrimmedTop (headHeight()).reduced (kPadX, kPadY);
        r.removeFromBottom (Dine::Metric::button + 14);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        Dine::drawFittedText (g, "No patches saved yet. A church patches the same desk the same way every Sunday; save "
                                 "this session's patch once and every later session starts from it.",
                              r.removeFromTop (60), juce::Justification::topLeft, 3);
    }
}

void RoutingPage::resized()
{
    auto navArea = getLocalBounds().removeFromLeft (kNavW).reduced (8, 0);
    navArea.removeFromTop (kPadY + 14 + 10);
    for (auto& item : nav)
    {
        item->setBounds (navArea.removeFromTop (34));
        navArea.removeFromTop (2);
    }

    auto body = getLocalBounds().withTrimmedLeft (kNavW).withTrimmedTop (headHeight()).reduced (kPadX, kPadY);

    if (isCovered())
    {
        auto card = body.withHeight (juce::jmin (body.getHeight(), 168)).reduced (24, 22);
        const int w = juce::jmax (180, unlockButton.idealWidth());
        unlockButton.setBounds (card.removeFromBottom (Dine::Metric::button).removeFromLeft (w));
        return;
    }

    if (section == Section::Outputs) outputs->setBounds (body);
    else if (section == Section::Maps)
    {
        auto foot = body.removeFromBottom (Dine::Metric::button);
        saveMapButton.setBounds (foot.removeFromLeft (juce::jmax (200, saveMapButton.idealWidth())));
        foot.removeFromLeft (10);
        importMapButton.setBounds (foot.removeFromLeft (juce::jmax (130, importMapButton.idealWidth())));
        body.removeFromBottom (14);
        mapsView.setBounds (body);
        layoutMaps();
    }
}

void RoutingPage::rebuildMaps()
{
    maps = InputMapStore::list();
    mapRows.clear();
    mapsHolder.removeAllChildren();
    lastDevice = services.currentInputDevice();
    const int available = services.numInputChannels();

    for (const auto& listing : maps)
    {
        auto row = std::make_unique<MapRow> (listing, available);
        const auto file = listing.file;
        row->applyButton().onClick = [this, file] { if (onApplyMap) onApplyMap (file); };
        row->moreButton().onClick = [this, file, name = listing.name] { showMapMenu (file, name); };
        mapsHolder.addAndMakeVisible (*row);
        mapRows.push_back (std::move (row));
    }
    layoutMaps();
    repaint();
}

int RoutingPage::mapsHeight() const { return int (mapRows.size()) * (kMapRowH + 8); }

void RoutingPage::layoutMaps()
{
    const int w = juce::jmax (40, mapsView.getWidth() - (mapsView.isVerticalScrollBarShown() ? 10 : 0));
    mapsHolder.setSize (w, juce::jmax (1, mapsHeight()));
    int y = 0;
    for (auto& row : mapRows)
    {
        row->setBounds (0, y, w, kMapRowH);
        y += kMapRowH + 8;
    }
}

// The rest of what a saved patch can have done to it. A submenu inside a submenu is where
// these lived; here they hang off the row they are about.
void RoutingPage::showMapMenu (const juce::File& file, const juce::String& name)
{
    juce::PopupMenu m;
    m.addItem (1, "Rename" + juce::String (Glyph::ellip()));
    m.addItem (2, "Duplicate");
    m.addItem (3, "Export" + juce::String (Glyph::ellip()));
    m.addSeparator();
    m.addItem (4, "Delete");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (200),
                     [this, file, name] (int chosen)
    {
        if (chosen <= 0) return;
        switch (chosen)
        {
            case 1:
                askForName ("Rename patch", name, [this, name] (const juce::String& to)
                {
                    const auto problem = InputMapStore::rename (name, to);
                    if (onToast) onToast (problem.isEmpty() ? "Renamed to \"" + to + "\"." : problem);
                    rebuildMaps();
                });
                break;
            case 2:
            {
                const auto problem = InputMapStore::duplicate (name, name + " copy");
                if (onToast) onToast (problem.isEmpty() ? "Duplicated \"" + name + "\"." : problem);
                rebuildMaps();
                break;
            }
            case 3:
            {
                InputMap map;
                if (! InputMapStore::load (file, map)) { if (onToast) onToast ("That patch could not be read."); return; }
                chooser = std::make_unique<juce::FileChooser> ("Export \"" + name + "\"",
                                                               juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                   .getChildFile (juce::File::createLegalFileName (name) + ".dlivemap.json"),
                                                               "*.dlivemap.json");
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                      [this, map] (const juce::FileChooser& fc)
                                      {
                                          const auto to = fc.getResult();
                                          if (to == juce::File()) return;
                                          if (onToast) onToast (InputMapStore::saveAs (map, to) ? "Exported to " + to.getFileName()
                                                                                                 : juce::String ("That patch could not be written."));
                                      });
                break;
            }
            case 4:
            default:
                if (onToast) onToast (InputMapStore::remove (name) ? "Deleted \"" + name + "\"."
                                                                   : juce::String ("That patch could not be deleted."));
                rebuildMaps();
                break;
        }
    });
}

void RoutingPage::askForName (const juce::String& title, const juce::String& current,
                              std::function<void (const juce::String&)> done)
{
    nameDialog = std::make_unique<juce::AlertWindow> (title, "", juce::MessageBoxIconType::NoIcon);
    nameDialog->addTextEditor ("name", current, "Name");
    nameDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    nameDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, done] (int result)
    {
        if (nameDialog == nullptr) return;
        const auto typed = nameDialog->getTextEditorContents ("name").trim();
        nameDialog.reset();
        if (result == 1 && typed.isNotEmpty() && done) done (typed);
    }), false);
}

} // namespace livemix
