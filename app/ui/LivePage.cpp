#include "LivePage.h"
#include "UI/Widgets.h"
#include <cmath>

namespace livemix
{

// One group: a name, a meter, a fader big enough to find without looking, and the two
// keys that can change what the room hears. Whatever state the group is in - muted,
// soloed - the tile says so in colour and in words: during a service nobody should have
// to work out why a group has gone quiet.
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
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.getProperties().set ("dineFader", true);
        fader.setTooltip ("Level for the whole group. Double-click for 0.0 dB.");
        fader.onValueChange = [this] { controller.setBusFader (bus, float (fader.getValue())); };

        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
        mute.setTooltip ("Mute this group.");
        solo.setTooltip ("Solo: hear this group alone.");
        mute.onClick = [this] { controller.setBusMute (bus, ! controller.getBase().buses[size_t (bus)].mute); };
        solo.onClick = [this] { controller.setBusSolo (bus, ! controller.getBase().buses[size_t (bus)].solo); };
    }

    void refresh()
    {
        const auto& b = controller.getBase().buses[size_t (bus)];
        if (! fader.isMouseButtonDown()) fader.setValue (b.faderDb, juce::dontSendNotification);
        levelText = juce::String (b.faderDb >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (b.faderDb), 1);

        float peak = -120.0f;
        if (controller.isPrepared())
            peak = controller.getEngine().getBus (bus).getOutputMeter().consumeMaxPeakDb();
        meter.setLevels (peak, peak, peak > -0.2f);
        meter.setMuted (b.mute);
        peakDb = meter.getPeakDb();
        peakText = peakDb <= -60.0f ? Glyph::dash() : juce::String (peakDb, 1);

        if (b.mute != muted || b.solo != soloed)
        {
            muted = b.mute;
            soloed = b.solo;
            mute.setOn (muted);
            mute.setLetter (muted ? "MUTED" : "MUTE");
            solo.setOn (soloed);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto ground = Dine::card;
        if (muted)       ground = ground.overlaidWith (Dine::keyMute.withAlpha (0.10f)).darker (0.18f);
        else if (soloed) ground = ground.overlaidWith (Dine::accent.withAlpha (0.10f));
        Dine::drawCard (g, r, ground, muted ? Dine::keyMute.withAlpha (0.45f)
                                            : soloed ? Dine::accent.withAlpha (0.5f) : Dine::hair);

        // the group's colour along the top edge, the way the console strip carries it
        {
            juce::Path cap;
            cap.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), 8.0f,
                                     Dine::Radius::card, Dine::Radius::card, true, true, false, false);
            g.saveState();
            g.reduceClipRegion (juce::Rectangle<int> (0, 0, getWidth(), 3));
            g.setColour (tint().withAlpha (muted ? 0.30f : 0.85f));
            g.fillPath (cap);
            g.restoreState();
        }

        auto inner = getLocalBounds().reduced (12, 0);
        auto head = inner.removeFromTop (34).withTrimmedTop (10);
        g.setColour (muted ? Dine::keyMute : Dine::ink);
        g.setFont (Dine::text (13.0f, 700).withExtraKerningFactor (0.06f));
        g.drawText (juce::String (mixBusName (bus)).toUpperCase(), head, juce::Justification::centred, true);
        Dine::drawRule (g, inner.withHeight (1), Dine::hairSoft);

        // the readouts under the throw: what it is set to, and what is coming through
        auto feet = getLocalBounds().reduced (12, 0).withTrimmedBottom (kMuteH + 16);
        auto row = feet.removeFromBottom (16);
        g.setColour (muted ? Dine::ink4 : Dine::ink);
        g.setFont (Dine::mono (12.0f, 600));
        g.drawText (levelText + " dB", row.removeFromLeft (row.getWidth() / 2), juce::Justification::centredLeft);
        g.setColour (muted || peakText == Glyph::dash() ? Dine::ink4 : Dine::levelColour (peakDb));
        g.setFont (Dine::mono (11.0f));
        g.drawText (peakText, row, juce::Justification::centredRight);

        if (muted)
        {
            g.setColour (Dine::keyMute.withAlpha (0.85f));
            g.setFont (Dine::text (10.0f, 700).withExtraKerningFactor (0.10f));
            g.drawText ("NOT HEARD", feet.removeFromBottom (14), juce::Justification::centred, false);
        }
        else if (soloed)
        {
            g.setColour (Dine::accent);
            g.setFont (Dine::text (10.0f, 700).withExtraKerningFactor (0.10f));
            g.drawText ("SOLO", feet.removeFromBottom (14), juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        r.removeFromTop (26);
        auto keys = r.removeFromBottom (kMuteH);
        const int w = (keys.getWidth() - 8) * 2 / 3;
        mute.setBounds (keys.removeFromLeft (w));
        keys.removeFromLeft (8);
        solo.setBounds (keys);
        r.removeFromBottom (16 + 14 + 8);          // the readouts and the state word

        // The throw and its meter are one object, centred in the tile: a group fader is
        // meant to be found with a glance and moved with one hand.
        auto body = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 46 + 10 + 18), r.getHeight());
        fader.setBounds (body.removeFromLeft (46));
        body.removeFromLeft (10);
        meter.setBounds (body.removeFromLeft (18));
    }

private:
    juce::Colour tint() const
    {
        switch (bus)
        {
            case MixBus::Drums:  return Dine::warn;
            case MixBus::Bass:   return Dine::accent;
            case MixBus::Music:  return juce::Colour (0xff8fa2d8);
            case MixBus::Vocals: return Dine::ok;
            case MixBus::Master: return juce::Colour (0xffc8ccd4);
            case MixBus::Count:  break;
        }
        return Dine::ink2;
    }

    static constexpr int kMuteH = 30;

    MixController& controller;
    MixBus bus;
    DineMeter meter { DineMeter::Style::Segments };
    juce::Slider fader;
    DineKey mute { "MUTE", Dine::keyMute };
    DineKey solo { "SOLO", Dine::keySolo };
    juce::String levelText { "+0.0" }, peakText { Glyph::dash() };
    float peakDb = -120.0f;
    bool muted = false, soloed = false;
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
    liveSafeButton.setTooltip ("Lock the session for the service: tuning, routing and timeline edits are refused "
                               "until it is switched off. The faders and the keys keep working.");
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

    // LIVE SAFE is a lock: it has to read as on or off from the back of the room, so the
    // button fills and says which it is, rather than relying on a toggle nobody can see.
    const bool safe = services.daw().getProject().liveSafe;
    if (safe != liveSafeOn)
    {
        liveSafeOn = safe;
        liveSafeButton.setStyle (safe ? DineButton::Style::Filled : DineButton::Style::Standard);
        liveSafeButton.setButtonText (safe ? "LIVE SAFE ON" : "LIVE SAFE");
        liveSafeButton.setIcon (safe ? Dine::Icon::Check : Dine::Icon::None);
        resized();
    }
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
    r.removeFromTop (juce::jlimit (240, 460, r.getHeight() - 152) + 20);
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

    // ---- the lock, and what it is doing right now
    {
        auto safeRow = body().removeFromBottom (52);
        safeRow.removeFromLeft (liveSafeButton.getWidth() + 14);
        g.setColour (liveSafeOn ? Dine::warn : Dine::ink4);
        g.setFont (Dine::text (12.0f));
        g.drawText (liveSafeOn ? "Locked for the service: tuning, routing and timeline edits are refused. "
                                 "Faders, mutes and the transport still work."
                               : "Lock the session before the service starts, so nothing can be changed by accident.",
                    safeRow.withSizeKeepingCentre (safeRow.getWidth(), 30), juce::Justification::centredLeft, true);
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
    // The tiles take the room that is left, down to the "what to watch" card: a fader with
    // nothing under it is wasted height, and one squeezed into 200 px cannot be trusted.
    auto row = r.withHeight (juce::jlimit (240, 460, r.getHeight() - 152));
    for (int i = 0; i < count; ++i)
    {
        faders[size_t (i)]->setBounds (row.removeFromLeft (width));
        row.removeFromLeft (gap);
    }
}

} // namespace livemix
