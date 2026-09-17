#include "TransportBar.h"

namespace livemix
{

namespace
{
    // The pill: 5 px of padding, the five keys 5 px apart, then the clock.
    constexpr int kKeyW      = 30;
    constexpr int kKeyH      = 26;
    constexpr int kKeyGap    = 5;
    constexpr int kPillPadX  = 8;
    constexpr int kClusterGap = 10;

    int keysWidth() { return kKeyW * 5 + kKeyGap * 4; }

    // The length beside the clock, the way the design writes it: hh:mm:ss.t
    juce::String shortTime (double seconds)
    {
        if (seconds < 0.0) seconds = 0.0;
        const int total = (int) seconds;
        return juce::String (total / 3600).paddedLeft ('0', 2) + ":" + juce::String ((total / 60) % 60).paddedLeft ('0', 2)
             + ":" + juce::String (total % 60).paddedLeft ('0', 2)
             + "." + juce::String (juce::jlimit (0, 9, (int) ((seconds - (double) total) * 10.0)));
    }
}

// A transport key: the control plane, a glyph drawn by hand. Play lights in the accent while
// playing, Record in red while recording, Loop in the accent while it is on.
class TransportBar::TransportButton : public juce::Button
{
public:
    enum class Glyph { Start, Play, Stop, Record, Loop };

    TransportButton (Glyph g, const juce::String& tip) : juce::Button (tip), glyph (g)
    {
        setTooltip (tip);
        setClickingTogglesState (false);
        setWantsKeyboardFocus (false);
    }

    void setActive (bool a) { if (a != active) { active = a; repaint(); } }
    void setPaused (bool p) { if (p != paused) { paused = p; repaint(); } }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        const bool lit = active;
        juce::Colour ground = down ? Dine::controlOn : over ? Dine::controlHot : Dine::control;
        if (lit) ground = glyph == Glyph::Record ? Dine::crit : Dine::accent;
        if (! isEnabled()) ground = ground.withAlpha (0.5f);
        Dine::fillRounded (g, r, ground, Dine::Radius::control);

        juce::Colour ink = lit ? Dine::onAccent : over ? Dine::ink : Dine::ink2;
        if (glyph == Glyph::Record && ! lit) ink = Dine::crit;
        if (! isEnabled()) ink = ink.withAlpha (0.5f);

        auto c = r.getCentre();
        const float s = 5.0f;
        g.setColour (ink);

        switch (glyph)
        {
            case Glyph::Play:
                if (paused)
                {
                    g.fillRect (juce::Rectangle<float> (2.4f, s * 2.0f).withCentre ({ c.x - 2.6f, c.y }));
                    g.fillRect (juce::Rectangle<float> (2.4f, s * 2.0f).withCentre ({ c.x + 2.6f, c.y }));
                }
                else
                {
                    juce::Path p;
                    p.addTriangle (c.x - s * 0.7f, c.y - s, c.x - s * 0.7f, c.y + s, c.x + s * 0.9f, c.y);
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
                juce::Path p;
                p.addCentredArc (c.x, c.y, s * 0.95f, s * 0.95f, 0.0f, 0.55f, juce::MathConstants<float>::twoPi - 0.35f, true);
                g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                juce::Path arrow;
                arrow.addTriangle (c.x + s * 0.55f, c.y - s * 1.35f, c.x + s * 1.2f, c.y - s * 0.55f, c.x + s * 0.2f, c.y - s * 0.55f);
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
    make (startButton, TransportButton::Glyph::Start, "Return to start (Return)");
    make (stopButton, TransportButton::Glyph::Stop, "Stop (Space)");
    make (playButton, TransportButton::Glyph::Play, "Play (Space)");
    make (recordButton, TransportButton::Glyph::Record, "Record: captures every track with its R key on (R)");
    make (loopButton, TransportButton::Glyph::Loop, "Loop the marked range (L)");

    startButton->onClick = [this] { returnToStart(); };
    playButton->onClick = [this] { togglePlay(); };
    stopButton->onClick = [this] { if (services.daw().getTransport().isPlaying()) togglePlay(); };
    recordButton->onClick = [this] { toggleRecord(); };
    loopButton->onClick = [this] { toggleLoop(); };
    setOpaque (false);
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
        if (onToast) onToast (takes == 1 ? "Recording stopped. 1 track was written." : "Recording stopped. " + juce::String (takes) + " tracks were written.");
        if (onTimelineChanged) onTimelineChanged();
        refresh();
        return;
    }
    if (! services.isAudioRunning()) { if (onToast) onToast ("Open an audio device first."); return; }

    if (services.sessionFolder() == juce::File())
    {
        const juce::String name = services.currentSessionName().isNotEmpty() ? services.currentSessionName() : "Untitled";
        const auto saveError = services.saveSessionAs (name);
        if (saveError.isNotEmpty()) { if (onToast) onToast (saveError); return; }
        if (onToast) onToast ("Saved \"" + name + "\". The recordings go in its folder.");
    }

    const auto err = daw.startRecording();
    if (err.isNotEmpty()) { if (onToast) onToast (err); return; }
    if (onToast) onToast ("Recording. " + juce::String (daw.getProject().numArmed()) + " inputs are being written to disk.");
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
        const auto full = Transport::formatTime (transport.getPositionSeconds());
        // hh:mm:ss.t - one decimal, the way the design's clock reads
        timeText = full.length() > 4 ? full.dropLastCharacters (2) : full;
        const double rate = transport.getSampleRate();
        lengthText = shortTime (rate > 0.0 ? double (length) / rate : 0.0);
        repaint (clockWell);
    }
}

// ---------------------------------------------------------------- measurement
int TransportBar::clockCellWidth() const
{
    return Dine::textWidth (Dine::mono (16.0f, 500).withExtraKerningFactor (0.04f), "00:00:00.0") + 8;
}

int TransportBar::lengthCellWidth() const
{
    return Dine::textWidth (Dine::mono (11.0f), "/ 00:00:00.0") + 8;
}

int TransportBar::idealWidth() const
{
    return kPillPadX * 2 + keysWidth() + kClusterGap + clockCellWidth() + lengthCellWidth() + 4;
}

int TransportBar::minimumWidth() const
{
    return kPillPadX * 2 + keysWidth() + kClusterGap + clockCellWidth();
}

int TransportBar::keysOnlyWidth() const { return kPillPadX * 2 + keysWidth(); }

// ---------------------------------------------------------------- paint
void TransportBar::paint (juce::Graphics& g)
{
    Dine::fillRounded (g, getLocalBounds().toFloat(), Dine::menubar, Dine::Radius::card);
    if (! showClock) return;
    g.setColour (recording ? Dine::crit : Dine::ink);
    g.setFont (Dine::mono (16.0f, 500).withExtraKerningFactor (0.04f));
    g.drawText (timeText, timeCell, juce::Justification::centredLeft);
    if (showLength)
    {
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (11.0f));
        g.drawText ("/ " + lengthText, lengthCell, juce::Justification::centredLeft);
    }
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (kPillPadX, 0);
    auto keys = r.removeFromLeft (keysWidth()).withSizeKeepingCentre (keysWidth(), kKeyH);
    keysWell = keys;
    auto place = [&keys] (juce::Component& c, int w)
    {
        c.setBounds (keys.removeFromLeft (w));
        keys.removeFromLeft (kKeyGap);
    };
    place (*startButton, kKeyW);
    place (*stopButton, kKeyW);
    place (*playButton, kKeyW);
    place (*recordButton, kKeyW);
    place (*loopButton, kKeyW);

    r.removeFromLeft (kClusterGap);
    showLength = r.getWidth() >= clockCellWidth() + lengthCellWidth();
    showClock = r.getWidth() >= clockCellWidth();
    if (! showClock) { clockWell = timeCell = lengthCell = {}; return; }
    clockWell = r;
    timeCell = r.removeFromLeft (clockCellWidth()).withTrimmedTop (1);
    lengthCell = r.withTrimmedTop (3);
}

} // namespace livemix
