#include "OutputsSheet.h"
#include "UI/Widgets.h"

namespace livemix
{

namespace
{
    constexpr int kRowH = 52;
    constexpr int kCardW = 820;

    juce::String db1 (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1);
    }

    const char* feedName (int index)
    {
        switch (index)
        {
            case 0:  return "Broadcast";
            case 1:  return "Feed 2";
            case 2:  return "Feed 3";
            default: return "Feed 4";
        }
    }

}

// ------------------------------------------------------------------ Row
class OutputsSheet::Row : public juce::Component
{
public:
    Row (OutputsSheet& owner, int index) : sheet (owner), feedIndex (index)
    {
        addAndMakeVisible (sourceButton);
        addAndMakeVisible (pairButton);
        addAndMakeVisible (levelSlider);
        addAndMakeVisible (monoButton);
        addAndMakeVisible (muteButton);
        addChildComponent (removeButton);

        sourceButton.setTooltip ("What goes out here: the finished mix, one group on its own, or your "
                                 "headphones - whatever you have soloed. Nothing you solo is ever heard by "
                                 "the room or the stream. (Engineers: the monitor / solo bus, PFL or AFL.)");
        pairButton.setTooltip ("Which pair of the device's outputs it leaves by.");
        levelSlider.setTooltip ("Monitoring level for this output. It never changes the mix.");
        monoButton.setTooltip ("Sum to mono: a single fill speaker, or a phone feed.");
        muteButton.setTooltip ("Silence this output.");
        removeButton.setTooltip ("Stop sending this feed");
        removeButton.setQuiet (true);
        removeButton.setPadX (4);

        levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        levelSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        Dine::dragOnly (levelSlider);
        levelSlider.setRange (-60.0, 12.0, 0.1);
        levelSlider.setDoubleClickReturnValue (true, 0.0);
        levelSlider.getProperties().set ("dineFader", true);
        levelSlider.onValueChange = [this]
        {
            if (updating) return;
            feed.gainDb = float (levelSlider.getValue());
            sheet.commit();
            repaint();
        };

        for (auto* b : { &monoButton, &muteButton, &removeButton })
        {
            b->setFontPx (10.5f);
            b->setPadX (6);
            b->setClickingTogglesState (false);
        }
        monoButton.onClick = [this] { feed.mono = ! feed.mono; sheet.commit(); sheet.refresh(); };
        muteButton.onClick = [this] { feed.mute = ! feed.mute; sheet.commit(); sheet.refresh(); };
        removeButton.onClick = [this] { sheet.removeFeed (feedIndex); };
        sourceButton.onClick = [this] { chooseSource(); };
        pairButton.onClick = [this] { choosePair(); };
    }

    void set (const OutputFeed& f, bool canRemove)
    {
        updating = true;
        feed = f;
        levelSlider.setValue (f.gainDb, juce::dontSendNotification);
        sourceButton.setValue (f.monitor ? juce::String ("MONITOR BUS")
                                         : (f.source == MixBus::Master ? juce::String ("MASTER") : juce::String (mixBusName (f.source)).toUpperCase()));
        sourceButton.setDot (f.monitor ? Dine::monitor : Dine::busTint (f.source));
        pairButton.setValue (sheet.pairName (f.left < 0 ? -1 : f.left / 2));
        monoButton.setStyle (DineButton::Style::Toggle);
        monoButton.setToggleState (f.mono, juce::dontSendNotification);
        monoButton.setCaps (true); muteButton.setCaps (true);
        monoButton.setFontPx (10.0f); muteButton.setFontPx (10.0f);
        // A mute is amber wherever it is pressed, so a muted feed reads as muted and not
        // as something DLIVE is doing.
        muteButton.setTint (Dine::keyMute);
        muteButton.setStyle (f.mute ? DineButton::Style::Filled : DineButton::Style::Standard);
        monoButton.setButtonText ("MONO");
        // The broadcast and the engineer's listen are always a real stereo pair, so the switch
        // is not offered on them - a mix that reaches the stream summed to mono is the kind of
        // fault nobody notices until it is on the recording. The extra feeds keep it, because
        // that is what it is for: one fill speaker, a feed to a phone.
        const bool alwaysStereo = feedIndex == 0 || f.monitor;
        monoButton.setEnabled (! alwaysStereo);
        monoButton.setTooltip (alwaysStereo ? "Always stereo: the broadcast and your own listen are never summed."
                                            : "Sum this output to mono - for a single fill speaker or a feed to a phone.");
        muteButton.setButtonText ("MUTE");
        removeButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")));
        removeButton.setVisible (canRemove);
        updating = false;
        repaint();
    }

    const OutputFeed& get() const noexcept { return feed; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Dine::fillRounded (g, r, Dine::item, Dine::Radius::control);
        auto inner = getLocalBounds().reduced (12, 0);
        g.setColour (feed.mute ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (13.0f));
        g.drawText (feed.monitor ? juce::String ("Monitor") : juce::String (feedName (feedIndex)), inner.removeFromLeft (150), juce::Justification::centredLeft);

        g.setColour (feed.mute ? Dine::ink4 : Dine::ink2);
        g.setFont (Dine::mono (11.0f, 500));
        g.drawText (db1 (feed.gainDb), levelRect.withTrimmedLeft (levelRect.getWidth() - 54), juce::Justification::centredRight);

        if (feed.left >= 0 && ! sheet.pairExists (feed.left / 2))
        {
            g.setColour (Dine::warn);
            g.setFont (Dine::text (10.0f));
            g.drawText ("not on this device", pairButton.getBounds().withY (pairButton.getBottom()).withHeight (11),
                        juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 0).withTrimmedLeft (150);
        auto line = r.withSizeKeepingCentre (r.getWidth(), Dine::Metric::control);

        removeButton.setBounds (line.removeFromRight (24).withSizeKeepingCentre (24, 24));
        line.removeFromRight (6);
        muteButton.setBounds (line.removeFromRight (juce::jmax (48, muteButton.idealWidth())));
        line.removeFromRight (6);
        monoButton.setBounds (line.removeFromRight (juce::jmax (48, monoButton.idealWidth())));
        line.removeFromRight (16);

        sourceButton.setBounds (line.removeFromLeft (178));
        line.removeFromLeft (12);
        pairButton.setBounds (line.removeFromLeft (158));
        line.removeFromLeft (12);

        levelRect = line;
        levelSlider.setBounds (line.withTrimmedRight (60));
    }

private:
    static constexpr int kMonitorId = 900;

    void chooseSource()
    {
        juce::PopupMenu m;
        m.addItem (int (MixBus::Master) + 1, "Main mix", true, ! feed.monitor && feed.source == MixBus::Master);
        m.addSeparator();
        for (int b = 0; b < int (MixBus::Master); ++b)
        {
            const auto bus = MixBus (b);
            const bool used = sheet.busAvailable (bus);
            m.addItem (b + 1, sentence (mixBusName (bus)), used, ! feed.monitor && feed.source == bus);
        }
        // The engineer's own listen. It is the one entry here that is not part of the
        // broadcast, so it gets its own section and says what it is for.
        m.addSeparator();
        m.addSectionHeader ("Just for you");
        m.addItem (kMonitorId, "My headphones (whatever is soloed)", true, feed.monitor);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sourceButton)
                             .withMinimumWidth (sourceButton.getWidth()),
                         [this] (int id)
                         {
                             if (id <= 0) return;
                             if (id == kMonitorId) feed.monitor = true;
                             else { feed.monitor = false; feed.source = MixBus (id - 1); }
                             sheet.commit();
                             sheet.refresh();
                         });
    }

    void choosePair()
    {
        juce::PopupMenu m;
        const int pairs = sheet.numPairs();
        for (int p = 0; p < juce::jmax (1, pairs); ++p)
            m.addItem (p + 1, sheet.pairName (p), true, feed.left >= 0 && feed.left / 2 == p);
        m.addSeparator();
        m.addItem (900, "Not routed", true, feed.left < 0);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pairButton)
                             .withMinimumWidth (pairButton.getWidth()),
                         [this] (int id)
                         {
                             if (id <= 0) return;
                             if (id == 900) { feed.left = -1; feed.right = -1; }
                             else { feed.left = (id - 1) * 2; feed.right = feed.left + 1; }
                             sheet.commit();
                             sheet.refresh();
                         });
    }

    static juce::String sentence (const juce::String& s)
    {
        return s.substring (0, 1).toUpperCase() + s.substring (1).toLowerCase();
    }

    OutputsSheet& sheet;
    int feedIndex;
    OutputFeed feed;
    bool updating = false;
    juce::Rectangle<int> levelRect;
    DinePopup sourceButton, pairButton;
    juce::Slider levelSlider;
    DineButton monoButton { "MONO", DineButton::Style::Toggle };
    DineButton muteButton { "MUTE", DineButton::Style::Standard };
    DineButton removeButton { "x", DineButton::Style::Standard };
};

// ------------------------------------------------------------------ OutputsSheet
OutputsSheet::OutputsSheet (MixController& c, AppServices& s) : controller (c), services (s)
{
    for (int i = 0; i < kMaxOutputFeeds; ++i)
    {
        rows[size_t (i)] = std::make_unique<Row> (*this, i);
        addChildComponent (*rows[size_t (i)]);
    }

    addAndMakeVisible (deviceButton);
    deviceButton.setTooltip ("The device the stream and the room hear. Always a stereo pair, on its outputs 1-2.");
    deviceButton.onClick = [this] { chooseDevice(); };

    addAndMakeVisible (addButton);
    addButton.setTooltip ("Send the mix, or one group, to another pair of outputs as well.");
    addButton.onClick = [this] { addFeed(); };

    // The second of the two choices this sheet exists for: which device the engineer listens
    // on. Picking a different one from the broadcast is allowed and is the normal case - DLIVE
    // joins the two underneath, and the words "aggregate device" never appear.
    addAndMakeVisible (soloDeviceButton);
    soloDeviceButton.setTooltip ("The device you listen on. Solo a channel and it comes out here - the room and "
                                 "the stream never hear it. It can be a different box from the broadcast; DLIVE "
                                 "joins them for you.");
    soloDeviceButton.onClick = [this] { chooseSoloDevice(); };

    addAndMakeVisible (doneButton);
    doneButton.onClick = [this] { if (onClose) onClose(); };

    refresh();
}

OutputsSheet::~OutputsSheet() = default;

int OutputsSheet::numPairs() const { return juce::jmax (1, channels / 2); }

bool OutputsSheet::pairExists (int pair) const { return pair >= 0 && pair < channels / 2; }

bool OutputsSheet::busAvailable (MixBus bus) const
{
    return controller.isPrepared() && controller.getEngine().isBusUsed (bus);
}

juce::String OutputsSheet::pairName (int pair) const
{
    if (pair < 0) return "Not routed";
    const auto names = services.outputChannelNames();
    const int l = pair * 2, r = l + 1;
    if (names.size() > r && names[l].isNotEmpty() && names[r].isNotEmpty())
        return names[l] + " / " + names[r];
    return "Outputs " + juce::String (l + 1) + "-" + juce::String (r + 1);
}

// Which device the engineer listens on. Every real output device is offered, including the one
// already carrying the broadcast (that is the four-output-interface case: the stream on 1-2 and
// solo on 3-4). Choosing a different box is the normal case and costs the user nothing to know
// about - the host joins the two.
void OutputsSheet::chooseSoloDevice()
{
    showSoloDeviceMenu (services, soloDeviceButton, [this] (const juce::String& message)
    {
        if (onToast) onToast (message);
        refresh();
    });
}

void OutputsSheet::showSoloDeviceMenu (AppServices& services, juce::Component& anchor, std::function<void (const juce::String&)> done)
{
    const auto current = services.soloOutputDevice();
    const auto broadcast = services.broadcastOutputDevice();

    juce::PopupMenu m;
    m.addItem (1, "Nowhere - I do not need solo", true, current.isEmpty());
    m.addSeparator();

    juce::StringArray names;
    for (const auto& d : services.outputDevices())
    {
        if (d.outputChannels <= 0) continue;
        if (d.name.startsWith ("DLIVE Monitoring")) continue;   // one DLIVE made: not a building block
        names.add (d.name);
    }

    int id = 100;
    for (const auto& name : names)
    {
        const bool same = name == broadcast;
        // The broadcast device itself only works when it has a second pair to spare.
        bool usable = true;
        if (same)
        {
            usable = false;
            for (const auto& d : services.outputDevices())
                if (d.name == name && d.outputChannels >= 4) usable = true;
        }
        m.addItem (id++, same ? name + "   (on its outputs 3-4)" : name, usable, name == current);
    }
    if (names.isEmpty()) m.addItem (-1, "No output devices found", false, false);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor).withMinimumWidth (320),
                     [&services, names, done] (int chosen)
                     {
                         if (chosen <= 0) return;
                         const juce::String wanted = chosen == 1 ? juce::String() : names[chosen - 100];
                         const auto result = services.setSoloOutputDevice (wanted);
                         if (done) done (result.message);
                     });
}

void OutputsSheet::refresh()
{
    channels = services.numOutputChannels();
    const auto solo = services.soloOutputDevice();
    soloDeviceButton.setValue (solo.isEmpty() ? juce::String ("Nowhere yet") : solo);
    soloDeviceButton.setEnabled (services.isAudioRunning());
    const auto& feeds = controller.getOutputFeeds();
    const int count = juce::jlimit (1, kMaxOutputFeeds, feeds.count);
    for (int i = 0; i < kMaxOutputFeeds; ++i)
    {
        const bool used = i < count;
        rows[size_t (i)]->setVisible (used);
        if (used) rows[size_t (i)]->set (feeds.feeds[size_t (i)], i > 0);
    }
    // While a combined device is open the *open* device is DLIVE's own; what the user chose
    // is the broadcast device, and that is what this has to say.
    const auto broadcast = services.broadcastOutputDevice();
    deviceButton.setValue (broadcast.isEmpty() ? "None" : broadcast);
    addButton.setEnabled (count < kMaxOutputFeeds && channels >= 2);
    resized();
    repaint();
}

void OutputsSheet::commit()
{
    OutputFeeds feeds;
    int n = 0;
    for (int i = 0; i < kMaxOutputFeeds; ++i)
        if (rows[size_t (i)]->isVisible()) feeds.feeds[size_t (n++)] = rows[size_t (i)]->get();
    feeds.count = juce::jmax (1, n);
    controller.setOutputFeeds (feeds);
}

void OutputsSheet::addFeed()
{
    auto feeds = controller.getOutputFeeds();
    if (feeds.count >= kMaxOutputFeeds) return;
    // The next free pair, so a second output is one click rather than one click and a menu.
    int pair = 1;
    for (; pair < numPairs(); ++pair)
    {
        bool taken = false;
        for (int i = 0; i < feeds.count; ++i) taken = taken || feeds.feeds[size_t (i)].left / 2 == pair;
        if (! taken) break;
    }
    if (pair >= numPairs()) pair = juce::jmax (0, numPairs() - 1);

    OutputFeed added;
    added.left = pair * 2;
    added.right = added.left + 1;
    added.source = MixBus::Master;
    feeds.feeds[size_t (feeds.count)] = added;
    ++feeds.count;
    controller.setOutputFeeds (feeds);
    refresh();
    if (onToast) onToast ("The mix now also goes to " + pairName (pair) + ".");
}

void OutputsSheet::removeFeed (int index)
{
    auto feeds = controller.getOutputFeeds();
    if (index <= 0 || index >= feeds.count) return;
    for (int i = index; i + 1 < feeds.count; ++i) feeds.feeds[size_t (i)] = feeds.feeds[size_t (i + 1)];
    --feeds.count;
    controller.setOutputFeeds (feeds);
    refresh();
}

void OutputsSheet::chooseDevice()
{
    const auto devices = services.outputDevices();
    if (devices.isEmpty()) { if (onToast) onToast ("No output devices found."); return; }
    juce::PopupMenu m;
    const auto current = services.currentOutputDevice();
    for (int i = 0; i < devices.size(); ++i)
        m.addItem (i + 1, devices[i].name + "   " + juce::String (devices[i].outputChannels) + " out",
                   true, devices[i].name == current);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (deviceButton).withMinimumWidth (deviceButton.getWidth()),
                     [this, devices] (int id)
                     {
                         if (id <= 0 || id > devices.size()) return;
                         if (onChooseDevice) onChooseDevice (devices[id - 1].name);
                         refresh();
                     });
}

// ------------------------------------------------------------------ geometry
juce::Rectangle<int> OutputsSheet::cardBounds() const
{
    const int count = juce::jlimit (1, kMaxOutputFeeds, controller.getOutputFeeds().count);
    const int h = 26 + 24 + 12 + Dine::Metric::control + 8 + Dine::Metric::control + 16 + 36 + count * (kRowH + 4) + 14 + Dine::Metric::button + 14 + 40 + 26;
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (kCardW, getWidth() - 60), juce::jmin (h, getHeight() - 40));
}

void OutputsSheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.84f));

    auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), 14.0f);

    auto r = card.reduced (26, 26);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (19.0f, 600));
    g.drawText ("Outputs", r.removeFromTop (24), juce::Justification::centredLeft);
    r.removeFromTop (12);

    // The two choices this sheet exists for: where the broadcast goes, and where the engineer listens.
    const int labelW = 96;
    {
        auto line = r.removeFromTop (Dine::Metric::control);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f));
        g.drawText ("Broadcast", line.removeFromLeft (labelW), juce::Justification::centredLeft);
        auto after = line.withTrimmedLeft (300 + 12);
        g.setColour (channels >= 2 ? Dine::ink3 : Dine::warn);
        g.setFont (Dine::text (12.0f));
        g.drawText (channels <= 0 ? "no device open"
                                  : juce::String (channels) + (channels == 1 ? " output channel" : " output channels")
                                        + "   " + Glyph::dot() + "   always stereo, on 1-2",
                    after, juce::Justification::centredLeft, true);
        r.removeFromTop (8);
    }
    {
        auto line = r.removeFromTop (Dine::Metric::control);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f));
        g.drawText ("Solo", line.removeFromLeft (labelW), juce::Justification::centredLeft);
        auto after = line.withTrimmedLeft (300 + 12);
        const bool set = services.soloOutputDevice().isNotEmpty();
        g.setColour (set ? Dine::ok : Dine::ink4);
        g.setFont (Dine::text (12.0f));
        g.drawText (set ? "only you hear this" : "solo has nowhere to go yet", after, juce::Justification::centredLeft, true);
        r.removeFromTop (16);
    }

    // The table's headings.
    auto head = r.removeFromTop (36).withTrimmedBottom (10).reduced (12, 0);
    g.setColour (Dine::ink4);
    g.setFont (Dine::caps (11.0f, 0.06f, 500));
    g.drawText ("FEED", head.removeFromLeft (150), juce::Justification::centredLeft);
    g.drawText ("SOURCE", head.removeFromLeft (190), juce::Justification::centredLeft);
    g.drawText ("DESTINATION", head.removeFromLeft (170), juce::Justification::centredLeft);
    g.drawText ("STATE", head.removeFromRight (158), juce::Justification::centredRight);
    g.drawText ("LEVEL", head, juce::Justification::centredLeft);

    // Along the foot: how many feeds, and what is set up for the engineer, in one sentence.
    auto foot = card.reduced (26, 26).removeFromBottom (40 + Dine::Metric::button);
    auto count = foot.removeFromTop (Dine::Metric::button);
    count.removeFromLeft (juce::jmax (120, addButton.getWidth()) + 12);
    g.setColour (Dine::ink4);
    g.setFont (Dine::text (13.0f));
    g.drawText (juce::String (controller.getOutputFeeds().count) + " of " + juce::String (kMaxOutputFeeds) + " feeds in use", count, juce::Justification::centredLeft);
    foot.removeFromTop (14);
    const auto headphones = services.headphonesSummary();
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.5f));
    g.drawFittedText (headphones.isNotEmpty()
                          ? headphones + " While LIVE SAFE is on the monitor bus cannot be re-routed: the room and the stream never hear it, and it never disappears on you."
                          : juce::String ("Pick the device you listen on above. It can be a different box from the broadcast - DLIVE joins them for you."),
                      foot, juce::Justification::topLeft, 2);
}

void OutputsSheet::resized()
{
    auto card = cardBounds();
    auto r = card.reduced (26, 26);
    r.removeFromTop (24 + 12);

    const int labelW = 96;
    auto broadcastLine = r.removeFromTop (Dine::Metric::control);
    broadcastLine.removeFromLeft (labelW);
    deviceButton.setBounds (broadcastLine.removeFromLeft (300));
    r.removeFromTop (8);
    auto soloLine = r.removeFromTop (Dine::Metric::control);
    soloLine.removeFromLeft (labelW);
    soloDeviceButton.setBounds (soloLine.removeFromLeft (300));
    r.removeFromTop (16);
    r.removeFromTop (36);

    auto foot = r.removeFromBottom (40 + Dine::Metric::button);
    auto actions = foot.removeFromTop (Dine::Metric::button);
    doneButton.setBounds (actions.removeFromRight (juce::jmax (80, doneButton.idealWidth())));
    addButton.setBounds (actions.removeFromLeft (juce::jmax (120, addButton.idealWidth())));
    r.removeFromBottom (14);

    for (int i = 0; i < kMaxOutputFeeds; ++i)
    {
        if (! rows[size_t (i)]->isVisible()) continue;
        rows[size_t (i)]->setBounds (r.removeFromTop (kRowH));
        r.removeFromTop (4);
    }
}

void OutputsSheet::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == this && ! cardBounds().contains (e.getPosition()) && onClose) onClose();
}

} // namespace livemix
