#include "LivePage.h"
#include "UI/Widgets.h"

namespace livemix
{

// One group: a name, a meter and a fader big enough to find without looking.
class LivePage::GroupFader : public juce::Component
{
public:
    GroupFader (MixController& c, MixBus b) : controller (c), bus (b)
    {
        addAndMakeVisible (meter);
        addAndMakeVisible (fader);
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.onValueChange = [this] { controller.setBusFader (bus, float (fader.getValue())); };
        addAndMakeVisible (mute);
        mute.setFontPx (11.0f);
        mute.setClickingTogglesState (false);
        mute.onClick = [this] { controller.setBusMute (bus, ! controller.getBase().buses[size_t (bus)].mute); };
    }

    void refresh()
    {
        const auto& params = controller.getBase();
        const auto& b = params.buses[size_t (bus)];
        if (! fader.isMouseButtonDown()) fader.setValue (b.faderDb, juce::dontSendNotification);
        mute.setToggleState (b.mute, juce::dontSendNotification);

        float peak = -120.0f;
        if (controller.isPrepared())
            peak = controller.getEngine().getBus (bus).getOutputMeter().consumeMaxPeakDb();
        meter.setLevels (peak, peak, peak > -0.2f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        Dine::drawCard (g, getLocalBounds().toFloat());
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 700).withExtraKerningFactor (0.06f));
        g.drawText (mixBusName (bus), getLocalBounds().reduced (8, 10).withHeight (16), juce::Justification::centred);

        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.0f));
        g.drawText (juce::String (controller.getBase().buses[size_t (bus)].faderDb, 1) + " dB",
                    getLocalBounds().reduced (8).withTrimmedBottom (34).removeFromBottom (14), juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        r.removeFromTop (20);
        mute.setBounds (r.removeFromBottom (24).reduced (6, 0));
        r.removeFromBottom (18);
        meter.setBounds (r.removeFromRight (14).reduced (2, 0));
        fader.setBounds (r);
    }

private:
    MixController& controller;
    MixBus bus;
    DineMeter meter { DineMeter::Style::Segments };
    juce::Slider fader;
    DineButton mute { "MUTE", DineButton::Style::Ghost };
};

LivePage::LivePage (MixController& c, AppServices& s) : controller (c), services (s)
{
    for (int i = 0; i < int (MixBus::Count); ++i)
    {
        faders[size_t (i)] = std::make_unique<GroupFader> (controller, MixBus (i));
        addAndMakeVisible (*faders[size_t (i)]);
    }
    addAndMakeVisible (liveSafeButton);
    liveSafeButton.setCaps (true);
    liveSafeButton.setClickingTogglesState (false);
    liveSafeButton.onClick = [this]
    {
        auto& project = services.daw().getProject();
        project.liveSafe = ! project.liveSafe;
        services.saveSession();
        if (onToast) onToast (project.liveSafe ? "LIVE SAFE on. Tune, routing and timeline edits are locked."
                                               : "LIVE SAFE off.");
        if (onLiveSafeChanged) onLiveSafeChanged();
        repaint();
    };
}

LivePage::~LivePage() = default;

void LivePage::rebuild()
{
    for (auto& f : faders) if (f != nullptr) f->refresh();
    repaint();
}

void LivePage::refresh()
{
    for (auto& f : faders) if (f != nullptr) f->refresh();

    health = controller.getMixHealthPercent();
    if (controller.isPrepared())
    {
        const float masterPeak = controller.getEngine().getBus (MixBus::Master).getOutputMeter().getMaxPeakDb();
        headroomDb = -masterPeak;
    }

    const auto& transport = services.daw().getTransport();
    clock = Transport::formatTime (transport.getPositionSeconds());
    state = services.daw().isRecording() ? "RECORDING"
          : transport.isPlaying()        ? "PLAYING"
          : services.isAudioRunning()    ? "READY" : "NO DEVICE";

    liveSafeButton.setToggleState (services.daw().getProject().liveSafe, juce::dontSendNotification);
    repaint();
}

juce::Rectangle<int> LivePage::body() const
{
    return getLocalBounds().reduced (Dine::Metric::padX, Dine::Metric::padY);
}

void LivePage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    auto r = body();

    // ---- title
    auto head = r.removeFromTop (58);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (22.0f, 700));
    g.drawText (services.currentSessionName().isEmpty() ? "Untitled" : services.currentSessionName(),
                head.removeFromTop (28), juce::Justification::centredLeft, true);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.5f));
    g.drawText (juce::String (mixPurposeName (controller.getSession().purpose)) + "   " + Glyph::dot() + "   "
                    + juce::String (styleProfileName (controller.getSession().profile)),
                head, juce::Justification::topLeft, true);

    r.removeFromTop (12);

    // ---- the four things that matter during a service
    auto strip = r.removeFromTop (86);
    const int cardWidth = (strip.getWidth() - 3 * 12) / 4;

    auto card = [&] (juce::Rectangle<int> area, const juce::String& caption, const juce::String& value,
                     juce::Colour colour, const juce::String& note)
    {
        Dine::drawCard (g, area.toFloat());
        auto inner = area.reduced (16, 13);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 600));
        g.drawText (caption, inner.removeFromTop (14), juce::Justification::topLeft);
        g.setColour (colour);
        g.setFont (Dine::mono (24.0f, 500));
        g.drawText (value, inner.removeFromTop (30), juce::Justification::topLeft, true);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f));
        g.drawText (note, inner, juce::Justification::topLeft, true);
    };

    const bool recording = services.daw().isRecording();
    card (strip.removeFromLeft (cardWidth), "STATE", state,
          recording ? Dine::crit : (services.isAudioRunning() ? Dine::ok : Dine::ink4), clock);
    strip.removeFromLeft (12);
    card (strip.removeFromLeft (cardWidth), "OUTPUT", services.isAudioRunning() ? "ACTIVE" : "OFF",
          services.isAudioRunning() ? Dine::ok : Dine::ink4,
          services.currentOutputDevice().isEmpty() ? "No output chosen" : services.currentOutputDevice());
    strip.removeFromLeft (12);
    card (strip.removeFromLeft (cardWidth), "MIX HEALTH", health > 0 ? juce::String (health) + "%" : Glyph::dash(),
          health >= 80 ? Dine::ok : health >= 50 ? Dine::warn : Dine::ink4,
          controller.getTuneCount() > 0 ? "Tuned" : "Not tuned yet");
    strip.removeFromLeft (12);
    card (strip, "HEADROOM", juce::String (headroomDb, 1) + " dB",
          headroomDb < 0.5f ? Dine::crit : headroomDb < 3.0f ? Dine::warn : Dine::ok,
          services.xrunCount() > 0 ? juce::String (services.xrunCount()) + " drops" : "No drops");

    r.removeFromTop (16);
    r.removeFromBottom (52);      // the LIVE SAFE row is laid out in resized()

    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.0f, 600));
    g.drawText ("GROUPS", r.removeFromTop (18), juce::Justification::topLeft);

    // Under the groups: what an operator would want to know without opening anything.
    r.removeFromTop (juce::jmin (r.getHeight(), 420) + 20);
    if (r.getHeight() > 60)
    {
        auto card = r.removeFromTop (juce::jmin (r.getHeight(), 132));
        Dine::drawCard (g, card.toFloat());
        auto inner = card.reduced (18, 14);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 600));
        g.drawText ("WHAT TO WATCH", inner.removeFromTop (16), juce::Justification::topLeft);
        inner.removeFromTop (4);

        const auto notes = controller.getMixHealthNotes();
        if (notes.empty())
        {
            g.setColour (Dine::ok);
            g.setFont (Dine::text (12.5f));
            g.drawText (controller.getTuneCount() > 0 ? "Every input is heard and at a healthy level."
                                                      : "Nothing is wrong. Run TUNE MIX when the band is playing.",
                        inner.removeFromTop (18), juce::Justification::topLeft, true);
        }
        else
        {
            g.setFont (Dine::text (12.5f));
            for (const auto& note : notes)
            {
                if (inner.getHeight() < 18) break;
                g.setColour (Dine::ink2);
                g.drawText (Glyph::dot() + juce::String ("  ") + juce::String (note),
                            inner.removeFromTop (18), juce::Justification::topLeft, true);
            }
        }
    }
}

void LivePage::resized()
{
    auto r = body();
    r.removeFromTop (58 + 12 + 86 + 16 + 18);
    auto safeRow = r.removeFromBottom (52);
    liveSafeButton.setBounds (safeRow.removeFromLeft (juce::jmax (140, liveSafeButton.idealWidth()))
                                  .withSizeKeepingCentre (juce::jmax (140, liveSafeButton.idealWidth()), 30));

    const int count = int (MixBus::Count);
    const int gap = 14;
    const int width = juce::jmax (60, (r.getWidth() - gap * (count - 1)) / count);
    auto row = r.withHeight (juce::jmin (r.getHeight(), 420));
    for (int i = 0; i < count; ++i)
    {
        faders[size_t (i)]->setBounds (row.removeFromLeft (width));
        row.removeFromLeft (gap);
    }
}

} // namespace livemix
