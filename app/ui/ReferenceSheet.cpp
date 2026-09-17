#include "ReferenceSheet.h"
#include "Profiles/Profile.h"
#include <cmath>

namespace livemix
{

namespace
{
    constexpr int kCardW = 640;
    constexpr int kBalanceH = 132;     // the two tonal balances, drawn against each other
    constexpr int kAimH = 20;          // one line of "this is what it will aim for"
    constexpr int kLimitH = 32;
    constexpr int kPromiseH = 78;      // the three lines of what matching does and does not do        // ... and one refusal, which needs two lines to say why

    juce::String clock (float seconds)
    {
        const int total = int (std::round (seconds));
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    // The short label under a bar. The sentences use the long words; a column 40 px wide cannot.
    const char* shortBand (Band b)
    {
        switch (b)
        {
            case Band::Sub:        return "sub";
            case Band::Low:        return "low";
            case Band::LowMid:     return "l-mid";
            case Band::Mid:        return "mid";
            case Band::UpperMid:   return "u-mid";
            case Band::Presence:   return "pres";
            case Band::Brilliance: return "top";
            default:               return "air";
        }
    }
}

// The file half, on a thread of its own. It owns everything it touches, so the sheet can
// be closed while it is still reading without the reader losing its footing.
class ReferenceSheet::Measurer : public juce::Thread
{
public:
    Measurer (const juce::File& f, StyleProfileId p) : juce::Thread ("DLIVE reference"), file (f), profile (p) {}
    ~Measurer() override { stopThread (5000); }

    void run() override
    {
        auto measured = ReferenceAudio::measure (file, profile);
        if (threadShouldExit()) return;
        result = std::move (measured);
        done = true;
    }

    juce::File file;
    StyleProfileId profile;
    ReferenceAudio::Result result;
    std::atomic<bool> done { false };
};

ReferenceSheet::ReferenceSheet (MixController& c) : controller (c)
{
    for (auto* b : { &choose, &another, &remove, &match, &close }) addChildComponent (*b);
    choose.setFontPx (12.5f);
    another.setFontPx (12.0f);
    remove.setFontPx (12.0f);
    close.setFontPx (12.0f);
    match.setCaps (true);                 // a product verb, like TUNE MIX and KEEP
    match.setFontPx (13.0f);
    match.setIcon (Dine::Icon::Target);
    choose.setIcon (Dine::Icon::Waveform);

    choose.onClick = [this] { chooseFile(); };
    another.onClick = [this] { chooseFile(); };
    remove.onClick = [this]
    {
        const juce::String name (controller.getReference().name);
        controller.clearReference();
        shown = ReferenceMatch {};
        if (onToast) onToast (name.isEmpty() ? juce::String ("The reference was removed.")
                                             : name + " is no longer the reference. From the next tune the mix is aimed at "
                                                      "the profile again; what the reference already set stays until then.");
        updateControls();
        resized();
        repaint();
    };
    match.onClick = [this]
    {
        const bool listened = controller.hasListened();
        controller.startReferenceMatch();
        if (onToast) onToast (listened ? "Aimed at " + juce::String (controller.getReference().name)
                                            + ". Compare it with BEFORE, then KEEP or REVERT."
                                       : "DLIVE has not heard the band yet, so it is listening first.");
        if (onClose) onClose();
    };
    close.onClick = [this] { if (onClose) onClose(); };

    setInterceptsMouseClicks (true, true);
    updateControls();
}

ReferenceSheet::~ReferenceSheet() = default;

ReferenceSheet::State ReferenceSheet::state() const
{
    if (measurer != nullptr) return State::Measuring;
    if (refusedReason.isNotEmpty() || error.isNotEmpty()) return State::Refused;
    return controller.hasReference() ? State::Chosen : State::Empty;
}

void ReferenceSheet::chooseFile()
{
    if (measurer != nullptr) return;
    chooser = std::make_unique<juce::FileChooser> ("Choose the song to sound like",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.mp3;*.m4a;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File()) return;
                              refusedReason.clear();
                              refusedGuidance.clear();
                              error.clear();
                              measuringName = file.getFileNameWithoutExtension();
                              measurer = std::make_unique<Measurer> (file, controller.getSession().profile);
                              measurer->startThread();
                              updateControls();
                              resized();
                              repaint();
                          });
}

void ReferenceSheet::refresh()
{
    if (measurer != nullptr && measurer->done.load())
    {
        auto result = std::move (measurer->result);
        measurer.reset();
        if (result.error.isNotEmpty())
        {
            error = result.error;
        }
        else if (! result.adequacy.usable)
        {
            refusedReason = juce::String (result.adequacy.reason);
            refusedGuidance = juce::String (result.adequacy.guidance);
        }
        else
        {
            controller.setReference (result.profile);
            if (onToast) onToast (juce::String (result.profile.name) + " is the reference. Press MATCH TO REFERENCE to aim the mix at it.");
        }
    }
    const int before = listHeight();
    shown = preview();
    if (state() != lastState || listHeight() != before) { lastState = state(); updateControls(); resized(); }
    repaint();
}

void ReferenceSheet::updateControls()
{
    const auto s = state();
    choose.setVisible (s == State::Empty || s == State::Refused);
    another.setVisible (s == State::Chosen);
    remove.setVisible (s == State::Chosen);
    match.setVisible (s == State::Chosen);
    // A chosen reference has Remove / Choose another / MATCH along its foot; a fourth button
    // saying Close is one more thing to read for something a click outside already does.
    close.setVisible (s == State::Empty || s == State::Refused);
    shown = preview();
    match.setButtonText (controller.hasListened() ? "Match to reference" : "Listen, then match");
    match.setEnabled (controller.isPrepared() && controller.getStage() != MixController::Stage::Listening
                      && controller.getStage() != MixController::Stage::Planning && ! controller.isTuningLive());
}

// What matching would aim for, worked out from the listen DLIVE already has. It is the same
// function the planner calls, so what the sheet promises and what the master gets cannot
// drift apart. With a plan already aimed at this reference, that plan's own record is used.
ReferenceMatch ReferenceSheet::preview() const
{
    if (const auto* plan = controller.getPlan())
        if (plan->reference.used && plan->reference.name == controller.getReference().name)
            return plan->reference;

    ReferenceMatch out;
    if (! controller.hasReference()) return out;
    const auto& session = controller.getSession();
    const ChannelRole role = busRole (MixBus::Master, session.purpose);
    const AnalysisResult& masterIn = controller.hasListened()
                                       ? controller.getLastListen().buses[size_t (MixBus::Master)]
                                       : AnalysisResult {};
    Reference::targets (Profiles::targets (session.profile, role), controller.getReference(),
                        masterIn, session.profile, out);
    return out;
}

// What the aims and the limits take. A sheet with empty space under its list reads as a
// sheet still loading, and one that cuts its list off hides the refusals - which are the
// half of this feature that has to be read.
int ReferenceSheet::listHeight() const
{
    if (state() != State::Chosen) return 0;
    return int (shown.aims.size()) * kAimH + int (shown.limits.size()) * kLimitH;
}

juce::Rectangle<int> ReferenceSheet::cardBounds() const
{
    const int w = juce::jmin (kCardW, getWidth() - 40);
    int h = 250;
    switch (state())
    {
        // 22 top pad, the head, the headline, the paragraph, the box of three, the foot.
        case State::Empty:     h = 22 + 18 + 12 + 26 + 6 + 56 + 10 + kPromiseH + 14 + Dine::Metric::button + 22; break;
        case State::Measuring: h = 200; break;
        case State::Refused:   h = 22 + 18 + 12 + 26 + 8 + 36 + 4 + 54 + 14 + Dine::Metric::button + 22; break;
        // 22 top pad, the head, the name, the readout, the two balances, the list, the foot.
        case State::Chosen:    h = 22 + 18 + 12 + 26 + 2 + 16 + 12 + kBalanceH + 10 + listHeight() + 14 + Dine::Metric::button + 22; break;
    }
    h = juce::jmin (h, juce::jmax (220, getHeight() - 20));
    return juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
}

// The two balances, band by band: what the reference carries and what this mix carries, in
// the same units (each band's share of its own record), so a quiet live mix and a loud
// master are directly comparable. The aim, where it is bounded, is drawn as the mark the
// bar is being pulled to.
void ReferenceSheet::drawBalance (juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto& m = shown;
    Dine::fillRounded (g, area.toFloat(), Dine::item, Dine::Radius::control);
    auto inner = area.reduced (14, 12);
    auto legend = inner.removeFromTop (14);
    {
        auto dot = legend.removeFromLeft (9).withSizeKeepingCentre (7, 7);
        g.setColour (Dine::monitor);
        g.fillRoundedRectangle (dot.toFloat(), 1.5f);
        legend.removeFromLeft (6);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (11.0f));
        const int w1 = Dine::textWidth (Dine::text (11.0f), "the reference");
        g.drawText ("the reference", legend.removeFromLeft (w1), juce::Justification::centredLeft);
        legend.removeFromLeft (14);
        dot = legend.removeFromLeft (9).withSizeKeepingCentre (7, 7);
        g.setColour (Dine::accent);
        g.fillRoundedRectangle (dot.toFloat(), 1.5f);
        legend.removeFromLeft (6);
        g.setColour (Dine::ink2);
        g.drawText (controller.hasListened() ? "this mix" : "this mix (listen first)", legend, juce::Justification::centredLeft, true);
    }
    inner.removeFromTop (8);
    auto labels = inner.removeFromBottom (13);

    const int n = int (Band::Count);
    const int cell = inner.getWidth() / n;
    const float floorDb = -42.0f;
    auto heightOf = [&] (float v) { return juce::jlimit (0.0f, 1.0f, (v - floorDb) / -floorDb); };

    for (int i = 0; i < n; ++i)
    {
        auto column = inner.withX (inner.getX() + i * cell).withWidth (cell);
        auto label = labels.withX (labels.getX() + i * cell).withWidth (cell);
        const auto& band = m.bands[size_t (i)];
        const int barW = juce::jmax (5, juce::jmin (11, cell / 2 - 4));

        auto refBar = column.withWidth (barW).withX (column.getCentreX() - barW - 1);
        auto mixBar = column.withWidth (barW).withX (column.getCentreX() + 1);
        auto fill = [&] (juce::Rectangle<int> r, float value, juce::Colour c)
        {
            const int h = juce::jmax (2, int (float (r.getHeight()) * heightOf (value)));
            Dine::fillRounded (g, r.removeFromBottom (h).toFloat(), c, 2.0f);
        };
        fill (refBar, band.referenceDb, Dine::monitor.withAlpha (0.85f));
        if (controller.hasListened()) fill (mixBar, band.mixDb, Dine::accent.withAlpha (0.85f));

        // Where a reference asked for more than a reference may have, the aim is drawn as the
        // line the master actually goes to, so "it stopped there" is visible and not only said.
        if (band.bounded)
        {
            const float y = float (column.getBottom()) - float (column.getHeight()) * heightOf (band.aimDb);
            g.setColour (Dine::warn.withAlpha (0.9f));
            g.fillRect (float (refBar.getX()), y, float (barW), 1.5f);
        }

        g.setColour (Dine::ink4);
        g.setFont (Dine::text (9.5f));
        g.drawText (shortBand (Band (i)), label, juce::Justification::centred);
    }
}

void ReferenceSheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.86f));

    auto card = cardBounds().toFloat();
    Dine::drawSheet (g, card, 14.0f);

    auto r = cardBounds().reduced (26, 22);
    const auto s = state();

    // ---- who this is about, on every state.
    {
        auto head = r.removeFromTop (18);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (19.0f, 600));
        g.drawText ("MATCH TO REFERENCE", head.removeFromLeft (240), juce::Justification::centredLeft);
        if (s == State::Chosen)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.5f));
            g.drawText (juce::String (controller.getReference().name), head, juce::Justification::centredRight, true);
        }
    }
    r.removeFromTop (12);

    g.setColour (Dine::ink);
    g.setFont (Dine::text (19.0f, 600));

    if (s == State::Measuring)
    {
        g.drawText ("Listening to " + measuringName, r.removeFromTop (26), juce::Justification::topLeft, true);
        r.removeFromTop (6);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText ("DLIVE is measuring the whole song the way it measures the band: its tonal balance, how dense it "
                          "is and how wide it sits. The console keeps running while it reads.",
                          r.removeFromTop (56), juce::Justification::topLeft, 3);
        return;
    }

    if (s == State::Refused)
    {
        g.setColour (Dine::warn);
        g.drawText (error.isNotEmpty() ? "That file will not do" : "That is not a mix to aim at",
                    r.removeFromTop (26), juce::Justification::topLeft, true);
        r.removeFromTop (8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        g.drawFittedText (error.isNotEmpty() ? error : refusedReason, r.removeFromTop (36), juce::Justification::topLeft, 2);
        r.removeFromTop (4);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText (error.isNotEmpty() ? "Pick the finished stereo mix of a song you want this service to sound like."
                                             : refusedGuidance,
                          r.removeFromTop (54), juce::Justification::topLeft, 3);
        return;
    }

    if (s == State::Empty)
    {
        g.drawText ("Sound like a record you know", r.removeFromTop (26), juce::Justification::topLeft, true);
        r.removeFromTop (6);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText ("Add a finished song and DLIVE aims the master at it: the tonal balance it has, how wide it sits "
                          "and how dense it is. It uses the listen it already has of your band, so it costs nothing at the "
                          "console.",
                          r.removeFromTop (56), juce::Justification::topLeft, 3);
        r.removeFromTop (10);

        auto box = r.removeFromTop (kPromiseH);
        Dine::fillRounded (g, box.toFloat(), Dine::item, Dine::Radius::control);
        auto inner = box.reduced (14, 12);
        const char* lines[] = {
            "The master's tone, image and density follow the reference.",
            "How loud the stream is delivered does not: that belongs to the broadcast.",
            "Who is loud in the mix does not: DLIVE balances your band from what it heard."
        };
        const Dine::Icon icons[] = { Dine::Icon::Check, Dine::Icon::Warn, Dine::Icon::Warn };
        const juce::Colour tints[] = { Dine::accent, Dine::ink3, Dine::ink3 };
        for (int i = 0; i < 3; ++i)
        {
            auto row = inner.removeFromTop (18);
            Dine::drawIcon (g, icons[i], row.removeFromLeft (13).toFloat().withSizeKeepingCentre (12.0f, 12.0f), tints[i]);
            row.removeFromLeft (9);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.0f));
            g.drawText (lines[i], row, juce::Justification::centredLeft, true);
        }
        return;
    }

    // ---- a reference is set
    const auto& ref = controller.getReference();
    const auto& m = shown;

    g.drawText (juce::String (ref.name), r.removeFromTop (26), juce::Justification::topLeft, true);
    r.removeFromTop (2);
    {
        juce::StringArray parts;
        parts.add (clock (ref.seconds));
        parts.add (juce::String (int (std::round (ref.loudnessLufs))) + " LUFS");
        parts.add (ref.channels > 1 ? "stereo" : "mono");
        if (ref.tempoConfidence > 0.35f && ref.tempoBpm > 40.0f) parts.add (juce::String (int (std::round (ref.tempoBpm))) + " BPM");
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.0f));
        g.drawText (parts.joinIntoString ("  " + juce::String (Glyph::dot()) + "  "), r.removeFromTop (16), juce::Justification::centredLeft, true);
    }

    r.removeFromTop (12);
    drawBalance (g, r.removeFromTop (kBalanceH));
    r.removeFromTop (10);

    // What it will aim for, then what it will not copy: both in the reference's own words,
    // from the same matcher the planner runs.
    auto list = r.removeFromTop (juce::jmax (0, r.getHeight() - (14 + Dine::Metric::button)));
    for (const auto& aim : m.aims)
    {
        if (list.getHeight() < kAimH) break;
        auto row = list.removeFromTop (kAimH);
        Dine::drawIcon (g, Dine::Icon::Check, row.removeFromLeft (13).toFloat().withSizeKeepingCentre (12.0f, 12.0f), Dine::accent);
        row.removeFromLeft (9);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.5f));
        g.drawText (juce::String (aim), row, juce::Justification::centredLeft, true);
    }
    for (const auto& limit : m.limits)
    {
        if (list.getHeight() < kLimitH) break;
        auto row = list.removeFromTop (kLimitH);
        Dine::drawIcon (g, Dine::Icon::Warn, row.removeFromLeft (13).toFloat().withSizeKeepingCentre (12.0f, 12.0f).withY (float (row.getY()) + 2.0f), Dine::ink3);
        row.removeFromLeft (9);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (11.5f));
        g.drawFittedText (juce::String (limit), row, juce::Justification::topLeft, 2);
    }

}

void ReferenceSheet::resized()
{
    auto r = cardBounds().reduced (26, 22);
    auto foot = r.removeFromBottom (Dine::Metric::button);
    const auto s = state();

    if (s == State::Chosen)
    {
        match.setBounds (foot.removeFromRight (juce::jmax (150, match.idealWidth())));
        foot.removeFromRight (8);
        another.setBounds (foot.removeFromRight (juce::jmax (128, another.idealWidth())));
        remove.setBounds (foot.removeFromLeft (juce::jmax (72, remove.idealWidth())));
        return;
    }
    if (s == State::Measuring) return;

    choose.setBounds (foot.removeFromRight (juce::jmax (140, choose.idealWidth())));
    close.setBounds (foot.removeFromLeft (juce::jmax (70, close.idealWidth())));
}

// Clicking the dimmed page behind the sheet closes it. Nothing here changes what is
// audible, so there is nothing to decide before it goes away.
void ReferenceSheet::mouseUp (const juce::MouseEvent& e)
{
    if (cardBounds().contains (e.getPosition()) || measurer != nullptr) return;
    if (onClose) onClose();
}

} // namespace livemix
