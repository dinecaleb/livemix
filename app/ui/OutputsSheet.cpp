#include "OutputsSheet.h"
#include "UI/Widgets.h"

namespace livemix
{

namespace
{
    constexpr int kRowH = 46;
    constexpr int kCardW = 780;

    juce::String db1 (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1);
    }

    const char* feedName (int index)
    {
        switch (index)
        {
            case 0:  return "Main";
            case 1:  return "Cue";
            case 2:  return "Feed 3";
            default: return "Feed 4";
        }
    }

    juce::Colour sourceTint (MixBus b) noexcept
    {
        switch (b)
        {
            case MixBus::Drums:  return Dine::warn;
            case MixBus::Bass:   return Dine::accent;
            case MixBus::Music:  return juce::Colour (0xff8fa2d8);
            case MixBus::Vocals: return Dine::ok;
            default:             return Dine::ink2;
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

        sourceButton.setTooltip ("What this output carries: the finished mix, or one group on its own.");
        pairButton.setTooltip ("Which pair of the device's outputs it leaves by.");
        levelSlider.setTooltip ("Monitoring level for this output. It never changes the mix.");
        monoButton.setTooltip ("Sum to mono: a single fill speaker, or a phone feed.");
        muteButton.setTooltip ("Silence this output.");
        removeButton.setTooltip ("Remove this output.");

        levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        levelSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
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
        sourceButton.setValue (f.source == MixBus::Master ? juce::String ("Main mix") : sentence (mixBusName (f.source)));
        pairButton.setValue (sheet.pairName (f.left < 0 ? -1 : f.left / 2));
        monoButton.setStyle (f.mono ? DineButton::Style::Filled : DineButton::Style::Standard);
        muteButton.setStyle (f.mute ? DineButton::Style::Filled : DineButton::Style::Standard);
        monoButton.setButtonText (f.mono ? "Mono" : "Stereo");
        muteButton.setButtonText (f.mute ? "Muted" : "Mute");
        removeButton.setButtonText ("Remove");
        removeButton.setVisible (canRemove);
        updating = false;
        repaint();
    }

    const OutputFeed& get() const noexcept { return feed; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Dine::drawCard (g, r, feed.mute ? Dine::card.darker (0.2f) : Dine::card);

        auto inner = getLocalBounds().reduced (12, 0);
        g.setColour (sourceTint (feed.source).withAlpha (feed.mute ? 0.3f : 0.9f));
        g.fillRoundedRectangle (float (inner.getX()), float (inner.getY()) + 9.0f, 3.0f,
                                float (inner.getHeight()) - 18.0f, 1.5f);
        inner.removeFromLeft (12);

        g.setColour (feed.mute ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (12.5f, 600));
        g.drawText (feedName (feedIndex), inner.removeFromLeft (52), juce::Justification::centredLeft);

        g.setColour (feed.mute ? Dine::ink4 : Dine::ink2);
        g.setFont (Dine::mono (11.0f, 500));
        g.drawText (db1 (feed.gainDb) + " dB", levelRect.withTrimmedLeft (levelRect.getWidth() - 56),
                    juce::Justification::centredRight);

        if (feed.left >= 0 && ! sheet.pairExists (feed.left / 2))
        {
            g.setColour (Dine::warn);
            g.setFont (Dine::text (10.5f, 600));
            g.drawText ("not on this device", pairButton.getBounds().withY (pairButton.getBottom() - 2).withHeight (12),
                        juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 0).withTrimmedLeft (12 + 52);
        auto line = r.withSizeKeepingCentre (r.getWidth(), Dine::Metric::control);

        removeButton.setBounds (line.removeFromRight (54));
        line.removeFromRight (6);
        muteButton.setBounds (line.removeFromRight (juce::jmax (54, muteButton.idealWidth())));
        line.removeFromRight (6);
        monoButton.setBounds (line.removeFromRight (juce::jmax (58, monoButton.idealWidth())));
        line.removeFromRight (12);

        sourceButton.setBounds (line.removeFromLeft (104));
        line.removeFromLeft (8);
        pairButton.setBounds (line.removeFromLeft (118));
        line.removeFromLeft (12);

        levelRect = line;
        levelSlider.setBounds (line.withTrimmedRight (58));
    }

private:
    void chooseSource()
    {
        juce::PopupMenu m;
        m.addItem (int (MixBus::Master) + 1, "Main mix", true, feed.source == MixBus::Master);
        m.addSeparator();
        for (int b = 0; b < int (MixBus::Master); ++b)
        {
            const auto bus = MixBus (b);
            const bool used = sheet.busAvailable (bus);
            m.addItem (b + 1, sentence (mixBusName (bus)), used, feed.source == bus);
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sourceButton)
                             .withMinimumWidth (sourceButton.getWidth()),
                         [this] (int id)
                         {
                             if (id <= 0) return;
                             feed.source = MixBus (id - 1);
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
    DineButton monoButton { "Stereo", DineButton::Style::Standard };
    DineButton muteButton { "Mute", DineButton::Style::Standard };
    DineButton removeButton { "-", DineButton::Style::Ghost };
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
    deviceButton.setTooltip ("The device the mix leaves by. Every one of its outputs is opened.");
    deviceButton.onClick = [this] { chooseDevice(); };

    addAndMakeVisible (addButton);
    addButton.setTooltip ("Send the mix, or one group, to another pair of outputs as well.");
    addButton.onClick = [this] { addFeed(); };

    addAndMakeVisible (aggregateButton);
    aggregateButton.setFontPx (11.5f);
    aggregateButton.setTooltip ("macOS can only play to one device at a time. An Aggregate Device joins two of "
                                "them into one, and DLIVE then shows all of its outputs here.");
    aggregateButton.onClick = [this]
    {
        for (const char* path : { "/System/Applications/Utilities/Audio MIDI Setup.app",
                                  "/Applications/Utilities/Audio MIDI Setup.app" })
        {
            juce::File app (path);
            if (app.exists() && app.startAsProcess()) return;
        }
        if (onToast) onToast ("Audio MIDI Setup could not be opened. It lives in Applications > Utilities.");
    };

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

void OutputsSheet::refresh()
{
    channels = services.numOutputChannels();
    const auto& feeds = controller.getOutputFeeds();
    const int count = juce::jlimit (1, kMaxOutputFeeds, feeds.count);
    for (int i = 0; i < kMaxOutputFeeds; ++i)
    {
        const bool used = i < count;
        rows[size_t (i)]->setVisible (used);
        if (used) rows[size_t (i)]->set (feeds.feeds[size_t (i)], i > 0);
    }
    deviceButton.setValue (services.currentOutputDevice().isEmpty() ? "None" : services.currentOutputDevice());
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
    const int h = 96 + 30 + count * (kRowH + 8) + 44 + 56;
    auto r = getLocalBounds().withSizeKeepingCentre (juce::jmin (kCardW, getWidth() - 60),
                                                     juce::jmin (h, getHeight() - 40));
    return r.withY (juce::jmax (20, r.getY() - 20));
}

void OutputsSheet::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0c0d0e).withAlpha (0.55f));

    auto card = cardBounds();
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 40, { 0, 16 }).drawForRectangle (g, card);
    Dine::drawSheet (g, card.toFloat(), Dine::Radius::window);

    auto r = card.reduced (26, 22);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (17.0f, 600));
    g.drawText ("Outputs", r.removeFromTop (22), juce::Justification::topLeft);
    r.removeFromTop (4);
    g.setColour (Dine::ink2);
    g.setFont (Dine::text (12.5f));
    g.drawFittedText ("Where the mix leaves this Mac. Each output carries what you choose, at its own level "
                      + Glyph::dash() + " none of it changes the mix or what you export.",
                      r.removeFromTop (34), juce::Justification::topLeft, 2);
    r.removeFromTop (12);

    // the device line
    auto deviceLine = r.removeFromTop (Dine::Metric::control);
    g.setColour (Dine::ink2);
    g.setFont (Dine::text (12.0f));
    g.drawText ("Device", deviceLine.removeFromLeft (62), juce::Justification::centredLeft);
    auto after = deviceLine.withTrimmedLeft (62 + 300 + 12 - 62);
    g.setColour (channels >= 2 ? Dine::ink3 : Dine::warn);
    g.setFont (Dine::text (11.5f));
    g.drawText (channels <= 0 ? "no device open"
                              : juce::String (channels) + (channels == 1 ? " output channel" : " output channels")
                                    + "   " + Glyph::dot() + "   " + juce::String (numPairs())
                                    + (numPairs() == 1 ? " pair" : " pairs"),
                after, juce::Justification::centredLeft, true);

    // the note about two devices at once, along the foot
    auto foot = card.reduced (26, 22).removeFromBottom (Dine::Metric::button);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.5f));
    g.drawText ("Two devices at once (an interface and the headphone jack) needs an Aggregate Device.",
                foot.withTrimmedRight (juce::jmax (90, doneButton.getWidth()) + 12).withTrimmedLeft (0),
                juce::Justification::centredLeft, true);
}

void OutputsSheet::resized()
{
    auto card = cardBounds();
    auto r = card.reduced (26, 22);
    r.removeFromTop (22 + 4 + 34 + 12);

    auto deviceLine = r.removeFromTop (Dine::Metric::control);
    deviceLine.removeFromLeft (62);
    deviceButton.setBounds (deviceLine.removeFromLeft (300));
    r.removeFromTop (16);

    auto foot = r.removeFromBottom (Dine::Metric::button);
    doneButton.setBounds (foot.removeFromRight (juce::jmax (90, doneButton.idealWidth())));
    r.removeFromBottom (10);

    auto aggregate = r.removeFromBottom (Dine::Metric::control);
    aggregateButton.setBounds (aggregate.removeFromLeft (juce::jmax (180, aggregateButton.idealWidth())));
    r.removeFromBottom (8);

    auto add = r.removeFromBottom (Dine::Metric::control);
    addButton.setBounds (add.removeFromLeft (juce::jmax (140, addButton.idealWidth())));
    r.removeFromBottom (10);

    for (int i = 0; i < kMaxOutputFeeds; ++i)
    {
        if (! rows[size_t (i)]->isVisible()) continue;
        rows[size_t (i)]->setBounds (r.removeFromTop (kRowH));
        r.removeFromTop (8);
    }
}

void OutputsSheet::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == this && ! cardBounds().contains (e.getPosition()) && onClose) onClose();
}

} // namespace livemix
