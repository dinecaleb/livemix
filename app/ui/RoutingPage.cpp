#include "RoutingPage.h"

namespace livemix
{

namespace
{
    constexpr int kHeadH   = 56;    // the page bar: the heading, the warning, the map, APPLY
    constexpr int kGutter  = 14;
    constexpr int kDeviceW = 320;   // the device list, the input access and the format
    constexpr int kOutputW = 330;   // the feeds and the monitoring
    constexpr int kMapMin  = 420;   // below this the input map has nowhere to be

    void caption (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour (Dine::ink4);
        g.setFont (Dine::Type::labelSection());
        Dine::drawText (g, text, r, juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------- the outputs column
//
// What leaves this Mac, and where the engineer's own listen goes. It states the feeds rather
// than editing them: a feed is four decisions (a pair, a source, mono, a level) and the
// Outputs sheet is where they are made. Stating them here is the point - the design's third
// column exists so that "where does the stream come out" is answered without opening anything.
class RoutingPage::OutputsColumn : public juce::Component
{
public:
    OutputsColumn (MixController& c, AppServices& s) : controller (c), services (s)
    {
        addAndMakeVisible (setUp);
        setUp.setTooltip ("Choose the pairs the broadcast and your headphones leave by.");
        setOpaque (false);
    }

    DineButton setUp { "Set up outputs...", DineButton::Style::Standard };

    void refresh()
    {
        Look next;
        const auto& feeds = controller.getOutputFeeds();
        for (int i = 0; i < feeds.count && i < kMaxOutputFeeds; ++i)
        {
            const auto& f = feeds.feeds[size_t (i)];
            Row row;
            row.title = i == 0 ? juce::String ("Broadcast")
                       : f.monitor ? juce::String ("Headphones")
                                   : juce::String (outputFeedSourceName (f));
            row.what = juce::String (outputFeedSourceName (f));
            row.where = f.routed()
                ? (f.mono ? "out " + juce::String (f.left + 1)
                          : "outs " + juce::String (f.left + 1) + "-" + juce::String (juce::jmax (f.left + 2, f.right + 1)))
                : juce::String ("not routed");
            row.note = i == 0 ? juce::String ("The stream. Never changes when you solo.")
                      : f.monitor ? juce::String ("Where solo goes.")
                                  : (f.mono ? juce::String ("Mono.") : juce::String());
            row.tint = f.monitor ? Dine::monitor : i == 0 ? Dine::accent : Dine::ink3;
            row.routed = f.routed();
            next.rows.push_back (row);
        }

        const auto& mon = controller.getMonitor();
        next.soloTo = services.headphonesSummary();
        if (next.soloTo.isEmpty()) next.soloTo = controller.hasMonitorOutput() ? "Set up" : "Not set up";
        next.soloPoint = mon.point == SoloPoint::PFL ? "Before fader" : "After fader";
        next.soloMode = mon.mode == SoloMode::InPlace ? "Everyone hears it" : "Only you hear it";
        next.speech = controller.getSpeechPriority() ? "On - the band steps back under speech"
                                                     : "Off";
        if (next != look) { look = next; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        caption (g, r.removeFromTop (14), "OUTPUTS");
        r.removeFromTop (8);

        for (const auto& row : look.rows)
        {
            auto card = r.removeFromTop (76);
            Dine::drawCard (g, card.toFloat(), Dine::card);
            // the lamp down the left edge: which feed this is
            g.setColour (row.routed ? row.tint : Dine::ink4);
            g.fillRect (float (card.getX()), float (card.getY() + 10), 2.0f, float (card.getHeight() - 20));

            auto inner = card.reduced (14, 10);
            g.setColour (Dine::ink);
            g.setFont (Dine::Type::headingCard());
            Dine::drawText (g, row.title, inner.removeFromTop (18), juce::Justification::centredLeft, true);
            g.setColour (row.routed ? Dine::ink3 : Dine::warn);
            g.setFont (Dine::Type::bodySmall());
            Dine::drawText (g, row.what + "   " + juce::String (Glyph::dash()) + "   " + row.where,
                        inner.removeFromTop (16), juce::Justification::centredLeft, true);
            if (row.note.isNotEmpty())
            {
                g.setColour (Dine::ink4);
                g.setFont (Dine::Type::caption());
                Dine::drawText (g, row.note, inner.removeFromTop (14), juce::Justification::centredLeft, true);
            }
            r.removeFromTop (8);
        }

        r.removeFromTop (setUp.getHeight() + 14);
        if (r.getHeight() < 90) return;

        caption (g, r.removeFromTop (14), "MONITORING");
        r.removeFromTop (8);
        auto card = r.removeFromTop (juce::jmin (r.getHeight(), 132));
        Dine::drawCard (g, card.toFloat(), Dine::card);
        auto inner = card.reduced (14, 10);
        auto row = [&] (const juce::String& k, const juce::String& v)
        {
            if (inner.getHeight() < 30) return;
            g.setColour (Dine::ink4);
            g.setFont (Dine::Type::caption());
            Dine::drawText (g, k, inner.removeFromTop (14), juce::Justification::centredLeft, true);
            g.setColour (Dine::ink2);
            g.setFont (Dine::Type::bodySmall());
            Dine::drawText (g, v, inner.removeFromTop (16), juce::Justification::centredLeft, true);
            inner.removeFromTop (2);
        };
        row ("Solo goes to", look.soloTo);
        row ("Solo point", look.soloPoint);
        row ("Speech priority", look.speech);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (14 + 8);
        for (size_t i = 0; i < look.rows.size(); ++i) r.removeFromTop (76 + 8);
        setUp.setBounds (r.removeFromTop (Dine::Metric::control));
    }

    int idealHeight() const
    {
        return 14 + 8 + int (look.rows.size()) * (76 + 8) + Dine::Metric::control + 14 + 14 + 8 + 132;
    }

private:
    struct Row
    {
        juce::String title, what, where, note;
        juce::Colour tint;
        bool routed = false;
        bool operator== (const Row& o) const
        {
            return title == o.title && what == o.what && where == o.where && note == o.note
                && tint == o.tint && routed == o.routed;
        }
    };
    struct Look
    {
        std::vector<Row> rows;
        juce::String soloTo, soloPoint, soloMode, speech;
        bool operator== (const Look& o) const
        {
            return rows == o.rows && soloTo == o.soloTo && soloPoint == o.soloPoint
                && soloMode == o.soloMode && speech == o.speech;
        }
        bool operator!= (const Look& o) const { return ! (*this == o); }
    };

    MixController& controller;
    AppServices& services;
    Look look;
};

// ---------------------------------------------------------------------- RoutingPage
RoutingPage::RoutingPage (MixController& c, AppServices& s, DevicePage& d, AssignPage& a)
    : controller (c), services (s), devicePage (d), assignPage (a)
{
    outputs = std::make_unique<OutputsColumn> (controller, services);
    addAndMakeVisible (*outputs);
    outputs->setUp.onClick = [this] { if (onOpenOutputs) onOpenOutputs(); };

    addAndMakeVisible (mapsButton);
    mapsButton.setTooltip ("Patch this console from a map you saved before.");
    mapsButton.onClick = [this] { if (onApplyMapping) onApplyMapping(); };

    addAndMakeVisible (saveMapButton);
    saveMapButton.setTooltip ("Keep this input map under a name, so the same console patches itself next Sunday.");
    saveMapButton.onClick = [this] { if (onSaveMapping) onSaveMapping(); };

    addAndMakeVisible (applyButton);
    applyButton.setTooltip ("Build the mix around these inputs.");
    applyButton.onClick = [this]
    {
        // Under LIVE SAFE this is exactly the kind of thing LIVE SAFE is for: the window owns
        // the question so it reads the same wherever it is asked.
        if (services.daw().isLiveSafe() && onConfirmUnderLiveSafe != nullptr
            && ! onConfirmUnderLiveSafe ("apply this routing"))
            return;
        services.reconfigure();
        if (onToast) onToast ("Routing applied.");
        if (onContinue) onContinue();
    };

    setOpaque (true);
}

RoutingPage::~RoutingPage() = default;

bool RoutingPage::showsInputMap() const noexcept { return ! mapArea.isEmpty(); }
bool RoutingPage::showsOutputs() const noexcept  { return ! outputArea.isEmpty(); }

void RoutingPage::measure()
{
    auto r = getLocalBounds();
    headArea = r.removeFromTop (kHeadH);
    r = r.reduced (Dine::Metric::padX, 0);
    r.removeFromBottom (14);

    // The columns give way from the outside in: the outputs first, then the device list, so
    // the input map - the thing somebody came here for - is the last thing to go.
    deviceArea = mapArea = outputArea = {};
    int available = r.getWidth();
    if (available >= kDeviceW + kGutter + kMapMin + kGutter + kOutputW)
    {
        deviceArea = r.removeFromLeft (kDeviceW);
        r.removeFromLeft (kGutter);
        outputArea = r.removeFromRight (kOutputW);
        r.removeFromRight (kGutter);
        mapArea = r;
    }
    else if (available >= kDeviceW + kGutter + kMapMin)
    {
        deviceArea = r.removeFromLeft (kDeviceW);
        r.removeFromLeft (kGutter);
        mapArea = r;
    }
    else
    {
        mapArea = r;
    }
}

void RoutingPage::refresh()
{
    outputs->refresh();
    const bool safe = services.daw().isLiveSafe();
    const juce::String note = safe
        ? "Changes here change what the room hears. Under LIVE SAFE, DLIVE asks before applying."
        : juce::String ("Changes here change what the room hears.");
    if (note != liveSafeNote) { liveSafeNote = note; repaint (headArea); }
}

void RoutingPage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);

    auto head = headArea.reduced (Dine::Metric::padX, 0);
    g.setColour (Dine::ink);
    g.setFont (Dine::Type::headingPage());
    const int titleW = Dine::textWidth (Dine::Type::headingPage(), "Routing") + 20;
    Dine::drawText (g, "Routing", head.removeFromLeft (titleW), juce::Justification::centredLeft);

    // The sentence that says what this workspace is, on a tint when LIVE SAFE makes it a
    // warning rather than a statement.
    head.removeFromRight (mapsButton.getWidth() + saveMapButton.getWidth() + applyButton.getWidth() + 20 + 14);
    if (head.getWidth() > 220 && liveSafeNote.isNotEmpty())
    {
        const bool safe = services.daw().isLiveSafe();
        auto chip = head.withSizeKeepingCentre (head.getWidth(), 26).withTrimmedRight (14);
        const int w = juce::jmin (chip.getWidth(), Dine::textWidth (Dine::Type::bodySmall(), liveSafeNote) + 34);
        chip = chip.withWidth (w);
        if (safe)
        {
            Dine::fillRounded (g, chip.toFloat(), Dine::refuse, Dine::Radius::chip);
            g.setColour (Dine::warn);
            g.fillEllipse (float (chip.getX() + 12), float (chip.getCentreY() - 3), 6.0f, 6.0f);
        }
        g.setColour (safe ? Dine::warn : Dine::ink4);
        g.setFont (Dine::Type::bodySmall());
        Dine::drawText (g, liveSafeNote, chip.withTrimmedLeft (safe ? 24 : 0), juce::Justification::centredLeft, true);
    }

    g.setColour (Dine::hair);
    g.fillRect (headArea.withTop (headArea.getBottom() - 1));

    if (! mapArea.isEmpty() && outputArea.isEmpty() && deviceArea.isEmpty())
    {
        // Too narrow for three columns or two: the input map has it all, and the device and
        // the outputs are a tab away rather than squeezed into nothing.
        g.setColour (Dine::ink4);
        g.setFont (Dine::Type::caption());
        Dine::drawText (g, "The device and the outputs need a wider window.",
                    headArea.reduced (Dine::Metric::padX, 0), juce::Justification::centredRight, true);
    }
}

void RoutingPage::resized()
{
    measure();

    auto head = headArea.reduced (Dine::Metric::padX, 0);
    const int aw = juce::jmax (84, applyButton.idealWidth());
    applyButton.setBounds (head.removeFromRight (aw).withSizeKeepingCentre (aw, Dine::Metric::button));
    head.removeFromRight (10);
    const int sw = juce::jmax (96, saveMapButton.idealWidth());
    saveMapButton.setBounds (head.removeFromRight (sw).withSizeKeepingCentre (sw, Dine::Metric::button));
    head.removeFromRight (10);
    const int mw = juce::jmax (110, mapsButton.idealWidth());
    mapsButton.setBounds (head.removeFromRight (mw).withSizeKeepingCentre (mw, Dine::Metric::button));

    devicePage.setVisible (! deviceArea.isEmpty());
    if (! deviceArea.isEmpty()) devicePage.setBounds (deviceArea);

    assignPage.setVisible (! mapArea.isEmpty());
    if (! mapArea.isEmpty()) assignPage.setBounds (mapArea);

    outputs->setVisible (! outputArea.isEmpty());
    if (! outputArea.isEmpty()) outputs->setBounds (outputArea);
}

} // namespace livemix
