#include "ReadySheet.h"
#include "native/BroadcastReadiness.h"
#include "UI/Widgets.h"
#include <cmath>

namespace livemix
{

namespace
{
    juce::String span (double seconds)
    {
        const int total = int (seconds);
        if (total >= 24 * 3600) return "a day or more";
        if (total >= 3600) return juce::String (total / 3600) + " h " + juce::String ((total / 60) % 60) + " m";
        return juce::String (juce::jmax (0, total / 60)) + " m";
    }
    juce::String lufs (float v) { return juce::String (v, 1).replace ("-", Glyph::minus()); }
}

juce::String ReadyCheck::diskText (juce::int64 bytesFree, double secondsFree)
{
    if (bytesFree <= 0) return {};
    const double gb = double (bytesFree) / (1000.0 * 1000.0 * 1000.0);
    juce::String s = gb >= 100.0 ? juce::String (juce::roundToInt (gb)) + " GB" : juce::String (gb, 1) + " GB";
    if (secondsFree > 0.0) s += " " + Glyph::dot() + " " + (secondsFree >= 24 * 3600 ? juce::String ("a day+") : span (secondsFree));
    return s;
}

std::vector<ReadyCheck::Row> ReadyCheck::gather (MixController& controller, AppServices& services)
{
    std::vector<Row> out;
    auto& daw = services.daw();
    const bool running = services.isAudioRunning();
    const auto device = services.deviceState();

    // ---- the device
    {
        Row r { "Audio device", {}, State::Ok, Fix::None, {} };
        const int dropped = services.xrunCount();
        if (! running)
        {
            r.state = State::Crit;
            r.sentence = device.why.isNotEmpty() ? device.why : juce::String ("No audio device is running, so nothing is heard or sent.");
            r.fix = Fix::AudioDevice;
            r.fixLabel = "Audio device";
        }
        else
        {
            const auto name = device.input.isNotEmpty() ? device.input : juce::String ("The device");
            r.sentence = name + " is running at " + juce::String (juce::roundToInt (services.sampleRate() / 1000.0)) + " kHz, "
                       + (dropped == 0 ? juce::String ("nothing dropped.")
                                       : juce::String (dropped) + (dropped == 1 ? " buffer dropped." : " buffers dropped."));
            if (dropped > 0) { r.state = State::Warn; r.fix = Fix::AudioDevice; r.fixLabel = "Audio device"; }
        }
        out.push_back (r);
    }

    // ---- the inputs: the ones the desk should still move
    {
        Row r { "Inputs", {}, State::Ok, Fix::None, {} };
        juce::StringArray named;
        int n = 0;
        if (controller.isPrepared())
            for (int i = 0; i < controller.getEngine().getNumStrips() && i < controller.getGraph().numStrips(); ++i)
            {
                const auto a = controller.getInputAdvice (i);
                if (! a.needsAttention()) continue;
                using Level = MixController::InputAdvice::Level;
                const juce::String what = a.level == Level::Clipping ? "is clipping" : a.level == Level::Hot ? "is hot"
                                        : a.level == Level::Digital ? "is only loud because DINE raised it"
                                        : a.level == Level::NotHeard ? "was not heard" : "is too quiet";
                if (named.size() < 3) named.add (juce::String (controller.getGraph().strips[size_t (i)].name) + " " + what);
                ++n;
            }
        const int assigned = int (controller.getSession().inputs.size());
        if (assigned == 0)
        {
            r.state = State::Crit;
            r.sentence = "No inputs are assigned yet.";
            r.fix = Fix::CheckInputs;
            r.fixLabel = "Check inputs";
        }
        else if (n > 0)
        {
            r.state = State::Warn;
            r.sentence = juce::String (n) + (n == 1 ? " input needs" : " inputs need") + " attention: " + named.joinIntoString (", ")
                       + (n > named.size() ? ", and more." : ".");
            r.fix = Fix::CheckInputs;
            r.fixLabel = "Check inputs";
        }
        else r.sentence = juce::String (assigned) + (assigned == 1 ? " input" : " inputs") + ", none waiting on the desk.";
        out.push_back (r);
    }

    // ---- recording
    {
        Row r { "Recording", {}, State::Ok, Fix::None, {} };
        int armed = 0;
        for (const auto& t : daw.getProject().tracks) if (t.armed) ++armed;
        if (daw.isRecording())
            r.sentence = "Recording " + juce::String (armed) + (armed == 1 ? " track" : " tracks") + " now.";
        else if (armed == 0)
        {
            r.state = State::Warn;
            r.sentence = "Nothing is set to record, so this service will not be kept.";
            r.fix = Fix::ArmAll;
            r.fixLabel = "Record every track";
        }
        else r.sentence = juce::String (armed) + (armed == 1 ? " input" : " inputs") + " set to record. Press R when the service starts.";
        out.push_back (r);
    }

    // ---- the disk
    {
        Row r { "Disk", {}, State::Ok, Fix::None, {} };
        const auto bytes = daw.recordingBytesFree();
        const double seconds = daw.isRecording() || daw.getRecordingSecondsFree() > 0.0 ? daw.getRecordingSecondsFree()
                                                                                         : daw.recordingSecondsFreeForEveryInput();
        if (bytes <= 0) { r.state = State::Neutral; r.sentence = "The disk will not say how much room it has."; }
        else
        {
            const double gb = double (bytes) / 1.0e9;
            r.sentence = (gb >= 100.0 ? juce::String (juce::roundToInt (gb)) : juce::String (gb, 1)) + " GB free"
                       + (seconds > 0.0 ? " " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) + " about " + span (seconds)
                                              + " at this input count." : juce::String ("."));
            if (seconds > 0.0 && seconds < 15.0 * 60.0) r.state = State::Crit;
            else if (seconds > 0.0 && seconds < 2.0 * 3600.0) r.state = State::Warn;
        }
        out.push_back (r);
    }

    // ---- on air
    {
        Row r { "On air", {}, State::Ok, Fix::None, {} };
        const auto output = services.outputDisplayName();
        if (! running || output.isEmpty())
        {
            r.state = State::Crit;
            r.sentence = "Nothing is going out: no broadcast output is open.";
            r.fix = Fix::Outputs;
            r.fixLabel = "Outputs";
        }
        else if (controller.isBroadcastMuted() || controller.isBroadcastDimmed())
        {
            r.state = State::Warn;
            r.sentence = output + " is routed, but the broadcast is " + (controller.isBroadcastMuted() ? "muted." : "dimmed 20 dB.");
            r.fix = Fix::Outputs;
            r.fixLabel = "Outputs";
        }
        else r.sentence = output + " is routed and carrying the master.";
        out.push_back (r);
    }

    // ---- loudness
    {
        Row r { "Loudness", {}, State::Ok, Fix::None, {} };
        const auto loud = controller.getMasterLoudness();
        if (! loud.known || loud.integratedLufs <= -100.0f)
        {
            r.state = State::Neutral;
            r.sentence = "Not measured yet. It reads as soon as the mix plays, against " + lufs (loud.targetLufs) + " LUFS.";
        }
        else
        {
            r.sentence = lufs (loud.integratedLufs) + " LUFS integrated against a " + lufs (loud.targetLufs) + " target"
                       + (loud.truePeakDb > -100.0f ? ", true peak " + lufs (loud.truePeakDb) + " dBTP." : juce::String ("."));
            if (! loud.onTarget())
            {
                r.state = State::Warn;
                const auto move = controller.previewLoudnessMove();
                if (move.possible && ! controller.isLiveSafe()) { r.fix = Fix::RaiseLoudness; r.fixLabel = "Raise to target"; }
            }
        }
        out.push_back (r);
    }

    // ---- BYPASS
    {
        Row r { "BYPASS", {}, State::Ok, Fix::None, {} };
        if (controller.isBypassed())
        {
            r.state = State::Crit;
            r.sentence = "On. The stream hears the raw inputs, not the kept mix.";
            r.fix = Fix::BypassOff;
            r.fixLabel = "Turn off";
        }
        else r.sentence = "Off. The stream hears the kept mix.";
        out.push_back (r);
    }

    // ---- LIVE SAFE
    {
        Row r { "LIVE SAFE", {}, State::Ok, Fix::None, {} };
        if (! controller.isLiveSafe())
        {
            r.state = State::Warn;
            r.sentence = "Off. Turn it on before the service so nothing can re-tune or re-route by accident.";
            r.fix = Fix::LiveSafeOn;
            r.fixLabel = "Turn on";
        }
        else r.sentence = "On. Re-routes and re-tunes are locked; faders, mutes and recording still work.";
        out.push_back (r);
    }

    // ---- the autosave
    {
        Row r { "Autosave", {}, State::Ok, Fix::None, {} };
        const auto when = services.lastAutosave();
        if (services.autosaveFailing())
        {
            r.state = State::Crit;
            r.sentence = "The last autosave did not reach the disk. Save now, and check the disk.";
            r.fix = Fix::SaveNow;
            r.fixLabel = "Save now";
        }
        else if (when == juce::Time())
        {
            r.state = State::Neutral;
            r.sentence = "Nothing to save yet. The session saves itself as soon as anything changes.";
        }
        else r.sentence = "Saved " + when.toString (false, true, false, true) + ". A crash costs nothing.";
        out.push_back (r);
    }

    // ---- the broadcast checklist, when this service has one under way: what is still to
    // confirm. Read only - it is ticked by a person, on its own sheet.
    if (broadcastReadinessApplies (controller.getSession().purpose))
    {
        const auto& active = controller.getReadiness().active;
        if (! active.id.empty() && active.hasWork())
        {
            const auto prog = active.progress();
            const int left = juce::jmax (0, prog.applicable - prog.checked);
            Row r { "Broadcast checklist", {}, left > 0 ? State::Warn : State::Ok, Fix::OpenChecklist, "Open checklist" };
            r.sentence = left == 0 ? juce::String ("Every item is confirmed.")
                                   : juce::String (left) + " of " + juce::String (prog.applicable) + " still to confirm by hand.";
            out.push_back (r);
        }
    }
    return out;
}

int ReadyCheck::problems (const std::vector<Row>& rows)
{
    int n = 0;
    for (const auto& r : rows) if (r.state == State::Warn || r.state == State::Crit) ++n;
    return n;
}

// ---------------------------------------------------------------------------------- the sheet
ReadySheet::ReadySheet (MixController& c, AppServices& s) : controller (c), services (s)
{
    close.setIcon (Dine::Icon::Close);
    close.setPadX (6);
    close.setTooltip ("Close");
    close.onClick = [this] { if (auto f = onClose) f(); };
    live.setFontPx (12.5f);
    live.onClick = [this] { if (auto f = onGoToLive) f(); };
    checklist.setFontPx (12.0f);
    checklist.setTooltip ("The broadcast checklist: the things only a person can confirm, ticked by hand.");
    checklist.onClick = [this] { if (auto f = onOpenChecklist) f(); };
    done.setFontPx (12.5f);
    done.onClick = [this] { if (auto f = onClose) f(); };
    for (auto* b : { &close, &live, &done }) addAndMakeVisible (*b);
    addChildComponent (checklist);
    setWantsKeyboardFocus (true);
    refresh();
}

void ReadySheet::refresh()
{
    auto next = ReadyCheck::gather (controller, services);
    bool same = next.size() == rows.size();
    for (size_t i = 0; same && i < next.size(); ++i)
        same = next[i].title == rows[i].title && next[i].sentence == rows[i].sentence && next[i].state == rows[i].state
            && next[i].fix == rows[i].fix;
    bool rowHasIt = false;
    for (const auto& r : next) rowHasIt = rowHasIt || r.fix == ReadyCheck::Fix::OpenChecklist;
    checklist.setVisible (broadcastReadinessApplies (controller.getSession().purpose) && onOpenChecklist != nullptr && ! rowHasIt);
    if (same) return;
    rows = std::move (next);
    rebuildButtons();
    resized();
    repaint();
}

void ReadySheet::rebuildButtons()
{
    fixes.clear();
    for (const auto& r : rows)
    {
        auto b = std::make_unique<DineButton> (r.fixLabel, DineButton::Style::Standard);
        b->setFontPx (12.0f);
        const auto fix = r.fix;
        b->onClick = [this, fix] { if (auto f = onFix) f (fix); };
        b->setVisible (r.fix != ReadyCheck::Fix::None);
        addChildComponent (*b);
        fixes.push_back (std::move (b));
    }
}

int ReadySheet::rowHeight() const
{
    const int n = juce::jmax (1, int (rows.size()));
    return juce::jlimit (48, kRowH, (getHeight() - 30 - kHeadH - kFootH - 10) / n);
}

juce::Rectangle<int> ReadySheet::cardBounds() const
{
    const int h = kHeadH + int (rows.size()) * rowHeight() + kFootH + 10;
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (kCardW, getWidth() - 40), juce::jmin (h, getHeight() - 30));
}

void ReadySheet::resized()
{
    const auto card = cardBounds();
    close.setBounds (card.getRight() - 46, card.getY() + 22, 28, 28);
    auto list = card.withTrimmedTop (kHeadH).withTrimmedBottom (kFootH).reduced (26, 0);
    for (size_t i = 0; i < rows.size() && i < fixes.size(); ++i)
    {
        auto row = list.removeFromTop (rowHeight());
        const int w = fixes[i]->idealWidth() + 8;
        fixes[i]->setBounds (row.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::button));
    }
    auto foot = card.withTop (card.getBottom() - kFootH).reduced (26, 14);
    const int dw = juce::jmax (80, done.idealWidth());
    done.setBounds (foot.removeFromRight (dw));
    foot.removeFromRight (10);
    const int lw = juce::jmax (96, live.idealWidth());
    live.setBounds (foot.removeFromRight (lw));
    if (checklist.isVisible()) checklist.setBounds (foot.removeFromLeft (juce::jmin (foot.getWidth(), checklist.idealWidth() + 8)));
}

void ReadySheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.6f));
    const auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), 14.0f);

    auto head = card.withHeight (kHeadH).reduced (26, 18);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (17.0f, 700));
    Dine::drawText (g, "Ready to go live?", head.removeFromTop (22), juce::Justification::centredLeft, false);
    const int n = ReadyCheck::problems (rows);
    g.setColour (Dine::ink2);
    g.setFont (Dine::text (12.5f));
    Dine::drawText (g, n == 0 ? juce::String ("Everything is ready.")
                              : juce::String (n) + (n == 1 ? " thing" : " things") + " to check before you go live.",
                    head.removeFromTop (18), juce::Justification::centredLeft, false);

    auto list = card.withTrimmedTop (kHeadH).withTrimmedBottom (kFootH).reduced (26, 0);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& r = rows[i];
        auto row = list.removeFromTop (rowHeight());
        g.setColour (Dine::hair);
        g.fillRect (row.removeFromBottom (1));
        auto lamp = row.removeFromLeft (22).withSizeKeepingCentre (18, 18).toFloat();
        const auto tint = r.state == ReadyCheck::State::Ok ? Dine::accent : r.state == ReadyCheck::State::Warn ? Dine::warn
                        : r.state == ReadyCheck::State::Crit ? Dine::crit : Dine::ink4;
        g.setColour (tint);
        g.fillEllipse (lamp);
        g.setColour (Dine::window);
        if (r.state == ReadyCheck::State::Ok)
        {
            juce::Path tick;
            tick.startNewSubPath (lamp.getX() + 5.0f, lamp.getCentreY());
            tick.lineTo (lamp.getX() + 8.0f, lamp.getCentreY() + 3.0f);
            tick.lineTo (lamp.getRight() - 4.5f, lamp.getCentreY() - 3.5f);
            g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (r.state != ReadyCheck::State::Neutral)
        {
            g.setFont (Dine::text (12.0f, 800));
            Dine::drawText (g, "!", lamp.toNearestInt(), juce::Justification::centred, false);
        }
        row.removeFromLeft (12);
        if (i < fixes.size() && fixes[i]->isVisible()) row.removeFromRight (fixes[i]->getWidth() + 14);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, r.title, row.removeFromTop (row.getHeight() / 2).withTrimmedTop (4), juce::Justification::bottomLeft, false);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.0f));
        Dine::drawFittedText (g, r.sentence, row.withTrimmedTop (2), juce::Justification::topLeft, 2, 0.9f);
    }
}

void ReadySheet::mouseUp (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition())) if (auto f = onClose) f();
}

bool ReadySheet::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { if (auto f = onClose) f(); return true; }
    return false;
}

} // namespace livemix
