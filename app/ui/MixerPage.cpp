#include "MixerPage.h"
#include "Core/DbUtils.h"
#include "UI/Widgets.h"

namespace livemix
{

namespace
{
    constexpr int kHeaderH = 56;
    constexpr int kPadX = 24;
    constexpr int kPadY = 18;
    constexpr int kChannelW = 72;
    constexpr int kBusW = 80;
    constexpr int kMasterW = 88;
    constexpr int kStripGap = 6;
    constexpr int kGroupGap = 18;
    constexpr int kLabelW = 22;
    constexpr int kAfterLabel = 4;

    juce::String db1 (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1);
    }

    juce::Colour busTint (MixBus b) noexcept
    {
        switch (b)
        {
            case MixBus::Drums:  return Dine::warn;
            case MixBus::Bass:   return Dine::accent;
            case MixBus::Music:  return juce::Colour (0xff8fa2d8);
            case MixBus::Vocals: return Dine::ok;
            case MixBus::Master: return Dine::ink;
            case MixBus::Count:  break;
        }
        return Dine::ink2;
    }

    juce::String sentenceCase (const juce::String& s)
    {
        return s.substring (0, 1).toUpperCase() + s.substring (1).toLowerCase();
    }
}

// ------------------------------------------------------------------ GroupLabel
// A vertical bus name between strip groups so the bank stays readable when scrolled.
class MixerPage::GroupLabel : public juce::Component
{
public:
    GroupLabel (const juce::String& title, MixBus b) : label (title), tint (busTint (b)) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (tint.withAlpha (0.85f));
        g.fillRoundedRectangle (r.withWidth (3.0f).withTrimmedTop (8.0f).withTrimmedBottom (28.0f), 1.5f);

        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                                         r.getCentreX(), r.getCentreY()));
        auto text = juce::Rectangle<float> (r.getCentreX() - float (getHeight()) * 0.5f + 10.0f,
                                            r.getCentreY() - 8.0f,
                                            float (getHeight()) - 20.0f, 16.0f);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 600));
        g.drawText (label, text, juce::Justification::centredLeft, true);
    }

private:
    juce::String label;
    juce::Colour tint;
};

// ------------------------------------------------------------------ StripColumn
class MixerPage::StripColumn : public juce::Component
{
public:
    enum class Kind { Channel, Bus, Master };

    StripColumn (MixController& c, Kind k, MixBus busFamily, ChannelRole role, int stripIndex,
                 const juce::String& title)
        : controller (c), kind (k), bus (busFamily), strip (stripIndex), name (title),
          icon (k == Kind::Channel ? Dine::iconForRole (role) : Dine::Icon::Bus),
          meter (DineMeter::Style::Segments),
          muteButton ("M", DineButton::Style::Standard),
          soloButton ("S", DineButton::Style::Standard)
    {
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (-60.0, 12.0, 0.5);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.onValueChange = [this]
        {
            if (updating) return;
            const float db = float (fader.getValue());
            if (kind == Kind::Channel) controller.setStripFader (strip, db);
            else controller.setBusFader (bus, db);
            repaint();
        };

        auto setupLatch = [] (DineButton& b)
        {
            b.setFontPx (11.5f);
            b.setPadX (7);
            b.setClickingTogglesState (false);
        };
        setupLatch (muteButton);
        setupLatch (soloButton);
        muteButton.setVisible (kind == Kind::Channel);
        soloButton.setVisible (kind != Kind::Master);
        muteButton.onClick = [this]
        {
            if (kind != Kind::Channel) return;
            controller.setStripMute (strip, ! controller.getKept().strips[size_t (strip)].mute);
        };
        soloButton.onClick = [this]
        {
            if (kind == Kind::Master) return;
            if (kind == Kind::Channel)
                controller.setStripSolo (strip, ! controller.getKept().strips[size_t (strip)].solo);
            else
                controller.setBusSolo (bus, ! controller.getKept().buses[size_t (bus)].solo);
        };

        addAndMakeVisible (meter);
        addAndMakeVisible (fader);
        addAndMakeVisible (muteButton);
        addAndMakeVisible (soloButton);
    }

    void setOpenHandler (std::function<void()> h) { open = std::move (h); }

    int columnWidth() const noexcept
    {
        if (kind == Kind::Master) return kMasterW;
        if (kind == Kind::Bus) return kBusW;
        return kChannelW;
    }

    void refresh()
    {
        if (! controller.isPrepared()) return;
        const auto& kept = controller.getBase();
        updating = true;

        float faderDb = 0.0f;
        bool muted = false, soloed = false;
        float peak = -120.0f, hold = -120.0f;
        bool clipped = false;

        if (kind == Kind::Channel && strip >= 0 && strip < kept.numStrips)
        {
            const auto& st = kept.strips[size_t (strip)];
            faderDb = st.faderDb;
            muted = st.mute;
            soloed = st.solo;
            const auto& m = controller.getEngine().getStrip (strip).getOutputMeter();
            peak = m.consumeMaxPeakDb();
            hold = m.getMaxRmsDb();
            clipped = m.hasClipped();
        }
        else
        {
            faderDb = kept.buses[size_t (bus)].faderDb;
            muted = kept.buses[size_t (bus)].mute;
            soloed = kept.buses[size_t (bus)].solo;
            const auto& m = controller.getEngine().getBus (bus).getOutputMeter();
            peak = m.consumeMaxPeakDb();
            hold = m.getMaxRmsDb();
            clipped = m.hasClipped();
        }

        fader.setValue (faderDb, juce::dontSendNotification);
        meter.setLevels (peak, hold, clipped);
        levelText = db1 (faderDb);
        peakText = peak <= -119.0f ? Glyph::dash() : db1 (peak);
        mute = muted;
        solo = soloed;
        if (kind == Kind::Channel)
            muteButton.setStyle (mute ? DineButton::Style::Filled : DineButton::Style::Standard);
        if (kind != Kind::Master)
            soloButton.setStyle (solo ? DineButton::Style::Filled : DineButton::Style::Standard);
        updating = false;
        repaint();
    }

    int buttonsHeight() const noexcept
    {
        if (kind == Kind::Master) return 0;
        return Dine::Metric::control + 6;
    }

    int footHeight() const noexcept { return (kind == Kind::Channel ? 52 : 36) + buttonsHeight(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto cardFill = mute ? Dine::card.darker (0.15f) : (solo ? Dine::accent.withAlpha (0.10f) : Dine::card);
        Dine::drawCard (g, r, cardFill);

        auto inner = getLocalBounds().reduced (8, 10);
        auto top = inner.removeFromTop (32);

        Dine::drawIcon (g, icon, top.removeFromTop (14).withSizeKeepingCentre (14, 14).toFloat(),
                        mute ? Dine::crit : solo ? Dine::accent : kind == Kind::Channel ? Dine::glyph : busTint (bus));
        top.removeFromTop (2);
        g.setColour (mute ? Dine::crit : (peakText == Glyph::dash() ? Dine::ink3 : Dine::levelColour (meter.getPeakDb())));
        g.setFont (Dine::mono (11.0f, 500));
        g.drawText (peakText, top.removeFromTop (14), juce::Justification::centred);

        auto foot = inner.removeFromBottom (footHeight());
        if (kind != Kind::Master)
            foot.removeFromTop (buttonsHeight());

        g.setColour (mute ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (kind == Kind::Channel ? 11.5f : 12.0f, kind == Kind::Channel ? 500 : 600));
        g.drawFittedText (name, foot.removeFromTop (28), juce::Justification::centredTop, 2);

        g.setColour (Dine::ink2);
        g.setFont (Dine::mono (11.0f, 500));
        g.drawText (levelText + " dB", foot, juce::Justification::centredTop);
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced (8, 10);
        inner.removeFromTop (32); // icon + peak
        auto foot = inner.removeFromBottom (footHeight());
        if (kind != Kind::Master)
        {
            auto row = foot.removeFromTop (Dine::Metric::control);
            if (kind == Kind::Channel)
            {
                const int w = juce::jmax (26, muteButton.idealWidth());
                soloButton.setBounds (row.removeFromLeft (w));
                row.removeFromLeft (4);
                muteButton.setBounds (row.removeFromLeft (w));
            }
            else
            {
                soloButton.setBounds (row.withSizeKeepingCentre (juce::jmax (28, soloButton.idealWidth()), Dine::Metric::control));
            }
        }

        auto meterCol = inner.removeFromLeft (18);
        meter.setBounds (meterCol.reduced (0, 4).withSizeKeepingCentre (10, meterCol.getHeight() - 8));
        inner.removeFromLeft (4);
        fader.setBounds (inner.reduced (2, 0));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == this && open && ! fader.getBounds().contains (e.getPosition())
            && ! muteButton.getBounds().contains (e.getPosition())
            && ! soloButton.getBounds().contains (e.getPosition())
            && ! meter.getBounds().contains (e.getPosition()))
            open();
    }

    MixController& controller;
    Kind kind;
    MixBus bus;
    int strip = -1;
    juce::String name, levelText { "0.0" }, peakText;
    bool mute = false, solo = false, updating = false;
    Dine::Icon icon;
    DineMeter meter;
    juce::Slider fader;
    DineButton muteButton;
    DineButton soloButton;
    std::function<void()> open;
};

// ------------------------------------------------------------------ MixerPage
MixerPage::MixerPage (MixController& c) : controller (c)
{
    viewport.setViewedComponent (&bank, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    rebuild();
}

MixerPage::~MixerPage() = default;

void MixerPage::rebuild()
{
    items.clear();
    columns.clear();
    bank.removeAllChildren();

    const auto& graph = controller.getGraph();
    auto addLabel = [&] (const juce::String& title, MixBus bus)
    {
        auto lab = std::make_unique<GroupLabel> (title, bus);
        bank.addAndMakeVisible (*lab);
        items.push_back (std::move (lab));
    };
    auto addColumn = [&] (std::unique_ptr<StripColumn> col)
    {
        bank.addAndMakeVisible (*col);
        columns.push_back (col.get());
        items.push_back (std::move (col));
    };

    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        const auto bus = MixBus (b);
        if (graph.stripsOnBus (bus) == 0) continue;

        addLabel (sentenceCase (mixBusName (bus)), bus);

        for (int i = 0; i < graph.numStrips(); ++i)
        {
            const auto& s = graph.strips[size_t (i)];
            if (s.bus != bus) continue;
            auto col = std::make_unique<StripColumn> (controller, StripColumn::Kind::Channel, s.bus, s.role, i, s.name);
            col->setOpenHandler ([this, i]
            {
                if (onOpenStrip) onOpenStrip (i);
            });
            addColumn (std::move (col));
        }

        if (controller.isPrepared() && controller.getEngine().isBusUsed (bus))
        {
            auto col = std::make_unique<StripColumn> (controller, StripColumn::Kind::Bus, bus,
                                                      ChannelRole::KickIn, -1,
                                                      sentenceCase (mixBusName (bus)));
            col->setOpenHandler ([this, bus]
            {
                if (onOpenBus) onOpenBus (bus);
            });
            addColumn (std::move (col));
        }
    }

    if (controller.isPrepared() && controller.getEngine().isBusUsed (MixBus::Master))
    {
        addLabel ("Output", MixBus::Master);
        auto col = std::make_unique<StripColumn> (controller, StripColumn::Kind::Master, MixBus::Master,
                                                  ChannelRole::KickIn, -1, "Master");
        col->setOpenHandler ([this]
        {
            if (onOpenBus) onOpenBus (MixBus::Master);
        });
        addColumn (std::move (col));
    }

    builtForStrips = graph.numStrips();
    resized();
}

void MixerPage::refresh()
{
    if (! controller.isPrepared()) return;
    if (controller.getGraph().numStrips() != builtForStrips) rebuild();
    for (auto* c : columns) c->refresh();
}

void MixerPage::paint (juce::Graphics& g)
{
    auto head = getLocalBounds().removeFromTop (kHeaderH).reduced (kPadX, 0).withTrimmedTop (14);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (21.0f, 600));
    g.drawText ("Mixer", head.removeFromTop (26), juce::Justification::centredLeft);
    g.setColour (Dine::ink2);
    g.setFont (Dine::text (12.5f));
    g.drawText ("Every input, group and the master " + Glyph::dash() + " drag a fader to set the level.",
                head, juce::Justification::topLeft, true);

    if (columns.empty())
    {
        auto empty = getLocalBounds().withTrimmedTop (kHeaderH).reduced (kPadX, kPadY);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        g.drawFittedText ("Assign inputs first. The mixer fills with a strip for each one, then the group buses and master.",
                          empty.removeFromTop (60), juce::Justification::topLeft, 3);
    }
}

void MixerPage::resized()
{
    auto area = getLocalBounds().withTrimmedTop (kHeaderH).reduced (kPadX, kPadY);
    viewport.setBounds (area);

    int totalW = 0;
    bool seenColumnInGroup = false;
    bool anyGroup = false;
    for (auto& item : items)
    {
        if (dynamic_cast<GroupLabel*> (item.get()) != nullptr)
        {
            if (anyGroup) totalW += kGroupGap;
            totalW += kLabelW + kAfterLabel;
            seenColumnInGroup = false;
            anyGroup = true;
        }
        else if (auto* col = dynamic_cast<StripColumn*> (item.get()))
        {
            if (seenColumnInGroup) totalW += kStripGap;
            totalW += col->columnWidth();
            seenColumnInGroup = true;
            anyGroup = true;
        }
    }

    const int h = viewport.getHeight();
    bank.setSize (juce::jmax (totalW, viewport.getWidth()), h);

    int x = 0;
    seenColumnInGroup = false;
    anyGroup = false;
    for (auto& item : items)
    {
        if (dynamic_cast<GroupLabel*> (item.get()) != nullptr)
        {
            if (anyGroup) x += kGroupGap;
            item->setBounds (x, 0, kLabelW, h);
            x += kLabelW + kAfterLabel;
            seenColumnInGroup = false;
            anyGroup = true;
        }
        else if (auto* col = dynamic_cast<StripColumn*> (item.get()))
        {
            if (seenColumnInGroup) x += kStripGap;
            col->setBounds (x, 0, col->columnWidth(), h);
            x += col->columnWidth();
            seenColumnInGroup = true;
            anyGroup = true;
        }
    }
}

} // namespace livemix
