#include "TransportBar.h"

namespace livemix
{

namespace
{
    // The v4 transport (docs/design/v4): one pill, 36 high and round-ended, holding five
    // 32 x 28 keys 2 apart - back to the start, stop, play, record, loop - and beside it, off
    // the pill, the clock on two lines: the time to the millisecond, and "of" the session's
    // length under it.
    constexpr int kKeyW      = 32;
    constexpr int kKeyH      = 28;
    constexpr int kKeyGap    = 2;
    constexpr int kPad       = 5;
    constexpr int kClockGap  = 8;
    constexpr int kNumKeys   = 5;

    int keysWidth() { return kKeyW * kNumKeys + kKeyGap * (kNumKeys - 1); }
    int pillWidth() { return kPad + keysWidth() + kPad; }

    // The length under the clock, the way the design writes it: hh:mm:ss
    juce::String shortTime (double seconds)
    {
        if (seconds < 0.0) seconds = 0.0;
        const int total = (int) seconds;
        return juce::String (total / 3600).paddedLeft ('0', 2) + ":" + juce::String ((total / 60) % 60).paddedLeft ('0', 2)
             + ":" + juce::String (total % 60).paddedLeft ('0', 2);
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
        // Flat inside the pill until the key is doing something: then a lifted round plane
        // (white at .16), and the record ground while a take is being written.
        const float radius = r.getHeight() * 0.5f;
        if (lit)        Dine::fillRounded (g, r, glyph == Glyph::Record ? Dine::recGround : juce::Colours::white.withAlpha (0.16f), radius);
        else if (down)  Dine::fillRounded (g, r, juce::Colours::white.withAlpha (0.12f), radius);
        else if (over)  Dine::fillRounded (g, r, juce::Colours::white.withAlpha (0.08f), radius);

        juce::Colour ink = lit ? (glyph == Glyph::Record ? Dine::keyRec : Dine::ink)
                               : over ? Dine::ink : (glyph == Glyph::Loop ? Dine::ink2 : Dine::ink);
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
    make (loopButton, TransportButton::Glyph::Loop, "Loop (L)");

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
        stopPressed = wasRecording;
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
        stopPressed = true;
        const int takes = daw.stopRecording();
        daw.stop();
        if (onToast) onToast (takes == 1 ? "Recording stopped. 1 track was written." : "Recording stopped. " + juce::String (takes) + " tracks were written.");
        if (onTimelineChanged) onTimelineChanged();
        refresh();
        return;
    }
    if (! services.isAudioRunning()) { if (onToast) onToast ("Open an audio device first."); return; }

    // NOTHING TO RECORD FROM IS SAID, NOT RECORDED. An output-only device (the microphone
    // refused, "Not now", the console not plugged in) counts as running, and the recorder
    // writes silence for a channel that is not there - so REC used to light up and write
    // hours of nothing. Every armed track has to reach a channel the device has open.
    {
        const auto& inputs = daw.getSession().inputs;
        const auto& tracks = daw.getProject().tracks;
        const int open = services.numInputChannels();
        int armed = 0, reachable = 0;
        for (size_t t = 0; t < tracks.size() && t < inputs.size(); ++t)
        {
            if (! tracks[t].armed) continue;
            ++armed;
            const auto& in = inputs[t];
            if (in.inputA >= 0 && in.inputA < open && in.inputB < open) ++reachable;
        }
        if (armed > 0 && reachable < armed)
        {
            if (onToast)
                onToast (open <= 0 ? juce::String ("Nothing to record from: no inputs are open. Choose the console under Audio device "
                                                   "(and allow the microphone in System Settings if macOS asked).")
                                   : juce::String (armed - reachable) + (armed - reachable == 1 ? " track that is" : " tracks that are")
                                         + " set to record " + (armed - reachable == 1 ? "listens" : "listen")
                                         + " to an input this device does not have. Fix the assignments, or take them off record.");
            return;
        }
    }

    if (services.sessionFolder() == juce::File())
    {
        // A name no other session has: REC must never be refused, or overwrite another
        // session, because of what this one happens to be called.
        const juce::String name = SessionStore::unusedName (services.currentSessionName().isNotEmpty() ? services.currentSessionName() : "Untitled");
        const auto saveError = services.saveSessionAs (name);
        if (saveError.isNotEmpty()) { if (onToast) onToast (saveError); return; }
        if (onToast) onToast ("Saved \"" + name + "\". The recordings go in its folder.");
    }

    const auto err = daw.startRecording();
    if (err.isNotEmpty()) { if (onToast) onToast (err); return; }
    stoppedByItself = false;
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

    // A take that ended with nobody pressing stop - the device changed rate, the disk failed.
    if (recording && ! nowRecording && ! stopPressed) stoppedByItself = true;
    if (! nowRecording) stopPressed = false;
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
        stoppedByItself = true;
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
        // hh:mm:ss.mmm, the way the v4 clock reads
        timeText = Transport::formatTime (transport.getPositionSeconds());
        const double rate = transport.getSampleRate();
        lengthText = shortTime (rate > 0.0 ? double (length) / rate : 0.0);
        repaint (clockWell.expanded (2));
    }
}

// ---------------------------------------------------------------- measurement
int TransportBar::clockCellWidth() const
{
    return juce::jmax (Dine::textWidth (Dine::mono (15.0f, 500), "00:00:00.000"),
                       Dine::textWidth (Dine::mono (10.5f), "of 00:00:00")) + 2;
}

int TransportBar::lengthCellWidth() const { return 0; }   // the length sits under the clock in v4

int TransportBar::idealWidth() const   { return pillWidth() + kClockGap + clockCellWidth(); }
int TransportBar::minimumWidth() const { return idealWidth(); }
int TransportBar::keysOnlyWidth() const { return pillWidth(); }

// ---------------------------------------------------------------- paint
void TransportBar::paint (juce::Graphics& g)
{
    const auto pill = getLocalBounds().removeFromLeft (pillWidth()).toFloat();
    Dine::fillRounded (g, pill, juce::Colours::white.withAlpha (0.06f), pill.getHeight() * 0.5f);
    Dine::hairlineRounded (g, pill.reduced (0.5f), juce::Colours::white.withAlpha (0.10f), pill.getHeight() * 0.5f - 0.5f);
    if (! showClock) return;
    // The clock turns red while a take is being written.
    g.setColour (recording ? Dine::keyRec : Dine::ink);
    g.setFont (Dine::mono (15.0f, 500));
    Dine::drawText (g, timeText, timeCell, juce::Justification::bottomLeft);
    if (showLength)
    {
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.5f));
        Dine::drawText (g, "of " + lengthText, lengthCell, juce::Justification::topLeft);
    }
}

void TransportBar::resized()
{
    auto r = getLocalBounds();
    auto keys = r.removeFromLeft (pillWidth()).reduced (kPad, 0).withSizeKeepingCentre (keysWidth(), kKeyH);
    keysWell = keys;
    auto place = [&keys] (juce::Component& c)
    {
        c.setBounds (keys.removeFromLeft (kKeyW));
        keys.removeFromLeft (kKeyGap);
    };
    place (*startButton);
    place (*stopButton);
    place (*playButton);
    place (*recordButton);
    place (*loopButton);

    r.removeFromLeft (kClockGap);
    showClock = r.getWidth() >= clockCellWidth();
    showLength = showClock;
    divider = {};
    if (! showClock) { clockWell = timeCell = lengthCell = {}; return; }
    clockWell = r.withWidth (clockCellWidth());
    const int mid = r.getCentreY() + 3;
    timeCell = clockWell.withBottom (mid);
    lengthCell = clockWell.withTop (mid);
}

} // namespace livemix
