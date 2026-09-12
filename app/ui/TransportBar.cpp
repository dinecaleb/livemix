#include "TransportBar.h"

namespace livemix
{

namespace
{
    constexpr int kKeyW      = 30;   // go to start, stop, record, loop
    constexpr int kPlayW     = 44;   // play is the one you reach for without looking
    constexpr int kKeyH      = 26;
    constexpr int kKeyGap    = 3;
    constexpr int kWellPad   = 3;
    constexpr int kClusterGap = 10;

    int keysWellWidth() { return kWellPad * 2 + kKeyW * 4 + kPlayW + kKeyGap * 4; }

    // The length beside the clock, short enough to sit in a toolbar: m:ss.s.
    juce::String shortTime (double seconds)
    {
        if (seconds < 0.0) seconds = 0.0;
        const int total = (int) seconds;
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2)
             + "." + juce::String (juce::jlimit (0, 9, (int) ((seconds - (double) total) * 10.0)));
    }
}

// A transport key: a glyph drawn by hand so the shapes stay crisp at 26 px.
class TransportBar::TransportButton : public juce::Button
{
public:
    enum class Glyph { Start, Play, Stop, Record, Loop };

    TransportButton (Glyph g, const juce::String& tip) : juce::Button (tip), glyph (g)
    {
        setTooltip (tip);
        setClickingTogglesState (false);
    }

    void setActive (bool a) { if (a != active) { active = a; repaint(); } }
    void setPaused (bool p) { if (p != paused) { paused = p; repaint(); } }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        const bool lit = active || (down && glyph == Glyph::Play);

        if (glyph == Glyph::Play)
        {
            // Play carries the accent whether it is running or waiting: it is the key
            // an operator hits without looking.
            juce::ColourGradient grad (lit ? Dine::accentTop : Dine::fill, r.getX(), r.getY(),
                                       lit ? Dine::accentBottom : Dine::fill, r.getX(), r.getBottom(), false);
            g.setGradientFill (grad);
            juce::Path p;
            p.addRoundedRectangle (r, Dine::Radius::chip);
            g.fillPath (p);
        }
        else if (lit)
        {
            const juce::Colour tint = glyph == Glyph::Record ? Dine::crit.withAlpha (0.9f)
                                                             : Dine::accent.withAlpha (0.22f);
            Dine::fillRounded (g, r, tint, Dine::Radius::chip);
        }
        else if (over)
        {
            Dine::fillRounded (g, r, Dine::fill, Dine::Radius::chip);
        }

        juce::Colour ink = Dine::ink2;
        if (glyph == Glyph::Play)        ink = lit ? Dine::onAccent : Dine::ink;
        else if (glyph == Glyph::Record) ink = lit ? Dine::onAccent : Dine::ink2;
        else if (glyph == Glyph::Loop)   ink = lit ? Dine::accent.brighter (0.35f) : Dine::ink2;
        if (! isEnabled()) ink = Dine::ink4;

        auto c = r.getCentre();
        const float s = 6.0f;
        g.setColour (ink);

        switch (glyph)
        {
            case Glyph::Play:
                if (paused)
                {
                    g.fillRect (juce::Rectangle<float> (2.6f, s * 2.0f).withCentre ({ c.x - 2.6f, c.y }));
                    g.fillRect (juce::Rectangle<float> (2.6f, s * 2.0f).withCentre ({ c.x + 2.6f, c.y }));
                }
                else
                {
                    juce::Path p;
                    p.addTriangle (c.x - s * 0.55f, c.y - s, c.x - s * 0.55f, c.y + s, c.x + s * 0.9f, c.y);
                    g.fillPath (p);
                }
                break;
            case Glyph::Stop:
                g.fillRect (juce::Rectangle<float> (s * 1.7f, s * 1.7f).withCentre (c));
                break;
            case Glyph::Record:
                g.fillEllipse (juce::Rectangle<float> (s * 1.8f, s * 1.8f).withCentre (c));
                break;
            case Glyph::Start:
            {
                juce::Path p;
                p.addTriangle (c.x + s * 0.9f, c.y - s, c.x + s * 0.9f, c.y + s, c.x - s * 0.4f, c.y);
                g.fillPath (p);
                g.fillRect (juce::Rectangle<float> (1.8f, s * 2.0f).withCentre ({ c.x - s * 0.9f, c.y }));
                break;
            }
            case Glyph::Loop:
            {
                const auto box = juce::Rectangle<float> (s * 2.2f, s * 1.5f).withCentre (c);
                juce::Path p;
                p.addRoundedRectangle (box, s * 0.7f);
                g.strokePath (p, juce::PathStrokeType (1.4f));
                juce::Path arrow;
                arrow.addTriangle (box.getCentreX() - 2.4f, box.getY() - 3.0f,
                                   box.getCentreX() - 2.4f, box.getY() + 3.0f,
                                   box.getCentreX() + 2.0f, box.getY());
                g.fillPath (arrow);
                break;
            }
        }
    }

private:
    Glyph glyph;
    bool active = false, paused = false;
};

TransportBar::TransportBar (MixController& c, AppServices& s) : controller (c), services (s)
{
    auto make = [this] (std::unique_ptr<TransportButton>& b, TransportButton::Glyph g, const juce::String& tip)
    {
        b = std::make_unique<TransportButton> (g, tip);
        addAndMakeVisible (*b);
    };
    make (startButton, TransportButton::Glyph::Start, "Go to the start (Return)");
    make (stopButton, TransportButton::Glyph::Stop, "Stop (Space)");
    make (playButton, TransportButton::Glyph::Play, "Play (Space)");
    make (recordButton, TransportButton::Glyph::Record, "Record: captures every track with its R key on (R)");
    make (loopButton, TransportButton::Glyph::Loop, "Loop the marked range (L)");

    startButton->onClick = [this] { returnToStart(); };
    playButton->onClick = [this] { togglePlay(); };
    stopButton->onClick = [this] { if (services.daw().getTransport().isPlaying()) togglePlay(); };
    recordButton->onClick = [this] { toggleRecord(); };
    loopButton->onClick = [this] { toggleLoop(); };
}

TransportBar::~TransportBar() = default;

void TransportBar::togglePlay()
{
    auto& daw = services.daw();
    if (daw.getTransport().isPlaying())
    {
        const bool wasRecording = daw.isRecording();
        daw.stop();
        if (wasRecording)
        {
            if (onToast) onToast ("Recording stopped.");
            if (onTimelineChanged) onTimelineChanged();
        }
    }
    else
    {
        if (! services.isAudioRunning()) { if (onToast) onToast ("Open an audio device first."); return; }
        daw.play();
    }
    refresh();
}

void TransportBar::toggleRecord()
{
    auto& daw = services.daw();
    if (daw.isRecording())
    {
        const int takes = daw.stopRecording();
        daw.stop();
        if (onToast) onToast (takes == 1 ? "Recorded 1 track." : "Recorded " + juce::String (takes) + " tracks.");
        if (onTimelineChanged) onTimelineChanged();
        refresh();
        return;
    }
    if (! services.isAudioRunning()) { if (onToast) onToast ("Open an audio device first."); return; }

    // Recording needs somewhere to put the audio. Rather than refusing, give the session its
    // folder now and say where it went.
    if (services.sessionFolder() == juce::File())
    {
        const juce::String name = services.currentSessionName().isNotEmpty() ? services.currentSessionName() : "Untitled";
        const auto saveError = services.saveSessionAs (name);
        if (saveError.isNotEmpty()) { if (onToast) onToast (saveError); return; }
        if (onToast) onToast ("Saved \"" + name + "\". The recordings go in its folder.");
    }

    const auto err = daw.startRecording();
    if (err.isNotEmpty()) { if (onToast) onToast (err); return; }
    if (onToast) onToast ("Recording.");
    refresh();
}

void TransportBar::returnToStart()
{
    services.daw().locate (0);
    refresh();
}

void TransportBar::toggleLoop()
{
    auto& daw = services.daw();
    auto& project = daw.getProject();
    if (project.loopEnd <= project.loopStart)
    {
        const auto length = project.lengthSamples();
        project.loopStart = 0;
        project.loopEnd = juce::jmax ((juce::int64) 1, length);
    }
    daw.setLoop (! project.loopEnabled, project.loopStart, project.loopEnd);
    refresh();
}

void TransportBar::refresh()
{
    auto& daw = services.daw();
    const auto& transport = daw.getTransport();

    const bool nowPlaying = transport.isPlaying();
    const bool nowRecording = daw.isRecording();
    const bool nowLooping = daw.getProject().loopEnabled;
    const juce::int64 position = transport.getPosition();
    const juce::int64 length = daw.getProject().lengthSamples();

    playButton->setActive (nowPlaying && ! nowRecording);
    playButton->setPaused (nowPlaying);
    recordButton->setActive (nowRecording);
    loopButton->setActive (nowLooping);

    const auto err = daw.getRecorder().getError();
    if (err.isNotEmpty() && nowRecording)
    {
        daw.stopRecording();
        daw.stop();
        if (onToast) onToast (err);
        if (onTimelineChanged) onTimelineChanged();
    }

    if (position != lastPosition || length != lastLength || nowPlaying != playing
        || nowRecording != recording || nowLooping != looping)
    {
        lastPosition = position;
        lastLength = length;
        playing = nowPlaying;
        recording = nowRecording;
        looping = nowLooping;
        timeText = Transport::formatTime (transport.getPositionSeconds());
        const double rate = transport.getSampleRate();
        lengthText = shortTime (rate > 0.0 ? double (length) / rate : 0.0);
        repaint();
    }
}

// ---------------------------------------------------------------- measurement
int TransportBar::clockCellWidth() const
{
    return juce::jmax (118, Dine::textWidth (Dine::mono (16.0f), "00:00:00.000") + 24);
}

int TransportBar::lengthCellWidth() const
{
    return juce::jmax (74, Dine::textWidth (Dine::mono (16.0f), "00:00.0") + 24);
}

int TransportBar::idealWidth() const
{
    return keysWellWidth() + kClusterGap + clockCellWidth() + 1 + lengthCellWidth();
}

int TransportBar::minimumWidth() const
{
    return keysWellWidth() + kClusterGap + clockCellWidth();
}

// ---------------------------------------------------------------- paint
void TransportBar::paint (juce::Graphics& g)
{
    Dine::fillRounded (g, keysWell.toFloat(), juce::Colours::black.withAlpha (0.34f), 8.0f);
    Dine::hairlineRounded (g, keysWell.toFloat(), Dine::hairSoft, 8.0f);

    if (! showClock) return;
    Dine::fillRounded (g, clockWell.toFloat(), juce::Colours::black.withAlpha (0.42f), 8.0f);
    if (showLength)
    {
        // The second cell sits a shade lighter, with the well showing through between them.
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path rounded;
        rounded.addRoundedRectangle (clockWell.toFloat(), 8.0f);
        g.reduceClipRegion (rounded);
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        g.fillRect (lengthCell);
        g.setColour (Dine::hairSoft);
        g.fillRect (float (lengthCell.getX()) - 1.0f, float (clockWell.getY()), 1.0f, float (clockWell.getHeight()));
    }
    Dine::hairlineRounded (g, clockWell.toFloat(), Dine::hairSoft, 8.0f);

    auto cell = [&g] (juce::Rectangle<int> r, const juce::String& caption,
                      const juce::String& value, juce::Colour valueColour)
    {
        auto inner = r.reduced (11, 0).withTrimmedTop (4).withTrimmedBottom (3);
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (9.5f, 600).withExtraKerningFactor (0.09f));
        g.drawText (caption, inner.removeFromTop (11), juce::Justification::centredLeft);
        g.setColour (valueColour);
        g.setFont (Dine::mono (16.0f));
        g.drawText (value, inner, juce::Justification::centredLeft);
    };

    cell (timeCell, "TIMECODE", timeText, recording ? Dine::crit : Dine::ink);
    if (showLength) cell (lengthCell, "LENGTH", lengthText, Dine::ink2);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().withSizeKeepingCentre (getWidth(), juce::jmin (getHeight(), height));

    // The keys sit in a shorter well than the clock, which carries two lines.
    keysWell = r.removeFromLeft (keysWellWidth()).withSizeKeepingCentre (keysWellWidth(), juce::jmin (r.getHeight(), 32));
    auto keys = keysWell.reduced (kWellPad).withSizeKeepingCentre (keysWell.getWidth() - kWellPad * 2, kKeyH);
    auto place = [&keys] (juce::Component& c, int w)
    {
        c.setBounds (keys.removeFromLeft (w));
        keys.removeFromLeft (kKeyGap);
    };
    place (*startButton, kKeyW);
    place (*stopButton, kKeyW);
    place (*playButton, kPlayW);
    place (*recordButton, kKeyW);
    place (*loopButton, kKeyW);

    r.removeFromLeft (kClusterGap);
    // The cells are dropped whole, never squeezed: a clock reading "00:..." is worse than
    // no clock at all, and the keys still say what the transport is doing.
    showLength = r.getWidth() >= clockCellWidth() + 1 + lengthCellWidth();
    showClock = r.getWidth() >= clockCellWidth();
    if (! showClock)
    {
        clockWell = timeCell = lengthCell = {};
        return;
    }
    clockWell = r.removeFromLeft (showLength ? clockCellWidth() + 1 + lengthCellWidth() : clockCellWidth());
    auto cells = clockWell;
    timeCell = cells.removeFromLeft (clockCellWidth());
    cells.removeFromLeft (1);
    lengthCell = cells;
}

} // namespace livemix
