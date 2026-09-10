#include "ChannelTuneSheet.h"
#include "Mix/MixPlanner.h"
#include <cmath>

namespace livemix
{

namespace
{
    constexpr int kCardW = 560;
    constexpr int kListenH = 238;      // the ring, what to play, and Cancel
    constexpr int kLineH = 30;         // one line of the plan
    constexpr int kLineWhyH = 54;      // ... and one with its why under it
    constexpr int kListMaxH = 152;

    juce::String db1 (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1) + " dB";
    }
}

ChannelTuneSheet::ChannelTuneSheet (MixController& c, int s) : controller (c), strip (s)
{
    const auto& graph = controller.getGraph();
    if (strip >= 0 && strip < graph.numStrips())
    {
        name = juce::String (graph.strips[size_t (strip)].name);
        icon = Dine::iconFor (graph.strips[size_t (strip)].icon, graph.strips[size_t (strip)].role);
    }

    for (auto* b : { &cancel, &before, &after, &keep, &revert, &inspect }) addChildComponent (*b);
    before.setClickingTogglesState (false);
    after.setClickingTogglesState (false);
    before.setFontPx (11.5f);
    after.setFontPx (11.5f);
    keep.setFontPx (12.0f);
    revert.setFontPx (12.0f);
    inspect.setFontPx (11.5f);

    cancel.onClick  = [this] { controller.abortTuneMix(); if (onClose) onClose(); };
    before.onClick  = [this] { controller.setCompare (MixController::Compare::Before); updateControls(); repaint(); };
    after.onClick   = [this] { controller.setCompare (MixController::Compare::After); updateControls(); repaint(); };
    keep.onClick    = [this]
    {
        controller.keepPlan();
        if (onToast) onToast (name + " is tuned. Everything else in the mix is exactly as it was.");
        if (onClose) onClose();
    };
    revert.onClick  = [this]
    {
        controller.revertPlan();
        if (onToast) onToast (name + " is back the way it was.");
        if (onClose) onClose();
    };
    inspect.onClick = [this] { if (onOpenInspector) onOpenInspector(); };

    setInterceptsMouseClicks (true, true);
    updateControls();
}

bool ChannelTuneSheet::previewing() const
{
    return controller.getStage() == MixController::Stage::Preview
        && controller.hasPlan() && controller.getTuningStrip() == strip;
}

// The card is as tall as what it holds: a listen is a ring and a sentence, a plan is as
// many lines as it wrote. A sheet with empty space in it reads as a sheet still loading.
juce::Rectangle<int> ChannelTuneSheet::cardBounds() const
{
    const int w = juce::jmin (kCardW, getWidth() - 40);
    int h = kListenH;
    if (previewing())
    {
        const auto* plan = controller.getPlan();
        const bool changed = plan != nullptr && ! plan->noChangeRequired;
        h = (changed ? 210 : 172) + listHeight();
    }
    h = juce::jmin (h, juce::jmax (200, getHeight() - 20));
    return { (getWidth() - w) / 2, 0, w, h };
}

// The sheet follows the controller: while it listens it shows the listen, the frame the
// plan arrives it shows what it proposes. A listen that heard nothing closes it, because
// the toast has already said so.
void ChannelTuneSheet::refresh()
{
    const auto stage = controller.getStage();
    const bool busy = (stage == MixController::Stage::Listening || stage == MixController::Stage::Planning)
                      && controller.getTuningStrip() == strip;
    const bool preview = previewing();
    if (! busy && ! preview) { if (onClose) onClose(); return; }
    if (preview != wasPreviewing) { wasPreviewing = preview; updateControls(); resized(); }
    repaint();
}

void ChannelTuneSheet::updateControls()
{
    const bool preview = previewing();
    const auto* plan = controller.getPlan();
    // Nothing to compare and nothing to put back when the channel was already right: the
    // one thing left to do is close the sheet.
    const bool changed = preview && plan != nullptr && ! plan->noChangeRequired;
    cancel.setVisible (! preview);
    keep.setVisible (preview);
    inspect.setVisible (preview);
    for (auto* b : { &before, &after, &revert }) b->setVisible (changed);
    keep.setButtonText (changed ? "KEEP" : "Done");
    keep.setCaps (changed);
    if (changed)
    {
        const bool showingAfter = controller.getCompare() == MixController::Compare::After;
        before.setToggleState (! showingAfter, juce::dontSendNotification);
        after.setToggleState (showingAfter, juce::dontSendNotification);
    }
}

// What the plan says about this channel: its own notes first (what it did), then the
// decisions it made in mix context (what it did it for). The list is only ever as long as
// the card can draw, so the sheet is never taller than what is in it.
std::vector<std::pair<juce::String, juce::String>> ChannelTuneSheet::lines() const
{
    std::vector<std::pair<juce::String, juce::String>> out;
    const auto* plan = controller.getPlan();
    if (plan == nullptr) return out;
    int height = 4;
    auto fits = [&height] (bool withWhy)
    {
        const int h = withWhy ? kLineWhyH : kLineH;
        if (height + h > kListMaxH) return false;
        height += h;
        return true;
    };
    for (const auto& n : plan->notes)
    {
        if (! fits (false)) return out;
        out.push_back ({ juce::String (n), {} });
    }
    for (const auto& r : plan->relationships)
    {
        if (! fits (true)) return out;
        out.push_back ({ juce::String (r.what), juce::String (r.why) });
    }
    return out;
}

int ChannelTuneSheet::listHeight() const
{
    int h = 4;
    for (const auto& line : lines()) h += line.second.isEmpty() ? kLineH : kLineWhyH;
    return juce::jmax (46, h);
}

void ChannelTuneSheet::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0c0d0e).withAlpha (0.55f));

    auto card = cardBounds().toFloat();
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 40, { 0, 16 }).drawForRectangle (g, card.toNearestInt());
    {
        juce::Path p;
        p.addRoundedRectangle (card.getX(), card.getY() - 12.0f, card.getWidth(), card.getHeight() + 12.0f,
                               Dine::Radius::window, Dine::Radius::window, false, false, true, true);
        g.setColour (Dine::sheet);
        g.fillPath (p);
        g.setColour (Dine::hairStrong);
        g.strokePath (p, juce::PathStrokeType (0.5f));
    }

    auto r = cardBounds().reduced (26, 22);

    // ---- who this is about, on every state: one channel, named, with its own icon.
    {
        auto head = r.removeFromTop (18);
        Dine::drawIcon (g, icon, head.removeFromLeft (16).toFloat().withSizeKeepingCentre (15.0f, 15.0f), Dine::accent);
        head.removeFromLeft (8);
        g.setColour (Dine::accent);
        g.setFont (Dine::text (11.5f, 700).withExtraKerningFactor (0.08f));
        g.drawText ("TUNE CHANNEL", head.removeFromLeft (110), juce::Justification::centredLeft);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f));
        g.drawText (name, head, juce::Justification::centredRight, true);
    }
    r.removeFromTop (10);

    if (! previewing())
    {
        const bool waiting = controller.isWaitingForBand();
        const bool planning = controller.getStage() == MixController::Stage::Planning;
        const float progress = planning ? 1.0f : controller.getListenProgress();

        auto ring = r.removeFromLeft (104).removeFromTop (104).toFloat();
        {
            const float radius = 44.0f, thickness = 5.0f;
            auto centre = ring.getCentre();
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.strokePath (track, juce::PathStrokeType (thickness));
            if (progress > 0.0f)
            {
                juce::Path arc;
                arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.0f,
                                   juce::MathConstants<float>::twoPi * juce::jlimit (0.02f, 1.0f, progress), true);
                g.setColour (Dine::accent);
                g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (24.0f, 500));
            g.drawText (waiting ? Glyph::dash() : juce::String (int (std::round (progress * 100.0f))),
                        ring.withTrimmedBottom (22.0f).toNearestInt(), juce::Justification::centred);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (10.0f));
            g.drawText (waiting ? "waiting" : "% listened",
                        ring.withTrimmedTop (ring.getHeight() * 0.5f + 8.0f).withHeight (16.0f).toNearestInt(),
                        juce::Justification::centred);
        }

        auto text = r.withTrimmedLeft (104 + 20);
        auto title = text.removeFromTop (24);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (18.0f, 600));
        g.drawText (planning ? "Building this channel" : waiting ? "Waiting for " + name : "Listening to " + name,
                    title, juce::Justification::topLeft, true);
        text.removeFromTop (6);
        auto body = text.removeFromTop (60);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText (waiting ? "Play this source the way it is played in the service. DLIVE starts as soon as it hears it, "
                                    "and the rest of the mix keeps running underneath."
                                  : "Keep playing. Only " + name + " is decided from this listen - every other channel, the groups "
                                    "and the master stay exactly where they are.",
                          body, juce::Justification::topLeft, 4);

        // Heard yet? The one thing the listen can say before it is finished.
        {
            const bool heardIt = controller.stripHeard (strip);
            auto chipRow = text.removeFromTop (24);
            const juce::String label = heardIt ? "Heard" : "waiting";
            const float w = Dine::pillWidth (label, true);
            Dine::drawPill (g, chipRow.removeFromLeft (int (w)).withSizeKeepingCentre (int (w), 22).toFloat(),
                            label, heardIt ? Dine::ok : Dine::ink3, heardIt ? Dine::Icon::Check : Dine::Icon::Target);
        }
        return;
    }

    // ---- the plan, for this channel
    const auto* plan = controller.getPlan();
    if (plan == nullptr) return;

    g.setColour (Dine::ink);
    g.setFont (Dine::text (19.0f, 600));
    g.drawText (juce::String (plan->headline), r.removeFromTop (24), juce::Justification::centredLeft, true);

    r.removeFromTop (4);
    {
        juce::StringArray parts;
        if (plan->parametersChanged > 0)
            parts.add (juce::String (plan->parametersChanged) + (plan->parametersChanged == 1 ? " setting" : " settings"));
        if (plan->fadersChanged > 0 && strip < plan->proposed.numStrips)
            parts.add ("level " + db1 (plan->proposed.strips[size_t (strip)].faderDb));
        if (plan->gainsChanged > 0 && strip < plan->proposed.numStrips)
            parts.add ("gain " + db1 (plan->proposed.strips[size_t (strip)].inputGainDb));
        if (plan->sendsChanged > 0)
            parts.add (juce::String (plan->sendsChanged) + (plan->sendsChanged == 1 ? " send" : " sends"));
        const juce::String counts = parts.isEmpty() ? juce::String ("nothing changed")
                                                    : parts.joinIntoString ("  " + juce::String (Glyph::dot()) + "  ");
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.0f));
        g.drawText (counts, r.removeFromTop (16), juce::Justification::centredLeft, true);
    }

    r.removeFromTop (10);
    auto list = r.removeFromTop (juce::jmax (0, r.getHeight() - (before.isVisible() ? 84 : 46)));
    Dine::fillRounded (g, list.toFloat(), juce::Colours::black.withAlpha (0.20f), 8.0f);
    auto inner = list.reduced (14, 2);
    bool first = true;
    for (const auto& line : lines())
    {
        const int h = line.second.isEmpty() ? kLineH : kLineWhyH;
        if (inner.getHeight() < h) break;
        auto row = inner.removeFromTop (h);
        if (! first) Dine::drawRule (g, row.withHeight (1), Dine::hairSoft);
        first = false;
        row = row.reduced (0, 7);
        Dine::drawIcon (g, Dine::Icon::Check, row.removeFromLeft (14).toFloat().withSizeKeepingCentre (13.0f, 13.0f).withY (float (row.getY()) + 1.0f), Dine::accent);
        row.removeFromLeft (10);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        g.drawText (line.first, row.removeFromTop (16), juce::Justification::topLeft, true);
        if (line.second.isNotEmpty())
        {
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.5f));
            g.drawFittedText (line.second, row, juce::Justification::topLeft, 2);
        }
    }

    if (! before.isVisible())
    {
        Dine::drawRule (g, cardBounds().reduced (26, 0).withY (keep.getBounds().getY() - 14).withHeight (1), Dine::hair);
        return;
    }

    auto ab = juce::Rectangle<int> (before.getBounds().getX(), before.getBounds().getY(),
                                    before.getWidth() + after.getWidth(), before.getHeight()).expanded (2, 2);
    Dine::fillRounded (g, ab.toFloat(), juce::Colours::white.withAlpha (0.08f), 7.0f);
    auto note = juce::Rectangle<int> (ab.getRight() + 12, ab.getY(), cardBounds().getRight() - 26 - ab.getRight() - 12, ab.getHeight());
    g.setColour (Dine::ink2);
    g.setFont (Dine::text (11.5f));
    g.drawText (controller.getCompare() == MixController::Compare::After
                    ? "AFTER is this channel as proposed."
                    : "BEFORE is this channel as it was.",
                note, juce::Justification::centredLeft, true);

    Dine::drawRule (g, cardBounds().reduced (26, 0).withY (keep.getBounds().getY() - 14).withHeight (1), Dine::hair);
}

void ChannelTuneSheet::resized()
{
    auto r = cardBounds().reduced (26, 22);
    if (! previewing())
    {
        auto foot = r.removeFromBottom (Dine::Metric::button);
        cancel.setBounds (foot.removeFromRight (juce::jmax (80, cancel.idealWidth())));
        return;
    }

    auto foot = r.removeFromBottom (Dine::Metric::button);
    const int kw = juce::jmax (86, keep.idealWidth());
    keep.setBounds (foot.removeFromRight (kw));
    if (revert.isVisible())
    {
        foot.removeFromRight (8);
        const int rw = juce::jmax (86, revert.idealWidth());
        revert.setBounds (foot.removeFromRight (rw));
    }
    inspect.setBounds (foot.removeFromLeft (juce::jmax (120, inspect.idealWidth())));

    if (! before.isVisible()) return;
    auto ab = r.removeFromBottom (Dine::Metric::control + 14).removeFromTop (Dine::Metric::control);
    before.setBounds (ab.removeFromLeft (juce::jmax (80, before.idealWidth())));
    after.setBounds (ab.removeFromLeft (juce::jmax (72, after.idealWidth())));
}

// Clicking the dimmed page behind the sheet cancels a listen. A proposal is not dismissed
// that way: what you are hearing is the proposed channel, so it takes KEEP or REVERT to
// say which one you want.
void ChannelTuneSheet::mouseUp (const juce::MouseEvent& e)
{
    if (previewing() || cardBounds().contains (e.getPosition())) return;
    controller.abortTuneMix();
    if (onClose) onClose();
}

} // namespace livemix
