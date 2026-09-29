#include "MainView.h"
#include "native/InputMapStore.h"
#include "native/MultitrackImport.h"
#include "native/OpenAiMixProvider.h"

namespace livemix
{

// Nothing opens by itself in the headless snapshot tool (MainView::setAutoTutorial), and it
// renders from the design rather than from this Mac's chosen theme (setStoredThemeUsed).
namespace { bool gAutoTutorial = true; bool gUseStoredTheme = true; }

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

    static juce::Font verbFont() { return Dine::caps (11.0f, 0.04f, 600); }

    int idealWidth() const
    {
        switch (kind)
        {
            case Kind::Glyph:   return 32;
            case Kind::Key:     return 16 + 6 + 6 + Dine::textWidth (verbFont(), label());
            case Kind::Primary: return 24 + 16 + 6 + Dine::textWidth (verbFont(), label());
            case Kind::Verb:
            default:            return 16 + (icon != Dine::Icon::None ? 16 + 6 : 0) + Dine::textWidth (verbFont(), label());
        }
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        const float alpha = isEnabled() ? 1.0f : Dine::disabled;
        g.setOpacity (alpha);

        if (kind == Kind::Primary)
        {
            // The one amber control in the product - and amber only while it is *on*, because
            // "is the sound locked" is a state a volunteer has to be able to read at a glance,
            // and a control that looks the same either way does not say it.
            if (on) Dine::fillRounded (g, r, down ? Dine::warn.darker (0.15f) : over ? Dine::warn.brighter (0.10f) : Dine::warn,
                                       Dine::Radius::control);
            else
            {
                if (over || down) Dine::fillRounded (g, r, down ? Dine::selected : Dine::control, Dine::Radius::control);
                Dine::hairlineRounded (g, r.reduced (0.5f), Dine::hair, Dine::Radius::control);
            }
            const auto fg = on ? Dine::onAccent : Dine::warn;
            auto inner = r.reduced (12.0f, 0.0f);
            Dine::drawIcon (g, Dine::Icon::Lock, inner.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f), fg);
            inner.removeFromLeft (6.0f);
            g.setColour (fg);
            g.setFont (verbFont());
            Dine::drawText (g, label(), inner.toNearestInt(), juce::Justification::centredLeft);
            return;
        }

        if (kind == Kind::Glyph)
        {
            if (on || over) Dine::fillRounded (g, r, on ? Dine::selected : Dine::control, Dine::Radius::control);
            Dine::drawIcon (g, icon, r.withSizeKeepingCentre (16.0f, 16.0f), on ? Dine::ink : over ? Dine::ink : Dine::ink2);
            return;
        }

        if (kind == Kind::Key)
        {
            if (on)            Dine::fillRounded (g, r, down ? tint.darker (0.15f) : over ? tint.brighter (0.10f) : tint, Dine::Radius::key);
            else if (over)     Dine::fillRounded (g, r, Dine::control, Dine::Radius::key);
            auto inner = r.reduced (8.0f, 0.0f);
            auto lamp = inner.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f);
            g.setColour (on ? Dine::onAccent : tint);
            g.fillEllipse (lamp);
            inner.removeFromLeft (6.0f);
            g.setColour (on ? Dine::onAccent : over ? Dine::ink : Dine::ink2);
            g.setFont (verbFont());
            Dine::drawText (g, label(), inner.toNearestInt(), juce::Justification::centredLeft);
            return;
        }

        // Verb: no plane until the pointer is on it.
        if (over || down) Dine::fillRounded (g, r, down ? Dine::selected : Dine::control, Dine::Radius::control);
        auto inner = r.reduced (8.0f, 0.0f);
        const auto colour = on ? Dine::accentHover : Dine::accent;
        if (icon != Dine::Icon::None)
        {
            Dine::drawIcon (g, icon, inner.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f), colour);
            inner.removeFromLeft (6.0f);
        }
        g.setColour (colour);
        g.setFont (verbFont());
        Dine::drawText (g, label(), inner.toNearestInt(), juce::Justification::centredLeft);
    }

private:
    Kind kind;
    Dine::Icon icon;
    juce::Colour tint;
    juce::String suffix;
    bool on = false;
};

// The sidebar switch: the design's two-pane glyph at 16 pt, on no plane, at the left of the
// toolbar beside the window's own buttons.
class MainView::SidebarButton : public juce::Button
{
public:
    SidebarButton() : juce::Button ("Sidebar") { setWantsKeyboardFocus (false); }
    void setOn (bool o) { if (o != on) { on = o; repaint(); } }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        if (over || down) Dine::fillRounded (g, r, down ? Dine::selected : Dine::control, Dine::Radius::control);
        Dine::drawIcon (g, Dine::Icon::Sidebar, r.withSizeKeepingCentre (16.0f, 16.0f), on ? Dine::ink2 : Dine::ink3);
    }
private:
    bool on = true;
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
        return 10 + 6 + 6 + Dine::textWidth (ToolbarToggle::verbFont(), "SOLO") + 10
             + juce::jmin (220, Dine::textWidth (Dine::text (12.0f, 500), names)) + 10 + 18 + 10;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Dine::fillRounded (g, r, Dine::menubar, Dine::Radius::control);
        Dine::hairlineRounded (g, r.reduced (0.5f), Dine::hair, Dine::Radius::control);
        auto inner = r.reduced (10.0f, 0.0f);
        g.setColour (Dine::accent);
        g.fillEllipse (inner.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f));
        inner.removeFromLeft (6.0f);
        g.setFont (ToolbarToggle::verbFont());
        const int verbW = Dine::textWidth (ToolbarToggle::verbFont(), "SOLO");
        Dine::drawText (g, "SOLO", inner.removeFromLeft (float (verbW)).toNearestInt(), juce::Justification::centredLeft);
        inner.removeFromLeft (10.0f);
        auto cross = inner.removeFromRight (18.0f).withSizeKeepingCentre (18.0f, 18.0f);
        inner.removeFromRight (10.0f);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.0f, 500));
        Dine::drawText (g, names, inner.toNearestInt(), juce::Justification::centredLeft, true);
        Dine::fillRounded (g, cross, crossOver ? Dine::accentHover : Dine::accent, Dine::Radius::key);
        Dine::drawIcon (g, Dine::Icon::Close, cross.reduced (4.0f), Dine::onAccent);
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
    StatusBar (MixController& c, AppServices& s) : controller (c), services (s) { setOpaque (true); }

    void update (bool slow)
    {
        Look next;
        const bool running = services.isAudioRunning();
        const bool lost = ! running && services.deviceStopped();
        next.engine = lost ? "Device lost" : ! running ? "Not running" : controller.isBypassed() ? "Bypassed" : "running";
        next.engineTint = lost ? Dine::crit : ! running ? Dine::ink3 : controller.isBypassed() ? Dine::warn : Dine::ink;
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
        next.disk = diskText;
        next.diskTint = diskLow ? Dine::warn : Dine::ink;

        const auto loud = controller.getMasterLoudness();
        next.loudness = ! loud.known || loud.integratedLufs <= -100.0f ? juce::String (Glyph::dash()) + " LUFS"
                                                                       : juce::String (loud.integratedLufs, 1) + " LUFS";
        next.loudTint = ! loud.known || loud.integratedLufs <= -100.0f ? Dine::ink3 : loud.onTarget() ? Dine::ink : Dine::warn;

        const auto& mon = controller.getMonitor();
        next.monitor = juce::String (mon.mode == SoloMode::InPlace ? "in place" : "solo") + " " + Glyph::dot() + " "
                     + (mon.point == SoloPoint::PFL ? "PFL" : "AFL");
        next.monitorTint = controller.hasMonitorOutput() ? Dine::ink : Dine::ink3;
        next.safe = project.liveSafe;

        if (next != look) { look = next; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        Dine::drawStatusBand (g, getLocalBounds());
        g.setColour (Dine::hair);
        g.fillRect (getLocalBounds().removeFromTop (1));   // the seam over the status foot

        // The counts the title row used to carry, at the right end where they stay put.
        auto r = getLocalBounds().withTrimmedTop (1).reduced (16, 0);
        if (look.counts.isNotEmpty())
        {
            const auto font = Dine::text (11.0f, 500);
            const int w = Dine::textWidth (font, look.counts);
            g.setColour (Dine::ink3);
            g.setFont (font);
            Dine::drawText (g, look.counts, r.removeFromRight (w), juce::Justification::centredRight);
            r.removeFromRight (24);
        }

        cell (g, r, "Engine", look.engine, look.engineTint);
        cell (g, r, "CPU", look.cpu, look.cpuTint);
        cell (g, r, "Disk", look.disk, look.diskTint);
        cell (g, r, "Recording", look.rec, look.recTint);
        cell (g, r, "Broadcast", look.loudness, look.loudTint);
        cell (g, r, "Monitor", look.monitor, look.monitorTint);
        cell (g, r, "Dropped", juce::String (look.drops), look.drops > 0 ? Dine::warn : Dine::ink3);
        cell (g, r, "Live safe", look.safe ? "on" : "off", look.safe ? Dine::warn : Dine::ink3);
    }

private:
    // Sentence case, the label quiet and the value beside it: "Engine running", "Disk 2.1 GB/h".
    // Nothing on this row is a heading, so nothing on it is in capitals.
    static void cell (juce::Graphics& g, juce::Rectangle<int>& r, const juce::String& label,
                      const juce::String& value, juce::Colour ink)
    {
        const auto labelFont = Dine::text (11.0f, 500);
        const auto valueFont = Dine::mono (11.0f, 500);
        const int labelW = Dine::textWidth (labelFont, label);
        const int valueW = Dine::textWidth (valueFont, value);
        const int w = labelW + 6 + valueW;
        if (w + 24 > r.getWidth()) return;
        auto area = r.removeFromLeft (w);
        r.removeFromLeft (24);
        g.setColour (Dine::ink3);
        g.setFont (labelFont);
        Dine::drawText (g, label, area.removeFromLeft (labelW), juce::Justification::centredLeft);
        area.removeFromLeft (6);
        g.setColour (ink);
        g.setFont (valueFont);
        Dine::drawText (g, value, area, juce::Justification::centredLeft);
    }

    struct Look
    {
        juce::String engine, cpu, disk, rec, loudness, monitor, counts;
        juce::Colour engineTint, cpuTint, diskTint, recTint, loudTint, monitorTint;
        int drops = 0;
        bool recording = false, safe = false;
        bool operator== (const Look& o) const
        {
            return engine == o.engine && cpu == o.cpu && disk == o.disk && rec == o.rec && loudness == o.loudness
                && monitor == o.monitor && counts == o.counts && engineTint == o.engineTint && cpuTint == o.cpuTint && diskTint == o.diskTint
                && recTint == o.recTint && loudTint == o.loudTint && monitorTint == o.monitorTint
                && drops == o.drops && recording == o.recording && safe == o.safe;
        }
        bool operator!= (const Look& o) const { return ! (*this == o); }
    };

    MixController& controller;
    AppServices& services;
    Look look;
    juce::String diskText;
    bool diskLow = false;
};

// The source list down the left (design: `Sidebar` 64:9437). 208 pt, four sections in sentence
// case - Library, Workspace, Safety, Setup - and the audio device along its foot. Folded, it
// does not become a handle: it becomes a 52 pt rail of the same icons (`Sidebar Rail`
// 112:10026), so every workspace is still one click away with the width given to the console.
class MainView::Sidebar : public juce::Component
{
public:
    // What a row does: go to a page, or ask the window for something (a sheet).
    enum class Action { None = 0, MixHistory, Scenes };

    struct Def { const char* label; Page page; Dine::Icon icon; Action action; const char* shortcut; };

    static const Def* rowDefs() noexcept
    {
        static const Def defs[kRows] = {
            { "Sessions",          Page::Sessions,   Dine::Icon::Sessions,      Action::None,       "" },
            { "Favourite mixes",   Page::Favourites, Dine::Icon::Purpose,       Action::None,       "" },
            { "Tracks",            Page::Tracks,     Dine::Icon::TracksNav,     Action::None,       "\u23181" },
            { "Mixer",             Page::Mixer,      Dine::Icon::MixerNav,      Action::None,       "\u23182" },
            { "Tune",              Page::Tune,       Dine::Icon::TuneNav,       Action::None,       "\u23183" },
            { "Live",              Page::Live,       Dine::Icon::LiveNav,       Action::None,       "\u23184" },
            { "Inspector",         Page::Inspector,  Dine::Icon::InspectorNav,  Action::None,       "\u23185" },
            { "Mix history",       Page::Tracks,     Dine::Icon::WindowNav,     Action::MixHistory, "" },
            { "Scenes",            Page::Live,       Dine::Icon::LiveNav,       Action::Scenes,     "" },
            { "Routing",           Page::Routing,    Dine::Icon::DeviceNav,     Action::None,       "" },
            { "Purpose and sound", Page::Purpose,    Dine::Icon::Purpose,       Action::None,       "" },
        };
        return defs;
    }

    Sidebar (AppServices& s, std::function<void (Page)> go, std::function<void (Action)> act) : services (s)
    {
        const auto* defs = rowDefs();
        for (int i = 0; i < kRows; ++i)
        {
            const auto& d = defs[i];
            items[size_t (i)] = std::make_unique<DineNavItem> (d.label, d.icon);
            items[size_t (i)]->onClick = [go, act, d] { if (d.action != Action::None) act (d.action); else go (d.page); };
            addAndMakeVisible (*items[size_t (i)]);

            rail[size_t (i)] = std::make_unique<DineNavItem> ("", d.icon);
            rail[size_t (i)]->onClick = items[size_t (i)]->onClick;
            rail[size_t (i)]->setTooltip (juce::String (d.label)
                                          + (juce::String (d.shortcut).isNotEmpty() ? "  " + juce::String (d.shortcut) : juce::String()));
            addChildComponent (*rail[size_t (i)]);
        }
        items[9]->setTooltip ("The device, the inputs and the output feeds. Under LIVE SAFE nothing there can be "
                              "changed until you say you mean it.");
        setOpaque (true);
    }

    DineNavItem& item (Page p) { return *items[size_t (indexOf (p))]; }
    void setSelected (Page p)
    {
        const int sel = indexOf (p);
        for (int i = 0; i < kRows; ++i)
        {
            const bool on = i == sel && rowDefs()[i].action == Action::None;
            items[size_t (i)]->setSelected (on);
            rail[size_t (i)]->setSelected (on);
        }
    }

    void setCollapsed (bool c)
    {
        if (c == collapsed) return;
        collapsed = c;
        for (auto& i : items) i->setVisible (! c);
        for (auto& i : rail)  i->setVisible (c);
        resized();
        repaint();
    }
    bool isCollapsed() const noexcept { return collapsed; }
    int width() const noexcept { return collapsed ? Dine::Metric::sidebarRail : Dine::Metric::sidebar; }

    // The device along the foot. Compared before a repaint.
    void refresh (bool recording)
    {
        const bool running = services.isAudioRunning();
        juce::String state = recording ? "Recording" : running ? "Running" : "Not running";
        // The input device, or the output one when the engine is running on outputs alone.
        juce::String name = ! running ? juce::String ("No audio device")
                          : services.currentInputDevice().isNotEmpty() ? services.currentInputDevice()
                          : services.currentOutputDevice().isNotEmpty() ? services.currentOutputDevice() : juce::String ("No audio device");
        const int xruns = services.xrunCount();
        juce::String spec = running ? juce::String (services.sampleRate() / 1000.0, 0) + " kHz " + Glyph::dot() + " "
                                          + juce::String (xruns) + (xruns == 1 ? " dropped buffer" : " dropped buffers")
                                    : juce::String ("Nothing is open");
        if (state == footState && name == footName && spec == footSpec && xruns == footXruns && recording == footRecording) return;
        footState = state; footName = name; footSpec = spec; footXruns = xruns; footRecording = recording;
        repaint (getLocalBounds().removeFromBottom (kFootH));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (Dine::sidebar);
        g.fillRect (r);
        g.setColour (Dine::hair);
        g.fillRect (r.removeFromRight (1));   // the seam against the workspace

        if (collapsed)
        {
            // The rail says what the device is doing with one lamp; the name has no room.
            auto lamp = getLocalBounds().removeFromBottom (kFootH).withSizeKeepingCentre (8, 8);
            g.setColour (footRecording ? Dine::crit : services.isAudioRunning() ? Dine::ok : Dine::ink4);
            g.fillEllipse (lamp.toFloat());
            return;
        }

        // the caption over each group of rows: 11 pt, quiet, sentence case
        for (const auto& c : captions)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f, 500));
            Dine::drawText (g, c.second, c.first, juce::Justification::centredLeft);
        }

        // the device, under a seam that stops short of both edges
        auto foot = getLocalBounds().removeFromBottom (kFootH);
        g.setColour (Dine::hair);
        g.fillRect (foot.removeFromTop (1).reduced (16, 0));
        foot = foot.reduced (16, 0).withTrimmedTop (13);
        g.setColour (footRecording ? Dine::crit : services.isAudioRunning() ? Dine::ink : Dine::ink3);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, footName, foot.removeFromTop (18), juce::Justification::centredLeft, true);
        foot.removeFromTop (2);
        g.setColour (footXruns > 0 ? Dine::warn : Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        Dine::drawText (g, footSpec, foot.removeFromTop (14), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (collapsed)
        {
            r.removeFromTop (12);
            r.removeFromBottom (kFootH);
            for (int i = 0; i < kRows; ++i)
            {
                rail[size_t (i)]->setBounds (r.removeFromTop (32).withSizeKeepingCentre (36, 32));
                r.removeFromTop (4);
            }
            return;
        }
        r.removeFromBottom (kFootH);
        captions.clear();
        // The design's rhythm: a caption 12 pt under the row above it, its first row 22 pt under
        // the caption, and 30 pt from one row to the next.
        int y = 14;
        auto caption = [&] (const char* text)
        {
            captions.push_back ({ juce::Rectangle<int> (16, y, getWidth() - 32, 14), text });
            y += 22;
        };
        auto rows = [&] (int from, int to)
        {
            for (int i = from; i < to; ++i)
            {
                items[size_t (i)]->setBounds (8, y, getWidth() - 17, Dine::Metric::row);
                y += 30;
            }
            y += 12 - 2;
        };
        caption ("Library");   rows (0, 2);
        caption ("Workspace"); rows (2, 7);
        caption ("Safety");    rows (7, 9);
        caption ("Setup");     rows (9, kRows);
    }

private:
    static constexpr int kRows = 11;
    static constexpr int kFootH = 72;
    static int indexOf (Page p) noexcept
    {
        switch (p)
        {
            case Page::Sessions: return 0;
            case Page::Favourites: return 1;
            case Page::Tracks: return 2; case Page::Mixer: return 3; case Page::Tune: return 4;
            case Page::Live: return 5; case Page::Inspector: return 6;
            case Page::Routing: case Page::Device: case Page::Assign:
            case Page::Outputs: case Page::Maps: return 9;
            case Page::Purpose: return 10;
        }
        return 0;
    }

    AppServices& services;
    std::array<std::unique_ptr<DineNavItem>, size_t (kRows)> items, rail;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;
    juce::String footState, footName, footSpec;
    int footXruns = 0;
    bool footRecording = false, collapsed = false;
};

// ---------------------------------------------------------------- mixer window
class MainView::MixerWindow : public juce::DocumentWindow, private juce::Timer
{
public:
    MixerWindow (MixController& c, AppServices& s, MainView& owner)
        : juce::DocumentWindow ("Mixer " + juce::String (Glyph::dash()) + " DLIVE", Dine::window,
                                juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton
                                    | juce::DocumentWindow::maximiseButton),
          view (owner)
    {
        page = std::make_unique<MixerPage> (c, s);
        page->setWindowButtonVisible (false);
        page->onOpenStrip = [&owner] (int strip) { owner.showPage (Page::Inspector); owner.getAdvancedPage().select (strip); };
        page->onOpenBus = [&owner] (MixBus bus) { owner.showPage (Page::Inspector); owner.getAdvancedPage().selectBus (bus); };
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
                m.addItem (104, "Import Multitrack Folder...");
                m.addItem (107, "Add a Reference Mix...");
                m.addSeparator();
                m.addItem (108, "Save Input Mapping" + juce::String (Glyph::ellip()),
                           ! view.controller.getSession().inputs.empty());
                m.addItem (109, "Input Mappings" + juce::String (Glyph::ellip()));
                m.addSeparator();
                m.addItem (105, "Export Stereo Mix (WAV)...");
                m.addItem (106, "Export Stereo Mix (MP3)...");
                break;
            case 1:
                m.addItem (200, "Undo", view.tracksPage != nullptr && view.tracksPage->canUndo());
                m.addSeparator();
                m.addItem (201, "Split at Playhead");
                m.addItem (202, "Delete Clip");
                m.addSeparator();
                m.addItem (203, "Add Marker at Playhead   M");
                break;
            case 2:
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
                // SPEECH PRIORITY: the one thing in DLIVE that moves a level by itself, so it
                // says what it does rather than only what it is called.
                m.addItem (413, "Speech Priority: the band steps back while somebody speaks", true,
                           view.controller.getSpeechPriority(), nullptr);
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
                // AUTOPILOT: the second thing in DLIVE allowed to move a level by itself, and
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
                    // Every theme, DLIVE's own then yours, the chosen one ticked; then the sheet.
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
                m.addSeparator();
                m.addItem (700, "About DLIVE");
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
        });
    sidebar->item (Page::Sessions).setTooltip ("The library: every saved session, and what each one was for.");
    sidebar->item (Page::Favourites).setTooltip ("The mixes that worked, with what they measured. A later tune can be aimed at one.");
    addAndMakeVisible (*sidebar);

    statusBar = std::make_unique<StatusBar> (controller, services);
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

    tuneLiveButton = std::make_unique<ToolbarToggle> ("TUNE LIVE MIX", ToolbarToggle::Kind::Verb, Dine::Icon::TuneNav);
    tuneLiveButton->setTooltip ("Start TUNE LIVE MIX from any workspace: DLIVE listens to the band, builds its mix and reasons "
                                "about what this band still needs. The listen and the result open on TUNE. Press again to stop.");
    tuneLiveButton->onClick = [this] { handleCommand (405); };
    addChildComponent (*tuneLiveButton);

    chatButton = std::make_unique<ToolbarToggle> ("MIX BUDDY", ToolbarToggle::Kind::Glyph, Dine::Icon::Chat);
    chatButton->setTooltip ("Open or close Mix Buddy, DLIVE's mix engineer in plain words: ask for a change to the mix - "
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

    // AUTOPILOT is the fourth broadcast key and the second thing in DLIVE allowed to move a
    // level by itself, so it is on the chrome wherever you are: lit while it is on, one press off.
    autopilotButton = std::make_unique<ToolbarToggle> ("AUTOPILOT", ToolbarToggle::Kind::Key, Dine::Icon::None, Dine::monitor);
    autopilotButton->setTooltip ("Hold the mix you set. Autopilot moves group faders only, slowly, inside a few dB of "
                                 "the mix it was engaged on, and says why every time. Touch a fader and it is yours again.");
    autopilotButton->onClick = [this] { handleCommand (626); };
    addChildComponent (*autopilotButton);

    liveSafeButton = std::make_unique<ToolbarToggle> ("LIVE SAFE", ToolbarToggle::Kind::Primary);
    liveSafeButton->setTooltip ("Locks the sound: re-routes and re-tunes are blocked, and a fader cannot move more "
                                "than 6 dB at a time. Mute, solo, the monitor and the recording always stay free.");
    liveSafeButton->onClick = [this] { handleCommand (614); };
    addChildComponent (*liveSafeButton);

    addAndMakeVisible (outputButton);
    outputButton.setFlat (true);
    outputButton.setTooltip ("Where the finished mix goes out.");
    outputButton.onClick = [this] { chooseOutput(); };

    addChildComponent (*toast);

    favouritesPage->onToast = [this] (const juce::String& t) { showToast (t); };
    sessionsPage->onNew = [this] { newSession(); };
    sessionsPage->onOpenFile = [this] { openSession(); };
    sessionsPage->onImportFolder = [this] { importMultitrack(); };
    sessionsPage->onOpen = [this] (const juce::File& file)
    {
        const auto err = services.loadSession (file);
        if (err.isNotEmpty()) { showToast (err); return; }
        advancedPage->rebuild();
        mixerPage->rebuild();
        if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
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
    devicePage->onImportRecording = [this] (const juce::File& folder)
    {
        const auto err = services.importMultitrack (folder);
        if (err.isNotEmpty()) { showToast (err); return; }
        assignPage->refresh();
        tracksPage->rebuild();
        showToast ("Imported " + folder.getFileName() + ".");
        showPage (Page::Assign);
    };
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
        mapChooser = std::make_unique<juce::FileChooser> ("Import an input patch", juce::File(), "*.dlivemap.json");
        mapChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this] (const juce::FileChooser& fc)
                                 {
                                     const auto file = fc.getResult();
                                     if (file == juce::File()) return;
                                     InputMap imported;
                                     if (! InputMapStore::load (file, imported)) { showToast ("That is not a DLIVE input patch."); return; }
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
    tracksPage->onOpenStrip = [this] (int strip) { showPage (Page::Inspector); advancedPage->select (strip); };
    tracksPage->onOpenAssign = [this] { assignPage->refresh(); showPage (Page::Assign); };
    tracksPage->onTuneStrip = [this] (int strip) { tuneChannel (strip); };
    tracksPage->onSessionChanged = [this]
    {
        advancedPage->rebuild();
        mixerPage->rebuild();
        if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
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
    mixerPage->onOpenStrip = [this] (int strip) { showPage (Page::Inspector); advancedPage->select (strip); };
    mixerPage->onOpenBus = [this] (MixBus bus) { showPage (Page::Inspector); advancedPage->selectBus (bus); };
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
    checkSheet.reset();
    historySheet.reset();
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
    if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
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

    updateChrome();
    resized();
    repaint();
    grabKeyboardFocus();
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
    const Page all[8] = { Page::Sessions, Page::Favourites, Page::Routing, Page::Purpose,
                          Page::Tracks, Page::Mixer, Page::Tune, Page::Live };
    for (const Page p : all)
        sidebar->item (p).setEnabled (isSetupPage (p) || mixable);
    sidebar->item (Page::Inspector).setEnabled (mixable);
    sidebar->setSelected (isRoutingPage (page) ? Page::Routing : page);
    sidebar->item (Page::Sessions).setMeta (juce::String (services.listSessions().size()));
    sidebar->item (Page::Favourites).setMeta (controller.numFavourites() > 0 ? juce::String (controller.numFavourites()) : juce::String());
    sidebar->item (Page::Routing).setMeta (hasInputs ? juce::String (int (session.inputs.size())) + " in" : juce::String());
    sidebar->item (Page::Routing).setDone (mixable && ! isRoutingPage (page));
    routingPage->refresh();

    // ---- the solo pill: what is soloed, beside the clock, on every workspace
    {
        auto soloed = controller.getSoloed();
        const bool wasShown = soloPill->isVisible();
        soloPill->setItems (soloed);
        if (soloPill->hasAny() != wasShown) { soloPill->setVisible (soloPill->hasAny()); resized(); }
    }

    // What is on the title row and the toolbar decides where everything else on them goes, so a
    // button appearing or disappearing lays both rows out again (resized() skips a hidden one).
    bool rowsChanged = false;
    auto show = [&rowsChanged] (juce::Component& c, bool visible)
    {
        if (c.isVisible() == visible) return;
        c.setVisible (visible);
        rowsChanged = true;
    };
    show (outputButton, running || inWorkspace);
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
    liveSafeButton->setSuffix (project.liveSafe ? "ON" : juce::String());
    show (*chatButton, mixable);
    chatButton->setOn (chatSheet != nullptr);
    show (*tuneLiveButton, mixable);
    tuneLiveButton->setOn (controller.isTuningLive());
    tuneLiveButton->setSuffix (controller.isTuningLive() ? juce::String ("  " + Glyph::dot() + "  STOP") : juce::String());
    show (*transportBar, inWorkspace);
    show (*chainFoot, inWorkspace && mixable);
    if (rowsChanged) resized();

    const juce::String out = services.outputDisplayName();
    outputButton.setValue (out.isEmpty() ? "No output" : out);
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

// The session button's popover: the four setup steps with what each is set to, and the
// ways out - the setup pages, the library, saving.
void MainView::setupPopover()
{
    const auto& session = controller.getSession();
    const bool running = services.isAudioRunning();
    const bool hasInputs = ! session.inputs.empty();
    const bool ready = controller.isPrepared() && hasInputs;

    juce::PopupMenu m;
    m.addSectionHeader (ready ? "Ready for soundcheck" : "Session and setup");
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
    m.addSeparator();
    m.addItem (11, "New session");
    m.addItem (6, "Open session" + juce::String (Glyph::ellip()));
    m.addItem (7, "Save");
    m.addItem (8, "Save as" + juce::String (Glyph::ellip()));
    m.addSeparator();
    m.addItem (12, "Import multitrack folder" + juce::String (Glyph::ellip()));
    m.addItem (13, "Add a reference mix" + juce::String (Glyph::ellip()));
    m.addItem (14, "Save input mapping" + juce::String (Glyph::ellip()), hasInputs);
    m.addItem (15, "Export stereo mix (WAV)" + juce::String (Glyph::ellip()));
    m.addSeparator();
    m.addItem (5, "Open setup");
    m.addItem (9, "Rename or fix the inputs" + juce::String (Glyph::ellip()));
    m.addItem (10, "Getting started");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sidebarButton.get())
                                               .withMinimumWidth (300),
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
void MainView::setSidebarShown (bool shown)
{
    if (shown == sidebarShown) return;
    sidebarShown = shown;
    sidebarButton->setOn (shown);
    sidebar->setCollapsed (! shown);
    resized();
    repaint();
}

juce::String MainView::panelName (bool left) const
{
    if (left)
    {
        if (page == Page::Tune) return "Inputs";
        if (page == Page::Inspector) return "Channels";
        return "Sidebar";
    }
    if (page == Page::Inspector) return "What DLIVE did";
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
    livePage->repaint();
    mixPage->repaint();
    updateChainFoot();
}

void MainView::closeSheets()
{
    if (mixPage != nullptr && mixPage->isScopeSheetOpen()) mixPage->closeScopeSheet();
    checkSheet.reset();
    historySheet.reset();
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
    updateChrome();
    if (menu != nullptr) menu->menuItemsChanged();
    showToast ("Text size: " + name);
}

// RESET MIX TO RAW. Everything DLIVE decided about the sound, taken back to the session's
// baseline. It is asked for out loud because it throws a service's mixing away - and answered
// with what it keeps, because the list of things it does *not* touch is the reassuring part.
void MainView::resetMixToRaw()
{
    if (liveSafeBlocks ("resetting the mix")) return;
    closeSheets();
    choiceSheet = std::make_unique<ChoiceSheet> (
        "Reset the mix to raw?",
        "Every channel goes back to how it sounded before DLIVE touched it, ready to show raw, then "
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
        if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
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
        showToast ("DLIVE is already listening. Let it finish, or cancel it first.");
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
            showToast ("Patch saved as \"" + name + "\": " + juce::String (map.inputs.size()) + " inputs across "
                       + juce::String (map.channelsNeeded()) + " channels.");
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
            mapChooser = std::make_unique<juce::FileChooser> ("Import an input mapping", juce::File(), "*.dlivemap.json");
            mapChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                     [this] (const juce::FileChooser& fc)
                                     {
                                         const auto file = fc.getResult();
                                         if (file == juce::File()) return;
                                         InputMap imported;
                                         if (! InputMapStore::load (file, imported)) { showToast ("That is not a DLIVE input mapping."); return; }
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
                                                                      .getChildFile (juce::File::createLegalFileName (map.name) + ".dlivemap.json"),
                                                                  "*.dlivemap.json");
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

        case 200: if (! liveSafeBlocks ("editing the timeline")) tracksPage->undo(); break;
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
            controller.undoMix(); updateChrome();
            showToast ("Stepped back a whole mix. Every value it set went with it.");
            break;
        case 412:
            if (! controller.canRedoMix()) { showToast ("This is the newest mix in this session."); break; }
            controller.redoMix(); updateChrome();
            showToast ("Stepped forward a whole mix.");
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
                           : "DLIVE has not heard the band yet, so it is listening first.");
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
        case 402: controller.clearSolos(); showToast ("Solo cleared."); break;
        case 403: setBypass (! controller.isBypassed()); break;
        case 406:
        {
            usingCloudMixEngineer = ! usingCloudMixEngineer;
            if (usingCloudMixEngineer && ! OpenAiMixProvider().isAvailable())
            {
                usingCloudMixEngineer = false;
                showToast ("No API key is configured, so DLIVE is using its own mix engineer.");
                break;
            }
            if (usingCloudMixEngineer) controller.setReasoningProvider (std::make_shared<OpenAiMixProvider>());
            else controller.setReasoningProvider (nullptr);
            showToast (usingCloudMixEngineer
                           ? "TUNE LIVE MIX will ask the cloud mix engineer. Measurements and source names are sent; no audio ever leaves this machine."
                           : "TUNE LIVE MIX is back on DLIVE's own mix engineer. Nothing leaves this machine.");
            break;
        }

        case 500: transportBar->togglePlay(); break;
        case 501: transportBar->toggleRecord(); break;
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
        case 700:
            showToast ("DLIVE - the live recording and broadcast DAW. Connect. Record. Mix. Tune. Broadcast.");
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
        if (code == 'Z') return 200;
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
    if (! controller.getSession().inputs.empty()) services.saveSession();
    services.newSession();
    assignPage->refresh();
    tracksPage->rebuild();
    mixerPage->rebuild();
    if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
    advancedPage->rebuild();
    showPage (Page::Device);
    showToast ("New session.");
}

void MainView::sessionMenu()
{
    setupPopover();
}

void MainView::importMultitrack()
{
    if (liveSafeBlocks ("importing")) return;
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder of recorded stems",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (folder == juce::File()) return;
                              const auto err = services.importMultitrack (folder);
                              if (err.isNotEmpty()) { showToast (err); return; }
                              assignPage->refresh();
                              tracksPage->rebuild();
                              mixerPage->rebuild();
                              if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
                              advancedPage->rebuild();
                              showToast ("Imported " + folder.getFileName() + ". Check the inputs, then build the mix.");
                              showPage (Page::Assign);
                          });
}

// EXPORT: the sheet asks what, how much and where, then the render happens on a worker while
// the console keeps playing. The menu's two items open the same sheet with their format already
// picked, so File > Export Stereo Mix (MP3) still means what it says.
void MainView::exportMix (AppServices::ExportFormat format)
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
    if (exporting) { showToast ("An export is already running. It will say when it is done."); return; }

    closeSheets();
    exportSheet = std::make_unique<ExportSheet> (controller, services);
    exportSheet->onClose = [this] { exportSheet.reset(); resized(); repaint(); grabKeyboardFocus(); };
    exportSheet->onToast = [this] (const juce::String& t) { showToast (t); };
    exportSheet->onExport = [this] (const juce::File& dest, AppServices::ExportFormat fmt, juce::int64 from, juce::int64 to)
    {
        if (exporting) { showToast ("An export is already running. It will say when it is done."); return; }
        dest.getParentDirectory().createDirectory();
        auto job = services.snapshotExport();
        if (job != nullptr)
        {
            auto ranged = std::make_shared<AppServices::ExportJob> (*job);
            ranged->from = from;
            ranged->to = to;
            job = ranged;
        }
        exporting = true;
        showToast ("Exporting " + dest.getFileName() + Glyph::ellip());
        juce::Component::SafePointer<MainView> safe (this);
        auto& srv = services;
        juce::Thread::launch ([safe, &srv, job, dest, fmt]
        {
            const auto err = srv.exportMix (job, dest, fmt, {});
            juce::MessageManager::callAsync ([safe, err, dest]
            {
                if (safe == nullptr) return;
                safe->exporting = false;
                safe->showToast (err.isNotEmpty() ? err : "Exported " + dest.getFileName() + ".");
            });
        });
    };
    addAndMakeVisible (*exportSheet);
    juce::ignoreUnused (format);
    resized();
    exportSheet->grabKeyboardFocus();
}

void MainView::saveNow()
{
    if (services.currentSessionName().isEmpty()) { saveAs(); return; }
    services.saveSession();
    showToast ("Session saved.");
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
    if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
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

void MainView::chooseOutput()
{
    const auto outs = services.outputDevices();
    if (outs.isEmpty()) { showToast ("No output devices found."); return; }
    juce::PopupMenu m;
    const juce::String current = services.broadcastOutputDevice();
    for (int i = 0; i < outs.size(); ++i)
    {
        if (outs[i].name.startsWith ("DLIVE Monitoring")) continue;
        m.addItem (i + 1, outs[i].name, true, outs[i].name == current);
    }

    m.addSeparator();
    m.addItem (900, "Set up outputs" + Glyph::ellip());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton).withMinimumWidth (outputButton.getWidth()),
                     [this, outs] (int result)
                     {
                         if (result == 900) { showOutputs(); return; }
                         if (result <= 0 || result > outs.size()) return;
                         const auto& name = outs[result - 1].name;
                         if (name == services.broadcastOutputDevice()) return;
                         if (liveSafeBlocks ("changing the output device")) return;
                         const auto err = services.isAudioRunning() ? services.changeOutput (name)
                                                                    : services.openOutputOnly (name);
                         if (err.isNotEmpty()) showToast (err);
                         else { showToast ("Broadcast: " + name); updateChrome(); }
                     });
}

// ---------------------------------------------------------------- ticking
void MainView::timerCallback()
{
    controller.poll();
    transportBar->refresh();

    if (page == Page::Tracks) tracksPage->refresh();
    else if (page == Page::Mixer) mixerPage->refresh();
    else if (page == Page::Tune) mixPage->refresh();
    else if (page == Page::Live) livePage->refresh();
    else if (page == Page::Inspector) advancedPage->refresh();

    if (channelSheet != nullptr) channelSheet->refresh();
    if (checkSheet != nullptr) checkSheet->refresh();
    if (chatSheet != nullptr) chatSheet->refresh();

    const bool slow = (++slowTicks % 30) == 0;
    statusBar->update (slow);
    if (slow || slowTicks % 10 == 0) sidebar->refresh (services.daw().isRecording());
    if (chainFoot->isVisible() && slowTicks % 3 == 0) updateChainFoot();

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

    const bool live = running && controller.isPrepared() && ! controller.getSession().inputs.empty();
    const float want = ! live ? 0.0f
                              : 0.35f + 0.65f * (0.5f + 0.5f * std::sin (float (juce::Time::getMillisecondCounter())
                                                                        * 0.0024f));
    if (std::abs (want - onAir) > 0.02f || (onAir > 0.0f && want == 0.0f))
    {
        onAir = want;
        repaint (0, 0, getWidth(), Dine::Metric::onAir);
    }
}

// ---------------------------------------------------------------- layout
juce::Rectangle<int> MainView::columnBounds() const
{
    auto r = getLocalBounds().withTrimmedTop (Dine::Metric::toolbar);
    if (sidebar != nullptr && sidebar->isVisible()) r.removeFromLeft (sidebar->width());
    return r;
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
    return r;
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);

    // ---- the strip along the very top: lit while what this Mac is doing reaches somebody else.
    if (onAir > 0.01f)
    {
        const bool recording = services.daw().isRecording();
        const auto tint = recording ? Dine::keyRec : Dine::accent;
        auto strip = getLocalBounds().removeFromTop (Dine::Metric::onAir).toFloat();
        juce::ColourGradient glow (tint.withAlpha (0.0f), strip.getX(), 0.0f,
                                   tint.withAlpha (0.0f), strip.getRight(), 0.0f, false);
        glow.addColour (0.18, tint.withAlpha (onAir));
        glow.addColour (0.82, tint.withAlpha (onAir));
        g.setGradientFill (glow);
        g.fillRect (strip);
    }

    // ---- the one toolbar, and the seam under it
    auto bar = getLocalBounds().removeFromTop (Dine::Metric::toolbar);
    Dine::drawChrome (g, bar);
    g.setColour (Dine::hair);
    g.fillRect (bar.removeFromBottom (1));

    // The divider between TUNE LIVE MIX and the broadcast keys: 1 x 20, centred in the row.
    if (dividerX > 0)
    {
        g.setColour (Dine::hair);
        g.fillRect (juce::Rectangle<int> (dividerX, (Dine::Metric::toolbar - 20) / 2, 1, 20));
    }
}

void MainView::resized()
{
    // ------------------------------------------------------------------ the toolbar
    // One row. The window's own buttons are drawn by macOS at the left of it (the peer is told
    // to put them there), so the first thing this lays out is the sidebar switch beside them;
    // the transport lines up with the left edge of the workspace column, which is where the eye
    // is already looking, and everything else is measured in from the right.
    auto bar = getLocalBounds().removeFromTop (Dine::Metric::toolbar);
    sidebarButton->setBounds (juce::Rectangle<int> (kTrafficLights, 0, 28, Dine::Metric::toolbar)
                                  .withSizeKeepingCentre (28, 28));

    auto right = bar.withTrimmedRight (12);
    auto place = [&right] (juce::Component& c, int w, int h, int gapAfter)
    {
        if (! c.isVisible()) return;
        c.setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, h));
        right.removeFromRight (gapAfter);
    };
    place (*chatButton, chatButton->idealWidth(), 28, 8);
    if (outputButton.isVisible())
    {
        const int w = juce::jlimit (60, 220, juce::jmin (outputButton.idealWidth(), juce::jmax (60, right.getWidth() / 3)));
        place (outputButton, w, 28, 16);
    }
    place (*liveSafeButton, liveSafeButton->idealWidth(), 30, 16);
    // DIM MUTE BYPASS AUTOPILOT, in that order left to right, so they are laid out backwards.
    place (*autopilotButton, autopilotButton->idealWidth(), 28, 2);
    place (*bypassButton, bypassButton->idealWidth(), 28, 2);
    place (*muteButton, muteButton->idealWidth(), 28, 2);
    place (*dimButton, dimButton->idealWidth(), 28, 10);
    dividerX = (dimButton->isVisible() || bypassButton->isVisible()) ? right.getRight() : 0;
    if (dividerX > 0) right.removeFromRight (11);
    place (*tuneLiveButton, tuneLiveButton->idealWidth(), 28, 12);

    // The transport, and the solo pill beside it. They start at the workspace column's left
    // edge and give way rather than run under the cluster when the window is narrow.
    auto left = bar.withX (sidebar != nullptr && sidebar->isVisible() ? sidebar->width() + 16 : kTrafficLights + 44)
                   .withRight (juce::jmax (0, right.getRight() - 16));
    // What is soloed takes its room before the clock's second cell does: the session's length
    // is a convenience, and an S left down is the thing that ruins a service.
    // Enough for the word and a name or two; the pill ellipsises past that rather than
    // pushing the clock out of the toolbar.
    const int pillNeed = (soloPill != nullptr && soloPill->isVisible())
                             ? juce::jmin (soloPill->idealWidth(), 150) + 12 : 0;
    if (transportBar->isVisible())
    {
        int want = transportBar->idealWidth();
        if (left.getWidth() - pillNeed < want) want = transportBar->minimumWidth();
        if (left.getWidth() - pillNeed < want) want = transportBar->keysOnlyWidth();
        const int w = juce::jmin (want, juce::jmax (transportBar->keysOnlyWidth(), left.getWidth()));
        transportBar->setBounds (left.removeFromLeft (w).withSizeKeepingCentre (w, TransportBar::height));
        left.removeFromLeft (12);
    }
    if (soloPill != nullptr && soloPill->isVisible())
    {
        const int w = juce::jlimit (0, juce::jmax (0, left.getWidth()), juce::jmin (soloPill->idealWidth(), 280));
        soloPill->setBounds (left.removeFromLeft (w).withSizeKeepingCentre (w, 28));
    }

    // ------------------------------------------------------------------ the body
    auto body = getLocalBounds().withTrimmedTop (Dine::Metric::toolbar);
    sidebar->setBounds (body.removeFromLeft (sidebar->width()));
    statusBar->setBounds (body.removeFromBottom (Dine::Metric::status));
    // The requests panel is a column beside the workspace, never over it: the pages and the chain foot
    // give up its width, so a sheet a page opens stays whole and the panel stays readable.
    const int panelW = chatSheet != nullptr ? juce::jmin (kRequestsW, body.getWidth() / 2) : 0;
    body.removeFromRight (panelW);
    if (chainFoot->isVisible()) chainFoot->setBounds (body.removeFromBottom (Dine::Metric::chainFoot));

    auto content = body;
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
    auto column = columnBounds();
    for (juce::Component* sheetComponent : { (juce::Component*) themeSheet.get(), (juce::Component*) historySheet.get(),
                                             (juce::Component*) channelSheet.get(), (juce::Component*) checkSheet.get(),
                                             (juce::Component*) exportSheet.get(), (juce::Component*) choiceSheet.get() })
        if (sheetComponent != nullptr) { sheetComponent->setBounds (column); sheetComponent->toFront (false); }
    if (chatSheet != nullptr)
    {
        chatSheet->setBounds (column.removeFromRight (panelW).withTrimmedBottom (Dine::Metric::status));
        chatSheet->toFront (false);
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
