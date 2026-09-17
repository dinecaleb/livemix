#include "ChannelRail.h"
#include "UI/Widgets.h"

namespace livemix
{

// ---------------------------------------------------------------- the list
// One component for every row on the console. Group headings and channels are painted in
// the same pass from the same vector, so nothing can get out of order, and the cost of a
// tick is the meter cells that moved rather than forty-eight components laying out.
class ChannelRail::List : public juce::Component
{
public:
    explicit List (ChannelRail& o) : rail (o) { setOpaque (true); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Dine::toolbar);
        auto clip = g.getClipBounds();
        int y = 0;
        for (const int index : rail.shown)
        {
            const auto& e = rail.entries[size_t (index)];
            const int h = rail.rowHeight (e);
            juce::Rectangle<int> r (0, y, getWidth(), h);
            y += h;
            if (! r.intersects (clip)) continue;
            if (e.header) paintHeader (g, r, e);
            else          paintRow (g, r, e, index);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int index = rail.indexAt (e.getPosition());
        if (index < 0) return;
        if (e.mods.isPopupMenu()) { rail.rowMenu (index); return; }
        rail.clicked (index, false);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int index = rail.indexAt (e.getPosition());
        if (index >= 0) rail.clicked (index, true);
    }
    void mouseMove (const juce::MouseEvent& e) override { setHover (rail.indexAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override    { setHover (-1); }

    void setHover (int index)
    {
        if (index == hovered) return;
        const int was = hovered;
        hovered = index;
        repaintEntry (was);
        repaintEntry (hovered);
    }

    // The one place a row's rectangle is worked out, so a repaint and a hit test agree.
    juce::Rectangle<int> boundsOf (int entryIndex) const
    {
        int y = 0;
        for (const int index : rail.shown)
        {
            const auto& e = rail.entries[size_t (index)];
            const int h = rail.rowHeight (e);
            if (index == entryIndex) return { 0, y, getWidth(), h };
            y += h;
        }
        return {};
    }

    void repaintEntry (int entryIndex)
    {
        if (entryIndex < 0) return;
        const auto r = boundsOf (entryIndex);
        if (! r.isEmpty()) repaint (r);
    }

    int hovered = -1;

private:
    static void paintHeader (juce::Graphics& g, juce::Rectangle<int> r, const Entry& e)
    {
        g.setColour (Dine::window.withAlpha (0.92f));
        g.fillRect (r);
        Dine::drawRule (g, r.removeFromBottom (1), Dine::hairSoft);
        auto inner = r.reduced (12, 0);
        auto dot = inner.removeFromLeft (5).withSizeKeepingCentre (5, 5);
        g.setColour (e.tint);
        g.fillRoundedRectangle (dot.toFloat(), 1.0f);
        inner.removeFromLeft (7);
        auto count = inner.removeFromRight (22);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (9.5f, 600).withExtraKerningFactor (0.13f));
        g.drawText (e.name, inner, juce::Justification::centredLeft);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (9.5f));
        g.drawText (juce::String (e.count), count, juce::Justification::centredRight);
    }

    void paintRow (juce::Graphics& g, juce::Rectangle<int> r, const Entry& e, int index)
    {
        const bool isSelected = e.isBus ? (rail.busSelected && rail.selectedBus == e.bus)
                                        : (! rail.busSelected && rail.selected == e.strip);
        if (isSelected)
        {
            g.setColour (Dine::selected);
            g.fillRect (r);
            g.setColour (Dine::accent);
            g.fillRect (r.getX(), r.getY(), 2, r.getHeight());
        }
        else if (index == hovered)
        {
            g.setColour (Dine::fillSoft);
            g.fillRect (r);
        }

        auto inner = r.reduced (0, 0).withTrimmedLeft (12).withTrimmedRight (10);
        Dine::drawIcon (g, e.icon, inner.removeFromLeft (14).toFloat().withSizeKeepingCentre (14.0f, 14.0f),
                        isSelected ? e.tint : Dine::glyph, 1.3f);
        inner.removeFromLeft (8);

        auto flag = inner.removeFromRight (14);
        inner.removeFromRight (4);
        auto meter = inner.removeFromRight (34).withSizeKeepingCentre (34, 3);
        inner.removeFromRight (6);

        auto text = inner;
        g.setColour (isSelected ? Dine::ink : Dine::ink2);
        g.setFont (Dine::text (11.5f));
        g.drawText (e.name, text.removeFromTop (r.getHeight() / 2 + 2).withTrimmedTop (4),
                    juce::Justification::centredLeft, true);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (9.0f));
        g.drawText (e.sub, text.withTrimmedBottom (3), juce::Justification::centredLeft, true);

        // The meter: a cut track with a bar of the level's own colour. It is the only lime
        // on the row, because it is the only thing on the row that is the audio.
        g.setColour (Dine::well);
        g.fillRoundedRectangle (meter.toFloat(), 1.5f);
        const float n = DineMeter::norm (e.peak);
        if (n > 0.001f)
        {
            g.setColour (Dine::levelColour (e.peak));
            g.fillRoundedRectangle (meter.toFloat().withWidth (juce::jmax (2.0f, float (meter.getWidth()) * n)), 1.5f);
        }

        if (e.flag != ' ')
        {
            g.setColour (e.flagColour);
            g.setFont (Dine::mono (9.5f, 700));
            g.drawText (juce::String::charToString (juce::juce_wchar (e.flag)), flag, juce::Justification::centredRight);
        }
    }

    ChannelRail& rail;
};

// The heading of whatever group is under the top of the list, pinned there. A console has
// more channels than rows, and a name with no group over it is half an answer.
class ChannelRail::Sticky : public juce::Component
{
public:
    Sticky() { setInterceptsMouseClicks (false, false); }

    void set (const juce::String& n, juce::Colour t, int c)
    {
        if (n == name && t == tint && c == count) return;
        name = n; tint = t; count = c;
        setVisible (name.isNotEmpty());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (Dine::window);
        g.fillRect (r);
        Dine::drawRule (g, r.removeFromBottom (1), Dine::hairSoft);
        auto inner = r.reduced (12, 0);
        auto dot = inner.removeFromLeft (5).withSizeKeepingCentre (5, 5);
        g.setColour (tint);
        g.fillRoundedRectangle (dot.toFloat(), 1.0f);
        inner.removeFromLeft (7);
        auto countArea = inner.removeFromRight (22);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (9.5f, 600).withExtraKerningFactor (0.13f));
        g.drawText (name, inner, juce::Justification::centredLeft);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (9.5f));
        g.drawText (juce::String (count), countArea, juce::Justification::centredRight);
    }

private:
    juce::String name;
    juce::Colour tint { Dine::ink2 };
    int count = 0;
};

// ---------------------------------------------------------------- the rail
ChannelRail::ChannelRail (MixController& c) : controller (c)
{
    list = std::make_unique<List> (*this);
    sticky = std::make_unique<Sticky>();

    view.setViewedComponent (list.get(), false);
    view.setScrollBarsShown (true, false, true, false);
    Dine::nativeScrolling (view);
    addAndMakeVisible (view);
    addChildComponent (*sticky);

    search.setTextToShowWhenEmpty ("Find a channel", Dine::ink4);
    search.setFont (Dine::text (11.5f));
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::textColourId, Dine::ink);
    search.setColour (juce::TextEditor::highlightColourId, Dine::accent.withAlpha (0.25f));
    search.setColour (juce::TextEditor::highlightedTextColourId, Dine::ink);
    search.setIndents (2, 2);
    search.setBorder ({});
    search.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (search);

    const char* names[3] = { "All", "Inputs", "Groups" };
    const Filter which[3] = { Filter::All, Filter::Inputs, Filter::Groups };
    for (int i = 0; i < 3; ++i)
    {
        filters[i] = std::make_unique<DineChip> (names[i]);
        filters[i]->setClickingTogglesState (false);
        filters[i]->onClick = [this, f = which[i]]
        {
            if (filter == f) return;
            filter = f;
            for (int k = 0; k < 3; ++k)
                filters[k]->setToggleState (k == int (f), juce::dontSendNotification);
            applyFilter();
        };
        addAndMakeVisible (*filters[i]);
    }
    filters[0]->setToggleState (true, juce::dontSendNotification);

    collapseButton = std::make_unique<DineButton> ("", DineButton::Style::Ghost);
    collapseButton->setIcon (Dine::Icon::Chevron);
    collapseButton->setPadX (4);
    collapseButton->setTooltip ("Hide the channel list.  Ctrl-Cmd-S");
    collapseButton->onClick = [this] { setCollapsed (true); };
    addAndMakeVisible (*collapseButton);

    handle = std::make_unique<DinePanelTab> (DinePanelTab::Side::Left, "Channels");
    handle->setCollapsed (true);
    handle->onClick = [this] { setCollapsed (false); };
    addChildComponent (*handle);

    setOpaque (true);
}

ChannelRail::~ChannelRail() { view.setViewedComponent (nullptr, false); }

void ChannelRail::setCollapsed (bool c)
{
    if (c == collapsed) return;
    collapsed = c;
    view.setVisible (! c);
    search.setVisible (! c);
    for (auto& f : filters) f->setVisible (! c);
    collapseButton->setVisible (! c);
    handle->setVisible (c);
    sticky->setVisible (! c && sticky->isVisible());
    if (onCollapsedChanged) onCollapsedChanged();
    resized();
    repaint();
}

void ChannelRail::setSelected (int strip)
{
    if (! busSelected && strip == selected) return;
    busSelected = false;
    selected = strip;
    list->repaint();
}

void ChannelRail::setSelectedBus (MixBus bus)
{
    if (busSelected && bus == selectedBus) return;
    busSelected = true;
    selectedBus = bus;
    list->repaint();
}

// ---------------------------------------------------------------- what is in it
void ChannelRail::buildEntries()
{
    entries.clear();
    if (! controller.isPrepared()) { builtFor = -1; return; }

    const auto& graph = controller.getGraph();

    // The inputs, under their group, in the order the console is laid out.
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        const MixBus bus = MixBus (b);
        std::vector<int> onBus;
        for (int i = 0; i < graph.numStrips(); ++i)
            if (graph.strips[size_t (i)].bus == bus) onBus.push_back (i);
        if (onBus.empty()) continue;

        Entry head;
        head.header = true;
        head.name = mixBusName (bus);
        head.group = head.name;
        head.tint = Dine::busTint (bus);
        head.count = int (onBus.size());
        head.bus = bus;
        entries.push_back (head);

        for (const int i : onBus)
        {
            const auto& s = graph.strips[size_t (i)];
            Entry e;
            e.strip = i;
            e.bus = bus;
            e.group = head.name;
            e.name = juce::String (s.name);
            e.sub = s.inputB >= 0 ? "In " + juce::String (s.inputA + 1) + juce::String (Glyph::dash()) + juce::String (s.inputB + 1)
                                  : "In " + juce::String (s.inputA + 1);
            e.icon = Dine::iconFor (s.icon, s.role);
            e.tint = head.tint;
            entries.push_back (e);
        }
    }

    // Then the buses themselves, so "Groups" has something to show and the master is one
    // click away from every workspace.
    Entry busHead;
    busHead.header = true;
    busHead.name = "GROUPS";
    busHead.group = "GROUPS";
    busHead.tint = Dine::busTint (MixBus::Master);
    busHead.isBus = true;
    entries.push_back (busHead);
    int busCount = 0;
    for (int b = 0; b <= int (MixBus::Master); ++b)
    {
        const MixBus bus = MixBus (b);
        if (bus != MixBus::Master && ! graph.busUsed[size_t (b)]) continue;
        Entry e;
        e.isBus = true;
        e.bus = bus;
        e.group = "GROUPS";
        e.name = bus == MixBus::Master ? "Main mix" : juce::String (mixBusName (bus)).toLowerCase().initialSectionNotContaining (" ");
        if (bus != MixBus::Master) e.name = juce::String (mixBusName (bus));
        e.sub = bus == MixBus::Master ? "everything" : juce::String (graph.stripsOnBus (bus)) + " in";
        e.icon = Dine::Icon::Bus;
        e.tint = Dine::busTint (bus);
        entries.push_back (e);
        ++busCount;
    }
    for (auto& e : entries)
        if (e.header && e.isBus) e.count = busCount;

    builtFor = graph.numStrips();
    applyFilter();
}

void ChannelRail::applyFilter()
{
    const auto needle = search.getText().trim().toLowerCase();
    shown.clear();
    shown.reserve (entries.size());

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        const bool wanted = filter == Filter::All ? true
                          : filter == Filter::Inputs ? ! e.isBus
                                                     : e.isBus;
        if (! wanted) continue;
        if (e.header) continue;                       // headers are decided by what follows
        if (needle.isNotEmpty() && ! e.name.toLowerCase().contains (needle)
            && ! e.sub.toLowerCase().contains (needle) && ! e.group.toLowerCase().contains (needle))
            continue;

        // Put this row's heading in first, if it is not already there.
        if (shown.empty() || entries[size_t (shown.back())].group != e.group)
        {
            for (int h = int (i); h >= 0; --h)
                if (entries[size_t (h)].header && entries[size_t (h)].group == e.group) { shown.push_back (h); break; }
        }
        shown.push_back (int (i));
    }

    int total = 0;
    for (const int index : shown) total += rowHeight (entries[size_t (index)]);
    list->setSize (juce::jmax (1, view.getMaximumVisibleWidth()), juce::jmax (1, total));
    list->repaint();
}

void ChannelRail::rebuild()
{
    buildEntries();
    resized();
}

// The 30 Hz tick. Everything here is a comparison first: a console that is not moving
// costs nothing, and a console that is moving costs the meter cells that moved.
void ChannelRail::refresh()
{
    if (collapsed || ! controller.isPrepared()) return;
    if (controller.getGraph().numStrips() != builtFor) { rebuild(); return; }

    const auto& engine = controller.getEngine();
    const auto& state = controller.getBase();

    const int tunes = controller.getTuneCount();
    const bool adviceDue = (adviceTicks++ % 15) == 0 || tunes != adviceFor;
    adviceFor = tunes;

    for (const int index : shown)
    {
        auto& e = entries[size_t (index)];
        if (e.header) continue;

        float peak = -120.0f;
        bool muted = false, soloed = false;
        if (e.isBus)
        {
            peak = engine.getBus (e.bus).getOutputMeter().getMaxPeakDb();
            muted = state.buses[size_t (e.bus)].mute;
            soloed = state.buses[size_t (e.bus)].solo;
        }
        else if (e.strip >= 0 && e.strip < state.numStrips)
        {
            peak = engine.getStrip (e.strip).getOutputMeter().getMaxPeakDb();
            muted = state.strips[size_t (e.strip)].mute;
            soloed = state.strips[size_t (e.strip)].solo;
        }

        // A slow release, so a meter never reads "nothing there" between two blocks.
        const float shown = peak > e.peak ? peak : juce::jmax (-120.0f, e.peak - 2.4f);

        // Mute, solo and the meter are atomics and are read every tick. The level advice is
        // not: it builds sentences, it only changes when a plan does, and forty-eight of them
        // thirty times a second is a measurable cost for news that is half a second old at
        // worst. So it is re-read twice a second, and immediately when a tune lands.
        if (adviceDue && ! e.isBus)
        {
            const auto advice = controller.getInputAdvice (e.strip);
            e.attention = advice.needsAttention()
                              ? (advice.level == MixController::InputAdvice::Level::Clipping
                                     || advice.level == MixController::InputAdvice::Level::NotHeard ? 2 : 1)
                              : 0;
        }

        char flag = ' ';
        juce::Colour flagColour = Dine::ink4;
        if (soloed)          { flag = 'S'; flagColour = Dine::keySolo; }
        else if (muted)      { flag = 'M'; flagColour = Dine::keyMute; }
        else if (e.attention){ flag = '!'; flagColour = e.attention == 2 ? Dine::crit : Dine::warn; }

        if (std::abs (shown - e.peak) > 0.4f || flag != e.flag || flagColour != e.flagColour)
        {
            e.peak = shown;
            e.flag = flag;
            e.flagColour = flagColour;
            list->repaintEntry (index);
        }
    }

    // The heading that belongs over the top of the list.
    const int top = view.getViewPositionY();
    int y = 0;
    const Entry* current = nullptr;
    for (const int index : shown)
    {
        const auto& e = entries[size_t (index)];
        const int h = rowHeight (e);
        if (e.header && y <= top + 1) current = &e;
        if (y > top) break;
        y += h;
    }
    if (current != nullptr) sticky->set (current->name, current->tint, current->count);
    else                    sticky->set ({}, Dine::ink2, 0);
}

// ---------------------------------------------------------------- hit testing
int ChannelRail::indexAt (juce::Point<int> p) const
{
    int y = 0;
    for (const int index : shown)
    {
        const auto& e = entries[size_t (index)];
        const int h = rowHeight (e);
        if (p.y >= y && p.y < y + h) return e.header ? -1 : index;
        y += h;
    }
    return -1;
}

void ChannelRail::clicked (int index, bool doubleClick)
{
    const auto& e = entries[size_t (index)];
    if (e.isBus)
    {
        setSelectedBus (e.bus);
        if (doubleClick) { if (onOpenBus) onOpenBus (e.bus); }
        else if (onSelectBus) onSelectBus (e.bus);
        return;
    }
    setSelected (e.strip);
    if (doubleClick) { if (onOpen) onOpen (e.strip); }
    else if (onSelect) onSelect (e.strip);
}

void ChannelRail::rowMenu (int index)
{
    const auto& e = entries[size_t (index)];
    if (e.isBus) { clicked (index, true); return; }
    const int strip = e.strip;
    juce::PopupMenu m;
    m.addItem (1, "Open in the Inspector");
    m.addItem (2, "TUNE CHANNEL");
    m.showMenuAsync (juce::PopupMenu::Options().withParentComponent (getTopLevelComponent()),
                     [this, strip] (int r)
                     {
                         if (r == 1 && onOpen) onOpen (strip);
                         if (r == 2 && onTune) onTune (strip);
                     });
}

// ---------------------------------------------------------------- layout
void ChannelRail::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    if (collapsed)
    {
        g.setColour (Dine::sidebar);
        g.fillRect (r);
        g.setColour (Dine::hair);
        g.fillRect (float (r.getRight()) - 0.5f, 0.0f, 0.5f, float (getHeight()));
        return;
    }

    Dine::drawPanelGround (g, r);
    g.setColour (Dine::hair);
    g.fillRect (float (r.getRight()) - 0.5f, 0.0f, 0.5f, float (getHeight()));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (float (r.getRight()) - 1.5f, 0.0f, 1.0f, float (getHeight()));

    // header
    auto head = r.removeFromTop (34);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (10.0f, 600).withExtraKerningFactor (0.14f));
    g.drawText ("CHANNELS", head.reduced (12, 0).withTrimmedRight (24), juce::Justification::centredLeft);
    Dine::drawRule (g, head.removeFromBottom (1), Dine::hairSoft);

    // the search field's own well
    auto controls = r.removeFromTop (60);
    Dine::fillRounded (g, controls.reduced (10, 0).withTrimmedTop (8).withHeight (24).toFloat(),
                       juce::Colours::white.withAlpha (0.07f), Dine::Radius::pill);
    Dine::drawIcon (g, Dine::Icon::Search,
                    juce::Rectangle<float> (float (controls.getX()) + 18.0f, float (controls.getY()) + 14.0f, 12.0f, 12.0f),
                    Dine::ink4, 1.5f);
    DineChip::drawTrack (g, controls.reduced (10, 0).withTrimmedTop (39).withHeight (24));
    Dine::drawRule (g, controls.removeFromBottom (1), Dine::hairSoft);
}

void ChannelRail::resized()
{
    auto r = getLocalBounds();
    if (collapsed)
    {
        handle->setBounds (r);
        return;
    }

    auto head = r.removeFromTop (34);
    collapseButton->setBounds (head.removeFromRight (28).withSizeKeepingCentre (20, 20));

    auto controls = r.removeFromTop (60);
    auto field = controls.reduced (10, 0).withTrimmedTop (8).withHeight (24);
    search.setBounds (field.withTrimmedLeft (26).withTrimmedRight (6));
    auto chips = controls.reduced (10, 0).withTrimmedTop (39).withHeight (24).reduced (2, 2);
    const int each = chips.getWidth() / 3;
    for (int i = 0; i < 3; ++i)
        filters[i]->setBounds (chips.removeFromLeft (i == 2 ? chips.getWidth() : each));

    view.setBounds (r);
    sticky->setBounds (r.withHeight (24));
    sticky->toFront (false);
    list->setSize (juce::jmax (1, view.getMaximumVisibleWidth()), list->getHeight());
}

} // namespace livemix
