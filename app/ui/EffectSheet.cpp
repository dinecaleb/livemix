#include "EffectSheet.h"
#include "native/MixController.h"
#include "UI/Widgets.h"
#include "FX/TempoSync.h"
#include <cmath>

namespace livemix
{

namespace
{
    constexpr int kCardW = 560, kPadX = 30, kPadY = 26, kDial = 48;
    constexpr int kTitleH = 28, kSentenceH = 40, kCaptionH = 18;

    // The notes a delay repeats at, the way a musician says them, in the engine's order.
    const char* noteName (int division)
    {
        switch (NoteDivision (division))
        {
            case NoteDivision::Whole:         return "Whole note";
            case NoteDivision::Half:          return "Half note";
            case NoteDivision::DottedQuarter: return "Dotted quarter";
            case NoteDivision::Quarter:       return "Quarter";
            case NoteDivision::DottedEighth:  return "Dotted eighth";
            case NoteDivision::Eighth:        return "Eighth";
            case NoteDivision::TripletEighth: return "Eighth triplet";
            case NoteDivision::Sixteenth:     return "Sixteenth";
            default:                          return "?";
        }
    }

    juce::String seconds (double v) { return juce::String (v, v < 10.0 ? 1 : 0) + " s"; }
    juce::String millis (double v)  { return juce::String (juce::roundToInt (v)) + " ms"; }
}

EffectSheet::EffectSheet (MixController& c, FxSlot s) : controller (c), slot (s)
{
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);

    auto knob = [this] (const juce::String& caption, double lo, double hi, double step, double mid,
                        double def, std::function<juce::String (double)> fmt, std::function<void (double)> change)
    {
        auto k = std::make_unique<DineKnob>();
        k->setRange (lo, hi, step, mid);
        k->setDefaultValue (def);
        k->setFormat (std::move (fmt));
        k->setCaption (caption);
        k->setDial (kDial);
        k->setTooltip (caption);
        k->onChange = std::move (change);
        addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
        return knobs.back().get();
    };

    if (isDelay())
    {
        note = std::make_unique<DinePopup>();
        note->setTooltip ("What the delay repeats at: a note of the song, or a time of its own");
        note->onClick = [this]
        {
            const auto& fx = controller.getKept().fx[size_t (slot)].fx;
            juce::PopupMenu m;
            for (int d = 0; d < int (NoteDivision::Count); ++d)
                m.addItem (d + 1, noteName (d), true, fx.delaySync && fx.delayDivision == d);
            m.addSeparator();
            m.addItem (100, "Free time", true, ! fx.delaySync);
            juce::Component::SafePointer<EffectSheet> self (this);
            m.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (note.get()), [self] (int r)
            {
                if (self == nullptr || r <= 0) return;
                self->commitFx ([r] (FxParameters& p)
                {
                    if (r == 100) { p.delaySync = false; return; }
                    p.delaySync = true;
                    p.delayDivision = r - 1;
                });
            });
        };
        addAndMakeVisible (*note);
        time = knob ("Time", 20.0, 2000.0, 1.0, 375.0, 375.0, millis,
                     [this] (double v) { commitFx ([v] (FxParameters& p) { p.delayTimeMs = float (v); }); });
        repeats = knob ("Repeats", 0.0, 90.0, 1.0, 35.0, 30.0,
                        [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; },
                        [this] (double v) { commitFx ([v] (FxParameters& p) { p.delayFeedback = float (v); }); });
        tempo = knob ("Tempo", 40.0, 240.0, 0.5, 110.0, 120.0,
                      [] (double v) { return juce::String (v, std::fmod (v, 1.0) == 0.0 ? 0 : 1) + " bpm"; },
                      [this] (double v)
                      {
                          if (controller.isBypassed()) { pull(); return; }
                          controller.setTempo (float (v));
                          if (onEdited) onEdited();
                          pull();
                      });
        tapButton = std::make_unique<DineButton> ("Tap", DineButton::Style::Standard);
        tapButton->setTooltip ("Tap along with the song, four times or more, to set the tempo");
        tapButton->onClick = [this] { tap (juce::Time::getMillisecondCounterHiRes()); };
        addAndMakeVisible (*tapButton);
    }
    else
    {
        decay = knob ("Decay", 0.2, 10.0, 0.05, 2.0, 2.0, seconds,
                      [this] (double v) { commitFx ([v] (FxParameters& p) { p.reverbDecayS = float (v); }); });
        preDelay = knob ("Pre-delay", 0.0, 250.0, 1.0, 40.0, 20.0, millis,
                         [this] (double v) { commitFx ([v] (FxParameters& p) { p.reverbPreDelayMs = float (v); }); });
    }

    doneButton = std::make_unique<DineButton> ("Done", DineButton::Style::Filled);
    doneButton->setFontPx (12.5f);
    doneButton->onClick = [this] { auto close = onClose; if (close) close(); };
    addAndMakeVisible (*doneButton);
    pull();
}

EffectSheet::~EffectSheet() = default;

bool EffectSheet::isDelay() const
{
    const auto& fx = controller.getKept().fx[size_t (slot)].fx;
    return fx.delayEnabled && ! fx.reverbEnabled;
}

void EffectSheet::commitFx (const std::function<void (FxParameters&)>& edit)
{
    // BYPASS is a comparison against the console feed: nothing in the mix moves under it.
    if (controller.isBypassed()) { pull(); return; }
    auto fx = controller.getKept().fx[size_t (slot)].fx;
    edit (fx);
    controller.setFxSlotCharacter (slot, fx);
    if (onEdited) onEdited();
    pull();
}

void EffectSheet::tap (double nowMs)
{
    // Taps more than two seconds apart start again: that is a new count, not a slow song.
    if (! taps.empty() && nowMs - taps.back() > 2000.0) taps.clear();
    taps.push_back (nowMs);
    if (taps.size() > 8) taps.erase (taps.begin());
    if (taps.size() < 2) return;
    const double beat = (taps.back() - taps.front()) / double (taps.size() - 1);
    if (beat <= 0.0) return;
    if (controller.isBypassed()) return;
    controller.setTempo (float (std::round (60000.0 / beat * 2.0) / 2.0));
    if (onEdited) onEdited();
    pull();
}

void EffectSheet::pull()
{
    const auto& fx = controller.getKept().fx[size_t (slot)].fx;
    if (decay != nullptr) decay->setValue (fx.reverbDecayS);
    if (preDelay != nullptr) preDelay->setValue (fx.reverbPreDelayMs);
    if (note != nullptr)
    {
        note->setValue (fx.delaySync ? juce::String (noteName (fx.delayDivision)) : juce::String ("Free time"));
        note->setBriefValue (fx.delaySync ? juce::String (kNoteDivisionNames[size_t (juce::jlimit (0, int (NoteDivision::Count) - 1, fx.delayDivision))])
                                          : juce::String ("Free"));
    }
    if (time != nullptr)
    {
        // Synced, the time is the note at this tempo: shown, and not a knob to argue with.
        time->setValue (fx.delaySync ? double (TempoSync::delayMs (NoteDivision (fx.delayDivision), double (controller.getTempo())))
                                     : double (fx.delayTimeMs));
        time->setEnabled (! fx.delaySync);
    }
    if (repeats != nullptr) repeats->setValue (fx.delayFeedback);
    if (tempo != nullptr)
    {
        tempo->setValue (controller.getTempo());
        tempo->setEnabled (fx.delaySync);
    }
    if (tapButton != nullptr) tapButton->setEnabled (fx.delaySync);
    repaint();
}

bool EffectSheet::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey)
    {
        auto close = onClose;
        if (close) close();
        return true;
    }
    if (key.getTextCharacter() == 't' && tapButton != nullptr && tapButton->isEnabled())
    {
        tap (juce::Time::getMillisecondCounterHiRes());
        return true;
    }
    return false;
}

juce::Rectangle<int> EffectSheet::cardBounds() const
{
    const int w = juce::jmin (kCardW, getWidth() - 60);
    const int h = kPadY + kTitleH + 6 + kSentenceH + 18 + kCaptionH + DineKnob::cellHeight (kDial) + 24
                + Dine::Metric::button + kPadY;
    return juce::Rectangle<int> (w, juce::jmin (h, getHeight() - 40)).withCentre (getLocalBounds().getCentre());
}

void EffectSheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.86f));
    auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), Dine::Radius::card);

    auto r = card.reduced (kPadX, kPadY);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (22.0f, 600));
    Dine::drawText (g, juce::String (fxSlotName (slot)), r.removeFromTop (kTitleH), juce::Justification::centredLeft, true);
    r.removeFromTop (6);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (13.0f));
    const juce::String sentence = isDelay()
        ? "What the repeats land on and how long they last. On a note, the delay follows the song's tempo - "
          "turn it, or tap along."
        : "How long the reverb rings, and how late it starts after the sound. A longer pre-delay keeps the words clear.";
    Dine::drawFittedText (g, sentence, r.removeFromTop (kSentenceH), juce::Justification::topLeft, 2, 1.0f);
    r.removeFromTop (18);

    if (note != nullptr)
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        Dine::drawText (g, "Note", note->getBounds().withY (note->getY() - kCaptionH).withHeight (kCaptionH - 4),
                        juce::Justification::bottomLeft);
    }
}

void EffectSheet::resized()
{
    auto r = cardBounds().reduced (kPadX, kPadY);
    auto foot = r.removeFromBottom (Dine::Metric::button);
    doneButton->setBounds (foot.removeFromRight (juce::jmax (88, doneButton->idealWidth())));
    r.removeFromTop (kTitleH + 6 + kSentenceH + 18);

    auto row = r.removeFromTop (kCaptionH + DineKnob::cellHeight (kDial)).withTrimmedTop (kCaptionH);
    if (note != nullptr)
    {
        auto cell = row.removeFromLeft (150);
        note->setBounds (cell.removeFromTop (Dine::Metric::button).withWidth (juce::jmax (140, juce::jmin (cell.getWidth(), note->idealWidth()))));
        row.removeFromLeft (12);
    }
    for (auto& k : knobs)
    {
        if (k.get() == tempo) row.removeFromLeft (12);     // the tempo is the song's, not this return's
        k->setBounds (row.removeFromLeft (k->cellWidth()));
        row.removeFromLeft (4);
    }
    if (tapButton != nullptr && tempo != nullptr)
        tapButton->setBounds (row.removeFromLeft (juce::jmax (56, tapButton->idealWidth()))
                                 .withSizeKeepingCentre (juce::jmax (56, tapButton->idealWidth()), Dine::Metric::button)
                                 .withY (tempo->getY() + kDial / 2 - Dine::Metric::button / 2));
}

} // namespace livemix
