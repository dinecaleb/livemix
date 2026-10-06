#include "SetlistSheet.h"
#include "native/MixHistory.h"
#include "UI/Widgets.h"

namespace livemix
{

namespace
{
    constexpr int kHeadH = 64;            // title and the sentence under it
    constexpr int kFieldH = 30;
    juce::Font captionFont() { return Dine::text (13.0f, 600); }
    juce::Font noteFont()    { return Dine::text (11.5f, 500); }
}

SetlistSheet::SetlistSheet (MixController& c, int selectCue) : controller (c)
{
    list = controller.getSetlist();

    done.setFontPx (12.5f);
    done.onClick = [this] { if (auto close = onClose) close(); };
    add.setFontPx (12.0f);
    add.setTooltip ("A new cue after the one picked, keeping the mix as it is until you give it a scene.");
    add.onClick = [this]
    {
        const int at = picked < 0 ? int (list.cues.size()) : picked + 1;
        Cue cue;
        cue.name = "Cue " + std::to_string (list.cues.size() + 1);
        cue.scene = 0;
        select (controller.addCue (cue, at));
        name.grabKeyboardFocus();
        name.selectAll();
    };
    up.setFontPx (12.0f);
    up.onClick = [this] { if (picked > 0) { controller.moveCue (picked, picked - 1); select (picked - 1); } };
    down.setFontPx (12.0f);
    down.onClick = [this] { if (picked >= 0 && picked + 1 < int (list.cues.size())) { controller.moveCue (picked, picked + 1); select (picked + 1); } };
    remove.setFontPx (12.0f);
    remove.setTint (Dine::crit);
    remove.onClick = [this]
    {
        if (picked < 0 || picked >= int (list.cues.size())) return;
        const juce::String gone (list.cues[size_t (picked)].name);
        controller.removeCue (picked);
        refresh();
        select (juce::jmin (picked, int (list.cues.size()) - 1));
        if (onToast) onToast ("Deleted the cue " + gone + ". Its scene and the mix are as they were.");
    };
    go.setFontPx (12.0f);
    go.setTooltip ("Go to this cue now: its scene comes back, as a scene does, and it becomes Now.");
    go.onClick = [this] { if (picked >= 0) { controller.goToCue (picked); refresh(); } };
    for (auto* b : { &done, &add, &up, &down, &remove, &go }) addAndMakeVisible (*b);

    for (auto* t : { &name, &louder, &softer })
    {
        t->setMultiLine (false);
        t->setReturnKeyStartsNewLine (false);
        t->addListener (this);
        addAndMakeVisible (*t);
    }
    name.setTextToShowWhenEmpty ("The song or the moment", Dine::ink3);
    louder.setTextToShowWhenEmpty ("What should come up - \"Lead singer\"", Dine::ink3);
    softer.setTextToShowWhenEmpty ("What should go down - \"Guitars, room mics\"", Dine::ink3);
    name.setTooltip ("The cue's name, as it shows on LIVE and in Up next.");
    louder.setTooltip ("A note for whoever is at the desk: what should come up in this cue. DINE does not move anything for it.");
    softer.setTooltip ("A note for whoever is at the desk: what should go down in this cue. DINE does not move anything for it.");

    for (int i = 0; i < 6; ++i)
    {
        const juce::String text = i < kMixScenes ? juce::String (controller.getScene (i).name)
                                : i == kMixScenes ? juce::String ("As it is") : juce::String ("A favourite") + Glyph::ellip();
        auto b = std::make_unique<DineButton> (text, DineButton::Style::Toggle);
        b->setFontPx (12.0f);
        b->setClickingTogglesState (false);
        if (i < kMixScenes)
            b->onClick = [this, i] { pickScene (i, {}); };
        else if (i == kMixScenes)
        {
            b->setTooltip ("The cue moves the setlist on and leaves the mix exactly as it is.");
            b->onClick = [this] { pickScene (-1, {}); };
        }
        else
        {
            b->setTooltip ("Bring back one of this session's favourite mixes, by its name.");
            b->onClick = [this] { showFavourites(); };
        }
        addAndMakeVisible (*b);
        scenes[size_t (i)] = std::move (b);
    }

    lookAndFeelChanged();
    setWantsKeyboardFocus (true);
    select (selectCue >= 0 ? selectCue : (list.cues.empty() ? -1 : 0));
}

SetlistSheet::~SetlistSheet() = default;

void SetlistSheet::lookAndFeelChanged()
{
    for (auto* t : { &name, &louder, &softer }) Dine::styleTextEditor (*t, Dine::control);
    name.setFont (Dine::text (17.0f, 600));
    louder.setFont (Dine::text (13.0f));
    softer.setFont (Dine::text (13.0f));
}

void SetlistSheet::refresh()
{
    const auto now = controller.getSetlist();
    if (now == list) return;
    list = now;
    if (picked >= int (list.cues.size())) picked = int (list.cues.size()) - 1;
    loadPicked();
    resized();
    repaint();
}

void SetlistSheet::select (int cue)
{
    list = controller.getSetlist();
    picked = list.cues.empty() ? -1 : juce::jlimit (0, int (list.cues.size()) - 1, cue);
    // Keep the picked row in view.
    const int rows = rowsShown();
    if (picked >= 0)
    {
        if (picked < firstRow) firstRow = picked;
        if (picked >= firstRow + rows) firstRow = picked - rows + 1;
    }
    firstRow = juce::jlimit (0, juce::jmax (0, int (list.cues.size()) - rows), firstRow);
    loadPicked();
    resized();
    repaint();
}

void SetlistSheet::loadPicked()
{
    loading = true;
    const bool any = picked >= 0 && picked < int (list.cues.size());
    const Cue cue = any ? list.cues[size_t (picked)] : Cue {};
    if (name.getText() != juce::String (cue.name)) name.setText (cue.name, false);
    if (louder.getText() != juce::String (cue.louder)) louder.setText (cue.louder, false);
    if (softer.getText() != juce::String (cue.softer)) softer.setText (cue.softer, false);
    for (auto* t : { &name, &louder, &softer }) t->setEnabled (any);
    for (int i = 0; i < 6; ++i)
    {
        auto& b = *scenes[size_t (i)];
        b.setEnabled (any);
        const bool on = i < kMixScenes ? cue.favourite.empty() && cue.scene == i
                      : i == kMixScenes ? cue.favourite.empty() && cue.scene < 0
                                        : ! cue.favourite.empty();
        b.setToggleState (any && on, juce::dontSendNotification);
        if (i < kMixScenes)
        {
            const auto& sc = controller.getScene (i);
            b.setButtonText (sc.name);
            b.setTooltip (sc.kept ? "Bring back the " + juce::String (sc.name) + " mix when this cue starts."
                                  : "Nothing is kept under " + juce::String (sc.name) + " yet: keep it on LIVE, and this cue brings it back.");
        }
        if (i == kMixScenes + 1)
            b.setButtonText (cue.favourite.empty() ? juce::String ("A favourite") + Glyph::ellip() : juce::String (cue.favourite));
    }
    up.setEnabled (any && picked > 0);
    down.setEnabled (any && picked + 1 < int (list.cues.size()));
    remove.setEnabled (any);
    go.setEnabled (any);
    loading = false;
}

void SetlistSheet::commit()
{
    if (loading || picked < 0 || picked >= int (list.cues.size())) return;
    auto cue = list.cues[size_t (picked)];
    cue.name = name.getText().trim().toStdString();
    cue.louder = louder.getText().trim().toStdString();
    cue.softer = softer.getText().trim().toStdString();
    if (cue.name.empty()) return;               // a cue always has a name; keep the old one until one is typed
    controller.updateCue (picked, cue);
    list = controller.getSetlist();
    repaint (listBounds());
}

void SetlistSheet::pickScene (int scene, const std::string& favourite)
{
    if (picked < 0 || picked >= int (list.cues.size())) return;
    auto cue = list.cues[size_t (picked)];
    cue.scene = scene;
    cue.favourite = favourite;
    controller.updateCue (picked, cue);
    list = controller.getSetlist();
    loadPicked();
    repaint();
}

void SetlistSheet::showFavourites()
{
    juce::PopupMenu m;
    const int n = controller.numFavourites();
    for (int i = 0; i < n; ++i) m.addItem (1 + i, controller.getFavourite (i).name);
    if (n == 0) m.addItem (-1, "No favourite mixes yet. Keep one from Favourite mixes.", false);
    juce::Component::SafePointer<SetlistSheet> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (scenes[size_t (kMixScenes + 1)].get()),
                     [safe] (int chosen)
                     {
                         if (safe == nullptr || chosen <= 0) return;
                         safe->pickScene (0, safe->controller.getFavourite (chosen - 1).name);
                     });
}

void SetlistSheet::textEditorTextChanged (juce::TextEditor&) { commit(); }
void SetlistSheet::textEditorReturnKeyPressed (juce::TextEditor& t)
{
    commit();
    if (&t == &name) louder.grabKeyboardFocus();
    else if (&t == &louder) softer.grabKeyboardFocus();
    else grabKeyboardFocus();
}

bool SetlistSheet::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { if (auto close = onClose) close(); return true; }
    if (key == juce::KeyPress::upKey)   { if (picked > 0) select (picked - 1); return true; }
    if (key == juce::KeyPress::downKey) { if (picked + 1 < int (list.cues.size())) select (picked + 1); return true; }
    return false;
}

juce::Rectangle<int> SetlistSheet::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (kCardW, getWidth() - 48), juce::jmin (640, getHeight() - 40));
}

juce::Rectangle<int> SetlistSheet::listBounds() const
{
    auto card = cardBounds();
    card.removeFromTop (kHeadH);
    auto left = card.removeFromLeft (kListW).reduced (10, 10);
    left.removeFromBottom (2 * (Dine::Metric::button + 8));
    return left;
}

int SetlistSheet::rowsShown() const { return juce::jmax (1, listBounds().getHeight() / kRowH); }

int SetlistSheet::rowAt (juce::Point<int> p) const
{
    const auto r = listBounds();
    if (! r.contains (p)) return -1;
    const int i = firstRow + (p.y - r.getY()) / kRowH;
    return (p.y - r.getY()) / kRowH < rowsShown() && i < int (list.cues.size()) ? i : -1;
}

void SetlistSheet::mouseUp (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition())) { if (auto close = onClose) close(); return; }
    const int i = rowAt (e.getPosition());
    if (i >= 0) { select (i); grabKeyboardFocus(); }
}

void SetlistSheet::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! listBounds().contains (e.getPosition())) return;
    const int step = w.deltaY > 0 ? -1 : w.deltaY < 0 ? 1 : 0;
    firstRow = juce::jlimit (0, juce::jmax (0, int (list.cues.size()) - rowsShown()), firstRow + step);
    repaint (listBounds());
}

void SetlistSheet::resized()
{
    // The list's scroll, settled at the size it is now: everything from the top when it all
    // fits, otherwise the picked cue in view.
    {
        const int rows = rowsShown(), n = int (list.cues.size());
        if (picked >= 0 && picked < firstRow) firstRow = picked;
        if (picked >= firstRow + rows) firstRow = picked - rows + 1;
        firstRow = juce::jlimit (0, juce::jmax (0, n - rows), firstRow);
    }

    auto card = cardBounds();
    auto head = card.removeFromTop (kHeadH).reduced (kPad, 0);
    done.setBounds (head.removeFromRight (juce::jmax (72, done.idealWidth())).withSizeKeepingCentre (juce::jmax (72, done.idealWidth()), Dine::Metric::button));

    auto left = card.removeFromLeft (kListW).reduced (10, 10);
    auto foot = left.removeFromBottom (2 * (Dine::Metric::button + 8));
    auto line1 = foot.removeFromTop (Dine::Metric::button);
    add.setBounds (line1.removeFromLeft (juce::jmax (96, add.idealWidth())));
    line1.removeFromLeft (6);
    remove.setBounds (line1.removeFromRight (juce::jmax (64, remove.idealWidth())));
    foot.removeFromTop (8);
    auto line2 = foot.removeFromTop (Dine::Metric::button);
    up.setBounds (line2.removeFromLeft (juce::jmax (80, up.idealWidth())));
    line2.removeFromLeft (6);
    down.setBounds (line2.removeFromLeft (juce::jmax (90, down.idealWidth())));

    auto right = card.reduced (kPad, 18);
    nameCaption = {};
    name.setBounds (right.removeFromTop (40));
    right.removeFromTop (22);
    sceneCaption = right.removeFromTop (18);
    sceneNote = right.removeFromTop (16);
    right.removeFromTop (10);
    {
        auto row = right.removeFromTop (Dine::Metric::button + 8);
        const int gap = 8, w = (row.getWidth() - gap * 2) / 3;
        auto row2 = right.withHeight (0);
        for (int i = 0; i < 3; ++i) { scenes[size_t (i)]->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
        right.removeFromTop (8);
        row2 = right.removeFromTop (Dine::Metric::button + 8);
        for (int i = 3; i < 6; ++i) { scenes[size_t (i)]->setBounds (row2.removeFromLeft (w)); row2.removeFromLeft (gap); }
    }
    right.removeFromTop (22);
    louderCaption = right.removeFromTop (18);
    right.removeFromTop (6);
    louder.setBounds (right.removeFromTop (kFieldH));
    right.removeFromTop (14);
    softerCaption = right.removeFromTop (18);
    right.removeFromTop (6);
    softer.setBounds (right.removeFromTop (kFieldH));
    right.removeFromTop (22);
    go.setBounds (right.removeFromTop (Dine::Metric::button).withWidth (juce::jmax (120, go.idealWidth())));
}

void SetlistSheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.84f));
    const auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), 14.0f);

    auto head = card.withHeight (kHeadH).reduced (kPad, 12);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (15.0f, 700));
    Dine::drawText (g, "Setlist", head.removeFromTop (22), juce::Justification::centredLeft, false);
    g.setColour (Dine::ink2);
    g.setFont (noteFont());
    Dine::drawText (g, "Each cue is a moment in the service. Space on LIVE goes to the next one.",
                    head.removeFromTop (16).withTrimmedRight (done.getWidth() + 12), juce::Justification::centredLeft, true);

    // the hairlines: under the head, between the list and the cue
    g.setColour (Dine::hair);
    g.fillRect (card.getX(), card.getY() + kHeadH, card.getWidth(), 1);
    g.fillRect (card.getX() + kListW, card.getY() + kHeadH, 1, card.getHeight() - kHeadH);

    // THE LIST: number, name and its scene, Now and Next.
    const auto lb = listBounds();
    if (list.cues.empty())
    {
        g.setColour (Dine::ink3);
        g.setFont (noteFont());
        Dine::drawFittedText (g, "No cues yet. Add a cue for each song and each moment, in the order they happen.",
                              lb.withHeight (60).reduced (8, 0), juce::Justification::topLeft, 3, 1.0f);
    }
    const int next = list.next();
    const auto numFont = Dine::mono (11.0f, 500), nameFont = Dine::text (13.0f, 600), subFont = Dine::text (11.0f, 500), tagFont = Dine::text (11.0f, 600);
    for (int k = 0; k < rowsShown() && firstRow + k < int (list.cues.size()); ++k)
    {
        const int i = firstRow + k;
        auto row = juce::Rectangle<int> (lb.getX(), lb.getY() + k * kRowH, lb.getWidth(), kRowH - 4);
        if (i == picked) Dine::fillRounded (g, row.toFloat(), Dine::controlOn, 8.0f);
        auto inner = row.reduced (10, 4);
        g.setColour (Dine::ink3);
        g.setFont (numFont);
        Dine::drawText (g, juce::String (i + 1), inner.removeFromLeft (Dine::textWidth (numFont, "00") + 6), juce::Justification::centredLeft, false);
        const juce::String tag = i == list.current ? "Now" : i == next ? "Next" : juce::String();
        if (tag.isNotEmpty())
        {
            g.setColour (i == list.current ? Dine::accent : Dine::ink3);
            g.setFont (tagFont);
            Dine::drawText (g, tag, inner.removeFromRight (Dine::textWidth (tagFont, tag) + 2), juce::Justification::centredRight, false);
            inner.removeFromRight (8);
        }
        const auto& cue = list.cues[size_t (i)];
        g.setColour (Dine::ink);
        g.setFont (nameFont);
        Dine::drawFittedText (g, juce::String (cue.name), inner.removeFromTop (18), juce::Justification::centredLeft, 1, 0.85f);
        g.setColour (Dine::ink3);
        g.setFont (subFont);
        Dine::drawFittedText (g, juce::String (controller.cueSceneName (cue)), inner, juce::Justification::centredLeft, 1, 0.85f);
    }

    // THE CUE: what is happening, and the two notes.
    if (picked >= 0)
    {
        g.setColour (Dine::ink);
        g.setFont (captionFont());
        Dine::drawText (g, juce::String (juce::CharPointer_UTF8 ("What\xe2\x80\x99s happening?")), sceneCaption, juce::Justification::centredLeft, false);
        g.setColour (Dine::ink3);
        g.setFont (noteFont());
        Dine::drawFittedText (g, "The scene this cue brings back. Keep a scene on LIVE to give it a mix.", sceneNote,
                              juce::Justification::centredLeft, 1, 0.9f);
        g.setColour (Dine::ink);
        g.setFont (captionFont());
        Dine::drawText (g, "Louder", louderCaption, juce::Justification::centredLeft, false);
        Dine::drawText (g, "Softer", softerCaption, juce::Justification::centredLeft, false);
    }
}

} // namespace livemix
