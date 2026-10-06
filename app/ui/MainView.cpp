#include "MainView.h"
#include "native/InputMapStore.h"
#include "native/MultitrackImport.h"
#include "native/OpenAiMixProvider.h"
#include "native/Telemetry.h"

namespace livemix
{

// Nothing opens by itself in the headless snapshot tool (MainView::setAutoTutorial), and it
// renders from the design rather than from this Mac's chosen theme (setStoredThemeUsed).
// Nothing opens by itself in the headless tool, and it renders from the design rather than
// from this Mac: `gGuides` is the workspace guides' own switch, so a snapshot walk is not a
// walk through whatever the person at this desk has already dismissed.
namespace
{
    bool gAutoTutorial = true, gUseStoredTheme = true, gGuides = true;
    juce::File gGuidePrefs;

    // Where the guides remember what has been dismissed: this Mac's preferences, or the file
    // the headless tool hands in so a render does not depend on what anybody dismissed here.
    juce::File guidePreferences()
    {
        return gGuidePrefs == juce::File() ? ThemeStore::preferencesFile() : gGuidePrefs;
    }
}

// ---------------------------------------------------------------- toast
// A sentence at the foot of the workspace, never a banner: nothing in the layout moves. A
// refusal (LIVE SAFE said no, something could not be done) is amber on its own ground.
class MainView::Toast : public juce::Component
{
public:
    void show (const juce::String& t)
    {
        text = t;
        const auto lower = t.toLowerCase();
        refuse = lower.contains ("could not") || lower.contains ("cannot") || lower.contains ("is locked")
              || lower.contains ("blocked") || lower.contains ("nowhere") || lower.contains ("no earlier")
              || lower.contains ("first") || lower.contains ("stopped") || lower.contains ("is not ");
        setVisible (true);
        repaint();
    }
    int idealWidth() const
    {
        return juce::jmin (640, Dine::textWidth (Dine::text (13.0f), text) + 38);
    }
    int idealHeight() const
    {
        const int w = idealWidth() - 36;
        juce::AttributedString s; s.setText (text); s.setFont (Dine::text (13.0f));
        juce::TextLayout l; l.createLayout (s, float (juce::jmax (80, w)));
        return juce::jlimit (44, 120, int (l.getHeight()) + 28);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 40, { 0, 18 }).drawForRectangle (g, getLocalBounds());
        Dine::fillRounded (g, r, refuse ? Dine::refuse : Dine::control, Dine::Radius::card);
        g.setColour (refuse ? Dine::warn : Dine::ink);
        g.setFont (Dine::text (13.0f));
        Dine::drawFittedText (g, text, getLocalBounds().reduced (18, 10), juce::Justification::centredLeft, 4, 1.0f);
    }
private:
    juce::String text;
    bool refuse = false;
};

// ---------------------------------------------------------------- toolbar toggle
// BYPASS, LIVE SAFE ON / OFF and MIX BUDDY: the accent when on, the control plane when
// not, tracked caps either way.
// ---------------------------------------------------------------- toolbar keys
// One class for the four shapes the toolbar's right cluster is made of, because they sit in
// one row and have to line up (design: `Broadcast Key` 61:9119, `Toolbar v3.4` 117:32462):
//
//   Verb     TUNE LIVE MIX - an icon and the word in the accent, on no plane at all.
//   Key      DIM / MUTE / BYPASS / AUTOPILOT - a 4 pt plane, and a lamp that is always visible
//            in its own colour so the key says which key it is before it says it is on. Flat
//            until it is on; on, the whole key fills with that colour and the type goes dark,
//            because these four are the ones that change what the broadcast hears.
//   Primary  LIVE SAFE - the one amber fill in the product, with the lock.
//   Glyph    Mix Buddy - an icon on its own.
class MainView::ToolbarToggle : public juce::Button
{
public:
    enum class Kind { Verb, Key, Primary, Glyph };

    ToolbarToggle (const juce::String& text, Kind k = Kind::Key, Dine::Icon ic = Dine::Icon::None,
                   juce::Colour lampTint = Dine::accent)
        : juce::Button (text), kind (k), icon (ic), tint (lampTint)
    {
        setClickingTogglesState (false);
        setWantsKeyboardFocus (false);
    }

    void setOn (bool o) { if (o != on) { on = o; repaint(); } }
    bool isOn() const noexcept { return on; }
    void setTint (juce::Colour c) { if (c != tint) { tint = c; repaint(); } }

    // A suffix changes the label's width ("TUNE LIVE MIX" grows a "STOP" while a live tune runs),
    // so the row it sits in is laid out again; a repaint alone left the button at its widest.
    void setSuffix (const juce::String& sfx)
    {
        if (sfx == suffix) return;
        const int was = idealWidth();
        suffix = sfx;
        repaint();
        if (idealWidth() != was)
            if (auto* parent = getParentComponent()) parent->resized();
    }
    juce::String label() const { return suffix.isEmpty() ? getButtonText() : getButtonText() + " " + suffix; }

    static juce::Font verbFont() { return Dine::caps (11.5f, 0.02f, 700); }
    static juce::Font keyFont()  { return Dine::caps (10.5f, 0.02f, 700); }

    int idealWidth() const
    {
        switch (kind)
        {
            case Kind::Glyph:   return 34;
            case Kind::Key:     return 16 + Dine::textWidth (keyFont(), label());
            case Kind::Primary: return 8 + 30 + 8 + Dine::textWidth (Dine::caps (11.0f, 0.02f, 700), label()) + 14;
            case Kind::Verb:
            default:            return 32 + Dine::textWidth (verbFont(), label());
        }
    }

    // The v4 toolbar (docs/design/v4), one shape per kind:
    //   Verb     TUNE LIVE MIX - the primary action: a white pill with dark type.
    //   Key      DIM / MUTE / BYPASS / Auto - words inside one shared pill MainView draws; a key
    //            that is on lifts to a filled pill of its own. MUTE's word is always red.
    //   Primary  LIVE SAFE - a pill with a switch in it, amber while the sound is locked.
    //   Glyph    Mix Buddy - a round button with the glyph in it.
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        g.setOpacity (isEnabled() ? 1.0f : Dine::disabled);
        const float radius = r.getHeight() * 0.5f;

        if (kind == Kind::Primary)
        {
            Dine::fillRounded (g, r, juce::Colours::white.withAlpha (over ? 0.10f : 0.06f), radius);
            Dine::hairlineRounded (g, r.reduced (0.5f), juce::Colours::white.withAlpha (0.10f), radius - 0.5f);
            auto inner = r.reduced (8.0f, 0.0f);
            auto track = inner.removeFromLeft (30.0f).withSizeKeepingCentre (30.0f, 18.0f);
            Dine::fillRounded (g, track, on ? Dine::warn : juce::Colours::white.withAlpha (0.18f), 9.0f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ on ? track.getRight() - 9.0f : track.getX() + 9.0f,
                                                                                track.getCentreY() }));
            inner.removeFromLeft (8.0f);
            g.setColour (on ? Dine::warn : Dine::ink);
            g.setFont (Dine::caps (11.0f, 0.02f, 700));
            Dine::drawText (g, label(), inner.toNearestInt(), juce::Justification::centredLeft);
            return;
        }

        if (kind == Kind::Glyph)
        {
            Dine::fillRounded (g, r, juce::Colours::white.withAlpha (on ? 0.16f : over ? 0.10f : 0.06f), radius);
            Dine::hairlineRounded (g, r.reduced (0.5f), juce::Colours::white.withAlpha (0.10f), radius - 0.5f);
            Dine::drawIcon (g, icon, r.withSizeKeepingCentre (16.0f, 16.0f), on || over ? Dine::ink : Dine::glyph);
            return;
        }

        if (kind == Kind::Key)
        {
            const bool mute = tint == Dine::crit;
            if (on)        Dine::fillRounded (g, r, mute ? Dine::crit : juce::Colours::white.withAlpha (down ? 0.80f : 0.92f), radius);
            else if (over) Dine::fillRounded (g, r, juce::Colours::white.withAlpha (0.08f), radius);
            g.setColour (on ? (mute ? juce::Colours::white : Dine::desk)
                            : mute ? juce::Colour (0xffff6961) : over ? Dine::ink : Dine::ink2);
            g.setFont (keyFont());
            Dine::drawText (g, label(), getLocalBounds(), juce::Justification::centred);
            return;
        }

        // Verb: the white pill. While it is running (a live tune), the accent, so "it is
        // listening" reads from across the room.
        const auto fill = on ? Dine::accent : Dine::ink;
        Dine::fillRounded (g, r, down ? fill.darker (0.12f) : over ? fill.brighter (0.05f) : fill, radius);
        g.setColour (on ? Dine::onAccent : Dine::desk);
        g.setFont (verbFont());
        Dine::drawText (g, label(), getLocalBounds(), juce::Justification::centred);
    }

private:
    Kind kind;
    Dine::Icon icon;
    juce::Colour tint;
    juce::String suffix;
    bool on = false;
};

// The sidebar switch (v4): the two-pane glyph on no plane at the sidebar card's top right
// while the sidebar is out, and a round button at the toolbar's left edge while it is hidden.
class MainView::SidebarButton : public juce::Button
{
public:
    SidebarButton() : juce::Button ("Sidebar") { setWantsKeyboardFocus (false); }
    void setOn (bool o) { if (o != on) { on = o; repaint(); } }
    void setRound (float amount) { if (std::abs (amount - round) > 0.001f) { round = amount; repaint(); } }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        const float radius = r.getHeight() * 0.5f;
        if (round > 0.01f)
        {
            Dine::fillRounded (g, r, juce::Colours::white.withAlpha (0.06f * round + (over ? 0.04f : 0.0f)), radius);
            Dine::hairlineRounded (g, r.reduced (0.5f), juce::Colours::white.withAlpha (0.10f * round), radius - 0.5f);
        }
        else if (over || down) Dine::fillRounded (g, r, juce::Colours::white.withAlpha (down ? 0.10f : 0.06f), 8.0f);
        Dine::drawIcon (g, Dine::Icon::Sidebar, r.withSizeKeepingCentre (16.0f, 16.0f), over ? Dine::ink : Dine::glyph);
    }
private:
    bool on = true;
    float round = 0.0f;
};

// ---------------------------------------------------------------- the session button
// The session's name and what it is, at the left of the toolbar (v4): the name in 13 pt with a
// small chevron, and under it the profile and whether there is work the disk does not have
// yet. A press opens the session menu - New, Open, Save, Import, Export, the setup, the tour.
class MainView::SessionButton : public juce::Button
{
public:
    SessionButton() : juce::Button ("Session") { setWantsKeyboardFocus (false); }
    void setText (const juce::String& t, const juce::String& sub)
    {
        if (t == title && sub == subtitle) return;
        title = t; subtitle = sub;
        if (auto* parent = getParentComponent()) parent->resized();
        repaint();
    }
    int idealWidth() const
    {
        return juce::jlimit (90, 180, juce::jmax (Dine::textWidth (Dine::text (13.0f, 600), title) + 16,
                                                  Dine::textWidth (Dine::text (11.0f), subtitle)) + 16);
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        if (over || down) Dine::fillRounded (g, getLocalBounds().toFloat(), juce::Colours::white.withAlpha (down ? 0.10f : 0.06f), 8.0f);
        auto r = getLocalBounds().reduced (8, 0);
        const int mid = getHeight() / 2;
        auto top = r.withBottom (mid + 2);
        auto chevron = top.removeFromRight (12);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawFittedText (g, title, top, juce::Justification::bottomLeft, 1, 0.8f);
        g.setColour (Dine::ink3);
        Dine::drawIcon (g, Dine::Icon::Chevron, chevron.withTrimmedTop (4).toFloat().withSizeKeepingCentre (10.0f, 10.0f), Dine::ink3);
        g.setColour (Dine::ink.withAlpha (0.45f));
        g.setFont (Dine::text (11.0f));
        Dine::drawFittedText (g, subtitle, r.withTop (mid + 2), juce::Justification::topLeft, 1, 0.8f);
    }
private:
    juce::String title, subtitle;
};

// ---------------------------------------------------------------- the readiness pill
// Beside the clock (v4): green-teal "Ready" when nothing needs the engineer, amber with a count
// when something does. A press opens the check that says what.
class MainView::ReadyPill : public juce::Button
{
public:
    ReadyPill() : juce::Button ("Ready") { setWantsKeyboardFocus (false); }
    void setCount (int n, bool known)
    {
        if (n == count && known == isKnown) return;
        count = n; isKnown = known;
        if (auto* parent = getParentComponent()) parent->resized();
        repaint();
    }
    int idealWidth() const { return 10 + 7 + 6 + Dine::textWidth (font(), text()) + 10; }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto tint = count > 0 ? Dine::warn : isKnown ? Dine::accent : Dine::ink3;
        auto r = getLocalBounds().toFloat();
        Dine::fillRounded (g, r, tint.withAlpha (over ? 0.24f : 0.16f), r.getHeight() * 0.5f);
        auto inner = getLocalBounds().reduced (10, 0);
        g.setColour (tint);
        g.fillEllipse (inner.removeFromLeft (7).withSizeKeepingCentre (7, 7).toFloat());
        inner.removeFromLeft (6);
        g.setColour (count > 0 ? juce::Colour (0xffffb340) : tint);
        g.setFont (font());
        Dine::drawText (g, text(), inner, juce::Justification::centredLeft);
    }
private:
    static juce::Font font() { return Dine::text (11.5f, 600); }
    juce::String text() const { return count > 0 ? juce::String (count) : juce::String ("Ready"); }
    int count = 0;
    bool isKnown = false;
};

// ---------------------------------------------------------------- the output pill
// Where the broadcast goes and where solo goes, in one pill at the right of the toolbar (v4):
// a lamp, the broadcast's name, "· solo" and the solo device, and a chevron.
class MainView::OutputPill : public juce::Button
{
public:
    OutputPill() : juce::Button ("Outputs") { setWantsKeyboardFocus (false); }
    void setText (const juce::String& main, const juce::String& solo, bool live)
    {
        if (main == mainText && solo == soloText && live == running) return;
        mainText = main; soloText = solo; running = live;
        if (auto* parent = getParentComponent()) parent->resized();
        repaint();
    }
    int idealWidth() const
    {
        return 12 + 7 + 6 + Dine::textWidth (Dine::text (12.0f), mainText)
             + (soloText.isEmpty() ? 0 : 5 + Dine::textWidth (Dine::text (11.0f), soloText)) + 10 + 10 + 10;
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        Dine::fillRounded (g, r, juce::Colours::white.withAlpha (over ? 0.10f : 0.06f), r.getHeight() * 0.5f);
        Dine::hairlineRounded (g, r.reduced (0.5f), juce::Colours::white.withAlpha (0.10f), r.getHeight() * 0.5f - 0.5f);
        auto inner = getLocalBounds().reduced (12, 0);
        g.setColour (running ? Dine::ok : Dine::ink4);
        g.fillEllipse (inner.removeFromLeft (7).withSizeKeepingCentre (7, 7).toFloat());
        inner.removeFromLeft (6);
        Dine::drawIcon (g, Dine::Icon::Chevron, inner.removeFromRight (10).toFloat().withSizeKeepingCentre (10.0f, 10.0f), Dine::ink3);
        inner.removeFromRight (8);
        // The device's name when it fits, squeezed a little if it must; "Outputs" when it does
        // not, rather than half a name - the menu it opens names every device in full.
        const auto mainFont = Dine::text (12.0f);
        const bool fits = float (Dine::textWidth (mainFont, mainText)) * 0.85f <= float (inner.getWidth());
        const auto shown = fits ? mainText : juce::String ("Outputs");
        const int mainW = juce::jmin (inner.getWidth(), Dine::textWidth (mainFont, shown));
        g.setColour (Dine::ink);
        g.setFont (mainFont);
        Dine::drawFittedText (g, shown, inner.removeFromLeft (mainW), juce::Justification::centredLeft, 1, 0.85f);
        // "· solo ..." only when it fits: the menu says it in full.
        if (soloText.isNotEmpty() && inner.getWidth() - 5 >= Dine::textWidth (Dine::text (11.0f), soloText))
        {
            inner.removeFromLeft (5);
            g.setColour (Dine::ink.withAlpha (0.5f));
            g.setFont (Dine::text (11.0f));
            Dine::drawText (g, soloText, inner, juce::Justification::centredLeft);
        }
    }
private:
    juce::String mainText, soloText;
    bool running = false;
};

// ---------------------------------------------------------------- the solo pill
// SOLO YOU CANNOT MISS.
//
// Solo is the one state that changes what the engineer hears and nothing at all about what the
// room and the stream hear - which is exactly what makes it the state most easily left on by
// accident. An S pressed on MIXER at ten past ten is invisible from TRACKS, from TUNE and from
// LIVE, and the first anyone knows about it is a service mixed through one microphone's worth
// of headphones.
//
// So while anything is soloed - a channel, a group, an effects return - this pill sits in the
// toolbar beside the clock, on every workspace, and names what. The name is a way to the thing
// it names; the cross clears every solo in one press; and it takes no room at all when nothing
// is soloed.
class MainView::SoloPill : public juce::SettableTooltipClient, public juce::Component
{
public:
    SoloPill() { setInterceptsMouseClicks (true, false); setWantsKeyboardFocus (false); }

    std::function<void()> onClear;
    std::function<void (const MixController::SoloedItem&)> onJump;

    // What is soloed now, in the order the window found it. Compared before a repaint.
    void setItems (std::vector<MixController::SoloedItem> next)
    {
        if (next.size() == items.size())
        {
            bool same = true;
            for (size_t i = 0; i < next.size(); ++i)
                if (next[i].kind != items[i].kind || next[i].index != items[i].index) { same = false; break; }
            if (same) return;
        }
        items = std::move (next);
        juce::StringArray each;
        for (const auto& it : items) each.add (juce::String (juce::CharPointer_UTF8 (it.name.c_str())));
        names = each.joinIntoString (", ");
        if (auto* parent = getParentComponent()) parent->resized();
        repaint();
    }

    bool hasAny() const noexcept { return ! items.empty(); }

    int idealWidth() const
    {
        return 10 + 7 + 6 + Dine::textWidth (ToolbarToggle::keyFont(), "SOLO") + 8
             + juce::jmin (220, Dine::textWidth (Dine::text (12.0f, 500), names)) + 10 + 18 + 10;
    }

    void paint (juce::Graphics& g) override
    {
        // v4: the solo colour, so a solo left down reads as one from anywhere in the room.
        auto r = getLocalBounds().toFloat();
        Dine::fillRounded (g, r, Dine::keySolo.withAlpha (0.16f), r.getHeight() * 0.5f);
        auto inner = r.reduced (10.0f, 0.0f);
        g.setColour (Dine::keySolo);
        g.fillEllipse (inner.removeFromLeft (7.0f).withSizeKeepingCentre (7.0f, 7.0f));
        inner.removeFromLeft (6.0f);
        g.setFont (ToolbarToggle::keyFont());
        const int verbW = Dine::textWidth (ToolbarToggle::keyFont(), "SOLO");
        Dine::drawText (g, "SOLO", inner.removeFromLeft (float (verbW)).toNearestInt(), juce::Justification::centredLeft);
        inner.removeFromLeft (8.0f);
        auto cross = inner.removeFromRight (18.0f).withSizeKeepingCentre (18.0f, 18.0f);
        inner.removeFromRight (8.0f);
        g.setColour (Dine::ink);
        const auto font = Dine::text (12.0f, 500);
        g.setFont (font);
        // The toolbar gives the pill what it can spare, which on a narrow window is not every
        // name. "BGV 2, DR..." says less than "3 soloed" does, so past that it counts instead.
        auto label = names;
        if (items.size() > 1 && Dine::textWidth (font, label) > inner.getWidth())
            label = juce::String (int (items.size())) + " soloed";
        // Too narrow even for that: SOLO and the cross say enough, and the tooltip names them.
        if (Dine::textWidth (font, label) <= int (inner.getWidth()))
            Dine::drawText (g, label, inner.toNearestInt(), juce::Justification::centredLeft, true);
        g.setColour (crossOver ? Dine::keySolo.brighter (0.2f) : Dine::keySolo);
        g.fillEllipse (cross);
        Dine::drawIcon (g, Dine::Icon::Close, cross.reduced (4.5f), Dine::desk);
        crossBox = cross.toNearestInt();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool over = crossBox.contains (e.getPosition());
        if (over != crossOver) { crossOver = over; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { if (crossOver) { crossOver = false; repaint(); } }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition())) return;
        if (crossBox.contains (e.getPosition())) { if (onClear) onClear(); return; }
        if (! items.empty() && onJump) onJump (items.front());
    }

private:
    std::vector<MixController::SoloedItem> items;
    juce::String names;
    juce::Rectangle<int> crossBox;
    bool crossOver = false;
};

// ---------------------------------------------------------------- status foot
// Always on screen: the engine, the CPU, the disk, what is recording, the broadcast, what
// the engineer is listening on, and dropped buffers. Painted, compared before a repaint.
class MainView::StatusBar : public juce::Component
{
public:
    StatusBar (MixController& c, AppServices& s) : controller (c), services (s) { setOpaque (false); }

    bool takeStopped = false;           // the last take was stopped by DINE, not by a person (TransportBar)
    // EXPORT, from MainView's tick: the words, their colour, how far (< 0 = no number for this
    // stage), and whether it is still working. Empty words = no export to speak of.
    juce::String exportText;
    juce::Colour exportTint;
    float exportFraction = -1.0f;
    bool exportWorking = false;
    std::function<void()> onExportClicked;

    void update (bool slow)
    {
        Look next;
        const bool running = services.isAudioRunning();
        const bool lost = ! running && services.deviceStopped();
        next.engine = lost ? "Device lost" : ! running ? "Not running" : controller.isBypassed() ? "Bypassed" : "running";
        next.engineTint = lost ? Dine::crit : ! running ? Dine::ink3 : controller.isBypassed() ? Dine::warn : Dine::ink;
        next.engineLabel = "Engine";
        // THE FIRST CELL IS THE ONE THAT IS ALWAYS THERE. A narrow window drops the cells from
        // the right and the toolbar's lamps with them, so whatever is most urgent about what is
        // going out is said here: a muted or dimmed broadcast, a take that stopped by itself,
        // then Autopilot at work.
        if (! lost && running)
        {
            if (controller.isBroadcastMuted())      { next.engineLabel = "On air"; next.engine = "muted";  next.engineTint = Dine::crit; }
            else if (takeStopped && ! services.daw().isRecording())
                                                    { next.engineLabel = "Recording"; next.engine = "stopped by itself"; next.engineTint = Dine::crit; }
            else if (controller.isBroadcastDimmed()) { next.engineLabel = "On air"; next.engine = "dimmed"; next.engineTint = Dine::warn; }
            else if (! controller.isBypassed() && controller.isAutopilotOn()) next.engine = "running, Autopilot on";
        }
        const double cpu = services.cpuLoad();
        next.cpu = cpu < 0.0 ? juce::String (Glyph::dash()) : juce::String (juce::roundToInt (cpu * 100.0)) + "%";
        next.cpuTint = cpu > 0.8 ? Dine::warn : Dine::ink;
        next.drops = services.xrunCount();

        const auto& daw = services.daw();
        const auto& project = daw.getProject();
        int armed = 0;
        for (const auto& t : project.tracks) if (t.armed) ++armed;
        next.recording = daw.isRecording();
        next.rec = next.recording ? juce::String (armed) + " tracks" : juce::String ("stopped");
        {
            const auto& session = controller.getSession();
            next.counts = session.inputs.empty() ? juce::String()
                        : juce::String (int (session.inputs.size())) + " inputs " + Glyph::dot() + " "
                              + juce::String (armed) + " to record";
        }
        next.recTint = next.recording ? Dine::crit : Dine::ink3;

        if (slow || diskText.isEmpty())
        {
            const double seconds = daw.getRecordingSecondsFree();
            if (seconds <= 0.0) diskText = Glyph::dash();
            else
            {
                const int total = int (seconds);
                diskText = total >= 24 * 3600 ? "a day+" : total >= 3600 ? juce::String (total / 3600) + " h " + juce::String ((total / 60) % 60) + " m"
                                                                         : juce::String (juce::jmax (0, total / 60)) + " m";
            }
            diskLow = seconds > 0.0 && seconds < 15.0 * 60.0;
        }
        // An autosave that stopped landing outranks how much room is left: it is the one thing
        // about the disk that is already losing work.
        const bool notSaving = services.autosaveFailing();
        next.disk = notSaving ? juce::String ("not saving") : diskText;
        next.diskTint = notSaving ? Dine::crit : diskLow ? Dine::warn : Dine::ink;

        const auto loud = controller.getMasterLoudness();
        next.loudness = ! loud.known || loud.integratedLufs <= -100.0f ? juce::String (Glyph::dash()) + " LUFS"
                                                                       : juce::String (loud.integratedLufs, 1) + " LUFS";
        next.loudTint = ! loud.known || loud.integratedLufs <= -100.0f ? Dine::ink3 : loud.onTarget() ? Dine::ink : Dine::warn;

        const auto& mon = controller.getMonitor();
        next.monitor = juce::String (mon.mode == SoloMode::InPlace ? "in place" : "solo") + " " + Glyph::dot() + " "
                     + (mon.point == SoloPoint::PFL ? "PFL" : "AFL");
        next.monitorTint = controller.hasMonitorOutput() ? Dine::ink : Dine::ink3;
        next.safe = project.liveSafe;
        next.tempo = juce::String (juce::roundToInt (project.tempo)) + " BPM";
        if (slow || autosaveText.isEmpty())
        {
            const auto when = services.lastAutosave();
            autosaveText = when == juce::Time() ? juce::String() : "Autosaved " + when.formatted ("%H:%M");
        }
        next.autosaved = notSaving ? juce::String ("Autosave failing") : autosaveText;

        next.exportText = exportText;
        next.exportTint = exportTint;
        next.exportWorking = exportWorking;
        // The line along the seam: a bar for a stage with a number, a short sweep for one
        // without. Only the sweep moves on its own, and only while an export is working.
        next.exportPermille = exportWorking && exportFraction >= 0.0f ? juce::roundToInt (exportFraction * 1000.0f) : -1;
        if (exportWorking && exportFraction < 0.0f) next.sweep = (look.sweep + 1) % 90;

        if (next != look) { look = next; repaint(); }
    }

    // v4: one quiet line on the window's own ground - no band, no seam - of plain sentences:
    // "Engine running", "CPU 14%", "Disk 9 h 40 m", "Not recording", "On air -23.1 LUFS",
    // "0 dropped", "Live safe off", "72 BPM", "Autosaved 9:58"; the counts at the right. A
    // word that needs the engineer takes its colour; everything else stays at half ink.
    void paint (juce::Graphics& g) override
    {
        if (look.exportWorking)
        {
            // AN EXPORT IS WORKING: a line in the accent along the top of the foot, across the
            // whole window - visible from every workspace and under every sheet's edge.
            const auto seam = getLocalBounds().removeFromTop (2).toFloat();
            g.setColour (Dine::accent);
            if (look.exportPermille >= 0)
                g.fillRect (seam.withWidth (seam.getWidth() * float (look.exportPermille) / 1000.0f));
            else
            {
                const float w = seam.getWidth() * 0.18f;
                const float x = (seam.getWidth() + w) * float (look.sweep) / 90.0f - w;
                g.fillRect (seam.withX (x).withWidth (w).getIntersection (seam));
            }
        }

        auto r = getLocalBounds().reduced (10, 0).withTrimmedRight (8);
        if (look.counts.isNotEmpty())
        {
            const int w = Dine::textWidth (font(), look.counts);
            g.setColour (quiet());
            g.setFont (font());
            Dine::drawText (g, look.counts, r.removeFromRight (w), juce::Justification::centredRight);
            r.removeFromRight (20);
        }

        // The engine's lamp, then its sentence: always first, because a narrow window drops
        // cells from the right and this is the one that has to stay.
        {
            g.setColour (look.engineTint == Dine::ink ? Dine::ok : look.engineTint);
            g.fillEllipse (r.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
            r.removeFromLeft (7);
        }
        cell (g, r, look.engineLabel + " " + look.engine, look.engineTint);
        // Second, so a narrow window keeps it: it is the one long thing DINE does by itself.
        exportBox = {};
        if (look.exportText.isNotEmpty())
        {
            const int before = r.getX();
            cell (g, r, "Export " + look.exportText, look.exportTint);
            if (r.getX() != before) exportBox = { before, 0, r.getX() - before, getHeight() };
        }
        cell (g, r, "CPU " + look.cpu, look.cpuTint);
        cell (g, r, "Disk " + look.disk, look.diskTint);
        cell (g, r, look.recording ? "Recording " + look.rec : juce::String ("Not recording"), look.recording ? Dine::crit : Dine::ink);
        cell (g, r, "On air " + look.loudness, look.loudTint);
        cell (g, r, "Monitor " + look.monitor, look.monitorTint);
        cell (g, r, juce::String (look.drops) + " dropped", look.drops > 0 ? Dine::warn : Dine::ink);
        cell (g, r, look.safe ? "Live safe on" : "Live safe off", look.safe ? Dine::warn : Dine::ink);
        cell (g, r, look.tempo, Dine::ink);
        if (look.autosaved.isNotEmpty()) cell (g, r, look.autosaved, look.autosaved.startsWith ("Autosave failing") ? Dine::crit : Dine::ink);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (exportBox.contains (e.getPosition()) && onExportClicked) onExportClicked();
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (exportBox.contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                             : juce::MouseCursor::NormalCursor);
    }

private:
    juce::Rectangle<int> exportBox;

    static juce::Font font() { return Dine::text (11.0f); }
    static juce::Colour quiet() { return Dine::ink.withAlpha (0.5f); }

    // One sentence per cell, at half ink unless it is saying something that needs the engineer.
    static void cell (juce::Graphics& g, juce::Rectangle<int>& r, const juce::String& text, juce::Colour ink)
    {
        const int w = Dine::textWidth (font(), text);
        if (w + 14 > r.getWidth()) { r.setWidth (0); return; }
        auto area = r.removeFromLeft (w);
        r.removeFromLeft (14);
        g.setColour (ink == Dine::ink || ink == Dine::ink3 ? quiet() : ink);
        g.setFont (font());
        Dine::drawText (g, text, area, juce::Justification::centredLeft);
    }

    struct Look
    {
        juce::String engineLabel, engine, cpu, disk, rec, loudness, monitor, counts, exportText, tempo, autosaved;
        juce::Colour engineTint, cpuTint, diskTint, recTint, loudTint, monitorTint, exportTint;
        int drops = 0, exportPermille = -1, sweep = 0;
        bool recording = false, safe = false, exportWorking = false;
        bool operator== (const Look& o) const
        {
            return exportText == o.exportText && exportTint == o.exportTint && exportPermille == o.exportPermille
                && sweep == o.sweep && exportWorking == o.exportWorking
                && engineLabel == o.engineLabel && engine == o.engine && cpu == o.cpu && disk == o.disk && rec == o.rec && loudness == o.loudness
                && monitor == o.monitor && counts == o.counts && tempo == o.tempo && autosaved == o.autosaved && engineTint == o.engineTint && cpuTint == o.cpuTint && diskTint == o.diskTint
                && recTint == o.recTint && loudTint == o.loudTint && monitorTint == o.monitorTint
                && drops == o.drops && recording == o.recording && safe == o.safe;
        }
        bool operator!= (const Look& o) const { return ! (*this == o); }
    };

    MixController& controller;
    AppServices& services;
    Look look;
    juce::String diskText, autosaveText;
    bool diskLow = false;
};

// The sidebar (v4, docs/design/v4): a card that floats 8 pt in from the window's edges, 214
// wide, with the window's own buttons at its top and the audio device along its foot. Four
// sections in the order of a service - Set up, Mix, Perform, Record - in sentence case.
//
// Hidden, it is not a rail: it goes completely, the way Mail's does (Ctrl-Cmd-S or the
// sidebar button). The card's width runs to nothing while its contents, which keep their
// width so nothing reflows, slide 24 pt left and fade (`setReveal`).
class MainView::Sidebar : public juce::Component
{
public:
    // What a row does: go to a page, or ask the window for something (a sheet).
    enum class Action { None = 0, MixHistory, Scenes, BroadcastReadiness, CheckInputs, Export };

    // `child` is a row that belongs to the one above it: ROUTING's sections, stepped in and
    // without an icon, so the device, the patch and the feeds are one press away.
    struct Def { const char* label; Page page; Dine::Icon icon; Action action; const char* shortcut; bool child; };

    static constexpr int kCardW = 214;

    static const Def* rowDefs() noexcept
    {
        static const Def defs[kRows] = {
            { "Sessions",          Page::Sessions,   Dine::Icon::NavSessions,  Action::None,        "",        false },
            { "Purpose and sound", Page::Purpose,    Dine::Icon::NavPurpose,   Action::None,        "",        false },
            { "Routing",           Page::Routing,    Dine::Icon::NavRouting,   Action::None,        "",        false },
            { "Audio device",      Page::Device,     Dine::Icon::None,         Action::None,        "",        true  },
            { "Inputs",            Page::Assign,     Dine::Icon::None,         Action::None,        "",        true  },
            { "Outputs",           Page::Outputs,    Dine::Icon::None,         Action::None,        "",        true  },
            { "Check inputs",      Page::Mixer,      Dine::Icon::NavCheck,     Action::CheckInputs, "",        false },
            { "Mixer",             Page::Mixer,      Dine::Icon::NavMixer,     Action::None,        "⌘2", false },
            { "Tune",              Page::Tune,       Dine::Icon::NavTune,      Action::None,        "⌘3", false },
            { "Inspector",         Page::Inspector,  Dine::Icon::NavInspector, Action::None,        "⌘5", false },
            { "Favourite mixes",   Page::Favourites, Dine::Icon::NavFavourite, Action::None,        "",        false },
            { "Mix history",       Page::Tracks,     Dine::Icon::NavHistory,   Action::MixHistory,  "",        false },
            { "Live",              Page::Live,       Dine::Icon::NavLive,      Action::None,        "⌘4", false },
            { "Setlist",           Page::Live,       Dine::Icon::NavSetlist,   Action::Scenes,      "",        false },
            { "Tracks",            Page::Tracks,     Dine::Icon::NavTracks,    Action::None,        "⌘1", false },
            { "Export",            Page::Tracks,     Dine::Icon::NavExport,    Action::Export,      "⇧⌘E", false },
        };
        return defs;
    }

    Sidebar (AppServices& s, std::function<void (Page)> go, std::function<void (Action)> act,
             std::function<void (Page)> openWindow = {}) : services (s)
    {
        const auto* defs = rowDefs();
        for (int i = 0; i < kRows; ++i)
        {
            const auto& d = defs[i];
            items[size_t (i)] = std::make_unique<DineNavItem> (d.label, d.icon);
            items[size_t (i)]->setSidebarLook (d.child);
            items[size_t (i)]->onClick = [go, act, d] { if (d.action != Action::None) act (d.action); else go (d.page); };
            if (juce::String (d.shortcut).isNotEmpty())
                items[size_t (i)]->setTooltip (juce::String (d.label) + "  " + juce::String (juce::CharPointer_UTF8 (d.shortcut)));
            // The workspaces that work in a window of their own say so on a right-click.
            if (openWindow && d.action == Action::None
                && (d.page == Page::Mixer || d.page == Page::Live || d.page == Page::Inspector))
            {
                auto* item = items[size_t (i)].get();
                const Page page = d.page;
                item->onSecondaryClick = [item, page, go, openWindow]
                {
                    juce::PopupMenu m;
                    m.addItem (1, "Open in a New Window");
                    m.addItem (2, "Show here");
                    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (item),
                                     [page, go, openWindow] (int r) { if (r == 1) openWindow (page); else if (r == 2) go (page); });
                };
            }
            addAndMakeVisible (*items[size_t (i)]);
        }
        item (Page::Routing).setTooltip ("The device, the inputs and the output feeds. Under LIVE SAFE nothing there can be "
                                         "changed until you say you mean it.");
        item (Page::Device).setTooltip ("The interface DINE is running on, its rate and its buffer.");
        item (Page::Assign).setTooltip ("The patch: what is plugged into every input, and what each one is.");
        item (Page::Outputs).setTooltip ("The output feeds: where the mix, or one group, is sent as well as the main pair.");
        if (auto* check = actionItem (Action::CheckInputs))
            check->setTooltip ("Every input, its level and one word about it: the soundcheck at a glance.");
        if (auto* setlist = actionItem (Action::Scenes))
            setlist->setTooltip ("The service's scenes, in order, on LIVE.");
        setOpaque (false);
    }

    DineNavItem& item (Page p) { return *items[size_t (indexOf (p))]; }
    DineNavItem* actionItem (Action a)
    {
        for (int i = 0; i < kRows; ++i) if (rowDefs()[i].action == a) return items[size_t (i)].get();
        return nullptr;
    }
    void setSelected (Page p)
    {
        const int sel = indexOf (p);
        for (int i = 0; i < kRows; ++i)
            items[size_t (i)]->setSelected (i == sel && rowDefs()[i].action == Action::None);
    }

    // How far the card is out: 1 shown, 0 gone. The rows keep the card's full width and slide
    // 24 pt left as it closes, so nothing in them is ever laid out at a width it cannot read at.
    void setReveal (float r)
    {
        reveal = juce::jlimit (0.0f, 1.0f, r);
        setAlpha (juce::jlimit (0.0f, 1.0f, reveal * 1.6f - 0.6f) * 0.999f + 0.001f);
        resized();
    }

    // Broadcast readiness is only for Church Broadcast / Livestream purposes. It has no row in
    // v4 - the toolbar's readiness pill opens it - so this is kept for the callers only.
    void setBroadcastReadinessVisible (bool) {}

    // The device along the foot. Compared before a repaint.
    void refresh (bool recording)
    {
        const bool running = services.isAudioRunning();
        juce::String state = recording ? "Recording" : running ? "Running" : "Not running";
        // The input device, or the output one when the engine is running on outputs alone.
        juce::String name = ! running ? juce::String ("No audio device")
                          : services.currentInputDevice().isNotEmpty() ? services.currentInputDevice()
                          : services.currentOutputDevice().isNotEmpty() ? services.currentOutputDevice() : juce::String ("No audio device");
        if (running && services.numInputChannels() > 0)
            name += " " + Glyph::dot() + " " + juce::String (services.numInputChannels()) + " in";
        const int xruns = services.xrunCount();
        juce::String spec = running ? juce::String (services.sampleRate() / 1000.0, 0) + " kHz " + Glyph::dot() + " "
                                          + juce::String (services.bufferSize()) + " smp " + Glyph::dot() + " "
                                          + juce::String (xruns) + " dropped"
                                    : juce::String ("Nothing is open");
        if (state == footState && name == footName && spec == footSpec && xruns == footXruns && recording == footRecording) return;
        footState = state; footName = name; footSpec = spec; footXruns = xruns; footRecording = recording;
        repaint (getLocalBounds().removeFromBottom (kFootH));
    }

    void paint (juce::Graphics& g) override
    {
        if (getWidth() < 2) return;
        const auto card = getLocalBounds().toFloat();
        Dine::fillRounded (g, card, Dine::sidebar, 16.0f);
        Dine::hairlineRounded (g, card.reduced (0.5f), juce::Colours::white.withAlpha (0.09f), 15.5f);

        const int dx = slide();
        for (const auto& c : captions)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f, 600));
            Dine::drawText (g, c.second, c.first.translated (dx, 0), juce::Justification::centredLeft);
        }

        // the device, under a seam that stops short of both edges
        auto foot = juce::Rectangle<int> (dx, getHeight() - kFootH, kCardW, kFootH);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (foot.removeFromTop (1).reduced (14, 0));
        foot = foot.reduced (15, 0).withTrimmedTop (12);
        auto first = foot.removeFromTop (16);
        g.setColour (footRecording ? Dine::crit : services.isAudioRunning() ? Dine::ok : Dine::ink4);
        g.fillEllipse (first.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        first.removeFromLeft (8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.5f, 600));
        Dine::drawText (g, footState, first, juce::Justification::centredLeft, true);
        foot.removeFromTop (3);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (11.5f));
        Dine::drawFittedText (g, footName, foot.removeFromTop (14), juce::Justification::centredLeft, 1, 0.85f);
        foot.removeFromTop (3);
        g.setColour (footXruns > 0 ? Dine::warn : Dine::ink3);
        g.setFont (Dine::mono (10.5f));
        Dine::drawFittedText (g, footSpec, foot.removeFromTop (14), juce::Justification::centredLeft, 1, 0.8f);
    }

    void resized() override
    {
        captions.clear();
        const int dx = slide();
        // The mockup's rhythm: a caption block 31 tall, rows 30 tall on a 32 pitch, a section's
        // children 26 tall on a 28 pitch, and the window's own buttons above the first caption.
        // A short window tightens the captions before the rows reach the device along the foot.
        const bool compact = getHeight() < 47 + 4 * 33 + 13 * 32 + 3 * 28 + kFootH + 8;
        const int captionH = compact ? 24 : 31;
        int y = compact ? 42 : 47;
        auto caption = [&] (const char* text)
        {
            captions.push_back ({ juce::Rectangle<int> (19, y, kCardW - 38, captionH), text });
            y += captionH + 2;
        };
        auto rows = [&] (int from, int to)
        {
            for (int i = from; i < to; ++i)
            {
                const bool child = rowDefs()[i].child;
                const int h = child ? 26 : 30;
                items[size_t (i)]->setBounds (dx + 11, y, kCardW - 22, h);
                y += h + 2;
            }
        };
        caption ("Set up");   rows (0, 7);
        caption ("Mix");      rows (7, 12);
        caption ("Perform");  rows (12, 14);
        caption ("Record");   rows (14, kRows);
    }

private:
    int slide() const noexcept { return - juce::roundToInt (24.0f * (1.0f - reveal)); }

    static constexpr int kRows = 16;
    static constexpr int kFootH = 82;
    static int indexOf (Page p) noexcept
    {
        switch (p)
        {
            case Page::Sessions:   return 0;
            case Page::Purpose:    return 1;
            // the saved patches light ROUTING, because they have no row of their own
            case Page::Routing: case Page::Maps: return 2;
            case Page::Device:     return 3;
            case Page::Assign:     return 4;
            case Page::Outputs:    return 5;
            case Page::Mixer:      return 7;
            case Page::Tune:       return 8;
            case Page::Inspector:  return 9;
            case Page::Favourites: return 10;
            case Page::Live:       return 12;
            case Page::Tracks:     return 14;
        }
        return 0;
    }

    AppServices& services;
    std::array<std::unique_ptr<DineNavItem>, size_t (kRows)> items;
    float reveal = 1.0f;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;
    juce::String footState, footName, footSpec;
    int footXruns = 0;
    bool footRecording = false;
};

// ---------------------------------------------------------------- mixer window
class MainView::MixerWindow : public juce::DocumentWindow, private juce::Timer
{
public:
    MixerWindow (MixController& c, AppServices& s, MainView& owner)
        : juce::DocumentWindow ("Mixer " + juce::String (Glyph::dash()) + " DINE", Dine::window,
                                juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton
                                    | juce::DocumentWindow::maximiseButton),
          view (owner)
    {
        page = std::make_unique<MixerPage> (c, s);
        page->setWindowButtonVisible (false);
        page->onOpenStrip = [&owner] (int strip) { owner.inspectStrip (strip); };
        page->onOpenBus = [&owner] (MixBus bus) { owner.inspectBus (bus); };
        page->onTuneStrip = [&owner] (int strip) { owner.toFront (true); owner.tuneChannel (strip); };
        page->onToast = [&owner] (const juce::String& t) { owner.showToast (t); };
        setUsingNativeTitleBar (true);
        setContentNonOwned (page.get(), false);
        setResizable (true, false);
        setResizeLimits (720, 420, 6000, 3000);
        centreWithSize (1180, 700);
        setVisible (true);
        startTimerHz (30);
    }

    ~MixerWindow() override { stopTimer(); clearContentComponent(); }

    MixerPage& getPage() { return *page; }
    void closeButtonPressed() override { view.closeMixerWindow(); }

private:
    void timerCallback() override { page->refresh(); }

    MainView& view;
    std::unique_ptr<MixerPage> page;
};

// A page in a window of its own (LIVE, the Inspector): the page, the 30 Hz refresh the main
// window would give it, and what to do when the session under it changes.
class MainView::PageWindow : public juce::DocumentWindow, private juce::Timer
{
public:
    PageWindow (const juce::String& title, std::unique_ptr<juce::Component> content,
                std::function<void()> refreshFn, std::function<void()> rebuildFn, std::function<void()> closeFn,
                int minW, int minH, int w, int h)
        : juce::DocumentWindow (title + " " + juce::String (Glyph::dash()) + " DINE", Dine::window,
                                juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton
                                    | juce::DocumentWindow::maximiseButton),
          page (std::move (content)), refreshPage (std::move (refreshFn)), rebuildPage (std::move (rebuildFn)), onClose (std::move (closeFn))
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (page.get(), false);
        setResizable (true, false);
        setResizeLimits (minW, minH, 6000, 3000);
        centreWithSize (w, h);
        setVisible (true);
        startTimerHz (30);
    }
    ~PageWindow() override { stopTimer(); clearContentComponent(); }

    void rebuild() { if (rebuildPage) rebuildPage(); }
    juce::Component& getPage() { return *page; }
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    void timerCallback() override { if (refreshPage) refreshPage(); }

    std::unique_ptr<juce::Component> page;
    std::function<void()> refreshPage, rebuildPage, onClose;
};

// ---------------------------------------------------------------- menu bar
// File / Edit / Track / Mix / Transport / View / Help. The shortcuts printed here are the
// ones MainView::keyPressed acts on, so the menu and the keyboard never disagree.
class MainView::Menu : public juce::MenuBarModel
{
public:
    explicit Menu (MainView& v) : view (v) {}

    juce::StringArray getMenuBarNames() override
    {
        return { "File", "Edit", "Track", "Mix", "Transport", "View", "Help" };
    }

    juce::PopupMenu getMenuForIndex (int index, const juce::String&) override
    {
        juce::PopupMenu m;
        switch (index)
        {
            case 0:
                m.addItem (100, "New Session");
                m.addItem (101, "Open Session...");
                m.addSeparator();
                m.addItem (102, "Save", true, false, nullptr);
                m.addItem (103, "Save As...");
                m.addSeparator();
                m.addItem (104, "Import Audio Files...");
                m.addItem (107, "Add a Reference Mix...");
                m.addSeparator();
                m.addItem (108, "Save Input Mapping" + juce::String (Glyph::ellip()),
                           ! view.controller.getSession().inputs.empty());
                m.addItem (109, "Input Mappings" + juce::String (Glyph::ellip()));
                m.addSeparator();
                m.addItem (105, "Export Stereo Mix (WAV)...");
                m.addItem (106, "Export Stereo Mix (MP3)...");
                m.addItem (110, "Export Multitrack (one file per input)...");
                break;
            case 1:
                {
                    const auto undoing = view.undoTarget();
                    const auto redoing = view.redoTarget();
                    m.addItem (200, undoing.label.isNotEmpty() ? "Undo " + undoing.label : juce::String ("Undo"),
                               undoing.domain != UndoDomain::None);
                    m.addItem (204, redoing.label.isNotEmpty() ? "Redo " + redoing.label : juce::String ("Redo"),
                               redoing.domain != UndoDomain::None);
                }
                m.addSeparator();
                m.addItem (201, "Split at Playhead");
                m.addItem (202, "Delete Clip");
                m.addSeparator();
                m.addItem (203, "Add Marker at Playhead   M");
                break;
            case 2:
            {
                // A track with nothing on it yet, of the source picked here (ids from 3000).
                juce::PopupMenu fresh;
                std::vector<ChannelRole> roles;
                TracksPage::fillNewTrackMenu (fresh, 3000, roles);
                m.addSubMenu ("New Track", fresh, ! view.controller.isLiveSafe());
                m.addSeparator();
            }
                m.addItem (300, "Set Every Track to Record");
                m.addItem (301, "Set No Tracks to Record");
                m.addSeparator();
                m.addItem (305, "Move Track Up", view.tracksPage != nullptr && view.tracksPage->canMoveSelectedTrack (-1));
                m.addItem (306, "Move Track Down", view.tracksPage != nullptr && view.tracksPage->canMoveSelectedTrack (1));
                m.addSeparator();
                m.addItem (302, "Monitoring: Input");
                m.addItem (303, "Monitoring: Auto");
                m.addItem (304, "Monitoring: Off");
                break;
            case 3:
                m.addItem (405, "TUNE LIVE MIX");
                // It asks what to tune before it tunes anything, so it carries the ellipsis
                // every other item that opens something carries.
                m.addItem (400, "TUNE MIX" + juce::String (Glyph::ellip()));
                m.addItem (404, "TUNE CHANNEL   T", view.selectedChannel() >= 0);
                m.addSeparator();
                m.addItem (407, view.controller.hasReference() ? "MATCH TO REFERENCE" : "MATCH TO REFERENCE...",
                           view.controller.hasReference());
                m.addItem (408, view.controller.hasReference()
                                    ? "Reference: " + juce::String (view.controller.getReference().name) + juce::String (Glyph::ellip())
                                    : "Add a Reference Mix...");
                m.addSeparator();
                // SPEECH PRIORITY: the one thing in DINE that moves a level by itself, so it
                // says what it does rather than only what it is called.
                m.addItem (413, "Speech Priority: the band steps back while somebody speaks", true,
                           view.controller.getSpeechPriority(), nullptr);
                // SHARE THE MICS: the speaking mics as an automatic mixer - a podcast table, a
                // panel, an interview. The second thing that moves a level by itself, so it too
                // says what it does.
                m.addItem (416, "Share the Mics: the one speaking opens, the others step back", true,
                           view.controller.getAutoMix(), nullptr);
                m.addSeparator();
                m.addItem (409, "Mix Buddy" + juce::String (Glyph::ellip()));
                m.addItem (410, "Try Another Mix", view.controller.canTryAnotherMix());
                m.addSeparator();
                m.addItem (411, view.controller.canUndoMix()
                                    ? "Undo " + juce::String (view.controller.undoMixLabel())
                                    : juce::String ("Undo Mix"),
                           view.controller.canUndoMix());
                m.addItem (412, view.controller.canRedoMix()
                                    ? "Redo " + juce::String (view.controller.redoMixLabel())
                                    : juce::String ("Redo Mix"),
                           view.controller.canRedoMix());
                m.addSeparator();
                {
                    const auto move = view.controller.previewLoudnessMove();
                    m.addItem (420, move.possible ? "Raise Loudness to Target   (" + juce::String (move.moveDb > 0 ? "+" : "") + juce::String (move.moveDb, 1) + " dB)"
                                                  : juce::String ("Raise Loudness to Target"),
                               move.possible && ! view.controller.isLiveSafe());
                    juce::PopupMenu target;
                    const auto current = view.controller.getDelivery();
                    for (int i = 0; i < int (DeliveryLoudness::Count); ++i)
                    {
                        const auto d = DeliveryLoudness (i);
                        target.addItem (430 + i, d == DeliveryLoudness::FromPurpose ? juce::String (deliveryLoudnessName (d))
                                                                                    : juce::String (deliveryLoudnessName (d)) + "   " + juce::String (deliveryLoudnessLufs (d), 0) + " LUFS",
                                        true, current == d);
                    }
                    m.addSubMenu ("Loudness Target", target);
                    juce::PopupMenu sound;
                    for (int i = 0; i < int (MasterVoicing::Count); ++i)
                        sound.addItem (450 + i, masterVoicingName (MasterVoicing (i)), true, view.controller.getVoicing() == MasterVoicing (i));
                    m.addSubMenu ("Master Sound", sound);
                }
                m.addSeparator();
                // AUTOPILOT: the second thing in DINE allowed to move a level by itself, and
                // the only way to turn it on. Ticked while it is holding the mix.
                m.addItem (415, "Autopilot: hold this mix", true, view.controller.isAutopilotOn());
                m.addSeparator();
                m.addItem (414, "Reset Mix to Raw" + juce::String (Glyph::ellip()), ! view.controller.isLiveSafe());
                m.addSeparator();
                m.addItem (401, "Centre Macro Pads");
                m.addItem (402, view.controller.numSoloed() > 0
                                    ? "Clear Solo (" + juce::String (view.controller.numSoloed()) + ")"
                                    : juce::String ("Clear Solo"),
                           view.controller.numSoloed() > 0);
                m.addSeparator();
                m.addItem (403, "Bypass: Hear the Inputs   B", true, view.controller.isBypassed());
                m.addSeparator();
                m.addItem (406, "Mix Engineer: Use the Cloud Model",
                           OpenAiMixProvider().isAvailable(), view.usingCloudMixEngineer);
                break;
            case 4:
                m.addItem (500, "Play / Stop");
                m.addItem (501, "Record");
                m.addItem (502, "Return to Start");
                m.addItem (503, "Loop");
                break;
            case 5:
                m.addItem (600, "Tracks");
                m.addItem (601, "Mixer");
                m.addItem (602, "Tune");
                m.addItem (603, "Live");
                m.addItem (604, "Inspector");
                m.addSeparator();
                m.addItem (613, "Set-up and Routing");
                m.addItem (616, "Saved Input Patches");
                m.addSeparator();
                m.addItem (608, "Open Mixer in a New Window");
                m.addItem (617, "Open Live in a New Window");
                m.addItem (618, "Open Inspector in a New Window");
                m.addItem (609, "Outputs" + juce::String (Glyph::ellip()));
                m.addItem (630, "Check Inputs" + juce::String (Glyph::ellip()));
                m.addSeparator();
                m.addItem (631, "Dim the Broadcast (20 dB)", true, view.controller.isBroadcastDimmed());
                m.addItem (632, "Mute the Broadcast", true, view.controller.isBroadcastMuted());
                m.addSeparator();
                m.addItem (610, (view.sidebarShown ? "Hide Sidebar" : "Show Sidebar") + juce::String ("   ")
                                    + juce::String (juce::CharPointer_UTF8 ("\xe2\x8c\x83\xe2\x8c\x98")) + "S");
                m.addItem (615, "Show/Hide the two side panels");
                m.addSeparator();
                {
                    // Every theme, DINE's own then yours, the chosen one ticked; then the sheet.
                    juce::PopupMenu appearance;
                    view.themeMenuNames.clear();
                    bool mine = false;
                    for (const auto& t : ThemeStore::all())
                    {
                        if (! t.builtIn && ! mine) { mine = true; appearance.addSeparator(); }
                        appearance.addItem (640 + view.themeMenuNames.size(), t.name, true, t.name.equalsIgnoreCase (Dine::currentThemeName()));
                        view.themeMenuNames.add (t.name);
                    }
                    appearance.addSeparator();
                    // Text size sits with the themes because it is the same question - how
                    // this Mac reads - and is remembered in the same place.
                    juce::PopupMenu sizes;
                    const auto scaleNow = Dine::textScale();
                    for (size_t i = 0; i < ThemeStore::textSizes().size(); ++i)
                    {
                        const auto& size = ThemeStore::textSizes()[i];
                        sizes.addItem (660 + int (i), juce::String (size.name), true,
                                       std::abs (size.scale - scaleNow) < 0.0005f);
                    }
                    appearance.addSubMenu ("Text size", sizes);
                    appearance.addSeparator();
                    appearance.addItem (620, "Customise Appearance" + juce::String (Glyph::ellip()));
                    appearance.addItem (621, "Import a Theme" + juce::String (Glyph::ellip()));
                    appearance.addItem (622, "Show Themes Folder");
                    m.addSubMenu ("Appearance", appearance);
                }
                m.addSeparator();
                m.addItem (605, "Zoom In");
                m.addItem (606, "Zoom Out");
                m.addItem (607, "Zoom to Fit");
                break;
            default:
                m.addItem (701, "Getting started");
                m.addItem (702, "Show the guides again");
                m.addSeparator();
                // docs/ANALYTICS.md: what is sent, and what never is.
                if (auto* t = Telemetry::instance(); t != nullptr && t->isConfigured())
                    m.addItem (703, "Share anonymous usage data", true, t->isSharing());
                m.addItem (700, "About DINE");
                break;
        }
        return m;
    }

    void menuItemSelected (int id, int) override { view.handleCommand (id); }

private:
    MainView& view;
};

// ---------------------------------------------------------------- MainView
MainView::MainView (MixController& c, AppServices& s) : controller (c), services (s)
{
    // The theme and the text size first, before a single page reads a token or measures a
    // string. The headless snapshot tool renders from the design, so it takes neither.
    if (gUseStoredTheme)
    {
        Dine::applyTheme (ThemeStore::find (ThemeStore::chosenTheme()));
        Dine::setTextScale (ThemeStore::chosenTextSize());
    }
    lookAndFeel.applyPalette();
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setLookAndFeel (&lookAndFeel);

    sessionsPage = std::make_unique<SessionsPage> (controller, services);
    favouritesPage = std::make_unique<FavouritesPage> (controller, services);
    devicePage = std::make_unique<DevicePage> (controller, services);
    assignPage = std::make_unique<AssignPage> (controller, services);
    purposePage = std::make_unique<PurposePage> (controller);
    routingPage = std::make_unique<RoutingPage> (controller, services);
    tracksPage = std::make_unique<TracksPage> (controller, services);
    mixerPage = std::make_unique<MixerPage> (controller, services);
    mixPage = std::make_unique<MixPage> (controller);
    livePage = std::make_unique<LivePage> (controller, services);
    advancedPage = std::make_unique<AdvancedPage> (controller);
    transportBar = std::make_unique<TransportBar> (controller, services);
    toast = std::make_unique<Toast>();
    menu = std::make_unique<Menu> (*this);

    // ROUTING is added before the three pages it hosts, so they sit over its section list and
    // its LIVE SAFE cover rather than under them.
    addChildComponent (*routingPage);
    for (juce::Component* p : { (juce::Component*) sessionsPage.get(), (juce::Component*) favouritesPage.get(),
                                (juce::Component*) purposePage.get(), (juce::Component*) tracksPage.get(),
                                (juce::Component*) mixerPage.get(), (juce::Component*) mixPage.get(),
                                (juce::Component*) livePage.get(), (juce::Component*) advancedPage.get() })
        addChildComponent (*p);
    // The device and the patch are sections of ROUTING, so they are its children: its section
    // control sits at the top right of the page, over them.
    routingPage->host (*devicePage);
    routingPage->host (*assignPage);
    addChildComponent (*transportBar);

    // The chain along the foot belongs to the window now: one strip under every workspace.
    tracksPage->setFootShown (false);
    mixerPage->setFootShown (false);
    chainFoot = std::make_unique<ChainStrip>();
    chainFoot->setEmpty ("Click a strip, a track header or a channel to read its chain here. Click the chain to open the Inspector.");
    chainFoot->onOpen = [this]
    {
        const int strip = selectedChannel() >= 0 ? selectedChannel() : lastChannel;
        if (strip >= 0) { showPage (Page::Inspector); advancedPage->select (strip); }
        else if (page == Page::Mixer && mixerPage->selectedStrip() < 0) showPage (Page::Inspector);
    };
    addChildComponent (*chainFoot);

    // ---- the sidebar
    sidebar = std::make_unique<Sidebar> (services,
        [this] (Page p)
        {
            if (p == Page::Assign) assignPage->refresh();
            showPage (p);
        },
        [this] (Sidebar::Action a)
        {
            // Safety: two rows that are not pages. MIX HISTORY is the sheet it has always been
            // (the design's own note says so); SCENES is the sheet beside it.
            if (a == Sidebar::Action::MixHistory) showHistory();
            else if (a == Sidebar::Action::Scenes) { showPage (Page::Live); livePage->focusScenes(); }
            else if (a == Sidebar::Action::BroadcastReadiness) showBroadcastReadiness (true);
            else if (a == Sidebar::Action::CheckInputs) showCheck();
            else if (a == Sidebar::Action::Export) exportMix (AppServices::ExportFormat::Wav);
        },
        [this] (Page p)
        {
            if (p == Page::Mixer) openMixerWindow();
            else if (p == Page::Live) openLiveWindow();
            else if (p == Page::Inspector) openInspectorWindow();
        });
    sidebar->item (Page::Sessions).setTooltip ("The library: every saved session, and what each one was for.");
    sidebar->item (Page::Favourites).setTooltip ("The mixes that worked, with what they measured. A later tune can be aimed at one.");
    addAndMakeVisible (*sidebar);

    statusBar = std::make_unique<StatusBar> (controller, services);
    statusBar->onExportClicked = [this] { exportCellClicked(); };
    addAndMakeVisible (*statusBar);

    soloPill = std::make_unique<SoloPill>();
    soloPill->onJump = [this] (const MixController::SoloedItem& item) { jumpToSoloed (item); };
    soloPill->onClear = [this] { controller.clearSolos(); updateChrome(); };
    soloPill->setTooltip ("Something is soloed, so you are hearing it on its own. The room and the stream are "
                          "unchanged. Click the name to go to it, or the cross to clear every solo.");
    addChildComponent (*soloPill);

    // ---- the toolbar
    sidebarButton = std::make_unique<SidebarButton>();
    sidebarButton->setTooltip ("Show or hide the sidebar (Ctrl-Cmd-S)");
    sidebarButton->onClick = [this] { setSidebarShown (! sidebarShown); };
    addAndMakeVisible (*sidebarButton);

    sessionButton = std::make_unique<SessionButton>();
    sessionButton->setTooltip ("The session: new, open, save, import, export, the setup and the tour.");
    sessionButton->onClick = [this] { sessionMenu(); };
    addAndMakeVisible (*sessionButton);

    readyPill = std::make_unique<ReadyPill>();
    readyPill->setTooltip ("Whether anything needs you before the service: the inputs, and for a broadcast its checklist.");
    readyPill->onClick = [this] { readyPillClicked(); };
    addChildComponent (*readyPill);

    tuneLiveButton = std::make_unique<ToolbarToggle> ("TUNE LIVE MIX", ToolbarToggle::Kind::Verb, Dine::Icon::None);
    tuneLiveButton->setTooltip ("Start TUNE LIVE MIX from any workspace: DINE listens to the band, builds its mix and reasons "
                                "about what this band still needs. The listen and the result open on TUNE. Press again to stop.");
    tuneLiveButton->onClick = [this] { handleCommand (405); };
    addChildComponent (*tuneLiveButton);

    chatButton = std::make_unique<ToolbarToggle> ("MIX BUDDY", ToolbarToggle::Kind::Glyph, Dine::Icon::Chat);
    chatButton->setTooltip ("Open or close Mix Buddy, DINE's mix engineer in plain words: ask for a change to the mix - "
                            "a source, a level, a tone. It proposes; you keep.");
    chatButton->onClick = [this] { if (chatSheet != nullptr) closeSheets(); else showChat(); };
    addChildComponent (*chatButton);

    bypassButton = std::make_unique<ToolbarToggle> ("BYPASS", ToolbarToggle::Kind::Key, Dine::Icon::None, Dine::hot);
    bypassButton->setTooltip ("Hear the raw console feed: no processing, no fader moves, no effects. Press it again "
                              "for your mix. Nothing is changed either way (B).");
    bypassButton->onClick = [this] { setBypass (! controller.isBypassed()); };
    addChildComponent (*bypassButton);

    // The emergency keys. One press each way; lit while on; never a mix change, so nothing to
    // save or undo, and LIVE SAFE never locks them. Only the engineer's own listen is spared.
    dimButton = std::make_unique<ToolbarToggle> ("DIM", ToolbarToggle::Kind::Key, Dine::Icon::None, Dine::hot);
    dimButton->setTooltip ("Pull the broadcast and the room down 20 dB, now. Your own listen is unchanged. Press again to bring it back.");
    dimButton->onClick = [this] { controller.setBroadcastDim (! controller.isBroadcastDimmed()); updateChrome(); };
    addChildComponent (*dimButton);
    muteButton = std::make_unique<ToolbarToggle> ("MUTE", ToolbarToggle::Kind::Key, Dine::Icon::None, Dine::crit);
    muteButton->setTooltip ("Silence the broadcast and the room, now. Your own listen is unchanged. Press again to bring it back.");
    muteButton->onClick = [this] { controller.setBroadcastMute (! controller.isBroadcastMuted()); updateChrome(); };
    addChildComponent (*muteButton);

    // AUTOPILOT is the fourth broadcast key and the second thing in DINE allowed to move a
    // level by itself, so it is on the chrome wherever you are: lit while it is on, one press off.
    // "Auto" while it is off, as the v4 toolbar writes it; AUTOPILOT while it is on, because
    // CLAUDE.md wants it named in full on every workspace while it is moving faders.
    autopilotButton = std::make_unique<ToolbarToggle> ("Auto", ToolbarToggle::Kind::Key, Dine::Icon::None, Dine::monitor);
    autopilotButton->setTooltip ("Hold the mix you set. Autopilot moves group faders only, slowly, inside a few dB of "
                                 "the mix it was engaged on, and says why every time. Touch a fader and it is yours again.");
    // The same command the Mix menu's "Autopilot: hold this mix" carries. It asked for 626,
    // which nothing handled, so the key looked dead while the menu item worked.
    autopilotButton->onClick = [this] { handleCommand (415); };
    addChildComponent (*autopilotButton);

    liveSafeButton = std::make_unique<ToolbarToggle> ("LIVE SAFE", ToolbarToggle::Kind::Primary);
    liveSafeButton->setTooltip ("Locks the sound: re-routes and re-tunes are blocked, and a fader cannot move more "
                                "than 6 dB at a time. Mute, solo, the monitor and the recording always stay free.");
    liveSafeButton->onClick = [this] { handleCommand (614); };
    addChildComponent (*liveSafeButton);

    outputButton = std::make_unique<OutputPill>();
    outputButton->setTooltip ("Where the broadcast and your solo go.");
    outputButton->onClick = [this] { chooseOutput(); };
    addAndMakeVisible (*outputButton);

    addChildComponent (*toast);

    favouritesPage->onToast = [this] (const juce::String& t) { showToast (t); };
    sessionsPage->onNew = [this] { newSession(); };
    sessionsPage->onOpenFile = [this] { openSession(); };
    sessionsPage->onImportFolder = [this] { importMultitrack (true); };
    sessionsPage->onOpen = [this] (const juce::File& file)
    {
        const auto err = services.loadSession (file);
        if (err.isNotEmpty()) { showToast (err); return; }
        advancedPage->rebuild();
        mixerPage->rebuild();
        rebuildWindows();
        tracksPage->rebuild();
        showPage (controller.getSession().inputs.empty() ? Page::Assign : Page::Tracks);
        const auto recovered = services.takeRecoveryNote();
        showToast ("Opened \"" + services.currentSessionName() + "\"." + (recovered.isEmpty() ? "" : " " + recovered));
        updateChrome();
    };
    devicePage->onBack = [this] { showPage (Page::Sessions); };
    devicePage->onToast = [this] (const juce::String& s) { showToast (s); };
    devicePage->onContinue = [this]
    {
        if (! controller.getSession().inputs.empty()) enterSession();
        else showPage (Page::Assign);
    };
    devicePage->onContinueToAssign = [this] { showPage (Page::Assign); };
    devicePage->onSetUpOutputs = [this] { showOutputs(); };
    // The same import as File > Import Audio Files, not a copy of it: the copy here
    // rebuilt the setup and TRACKS but never the console, so the MIXER, its window and the
    // Inspector kept the strips of whatever was open before and the import looked partial.
    devicePage->onImportRecording = [this] (const juce::File& folder) { importMultitrackFolder (folder); };
    assignPage->onBack = [this] { showPage (Page::Device); };
    assignPage->onContinue = [this] { showPage (Page::Purpose); };
    assignPage->onSaveMapping = [this] { saveInputMapping(); };
    assignPage->onApplyMapping = [this] { showPage (Page::Maps); };

    // ---- ROUTING: the sections, and the two it owns
    routingPage->onSection = [this] (RoutingPage::Section s) { showPage (pageForSection (s)); };
    routingPage->onCoverChanged = [this] { if (isRoutingPage (page)) showPage (page); };
    routingPage->onToast = [this] (const juce::String& t) { showToast (t); };
    routingPage->onSaveMap = [this] { saveInputMapping(); };
    routingPage->onApplyMap = [this] (const juce::File& file) { applyInputMapping (file); };
    routingPage->onImportMap = [this]
    {
        mapChooser = std::make_unique<juce::FileChooser> ("Import an input patch", juce::File(), "*.dinemap.json");
        mapChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this] (const juce::FileChooser& fc)
                                 {
                                     const auto file = fc.getResult();
                                     if (file == juce::File()) return;
                                     InputMap imported;
                                     if (! InputMapStore::load (file, imported)) { showToast ("That is not a DINE input patch."); return; }
                                     if (! InputMapStore::save (imported)) { showToast ("That patch could not be saved."); return; }
                                     showToast ("Imported \"" + imported.name + "\".");
                                     showPage (Page::Maps);
                                 });
    };
    routingPage->onChooseOutputDevice = [this] (const juce::String& name)
    {
        if (name == services.currentOutputDevice()) return;
        const auto err = services.isAudioRunning() ? services.changeOutput (name) : services.openOutputOnly (name);
        if (err.isNotEmpty()) showToast (err);
        else { showToast ("Output: " + name); updateChrome(); }
    };
    purposePage->onBack = [this] { showPage (Page::Assign); };
    purposePage->onContinue = [this] { enterSession(); };

    tracksPage->onToast = [this] (const juce::String& t) { showToast (t); };
    tracksPage->onPanelWidthChanged = [this]
    {
        services.setTrackPanelWidth (tracksPage->panelWidth());
        services.touchSession();
    };
    tracksPage->onTimelineChanged = [this] { updateChrome(); };
    tracksPage->onOpenStrip = [this] (int strip) { inspectStrip (strip); };
    tracksPage->onOpenAssign = [this] { assignPage->refresh(); showPage (Page::Assign); };
    tracksPage->onTuneStrip = [this] (int strip) { tuneChannel (strip); };
    tracksPage->onSessionChanged = [this]
    {
        advancedPage->rebuild();
        mixerPage->rebuild();
        rebuildWindows();
        updateChrome();
    };
    mixPage->onOpenAdvanced = [this] { showPage (Page::Inspector); };
    mixPage->onToast = [this] (const juce::String& t) { showToast (t); };
    mixPage->onTuneStrip = [this] (int strip) { tuneChannel (strip); };
    mixPage->onOpenChat = [this] { showChat(); };
    mixPage->onOpenHistory = [this] { showHistory(); };
    mixPage->onOpenCheck = [this] { showCheck(); };
    mixPage->onOpenFavourites = [this] { showPage (Page::Favourites); };
    mixPage->onSelectStrip = [this] (int strip) { lastChannel = strip; updateChainFoot(); };
    mixPage->onGraphChanged = [this] { services.reconfigure(); };
    mixerPage->onOpenStrip = [this] (int strip) { inspectStrip (strip); };
    mixerPage->onOpenBus = [this] (MixBus bus) { inspectBus (bus); };
    mixerPage->onTuneStrip = [this] (int strip) { tuneChannel (strip); };
    mixerPage->onOpenWindow = [this] { openMixerWindow(); };
    mixerPage->onToast = [this] (const juce::String& t) { showToast (t); };
    mixerPage->onOpenAssign = [this] { assignPage->refresh(); showPage (Page::Assign); };
    livePage->onToast = [this] (const juce::String& t) { showToast (t); };
    livePage->onOpenHistory = [this] { showHistory(); };
    livePage->onLiveSafeChanged = [this] { updateChrome(); repaint(); };
    livePage->onToggleRecord = [this] { handleCommand (501); };
    advancedPage->onBack = [this] { showPage (Page::Tune); };
    advancedPage->onRetune = [this] { handleCommand (400); };
    // ADD A SOUND: a .wav of this church's own becomes one of the drum's sounds, copied into
    // the session folder so the session travels with the sound it was mixed with.
    advancedPage->onImportSample = [this] (RoleFamily family)
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a sound", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, family] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file == juce::File()) return;
                                  showToast (services.importSample (family, file));
                                  advancedPage->rebuild();
                              });
    };
    advancedPage->onTuneChannel = [this] (int strip) { tuneChannel (strip); };
    advancedPage->drumKitName = [this] { return drumKitName(); };
    advancedPage->onDrumKit = [this] (juce::Component& anchor) { drumKitMenu (anchor); };
    transportBar->onToast = [this] (const juce::String& t) { showToast (t); };
    transportBar->onTimelineChanged = [this] { timelineChanged(); };

    controller.onMessage = [this] (const std::string& m) { showToast (m); };
    seenRevision = services.sessionRevision();
    seenMilestone = services.sessionMilestone();

    setWantsKeyboardFocus (true);
    showPage (! services.listSessions().isEmpty() ? Page::Sessions : Page::Device);
    startTimerHz (30);

    if (gAutoTutorial && ! Tutorial::hasBeenSeen() && services.listSessions().isEmpty()
        && controller.getSession().inputs.empty())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainView> (this)]
                                         { if (safe != nullptr) safe->showTutorial(); });
}

MainView::~MainView()
{
    stopTimer();
    mixerWindow.reset();
    liveWindow.reset();
    inspectorWindow.reset();
    checkSheet.reset();
    historySheet.reset();
    readinessSheet.reset();
    themeSheet.reset();
    channelSheet.reset();
    chatSheet.reset();
    controller.onMessage = nullptr;
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

juce::MenuBarModel* MainView::getMenuModel() { return menu.get(); }

void MainView::enterSession()
{
    services.reconfigure();
    services.touchSession();
    advancedPage->rebuild();
    mixerPage->rebuild();
    rebuildWindows();
    tracksPage->rebuild();
    showPage (Page::Tracks);
}

void MainView::timelineChanged()
{
    tracksPage->rebuild();
    services.touchSession();
    updateChrome();
}

bool MainView::liveSafeBlocks (const juce::String& what)
{
    if (! services.daw().getProject().liveSafe) return false;
    showToast ("LIVE SAFE is on, so " + what + " is blocked: it would change what is on air. "
               + juce::String (liveSafe::allowedSummary()) + " Turn LIVE SAFE off first.");
    return true;
}

RoutingPage::Section MainView::sectionForPage (Page p) noexcept
{
    switch (p)
    {
        case Page::Assign:  return RoutingPage::Section::Inputs;
        case Page::Outputs: return RoutingPage::Section::Outputs;
        case Page::Maps:    return RoutingPage::Section::Maps;
        default:            return RoutingPage::Section::Device;
    }
}

MainView::Page MainView::pageForSection (RoutingPage::Section s) noexcept
{
    switch (s)
    {
        case RoutingPage::Section::Inputs:  return Page::Assign;
        case RoutingPage::Section::Outputs: return Page::Outputs;
        case RoutingPage::Section::Maps:    return Page::Maps;
        case RoutingPage::Section::Device:
        case RoutingPage::Section::Count:
        default:                            return Page::Device;
    }
}

void MainView::showPage (Page p)
{
    // "Routing" means the workspace at whatever section it was left on, which is what the
    // sidebar row and the View menu ask for.
    if (p == Page::Routing) p = pageForSection (routingPage->getSection());
    // Leaving the routing workspace locks it again: a confirmation is for one visit.
    if (isRoutingPage (page) && ! isRoutingPage (p)) routingPage->resetConfirmation();
    // ...and takes what was changed there to the audio. CONTINUE did this; the sidebar did
    // not, so an input linked as a stereo pair showed as one strip on the mixer while the
    // engine still ran the graph from before (2026-10-05).
    if (isRoutingPage (page) && ! isRoutingPage (p) && controller.needsReconfigure())
    {
        services.reconfigure();
        advancedPage->rebuild();
        mixerPage->rebuild();
        rebuildWindows();
        tracksPage->rebuild();
    }

    if (p == Page::Live && page != Page::Live) trackEvent ("live_view_opened", { { "audio_running", services.isAudioRunning() } });
    // LIVE wants the width (v4): going there folds the sidebar away, and leaving brings it back
    // only if that is what folded it - a sidebar somebody hid stays hidden.
    if (p == Page::Live && page != Page::Live && sidebarShown) { setSidebarShown (false, true); sidebarAutoHidden = true; }
    else if (p != Page::Live && page == Page::Live && sidebarAutoHidden) { sidebarAutoHidden = false; setSidebarShown (true, true); }
    page = p;
    sessionsPage->setVisible (p == Page::Sessions);
    favouritesPage->setVisible (p == Page::Favourites);
    purposePage->setVisible (p == Page::Purpose);
    routingPage->setVisible (isRoutingPage (p));
    if (isRoutingPage (p)) routingPage->setSection (sectionForPage (p));
    const bool hosted = isRoutingPage (p) && ! routingPage->isCovered();
    devicePage->setVisible (hosted && p == Page::Device);
    assignPage->setVisible (hosted && p == Page::Assign);
    tracksPage->setVisible (p == Page::Tracks);
    mixerPage->setVisible (p == Page::Mixer);
    mixPage->setVisible (p == Page::Tune);
    livePage->setVisible (p == Page::Live);
    advancedPage->setVisible (p == Page::Inspector);

    if (p == Page::Sessions) sessionsPage->refresh();
    if (p == Page::Favourites) favouritesPage->refresh();
    if (p == Page::Device) devicePage->refresh();
    if (p == Page::Assign) assignPage->refresh();
    if (p == Page::Purpose) purposePage->refresh();
    if (p == Page::Tracks)
    {
        if (const int w = services.trackPanelWidth(); w > 0) tracksPage->setPanelWidth (w);
        tracksPage->rebuild();
    }
    if (p == Page::Mixer) mixerPage->rebuild();
    if (p == Page::Live) livePage->rebuild();
    if (p == Page::Inspector) advancedPage->rebuild();

    maybeShowGuide();
    updateChrome();
    resized();
    repaint();
    grabKeyboardFocus();
}

// WHAT THIS WORKSPACE IS FOR, once. The first time a workspace is opened, a card in its
// corner says in two sentences what it is for; GOT IT dismisses that one for good and the
// chip beside it switches every one of them off. Nothing is blocked behind it, and Help >
// Show the guides again brings them all back.
void MainView::maybeShowGuide()
{
    guide.reset();
    // Never over a tour, and never while a sheet is asking something: two things explaining
    // themselves at once is worse than neither.
    if (! gGuides || tutorial != nullptr || openSheetName().isNotEmpty()) return;
    const auto prefs = guidePreferences();
    if (! Guides::enabled (prefs)) return;

    const auto* entry = WorkspaceGuide::entryFor (int (page));
    if (entry == nullptr || Guides::seen (entry->key, prefs)) return;

    const juce::String key (entry->key);
    guide = std::make_unique<WorkspaceGuide> (*entry);
    guide->onDismiss = [this, key, prefs] { Guides::markSeen (key, prefs); guide.reset(); resized(); repaint(); };
    guide->onTurnOff = [this, prefs]
    {
        Guides::setEnabled (false, prefs);
        guide.reset();
        resized();
        repaint();
        showToast ("The workspace guides are off. Help > Show the guides again brings them back.");
    };
    addAndMakeVisible (*guide);
    guide->toFront (false);
}

// THE SOLO PILL: what is soloed, beside the clock, on every workspace. It follows the mix on
// the tick as well as on the chrome's own events, because an S pressed on a MIXER strip, a TUNE
// or LIVE tile or a channel sheet goes straight to the controller - and a pill that only heard
// about some of them went on saying "SOLO MUSIC" over a console where nothing was soloed.
void MainView::refreshSoloPill()
{
    const bool wasShown = soloPill->isVisible();
    soloPill->setItems (controller.getSoloed());
    if (soloPill->hasAny() != wasShown) { soloPill->setVisible (soloPill->hasAny()); resized(); }
}

void MainView::updateChrome()
{
    const auto& session = controller.getSession();
    const bool running = services.isAudioRunning();
    const bool hasInputs = ! session.inputs.empty();
    const bool mixable = controller.isPrepared() && hasInputs;
    const bool inWorkspace = ! isSetupPage (page);
    const auto& project = services.daw().getProject();

    // ---- the sidebar's rows
    const Page all[11] = { Page::Sessions, Page::Favourites, Page::Routing, Page::Device, Page::Assign,
                           Page::Outputs, Page::Purpose, Page::Tracks, Page::Mixer, Page::Tune, Page::Live };
    for (const Page p : all)
        sidebar->item (p).setEnabled (isSetupPage (p) || mixable);
    sidebar->item (Page::Inspector).setEnabled (mixable);
    const bool showReadiness = broadcastReadinessApplies (session.purpose);
    sidebar->setBroadcastReadinessVisible (showReadiness);
    if (! showReadiness && readinessSheet != nullptr) readinessSheet.reset();
    // ROUTING's sections light their own rows now, so the page is handed over as it is: the
    // device and the saved patches still light ROUTING, because `indexOf` puts them there.
    sidebar->setSelected (page);
    sidebar->item (Page::Favourites).setMeta (controller.numFavourites() > 0 ? juce::String (controller.numFavourites()) : juce::String());
    // The inputs that need a hand on the desk, the way the v4 sidebar badges Check inputs.
    const int attention = mixable ? inputsNeedingAttention() : 0;
    if (auto* check = sidebar->actionItem (Sidebar::Action::CheckInputs))
    {
        check->setMeta (attention > 0 ? juce::String (attention) : juce::String());
        check->setMetaTint (Dine::warn);
        check->setEnabled (mixable);
    }
    if (auto* setlist = sidebar->actionItem (Sidebar::Action::Scenes)) setlist->setEnabled (mixable);
    if (auto* history = sidebar->actionItem (Sidebar::Action::MixHistory)) history->setEnabled (mixable);
    if (auto* exporting = sidebar->actionItem (Sidebar::Action::Export)) exporting->setEnabled (mixable);
    routingPage->refresh();

    // ---- the session button and the readiness pill
    {
        const auto name = services.currentSessionName();
        sessionButton->setText (name.isNotEmpty() ? name : juce::String ("Untitled"),
                                juce::String (styleProfileName (session.profile))
                                    + (services.autosavePending() ? " " + Glyph::dot() + " Edited" : juce::String()));
        // How far this service's checklist has got counts too, when the purpose has one.
        int open = attention;
        if (broadcastReadinessApplies (session.purpose))
        {
            const auto& active = controller.getReadiness().active;
            if (! active.id.empty() && active.hasWork())
            {
                const auto prog = active.progress();
                open += juce::jmax (0, prog.applicable - prog.checked);
            }
        }
        readyPill->setCount (open, running);
    }

    refreshSoloPill();

    // What is on the title row and the toolbar decides where everything else on them goes, so a
    // button appearing or disappearing lays both rows out again (resized() skips a hidden one).
    bool rowsChanged = false;
    auto show = [&rowsChanged] (juce::Component& c, bool visible)
    {
        if (c.isVisible() == visible) return;
        c.setVisible (visible);
        rowsChanged = true;
    };
    show (*outputButton, running || inWorkspace);
    show (*readyPill, mixable);
    show (*bypassButton, inWorkspace && mixable);
    bypassButton->setOn (controller.isBypassed());
    show (*dimButton, inWorkspace && mixable);
    dimButton->setOn (controller.isBroadcastDimmed());
    show (*muteButton, inWorkspace && mixable);
    muteButton->setOn (controller.isBroadcastMuted());
    show (*autopilotButton, inWorkspace && mixable);
    autopilotButton->setOn (controller.isAutopilotOn());
    show (*liveSafeButton, inWorkspace && mixable);
    liveSafeButton->setOn (project.liveSafe);
    if (autopilotButton->getButtonText() != (controller.isAutopilotOn() ? "AUTOPILOT" : "Auto"))
    {
        autopilotButton->setButtonText (controller.isAutopilotOn() ? "AUTOPILOT" : "Auto");
        rowsChanged = true;
    }
    show (*chatButton, mixable);
    chatButton->setOn (chatSheet != nullptr);
    show (*tuneLiveButton, mixable);
    tuneLiveButton->setOn (controller.isTuningLive());
    tuneLiveButton->setSuffix (controller.isTuningLive() ? juce::String ("  " + Glyph::dot() + "  STOP") : juce::String());
    show (*transportBar, inWorkspace);
    show (*chainFoot, inWorkspace && mixable);
    if (rowsChanged) resized();

    const juce::String out = services.outputDisplayName();
    outputButton->setText (out.isEmpty() ? "No output" : out,
                           Glyph::dot() + " solo " + OutputsSheet::soloChoiceLabel (controller, services, "nowhere"),
                           running);
    updateChainFoot();
}

// The chain along the foot reads the channel that was picked out last, wherever that was:
// the console, a track header, the Inspector, TUNE's rail.
void MainView::updateChainFoot()
{
    if (! chainFoot->isVisible()) return;
    if (! controller.isPrepared()) { chainFoot->setEmpty ("Set the device up first."); return; }

    const auto& state = controller.getBase();
    const auto& graph = controller.getGraph();
    int strip = selectedChannel();
    MixBus bus = MixBus::Count;
    if (page == Page::Mixer && strip < 0 && mixerPage->selectedBus() != MixBus::Count) bus = mixerPage->selectedBus();
    if (page == Page::Inspector && strip < 0) bus = advancedPage->selectedBus();
    if (strip >= 0) lastChannel = strip;
    if (strip < 0 && bus == MixBus::Count) strip = lastChannel;

    if (strip >= 0 && strip < state.numStrips && strip < graph.numStrips())
    {
        const auto& r = graph.strips[size_t (strip)];
        chainFoot->setSource (juce::String (r.name), Dine::busTint (r.bus), state.strips[size_t (strip)].channel,
                              false, r.inputB >= 0, hasSampleStage (r.role));
    }
    else if (bus != MixBus::Count)
    {
        const juce::String n = bus == MixBus::Master ? juce::String ("Master") : juce::String (mixBusName (bus));
        chainFoot->setSource (n.substring (0, 1) + n.substring (1).toLowerCase(), Dine::busTint (bus),
                              state.buses[size_t (bus)].channel, bus == MixBus::Master, true);
    }
    else chainFoot->setEmpty ("Click a strip, a track header or a channel to read its chain here. Click the chain to open the Inspector.");

    juce::String note;
    if (controller.isBypassed()) note = "BYPASS";
    chainFoot->setNote (note);
}

// The session menu (v4: the session button at the left of the toolbar). The document, the
// imports, the export, the setup, the look and the tour; then, quieter, the four set-up steps
// with what each is set to, which is how a volunteer finds out what is not done yet.
void MainView::setupPopover()
{
    const auto& session = controller.getSession();
    const bool running = services.isAudioRunning();
    const bool hasInputs = ! session.inputs.empty();
    const bool ready = controller.isPrepared() && hasInputs;
    const bool locked = services.daw().getProject().liveSafe;
    const juce::String ellip = Glyph::ellip();

    // A menu item with the shortcut written beside it, the way the menu bar writes it.
    const auto keyed = [] (int id, const juce::String& text, const char* keys)
    {
        juce::PopupMenu::Item item (text);
        item.itemID = id;
        item.shortcutKeyDescription = juce::String (juce::CharPointer_UTF8 (keys));
        return item;
    };

    juce::PopupMenu m;
    m.addSectionHeader ("Session and setup");
    m.addItem (keyed (11, "New Session", "⌘N"));
    m.addItem (keyed (6, "Open Session" + ellip, "⌘O"));
    m.addItem (keyed (7, "Save", "⌘S"));
    m.addItem (keyed (8, "Save As" + ellip, "⇧⌘S"));
    m.addSeparator();
    m.addItem (12, "Import Multitrack Folder" + ellip);
    m.addItem (13, "Add a Reference Mix" + ellip);
    m.addItem (14, "Save Input Mapping" + ellip, hasInputs);
    m.addItem (17, "Input Mappings" + ellip);
    m.addSeparator();
    m.addItem (keyed (15, "Export" + ellip, "⇧⌘E"));
    m.addItem (16, "Export Multitrack" + ellip);
    m.addItem (5, "Open Setup");
    m.addItem (9, "Rename or Fix the Inputs" + ellip);
    if (broadcastReadinessApplies (session.purpose)) m.addItem (20, "Broadcast Checklist" + ellip, ready);
    m.addSeparator();
    m.addItem (18, "Appearance" + ellip);
    m.addItem (10, "Getting Started");
    m.addItem (juce::PopupMenu::Item ("Reset Mix to Raw" + ellip).setID (19).setEnabled (ready && ! locked)
                   .setColour (Dine::crit));
    m.addItem (21, "Recover Session" + ellip);
    m.addSeparator();
    m.addSectionHeader ("Setup");
    m.addItem (1, juce::String (running ? juce::String (Glyph::check()) : juce::String (Glyph::dash()))
                      + "  Audio device" + juce::String ("   ")
                      + (running && services.currentInputDevice().isNotEmpty() ? services.currentInputDevice()
                                                                              : juce::String ("not chosen")));
    m.addItem (2, juce::String (hasInputs ? juce::String (Glyph::check()) : juce::String (Glyph::dash()))
                      + "  Inputs" + juce::String ("   ")
                      + (hasInputs ? juce::String (int (session.inputs.size())) + " named" : juce::String ("none yet")));
    m.addItem (3, juce::String (ready ? juce::String (Glyph::check()) : juce::String (Glyph::dash()))
                      + "  Purpose and sound" + juce::String ("   ")
                      + juce::String (mixPurposeName (session.purpose)) + ", "
                      + juce::String (styleProfileName (session.profile)));
    m.addItem (4, juce::String (Glyph::dash()) + juce::String ("  Recording destination   ")
                      + (services.sessionFolder() != juce::File() ? services.sessionFolder().getFileName()
                                                                  : juce::String ("chosen when you save")));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sessionButton.get())
                                               .withMinimumWidth (280),
                     [this] (int r)
                     {
                         switch (r)
                         {
                             case 1: showPage (Page::Device); break;
                             case 2: assignPage->refresh(); showPage (Page::Assign); break;
                             case 3: showPage (Page::Purpose); break;
                             case 4:
                             case 7: saveNow(); break;
                             case 5: showPage (controller.isPrepared() ? Page::Purpose : Page::Device); break;
                             case 6: showPage (Page::Sessions); break;
                             case 8: saveAs(); break;
                             case 9: assignPage->refresh(); showPage (Page::Assign); break;
                             case 10: showTutorial(); break;
                             case 11: newSession(); break;
                             case 12: importMultitrack(); break;
                             case 13: handleCommand (107); break;
                             case 14: saveInputMapping(); break;
                             case 15: exportMix (AppServices::ExportFormat::Wav); break;
                             case 16: exportMix (AppServices::ExportFormat::Wav, AppServices::ExportWhat::RawMultitrack); break;
                             case 17: showPage (Page::Maps); break;
                             case 18: showThemes(); break;
                             case 19: resetMixToRaw(); break;
                             case 20: showBroadcastReadiness (true); break;
                             // TODO(v4-backend): recovery is offered at launch, when DINE finds an
                             // autosave newer than the document; there is no way to ask for it later.
                             case 21: showToast ("Nothing to recover. When DINE finds unsaved work at launch it offers it to you then."); break;
                             default: break;
                         }
                     });
}

juce::Rectangle<int> MainView::spotlight (const juce::String& what) const
{
    if (what == "session" && sidebarButton != nullptr) return sidebarButton->getBounds().expanded (6, 4);
    if (what == "transport" && transportBar != nullptr && transportBar->isVisible()) return transportBar->getBounds();
    if (what == "livesafe" && liveSafeButton != nullptr && liveSafeButton->isVisible()) return liveSafeButton->getBounds();
    if (what == "rail" && sidebar != nullptr && sidebar->isVisible()) return sidebar->getBounds();
    // The workspaces are rows in the sidebar now, not a row of tabs.
    if (what == "tabs" && sidebar != nullptr && sidebar->isVisible())
        return sidebar->item (Page::Tracks).getBounds().getUnion (sidebar->item (Page::Inspector).getBounds())
                   .translated (sidebar->getX(), sidebar->getY()).expanded (4, 4);
    return {};
}

void MainView::setAutoTutorial (bool on) { gAutoTutorial = on; }
void MainView::setStoredThemeUsed (bool on) { gUseStoredTheme = on; }
void MainView::setGuidesUsed (bool on, juce::File preferences) { gGuides = on; gGuidePrefs = preferences; }

void MainView::closeTutorial() { tutorial.reset(); }

void MainView::showTutorial()
{
    if (tutorial != nullptr) { tutorial->toFront (true); return; }
    tutorial = std::make_unique<Tutorial>();
    tutorial->onStep = [this] (int p) { showPage (Page (juce::jlimit (0, 8, p))); };
    tutorial->spotFor = [this] (const juce::String& what) { return spotlight (what); };
    tutorial->onFinished = [this]
    {
        tutorial.reset();
        grabKeyboardFocus();
    };
    addAndMakeVisible (*tutorial);
    tutorial->setBounds (getLocalBounds());
    tutorial->start();
    tutorial->toFront (true);
}

// ---------------------------------------------------------------- the panels
void MainView::setSidebarShown (bool shown, bool automatic)
{
    if (! automatic) sidebarAutoHidden = false;
    if (shown == sidebarShown) return;
    sidebarShown = shown;
    sidebarButton->setOn (shown);
    sidebar->setVisible (true);

    // Mail's slide, on the display's clock: only in the real window (the application says
    // whether the Mac asks for less motion; the snapshot tool and the tests never animate),
    // and instant when Reduce Motion is on.
    const bool animate = prefersReducedMotion && ! prefersReducedMotion() && isShowing();
    if (! animate)
    {
        sidebarClock.reset();
        sidebarReveal = shown ? 1.0f : 0.0f;
        sidebar->setReveal (sidebarReveal);
        sidebar->setVisible (shown);
        resized();
        repaint();
        return;
    }
    revealFrom = sidebarReveal;
    revealStartMs = juce::Time::getMillisecondCounterHiRes();
    if (sidebarClock == nullptr)
        sidebarClock = std::make_unique<juce::VBlankAttachment> (this, [this] { stepSidebar(); });
}

namespace
{
    // cubic-bezier(0.32, 0.72, 0, 1), the curve the mockup's sidebar moves on: x(u) solved for
    // the time by Newton's method, then y(u). Six steps are more than enough at 60 frames.
    float sidebarEase (float t)
    {
        constexpr float x1 = 0.32f, y1 = 0.72f, x2 = 0.0f, y2 = 1.0f;
        const auto bez = [] (float u, float a, float b) { const float v = 1.0f - u; return 3.0f * v * v * u * a + 3.0f * v * u * u * b + u * u * u; };
        const auto dBez = [] (float u, float a, float b) { const float v = 1.0f - u; return 3.0f * v * v * a + 6.0f * v * u * (b - a) + 3.0f * u * u * (1.0f - b); };
        float u = t;
        for (int i = 0; i < 6; ++i)
        {
            const float d = dBez (u, x1, x2);
            if (std::abs (d) < 1.0e-5f) break;
            u = juce::jlimit (0.0f, 1.0f, u - (bez (u, x1, x2) - t) / d);
        }
        return bez (u, y1, y2);
    }
}

void MainView::stepSidebar()
{
    const float t = juce::jlimit (0.0f, 1.0f, float ((juce::Time::getMillisecondCounterHiRes() - revealStartMs) / 420.0));
    const float target = sidebarShown ? 1.0f : 0.0f;
    sidebarReveal = revealFrom + (target - revealFrom) * sidebarEase (t);
    sidebar->setReveal (sidebarReveal);
    resized();
    repaint();
    if (t >= 1.0f)
    {
        sidebarReveal = target;
        sidebar->setVisible (sidebarShown);
        // Released on the next message, not from inside its own callback.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainView> (this)]
                                         { if (safe != nullptr && safe->sidebarReveal == (safe->sidebarShown ? 1.0f : 0.0f)) safe->sidebarClock.reset(); });
    }
}

juce::String MainView::panelName (bool left) const
{
    if (left)
    {
        if (page == Page::Tune) return "Inputs";
        if (page == Page::Inspector) return "Channels";
        return "Sidebar";
    }
    if (page == Page::Inspector) return "What DINE did";
    return {};
}

bool MainView::panelShown (bool left) const
{
    if (left)
    {
        if (page == Page::Tune) return mixPage->isRailShown();
        if (page == Page::Inspector) return advancedPage->isRailShown();
        return sidebarShown;
    }
    if (page == Page::Inspector) return advancedPage->isTrailShown();
    if (page == Page::Tune) return mixPage->isSideShown();
    return true;
}

void MainView::togglePanel (bool left)
{
    if (left)
    {
        if (page == Page::Tune) { mixPage->setRailShown (! mixPage->isRailShown()); return; }
        if (page == Page::Inspector) { advancedPage->setRailShown (! advancedPage->isRailShown()); return; }
        setSidebarShown (! sidebarShown);
        return;
    }
    if (page == Page::Inspector) { advancedPage->setTrailShown (! advancedPage->isTrailShown()); return; }
    if (page == Page::Tune) mixPage->setSideShown (! mixPage->isSideShown());
}

// ---------------------------------------------------------------- bypass and the second console
void MainView::setBypass (bool on)
{
    if (! controller.isPrepared() || controller.getSession().inputs.empty())
    {
        showToast ("There is no mix to bypass yet. Assign your inputs first.");
        return;
    }
    if (controller.isBypassed() == on) return;
    controller.setBypass (on);
    bypassButton->setOn (on);
    showToast (on ? "Bypass on: you are hearing the raw console feed. Faders are disabled and the kept mix is untouched."
                  : "Bypass off. You are hearing the kept mix again.");
    mixerPage->repaint();
    if (mixerWindow != nullptr) mixerWindow->getPage().repaint();
    if (liveWindow != nullptr) liveWindow->getPage().repaint();
    if (inspectorWindow != nullptr) inspectorWindow->getPage().repaint();
    livePage->repaint();
    mixPage->repaint();
    updateChainFoot();
}

void MainView::closeSheets()
{
    if (mixPage != nullptr && mixPage->isScopeSheetOpen()) mixPage->closeScopeSheet();
    checkSheet.reset();
    historySheet.reset();
    readinessSheet.reset();
    themeSheet.reset();
    channelSheet.reset();
    chatSheet.reset();
    exportSheet.reset();
    choiceSheet.reset();
    updateChrome();
    resized();
}

void MainView::showThemes()
{
    if (themeSheet != nullptr) { themeSheet->refresh(); return; }
    themeSheet = std::make_unique<ThemeSheet> (gUseStoredTheme);
    themeSheet->onToast = [this] (const juce::String& t) { showToast (t); };
    themeSheet->onThemeChanged = [this]
    {
        updateChrome();
        if (menu != nullptr) menu->menuItemsChanged();   // the tick in View > Appearance follows
    };
    themeSheet->onClose = [this]
    {
        juce::Component::SafePointer<MainView> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->themeSheet.reset(); safe->updateChrome(); } });
    };
    addAndMakeVisible (*themeSheet);
    resized();
    themeSheet->toFront (true);
}

void MainView::applyThemeNamed (const juce::String& name)
{
    if (themeSheet != nullptr) { themeSheet->chooseTheme (name); return; }
    const auto theme = ThemeStore::find (name);
    Dine::applyTheme (theme);
    if (gUseStoredTheme) ThemeStore::setChosenTheme (theme.name);
    Dine::refreshAllWindows();
    updateChrome();
    if (menu != nullptr) menu->menuItemsChanged();       // the macOS menu is cached until the model says it changed
    showToast ("Appearance: " + theme.name);
}

// View > Appearance > Text size. Every string in the window is laid out again at the new
// size (setTextScale throws the layout cache away), every component is told to re-read the
// look, and every page lays itself out again - a page measures its own text, so a bigger
// name changes where things sit even though no metric moved.
void MainView::applyTextSize (float scale, const juce::String& name)
{
    Dine::setTextScale (scale);
    if (gUseStoredTheme) ThemeStore::setChosenTextSize (scale);
    Dine::refreshAllWindows();
    Dine::relayoutTree (*this);
    if (mixerWindow != nullptr) Dine::relayoutTree (*mixerWindow);
    if (liveWindow != nullptr) Dine::relayoutTree (*liveWindow);
    if (inspectorWindow != nullptr) Dine::relayoutTree (*inspectorWindow);
    updateChrome();
    if (menu != nullptr) menu->menuItemsChanged();
    showToast ("Text size: " + name);
}

// RESET MIX TO RAW. Everything DINE decided about the sound, taken back to the session's
// baseline. It is asked for out loud because it throws a service's mixing away - and answered
// with what it keeps, because the list of things it does *not* touch is the reassuring part.
void MainView::resetMixToRaw()
{
    if (liveSafeBlocks ("resetting the mix")) return;
    closeSheets();
    choiceSheet = std::make_unique<ChoiceSheet> (
        "Reset the mix to raw?",
        "Every channel goes back to how it sounded before DINE touched it, ready to show raw, then "
        "TUNE MIX, then the finished mix again.");
    choiceSheet->setColumns (
        { "Resets", Dine::warn, { "Faders, pans and sends", "EQ, dynamics and effects", "TUNE MIX results",
                                  "Sample replacement", "Group and master processing" }, false },
        { "Keeps", Dine::ok, { "Recordings and clips", "Track names and order", "Input map and routing",
                               "Scenes, favourites and history", "The reference mix" }, true });
    choiceSheet->setNote ("A checkpoint \"Before reset to raw\" is saved first. Undo brings everything back.",
                          "Not the same as BYPASS, which only lets you listen to the raw inputs.");
    choiceSheet->addAction ("Cancel", false, false, {});
    choiceSheet->addAction ("Reset to raw", true, false, [this]
    {
        if (! controller.resetMixToRaw()) return;
        mixerPage->rebuild();
        rebuildWindows();
        advancedPage->rebuild();
        livePage->rebuild();
        updateChrome();
    });
    choiceSheet->onClose = [this] { choiceSheet.reset(); resized(); repaint(); grabKeyboardFocus(); };
    addAndMakeVisible (*choiceSheet);
    resized();
    choiceSheet->grabKeyboardFocus();
}

// RECOVER SESSION? The autosave and the last save, side by side, with what each one holds:
// a volunteer choosing between two mixes needs to be told what is in them, not asked to guess
// from two timestamps. Nothing is deleted by any of the three answers.
void MainView::offerRecovery (RecoveryOffer offer)
{
    closeSheets();
    choiceSheet = std::make_unique<ChoiceSheet> ("Recover session?", offer.sentence);
    choiceSheet->setColumns ({ "Autosave  " + offer.autosaveWhen, Dine::accent, offer.autosaveFacts, false },
                             { "Last saved  " + offer.documentWhen, Dine::ink2, offer.documentFacts, false });
    choiceSheet->setNote ("Recorded takes are safe either way: unfinished takes were repaired and put back on their tracks.",
                          {});
    choiceSheet->addAction ("Keep both", false, false, offer.onKeepBoth);
    choiceSheet->addAction ("Open last saved", false, false, offer.onOpenSaved);
    choiceSheet->addAction ("Recover " + offer.autosaveWhen, false, true, offer.onRecover);
    choiceSheet->onClose = [this] { choiceSheet.reset(); resized(); repaint(); grabKeyboardFocus(); };
    addAndMakeVisible (*choiceSheet);
    resized();
    choiceSheet->grabKeyboardFocus();
}

// macOS IS ABOUT TO ASK. The one thing a volunteer needs to be told is that DINE is not
// asking for the room's microphone - it is asking for the desk - and that macOS has one switch
// for both. Two columns: what it listens to, and what it does with it. Saying no costs nothing
// that matters, and the note says so rather than leaving it to be found out.
void MainView::explainMicrophone (MicrophoneAsk ask)
{
    micExplained = true;
    closeSheets();
    const auto what = ask.device.isNotEmpty() ? ask.device : juce::String ("your audio device");
    choiceSheet = std::make_unique<ChoiceSheet> (
        "macOS is about to ask about the microphone",
        "DINE is opening " + what + ". macOS calls every audio input a microphone "
        + juce::String (Glyph::dash()) + " a thirty-two channel desk and this Mac's own mic are the "
        "same switch " + Glyph::dash() + " so it asks once, the first time DINE listens.");
    choiceSheet->setColumns (
        { "What DINE listens to", Dine::accent, { "The inputs of " + what,
                                                   "Nothing else on this Mac",
                                                   "Only while a device is open" }, true },
        { "What it does with them", Dine::ink2, { "The meters, the mix and the broadcast",
                                                  "Recording, on the tracks you set to record",
                                                  "TUNE, which listens and then sets the mix",
                                                  "No audio leaves this Mac" }, true });
    choiceSheet->setNote ("Say Not now and DINE still plays, mixes, saves and exports. Only the meters stay still.",
                          "You can change it any time in System Settings > Privacy & Security > Microphone.");

    // Escape, or the sheet closed any other way, is Not now: a session has to open either way,
    // and the careful answer is the one that asks macOS for nothing. An action's own handler
    // runs immediately after onClose, so the fallback is deferred by one message and finds the
    // flag already set when one did.
    auto answered = std::make_shared<bool> (false);
    auto notNow = ask.onNotNow;
    choiceSheet->addAction ("Not now", false, false, [answered, f = ask.onNotNow] { *answered = true; if (f) f(); });
    choiceSheet->addAction ("Continue", false, true, [answered, f = ask.onContinue] { *answered = true; if (f) f(); });
    choiceSheet->onClose = [this, answered, notNow]
    {
        choiceSheet.reset();
        resized();
        repaint();
        grabKeyboardFocus();
        juce::MessageManager::callAsync ([answered, notNow]
        {
            if (*answered) return;
            *answered = true;
            if (notNow) notNow();
        });
    };
    addAndMakeVisible (*choiceSheet);
    resized();
    choiceSheet->grabKeyboardFocus();
}

void MainView::followMicrophone()
{
    const auto held = services.inputHeldBack();
    if (held == InputAccess::Listen) return;
    // macOS said yes - the prompt was answered, or the switch was turned on in System Settings
    // while DINE was open. Nobody has to quit and open it again.
    if (services.retryHeldInput())
    {
        const auto st = services.deviceState();
        showToast ("macOS now lets DINE hear " + (st.input.isNotEmpty() ? st.input : juce::String ("the inputs"))
                   + ". The inputs are open.");
        updateChrome();
        return;
    }
    // Never asked, and nobody has been told yet this run: the same sheet launch shows, then
    // the prompt. Not while another sheet is up - a recovery question comes first.
    // (AskFirst with macOS already saying yes cannot reach here: the retry above opened it.)
    if (held == InputAccess::AskFirst && ! micExplained && openSheetName().isEmpty() && choiceSheet == nullptr)
    {
        MicrophoneAsk ask;
        ask.device = services.deviceState().input;
        juce::Component::SafePointer<MainView> safe (this);
        ask.onContinue = [safe]
        {
            if (safe == nullptr) return;
            safe->services.askForInputPermission ([safe] (bool) { if (safe != nullptr) safe->followMicrophone(); });
        };
        explainMicrophone (std::move (ask));
    }
}

void MainView::showCheck()
{
    if (checkSheet != nullptr) { checkSheet->refresh(); return; }
    checkSheet = std::make_unique<CheckSheet> (controller, services);
    checkSheet->onClose = [this]
    {
        juce::Component::SafePointer<MainView> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->checkSheet.reset(); safe->updateChrome(); } });
    };
    addAndMakeVisible (*checkSheet);
    resized();
    checkSheet->toFront (true);
}

void MainView::showHistory()
{
    if (historySheet != nullptr) { historySheet->refresh(); return; }
    historySheet = std::make_unique<HistorySheet> (controller, services);
    historySheet->onToast = [this] (const juce::String& s) { showToast (s); };
    historySheet->onClose = [this]
    {
        juce::Component::SafePointer<MainView> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->historySheet.reset(); safe->updateChrome(); } });
    };
    addAndMakeVisible (*historySheet);
    resized();
    historySheet->toFront (true);
}

void MainView::showBroadcastReadiness (bool history)
{
    if (! broadcastReadinessApplies (controller.getSession().purpose))
    {
        showToast ("Broadcast readiness is for Church Broadcast or Livestream. Other purposes will get their own checklist later.");
        if (readinessSheet != nullptr) readinessSheet.reset();
        return;
    }
    if (readinessSheet != nullptr)
    {
        readinessSheet->setMode (history ? BroadcastReadinessSheet::Mode::History
                                         : BroadcastReadinessSheet::Mode::Checklist);
        readinessSheet->refresh();
        readinessSheet->toFront (true);
        return;
    }
    readinessSheet = std::make_unique<BroadcastReadinessSheet> (
        controller, services,
        history ? BroadcastReadinessSheet::Mode::History : BroadcastReadinessSheet::Mode::Checklist);
    readinessSheet->onToast = [this] (const juce::String& s) { showToast (s); };
    // Each of these runs after the sheet's own click has finished (BroadcastReadinessSheet::later),
    // so closing the sheet here never destroys a button inside its own callback.
    readinessSheet->onOpenCheck = [this] { readinessSheet.reset(); showCheck(); };
    readinessSheet->onOpenHistory = [this] { readinessSheet.reset(); showHistory(); };
    readinessSheet->onOpenOutputs = [this] { readinessSheet.reset(); showOutputs(); };
    readinessSheet->onClose = [this]
    {
        juce::Component::SafePointer<MainView> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->readinessSheet.reset(); safe->updateChrome(); } });
    };
    addAndMakeVisible (*readinessSheet);
    resized();
    readinessSheet->toFront (true);
}

// Outputs is a section of ROUTING now, not a sheet over the console: where the sound leaves
// this Mac belongs with the device it leaves by and the inputs it came in on. Everything that
// asked for the sheet - the toolbar, the View menu, the LIVE page - asks for the section.
void MainView::showOutputs() { showPage (Page::Outputs); }

int MainView::selectedChannel() const
{
    switch (page)
    {
        case Page::Mixer:     return mixerPage->selectedStrip();
        case Page::Tracks:    return tracksPage->selectedTrack();
        case Page::Inspector: return advancedPage->selectedStrip();
        case Page::Tune:      return mixPage->selectedStrip();
        default:              return -1;
    }
}

void MainView::tuneChannel (int strip, const MixController::ListenSettings& settings)
{
    if (liveSafeBlocks ("TUNE CHANNEL")) return;
    if (! controller.isPrepared() || strip < 0 || strip >= controller.getEngine().getNumStrips())
    {
        showToast ("Assign your inputs first: there is nothing to tune yet.");
        return;
    }
    if (controller.isBypassed())
    {
        showToast ("BYPASS is on: switch it off to tune a channel.");
        return;
    }
    if (controller.getStage() == MixController::Stage::Listening || controller.getStage() == MixController::Stage::Planning)
    {
        showToast ("DINE is already listening. Let it finish, or cancel it first.");
        return;
    }

    channelSheet.reset();
    controller.startTuneChannel (strip, settings);
    if (! controller.isListening()) return;

    channelSheet = std::make_unique<ChannelTuneSheet> (controller, strip);
    channelSheet->onToast = [this] (const juce::String& t) { showToast (t); };
    channelSheet->onOpenInspector = [this, strip] { showPage (Page::Inspector); advancedPage->select (strip); };
    channelSheet->onClose = [this]
    {
        juce::Component::SafePointer<MainView> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->channelSheet.reset(); safe->updateChrome(); } });
    };
    addAndMakeVisible (*channelSheet);
    resized();
    channelSheet->toFront (true);
}

void MainView::showChat() { openChat(); }

void MainView::performBuddyAction (const BuddyAction& a)
{
    const int strips = controller.isPrepared() ? controller.getEngine().getNumStrips() : 0;
    const bool validStrip = a.strip >= 0 && a.strip < strips;
    switch (a.kind)
    {
        case BuddyActionKind::ShowPage:
        {
            switch (a.page)
            {
                case BuddyPage::Tracks:    showPage (Page::Tracks); break;
                case BuddyPage::Mixer:     showPage (Page::Mixer); break;
                case BuddyPage::Tune:      showPage (Page::Tune); break;
                case BuddyPage::Live:      showPage (Page::Live); break;
                case BuddyPage::Inspector: showPage (Page::Inspector); break;
                case BuddyPage::Routing:   showPage (Page::Routing); break;
                case BuddyPage::Assign:    showPage (Page::Assign); break;
                case BuddyPage::Device:    showPage (Page::Device); break;
                case BuddyPage::Purpose:   showPage (Page::Purpose); break;
                case BuddyPage::Outputs:   showOutputs(); break;
                case BuddyPage::Sessions:  showPage (Page::Sessions); break;
            }
            break;
        }
        case BuddyActionKind::OpenInspector:
            if (validStrip) { showPage (Page::Inspector); advancedPage->select (a.strip); }
            break;
        case BuddyActionKind::SoloStrip:
            // Only ever the engineer's listen: with solo set to change the main mix, Mix Buddy
            // does not press it - that would be Mix Buddy changing what the room hears.
            if (! validStrip) break;
            if (controller.getKept().monitor.mode == SoloMode::InPlace)
            {
                showToast ("Solo is set to SOLO IN PLACE, which changes the main mix, so Mix Buddy leaves it to you.");
                break;
            }
            controller.setStripSolo (a.strip, ! controller.getKept().strips[size_t (a.strip)].solo);
            break;
        case BuddyActionKind::OpenCheckInputs: showCheck(); break;
        case BuddyActionKind::OpenHistory:     showHistory(); break;
        case BuddyActionKind::RunTuneMix:      showPage (Page::Tune); break;     // TUNE MIX asks what to tune, there
        case BuddyActionKind::RunTuneChannel:  if (validStrip) tuneChannel (a.strip); break;
        case BuddyActionKind::AskForChange:    controller.askForChange (a.request); break;
    }
    updateChrome();
}

void MainView::openChat()
{
    if (chatSheet != nullptr)
    {
        chatSheet->toFront (true);
        chatSheet->takeFocus();
        return;
    }
    if (! controller.isPrepared())
    {
        showToast ("Assign your inputs first: there is nothing to talk about yet.");
        return;
    }
    chatSheet = std::make_unique<ChatSheet> (controller);
    chatSheet->onToast = [this] (const juce::String& t) { showToast (t); };
    chatSheet->onAction = [this] (const BuddyAction& a) { performBuddyAction (a); };
    chatSheet->onClose = [this]
    {
        juce::Component::SafePointer<MainView> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->chatSheet.reset(); safe->updateChrome(); safe->resized(); } });
    };
    addAndMakeVisible (*chatSheet);
    updateChrome();
    resized();
    chatSheet->toFront (true);
    chatSheet->takeFocus();
}

// ---------------------------------------------------------------------------
// Input mappings
// ---------------------------------------------------------------------------
void MainView::saveInputMapping()
{
    const auto& session = controller.getSession();
    if (session.inputs.empty()) { showToast ("There is nothing patched yet."); return; }

    mapDialog = std::make_unique<juce::AlertWindow> ("Save input mapping",
                                                     "A name you will recognise next Sunday.",
                                                     juce::MessageBoxIconType::NoIcon);
    mapDialog->addTextEditor ("name", services.currentSessionName().isEmpty() ? "Main hall" : services.currentSessionName(), "Name");
    mapDialog->addTextEditor ("note", "", "Note (optional)");
    mapDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    mapDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    mapDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (mapDialog == nullptr) return;
        const auto name = mapDialog->getTextEditorContents ("name").trim();
        const auto note = mapDialog->getTextEditorContents ("note").trim();
        mapDialog.reset();
        if (result != 1 || name.isEmpty()) return;

        auto map = InputMapStore::fromSession (controller.getSession(), name, services.currentInputDevice(),
                                               services.numInputChannels(), true);
        map.note = note;
        if (InputMapStore::save (map))
        {
            trackEvent ("preset_saved", { { "kind", "input_map" }, { "inputs", int (map.inputs.size()) } });
            showToast ("Patch saved as \"" + name + "\": " + juce::String (map.inputs.size()) + " inputs across "
                       + juce::String (map.channelsNeeded()) + " channels.");
        }
        else
            showToast ("That mapping could not be saved.");
    }), false);
}

void MainView::openInputMappings()
{
    const auto maps = InputMapStore::list();
    juce::PopupMenu m;
    if (maps.isEmpty())
        m.addItem (-1, "No saved patches yet", false, false);

    int id = 1;
    juce::Array<juce::File> files;
    for (const auto& l : maps)
    {
        juce::PopupMenu sub;
        const int base = id;
        files.add (l.file);
        sub.addItem (base, "Apply to this session");
        sub.addSeparator();
        sub.addItem (base + 1, "Rename" + juce::String (Glyph::ellip()));
        sub.addItem (base + 2, "Duplicate");
        sub.addItem (base + 3, "Export" + juce::String (Glyph::ellip()));
        sub.addSeparator();
        sub.addItem (base + 4, "Delete");
        id += 10;
        m.addSubMenu (l.name + "   (" + juce::String (l.inputCount) + " inputs, "
                          + juce::String (l.channelsNeeded) + " channels)", sub);
    }
    m.addSeparator();
    m.addItem (900, "Import a patch" + juce::String (Glyph::ellip()));
    m.addItem (901, "Save this session's patch" + juce::String (Glyph::ellip()),
               ! controller.getSession().inputs.empty());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (300),
                     [this, files] (int chosen)
    {
        if (chosen <= 0) return;
        if (chosen == 901) { saveInputMapping(); return; }
        if (chosen == 900)
        {
            mapChooser = std::make_unique<juce::FileChooser> ("Import an input mapping", juce::File(), "*.dinemap.json");
            mapChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                     [this] (const juce::FileChooser& fc)
                                     {
                                         const auto file = fc.getResult();
                                         if (file == juce::File()) return;
                                         InputMap imported;
                                         if (! InputMapStore::load (file, imported)) { showToast ("That is not a DINE input mapping."); return; }
                                         if (! InputMapStore::save (imported)) { showToast ("That mapping could not be saved."); return; }
                                         showToast ("Imported \"" + imported.name + "\". Open Input Mappings to apply it.");
                                     });
            return;
        }

        const int index = (chosen - 1) / 10;
        const int action = (chosen - 1) % 10;
        if (index < 0 || index >= files.size()) return;
        InputMap map;
        if (! InputMapStore::load (files[index], map)) { showToast ("That mapping could not be read."); return; }

        switch (action)
        {
            case 0: applyInputMapping (files[index]); break;
            case 1:
            {
                mapDialog = std::make_unique<juce::AlertWindow> ("Rename mapping", "", juce::MessageBoxIconType::NoIcon);
                mapDialog->addTextEditor ("name", map.name, "Name");
                mapDialog->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
                mapDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                const auto from = map.name;
                mapDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, from] (int r)
                {
                    if (mapDialog == nullptr) return;
                    const auto to = mapDialog->getTextEditorContents ("name").trim();
                    mapDialog.reset();
                    if (r != 1) return;
                    const auto err = InputMapStore::rename (from, to);
                    showToast (err.isEmpty() ? "Renamed to \"" + to + "\"." : err);
                }), false);
                break;
            }
            case 2:
            {
                const auto err = InputMapStore::duplicate (map.name, map.name + " copy");
                showToast (err.isEmpty() ? "Duplicated \"" + map.name + "\"." : err);
                break;
            }
            case 3:
            {
                mapChooser = std::make_unique<juce::FileChooser> ("Export input mapping",
                                                                  juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                      .getChildFile (juce::File::createLegalFileName (map.name) + ".dinemap.json"),
                                                                  "*.dinemap.json");
                mapChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                         [this, map] (const juce::FileChooser& fc)
                                         {
                                             const auto dest = fc.getResult();
                                             if (dest == juce::File()) return;
                                             showToast (InputMapStore::saveAs (map, dest)
                                                            ? "Exported to " + dest.getFileName() + "."
                                                            : juce::String ("That mapping could not be exported."));
                                         });
                break;
            }
            case 4:
                if (InputMapStore::remove (map.name)) showToast ("Deleted \"" + map.name + "\".");
                break;
            default: break;
        }
    });
}

void MainView::applyInputMapping (const juce::File& file)
{
    if (liveSafeBlocks ("changing the routing")) return;
    InputMap map;
    if (! InputMapStore::load (file, map)) { showToast ("That mapping could not be read."); return; }
    trackEvent ("preset_applied", { { "kind", "input_map" } });

    const auto result = InputMapStore::apply (map, controller.getSession(), services.numInputChannels(),
                                              services.currentInputDevice(), map.hasSound);

    if (! result.problems.empty())
    {
        juce::String body;
        for (const auto& p : result.problems) body << p << "\n\n";
        body << juce::String (result.restored) << " of " << juce::String (map.inputs.size())
             << " inputs can be restored exactly as they were. Nothing is routed until you say so.";
        mapDialog = std::make_unique<juce::AlertWindow> ("Apply \"" + map.name + "\"?", body,
                                                         juce::MessageBoxIconType::WarningIcon);
        mapDialog->addButton ("Apply anyway", 1, juce::KeyPress (juce::KeyPress::returnKey));
        mapDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        const auto session = result.session;
        mapDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, session, map, result] (int r)
        {
            mapDialog.reset();
            if (r != 1) return;
            controller.setSession (session);
            services.reconfigure();
            services.touchSession();
            assignPage->refresh();
            updateChrome();
            resized();
            showToast ("Applied \"" + map.name + "\": " + juce::String (result.restored) + " inputs restored, "
                       + juce::String (result.unavailable + result.conflicts) + " switched off. Check the INPUTS page.");
        }), false);
        return;
    }

    controller.setSession (result.session);
    services.reconfigure();
    services.touchSession();
    assignPage->refresh();
    updateChrome();
    resized();
    showToast ("Applied \"" + map.name + "\": " + juce::String (result.restored) + " inputs.");
}

void MainView::openMixerWindow()
{
    if (! controller.isPrepared() || controller.getSession().inputs.empty())
    {
        showToast ("Assign your inputs first: there is nothing to mix yet.");
        return;
    }
    if (mixerWindow != nullptr)
    {
        mixerWindow->toFront (true);
        return;
    }
    mixerWindow = std::make_unique<MixerWindow> (controller, services, *this);
    showToast ("The console is open in its own window. Close it and it comes back here.");
}

void MainView::rebuildWindows()
{
    if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
    if (liveWindow != nullptr) liveWindow->rebuild();
    if (inspectorWindow != nullptr) inspectorWindow->rebuild();
}

void MainView::openLiveWindow()
{
    if (liveWindow != nullptr) { liveWindow->toFront (true); return; }
    auto page = std::make_unique<LivePage> (controller, services);
    auto* p = page.get();
    p->onToast = [this] (const juce::String& t) { showToast (t); };
    p->onOpenHistory = [this] { toFront (true); showHistory(); };
    p->onLiveSafeChanged = [this] { updateChrome(); repaint(); };
    p->onToggleRecord = [this] { handleCommand (501); };
    p->rebuild();
    liveInWindow = p;
    liveWindow = std::make_unique<PageWindow> ("Live", std::move (page),
                                               [p] { p->refresh(); }, [p] { p->rebuild(); },
                                               [this] { closePageWindow (liveWindow); }, 900, 560, 1280, 760);
    showToast ("LIVE is open in its own window. Close it and it is still here in the sidebar.");
}

void MainView::openInspectorWindow()
{
    if (! controller.isPrepared() || controller.getSession().inputs.empty())
    {
        showToast ("Assign your inputs first: there is no channel to look at yet.");
        return;
    }
    if (inspectorWindow != nullptr) { inspectorWindow->toFront (true); return; }
    auto page = std::make_unique<AdvancedPage> (controller);
    auto* p = page.get();
    p->onRetune = [this] { toFront (true); handleCommand (400); };
    p->onTuneChannel = [this] (int strip) { toFront (true); tuneChannel (strip); };
    p->onImportSample = [this] (RoleFamily family) { if (advancedPage->onImportSample) advancedPage->onImportSample (family); };
    p->drumKitName = [this] { return drumKitName(); };
    p->onDrumKit = [this] (juce::Component& anchor) { drumKitMenu (anchor); };
    p->onBack = [this] { closePageWindow (inspectorWindow); };
    p->rebuild();
    inspectorInWindow = p;
    inspectorWindow = std::make_unique<PageWindow> ("Inspector", std::move (page),
                                                    [p] { p->refresh(); }, [p] { p->rebuild(); },
                                                    [this] { closePageWindow (inspectorWindow); }, 900, 620, 1240, 820);
    showToast ("The Inspector is open in its own window. Double-click a channel on the console and it opens there.");
}

void MainView::closePageWindow (std::unique_ptr<PageWindow>& w)
{
    if (&w == &liveWindow) liveInWindow = nullptr;
    if (&w == &inspectorWindow) inspectorInWindow = nullptr;
    auto* which = &w;
    juce::Component::SafePointer<MainView> safe (this);
    juce::MessageManager::callAsync ([safe, which] { if (safe != nullptr) which->reset(); });
}

void MainView::inspectStrip (int strip)
{
    if (inspectorInWindow != nullptr) { inspectorInWindow->select (strip); inspectorWindow->toFront (true); return; }
    showPage (Page::Inspector);
    advancedPage->select (strip);
}

void MainView::inspectBus (MixBus bus)
{
    if (inspectorInWindow != nullptr) { inspectorInWindow->selectBus (bus); inspectorWindow->toFront (true); return; }
    showPage (Page::Inspector);
    advancedPage->selectBus (bus);
}

void MainView::closeMixerWindow()
{
    juce::Component::SafePointer<MainView> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->mixerWindow.reset(); });
}

void MainView::showToast (const juce::String& text)
{
    toast->show (text);
    toastTicks = 30 * 5;
    resized();
}

// ---------------------------------------------------------------- commands
void MainView::handleCommand (int id)
{
    if (id >= 640 && id < 640 + themeMenuNames.size()) { applyThemeNamed (themeMenuNames[id - 640]); return; }
    if (id >= 3000 && id < 3200)
    {
        juce::PopupMenu unused;
        std::vector<ChannelRole> roles;
        TracksPage::fillNewTrackMenu (unused, 3000, roles);
        if (tracksPage != nullptr && id - 3000 < int (roles.size()))
        {
            showPage (Page::Tracks);
            tracksPage->addTrack (roles[size_t (id - 3000)]);
        }
        return;
    }
    switch (id)
    {
        case 100: newSession(); break;
        case 101: openSession(); break;
        case 102: saveNow(); break;
        case 103: saveAs(); break;
        case 104: importMultitrack(); break;
        case 107:
        case 408:
            showPage (Page::Tune);
            mixPage->openReference();
            break;
        case 105: exportMix (AppServices::ExportFormat::Wav); break;
        case 106: exportMix (AppServices::ExportFormat::Mp3); break;
        case 110: exportMix (AppServices::ExportFormat::Wav, AppServices::ExportWhat::RawMultitrack); break;

        // Cmd+Z: the undo of whatever this workspace edits (undoTarget). Never a reset, never
        // a session reload, never the devices: each domain only puts back its own edits.
        case 200: undoHere(); break;
        case 204: redoHere(); break;
        case 201: if (! liveSafeBlocks ("editing the timeline")) tracksPage->splitAtPlayhead(); break;
        case 202: if (! liveSafeBlocks ("editing the timeline")) tracksPage->deleteSelection(); break;
        case 203:
            if (liveSafeBlocks ("adding a marker")) break;
            showPage (Page::Tracks);
            tracksPage->addMarkerAtPlayhead();
            break;

        case 300:
        case 301:
        {
            auto& project = services.daw().getProject();
            for (auto& t : project.tracks) t.armed = (id == 300);
            services.daw().refresh();
            tracksPage->repaint();
            showToast (id == 300 ? "Every track is set to record." : "No tracks are set to record.");
            break;
        }
        case 302:
        case 303:
        case 304:
        {
            const MonitorMode mode = id == 302 ? MonitorMode::Input : id == 303 ? MonitorMode::Auto : MonitorMode::Off;
            auto& project = services.daw().getProject();
            for (auto& t : project.tracks) t.monitor = mode;
            services.daw().refresh();
            tracksPage->repaint();
            showToast (juce::String ("Monitoring: ") + monitorModeName (mode) + " on every track.");
            break;
        }

        case 409:
            openChat();
            break;
        case 410:
            if (liveSafeBlocks ("TUNE LIVE MIX")) break;
            showPage (Page::Tune);
            controller.tryAnotherMix();
            break;
        case 411:
            if (! controller.canUndoMix()) { showToast ("There is no earlier mix to step back to in this session."); break; }
            controller.undoMix(); updateChrome();   // its own toast names what was undone
            lastUndone = UndoDomain::Mix;
            break;
        case 412:
            if (! controller.canRedoMix()) { showToast ("This is the newest mix in this session."); break; }
            controller.redoMix(); updateChrome();
            break;
        case 108: saveInputMapping(); break;
        case 109: showPage (Page::Maps); break;

        case 400:
            if (liveSafeBlocks ("TUNE MIX")) break;
            showPage (Page::Tune);
            mixPage->pressTune();
            break;
        case 405:
            if (liveSafeBlocks ("TUNE LIVE MIX")) break;
            showPage (Page::Tune);
            mixPage->pressLiveTune();
            break;
        case 404:
            if (liveSafeBlocks ("TUNE CHANNEL")) break;
            if (selectedChannel() < 0 && lastChannel < 0) { showToast ("Pick a channel first: click a strip on the mixer or a track header."); break; }
            tuneChannel (selectedChannel() >= 0 ? selectedChannel() : lastChannel);
            break;
        case 407:
            if (liveSafeBlocks ("MATCH TO REFERENCE")) break;
            if (! controller.hasReference()) { showPage (Page::Tune); mixPage->openReference(); break; }
            showPage (Page::Tune);
            controller.startReferenceMatch();
            showToast (controller.hasListened()
                           ? "Aimed at " + juce::String (controller.getReference().name) + ". Compare it with BEFORE, then KEEP or REVERT."
                           : "DINE has not heard the band yet, so it is listening first.");
            break;
        case 305:
        case 306:
            if (tracksPage == nullptr) break;
            if (tracksPage->selectedTrack() < 0) { showToast ("Pick a track first: click its header on TRACKS."); break; }
            showPage (Page::Tracks);
            tracksPage->moveSelectedTrack (id == 305 ? -1 : 1);
            break;
        case 415:
            controller.setAutopilot (! controller.isAutopilotOn());
            updateChrome();
            if (menu != nullptr) menu->menuItemsChanged();
            break;
        case 414: resetMixToRaw(); break;
        case 401: mixPage->centreMacroPads(); showToast ("Both pads and the ENERGY ribbon are back to the plan."); break;
        case 420: showToast (juce::String (controller.raiseLoudnessToTarget())); break;
        case 430: case 431: case 432: case 433: case 434: case 435: case 436:
            controller.setDelivery (DeliveryLoudness (id - 430));
            break;
        case 450: case 451: case 452: case 453: case 454: case 455: case 456: case 457:
            controller.setVoicing (MasterVoicing (id - 450));
            showToast (MasterVoicing (id - 450) == MasterVoicing::Neutral ? juce::String ("Master back to exactly what TUNE MIX built.")
                                                                          : "Master voiced for " + juce::String (masterVoicingName (MasterVoicing (id - 450))).toLowerCase() + ".");
            break;
        case 413: controller.setSpeechPriority (! controller.getSpeechPriority()); break;
        case 416: controller.setAutoMix (! controller.getAutoMix()); break;
        case 402: controller.clearSolos(); showToast ("Solo cleared."); break;
        case 403: setBypass (! controller.isBypassed()); break;
        case 406:
        {
            usingCloudMixEngineer = ! usingCloudMixEngineer;
            if (usingCloudMixEngineer && ! OpenAiMixProvider().isAvailable())
            {
                usingCloudMixEngineer = false;
                showToast ("No API key is configured, so DINE is using its own mix engineer.");
                break;
            }
            if (usingCloudMixEngineer) controller.setReasoningProvider (std::make_shared<OpenAiMixProvider>());
            else controller.setReasoningProvider (nullptr);
            showToast (usingCloudMixEngineer
                           ? "TUNE LIVE MIX will ask the cloud mix engineer. Measurements and source names are sent; no audio ever leaves this machine."
                           : "TUNE LIVE MIX is back on DINE's own mix engineer. Nothing leaves this machine.");
            break;
        }

        // A SERVICE TAKE IS NEVER STOPPED BY ONE STRAY KEY. While recording, Space or R (or the
        // menu item) asks for a second press within three seconds; the on-screen buttons are a
        // deliberate click and act at once.
        case 500:
        case 501:
        {
            if (services.daw().isRecording())
            {
                const auto now = juce::Time::getMillisecondCounter();
                if (stopAskedAt == 0 || now - stopAskedAt > 3000)
                {
                    stopAskedAt = now;
                    showToast ("Recording is running. Press again to stop it.");
                    break;
                }
                stopAskedAt = 0;
            }
            if (id == 500) transportBar->togglePlay(); else transportBar->toggleRecord();
            break;
        }
        case 502: transportBar->returnToStart(); break;
        case 503: transportBar->toggleLoop(); break;

        case 600: showPage (Page::Tracks); break;
        case 601: showPage (Page::Mixer); break;
        case 602: showPage (Page::Tune); break;
        case 603: showPage (Page::Live); break;
        case 604: showPage (Page::Inspector); break;
        case 605: tracksPage->zoom (1.25); break;
        case 606: tracksPage->zoom (0.8); break;
        case 607: tracksPage->zoomToFit(); break;
        case 608: openMixerWindow(); break;
        case 617: openLiveWindow(); break;
        case 618: openInspectorWindow(); break;
        case 609: showOutputs(); break;
        case 630: showCheck(); break;
        case 631: controller.setBroadcastDim (! controller.isBroadcastDimmed()); updateChrome(); break;
        case 632: controller.setBroadcastMute (! controller.isBroadcastMuted()); updateChrome(); break;
        case 660: case 661: case 662:
        {
            const auto& sizes = ThemeStore::textSizes();
            const size_t which = size_t (id - 660);
            if (which < sizes.size()) applyTextSize (sizes[which].scale, juce::String (sizes[which].name));
            break;
        }
        case 620: showThemes(); break;
        case 621: showThemes(); if (themeSheet != nullptr) themeSheet->importTheme(); break;
        case 622: ThemeStore::folder().createDirectory(); ThemeStore::folder().revealToUser(); break;
        case 610: setSidebarShown (! sidebarShown); break;
        case 611: togglePanel (true); break;
        case 612: togglePanel (false); break;
        case 613: showPage (isRoutingPage (page) ? page : Page::Routing); break;
        case 616: showPage (Page::Maps); break;
        case 615:
            if (page == Page::Inspector)
            {
                const bool open = advancedPage->isRailShown() || advancedPage->isTrailShown();
                advancedPage->setRailShown (! open);
                advancedPage->setTrailShown (! open);
            }
            else if (page == Page::Tune) mixPage->setRailShown (! mixPage->isRailShown());
            else setSidebarShown (! sidebarShown);
            break;

        case 614:
        {
            auto& daw = services.daw();
            daw.setLiveSafe (! daw.isLiveSafe());
            livePage->rebuild();
            updateChrome();
            showToast (daw.isLiveSafe()
                           ? juce::String ("LIVE SAFE on. The sound is locked: re-routes and re-tunes are blocked. ") + liveSafe::allowedSummary()
                           : juce::String ("LIVE SAFE off. Re-routes and re-tunes are allowed again."));
            break;
        }

        case 701: showTutorial(); break;
        case 702:
            Guides::reset (guidePreferences());
            maybeShowGuide();
            resized();
            repaint();
            showToast ("Every workspace will explain itself once more.");
            break;
        case 703:
            if (auto* t = Telemetry::instance())
            {
                t->setSharing (! t->isSharing());
                showToast (t->isSharing() ? "DINE will share anonymous usage data: which features are used and what went "
                                            "wrong. Never audio, names, files or anything you type."
                                          : "Nothing more will be shared. Milestones still work; they live on this Mac.");
            }
            break;
        case 700:
            showToast ("DINE - the live recording and broadcast DAW. Connect. Record. Mix. Tune. Broadcast.");
            break;
        default: break;
    }
}

// The shortcut table, as data. Nothing here runs a command or touches the window, so it can
// be read and asserted (app/Tests/ReachabilityTests.cpp) without a file chooser opening.
int MainView::commandForKey (const juce::KeyPress& key, Page page)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();

    if (mods.isCommandDown())
    {
        if (code == 'S' && mods.isCtrlDown()) return 610;
        if (code == 'S' && ! mods.isShiftDown()) return 102;
        if (code == 'S') return 103;
        // UNDO and REDO, the way every Mac app has them. Cmd+Shift+Z used to be undo as well.
        if (code == 'Z') return mods.isShiftDown() ? 204 : 200;
        if (code == 'E') return 201;
        if (code == 'O') return 101;
        if (code == 'N') return 100;
        if (code == '=' || code == '+') return 605;
        if (code == '-') return 606;
        if (code == '0') return 607;
        // Cmd-1..5 follow the tab bar left to right: TRACKS MIXER TUNE LIVE INSPECTOR.
        if (code >= '1' && code <= '5')
        {
            static constexpr int kTabCommand[5] = { 600, 601, 602, 603, 604 };
            return kTabCommand[code - '1'];
        }
        return 0;
    }

    if (code == juce::KeyPress::spaceKey)  return 500;
    if (code == juce::KeyPress::returnKey) return 502;
    if (code == 'R') return 501;
    if (code == 'L') return 503;
    if (code == 'B') return 403;
    if (code == 'T') return 404;
    if (code == 'M') return 203;
    if (code == '[') return 611;
    if (code == ']') return 612;
    // A clip is only deleted where there are clips.
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        return page == Page::Tracks ? 202 : 0;
    return 0;
}

juce::String MainView::openSheetName() const
{
    // The scope picker belongs to TUNE rather than to the window, but it is a sheet over the
    // workspace like any other and Escape has to mean the same thing over it.
    if (mixPage != nullptr && mixPage->isScopeSheetOpen()) return "tunescope";
    if (checkSheet   != nullptr) return "check";
    if (historySheet != nullptr) return "history";
    if (readinessSheet != nullptr) return "readiness";
    if (themeSheet   != nullptr) return "appearance";
    if (channelSheet != nullptr) return "channel";
    if (chatSheet    != nullptr) return "chat";
    if (exportSheet  != nullptr) return "export";
    if (choiceSheet  != nullptr) return "choice";
    return {};
}

bool MainView::keyPressed (const juce::KeyPress& key)
{
    // Escape belongs to whatever is open over the workspace, so it is not in the table.
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        if (openSheetName().isNotEmpty()) { closeSheets(); return true; }
        return false;
    }

    if (const int command = commandForKey (key, page); command != 0)
    {
        handleCommand (command);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- session document
void MainView::newSession()
{
    if (liveSafeBlocks ("starting a new session")) return;
    if (services.daw().isRecording())
    {
        showToast ("Recording is running. Stop recording first - a new session would close the one it is recording into.");
        return;
    }
    if (! services.saveSession())
    {
        showToast ("\"" + services.currentSessionName() + "\" could not be saved, so it is still open. Check the disk, then try again.");
        return;
    }
    services.newSession();
    assignPage->refresh();
    tracksPage->rebuild();
    mixerPage->rebuild();
    rebuildWindows();
    advancedPage->rebuild();
    showPage (Page::Device);
    showToast ("New session.");
}

void MainView::sessionMenu()
{
    setupPopover();
}

void MainView::importMultitrack (bool newSessionFirst)
{
    if (liveSafeBlocks ("importing")) return;
    if (services.daw().isRecording())
    {
        showToast ("Recording is running. Stop recording first.");
        return;
    }
    // Files, a folder, or several of either - the way a DAW's import takes them.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    chooser = std::make_unique<juce::FileChooser> ("Choose audio files, or a folder of recorded stems",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   formats.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, newSessionFirst] (const juce::FileChooser& fc)
                          {
                              const auto chosen = fc.getResults();
                              if (chosen.isEmpty()) return;
                              importAudio (chosen, newSessionFirst);
                          });
}

void MainView::importMultitrackFolder (const juce::File& folder)
{
    importAudio ({ folder }, false);
}

// Everything that imports lands here. It ADDS to the open session: a track already set up but
// still empty takes the file with its name, the rest become new tracks, and nothing that was
// recorded is thrown away. From the launcher it means "a new session from these", so the one
// that was open is saved and put away first - if it had anything in it.
void MainView::importAudio (const juce::Array<juce::File>& chosen, bool newSessionFirst)
{
    if (liveSafeBlocks ("importing")) return;
    if (services.daw().isRecording())
    {
        showToast ("Recording is running. Stop recording first.");
        return;
    }
    if (newSessionFirst && services.daw().getProject().hasAudio())
    {
        if (! services.saveSession())
        {
            showToast ("\"" + services.currentSessionName() + "\" could not be saved, so it is still open. Check the disk, then try again.");
            return;
        }
        services.newSession();
    }
    const bool inSetup = page == Page::Device || page == Page::Sessions || page == Page::Assign;
    const auto outcome = services.importAudio (chosen);
    if (outcome.error.isNotEmpty()) { showToast (outcome.error); return; }
    assignPage->refresh();
    tracksPage->rebuild();
    mixerPage->rebuild();
    rebuildWindows();
    advancedPage->rebuild();
    updateChrome();
    showToast (outcome.summary);
    // In the setup the inputs are the next thing to look at; anywhere else, the tracks that came in.
    showPage (inSetup ? Page::Assign : Page::Tracks);
}

// EXPORT: the sheet asks what, how much and where, then the render happens on a worker while
// the console keeps playing. Every menu item opens the same sheet with its own choice already
// picked, so File > Export Stereo Mix (MP3) and Export Multitrack each mean what they say.
void MainView::exportMix (AppServices::ExportFormat format, AppServices::ExportWhat what)
{
    if (! controller.isPrepared() || controller.getSession().inputs.empty())
    {
        showToast ("Finish setup and assign inputs before exporting.");
        return;
    }
    if (! services.daw().getProject().hasAudio())
    {
        showToast ("There is nothing recorded yet. Record a take, or import a multitrack folder.");
        return;
    }
    if (isExporting()) { showToast ("An export is already running - the status bar says how far it is."); return; }

    closeSheets();
    exportSheet = std::make_unique<ExportSheet> (controller, services);
    exportSheet->onClose = [this] { exportSheet.reset(); resized(); repaint(); grabKeyboardFocus(); };
    exportSheet->onToast = [this] (const juce::String& t) { showToast (t); };
    exportSheet->onExport = [this] (const ExportSheet::Request& req)
    {
        // One at a time: two renders would read the disk against each other and the status foot
        // has one place to say how far an export is.
        if (isExporting()) { showToast ("An export is already running - the status bar says how far it is."); return; }
        const auto dest = req.dest;
        dest.getParentDirectory().createDirectory();
        auto job = services.snapshotExport();
        if (job != nullptr)
        {
            auto asked = std::make_shared<AppServices::ExportJob> (*job);
            asked->from = req.from;
            asked->to = req.to;
            asked->what = req.what;
            asked->loudness = req.loudness;
            job = asked;
        }
        // A folder of parts says so while it is being made, and says so again when it is done:
        // "Exported Sunday.wav" is not the truth about sixteen files.
        const bool folder = req.what != AppServices::ExportWhat::StereoMix;
        const juce::String what = folder ? (req.what == AppServices::ExportWhat::GroupStems ? "the group stems"
                                                                                           : "the raw multitrack")
                                         : dest.getFileName();
        exportWhat = req.what == AppServices::ExportWhat::GroupStems ? MixBounce::What::GroupStems
                   : req.what == AppServices::ExportWhat::RawMultitrack ? MixBounce::What::RawMultitrack
                                                                        : MixBounce::What::StereoMix;
        // Where the result will be, for "Show in Finder": the folder of parts, or the file.
        exportShown = folder ? dest.getParentDirectory().getChildFile (dest.getFileNameWithoutExtension()
                                    + (req.what == AppServices::ExportWhat::GroupStems ? " stems" : " multitrack"))
                             : req.format == AppServices::ExportFormat::Mp3 ? dest.withFileExtension ("mp3")
                                                                            : dest;
        exportError = {};
        exportDoneTicks = 0;
        auto run = std::make_shared<ExportProgress>();
        run->state.store (int (ExportProgress::State::Running));
        run->workerBusy.store (true);
        exportRun = run;
        showToast ("Exporting " + what + Glyph::ellip() + " The status bar shows how far it is.");
        juce::Component::SafePointer<MainView> safe (this);
        auto& srv = services;
        const auto fmt = req.format;
        juce::Thread::launch ([safe, &srv, job, dest, fmt, what, folder, run]
        {
            const auto err = srv.exportMix (job, dest, fmt, *run);
            // Only now - every writer closed, the encoder finished, the file moved into place -
            // is it done. The state says so before the worker lets go of `srv`.
            run->state.store (int (err.isEmpty() ? ExportProgress::State::Done
                                 : run->cancel.load() ? ExportProgress::State::Cancelled
                                                      : ExportProgress::State::Failed));
            run->workerBusy.store (false);
            juce::MessageManager::callAsync ([safe, err, what, folder, run]
            {
                if (safe == nullptr || safe->exportRun != run) return;
                safe->exportError = run->getState() == ExportProgress::State::Failed ? err : juce::String();
                safe->exportDoneTicks = 30 * 12;
                safe->showToast (run->getState() == ExportProgress::State::Cancelled ? juce::String ("The export was stopped. Nothing half-written was kept.")
                               : err.isNotEmpty() ? err + " Click Export failed in the status bar to read this again."
                               : folder ? "Exported " + what + " into a folder beside the session."
                                        : "Exported " + what + ".");
            });
        });
    };
    exportSheet->choose (what, format);
    addAndMakeVisible (*exportSheet);
    resized();
    exportSheet->grabKeyboardFocus();
}

// ---------------------------------------------------------------- drum kit
// Read off the strips every time it is asked: the kit is never stored, so a hand-picked snare,
// an undo or an old session can never disagree with what the picker says (DrumKits.h).
juce::String MainView::drumKitName() const
{
    const auto* library = services.sampleLibrary();
    if (library == nullptr) return {};
    std::array<SampleChoice, kMaxStrips> now {};
    readSampleChoices (controller, *library, now);
    return juce::String (currentDrumKit (now, controller));
}

void MainView::drumKitMenu (juce::Component& anchor)
{
    const auto* library = services.sampleLibrary();
    if (library == nullptr) return;
    const auto current = drumKitName();
    juce::PopupMenu m;
    m.addSectionHeader ("Drum kit: the kick, the snare and the toms together");
    const auto& kits = builtInDrumKits();
    for (size_t i = 0; i < kits.size(); ++i)
        m.addItem (int (i) + 1, juce::String (kits[i].name) + "   " + Glyph::dash() + "   " + juce::String (kits[i].sentence),
                   true, current == juce::String (kits[i].name));
    m.addSeparator();
    m.addItem (-1, "Custom   " + juce::String (Glyph::dash()) + "   each drum has its own sound", false, current == "Custom");
    juce::Component::SafePointer<MainView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        const auto& all = builtInDrumKits();
        if (r > int (all.size())) return;
        // Every drum's sound at once is a change to what the room hears, so LIVE SAFE holds it.
        if (safe->liveSafeBlocks ("changing the drum kit")) return;
        const auto* lib = safe->services.sampleLibrary();
        if (lib == nullptr) return;
        safe->showToast (juce::String (applyDrumKit (all[size_t (r - 1)], *lib, safe->controller)));
        safe->updateChrome();
    });
}

// ---------------------------------------------------------------- undo
// TWO DOMAINS, NEVER ONE SNAPSHOT (2026-10-05). The mix history (MixController: faders, chains,
// sends, TUNE, scenes, RESET TO RAW - each one entry) and the timeline's edits (TracksPage:
// clips and markers, in the timeline epoch they were made in). Cmd+Z on TRACKS takes back
// whichever of the two was touched last; everywhere else it is the mix, because the mix is
// what those workspaces edit and a timeline change there would be invisible. Devices,
// permissions, recording, autosave and DIM / MUTE are in neither, so no undo can touch them.
MainView::UndoStep MainView::undoTarget() const
{
    const bool mixUndo = controller.canUndoMix();
    const bool timelineUndo = page == Page::Tracks && tracksPage != nullptr && ! controller.isLiveSafe() && tracksPage->canUndo();
    if (timelineUndo && (! mixUndo || tracksPage->lastEditMs() >= controller.undoMixAtMs()))
        return { UndoDomain::Timeline, tracksPage->undoLabel() };
    if (mixUndo) return { UndoDomain::Mix, juce::String (controller.undoMixLabel()) };
    return {};
}

MainView::UndoStep MainView::redoTarget() const
{
    const bool timelineRedo = page == Page::Tracks && tracksPage != nullptr && ! controller.isLiveSafe() && tracksPage->canRedo();
    const bool mixRedo = controller.canRedoMix();
    // Redo follows the undo it reverses.
    if (timelineRedo && (lastUndone == UndoDomain::Timeline || ! mixRedo)) return { UndoDomain::Timeline, tracksPage->redoLabel() };
    if (mixRedo) return { UndoDomain::Mix, juce::String (controller.redoMixLabel()) };
    return {};
}

void MainView::undoHere()
{
    const auto step = undoTarget();
    if (step.domain == UndoDomain::Timeline) { tracksPage->undo(); lastUndone = UndoDomain::Timeline; return; }
    if (step.domain == UndoDomain::Mix) { controller.undoMix(); updateChrome(); lastUndone = UndoDomain::Mix; return; }
    showToast (page == Page::Tracks && controller.isLiveSafe() && tracksPage->canUndo()
                   ? "LIVE SAFE is on: the timeline is locked, and there is no mix change to undo."
                   : "There is nothing to undo here yet.");
}

void MainView::redoHere()
{
    const auto step = redoTarget();
    if (step.domain == UndoDomain::Timeline) { tracksPage->redo(); return; }
    if (step.domain == UndoDomain::Mix) { controller.redoMix(); updateChrome(); return; }
    showToast ("There is nothing to redo.");
}

// The status foot's Export cell. Working: stop it. Done: show it. Failed: say why again.
void MainView::exportCellClicked()
{
    if (exportRun == nullptr) return;
    const auto st = exportRun->getState();
    if (st == ExportProgress::State::Running)
    {
        juce::PopupMenu m;
        m.addItem (1, "Stop the export");
        auto run = exportRun;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (statusBar.get()),
                         [run] (int r) { if (r == 1) run->cancel.store (true); });
    }
    else if (st == ExportProgress::State::Done)
    {
        if (exportShown.exists()) exportShown.revealToUser();
    }
    else if (st == ExportProgress::State::Failed)
    {
        showToast (exportError.isNotEmpty() ? exportError : juce::String ("The export did not finish."));
        exportRun.reset();
    }
    else exportRun.reset();
}

bool MainView::stopExportAndWait (int timeoutMs)
{
    if (! isExporting()) return true;
    exportRun->cancel.store (true);
    // The worker checks once a block, and the encoder every tenth of a second.
    for (int waited = 0; waited < timeoutMs && exportRun->workerBusy.load(); waited += 20)
        juce::Thread::sleep (20);
    return ! exportRun->workerBusy.load();
}

void MainView::showExportProgressForSnapshot (ExportProgress::State st, MixBounce::Stage stage, float fraction)
{
    if (st == ExportProgress::State::Idle) { exportRun.reset(); timerCallback(); return; }
    exportRun = std::make_shared<ExportProgress>();
    exportRun->state.store (int (st));
    exportRun->stage.store (int (stage));
    exportRun->fraction.store (fraction);
    exportDoneTicks = 0;
    timerCallback();
}

void MainView::saveNow()
{
    if (services.currentSessionName().isEmpty()) { saveAs(); return; }
    showToast (services.saveSession() ? juce::String ("Session saved.")
                                      : "\"" + services.currentSessionName() + "\" could not be saved. Check the disk - "
                                        "the autosave still holds this work.");
}

void MainView::saveAs()
{
    auto* alert = new juce::AlertWindow ("Save session",
                                         "Name this session. It becomes a folder on this Mac, with its recordings inside.",
                                         juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor ("name", services.currentSessionName().isNotEmpty() ? services.currentSessionName() : "Sunday", "Name");
    alert->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, alert] (int r)
    {
        std::unique_ptr<juce::AlertWindow> closer (alert);
        if (r != 1) return;
        const auto err = services.saveSessionAs (alert->getTextEditorContents ("name"));
        if (err.isNotEmpty()) showToast (err);
        else { showToast ("Saved \"" + services.currentSessionName() + "\"."); updateChrome(); }
    }), true);
}

// A different document is open: every workspace was built for the last one. Called by opening
// a session from the library and by recovering one after a crash, so the two cannot drift.
void MainView::sessionReplaced()
{
    seenRevision = services.sessionRevision();
    seenMilestone = services.sessionMilestone();
    advancedPage->rebuild();
    mixerPage->rebuild();
    rebuildWindows();
    tracksPage->rebuild();
    livePage->rebuild();
    showPage (controller.getSession().inputs.empty() ? Page::Assign : Page::Tracks);
    updateChrome();
}

void MainView::openSession()
{
    juce::PopupMenu m;
    const auto listed = services.listSessions();
    if (listed.isEmpty())
    {
        showToast ("No saved sessions yet. Save one from the session menu.");
        return;
    }
    for (int i = 0; i < listed.size(); ++i)
        m.addItem (i + 1, listed[i].name + "   " + listed[i].modified.formatted ("%d %b"));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sidebarButton.get()).withMinimumWidth (280),
                     [this, listed] (int result)
                     {
                         if (result <= 0 || result > listed.size()) return;
                         const auto err = services.loadSession (listed[result - 1].file);
                         if (err.isNotEmpty()) { showToast (err); return; }
                         sessionReplaced();
                         const auto recovered = services.takeRecoveryNote();
                         showToast ("Opened \"" + services.currentSessionName() + "\"." + (recovered.isEmpty() ? "" : " " + recovered));
                     });
}

// The toolbar's output pill (v4): where the broadcast goes, where solo goes, and the way to
// the feeds. The broadcast devices are a submenu; solo is the one shared menu the Outputs
// section and LIVE use (OutputsSheet::showSoloDeviceMenu), so the three can never disagree.
void MainView::chooseOutput()
{
    const auto outs = services.outputDevices();
    if (outs.isEmpty()) { showToast ("No output devices found."); return; }
    const juce::String current = services.broadcastOutputDevice();
    juce::PopupMenu devices;
    for (int i = 0; i < outs.size(); ++i)
    {
        if (outs[i].name.startsWith ("DINE Monitoring")) continue;
        devices.addItem (i + 1, outs[i].name, true, outs[i].name == current);
    }

    juce::PopupMenu m;
    m.addSectionHeader ("Outputs");
    m.addSubMenu ("Broadcast " + Glyph::dot() + " " + (current.isNotEmpty() ? current : juce::String ("not chosen")), devices);
    m.addSeparator();
    m.addItem (901, "Solo " + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")) + " "
                        + OutputsSheet::soloChoiceLabel (controller, services, "Nowhere") + Glyph::ellip(),
               services.isAudioRunning());
    m.addSeparator();
    m.addItem (900, "Set up outputs" + Glyph::ellip());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (outputButton.get()).withMinimumWidth (outputButton->getWidth()),
                     [this, outs] (int result)
                     {
                         if (result == 900) { showOutputs(); return; }
                         if (result == 901)
                         {
                             OutputsSheet::showSoloDeviceMenu (controller, services, *outputButton,
                                                               [this] (const juce::String& said) { if (said.isNotEmpty()) showToast (said); updateChrome(); });
                             return;
                         }
                         if (result <= 0 || result > outs.size()) return;
                         const auto& name = outs[result - 1].name;
                         if (name == services.broadcastOutputDevice()) return;
                         if (liveSafeBlocks ("changing the output device")) return;
                         const auto err = services.isAudioRunning() ? services.changeOutput (name)
                                                                    : services.openOutputOnly (name);
                         if (err.isNotEmpty()) showToast (err);
                         else
                         {
                             controller.flagReadiness (ReadinessChange::BroadcastDevice);
                             showToast ("Broadcast: " + name);
                             updateChrome();
                         }
                     });
}

// The inputs whose gain the desk should still move: the same verdict Check inputs, the
// Inputs page and TUNE give (MixController::InputAdvice), counted.
int MainView::inputsNeedingAttention() const
{
    const int n = controller.getBase().numStrips;
    int count = 0;
    for (int i = 0; i < n; ++i)
        if (controller.getInputAdvice (i).needsAttention()) ++count;
    return count;
}

// The readiness pill. v4 draws a "Ready to go live?" sheet of its own - device, inputs,
// recording, disk, on air, loudness, BYPASS, LIVE SAFE, autosave - and nothing aggregates those
// yet, so the pill opens what exists: the broadcast checklist where the purpose has one, and
// Check inputs otherwise.
// TODO(v4-backend): a Ready model that answers every row of the v4 sheet (docs/design/v4/GAPS.md).
void MainView::readyPillClicked()
{
    if (broadcastReadinessApplies (controller.getSession().purpose)) showBroadcastReadiness (true);
    else showCheck();
}

// ---------------------------------------------------------------- ticking
void MainView::timerCallback()
{
    controller.poll();
    transportBar->refresh();

    if (page == Page::Tracks) tracksPage->refresh();
    else if (page == Page::Mixer) mixerPage->refresh();
    // The patch is where the preamps are set, so its meters and its gain-staging verdicts move
    // while the band plays - it is the one set-up page with live numbers on it.
    else if (page == Page::Assign && assignPage->isVisible()) assignPage->tick();
    else if (page == Page::Tune) mixPage->refresh();
    else if (page == Page::Live) livePage->refresh();
    else if (page == Page::Inspector) advancedPage->refresh();

    if (channelSheet != nullptr) channelSheet->refresh();
    if (checkSheet != nullptr) checkSheet->refresh();
    if (readinessSheet != nullptr) readinessSheet->refresh();
    if (chatSheet != nullptr) chatSheet->refresh();

    const bool slow = (++slowTicks % 30) == 0;
    statusBar->takeStopped = transportBar->takeStoppedByItself();
    // Once, a few seconds in, on the first run that could share anything: what is sent and how
    // to stop it. Nothing is sent before this has been said (Telemetry::needsNotice).
    if (slowTicks == 90)
        if (auto* t = Telemetry::instance(); t != nullptr && t->needsNotice())
        {
            showToast ("DINE shares anonymous usage and crash reports - which features are used and what went wrong. "
                       "Never audio, names, files or anything you type. Help > Share anonymous usage data turns it off.");
            t->markNoticeShown();
        }
    if (exportRun != nullptr)
    {
        const auto st = exportRun->getState();
        statusBar->exportText = exportStatusText (*exportRun, exportWhat);
        statusBar->exportTint = st == ExportProgress::State::Failed ? Dine::crit
                              : st == ExportProgress::State::Done ? Dine::accent
                              : st == ExportProgress::State::Running ? Dine::ink : Dine::ink3;
        statusBar->exportFraction = exportRun->fraction.load();
        statusBar->exportWorking = st == ExportProgress::State::Running;
        // Done and stopped go by themselves; a failure stays until somebody has read it.
        if (st == ExportProgress::State::Done || st == ExportProgress::State::Cancelled)
            if (exportDoneTicks > 0 && --exportDoneTicks == 0) exportRun.reset();
    }
    if (exportRun == nullptr) { statusBar->exportText = {}; statusBar->exportWorking = false; }
    statusBar->update (slow);
    {
        const bool failing = services.autosaveFailing();
        if (failing && ! saidAutosaveFailing)
            showToast ("The autosave could not be written. Check the disk - until it lands, only what is saved is safe.");
        saidAutosaveFailing = failing;
    }
    if (slow || slowTicks % 10 == 0) sidebar->refresh (services.daw().isRecording());
    if (slow) followMicrophone();
    if (chainFoot->isVisible() && slowTicks % 3 == 0) updateChainFoot();
    if (slowTicks % 3 == 0) refreshSoloPill();

    if (toastTicks > 0 && --toastTicks == 0) toast->setVisible (false);

    // What is written down follows the document's revision. A milestone - a tune kept, a scene
    // recalled - does not wait for the quiet; everything else is handed over a third of a
    // second after the last change. docs/SESSION-STATE.md §5.3.
    if (const auto revision = services.sessionRevision(); revision != seenRevision)
    {
        seenRevision = revision;
        if (const auto milestone = services.sessionMilestone(); milestone != seenMilestone)
        {
            seenMilestone = milestone;
            saveTicks = 0;
            services.autosaveNow (true);
        }
        else saveTicks = 10;          // a third of a second of quiet, then hand it over
    }
    if (saveTicks > 0 && --saveTicks == 0) services.autosaveNow (false);

    if (tuningLiveWasOn != controller.isTuningLive()) { tuningLiveWasOn = controller.isTuningLive(); updateChrome(); }

    const bool running = services.isAudioRunning();
    if (audioWasRunning != running) updateChrome();
    if (audioWasRunning && ! running && services.deviceStopped())
        showToast ("The audio device stopped. Check its connection, then choose it again under Audio device.");
    audioWasRunning = running;

    // (v3 pulsed a strip along the top of the window while the mix was live. v4 has none, and
    // it repainted the window's width every tick while nothing else moved; the status foot's
    // "On air" cell says the same.)
}

// ---------------------------------------------------------------- layout
// v4's frame: an 8 pt margin all round; the sidebar card (214) in it at the left; the toolbar
// (60) over the column beside it; the workspace card; the chain card 6 under it; the status
// foot (30) along the bottom. While the sidebar slides, the column's left edge goes with it.
int MainView::columnLeft() const noexcept
{
    return juce::roundToInt (8.0f + float (Dine::Metric::sidebar - 8) * sidebarReveal);
}

juce::Rectangle<int> MainView::columnBounds() const
{
    return getLocalBounds().withTrimmedTop (Dine::Metric::toolbar).withLeft (columnLeft()).withTrimmedRight (8);
}

// The solo pill lives in the toolbar now, so a sheet can never cover it: it is the one thing
// the window says that has to stay true whatever else is open.
bool MainView::isSoloBarShown() const { return soloPill != nullptr && soloPill->isVisible(); }

// A name on the solo band is a way to the thing it names: the console, with that strip, group
// or return picked out. Nothing about the mix changes - it is a way of looking, like the band.
void MainView::jumpToSoloed (const MixController::SoloedItem& item)
{
    showPage (Page::Mixer);
    if (mixerPage == nullptr) return;
    using Kind = MixController::SoloedItem::Kind;
    if (item.kind == Kind::Strip) mixerPage->selectStrip (item.index);
    else if (item.kind == Kind::Bus) mixerPage->selectBus (MixBus (item.index));
    else showToast (juce::String (item.name) + " is soloed. The returns live on the Inspector and on LIVE.");
}

juce::Rectangle<int> MainView::contentBounds() const
{
    auto r = columnBounds().withTrimmedBottom (Dine::Metric::status);
    if (chainFoot != nullptr && chainFoot->isVisible()) r.removeFromBottom (Dine::Metric::chainFoot);
    else r.removeFromBottom (6);
    // BYPASS: the banner under the toolbar takes the top of the column.
    if (controller.isBypassed()) r.removeFromTop (kBypassBanner);
    return r;
}

// The workspace card: the content, less the Mix Buddy panel when it is open beside it.
juce::Rectangle<int> MainView::workspaceCard() const
{
    auto r = contentBounds();
    if (chatSheet != nullptr) r.removeFromRight (juce::jmin (kRequestsW, r.getWidth() / 2) + 6);
    return r;
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk);

    // The workspace card, for the moment no page covers it.
    const auto card = workspaceCard().toFloat();
    Dine::fillRounded (g, card, Dine::window, 14.0f);

    // BYPASS: a white bar under the toolbar, said in a sentence - the one state where what is
    // heard is not the mix, so it is the loudest thing on the screen while it lasts.
    if (controller.isBypassed())
    {
        auto banner = columnBounds().removeFromTop (kBypassBanner).withTrimmedBottom (8).withTrimmedLeft (6).withTrimmedRight (-2).toFloat();
        Dine::fillRounded (g, banner, Dine::ink, 10.0f);
        auto text = banner.toNearestInt().reduced (14, 0);
        const auto bold = Dine::caps (12.0f, 0.02f, 700);
        g.setColour (Dine::desk);
        g.setFont (bold);
        const int w = Dine::textWidth (bold, "BYPASS");
        Dine::drawText (g, "BYPASS", text.removeFromLeft (w), juce::Justification::centredLeft);
        text.removeFromLeft (10);
        g.setFont (Dine::text (12.5f));
        Dine::drawFittedText (g, "You're hearing the raw console feed. The kept mix is untouched and the faders are locked until you turn it off.",
                              text, juce::Justification::centredLeft, 1, 0.8f);
    }

    // The broadcast keys share one pill, and a hairline divides them from TUNE LIVE MIX.
    if (! broadcastPill.isEmpty())
    {
        const auto r = broadcastPill.toFloat();
        Dine::fillRounded (g, r, juce::Colours::white.withAlpha (0.06f), r.getHeight() * 0.5f);
        Dine::hairlineRounded (g, r.reduced (0.5f), juce::Colours::white.withAlpha (0.10f), r.getHeight() * 0.5f - 0.5f);
    }
    if (dividerX > 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.fillRect (juce::Rectangle<int> (dividerX, (Dine::Metric::toolbar - 22) / 2, 1, 22));
    }
}

// The workspace card's corners. The pages are opaque rectangles, so the card's 14 pt corners
// are cut back out of them here, with the hairline round the edge - but only where a repaint
// actually touches an edge, so a meter in the middle of a page costs nothing extra.
void MainView::paintOverChildren (juce::Graphics& g)
{
    const auto card = workspaceCard();
    if (card.isEmpty() || ! g.clipRegionIntersects (card)) return;
    const auto clip = g.getClipBounds();
    if (card.reduced (16).contains (clip)) return;

    constexpr float radius = 14.0f;
    const auto cardF = card.toFloat();
    juce::Path outside;
    outside.addRectangle (cardF.expanded (1.0f));
    outside.addRoundedRectangle (cardF, radius);
    outside.setUsingNonZeroWinding (false);
    g.setColour (Dine::desk);
    g.fillPath (outside);
    Dine::hairlineRounded (g, cardF.reduced (0.5f), juce::Colours::white.withAlpha (0.06f), radius - 0.5f);
}

void MainView::mouseDown (const juce::MouseEvent& e)
{
    // The second press of a double-click is the zoom, not the start of a drag.
    if (e.y < Dine::Metric::toolbar && e.getNumberOfClicks() == 1 && onToolbarPressed) onToolbarPressed();
}

void MainView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < Dine::Metric::toolbar && onToolbarDoubleClicked) onToolbarDoubleClicked();
}

void MainView::resized()
{
    constexpr int bar = Dine::Metric::toolbar;
    const float shown = sidebarReveal;
    const auto lerp = [shown] (float hidden, float out) { return juce::roundToInt (hidden + (out - hidden) * shown); };

    // ------------------------------------------------------------------ the sidebar card
    {
        const int w = juce::roundToInt (float (Sidebar::kCardW) * shown);
        sidebar->setBounds (juce::roundToInt (8.0f * shown), 8, w, juce::jmax (0, getHeight() - 16));
    }

    // ------------------------------------------------------------------ the toolbar, left
    // The window's own buttons are drawn by macOS at 25 / 45 / 65 whatever the sidebar does.
    // The sidebar switch sits at the card's top right while it is out, and becomes a round
    // button just past the window's buttons while it is hidden.
    sidebarButton->setRound (1.0f - shown);
    {
        const int size = lerp (34.0f, 28.0f);
        sidebarButton->setBounds (lerp (85.0f, 186.0f), (bar - size) / 2 + lerp (0.0f, 2.0f), size, size);
    }

    auto right = juce::Rectangle<int> (0, 0, getWidth() - 8, bar);
    int leftX = lerp (85.0f + 34.0f + 12.0f, float (Dine::Metric::sidebar + 4));
    // The transport's keys and the readiness pill are never given away to the right-hand
    // cluster: a narrow window loses the output pill's width, then the broadcast keys, first.
    const int floorX = leftX + (transportBar->isVisible() ? transportBar->keysOnlyWidth() + 10 : 0)
                     + (readyPill->isVisible() ? readyPill->idealWidth() + 8 : 0);
    auto place = [&right, floorX] (juce::Component& c, int w, int h, int gapAfter)
    {
        if (! c.isVisible()) return;
        if (w <= 0 || right.getRight() - w < floorX) { c.setBounds ({}); return; }
        c.setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, h));
        right.removeFromRight (gapAfter);
    };

    // ------------------------------------------------------------------ the toolbar, right
    place (*chatButton, chatButton->idealWidth(), 34, 6);
    if (outputButton->isVisible())
    {
        const int spare = right.getRight() - floorX - 700;   // what the rest of the right cluster leaves it
        const int w = juce::jlimit (110, 240, juce::jmin (outputButton->idealWidth(), juce::jmax (110, spare)));
        place (*outputButton, w, 34, 6);
    }
    place (*liveSafeButton, liveSafeButton->idealWidth(), 34, 6);
    // DIM MUTE BYPASS Auto share one pill, left to right, so they are laid out backwards. They
    // go together or not at all: three of the four is a row with a hole in it.
    broadcastPill = {};
    {
        int need = 8;
        int keys = 0;
        for (auto* b : { autopilotButton.get(), bypassButton.get(), muteButton.get(), dimButton.get() })
            if (b->isVisible()) { need += b->idealWidth() + 2; ++keys; }
        const bool room = keys > 0 && right.getRight() - need - 8 >= floorX;
        if (room)
        {
            auto pill = right.removeFromRight (need).withSizeKeepingCentre (need, 34);
            broadcastPill = pill;
            right.removeFromRight (6);
            auto inner = pill.reduced (4, 3);
            for (auto* b : { dimButton.get(), muteButton.get(), bypassButton.get(), autopilotButton.get() })
            {
                if (! b->isVisible()) continue;
                b->setBounds (inner.removeFromLeft (b->idealWidth()));
                inner.removeFromLeft (2);
            }
        }
        else for (auto* b : { autopilotButton.get(), bypassButton.get(), muteButton.get(), dimButton.get() }) b->setBounds ({});
    }
    dividerX = ! broadcastPill.isEmpty() ? right.getRight() - 2 : 0;
    if (dividerX > 0) right.removeFromRight (6);
    place (*tuneLiveButton, tuneLiveButton->idealWidth(), 34, 0);
    if (tuneLiveButton->getWidth() == 0) dividerX = 0;

    // ------------------------------------------------------------------ the toolbar, middle
    // The session, the transport and its clock, the readiness pill, the solo pill - from the
    // left, giving way rather than running under the right-hand cluster.
    auto left = juce::Rectangle<int> (leftX, 0, juce::jmax (0, right.getRight() - 12 - leftX), bar);
    {
        // Whole or not at all: the name is the session menu's handle, and half a name is a
        // button that reads as broken. The transport's keys come first; the menu bar and the
        // window's title still name the session when there is no room for it here.
        const int w = sessionButton->idealWidth();
        const int need = w + 8 + (transportBar->isVisible() ? transportBar->keysOnlyWidth() + 10 : 0)
                       + (readyPill->isVisible() ? readyPill->idealWidth() : 0);
        if (left.getWidth() >= need)
        {
            sessionButton->setBounds (left.removeFromLeft (w).withSizeKeepingCentre (w, 40));
            left.removeFromLeft (8);
        }
        else sessionButton->setBounds ({});
    }
    const int pillsNeed = (readyPill->isVisible() ? readyPill->idealWidth() + 10 : 0)
                        + (soloPill != nullptr && soloPill->isVisible() ? juce::jmin (soloPill->idealWidth(), 150) + 10 : 0);
    if (transportBar->isVisible())
    {
        int want = transportBar->idealWidth();
        if (left.getWidth() - pillsNeed < want) want = transportBar->keysOnlyWidth();
        if (left.getWidth() < transportBar->keysOnlyWidth()) transportBar->setBounds ({});
        else
        {
            const int w = juce::jmin (want, left.getWidth());
            transportBar->setBounds (left.removeFromLeft (w).withSizeKeepingCentre (w, TransportBar::height));
            left.removeFromLeft (10);
        }
    }
    if (readyPill->isVisible())
    {
        const int w = juce::jmin (readyPill->idealWidth(), left.getWidth());
        readyPill->setBounds (left.removeFromLeft (w).withSizeKeepingCentre (w, 30));
        left.removeFromLeft (8);
    }
    if (soloPill != nullptr && soloPill->isVisible())
    {
        const int w = juce::jlimit (0, juce::jmax (0, left.getWidth()), juce::jmin (soloPill->idealWidth(), 280));
        soloPill->setBounds (left.removeFromLeft (w).withSizeKeepingCentre (w, 30));
    }

    // ------------------------------------------------------------------ the column
    const auto column = columnBounds();
    statusBar->setBounds (column.withTop (getHeight() - Dine::Metric::status).withRight (getWidth()));
    if (chainFoot->isVisible())
        chainFoot->setBounds (column.withTrimmedBottom (Dine::Metric::status)
                                    .removeFromBottom (Dine::Metric::chainFoot).withTrimmedTop (Dine::Metric::chainFoot - ChainStrip::height));

    const auto content = workspaceCard();
    const int panelW = chatSheet != nullptr ? juce::jmin (kRequestsW, contentBounds().getWidth() / 2) : 0;
    for (juce::Component* p : { (juce::Component*) sessionsPage.get(), (juce::Component*) favouritesPage.get(),
                                (juce::Component*) purposePage.get(), (juce::Component*) tracksPage.get(),
                                (juce::Component*) mixerPage.get(), (juce::Component*) mixPage.get(),
                                (juce::Component*) livePage.get(), (juce::Component*) advancedPage.get() })
        p->setBounds (content);

    // ROUTING fills the workspace and the two set-up pages sit inside it, where its section
    // row and its LIVE SAFE cover can be over them. Covered, `contentBounds` is empty, so the
    // page underneath has no size and nothing on it can be reached.
    if (routingPage != nullptr)
    {
        routingPage->setBounds (content);
        const auto inner = routingPage->contentBounds();
        for (juce::Component* p : { (juce::Component*) devicePage.get(), (juce::Component*) assignPage.get() })
            p->setBounds (inner);
    }

    // A sheet covers the workspace column; the chat is a panel down the right of it.
    auto sheetColumn = columnBounds();
    for (juce::Component* sheetComponent : { (juce::Component*) themeSheet.get(), (juce::Component*) historySheet.get(),
                                             (juce::Component*) channelSheet.get(), (juce::Component*) checkSheet.get(),
                                             (juce::Component*) readinessSheet.get(),
                                             (juce::Component*) exportSheet.get(), (juce::Component*) choiceSheet.get() })
        if (sheetComponent != nullptr) { sheetComponent->setBounds (sheetColumn); sheetComponent->toFront (false); }
    if (chatSheet != nullptr)
    {
        chatSheet->setBounds (contentBounds().removeFromRight (panelW));
        chatSheet->toFront (false);
    }

    // The guide sits in the bottom-left of the workspace, clear of the chain foot and of the
    // side panel a workspace may have there - out of the way of the thing it is describing.
    if (guide != nullptr)
    {
        // Never over a sheet or a tour. The sheets are brought to the front just above, so a
        // card raised after them would float over a scrim that is asking a question.
        const bool clear = openSheetName().isEmpty() && tutorial == nullptr;
        guide->setVisible (clear);
        if (clear)
        {
            auto area = workspaceCard();
            const int w = juce::jmin (WorkspaceGuide::width, juce::jmax (220, area.getWidth() - 2 * WorkspaceGuide::gap));
            const int h = juce::jmin (guide->wantedHeight(), juce::jmax (80, area.getHeight() - 2 * WorkspaceGuide::gap));
            guide->setBounds (area.removeFromBottom (h + WorkspaceGuide::gap).withTrimmedBottom (WorkspaceGuide::gap)
                                  .removeFromLeft (w + WorkspaceGuide::gap).withTrimmedLeft (WorkspaceGuide::gap));
            guide->toFront (false);
        }
    }
    if (tutorial != nullptr) { tutorial->setBounds (getLocalBounds()); tutorial->toFront (false); }

    if (toast->isVisible())
    {
        const int w = toast->idealWidth();
        const int h = toast->idealHeight();
        auto area = contentBounds();
        if (chatSheet != nullptr) area.removeFromRight (chatSheet->getWidth());
        toast->setBounds (area.getCentreX() - w / 2, area.getBottom() - 30 - h, w, h);
        toast->toFront (false);
    }
}

} // namespace livemix
