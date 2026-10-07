#include "BroadcastReadinessSheet.h"
#include <algorithm>

namespace livemix
{

namespace
{
    // The row's grid. One place, so the paint and the controls can never disagree.
    constexpr int kLampW = 22;          // the lamp's column
    constexpr int kControlsW = 222;     // DONE / PROBLEM / SKIP, and the two quiet links under them
    constexpr int kGapX = 16;
    constexpr int kTitleH = 18, kLineH = 16, kPadY = 12, kCaptionH = 34;
    constexpr int kSegH = 26, kLinkH = 22;

    juce::Font titleFont()   { return Dine::text (13.0f, 500); }
    juce::Font bodyFont()    { return Dine::text (11.5f); }
    juce::Font captionFont() { return Dine::text (11.0f, 500); }

    // Greedy word wrap with the same measure the text is drawn with, so a wrapped line always
    // fits its cell and nothing is ever cut (the snapshot tool's clipping report checks).
    juce::StringArray wrap (const juce::Font& font, const juce::String& text, int width)
    {
        juce::StringArray lines;
        if (text.isEmpty() || width <= 20) return lines;
        for (const auto& paragraph : juce::StringArray::fromLines (text))
        {
            juce::String line;
            for (const auto& word : juce::StringArray::fromTokens (paragraph, " ", ""))
            {
                const auto candidate = line.isEmpty() ? word : line + " " + word;
                if (line.isNotEmpty() && Dine::textWidth (font, candidate) > width)
                {
                    lines.add (line);
                    line = word;
                }
                else line = candidate;
            }
            lines.add (line);
        }
        return lines;
    }

    juce::String whenText (long long ms)
    {
        if (ms <= 0) return {};
        const juce::Time t (ms);
        return t.getWeekdayName (true) + " " + juce::String (t.getDayOfMonth()) + " " + t.getMonthName (true) + ", "
             + t.toString (false, true, false, true);
    }

    juce::Colour lampColour (ReadinessStatus s) noexcept
    {
        switch (s)
        {
            case ReadinessStatus::Checked:        return Dine::accent;
            case ReadinessStatus::NeedsAttention: return Dine::warn;
            case ReadinessStatus::NotNeeded:      return Dine::ink4;
            case ReadinessStatus::Pending:        break;
        }
        return Dine::ink3;
    }

    std::unique_ptr<DineButton> segment (const juce::String& text)
    {
        auto b = std::make_unique<DineButton> (text, DineButton::Style::Segment);
        b->setFontPx (12.0f);
        return b;
    }

    std::unique_ptr<DineButton> link (const juce::String& text)
    {
        auto b = std::make_unique<DineButton> (text, DineButton::Style::Ghost);
        b->setFontPx (11.5f);
        return b;
    }
}

BroadcastReadinessSheet::BroadcastReadinessSheet (MixController& c, AppServices& s, Mode m)
    : controller (c), services (s), mode (m)
{
    // Opening the sheet is not an edit: only a checklist that did not exist yet is written down.
    if (controller.getReadiness().active.id.empty())
    {
        controller.editReadiness().ensureActive();
        controller.touch();
    }

    closeButton.setIcon (Dine::Icon::Close);
    closeButton.setTooltip ("Close. Everything marked here is kept with the session.");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    checklistTab.onClick = [this] { setMode (Mode::Checklist); };
    historyTab.onClick = [this] { setMode (Mode::History); };
    backButton.onClick = [this] { openRecord.clear(); correcting = false; later ([this] { rebuild(); }); };
    startOverButton.setTooltip ("Everything back to To do for this broadcast. Notes stay. The mix is not touched.");
    startOverButton.onClick = [this] { startOver(); };
    reopenButton.setTooltip ("Change this broadcast's answers again. Finish puts it back in Past broadcasts.");
    reopenButton.onClick = [this]
    {
        controller.editReadiness().reopenActive();
        controller.touch();
        later ([this] { rebuild(); });
    };
    correctButton.onClick = [this]
    {
        correcting = ! correcting;
        if (! correcting) controller.touch();
        later ([this] { rebuild(); });
    };
    finishButton.setTooltip ("Keep this broadcast in Past broadcasts as it stands. Anything still to do stays to do.");
    finishButton.onClick = [this] { finishService(); };
    nextButton.setTooltip ("A fresh checklist for the next broadcast. This one stays in Past broadcasts.");
    nextButton.onClick = [this] { startNextService(); };

    for (auto* b : { &closeButton, &checklistTab, &historyTab, &backButton, &startOverButton, &reopenButton,
                     &correctButton, &finishButton, &nextButton })
    {
        if (b != &closeButton) b->setFontPx (12.0f);
        addAndMakeVisible (*b);
    }
    for (auto* f : { &nameField, &byField })
    {
        Dine::styleTextEditor (*f, Dine::control);
        f->setFont (Dine::text (12.5f));
        f->setIndents (8, 6);
        f->onReturnKey = [this] { commitMeta(); grabKeyboardFocus(); };
        f->onFocusLost = [this] { commitMeta(); };
        f->onEscapeKey = [this] { builtFor.clear(); grabKeyboardFocus(); };
        addAndMakeVisible (*f);
    }
    nameField.setTextToShowWhenEmpty ("Sunday morning", Dine::ink4);
    byField.setTextToShowWhenEmpty ("Your name", Dine::ink4);
    byField.setTooltip ("Who ran the checklist. A name on this Mac only - no accounts.");

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    Dine::nativeScrolling (viewport);
    addAndMakeVisible (viewport);
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);
    rebuild();
}

BroadcastReadinessSheet::~BroadcastReadinessSheet()
{
    if (dialog != nullptr) dialog->exitModalState (0);
}

void BroadcastReadinessSheet::lookAndFeelChanged()
{
    for (auto* f : { &nameField, &byField }) Dine::styleTextEditor (*f, Dine::control);
    builtFor.clear();
}

void BroadcastReadinessSheet::later (std::function<void()> fn)
{
    juce::Component::SafePointer<BroadcastReadinessSheet> safe (this);
    juce::MessageManager::callAsync ([safe, fn = std::move (fn)] { if (safe != nullptr) fn(); });
}

void BroadcastReadinessSheet::setMode (Mode m)
{
    if (m == mode) return;
    mode = m;
    openRecord.clear();
    correcting = false;
    later ([this] { rebuild(); });
}

// ------------------------------------------------------------------ what is shown
const ReadinessRecord* BroadcastReadinessSheet::shown() const
{
    const auto& r = controller.getReadiness();
    if (mode == Mode::Checklist) return &r.active;
    return openRecord.empty() ? nullptr : r.historyFind (openRecord);
}

ReadinessRecord* BroadcastReadinessSheet::shownMutable()
{
    auto& r = controller.editReadiness();
    if (mode == Mode::Checklist) return &r.active;
    return openRecord.empty() ? nullptr : r.historyMutable (openRecord);
}

bool BroadcastReadinessSheet::shownEditable() const
{
    const auto* rec = shown();
    if (rec == nullptr) return false;
    return mode == Mode::Checklist ? ! rec->finished : correcting;
}

int BroadcastReadinessSheet::numItemRows() const noexcept
{
    int n = 0;
    for (const auto& r : rows) if (r->kind == Row::Kind::Item) ++n;
    return n;
}

// Everything the list shows, as one string. The tick compares it, so the list is rebuilt when
// something it shows has changed and never otherwise.
std::string BroadcastReadinessSheet::fingerprint() const
{
    const auto& r = controller.getReadiness();
    // Not the width (resized() re-wraps the same rows) and not DINE's observations, which move
    // with the meters every tick (refresh() updates those in place, on the rows that exist).
    std::string f = std::to_string (int (mode)) + "|" + openRecord + "|" + (correcting ? "c" : "") + "|"
                  + std::to_string (r.history.size());
    if (const auto* rec = shown())
    {
        f += "|" + rec->id + (rec->finished ? "F" : "") + rec->name + "/" + rec->operatorName;
        for (const auto& it : rec->items)
            f += char ('0' + int (it.status)) + std::string (it.needsReview ? "r" : "") + it.note + ";";
    }
    if (mode == Mode::History && openRecord.empty())
        for (const auto& h : r.history) f += h.id + std::to_string (h.progress().checked) + h.name;
    return f;
}

void BroadcastReadinessSheet::refresh()
{
    if (fingerprint() != builtFor) { rebuild(); return; }
    // What DINE can see moves with the meters: a few times a second, the text on the rows that
    // already exist is updated and re-wrapped. No control is made again.
    if (mode != Mode::Checklist || (++ticks % 8) != 0) return;
    bool changed = false;
    const auto hints = controller.readinessHints();
    for (auto& r : rows)
    {
        if (r->kind != Row::Kind::Item) continue;
        juce::String seen;
        bool concerning = false;
        for (const auto& h : hints)
            if (h.item == r->item) { seen = "Now: " + juce::String (h.text); concerning = h.concerning; }
        if (seen != r->seen || concerning != r->concerning) { r->seen = seen; r->concerning = concerning; changed = true; }
    }
    if (changed) { layoutRows(); list.repaint(); }
}

// ------------------------------------------------------------------ building
void BroadcastReadinessSheet::rebuild()
{
    ++rebuilds;
    rows.clear();
    list.removeAllChildren();

    if (mode == Mode::Checklist || ! openRecord.empty()) buildChecklist();
    else buildHistory();

    // The meta fields follow the record unless somebody is typing in them.
    const auto* rec = shown();
    const bool showMeta = rec != nullptr;
    for (auto* f : { &nameField, &byField })
    {
        f->setVisible (showMeta);
        f->setReadOnly (! shownEditable());
    }
    if (rec != nullptr)
    {
        if (! nameField.hasKeyboardFocus (true)) nameField.setText (juce::String (rec->name), false);
        if (! byField.hasKeyboardFocus (true)) byField.setText (juce::String (rec->operatorName), false);
    }

    checklistTab.setToggleState (mode == Mode::Checklist, juce::dontSendNotification);
    historyTab.setToggleState (mode == Mode::History, juce::dontSendNotification);
    {
        const int past = int (controller.getReadiness().history.size());
        historyTab.setButtonText (past > 0 ? "Past broadcasts (" + juce::String (past) + ")" : juce::String ("Past broadcasts"));
    }

    const bool active = mode == Mode::Checklist;
    const bool finished = active && rec != nullptr && rec->finished;
    startOverButton.setVisible (active && ! finished);
    finishButton.setVisible (active && ! finished);
    reopenButton.setVisible (finished);
    nextButton.setVisible (finished);
    backButton.setVisible (! active && ! openRecord.empty());
    correctButton.setVisible (! active && ! openRecord.empty());
    correctButton.setButtonText (correcting ? "Done" : "Correct it");
    correctButton.setTooltip (correcting ? "Keep the corrections."
                                         : "Change this past broadcast's answers - for a mistake, not a different broadcast.");

    resized();
    builtFor = fingerprint();
    repaint();
}

void BroadcastReadinessSheet::buildChecklist()
{
    const auto* rec = shown();
    if (rec == nullptr) return;
    const bool editable = shownEditable();
    const bool active = mode == Mode::Checklist;
    const auto hints = active ? controller.readinessHints() : std::vector<MixController::ReadinessHint> {};

    ReadinessGroup last = ReadinessGroup (-1);
    for (int i = 0; i < kReadinessItemCount; ++i)
    {
        const auto id = ReadinessItemId (i);
        const auto& def = readinessItemDef (id);
        if (def.group != last)
        {
            last = def.group;
            auto caption = std::make_unique<Row>();
            caption->kind = Row::Kind::Caption;
            caption->title = readinessGroupName (def.group);
            rows.push_back (std::move (caption));
        }
        const auto& state = rec->items[size_t (i)];
        auto row = std::make_unique<Row>();
        row->kind = Row::Kind::Item;
        row->item = id;
        row->status = state.status;
        row->needsReview = state.needsReview;
        row->allowsSkip = def.allowsNotNeeded;
        row->editable = editable;
        row->title = def.label;
        row->why = def.guidance;
        row->note = state.note.empty() ? juce::String() : "Note: " + juce::String (state.note);
        for (const auto& h : hints)
            if (h.item == id) { row->seen = "Now: " + juce::String (h.text); row->concerning = h.concerning; }

        auto bind = [this, id] (DineButton& b, ReadinessStatus st, bool lit)
        {
            b.setToggleState (lit, juce::dontSendNotification);
            // The lit one again is "not answered yet": every answer can be taken back.
            b.onClick = [this, id, st, lit] { answer (id, lit ? ReadinessStatus::Pending : st); };
        };
        row->done = segment ("Done");
        bind (*row->done, ReadinessStatus::Checked, state.status == ReadinessStatus::Checked);
        row->problem = segment ("Problem");
        bind (*row->problem, ReadinessStatus::NeedsAttention, state.status == ReadinessStatus::NeedsAttention);
        if (def.allowsNotNeeded)
        {
            row->skip = segment ("Skip");
            row->skip->setTooltip ("Not part of this broadcast (no room mics, nothing recorded).");
            bind (*row->skip, ReadinessStatus::NotNeeded, state.status == ReadinessStatus::NotNeeded);
        }
        for (auto* b : { row->done.get(), row->problem.get(), row->skip.get() })
            if (b != nullptr) { b->setEnabled (editable); list.addAndMakeVisible (*b); }

        // A note is for a problem, or for one already written - not a link on every row.
        if (editable && (state.status == ReadinessStatus::NeedsAttention || ! state.note.empty()))
        {
            row->noteButton = link (state.note.empty() ? "Add a note" : "Edit note");
            row->noteButton->onClick = [this, id] { askNote (id); };
            list.addAndMakeVisible (*row->noteButton);
        }
        // The one place DINE can help with the check: open the screen it is about. Nothing runs.
        if (active)
        {
            std::function<void()>* target = nullptr;
            juce::String label;
            if (id == ReadinessItemId::InputLevels)           { label = "Check inputs"; target = &onOpenCheck; }
            else if (id == ReadinessItemId::BroadcastOutput)  { label = "Outputs";      target = &onOpenOutputs; }
            else if (id == ReadinessItemId::RecoverySnapshot) { label = "Mix history";  target = &onOpenHistory; }
            if (target != nullptr)
            {
                row->shortcut = link (label);
                // A copy: the window closes this sheet to open the other one, which destroys `*target`.
                row->shortcut->onClick = [this, target] { later ([fn = *target] { if (fn) fn(); }); };
                list.addAndMakeVisible (*row->shortcut);
            }
        }
        rows.push_back (std::move (row));
    }
}

void BroadcastReadinessSheet::buildHistory()
{
    const auto& readiness = controller.getReadiness();
    const auto summary = readiness.summarise (0, 0, {});

    if (summary.broadcasts > 0)
    {
        // What keeps happening, which is what a list of services is good for.
        int topPending = -1, topProblem = -1, pendingN = 0, problemN = 0;
        for (int i = 0; i < kReadinessItemCount; ++i)
        {
            if (summary.pendingCount[size_t (i)] > pendingN) { pendingN = summary.pendingCount[size_t (i)]; topPending = i; }
            if (summary.attentionCount[size_t (i)] > problemN) { problemN = summary.attentionCount[size_t (i)]; topProblem = i; }
        }
        juce::String text;
        if (topProblem >= 0)
            text << "Most often a problem: " << readinessItemDef (ReadinessItemId (topProblem)).label
                 << " (" << problemN << " of " << summary.broadcasts << "). ";
        if (topPending >= 0)
            text << "Most often left to do: " << readinessItemDef (ReadinessItemId (topPending)).label
                 << " (" << pendingN << " of " << summary.broadcasts << ").";
        if (text.isNotEmpty())
        {
            auto row = std::make_unique<Row>();
            row->kind = Row::Kind::Note;
            row->why = text.trim();
            rows.push_back (std::move (row));
        }
    }

    for (const auto* rec : readiness.filterHistory (0, 0, {}))
    {
        auto row = std::make_unique<Row>();
        row->kind = Row::Kind::Record;
        row->recordId = rec->id;
        row->title = juce::String (rec->name);
        const auto p = rec->progress();
        row->value = juce::String (p.checked) + " of " + juce::String (p.applicable) + " done";
        juce::String when = whenText (rec->finishedMs > 0 ? rec->finishedMs : rec->startedMs);
        if (! rec->operatorName.empty()) when << ", checked by " << juce::String (rec->operatorName);
        if (p.needsAttention > 0) when << ". " << p.needsAttention << (p.needsAttention == 1 ? " problem" : " problems");
        if (p.pending > 0) when << (p.needsAttention > 0 ? ", " : ". ") << p.pending << " left to do";
        row->why = when + ".";
        juce::StringArray problems;
        for (int i = 0; i < kReadinessItemCount; ++i)
        {
            const auto& it = rec->items[size_t (i)];
            if (it.status != ReadinessStatus::NeedsAttention && it.note.empty()) continue;
            juce::String p = readinessItemDef (ReadinessItemId (i)).label;
            if (! it.note.empty()) p << ": " << juce::String (it.note);
            problems.add (p);
        }
        row->note = problems.joinIntoString ("\n");
        row->concerning = rec->progress().needsAttention > 0;
        row->open = link ("Open");
        const auto id = rec->id;
        row->open->onClick = [this, id] { openRecord = id; correcting = false; later ([this] { rebuild(); }); };
        list.addAndMakeVisible (*row->open);
        rows.push_back (std::move (row));
    }

    if (rows.empty())
    {
        auto row = std::make_unique<Row>();
        row->kind = Row::Kind::Note;
        row->why = "Nothing here yet. A broadcast lands here when its checklist is finished.";
        rows.push_back (std::move (row));
    }
}

// Every row's height comes from its own text at the width it has, so a long sentence makes a
// taller row instead of running into the next one.
void BroadcastReadinessSheet::layoutRows()
{
    const int w = juce::jmax (300, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
    int y = 0;
    for (auto& r : rows)
    {
        switch (r->kind)
        {
            case Row::Kind::Caption:
                r->height = kCaptionH;
                break;
            case Row::Kind::Note:
                r->whyLines = wrap (bodyFont(), r->why, w);
                r->height = kPadY + r->whyLines.size() * kLineH + kPadY;
                break;
            case Row::Kind::Record:
            {
                const int textW = w - kGapX - 160;
                r->whyLines = wrap (bodyFont(), r->why, textW);
                r->noteLines = wrap (bodyFont(), r->note, textW);
                r->height = kPadY + kTitleH + (r->whyLines.size() + r->noteLines.size()) * kLineH + kPadY;
                if (r->open != nullptr)
                    r->open->setBounds (w - 64, y + kPadY + kTitleH + 2, 64, kLinkH);
                break;
            }
            case Row::Kind::Item:
            {
                const int textW = w - kLampW - kGapX - kControlsW;
                r->whyLines = wrap (bodyFont(), r->why, textW);
                r->seenLines = wrap (bodyFont(), r->seen, textW);
                r->noteLines = wrap (bodyFont(), r->note, textW);
                const int textH = kPadY + kTitleH + 2 + (r->whyLines.size() + r->seenLines.size() + r->noteLines.size()) * kLineH + kPadY;
                const bool links = r->noteButton != nullptr || r->shortcut != nullptr;
                const int controlsH = kPadY + kSegH + (links ? 6 + kLinkH : 0) + kPadY;
                r->height = juce::jmax (textH, controlsH);

                auto controls = juce::Rectangle<int> (w - kControlsW, y + kPadY, kControlsW, kSegH);
                const int n = r->skip != nullptr ? 3 : 2;
                const int segW = (kControlsW - 4 - (n - 1) * 2) / n;
                auto track = controls.reduced (2, 2);
                for (auto* b : { r->done.get(), r->problem.get(), r->skip.get() })
                {
                    if (b == nullptr) continue;
                    b->setBounds (track.removeFromLeft (segW));
                    track.removeFromLeft (2);
                }
                auto links2 = juce::Rectangle<int> (w - kControlsW, controls.getBottom() + 6, kControlsW, kLinkH);
                if (r->noteButton != nullptr)
                    r->noteButton->setBounds (links2.removeFromRight (juce::jmax (80, r->noteButton->idealWidth())));
                if (r->shortcut != nullptr)
                {
                    links2.removeFromRight (4);
                    r->shortcut->setBounds (links2.removeFromRight (juce::jmax (80, r->shortcut->idealWidth())));
                }
                break;
            }
        }
        y += r->height;
    }
    list.setSize (w, juce::jmax (y, 40));
    if (y != contentH)
    {
        contentH = y;                  // the card's height follows; lay out once more at the new size
        juce::Component::SafePointer<BroadcastReadinessSheet> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->resized(); safe->repaint(); } });
    }
}

// ------------------------------------------------------------------ painting
juce::Rectangle<int> BroadcastReadinessSheet::cardBounds() const
{
    // As tall as what it holds, up to 740: one past service is not a screenful of empty card.
    const int chrome = kPad + 24 + 8 + 18 + 16 + 30 + 14 + (nameField.isVisible() ? 54 : 0) + 14 + Dine::Metric::button + kPad;
    const int h = juce::jlimit (360, 740, chrome + juce::jmax (120, contentH));
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (kCardW, getWidth() - 60), juce::jmin (h, getHeight() - 40));
}

void BroadcastReadinessSheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.84f));
    auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), 14.0f);

    auto r = card.reduced (kPad, kPad);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (19.0f, 600));
    Dine::drawText (g, "Broadcast readiness", r.removeFromTop (24).withTrimmedRight (40), juce::Justification::centredLeft);

    // WHERE THINGS STAND, in one line under the title.
    juce::String line;
    const auto& readiness = controller.getReadiness();
    if (const auto* rec = shown())
    {
        const auto p = rec->progress();
        if (rec->finished)
            line << "Finished " << whenText (rec->finishedMs)
                 << (rec->operatorName.empty() ? juce::String() : " by " + juce::String (rec->operatorName))
                 << ". " << juce::String (p.summaryLine()).substring (0, 1).toUpperCase()
                 << juce::String (p.summaryLine()).substring (1) << ".";
        else
            line << juce::String (p.summaryLine()).substring (0, 1).toUpperCase() << juce::String (p.summaryLine()).substring (1)
                 << ". Ticks are what someone heard, not proof the stream is right.";
    }
    else
    {
        const int n = int (readiness.history.size());
        line << (n == 0 ? juce::String ("No past broadcasts yet.")
                        : juce::String (n) + (n == 1 ? " past broadcast" : " past broadcasts") + ", newest first.");
    }
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.5f));
    Dine::drawFittedText (g, line, headerLine, juce::Justification::centredLeft, 1, 0.9f);

    // The tabs' track, and the captions over the two fields.
    {
        const auto tabs = checklistTab.getBounds().getUnion (historyTab.getBounds()).expanded (2);
        Dine::fillRounded (g, tabs.toFloat(), Dine::control, Dine::Radius::control);
    }
    if (nameField.isVisible())
    {
        g.setColour (Dine::ink3);
        g.setFont (captionFont());
        Dine::drawText (g, "Broadcast", nameField.getBounds().withY (metaArea.getY()).withHeight (14), juce::Justification::centredLeft);
        Dine::drawText (g, "Checked by", byField.getBounds().withY (metaArea.getY()).withHeight (14), juce::Justification::centredLeft);
    }
}

void BroadcastReadinessSheet::paintRows (juce::Graphics& g)
{
    const int w = list.getWidth();
    int y = 0;
    for (size_t k = 0; k < rows.size(); ++k)
    {
        auto& r = *rows[k];
        auto area = juce::Rectangle<int> (0, y, w, r.height);
        y += r.height;
        if (r.kind == Row::Kind::Caption)
        {
            Dine::drawSection (g, area.withTrimmedTop (12).withHeight (18), r.title);
            continue;
        }
        // Rows are separated by space and a hairline, never boxed (List Row, 61:9171).
        if (k + 1 < rows.size() && rows[k + 1]->kind != Row::Kind::Caption)
            Dine::drawRule (g, area.removeFromBottom (1));

        auto drawLines = [&g] (const juce::StringArray& lines, juce::Rectangle<int>& at, juce::Colour c)
        {
            g.setColour (c);
            g.setFont (bodyFont());
            for (const auto& l : lines) Dine::drawText (g, l, at.removeFromTop (kLineH), juce::Justification::centredLeft, false);
        };

        if (r.kind == Row::Kind::Note)
        {
            auto t = area.reduced (0, kPadY);
            drawLines (r.whyLines, t, Dine::ink2);
            continue;
        }

        if (r.kind == Row::Kind::Record)
        {
            auto t = area.reduced (0, kPadY);
            auto right = t.removeFromRight (160);
            g.setColour (r.concerning ? Dine::warn : Dine::ink2);
            g.setFont (Dine::mono (11.0f, 500));
            Dine::drawFittedText (g, r.value, right.removeFromTop (kTitleH), juce::Justification::centredRight, 1, 0.8f);
            t.removeFromRight (kGapX);
            g.setColour (Dine::ink);
            g.setFont (titleFont());
            Dine::drawFittedText (g, r.title, t.removeFromTop (kTitleH), juce::Justification::centredLeft, 1, 0.9f);
            drawLines (r.whyLines, t, Dine::ink3);
            drawLines (r.noteLines, t, r.concerning ? Dine::warn : Dine::ink2);
            continue;
        }

        // An item: lamp, what, why, what DINE can see, the note; the answer is on the right,
        // DONE / PROBLEM / SKIP on one segment track (Segment, 61:9145).
        Dine::fillRounded (g, juce::Rectangle<float> (float (w - kControlsW), float (area.getY() + kPadY), float (kControlsW), float (kSegH)),
                           Dine::control, Dine::Radius::control);
        auto t = area.reduced (0, kPadY);
        auto lamp = t.removeFromLeft (kLampW).removeFromTop (kTitleH).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
        const auto lc = lampColour (r.status);
        if (r.status == ReadinessStatus::Pending)
        {
            g.setColour (lc);
            g.drawEllipse (lamp.reduced (0.7f), 1.4f);
        }
        else if (r.status == ReadinessStatus::NotNeeded)
        {
            g.setColour (lc);
            g.fillRect (lamp.withSizeKeepingCentre (8.0f, 1.6f));
        }
        else
        {
            g.setColour (lc);
            g.fillEllipse (lamp);
        }
        t.removeFromRight (kControlsW + kGapX);

        auto titleRow = t.removeFromTop (kTitleH);
        g.setColour (r.status == ReadinessStatus::NotNeeded ? Dine::ink3 : Dine::ink);
        g.setFont (titleFont());
        const int titleW = juce::jmin (titleRow.getWidth(), Dine::textWidth (titleFont(), r.title) + 2);
        Dine::drawFittedText (g, r.title, titleRow.removeFromLeft (titleW), juce::Justification::centredLeft, 1, 0.85f);
        if (r.needsReview)
        {
            // Something it depends on changed after it was answered: say so beside it.
            const juce::String chip = "Changed since";
            const float cw = Dine::pillWidth (chip, false);
            titleRow.removeFromLeft (8);
            if (titleRow.getWidth() >= int (cw))
                Dine::drawStatusChip (g, titleRow.removeFromLeft (int (cw)).toFloat().withSizeKeepingCentre (cw, 16.0f), chip, Dine::warn);
        }
        t.removeFromTop (2);
        drawLines (r.whyLines, t, Dine::ink3);
        drawLines (r.seenLines, t, r.concerning ? Dine::warn : Dine::ink2);
        drawLines (r.noteLines, t, Dine::ink2);
    }
}

void BroadcastReadinessSheet::resized()
{
    auto card = cardBounds().reduced (kPad, kPad);
    auto top = card.removeFromTop (24);
    closeButton.setBounds (top.removeFromRight (28).withSizeKeepingCentre (28, 28));
    card.removeFromTop (8);
    headerLine = card.removeFromTop (18);
    card.removeFromTop (16);

    auto tabs = card.removeFromTop (30);
    {
        auto track = tabs.reduced (2, 2);
        checklistTab.setBounds (track.removeFromLeft (juce::jmax (112, checklistTab.idealWidth() + 16)));
        track.removeFromLeft (2);
        historyTab.setBounds (track.removeFromLeft (juce::jmax (112, historyTab.idealWidth() + 16)));
        backButton.setBounds (tabs.removeFromRight (juce::jmax (100, backButton.idealWidth() + 8)));
    }
    card.removeFromTop (14);

    if (nameField.isVisible())
    {
        auto meta = card.removeFromTop (14 + 4 + 30);
        metaArea = meta;
        meta.removeFromTop (18);
        nameField.setBounds (meta.removeFromLeft (juce::jmin (300, meta.getWidth() / 2 - 8)));
        meta.removeFromLeft (16);
        byField.setBounds (meta.removeFromLeft (juce::jmin (220, meta.getWidth())));
        card.removeFromTop (6);
    }

    auto foot = card.removeFromBottom (Dine::Metric::button);
    card.removeFromBottom (14);
    // Sheet Footer (65:9354): the quiet action left, the default one last on the right.
    auto place = [&foot] (DineButton& b, bool right)
    {
        if (! b.isVisible()) return;
        const int w = juce::jmax (84, b.idealWidth() + 8);
        b.setBounds (right ? foot.removeFromRight (w) : foot.removeFromLeft (w));
        if (right) foot.removeFromRight (8); else foot.removeFromLeft (8);
    };
    place (finishButton, true);
    place (nextButton, true);
    place (reopenButton, true);
    place (correctButton, true);
    place (startOverButton, false);

    viewport.setBounds (card);
    layoutRows();
}

void BroadcastReadinessSheet::mouseUp (const juce::MouseEvent& e)
{
    // A click on the dimmed desk around the card closes it, as every sheet does.
    if (e.eventComponent == this && ! cardBounds().contains (e.getPosition()))
        if (onClose) onClose();
}

// ------------------------------------------------------------------ answers
void BroadcastReadinessSheet::answer (ReadinessItemId id, ReadinessStatus st)
{
    if (! shownEditable()) return;
    if (mode == Mode::Checklist) controller.editReadiness().setItem (id, st);
    else if (auto* rec = shownMutable()) setReadinessItem (*rec, id, st);
    controller.touch();
    later ([this] { refresh(); });
}

void BroadcastReadinessSheet::askNote (ReadinessItemId id)
{
    const auto* rec = shown();
    if (rec == nullptr || ! shownEditable()) return;
    dialog = std::make_unique<juce::AlertWindow> (readinessItemDef (id).label, "What did you hear, or what still needs doing?",
                                                  juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("note", juce::String (rec->items[size_t (id)].note), "Note");
    dialog->addButton ("Keep the note", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<BroadcastReadinessSheet> safe (this);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, id] (int result)
    {
        if (safe == nullptr || safe->dialog == nullptr) return;
        const auto text = safe->dialog->getTextEditorContents ("note").trim().toStdString();
        safe->dialog.reset();
        if (result != 1) return;
        if (auto* r = safe->shownMutable()) r->items[size_t (id)].note = text;
        safe->controller.touch();
        safe->refresh();
    }), false);
}

void BroadcastReadinessSheet::commitMeta()
{
    if (! shownEditable()) return;
    auto* rec = shownMutable();
    if (rec == nullptr) return;
    const auto name = nameField.getText().trim().toStdString();
    const auto by = byField.getText().trim().toStdString();
    if ((name.empty() || name == rec->name) && by == rec->operatorName) return;
    if (! name.empty()) rec->name = name;
    rec->operatorName = by;
    controller.editReadiness().rememberOperator (by);
    controller.touch();
}

void BroadcastReadinessSheet::finishService()
{
    commitMeta();
    controller.editReadiness().finishActive();
    controller.touch();
    if (onToast) onToast ("Kept in Past broadcasts. Nothing about the mix changed.");
    later ([this] { rebuild(); });
}

void BroadcastReadinessSheet::startNextService()
{
    auto& r = controller.editReadiness();
    const auto who = r.active.operatorName;
    r.newService ("Broadcast " + juce::Time::getCurrentTime().formatted ("%e %b").trim().toStdString(), who);
    controller.touch();
    later ([this] { rebuild(); nameField.grabKeyboardFocus(); nameField.selectAll(); });
}

void BroadcastReadinessSheet::startOver()
{
    controller.editReadiness().resetActiveStatuses();
    controller.touch();
    if (onToast) onToast ("Every check is back to To do. Notes kept; the mix is not touched.");
    later ([this] { refresh(); });
}

} // namespace livemix
