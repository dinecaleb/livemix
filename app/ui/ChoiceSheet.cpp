#include "ChoiceSheet.h"
#include "UI/Widgets.h"

namespace livemix
{

namespace
{
    constexpr int kCardW = 640, kPadX = 30, kPadY = 26;
    constexpr int kLineH = 21, kColGap = 28;

    int wrapped (const juce::Font& f, const juce::String& text, int width)
    {
        if (text.isEmpty() || width < 40) return 0;
        juce::AttributedString a; a.setText (text); a.setFont (f);
        juce::TextLayout tl; tl.createLayout (a, float (width));
        return int (std::ceil (tl.getHeight())) + 2;
    }
}

ChoiceSheet::ChoiceSheet (const juce::String& t, const juce::String& s) : title (t), sentence (s)
{
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);
}

ChoiceSheet::~ChoiceSheet() = default;

void ChoiceSheet::setColumns (Column l, Column r)
{
    left = std::move (l);
    right = std::move (r);
    resized();
    repaint();
}

void ChoiceSheet::setNote (const juce::String& boxed, const juce::String& quiet)
{
    boxedNote = boxed;
    quietNote = quiet;
    resized();
    repaint();
}

void ChoiceSheet::addAction (const juce::String& text, bool destructive, bool isDefault, std::function<void()> action)
{
    auto b = std::make_unique<DineButton> (text, isDefault ? DineButton::Style::Filled : DineButton::Style::Standard);
    b->setFontPx (12.5f);
    if (destructive)
    {
        b->setStyle (DineButton::Style::Ghost);
        b->setTint (Dine::crit);
    }
    const int index = int (actions.size());
    b->onClick = [this, index]
    {
        auto go = handlers[size_t (index)];
        if (onClose) onClose();              // the sheet is gone before the work starts
        if (go) go();
    };
    addAndMakeVisible (*b);
    actions.push_back (std::move (b));
    handlers.push_back (std::move (action));
    if (isDefault) defaultAction = index;
    resized();
}

bool ChoiceSheet::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (key == juce::KeyPress::returnKey && defaultAction >= 0)
    {
        actions[size_t (defaultAction)]->onClick();
        return true;
    }
    return false;
}

int ChoiceSheet::columnsHeight() const
{
    const int rows = juce::jmax (left.lines.size(), right.lines.size());
    return rows > 0 ? 18 + 8 + rows * kLineH : 0;
}

juce::Rectangle<int> ChoiceSheet::cardBounds() const
{
    const int w = juce::jmin (kCardW, getWidth() - 60);
    const int inner = w - kPadX * 2;
    int h = kPadY + 28 + 6 + wrapped (Dine::text (13.0f), sentence, inner) + 22;
    h += columnsHeight();
    if (boxedNote.isNotEmpty()) h += 20 + 14 + wrapped (Dine::text (12.5f), boxedNote, inner - 32) + 14;
    if (quietNote.isNotEmpty()) h += 16 + wrapped (Dine::text (12.0f), quietNote, inner);
    h += 24 + Dine::Metric::button + kPadY;
    return juce::Rectangle<int> (w, juce::jmin (h, getHeight() - 40)).withCentre (getLocalBounds().getCentre());
}

void ChoiceSheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.86f));
    auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), Dine::Radius::card);

    auto r = card.reduced (kPadX, kPadY);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (22.0f, 600));
    Dine::drawText (g, title, r.removeFromTop (28), juce::Justification::centredLeft, true);
    r.removeFromTop (6);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (13.0f));
    {
        const int h = wrapped (Dine::text (13.0f), sentence, r.getWidth());
        Dine::drawFittedText (g, sentence, r.removeFromTop (h), juce::Justification::topLeft, 4, 1.0f);
    }
    r.removeFromTop (22);

    if (columnsHeight() > 0)
    {
        auto block = r.removeFromTop (columnsHeight());
        const int colW = (block.getWidth() - kColGap) / 2;
        const Column* cols[2] = { &left, &right };
        for (int c = 0; c < 2; ++c)
        {
            auto area = block.removeFromLeft (colW);
            block.removeFromLeft (kColGap);
            if (cols[c]->heading.isEmpty() && cols[c]->lines.isEmpty()) continue;
            g.setColour (cols[c]->tint);
            g.setFont (Dine::text (13.0f, 600));
            Dine::drawText (g, cols[c]->heading, area.removeFromTop (18), juce::Justification::centredLeft, true);
            area.removeFromTop (8);
            for (const auto& line : cols[c]->lines)
            {
                auto row = area.removeFromTop (kLineH);
                g.setColour (cols[c]->tick ? Dine::ok : Dine::ink4);
                g.setFont (Dine::text (12.5f, 500));
                Dine::drawText (g, cols[c]->tick ? Glyph::check() : juce::String (Glyph::minus()),
                                row.removeFromLeft (14), juce::Justification::centredLeft);
                row.removeFromLeft (4);
                g.setColour (Dine::ink2);
                g.setFont (Dine::text (12.5f));
                Dine::drawText (g, line, row, juce::Justification::centredLeft, true);
            }
        }
    }

    if (boxedNote.isNotEmpty())
    {
        r.removeFromTop (20);
        const int h = 14 + wrapped (Dine::text (12.5f), boxedNote, r.getWidth() - 32) + 14;
        auto box = r.removeFromTop (h);
        Dine::fillRounded (g, box.toFloat(), Dine::item, Dine::Radius::control);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        Dine::drawFittedText (g, boxedNote, box.reduced (16, 14), juce::Justification::topLeft, 3, 1.0f);
    }
    if (quietNote.isNotEmpty())
    {
        r.removeFromTop (16);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawFittedText (g, quietNote, r.removeFromTop (wrapped (Dine::text (12.0f), quietNote, r.getWidth())),
                              juce::Justification::topLeft, 3, 1.0f);
    }
}

void ChoiceSheet::resized()
{
    auto foot = cardBounds().reduced (kPadX, kPadY).removeFromBottom (Dine::Metric::button);
    // The default is last, the way macOS orders them, so the row is laid out from the right.
    for (int i = int (actions.size()) - 1; i >= 0; --i)
    {
        const int w = juce::jmax (88, actions[size_t (i)]->idealWidth());
        actions[size_t (i)]->setBounds (foot.removeFromRight (w));
        foot.removeFromRight (10);
    }
}

} // namespace livemix
