#include "Tutorial.h"

namespace livemix
{

namespace
{
    juce::File seenMarker()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile ("DLIVE").getChildFile (".getting-started-seen");
    }
}

// Seven sentences, in the order a Sunday actually happens. Every one of them is a thing
// the person will do in the next twenty minutes; none of them is a feature tour.
const std::vector<Tutorial::Step>& Tutorial::steps()
{
    static const std::vector<Step> all {
        { "GETTING STARTED  1 / 7", "This is your session.",
          "Everything you set up, record and mix lives in a session, and it saves itself. The "
          "sidebar is where you open one, set up the inputs and pick a workspace; this button "
          "shows or hides it.",
          0 /* Sessions */, "session" },

        { "GETTING STARTED  2 / 7", "Tell DLIVE what is plugged in.",
          "Pick your audio interface, then give each input a name and say what it is — a kick "
          "drum, a lead vocal, the pastor's microphone. That is the only thing DLIVE needs to "
          "know to build a mix; it works the rest out by listening.",
          1 /* Device */, "tabs" },

        { "GETTING STARTED  3 / 7", "Press record and play for thirty seconds.",
          "Press the red button, ask the band to play. Every input is recorded on its own, so you "
          "can mix it again afterwards — and DLIVE has something real to listen to.",
          4 /* Tracks */, "transport" },

        { "GETTING STARTED  4 / 7", "TUNE MIX does the mix.",
          "DLIVE listens to the band for thirty seconds and sets every level, tone and effect for "
          "this room. Then it tells you what it did and why, in sentences. Nothing changes until "
          "you press KEEP, and REVERT puts it all back.",
          6 /* Tune */, "tabs" },

        { "GETTING STARTED  5 / 7", "The console, if you want it.",
          "Faders, mutes and the meters. M is mute, S is solo, R sets a track to record. Solo only "
          "goes to your own headphones — the room and the stream never hear it.",
          5 /* Mixer */, "rail" },

        { "GETTING STARTED  6 / 7", "One channel, in full detail.",
          "Every processor on a channel, what it is set to, and whether DLIVE set it or you did. "
          "Change anything you like; the next TUNE MIX works around you.",
          8 /* Inspector */, "tabs" },

        { "GETTING STARTED  7 / 7", "Before the service starts, lock it.",
          "LIVE SAFE stops anything that could change the whole mix by accident — re-routing, "
          "re-tuning, opening another session — and limits how far one fader can move. Mute, solo "
          "and the recording always stay free.",
          7 /* Live */, "livesafe" },
    };
    return all;
}

bool Tutorial::hasBeenSeen() { return seenMarker().existsAsFile(); }

void Tutorial::markSeen()
{
    auto f = seenMarker();
    f.getParentDirectory().createDirectory();
    f.replaceWithText ("1");
}

Tutorial::Tutorial()
{
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    backButton.onClick = [this] { go (step - 1); };
    nextButton.onClick = [this]
    {
        if (step + 1 >= int (steps().size())) { markSeen(); if (onFinished) onFinished(); return; }
        go (step + 1);
    };
    skipButton.onClick = [this] { markSeen(); if (onFinished) onFinished(); };
    addAndMakeVisible (backButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (skipButton);
}

void Tutorial::start() { go (0); }

void Tutorial::go (int index)
{
    step = juce::jlimit (0, int (steps().size()) - 1, index);
    const auto& s = steps()[size_t (step)];
    if (onStep) onStep (s.page);
    spot = spotFor && juce::String (s.spot).isNotEmpty() ? spotFor (s.spot) : juce::Rectangle<int>();
    backButton.setEnabled (step > 0);
    nextButton.setButtonText (step + 1 >= int (steps().size()) ? "Start mixing" : "Next");
    resized();
    repaint();
    grabKeyboardFocus();
}

bool Tutorial::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::escapeKey)) { markSeen(); if (onFinished) onFinished(); return true; }
    if (k.isKeyCode (juce::KeyPress::rightKey) || k.isKeyCode (juce::KeyPress::returnKey)) { nextButton.triggerClick(); return true; }
    if (k.isKeyCode (juce::KeyPress::leftKey)) { if (step > 0) go (step - 1); return true; }
    return false;
}

juce::Rectangle<int> Tutorial::cardBounds() const
{
    const int w = juce::jmin (560, getWidth() - 48);
    const int h = 216;
    // Under whatever is being pointed at, so the ring and the sentence are read together.
    int y = spot.isEmpty() ? getHeight() - h - 56
                           : juce::jlimit (16, getHeight() - h - 16, spot.getBottom() + 22);
    int x = spot.isEmpty() ? (getWidth() - w) / 2
                           : juce::jlimit (16, juce::jmax (16, getWidth() - w - 16), spot.getCentreX() - w / 2);
    return { x, y, w, h };
}

void Tutorial::paint (juce::Graphics& g)
{
    // The scrim, with a hole cut where the step is pointing. A hole rather than a ring on
    // top, so what is being explained is the only thing at full brightness on the screen.
    if (spot.isEmpty())
    {
        g.setColour (Dine::desk.withAlpha (0.66f));
        g.fillAll();
    }
    else
    {
        juce::Path scrim;
        scrim.addRectangle (getLocalBounds().toFloat());
        juce::Path hole;
        hole.addRoundedRectangle (spot.toFloat().expanded (6.0f), 8.0f);
        scrim.setUsingNonZeroWinding (false);
        scrim.addPath (hole);
        g.setColour (Dine::desk.withAlpha (0.74f));
        g.fillPath (scrim);
        g.setColour (Dine::accent.withAlpha (0.85f));
        g.drawRoundedRectangle (spot.toFloat().expanded (6.0f), 8.0f, 1.5f);
    }

    const auto& s = steps()[size_t (step)];
    auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), 14.0f);

    auto inner = card.reduced (22, 18);
    inner.removeFromBottom (kFooterH);          // the buttons place themselves there
    auto dots = inner.removeFromBottom (12);
    inner.removeFromBottom (8);

    g.setColour (Dine::accent);
    g.setFont (Dine::caps (10.0f, 0.14f));
    g.drawText (s.eyebrow, inner.removeFromTop (14), juce::Justification::topLeft);
    inner.removeFromTop (6);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (21.0f, 600));
    g.drawText (s.title, inner.removeFromTop (26), juce::Justification::topLeft, true);
    inner.removeFromTop (6);

    g.setColour (Dine::ink2);
    g.setFont (Dine::text (12.5f));
    g.drawFittedText (s.body, inner, juce::Justification::topLeft, 4);

    // Seven dots: where you are, and how much is left.
    for (int i = 0; i < int (steps().size()); ++i)
    {
        auto d = dots.removeFromLeft (13).withSizeKeepingCentre (i == step ? 7 : 5, i == step ? 7 : 5);
        g.setColour (i == step ? Dine::accent : Dine::ink4.withAlpha (0.5f));
        g.fillEllipse (d.toFloat());
    }
}

void Tutorial::resized()
{
    auto row = cardBounds().reduced (22, 18).removeFromBottom (kFooterH).withHeight (26);
    nextButton.setBounds (row.removeFromRight (juce::jmax (92, nextButton.idealWidth())));
    row.removeFromRight (8);
    backButton.setBounds (row.removeFromRight (juce::jmax (64, backButton.idealWidth())));
    skipButton.setBounds (row.removeFromLeft (juce::jmin (row.getWidth(),
                                                          juce::jmax (96, skipButton.idealWidth()))));
}

} // namespace livemix
