#include "SetlistSheet.h"
#include "native/MixHistory.h"
#include "UI/Widgets.h"

namespace livemix
{

namespace
{
    juce::Font noteFont() { return Dine::text (11.5f, 500); }

    const char* kindLine (CueKind k) noexcept
    {
        switch (k)
        {
            case CueKind::Band:          return "Singers and band";
            case CueKind::Speaking:      return "Someone at the mic";
            case CueKind::QuietMoment:   return "Speaking over soft keys";
            case CueKind::MusicPlayback: return "Walk-in or walk-out music";
            default:                     return "";
        }
    }

    // One of the four "What's happening?" cards: what it is, and the line under it.
    class KindCard : public juce::Button
    {
    public:
        explicit KindCard (CueKind k) : juce::Button (cueKindName (k)), kind (k) { setClickingTogglesState (false); }
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool on = getToggleState();
            Dine::fillRounded (g, r, on ? Dine::controlOn : over ? Dine::control : Dine::item, 10.0f);
            Dine::hairlineRounded (g, r, on ? Dine::edge : Dine::hair, 10.0f);
            auto t = getLocalBounds().reduced (12, 10);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (13.0f, 600));
            Dine::drawFittedText (g, cueKindName (kind), t.removeFromTop (18), juce::Justification::centredLeft, 1, 0.85f);
            g.setColour (Dine::ink3);
            g.setFont (noteFont());
            Dine::drawFittedText (g, kindLine (kind), t, juce::Justification::topLeft, 2, 0.9f);
        }
        CueKind kind;
    };
}

// ------------------------------------------------------------------------------- the editor
class SetlistSheet::Editor : public juce::Component
{
public:
    explicit Editor (SetlistSheet& s) : sheet (s)
    {
        name.setFont (Dine::text (17.0f, 600));
        name.setIndents (12, 0);
        name.setBorder (juce::BorderSize<int> (0));
        name.setJustification (juce::Justification::centredLeft);
        name.setTextToShowWhenEmpty ("The song or the moment", Dine::ink3);
        name.setTooltip ("The cue's name, as it shows on LIVE and in Up next.");
        name.onTextChange = [this] { commitName(); };
        name.onReturnKey = [this] { sheet.grabKeyboardFocus(); };
        addAndMakeVisible (name);

        for (int k = 0; k < int (CueKind::Count); ++k)
        {
            auto card = std::make_unique<KindCard> (CueKind (k));
            card->onClick = [this, k] { sheet.chooseKind (CueKind (k)); };
            addAndMakeVisible (*card);
            kinds[size_t (k)] = std::move (card);
        }
        useNow.setFontPx (12.0f);
        useNow.setTooltip ("Who is on in this cue becomes who is heard in the mix right now.");
        useNow.onClick = [this] { sheet.useWhatIsOnNow(); };
        addAndMakeVisible (useNow);

        startFrom.setTooltip ("Optional: bring back a kept scene or favourite first, then switch who is on.");
        startFrom.onClick = [this] { showStartMenu(); };
        addAndMakeVisible (startFrom);

        go.setFontPx (12.0f);
        go.setTooltip ("Go to this cue now: who is on is switched, and it becomes Now.");
        go.onClick = [this] { if (sheet.picked >= 0) { sheet.controller.goToCue (sheet.picked); sheet.refresh(); } };
        addAndMakeVisible (go);
        lookAndFeelChanged();
    }

    void lookAndFeelChanged() override { Dine::styleTextEditor (name, Dine::control); }

    void load (const Cue* cue)
    {
        const bool any = cue != nullptr;
        for (auto* c : std::initializer_list<juce::Component*> { &name, &useNow, &startFrom, &go }) c->setVisible (any);
        for (auto& k : kinds) k->setVisible (any);
        rows.clear();
        if (! any) { resized(); repaint(); return; }
        if (! name.hasKeyboardFocus (true) && name.getText() != juce::String (cue->name)) name.setText (cue->name, false);
        for (auto& k : kinds) k->setToggleState (k->kind == cue->kind, juce::dontSendNotification);
        startFrom.setValue ("Start from: " + juce::String (sheet.controller.cueSceneName (*cue)));
        for (const auto& u : sheet.controller.cueUnits())
        {
            auto row = std::make_unique<Row> (sheet, u.key, juce::String (u.name), cue->levelOf (u.key));
            addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
        setSize (getWidth(), heightFor (getWidth()));
        resized();
        repaint();
    }

    int heightFor (int) const { return 40 + 26 + 40 + 76 + 30 + 40 + int (rows.size()) * kWhoH + 20 + 32 + 18 + Dine::Metric::button + 20; }

    void resized() override
    {
        auto r = getLocalBounds();
        name.setBounds (r.removeFromTop (40));
        r.removeFromTop (26);
        captionKind = r.removeFromTop (40);
        auto cards = r.removeFromTop (76);
        const int gap = 10, w = (cards.getWidth() - gap * 3) / 4;
        for (auto& k : kinds) { k->setBounds (cards.removeFromLeft (w)); cards.removeFromLeft (gap); }
        r.removeFromTop (30);
        captionWho = r.removeFromTop (40);
        const int uw = useNow.idealWidth() + 8;
        useNow.setBounds (captionWho.withLeft (captionWho.getRight() - uw).withSizeKeepingCentre (uw, Dine::Metric::button));
        whoList = r.removeFromTop (int (rows.size()) * kWhoH);
        auto list = whoList;
        for (auto& row : rows) row->setBounds (list.removeFromTop (kWhoH));
        r.removeFromTop (20);
        startFrom.setBounds (r.removeFromTop (32).withWidth (juce::jmin (r.getWidth(), juce::jmax (220, startFrom.idealWidth()))));
        r.removeFromTop (18);
        go.setBounds (r.removeFromTop (Dine::Metric::button).withWidth (juce::jmax (120, go.idealWidth())));
    }

    void paint (juce::Graphics& g) override
    {
        if (! name.isVisible()) return;
        auto caption = [&g] (juce::Rectangle<int> r, const juce::String& title, const juce::String& line)
        {
            g.setColour (Dine::ink);
            g.setFont (Dine::text (13.0f, 600));
            Dine::drawText (g, title, r.removeFromTop (20), juce::Justification::centredLeft, false);
            g.setColour (Dine::ink3);
            g.setFont (noteFont());
            Dine::drawText (g, line, r.removeFromTop (16), juce::Justification::centredLeft, false);
        };
        caption (captionKind, juce::String (juce::CharPointer_UTF8 ("What\xe2\x80\x99s happening?")), "Pick one to start. You can change who's on below.");
        caption (captionWho.withTrimmedRight (useNow.getWidth() + 12), juce::String (juce::CharPointer_UTF8 ("Who\xe2\x80\x99s on?")),
                 "Everyone else is muted when this cue starts.");
        if (! whoList.isEmpty())
        {
            Dine::fillRounded (g, whoList.toFloat(), Dine::item, 10.0f);
            g.setColour (Dine::hair);
            for (int i = 1; i < int (rows.size()); ++i) g.fillRect (whoList.getX() + 14, whoList.getY() + i * kWhoH, whoList.getWidth() - 28, 1);
        }
    }

private:
    // A source in the cue: on or off, and for one that is on, Softer, Normal or Up front.
    class Row : public juce::Component
    {
    public:
        Row (SetlistSheet& s, std::string unitKey, juce::String label, CueLevel level)
            : sheet (s), unit (std::move (unitKey)), text (std::move (label)), on (level != CueLevel::Off)
        {
            onOff.setClickingTogglesState (false);
            onOff.setToggleState (on, juce::dontSendNotification);
            onOff.setTooltip (on ? "On in this cue. Switch it off and it is muted when the cue starts." : "Muted in this cue. Switch it on to hear it.");
            onOff.onClick = [this] { sheet.setWho (unit, on ? CueLevel::Off : CueLevel::Normal); };
            addAndMakeVisible (onOff);
            const char* labels[3] = { "Softer", "Normal", "Up front" };
            const CueLevel levels[3] = { CueLevel::Softer, CueLevel::Normal, CueLevel::UpFront };
            for (int i = 0; i < 3; ++i)
            {
                auto b = std::make_unique<DineButton> (labels[i], DineButton::Style::Segment);
                b->setFontPx (12.0f);
                b->setClickingTogglesState (false);
                b->setToggleState (level == levels[i], juce::dontSendNotification);
                b->onClick = [this, l = levels[i]] { sheet.setWho (unit, l); };
                if (on) addAndMakeVisible (*b); else addChildComponent (*b);
                segments[size_t (i)] = std::move (b);
            }
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (14, 0);
            onOff.setBounds (r.removeFromLeft (44).withSizeKeepingCentre (40, 24));
            track = r.removeFromRight (264).withSizeKeepingCentre (264, 30);
            auto seg = track.reduced (2);
            const int w = seg.getWidth() / 3;
            for (auto& b : segments) b->setBounds (seg.removeFromLeft (w));
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().reduced (14, 0).withTrimmedLeft (56);
            if (on) { r.removeFromRight (track.getWidth() + 12); Dine::drawSegmentTrack (g, track); }
            else
            {
                g.setColour (Dine::ink3);
                g.setFont (Dine::text (12.0f));
                Dine::drawText (g, "Muted", r.removeFromRight (60), juce::Justification::centredRight, false);
                r.removeFromRight (8);
            }
            g.setColour (on ? Dine::ink : Dine::ink2);
            g.setFont (Dine::text (13.0f, 500));
            Dine::drawFittedText (g, text, r, juce::Justification::centredLeft, 1, 0.85f);
        }
    private:
        SetlistSheet& sheet;
        std::string unit;
        juce::String text;
        bool on = true;
        DineSwitch onOff { "", "" };
        std::array<std::unique_ptr<DineButton>, 3> segments;
        juce::Rectangle<int> track;
    };

    void commitName()
    {
        if (sheet.picked < 0 || sheet.picked >= int (sheet.list.cues.size())) return;
        auto cue = sheet.list.cues[size_t (sheet.picked)];
        const auto t = name.getText().trim().toStdString();
        if (t.empty() || t == cue.name) return;
        cue.name = t;
        sheet.store (cue);
    }

    void showStartMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "As it is (the mix when the cue starts)");
        m.addSeparator();
        for (int i = 0; i < 4; ++i)
        {
            const auto& sc = sheet.controller.getScene (i);
            m.addItem (10 + i, juce::String (sc.name) + (sc.kept ? juce::String() : juce::String ("  (nothing kept yet)")));
        }
        const int n = sheet.controller.numFavourites();
        if (n > 0) m.addSeparator();
        for (int i = 0; i < n; ++i) m.addItem (100 + i, juce::String (sheet.controller.getFavourite (i).name));
        juce::Component::SafePointer<Editor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&startFrom), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            auto& sh = safe->sheet;
            if (sh.picked < 0 || sh.picked >= int (sh.list.cues.size())) return;
            auto cue = sh.list.cues[size_t (sh.picked)];
            cue.scene = r >= 10 && r < 14 ? r - 10 : -1;
            cue.favourite = r >= 100 ? sh.controller.getFavourite (r - 100).name : std::string();
            sh.store (cue);
        });
    }

    static constexpr int kWhoH = 50;
    SetlistSheet& sheet;
    juce::TextEditor name;
    std::array<std::unique_ptr<KindCard>, size_t (CueKind::Count)> kinds;
    DineButton useNow { juce::String (juce::CharPointer_UTF8 ("Use what\xe2\x80\x99s on right now")), DineButton::Style::Standard };
    DinePopup startFrom;
    DineButton go { "Go to this cue", DineButton::Style::Standard };
    std::vector<std::unique_ptr<Row>> rows;
    juce::Rectangle<int> captionKind, captionWho, whoList;
};

// ------------------------------------------------------------------------------- the sheet
SetlistSheet::SetlistSheet (MixController& c, int selectCue) : controller (c)
{
    list = controller.getSetlist();
    editor = std::make_unique<Editor> (*this);
    editorView.setViewedComponent (editor.get(), false);
    editorView.setScrollBarsShown (true, false);
    editorView.setScrollBarThickness (8);
    addAndMakeVisible (editorView);

    done.setFontPx (12.5f);
    done.onClick = [this] { if (auto close = onClose) close(); };
    add.setFontPx (12.0f);
    add.setTooltip ("A new cue after the one picked: a song, with the band on.");
    add.onClick = [this]
    {
        const int at = picked < 0 ? int (list.cues.size()) : picked + 1;
        Cue cue;
        cue.name = "New cue";
        controller.fillCueFor (cue, CueKind::Band);
        select (controller.addCue (cue, at));
    };
    up.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x91")));
    up.setTooltip ("Move this cue up");
    up.onClick = [this] { if (picked > 0) { controller.moveCue (picked, picked - 1); select (picked - 1); } };
    down.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x93")));
    down.setTooltip ("Move this cue down");
    down.onClick = [this] { if (picked >= 0 && picked + 1 < int (list.cues.size())) { controller.moveCue (picked, picked + 1); select (picked + 1); } };
    remove.setFontPx (12.0f);
    remove.setTint (Dine::crit);
    remove.onClick = [this]
    {
        if (picked < 0 || picked >= int (list.cues.size())) return;
        const juce::String gone (list.cues[size_t (picked)].name);
        controller.removeCue (picked);
        list = controller.getSetlist();
        select (juce::jmin (picked, int (list.cues.size()) - 1));
        if (onToast) onToast ("Deleted the cue " + gone + ". The mix is as it was.");
    };
    for (auto* b : { &done, &add, &up, &down, &remove }) addAndMakeVisible (*b);
    setWantsKeyboardFocus (true);
    select (selectCue >= 0 ? selectCue : (list.cues.empty() ? -1 : 0));
}

SetlistSheet::~SetlistSheet() = default;

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
    loadPicked();
    resized();
    repaint();
}

void SetlistSheet::loadPicked()
{
    const bool any = picked >= 0 && picked < int (list.cues.size());
    editor->load (any ? &list.cues[size_t (picked)] : nullptr);
    up.setEnabled (any && picked > 0);
    down.setEnabled (any && picked + 1 < int (list.cues.size()));
    remove.setEnabled (any);
}

void SetlistSheet::store (const Cue& cue)
{
    if (picked < 0) return;
    controller.updateCue (picked, cue);
    list = controller.getSetlist();
    // Rows are rebuilt from the cue, so a switch or a level redraws as it now is - after the
    // button that asked has finished its click.
    juce::Component::SafePointer<SetlistSheet> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->loadPicked(); safe->resized(); safe->repaint(); } });
}

void SetlistSheet::chooseKind (CueKind kind)
{
    if (picked < 0 || picked >= int (list.cues.size())) return;
    auto cue = list.cues[size_t (picked)];
    controller.fillCueFor (cue, kind);
    store (cue);
}

void SetlistSheet::setWho (const std::string& unit, CueLevel level)
{
    if (picked < 0 || picked >= int (list.cues.size())) return;
    auto cue = list.cues[size_t (picked)];
    for (const auto& u : controller.cueUnits()) if (cue.who.count (u.key) == 0) cue.who[u.key] = int (CueLevel::Normal);
    cue.who[unit] = int (level);
    store (cue);
}

void SetlistSheet::useWhatIsOnNow()
{
    if (picked < 0 || picked >= int (list.cues.size())) return;
    auto cue = list.cues[size_t (picked)];
    controller.fillCueFromNow (cue);
    store (cue);
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
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (kCardW, getWidth() - 48), juce::jmin (760, getHeight() - 40));
}

juce::Rectangle<int> SetlistSheet::listBounds() const
{
    auto card = cardBounds();
    card.removeFromTop (kHeadH);
    auto left = card.removeFromLeft (kListW).reduced (10, 10);
    left.removeFromBottom (Dine::Metric::button + 14);
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
    {
        const int rows = rowsShown(), n = int (list.cues.size());
        if (picked >= 0 && picked < firstRow) firstRow = picked;
        if (picked >= firstRow + rows) firstRow = picked - rows + 1;
        firstRow = juce::jlimit (0, juce::jmax (0, n - rows), firstRow);
    }
    auto card = cardBounds();
    auto head = card.removeFromTop (kHeadH).reduced (kPad, 0);
    const int dw = juce::jmax (72, done.idealWidth());
    done.setBounds (head.removeFromRight (dw).withSizeKeepingCentre (dw, Dine::Metric::button));

    auto left = card.removeFromLeft (kListW).reduced (10, 10);
    auto foot = left.removeFromBottom (Dine::Metric::button);
    add.setBounds (foot.removeFromLeft (juce::jmax (84, add.idealWidth())));
    remove.setBounds (foot.removeFromRight (juce::jmax (64, remove.idealWidth())));
    foot.removeFromRight (6);
    down.setBounds (foot.removeFromRight (Dine::Metric::button));
    foot.removeFromRight (6);
    up.setBounds (foot.removeFromRight (Dine::Metric::button));

    auto right = card.reduced (kPad, 18);
    editorView.setBounds (right);
    editor->setSize (right.getWidth() - 12, juce::jmax (right.getHeight(), editor->heightFor (right.getWidth())));
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

    g.setColour (Dine::hair);
    g.fillRect (card.getX(), card.getY() + kHeadH, card.getWidth(), 1);
    g.fillRect (card.getX() + kListW, card.getY() + kHeadH, 1, card.getHeight() - kHeadH);

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
        Dine::drawText (g, juce::String (i + 1), inner.removeFromLeft (Dine::textWidth (numFont, "00") + 8), juce::Justification::centredLeft, false);
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
        Dine::drawFittedText (g, juce::String (cue.name), inner.removeFromTop (20), juce::Justification::centredLeft, 1, 0.85f);
        g.setColour (Dine::ink3);
        g.setFont (subFont);
        Dine::drawFittedText (g, cueKindName (cue.kind), inner, juce::Justification::centredLeft, 1, 0.85f);
    }
}

} // namespace livemix
