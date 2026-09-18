#include "ChatSheet.h"
#include "MixAI/MixRequestParser.h"

namespace livemix
{

namespace
{
    constexpr int kBubblePad = 12;
}

// The conversation, drawn rather than built from components: a service's worth of turns is
// a lot of child components for something that only ever scrolls.
class ChatSheet::Transcript : public juce::Component
{
public:
    explicit Transcript (MixController& c) : controller (c) {}

    // Lays the turns out and returns the height they need, so the viewport can size itself.
    int layout (int width)
    {
        rows.clear();
        int y = 0;
        const auto& chat = controller.getChat();
        if (chat.empty())
        {
            // An empty chat is where somebody learns what this can do, so the examples are
            // real sentences that are guaranteed to work rather than a blinking caret.
            Row intro;
            intro.kind = Row::Kind::Intro;
            intro.text = "Say what you want and Mix Buddy will change the mix. For example:";
            intro.lines = mixRequestExamples();
            intro.bounds = { 0, y, width, 42 + int (intro.lines.size()) * 20 };
            y += intro.bounds.getHeight() + 8;
            rows.push_back (intro);
        }
        for (const auto& turn : chat)
        {
            Row r;
            r.kind = turn.fromEngineer ? Row::Kind::Engineer : (turn.failed ? Row::Kind::Refused : Row::Kind::Dine);
            r.text = turn.text;
            for (const auto& d : turn.detail) r.lines.push_back (d);

            const int bubbleWidth = juce::jmax (140, width - (turn.fromEngineer ? 60 : 24));
            const int textWidth = bubbleWidth - 2 * kBubblePad;
            int h = 2 * kBubblePad + textHeight (r.text, textWidth, 13.0f);
            for (const auto& l : r.lines) h += textHeight (l, textWidth - 12, 11.5f) + 4;
            if (! r.lines.empty()) h += 6;
            r.bounds = { turn.fromEngineer ? width - bubbleWidth : 0, y, bubbleWidth, h };
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
                case Row::Kind::Engineer:
                    Dine::fillRounded (g, box, Dine::control, Dine::Radius::card);
                    break;
                case Row::Kind::Refused:
                    Dine::fillRounded (g, box, Dine::refuse, Dine::Radius::card);
                    break;
                default:
                    Dine::fillRounded (g, box, Dine::tile, Dine::Radius::card);
                    break;
            }

            auto inner = r.bounds.reduced (kBubblePad);
            if (r.kind == Row::Kind::Intro)
            {
                g.setColour (Dine::ink2);
                g.setFont (Dine::text (13.0f));
                g.drawFittedText (r.text, inner.removeFromTop (20), juce::Justification::topLeft, 1);
                inner.removeFromTop (6);
                g.setFont (Dine::text (12.0f));
                for (const auto& l : r.lines)
                {
                    g.setColour (Dine::ink3);
                    g.drawFittedText (juce::String (Glyph::dot()) + "  " + juce::String (l),
                                      inner.removeFromTop (20), juce::Justification::topLeft, 1);
                }
                continue;
            }

            g.setColour (r.kind == Row::Kind::Refused ? Dine::warn : Dine::ink);
            g.setFont (Dine::text (13.0f));
            const int th = textHeight (r.text, inner.getWidth(), 13.0f);
            g.drawFittedText (r.text, inner.removeFromTop (th), juce::Justification::topLeft, 40);

            if (! r.lines.empty()) inner.removeFromTop (6);
            g.setFont (Dine::text (11.5f));
            for (const auto& l : r.lines)
            {
                const int lh = textHeight (l, inner.getWidth() - 12, 11.5f);
                auto line = inner.removeFromTop (lh + 4);
                g.setColour (Dine::accent.withAlpha (0.55f));
                g.fillRect (line.getX(), line.getY() + 4, 2, juce::jmax (8, lh - 4));
                g.setColour (Dine::ink3);
                g.drawFittedText (l, line.withTrimmedLeft (12), juce::Justification::topLeft, 40);
            }
        }
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
};

// ---------------------------------------------------------------------------

ChatSheet::ChatSheet (MixController& c) : controller (c)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    transcript = std::make_unique<Transcript> (controller);
    scroller = std::make_unique<juce::Viewport>();
    scroller->setViewedComponent (transcript.get(), false);
    scroller->setScrollBarsShown (true, false);
    Dine::nativeScrolling (*scroller);
    addAndMakeVisible (*scroller);

    addAndMakeVisible (input);
    input.setMultiLine (false, false);
    input.setJustification (juce::Justification::centredLeft);
    input.setReturnKeyStartsNewLine (false);
    input.setTextToShowWhenEmpty ("Bring the lead vocal forward", Dine::ink4);
    input.setFont (Dine::text (13.5f));
    input.setColour (juce::TextEditor::backgroundColourId, Dine::tile);
    input.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::focusedOutlineColourId, Dine::accent.withAlpha (0.6f));
    input.setColour (juce::TextEditor::textColourId, Dine::ink);
    input.onReturnKey = [this] { send(); };

    for (auto* b : { &sendButton, &keepButton, &revertButton, &compareButton, &undoButton, &redoButton, &close })
        addAndMakeVisible (*b);
    for (auto* b : { &keepButton, &revertButton, &compareButton, &undoButton, &redoButton })
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
    keepButton.setTooltip ("Make this change part of the mix.");
    revertButton.setTooltip ("Put the mix back the way it was before this change.");
    compareButton.setTooltip ("Hear the mix before the change, then after it.");
    undoButton.setTooltip ("Step back through the changes this conversation made.");

    keepButton.onClick = [this] { controller.keepPlan(); updateControls(); };
    revertButton.onClick = [this] { controller.revertPlan(); updateControls(); };
    compareButton.onClick = [this]
    {
        controller.setCompare (controller.getCompare() == MixController::Compare::Before
                                   ? MixController::Compare::After : MixController::Compare::Before);
        updateControls();
    };
    undoButton.onClick = [this] { controller.undoMix(); updateControls(); };
    redoButton.onClick = [this] { controller.redoMix(); updateControls(); };
    close.onClick = [this] { if (onClose) onClose(); };

    updateControls();
}

ChatSheet::~ChatSheet() = default;

void ChatSheet::takeFocus() { input.grabKeyboardFocus(); }

juce::Rectangle<int> ChatSheet::cardBounds() const
{
    return getLocalBounds();
}

void ChatSheet::send()
{
    const auto text = input.getText().trim();
    if (text.isEmpty()) return;
    if (controller.sendChatRequest (text.toStdString()))
        input.clear();
    // A request that could not be made has already put its own reply in the transcript.
    refresh();
    updateControls();
}

void ChatSheet::updateControls()
{
    const bool busy = controller.isChatBusy();
    const bool previewing = controller.hasPlan() && controller.getStage() == MixController::Stage::Preview;
    sendButton.setButtonText (busy ? "Thinking" + juce::String (Glyph::ellip()) : "Ask");
    sendButton.setEnabled (! busy);
    input.setEnabled (! busy);

    keepButton.setVisible (previewing);
    revertButton.setVisible (previewing);
    compareButton.setVisible (previewing);
    compareButton.setButtonText (controller.getCompare() == MixController::Compare::Before ? "AFTER" : "BEFORE");
    compareButton.setStyle (DineButton::Style::Toggle);
    compareButton.setToggleState (controller.getCompare() == MixController::Compare::Before,
                                  juce::dontSendNotification);

    undoButton.setEnabled (controller.canUndoMix());
    redoButton.setEnabled (controller.canRedoMix());
    undoButton.setButtonText (controller.canUndoMix() ? "Undo" : "Undo");
    resized();
    repaint();
}

void ChatSheet::refresh()
{
    const auto& chat = controller.getChat();
    const bool busy = controller.isChatBusy();
    if (chat.size() != shownTurns || busy != wasBusy)
    {
        shownTurns = chat.size();
        wasBusy = busy;
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
    g.drawText ("Mix Buddy", head.withTrimmedRight (70), juce::Justification::centredLeft);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.5f));
    g.drawText ("DLIVE's mix engineer, in plain words", head.withTrimmedLeft (86).withTrimmedRight (70), juce::Justification::centredLeft, true);

    // What it is for and what it is not, always on screen - so nobody types a request it cannot honour.
    {
        auto note = inner.withTrimmedTop (10).removeFromTop (kNoteH);
        Dine::fillRounded (g, note.toFloat(), Dine::item, Dine::Radius::control);
        auto r = note.reduced (12, 9);
        g.setFont (Dine::text (11.5f));
        g.setColour (Dine::ink2);
        g.drawFittedText ("Ask about the mix: a source louder or quieter, forward or back, brighter, warmer, less boom, "
                          "less harsh, more or less room, the master louder without clipping.",
                          r.removeFromTop (r.getHeight() / 2), juce::Justification::topLeft, 3, 1.0f);
        g.setColour (Dine::ink3);
        g.drawFittedText ("Not for anything else: it cannot touch the recording, the routing, the device or a preamp, "
                          "and it never keeps a change - you press KEEP.",
                          r, juce::Justification::topLeft, 3, 1.0f);
    }

    auto foot = getLocalBounds().reduced (18, 14).removeFromBottom (16);
    g.setColour (Dine::ink4);
    g.setFont (Dine::text (10.5f));
    const auto provider = controller.getTuneLive().getProvider();
    juce::String who = provider != nullptr ? juce::String (provider->getName()) : juce::String ("DLIVE built-in, offline");
    g.drawText ((controller.isLiveSafe() ? juce::String ("LIVE SAFE is on: changes stay small. ") : juce::String ("Nothing changes until you press KEEP. "))
                    + who, foot, juce::Justification::centredLeft, true);
}

void ChatSheet::resized()
{
    auto inner = cardBounds().reduced (18, 16);
    inner.removeFromTop (26 + 10 + kNoteH + 10);
    inner.removeFromBottom (16 + 8);
    close.setBounds (cardBounds().reduced (18, 16).removeFromTop (26).removeFromRight (juce::jmax (56, close.idealWidth())));

    auto actions = inner.removeFromBottom (Dine::Metric::button + 4);
    if (keepButton.isVisible())
    {
        keepButton.setBounds (actions.removeFromLeft (juce::jmax (56, keepButton.idealWidth())).withHeight (Dine::Metric::button));
        actions.removeFromLeft (6);
        revertButton.setBounds (actions.removeFromLeft (juce::jmax (56, revertButton.idealWidth())).withHeight (Dine::Metric::button));
        actions.removeFromLeft (6);
        compareButton.setBounds (actions.removeFromLeft (juce::jmax (56, compareButton.idealWidth())).withHeight (Dine::Metric::button));
        actions.removeFromLeft (10);
    }
    redoButton.setBounds (actions.removeFromRight (juce::jmax (48, redoButton.idealWidth())).withHeight (Dine::Metric::button));
    actions.removeFromRight (6);
    undoButton.setBounds (actions.removeFromRight (juce::jmax (48, undoButton.idealWidth())).withHeight (Dine::Metric::button));

    inner.removeFromBottom (10);
    auto entry = inner.removeFromBottom (32);
    sendButton.setBounds (entry.removeFromRight (juce::jmax (72, sendButton.idealWidth())).withSizeKeepingCentre (
                              juce::jmax (72, sendButton.idealWidth()), Dine::Metric::button));
    entry.removeFromRight (8);
    input.setBounds (entry);

    inner.removeFromBottom (10);
    scroller->setBounds (inner);
    transcript->layout (scroller->getMaximumVisibleWidth());
}

void ChatSheet::mouseUp (const juce::MouseEvent&) {}

bool ChatSheet::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

} // namespace livemix
