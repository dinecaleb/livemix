#include "ChatSheet.h"
#include "MixAI/MixBuddy.h"

namespace livemix
{

namespace
{
    constexpr int kBubblePad = 12;
    constexpr int kChipH = 24;
    constexpr int kChipGap = 6;
    constexpr float kChipPx = 11.5f;
}

// The conversation, drawn rather than built from components: a service's worth of turns is
// a lot of child components for something that only ever scrolls. An answer's buttons are
// drawn too, and a click on one is looked up by where it landed.
class ChatSheet::Transcript : public juce::Component
{
public:
    explicit Transcript (MixController& c) : controller (c) {}

    std::function<void (const BuddyAction&)> onAction;

    // Lays the turns out and returns the height they need, so the viewport can size itself.
    int layout (int width)
    {
        rows.clear();
        chips.clear();
        int y = 0;
        const auto& chat = controller.getChat();
        if (chat.empty())
        {
            // An empty panel is where somebody learns what this is for, so the examples are
            // real questions that are guaranteed an answer rather than a blinking caret.
            Row intro;
            intro.kind = Row::Kind::Intro;
            intro.text = "Ask how to do something in DLIVE, or why something sounds the way it does. For example:";
            intro.lines = MixBuddy::examples();
            const int textWidth = width - 2 * kBubblePad;
            intro.bounds = { 0, y, width, 2 * kBubblePad + textHeight (intro.text, textWidth, 13.0f) + 6
                                             + int (intro.lines.size()) * 20 };
            y += intro.bounds.getHeight() + 8;
            rows.push_back (intro);
        }
        for (size_t t = 0; t < chat.size(); ++t)
        {
            const auto& turn = chat[t];
            Row r;
            r.kind = turn.fromEngineer ? Row::Kind::Engineer : (turn.failed ? Row::Kind::Refused : Row::Kind::Dine);
            r.text = turn.text;
            for (const auto& d : turn.detail) r.lines.push_back (d);

            const int bubbleWidth = juce::jmax (140, width - (turn.fromEngineer ? 60 : 24));
            const int textWidth = bubbleWidth - 2 * kBubblePad;
            int h = 2 * kBubblePad + textHeight (r.text, textWidth, 13.0f);
            for (const auto& l : r.lines) h += textHeight (l, textWidth - 12, 11.5f) + 4;
            if (! r.lines.empty()) h += 6;

            // The buttons, flowed left to right and onto another line when they run out of room.
            if (! turn.actions.empty())
            {
                h += 8;
                int x = 0, lineY = h;
                const auto font = Dine::text (kChipPx);
                for (const auto& action : turn.actions)
                {
                    const int w = juce::jmin (textWidth, Dine::textWidth (font, juce::String (action.label)) + 20);
                    if (x > 0 && x + w > textWidth) { x = 0; lineY += kChipH + kChipGap; }
                    Chip chip;
                    chip.action = action;
                    chip.row = rows.size();
                    chip.local = { kBubblePad + x, lineY - 0, w, kChipH };
                    chips.push_back (chip);
                    x += w + kChipGap;
                }
                h = lineY + kChipH + kBubblePad;
            }
            r.bounds = { turn.fromEngineer ? width - bubbleWidth : 0, y, bubbleWidth, h };
            for (auto& chip : chips)
                if (chip.row == rows.size()) chip.bounds = chip.local.translated (r.bounds.getX(), r.bounds.getY());
            y += h + 8;
            rows.push_back (r);
        }
        setSize (width, juce::jmax (1, y));
        return y;
    }

    void paint (juce::Graphics& g) override
    {
        for (const auto& r : rows)
        {
            const auto box = r.bounds.toFloat();
            switch (r.kind)
            {
                case Row::Kind::Engineer: Dine::fillRounded (g, box, Dine::control, Dine::Radius::card); break;
                case Row::Kind::Refused:  Dine::fillRounded (g, box, Dine::refuse, Dine::Radius::card); break;
                default:                  Dine::fillRounded (g, box, Dine::tile, Dine::Radius::card); break;
            }

            auto inner = r.bounds.reduced (kBubblePad);
            if (r.kind == Row::Kind::Intro)
            {
                g.setColour (Dine::ink2);
                g.setFont (Dine::text (13.0f));
                const int th = textHeight (r.text, inner.getWidth(), 13.0f);
                Dine::drawFittedText (g, r.text, inner.removeFromTop (th), juce::Justification::topLeft, 4);
                inner.removeFromTop (6);
                g.setFont (Dine::text (12.0f));
                for (const auto& l : r.lines)
                {
                    g.setColour (Dine::ink3);
                    Dine::drawFittedText (g, juce::String (Glyph::dot()) + "  " + juce::String (l),
                                          inner.removeFromTop (20), juce::Justification::topLeft, 1);
                }
                continue;
            }

            g.setColour (r.kind == Row::Kind::Refused ? Dine::warn : Dine::ink);
            g.setFont (Dine::text (13.0f));
            const int th = textHeight (r.text, inner.getWidth(), 13.0f);
            Dine::drawFittedText (g, r.text, inner.removeFromTop (th), juce::Justification::topLeft, 40);

            if (! r.lines.empty()) inner.removeFromTop (6);
            g.setFont (Dine::text (11.5f));
            for (const auto& l : r.lines)
            {
                const int lh = textHeight (l, inner.getWidth() - 12, 11.5f);
                auto line = inner.removeFromTop (lh + 4);
                g.setColour (Dine::accent.withAlpha (0.55f));
                g.fillRect (line.getX(), line.getY() + 4, 2, juce::jmax (8, lh - 4));
                g.setColour (Dine::ink3);
                Dine::drawFittedText (g, l, line.withTrimmedLeft (12), juce::Justification::topLeft, 40);
            }
        }

        g.setFont (Dine::text (kChipPx));
        for (size_t i = 0; i < chips.size(); ++i)
        {
            const auto& c = chips[i];
            const bool hot = int (i) == hover;
            Dine::fillRounded (g, c.bounds.toFloat(), hot ? Dine::control : Dine::item, Dine::Radius::control);
            g.setColour (Dine::hairSoft);
            g.drawRoundedRectangle (c.bounds.toFloat().reduced (0.5f), Dine::Radius::control, 1.0f);
            g.setColour (c.action.kind == BuddyActionKind::AskForChange ? Dine::accent : Dine::ink);
            Dine::drawFittedText (g, juce::String (c.action.label), c.bounds.reduced (10, 0), juce::Justification::centred, 1);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int now = chipAt (e.getPosition());
        if (now != hover) { hover = now; repaint(); }
        setMouseCursor (now >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { if (hover >= 0) { hover = -1; repaint(); } }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = chipAt (e.getPosition());
        if (i >= 0 && onAction) onAction (chips[size_t (i)].action);
    }

private:
    struct Row
    {
        enum class Kind { Intro, Engineer, Dine, Refused };
        Kind kind = Kind::Dine;
        juce::String text;
        std::vector<std::string> lines;
        juce::Rectangle<int> bounds;
    };
    struct Chip
    {
        BuddyAction action;
        size_t row = 0;
        juce::Rectangle<int> local, bounds;
    };

    int chipAt (juce::Point<int> p) const
    {
        for (size_t i = 0; i < chips.size(); ++i) if (chips[i].bounds.contains (p)) return int (i);
        return -1;
    }

    static int textHeight (const juce::String& text, int width, float px)
    {
        if (width < 20) return 18;
        juce::AttributedString a;
        a.append (text, Dine::text (px));
        juce::TextLayout layout;
        layout.createLayout (a, float (width));
        return juce::jmax (18, int (std::ceil (layout.getHeight())));
    }
    static int textHeight (const std::string& text, int width, float px) { return textHeight (juce::String (text), width, px); }

    MixController& controller;
    std::vector<Row> rows;
    std::vector<Chip> chips;
    int hover = -1;
};

// ---------------------------------------------------------------------------

ChatSheet::ChatSheet (MixController& c) : controller (c)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    transcript = std::make_unique<Transcript> (controller);
    transcript->onAction = [this] (const BuddyAction& a) { if (onAction) onAction (a); refresh(); updateControls(); };
    scroller = std::make_unique<juce::Viewport>();
    scroller->setViewedComponent (transcript.get(), false);
    scroller->setScrollBarsShown (true, false);
    Dine::nativeScrolling (*scroller);
    addAndMakeVisible (*scroller);

    addAndMakeVisible (input);
    input.setMultiLine (false, false);
    input.setJustification (juce::Justification::centredLeft);
    input.setReturnKeyStartsNewLine (false);
    input.setTextToShowWhenEmpty ("Why can't I hear the lead vocal?", Dine::ink4);
    input.setFont (Dine::text (13.5f));
    Dine::styleTextEditor (input, Dine::tile, true);
    input.onReturnKey = [this] { send(); };

    for (auto* b : { &sendButton, &keepButton, &revertButton, &compareButton, &close })
        addAndMakeVisible (*b);
    for (auto* b : { &keepButton, &revertButton, &compareButton })
    {
        b->setFontPx (11.0f);
        b->setPadX (8);
    }
    close.setFontPx (12.0f);
    close.setPadX (10);

    sendButton.onClick = [this] { send(); };
    keepButton.setCaps (true);
    revertButton.setCaps (true);
    compareButton.setCaps (true);
    keepButton.setTooltip ("Make the proposed change part of the mix.");
    revertButton.setTooltip ("Put the mix back the way it was before the proposal.");
    compareButton.setTooltip ("Hear the mix before the proposal, then after it.");

    keepButton.onClick = [this] { controller.keepPlan(); updateControls(); };
    revertButton.onClick = [this] { controller.revertPlan(); updateControls(); };
    compareButton.onClick = [this]
    {
        controller.setCompare (controller.getCompare() == MixController::Compare::Before
                                   ? MixController::Compare::After : MixController::Compare::Before);
        updateControls();
    };
    close.onClick = [this] { if (onClose) onClose(); };

    updateControls();
}

ChatSheet::~ChatSheet() = default;

void ChatSheet::takeFocus() { input.grabKeyboardFocus(); }

juce::Rectangle<int> ChatSheet::cardBounds() const
{
    return getLocalBounds();
}

void ChatSheet::ask (const juce::String& question)
{
    const auto text = question.trim();
    if (text.isEmpty()) return;
    controller.askBuddy (text.toStdString());
    refresh();
    updateControls();
}

void ChatSheet::send()
{
    const auto text = input.getText().trim();
    if (text.isEmpty()) return;
    input.clear();
    ask (text);
}

void ChatSheet::updateControls()
{
    const bool busy = controller.isChatBusy();
    // KEEP / REVERT / BEFORE belong to a proposal this panel asked for; any other proposal is
    // decided where it was made (TUNE), so this panel does not grow a second set of them.
    const bool proposing = controller.hasBuddyProposal();
    sendButton.setButtonText (busy ? "Working" + juce::String (Glyph::ellip()) : "Ask");

    keepButton.setVisible (proposing);
    revertButton.setVisible (proposing);
    compareButton.setVisible (proposing);
    compareButton.setButtonText (controller.getCompare() == MixController::Compare::Before ? "AFTER" : "BEFORE");
    compareButton.setStyle (DineButton::Style::Toggle);
    compareButton.setToggleState (controller.getCompare() == MixController::Compare::Before,
                                  juce::dontSendNotification);
    resized();
    repaint();
}

void ChatSheet::refresh()
{
    const auto& chat = controller.getChat();
    const bool busy = controller.isChatBusy();
    const bool proposing = controller.hasBuddyProposal();
    if (chat.size() != shownTurns || busy != wasBusy || proposing != wasProposing)
    {
        shownTurns = chat.size();
        wasBusy = busy;
        wasProposing = proposing;
        transcript->layout (scroller->getMaximumVisibleWidth());
        // A new turn should be the one you are looking at.
        scroller->setViewPosition (0, juce::jmax (0, transcript->getHeight() - scroller->getHeight()));
        updateControls();
    }
}

void ChatSheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::sheet);
    g.setColour (Dine::hairSoft);
    g.fillRect (getLocalBounds().removeFromLeft (1));   // the panel's edge against the workspace
    auto inner = getLocalBounds().reduced (18, 16);
    auto head = inner.removeFromTop (26);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (14.0f));
    Dine::drawText (g, "Mix Buddy", head.withTrimmedRight (70), juce::Justification::centredLeft);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.5f));
    Dine::drawText (g, "Help, in plain words", head.withTrimmedLeft (86).withTrimmedRight (70), juce::Justification::centredLeft, true);

    // What it is for and what it is not, always on screen.
    {
        auto note = inner.withTrimmedTop (10).removeFromTop (kNoteH);
        Dine::fillRounded (g, note.toFloat(), Dine::item, Dine::Radius::control);
        auto r = note.reduced (12, 9);
        g.setFont (Dine::text (11.5f));
        g.setColour (Dine::ink2);
        Dine::drawFittedText (g, "Ask how to do something, or why something sounds wrong. Mix Buddy reads the session as it "
                                 "is right now and says what it finds.",
                              r.removeFromTop (r.getHeight() / 2), juce::Justification::topLeft, 3, 1.0f);
        g.setColour (Dine::ink3);
        Dine::drawFittedText (g, "It never changes the mix by itself. TUNE MIX improves a mix; Autopilot holds one.",
                              r, juce::Justification::topLeft, 3, 1.0f);
    }

    auto foot = getLocalBounds().reduced (18, 14).removeFromBottom (16);
    g.setColour (Dine::ink4);
    g.setFont (Dine::text (10.5f));
    Dine::drawText (g, controller.hasBuddyProposal() ? juce::String ("A proposed change is on BEFORE / AFTER. Nothing is kept until you press KEEP.")
                                                    : juce::String ("Built into DLIVE. Works offline."),
                    foot, juce::Justification::centredLeft, true);
}

void ChatSheet::resized()
{
    auto inner = cardBounds().reduced (18, 16);
    inner.removeFromTop (26 + 10 + kNoteH + 10);
    inner.removeFromBottom (16 + 8);
    close.setBounds (cardBounds().reduced (18, 16).removeFromTop (26).removeFromRight (juce::jmax (56, close.idealWidth())));

    if (keepButton.isVisible())
    {
        auto actions = inner.removeFromBottom (Dine::Metric::button + 4);
        keepButton.setBounds (actions.removeFromLeft (juce::jmax (56, keepButton.idealWidth())).withHeight (Dine::Metric::button));
        actions.removeFromLeft (6);
        revertButton.setBounds (actions.removeFromLeft (juce::jmax (56, revertButton.idealWidth())).withHeight (Dine::Metric::button));
        actions.removeFromLeft (6);
        compareButton.setBounds (actions.removeFromLeft (juce::jmax (56, compareButton.idealWidth())).withHeight (Dine::Metric::button));
        inner.removeFromBottom (10);
    }

    auto entry = inner.removeFromBottom (32);
    sendButton.setBounds (entry.removeFromRight (juce::jmax (72, sendButton.idealWidth())).withSizeKeepingCentre (
                              juce::jmax (72, sendButton.idealWidth()), Dine::Metric::button));
    entry.removeFromRight (8);
    input.setBounds (entry);

    inner.removeFromBottom (10);
    scroller->setBounds (inner);
    transcript->layout (scroller->getMaximumVisibleWidth());
}

bool ChatSheet::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

} // namespace livemix

void livemix::ChatSheet::lookAndFeelChanged() { Dine::styleTextEditor (input, Dine::tile, true); }
