#include "TransportBar.h"

namespace livemix
{

// A square transport key: a glyph drawn by hand so the shapes stay crisp at 26 px.
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

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool lit = active || down;
        const juce::Colour tint = glyph == Glyph::Record ? Dine::crit : Dine::accent;

        if (lit) Dine::fillRounded (g, r, tint.withAlpha (glyph == Glyph::Record ? 0.9f : 0.85f), Dine::Radius::control);
        else if (over) Dine::fillRounded (g, r, Dine::fillHover, Dine::Radius::control);
        else Dine::fillRounded (g, r, Dine::fill, Dine::Radius::control);
        Dine::hairlineRounded (g, r, Dine::hair, Dine::Radius::control);

        const juce::Colour ink = lit ? Dine::onAccent : (isEnabled() ? Dine::ink : Dine::ink4);
        auto c = r.getCentre();
        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.36f;
        g.setColour (ink);

        switch (glyph)
        {
            case Glyph::Play:
            {
                juce::Path p;
                p.addTriangle (c.x - s * 0.6f, c.y - s, c.x - s * 0.6f, c.y + s, c.x + s * 0.9f, c.y);
                g.fillPath (p);
                break;
            }
            case Glyph::Stop:
                g.fillRoundedRectangle (juce::Rectangle<float> (s * 1.7f, s * 1.7f).withCentre (c), 1.5f);
                break;
            case Glyph::Record:
                g.fillEllipse (juce::Rectangle<float> (s * 1.9f, s * 1.9f).withCentre (c));
                break;
            case Glyph::Start:
            {
                juce::Path p;
                p.addTriangle (c.x + s * 0.9f, c.y - s, c.x + s * 0.9f, c.y + s, c.x - s * 0.4f, c.y);
                g.fillPath (p);
                g.fillRect (juce::Rectangle<float> (1.6f, s * 2.0f).withCentre ({ c.x - s * 0.8f, c.y }));
                break;
            }
            case Glyph::Loop:
            {
                juce::Path p;
                const auto box = juce::Rectangle<float> (s * 2.1f, s * 1.5f).withCentre (c);
                p.addRoundedRectangle (box, s * 0.7f);
                g.strokePath (p, juce::PathStrokeType (1.5f));
                juce::Path arrow;
                arrow.addTriangle (box.getCentreX() - 2.5f, box.getY() - 3.0f,
                                   box.getCentreX() - 2.5f, box.getY() + 3.0f,
                                   box.getCentreX() + 2.0f, box.getY());
                g.fillPath (arrow);
                break;
            }
        }
    }

private:
    Glyph glyph;
    bool active = false;
};

TransportBar::TransportBar (MixController& c, AppServices& s) : controller (c), services (s)
{
    auto make = [this] (std::unique_ptr<TransportButton>& b, TransportButton::Glyph g, const juce::String& tip)
    {
        b = std::make_unique<TransportButton> (g, tip);
        addAndMakeVisible (*b);
    };
    make (startButton, TransportButton::Glyph::Start, "Go to the start (Return)");
    make (playButton, TransportButton::Glyph::Play, "Play (Space)");
    make (stopButton, TransportButton::Glyph::Stop, "Stop (Space)");
    make (recordButton, TransportButton::Glyph::Record, "Record every armed track (R)");
    make (loopButton, TransportButton::Glyph::Loop, "Loop the marked range (L)");

    startButton->onClick = [this] { returnToStart(); };
    playButton->onClick = [this] { if (! services.daw().getTransport().isPlaying()) togglePlay(); };
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

    playButton->setActive (nowPlaying && ! nowRecording);
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

    if (position != lastPosition || nowPlaying != playing || nowRecording != recording || nowLooping != looping)
    {
        lastPosition = position;
        playing = nowPlaying;
        recording = nowRecording;
        looping = nowLooping;
        timeText = Transport::formatTime (transport.getPositionSeconds());
        repaint();
    }
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (Dine::toolbar);
    g.setColour (Dine::hair);
    g.fillRect (0.0f, 0.0f, float (getWidth()), 0.5f);

    // The clock: the one number an operator glances at during a service.
    auto clock = juce::Rectangle<int> (loopButton->getRight() + 16, 0, 168, getHeight()).reduced (0, 11);
    Dine::fillRounded (g, clock.toFloat(), Dine::well, Dine::Radius::chip);
    g.setColour (recording ? Dine::crit : Dine::ink);
    g.setFont (Dine::mono (16.0f, 500));
    g.drawText (timeText, clock, juce::Justification::centred);

    auto right = getLocalBounds().reduced (16, 0).withTrimmedLeft (clock.getRight());

    // Recording / output state, then the machine's numbers.
    auto label = [&] (juce::Rectangle<int>& r, const juce::String& caption, const juce::String& value, juce::Colour colour)
    {
        const int w = juce::jmax (Dine::textWidth (Dine::text (11.0f, 600), caption),
                                  Dine::textWidth (Dine::mono (12.0f), value)) + 18;
        auto cell = r.removeFromRight (w);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (10.5f, 600));
        g.drawText (caption, cell.withTrimmedBottom (cell.getHeight() / 2), juce::Justification::centredRight);
        g.setColour (colour);
        g.setFont (Dine::mono (12.0f));
        g.drawText (value, cell.withTrimmedTop (cell.getHeight() / 2), juce::Justification::centredRight);
    };

    const bool running = services.isAudioRunning();
    const int drops = services.xrunCount();
    label (right, "DROPS", juce::String (drops), drops > 0 ? Dine::warn : Dine::ink2);
    label (right, "BUFFER", running ? juce::String (services.bufferSize()) : "--", Dine::ink2);
    label (right, "RATE", running ? juce::String (services.sampleRate() / 1000.0, 1) + "k" : "--", Dine::ink2);

    if (recording)
    {
        auto pill = right.removeFromRight (108).withSizeKeepingCentre (100, 22);
        Dine::drawPill (g, pill.toFloat(), "RECORDING", Dine::crit);
    }
    else if (services.daw().getProject().numArmed() > 0)
    {
        auto pill = right.removeFromRight (100).withSizeKeepingCentre (92, 22);
        Dine::drawPill (g, pill.toFloat(), juce::String (services.daw().getProject().numArmed()) + " armed", Dine::warn);
    }
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (16, 0);
    const int size = 28;
    auto place = [&] (juce::Component& c) { c.setBounds (r.removeFromLeft (size).withSizeKeepingCentre (size, size)); r.removeFromLeft (6); };
    place (*startButton);
    place (*playButton);
    place (*stopButton);
    place (*recordButton);
    r.removeFromLeft (8);
    place (*loopButton);
}

} // namespace livemix
