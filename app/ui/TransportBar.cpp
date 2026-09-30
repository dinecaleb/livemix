#include "TransportBar.h"

namespace livemix
{

namespace
{
    // The well (design: `Transport v2`, 117:10151): 4 px in on the left, 14 on the right, four
    // 28 x 28 keys 2 px apart, a divider, then the clock. Loop is not a transport key in v3 -
    // it is a button on the TRACKS tool row, where the loop it sets is drawn.
    constexpr int kKeyW      = 28;
    constexpr int kKeyH      = 28;
    constexpr int kKeyGap    = 2;
    constexpr int kPadLeft   = 4;
    constexpr int kPadRight  = 14;
    constexpr int kClusterGap = 6;
    constexpr int kNumKeys   = 4;

    int keysWidth() { return kKeyW * kNumKeys + kKeyGap * (kNumKeys - 1); }

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
        // Flat inside the well until the key is doing something: then a quiet plane, and the
        // record ground while a take is being written.
        if (lit)        Dine::fillRounded (g, r, glyph == Glyph::Record ? Dine::recGround : Dine::selected, Dine::Radius::control);
        else if (down)  Dine::fillRounded (g, r, Dine::selected, Dine::Radius::control);
        else if (over)  Dine::fillRounded (g, r, Dine::control, Dine::Radius::control);

        juce::Colour ink = lit ? (glyph == Glyph::Record ? Dine::keyRec : Dine::accent)
                               : over ? Dine::ink : Dine::ink2;
        if (glyph == Glyph::Record && ! lit) ink = Dine::keyRec;
        if (! isEnabled()) ink = ink.withAlpha (0.5f);

        // One glyph family, all filled, all drawn in a 12 pt box at the same weight.
        auto c = r.getCentre();
        const float s = 6.0f;
        g.setColour (ink);

        switch (glyph)
        {
            case Glyph::Play:
                if (paused)
                {
                    g.fillRect (juce::Rectangle<float> (2.6f, s * 1.7f).withCentre ({ c.x - 2.8f, c.y }));
                    g.fillRect (juce::Rectangle<float> (2.6f, s * 1.7f).withCentre ({ c.x + 2.8f, c.y }));
                }
                else
                {
                    // nudged 1 pt right so a triangle sits optically centred
                    juce::Path p;
                    p.addTriangle (c.x - s * 0.55f + 1.0f, c.y - s * 0.85f, c.x - s * 0.55f + 1.0f, c.y + s * 0.85f,
                                   c.x + s * 0.85f + 1.0f, c.y);
                    g.fillPath (p);
                }
                break;
            case Glyph::Stop:
                g.fillRect (juce::Rectangle<float> (s * 1.5f, s * 1.5f).withCentre (c));
                break;
            case Glyph::Record:
                g.fillEllipse (juce::Rectangle<float> (s * 1.7f, s * 1.7f).withCentre (c));
                break;
            case Glyph::Start:
            {
                juce::Path p;
                p.addTriangle (c.x + s * 0.85f, c.y - s * 0.85f, c.x + s * 0.85f, c.y + s * 0.85f, c.x - s * 0.35f, c.y);
                g.fillPath (p);
                g.fillRect (juce::Rectangle<float> (2.0f, s * 1.7f).withCentre ({ c.x - s * 0.85f, c.y }));
                break;
            }
            case Glyph::Loop:
            {
                juce::Path p;
                p.addCentredArc (c.x, c.y, s * 0.85f, s * 0.85f, 0.0f, 0.55f, juce::MathConstants<float>::twoPi - 0.35f, true);
                g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                juce::Path arrow;
                arrow.addTriangle (c.x + s * 0.5f, c.y - s * 1.25f, c.x + s * 1.1f, c.y - s * 0.5f, c.x + s * 0.15f, c.y - s * 0.5f);
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

    startButton->onClick = [this] { returnToStart(); };
    playButton->onClick = [this] { togglePlay(); };
    stopButton->onClick = [this] { if (services.daw().getTransport().isPlaying()) togglePlay(); };
    recordButton->onClick = [this] { toggleRecord(); };
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

    if (const auto notice = daw.takeStopNotice(); notice.isNotEmpty())
    {
        if (onToast) onToast (notice);
        if (onTimelineChanged) onTimelineChanged();
    }
    // The disk fell behind and some of the take is silence - every track still lines up, and
    // the take goes on. Said once per take, because the volunteer needs to know before Monday.
    if (! nowRecording) dropSaid = false;
    else if (! dropSaid && daw.getRecorder().getDroppedSeconds() > 0.0)
    {
        dropSaid = true;
        if (onToast)
            onToast ("The recording disk fell behind for a moment, so a short gap in this take is silent on every track. "
                     "Recording goes on. Close other apps, or record to a faster drive.");
    }
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
    return Dine::textWidth (Dine::mono (17.0f, 500), "00:00:00.0") + 2;
}

int TransportBar::lengthCellWidth() const
{
    return Dine::textWidth (Dine::mono (11.0f), "/ 00:00:00.0") + 10;
}

int TransportBar::idealWidth() const
{
    return kPadLeft + keysWidth() + kClusterGap + 1 + kClusterGap + clockCellWidth() + lengthCellWidth() + kPadRight;
}

int TransportBar::minimumWidth() const
{
    return kPadLeft + keysWidth() + kClusterGap + 1 + kClusterGap + clockCellWidth() + kPadRight;
}

int TransportBar::keysOnlyWidth() const { return kPadLeft + keysWidth() + kPadRight; }

// ---------------------------------------------------------------- paint
void TransportBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Dine::fillRounded (g, r, Dine::menubar, Dine::Radius::well);
    Dine::hairlineRounded (g, r.reduced (0.5f), Dine::hair, Dine::Radius::well);
    if (! showClock) return;
    if (! divider.isEmpty())
    {
        g.setColour (Dine::hair);
        g.fillRect (divider);
    }
    // The clock turns red while a take is being written.
    g.setColour (recording ? Dine::keyRec : Dine::ink);
    g.setFont (Dine::mono (17.0f, 500));
    Dine::drawText (g, timeText, timeCell, juce::Justification::centredLeft);
    if (showLength)
    {
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (11.0f));
        Dine::drawText (g, "/ " + lengthText, lengthCell, juce::Justification::centredLeft);
    }
}

void TransportBar::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (kPadLeft).withTrimmedRight (kPadRight);
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

    r.removeFromLeft (kClusterGap);
    showLength = r.getWidth() >= 1 + kClusterGap + clockCellWidth() + lengthCellWidth();
    showClock = r.getWidth() >= 1 + kClusterGap + clockCellWidth();
    if (! showClock) { divider = clockWell = timeCell = lengthCell = {}; return; }
    divider = r.removeFromLeft (1).withSizeKeepingCentre (1, 20);
    r.removeFromLeft (kClusterGap);
    clockWell = r;
    timeCell = r.removeFromLeft (clockCellWidth());
    lengthCell = r.withTrimmedTop (3);
}

} // namespace livemix
