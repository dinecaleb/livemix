#include "SetupPages.h"
#include "AppTheme.h"
#include "Core/ProductDefinition.h"

namespace livemix
{

namespace
{
    // Every setup screen opens the same way: a 22 px title over one grey line.
    void drawPageTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::String& subtitle)
    {
        g.setColour (Dine::ink);
        g.setFont (Dine::text (22.0f, 600));
        g.drawText (title, area.removeFromTop (28), juce::Justification::centredLeft);
        area.removeFromTop (2);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f));
        g.drawFittedText (subtitle, area.removeFromTop (20), juce::Justification::topLeft, 1);
    }

    constexpr int kTitleBlock = 54;   // title + subtitle

    // The sources and their plain names live in AppTheme (Dine::roleGroups /
    // Dine::friendlyRoleName), so this page and the TRACKS header menu offer the same list.
    using Dine::roleGroups;
    using Dine::friendlyRoleName;

    juce::Colour busColour (MixBus b) noexcept
    {
        switch (b)
        {
            case MixBus::Drums:  return Dine::warn;
            case MixBus::Bass:   return Dine::accent;
            case MixBus::Music:  return juce::Colour (0xff8fa2d8);
            case MixBus::Vocals: return Dine::ok;
            default:             return Dine::ink2;
        }
    }
}

// ============================================================================ DevicePage

class DevicePage::DeviceRow : public juce::Button
{
public:
    DeviceRow (const juce::String& name, int channels, bool firstRow)
        : juce::Button (name), deviceName (name), inputChannels (channels), first (firstRow) {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto b = getLocalBounds();
        if (on)        g.setColour (Dine::accent.withAlpha (0.13f));
        else if (over) g.setColour (juce::Colours::white.withAlpha (0.05f));
        else           g.setColour (juce::Colours::transparentBlack);
        g.fillRect (b);
        if (! first) Dine::drawRule (g, b.withHeight (1), Dine::hairSoft);

        auto r = b.reduced (14, 0);
        Dine::drawIcon (g, Dine::Icon::Device, r.removeFromLeft (18).toFloat().withSizeKeepingCentre (18.0f, 18.0f),
                        on ? Dine::accent : Dine::glyph);
        r.removeFromLeft (11);
        if (on)
        {
            Dine::drawIcon (g, Dine::Icon::Check, r.removeFromRight (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f), Dine::accent);
            r.removeFromRight (10);
        }
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (12.0f));
        const juce::String count = inputChannels > 0 ? juce::String (inputChannels) + (inputChannels == 1 ? " input" : " inputs") : "no inputs";
        g.drawText (count, r.removeFromRight (80), juce::Justification::centredRight);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, on ? 600 : 400));
        g.drawText (deviceName, r, juce::Justification::centredLeft, true);
    }

    juce::String deviceName;
    int inputChannels;
    bool first;
};

DevicePage::DevicePage (MixController& c, AppServices& s) : controller (c), services (s)
{
    viewport.setViewedComponent (&listHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    addAndMakeVisible (outputButton);
    addAndMakeVisible (continueButton);
    addAndMakeVisible (rescanButton);
    addAndMakeVisible (recordingButton);
    continueButton.setCaps (false);
    rescanButton.setIcon (Dine::Icon::Refresh);
    recordingButton.setIcon (Dine::Icon::Waveform);
    outputButton.onClick = [this] { chooseOutput (outputButton); };
    addAndMakeVisible (outputsButton);
    outputsButton.setTooltip ("Send the mix to more than one pair of outputs at once: the PA on 1-2, headphones or a "
                              "cue on 3-4, each with its own level.");
    outputsButton.onClick = [this] { if (onSetUpOutputs) onSetUpOutputs(); };
    rescanButton.onClick = [this] { refresh(); };
    recordingButton.setTooltip ("Turn a folder of recorded stems (AIFF / WAV / FLAC) into tracks, so you can mix, tune and export without a band in the room.");
    recordingButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder of multitrack stems", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            const auto folder = fc.getResult();
            if (folder.isDirectory() && onImportRecording) onImportRecording (folder);
        });
    };
    continueButton.onClick = [this]
    {
        if (selected < 0 || selected >= inputs.size()) return;
        error = services.openDevices (inputs[selected].name, outputName);
        if (error.isNotEmpty()) { repaint(); return; }
        if (onContinue) onContinue();
    };
    refresh();
}

DevicePage::~DevicePage() = default;

void DevicePage::refresh()
{
    inputs = services.inputDevices();
    outputs = services.outputDevices();
    rows.clear();
    listHolder.removeAllChildren();
    const juce::String current = services.currentInputDevice();
    selected = -1;
    for (int i = 0; i < inputs.size(); ++i)
    {
        auto row = std::make_unique<DeviceRow> (inputs[i].name, inputs[i].inputChannels, i == 0);
        row->setClickingTogglesState (false);
        row->onClick = [this, i] { select (i); };
        listHolder.addAndMakeVisible (*row);
        if (inputs[i].name == current) selected = i;
        rows.push_back (std::move (row));
    }
    if (selected < 0 && ! inputs.isEmpty())
    {
        // Prefer the device with the most inputs: that is the console or Dante, not the built-in mic.
        int best = 0;
        for (int i = 1; i < inputs.size(); ++i) if (inputs[i].inputChannels > inputs[best].inputChannels) best = i;
        selected = best;
    }
    outputName = services.currentOutputDevice();
    if (outputName.isEmpty() && selected >= 0)
    {
        // Same device when it has outputs, else the first output device.
        for (const auto& o : outputs) if (o.name == inputs[selected].name) outputName = o.name;
        if (outputName.isEmpty() && ! outputs.isEmpty()) outputName = outputs[0].name;
    }
    outputButton.setValue (outputName.isEmpty() ? "None" : outputName);
    for (int i = 0; i < int (rows.size()); ++i) rows[size_t (i)]->setToggleState (i == selected, juce::dontSendNotification);
    continueButton.setEnabled (selected >= 0);
    resized();
    repaint();
}

void DevicePage::select (int index)
{
    selected = index;
    for (int i = 0; i < int (rows.size()); ++i) rows[size_t (i)]->setToggleState (i == selected, juce::dontSendNotification);
    continueButton.setEnabled (selected >= 0);
    repaint();
}

void DevicePage::chooseOutput (juce::Component& anchor)
{
    juce::PopupMenu m;
    for (int i = 0; i < outputs.size(); ++i) m.addItem (i + 1, outputs[i].name, true, outputs[i].name == outputName);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor).withMinimumWidth (anchor.getWidth()), [this] (int id)
    {
        if (id <= 0 || id > outputs.size()) return;
        outputName = outputs[id - 1].name;
        outputButton.setValue (outputName);
        resized();
    });
}

// The page is one column: the device list, then a grouped box of settings.
juce::Rectangle<int> DevicePage::body() const
{
    auto r = getLocalBounds().withTrimmedBottom (Dine::Metric::footer).reduced (Dine::Metric::padX, 0);
    r.removeFromTop (Dine::Metric::padY);
    return r.withWidth (juce::jmin (r.getWidth(), 720));
}

void DevicePage::paint (juce::Graphics& g)
{
    auto area = body();
    drawPageTitle (g, area.removeFromTop (kTitleBlock), "Audio device",
                   "Choose where the band comes in. DLIVE never changes your console.");
    area.removeFromTop (18);

    const int listH = juce::jmax (46, int (rows.size()) * 46);
    if (! rows.empty())
    {
        auto list = area.removeFromTop (juce::jmin (listH, juce::jmax (92, area.getHeight() - 158)));
        Dine::drawCard (g, list.toFloat());
        area.removeFromTop (20);
    }
    else
    {
        auto empty = area.removeFromTop (188);
        Dine::drawCard (g, empty.toFloat());
        auto r = empty.reduced (30, 34);
        Dine::drawIcon (g, Dine::Icon::Device, r.removeFromTop (34).withSizeKeepingCentre (34, 34).toFloat(), Dine::ink4);
        r.removeFromTop (14);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (15.0f, 600));
        g.drawText ("No inputs yet", r.removeFromTop (20), juce::Justification::centred);
        r.removeFromTop (5);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f));
        g.drawFittedText ("Connect your interface or console and rescan. You can also work from a recording while nothing is plugged in.",
                          r.removeFromTop (40).withSizeKeepingCentre (400, 40), juce::Justification::centredTop, 2);
        area.removeFromTop (20);
    }

    // ---- the grouped settings box
    auto box = area.removeFromTop (juce::jmin (area.getHeight(), 138));
    Dine::drawCard (g, box.toFloat());
    auto inner = box.reduced (16, 14);
    auto row = [&] (int h, const juce::String& label) -> juce::Rectangle<int>
    {
        auto r = inner.removeFromTop (h);
        auto l = r.removeFromLeft (110);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f));
        g.drawText (label, l, juce::Justification::centredRight);
        r.removeFromLeft (14);
        inner.removeFromTop (12);
        return r;
    };
    row (Dine::Metric::control, "Output pair");
    auto clock = row (18, "Clock");
    g.setColour (Dine::ink);
    g.setFont (Dine::mono (12.5f));
    g.drawText (services.isAudioRunning()
                    ? juce::String (services.sampleRate() / 1000.0, 1) + " kHz  " + Glyph::dot() + "  "
                          + juce::String (services.bufferSize()) + " samples  " + Glyph::dot() + "  "
                          + juce::String (services.numInputChannels()) + " in"
                    : juce::String ("48.0 kHz  ") + Glyph::dot() + "  set when the device opens",
                clock, juce::Justification::centredLeft);
    auto practice = row (Dine::Metric::control, "Recording");
    practice.removeFromLeft (recordingButton.getWidth() + 12);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.5f));
    g.drawText ("Loads a folder of stems as tracks you can play, mix and export.", practice, juce::Justification::centredLeft, true);

    if (error.isNotEmpty())
    {
        g.setColour (Dine::crit);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText (error, getLocalBounds().withTrimmedBottom (Dine::Metric::footer).removeFromBottom (34)
                                     .reduced (Dine::Metric::padX, 0), juce::Justification::centredLeft, 2);
    }

    // ---- footer
    auto footer = getLocalBounds().removeFromBottom (Dine::Metric::footer);
    g.setColour (Dine::window.brighter (0.02f));
    g.fillRect (footer);
    Dine::drawRule (g, footer.withHeight (1), Dine::hair);
    if (selected >= 0 && selected < inputs.size() && ! rows.empty())
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        auto note = footer.reduced (Dine::Metric::padX, 0);
        note.removeFromRight (continueButton.getWidth() + 12);
        g.drawText (juce::String (inputs[selected].inputChannels) + " inputs on " + inputs[selected].name,
                    note, juce::Justification::centredRight, true);
    }
}

void DevicePage::resized()
{
    auto area = body();
    area.removeFromTop (kTitleBlock + 18);

    const int listH = juce::jmax (46, int (rows.size()) * 46);
    if (! rows.empty())
    {
        auto list = area.removeFromTop (juce::jmin (listH, juce::jmax (92, area.getHeight() - 158)));
        viewport.setBounds (list);
        viewport.setVisible (true);
        listHolder.setSize (viewport.getWidth() - (listH > viewport.getHeight() ? 10 : 0), listH);
        int y = 0;
        for (auto& r : rows) { r->setBounds (0, y, listHolder.getWidth(), 46); y += 46; }
        area.removeFromTop (20);
    }
    else
    {
        viewport.setVisible (false);
        auto empty = area.removeFromTop (188);
        auto buttons = empty.removeFromBottom (52).withSizeKeepingCentre (
            juce::jmax (110, rescanButton.idealWidth()) + 12 + juce::jmax (170, recordingButton.idealWidth()), 28);
        rescanButton.setBounds (buttons.removeFromLeft (juce::jmax (110, rescanButton.idealWidth())).withHeight (28));
        buttons.removeFromLeft (12);
        recordingButton.setBounds (buttons.withHeight (28));
        area.removeFromTop (20);
    }

    auto box = area.removeFromTop (juce::jmin (area.getHeight(), 138));
    auto inner = box.reduced (16, 14);
    auto row = [&] (int h) -> juce::Rectangle<int>
    {
        auto r = inner.removeFromTop (h);
        r.removeFromLeft (110 + 14);
        inner.removeFromTop (12);
        return r;
    };
    {
        auto outRow = row (Dine::Metric::control);
        outputButton.setBounds (outRow.removeFromLeft (330));
        outRow.removeFromLeft (10);
        outputsButton.setBounds (outRow.removeFromLeft (juce::jmax (150, outputsButton.idealWidth())));
    }
    row (18);
    auto practice = row (Dine::Metric::control);
    if (! rows.empty())
        recordingButton.setBounds (practice.removeFromLeft (juce::jmax (170, recordingButton.idealWidth())));

    auto footer = getLocalBounds().removeFromBottom (Dine::Metric::footer).reduced (Dine::Metric::padX, 0);
    continueButton.setBounds (footer.removeFromRight (juce::jmax (110, continueButton.idealWidth()))
                                  .withSizeKeepingCentre (juce::jmax (110, continueButton.idealWidth()), Dine::Metric::button));
    if (! rows.empty())
        rescanButton.setBounds (footer.removeFromLeft (juce::jmax (100, rescanButton.idealWidth()))
                                    .withSizeKeepingCentre (juce::jmax (100, rescanButton.idealWidth()), Dine::Metric::button));
}

// ============================================================================ AssignPage

class AssignPage::Row : public juce::Component
{
public:
    Row (AssignPage& owner, int index) : page (owner), input (index)
    {
        addAndMakeVisible (name);
        name.setFont (Dine::text (13.0f));
        name.setIndents (7, 0);
        name.setBorder (juce::BorderSize<int> (0));
        name.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        name.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        name.setColour (juce::TextEditor::focusedOutlineColourId, Dine::accent);
        name.setColour (juce::TextEditor::textColourId, Dine::ink);
        name.setColour (juce::TextEditor::highlightedTextColourId, Dine::ink);
        name.setTextToShowWhenEmpty ("Name", Dine::ink4);
        name.setSelectAllWhenFocused (true);
        name.onTextChange = [this] { page.entries[size_t (input)].name = name.getText(); };
        name.onReturnKey = [this] { name.giveAwayKeyboardFocus(); page.commit(); };
        name.onFocusLost = [this] { page.commit(); };

        addAndMakeVisible (source);
        source.onClick = [this] { page.showSourceMenu (input, source); };

        addAndMakeVisible (link);
        link.setClickingTogglesState (false);
        link.onClick = [this]
        {
            auto& e = page.entries[size_t (input)];
            if (input + 1 >= int (page.entries.size())) return;
            e.linkedToNext = ! e.linkedToNext;
            page.entries[size_t (input) + 1].linkedFromPrevious = e.linkedToNext;
            page.commit();
            page.rebuild();
        };
    }

    void refresh()
    {
        const auto& e = page.entries[size_t (input)];
        name.setText (e.name, false);
        source.setValue (e.assigned ? friendlyRoleName (e.role) : "Not used");
        source.setVisible (! e.linkedFromPrevious);
        name.setVisible (! e.linkedFromPrevious);
        link.setToggleState (e.linkedToNext, juce::dontSendNotification);
        link.setVisible (! e.linkedFromPrevious && input + 1 < int (page.entries.size()));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& e = page.entries[size_t (input)];
        auto b = getLocalBounds();
        if (e.assigned && ! e.linkedFromPrevious) g.setColour (juce::Colours::white.withAlpha (0.025f));
        else g.setColour (juce::Colours::transparentBlack);
        g.fillRect (b);
        Dine::drawRule (g, b.removeFromBottom (1), Dine::hairSoft);

        auto r = getLocalBounds().reduced (14, 0);
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.5f));
        g.drawText (juce::String (input + 1), r.removeFromLeft (kNumW), juce::Justification::centredLeft);
        r.removeFromLeft (kGap);

        if (e.linkedFromPrevious)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (12.5f));
            g.drawText ("Right of input " + juce::String (input) + "  " + Glyph::dot() + "  stereo pair", r, juce::Justification::centredLeft);
            return;
        }

        Dine::drawIcon (g, e.assigned ? Dine::iconFor (e.icon, e.role) : Dine::Icon::Dash,
                        r.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                        e.assigned ? Dine::glyph : Dine::ink4);

        if (e.assigned)
        {
            auto tag = getLocalBounds().reduced (14, 0).removeFromRight (kBusW);
            const auto bus = mixBusForRole (e.role);
            const juce::String busName (mixBusName (bus));
            const juce::String label = busName.substring (0, 1) + busName.substring (1).toLowerCase();
            const int pw = Dine::textWidth (Dine::text (11.0f, 600), label) + 14;
            auto pill = tag.removeFromLeft (pw).withSizeKeepingCentre (pw, 18);
            Dine::fillRounded (g, pill.toFloat(), busColour (bus).withAlpha (0.16f), 4.0f);
            g.setColour (busColour (bus));
            g.setFont (Dine::text (11.0f, 600));
            g.drawText (label, pill, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 0);
        r.removeFromLeft (kNumW + kGap);
        r.removeFromRight (kBusW);
        auto pair = r.removeFromRight (kPairW);
        link.setBounds (pair.withSizeKeepingCentre (kPairW - kGap, 20));
        r.removeFromRight (kGap);
        source.setBounds (r.removeFromRight (kSourceW).withSizeKeepingCentre (kSourceW, Dine::Metric::control));
        r.removeFromRight (kGap);
        r.removeFromLeft (16 + 8);   // source icon
        name.setBounds (r.withSizeKeepingCentre (r.getWidth(), Dine::Metric::control));
        const bool linked = page.entries[size_t (input)].linkedFromPrevious;
        name.setVisible (! linked);
        source.setVisible (! linked);
    }

    static constexpr int kNumW = 26, kGap = 10, kSourceW = 200, kPairW = 96, kBusW = 84;

private:
    AssignPage& page;
    int input;
    juce::TextEditor name;
    DinePopup source;
    DineSwitch link { "Stereo", "Link" };
};

AssignPage::AssignPage (MixController& c, AppServices& s) : controller (c), services (s)
{
    viewport.setViewedComponent (&listHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    addAndMakeVisible (continueButton);
    addAndMakeVisible (backButton);
    addAndMakeVisible (clearButton);
    continueButton.onClick = [this] { commit(); if (assignedCount() > 0 && onContinue) onContinue(); };
    backButton.onClick = [this] { if (onBack) onBack(); };
    clearButton.onClick = [this] { clearAll(); };
}

AssignPage::~AssignPage() = default;

void AssignPage::refresh()
{
    // Every channel the device brings in, and never fewer than the session already uses: an
    // imported multitrack, or a session opened without its console, still shows all its inputs.
    numInputs = juce::jlimit (0, kMaxInputs, services.numInputChannels());
    for (const auto& in : controller.getSession().inputs)
        numInputs = juce::jlimit (0, kMaxInputs, juce::jmax (numInputs, in.inputA + 1, in.inputB + 1));
    entries.assign (size_t (numInputs), Entry {});
    for (const auto& in : controller.getSession().inputs)
    {
        if (in.inputA < 0 || in.inputA >= numInputs) continue;
        auto& e = entries[size_t (in.inputA)];
        e.name = in.name;
        e.icon = in.icon;
        e.assigned = in.enabled;
        e.role = in.role;
        if (in.inputB >= 0 && in.inputB < numInputs) { e.linkedToNext = in.inputB == in.inputA + 1; entries[size_t (in.inputB)].linkedFromPrevious = e.linkedToNext; }
    }
    rebuild();
}

void AssignPage::rebuild()
{
    rows.clear();
    listHolder.removeAllChildren();
    for (int i = 0; i < numInputs; ++i)
    {
        auto row = std::make_unique<Row> (*this, i);
        listHolder.addAndMakeVisible (*row);
        row->refresh();
        rows.push_back (std::move (row));
    }
    continueButton.setEnabled (assignedCount() > 0);
    resized();
    repaint();
}

void AssignPage::commit()
{
    MixSession s = controller.getSession();
    s.inputs.clear();
    for (int i = 0; i < numInputs; ++i)
    {
        const auto& e = entries[size_t (i)];
        if (! e.assigned || e.linkedFromPrevious) continue;
        InputAssignment a;
        a.name = e.name.isEmpty() ? juce::String (channelRoleName (e.role)).toStdString() : e.name.toStdString();
        a.icon = e.icon;
        a.role = e.role;
        a.inputA = i;
        a.inputB = e.linkedToNext && i + 1 < numInputs ? i + 1 : -1;
        a.enabled = true;
        s.inputs.push_back (a);
    }
    controller.setSession (s);
    continueButton.setEnabled (assignedCount() > 0);
    repaint();
}

int AssignPage::assignedCount() const
{
    int n = 0;
    for (const auto& e : entries) if (e.assigned && ! e.linkedFromPrevious) ++n;
    return n;
}

void AssignPage::assign (int input, ChannelRole role, const juce::String& name, bool linkWithNext)
{
    if (input < 0 || input >= numInputs) return;
    auto& e = entries[size_t (input)];
    e.assigned = true;
    e.role = role;
    e.name = name;
    e.linkedToNext = linkWithNext && input + 1 < numInputs;
    if (e.linkedToNext) entries[size_t (input) + 1].linkedFromPrevious = true;
    commit();
    rebuild();
}

void AssignPage::clearAll()
{
    for (auto& e : entries) e = Entry {};
    commit();
    rebuild();
}

void AssignPage::showSourceMenu (int input, juce::Component& anchor)
{
    juce::PopupMenu m;
    m.addItem (1, "Not used", true, ! entries[size_t (input)].assigned);
    m.addSeparator();

    int id = 100;
    std::vector<ChannelRole> byId;
    // Hierarchical menus read as native macOS — not one endless flat list.
    for (const auto& group : roleGroups())
    {
        juce::PopupMenu sub;
        for (auto r : group.roles)
        {
            const bool on = entries[size_t (input)].assigned && entries[size_t (input)].role == r;
            sub.addItem (id++, friendlyRoleName (r), true, on);
            byId.push_back (r);
        }
        m.addSubMenu (group.name, sub, true);
    }

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetComponent (&anchor)
                         .withMinimumWidth (220),
                     [this, input, byId] (int chosen)
                     {
                         if (chosen <= 0 || input >= int (entries.size())) return;
                         auto& e = entries[size_t (input)];
                         if (chosen == 1) { e.assigned = false; }
                         else
                         {
                             e.assigned = true;
                             e.role = byId[size_t (chosen - 100)];
                             if (e.name.isEmpty()) e.name = channelRoleName (e.role);
                             if ((e.role == ChannelRole::Overhead || e.role == ChannelRole::DrumBus || e.role == ChannelRole::Piano
                                  || e.role == ChannelRole::ElectricPiano || e.role == ChannelRole::SynthPad)
                                 && input + 1 < numInputs && ! entries[size_t (input) + 1].assigned && ! e.linkedToNext)
                             {
                                 e.linkedToNext = true;
                                 entries[size_t (input) + 1].linkedFromPrevious = true;
                             }
                         }
                         commit();
                         rebuild();
                     });
}

juce::Rectangle<int> AssignPage::body() const
{
    auto r = getLocalBounds().withTrimmedBottom (Dine::Metric::footer).reduced (Dine::Metric::padX, 0);
    r.removeFromTop (Dine::Metric::padY);
    return r;
}

void AssignPage::paint (juce::Graphics& g)
{
    auto area = body();
    auto head = area.removeFromTop (kTitleBlock);
    drawPageTitle (g, head, "Inputs", "Name each input and say what it is. DLIVE picks the bus; you can change it in Advanced.");
    g.setColour (assignedCount() > 0 ? Dine::accent : Dine::ink3);
    g.setFont (Dine::mono (12.0f));
    g.drawText (juce::String (assignedCount()) + " of " + juce::String (numInputs) + " assigned",
                head.withHeight (28), juce::Justification::centredRight);
    area.removeFromTop (16);

    // The table: a card with a header rule and 32 px rows inside.
    Dine::drawCard (g, area.toFloat());
    auto header = area.removeFromTop (28);
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRect (header.reduced (1, 0).withTrimmedTop (1));
    Dine::drawRule (g, header.removeFromBottom (1), Dine::hair);

    auto r = header.reduced (14, 0);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.0f, 600));
    g.drawText ("#", r.removeFromLeft (Row::kNumW), juce::Justification::centredLeft);
    r.removeFromLeft (Row::kGap);
    g.drawText ("Bus", r.removeFromRight (Row::kBusW), juce::Justification::centredLeft);
    g.drawText ("Pair", r.removeFromRight (Row::kPairW), juce::Justification::centredLeft);
    r.removeFromRight (Row::kGap);
    g.drawText ("Source", r.removeFromRight (Row::kSourceW), juce::Justification::centredLeft);
    r.removeFromRight (Row::kGap);
    g.drawText ("Name", r, juce::Justification::centredLeft);

    auto footer = getLocalBounds().removeFromBottom (Dine::Metric::footer);
    g.setColour (Dine::window.brighter (0.02f));
    g.fillRect (footer);
    Dine::drawRule (g, footer.withHeight (1), Dine::hair);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.0f));
    auto note = footer.reduced (Dine::Metric::padX, 0);
    note.removeFromRight (continueButton.getWidth() + 12);
    g.drawText (assignedCount() > 0 ? "Unassigned inputs stay out of the mix." : "Assign at least one input to continue.",
                note, juce::Justification::centredRight, true);
}

void AssignPage::resized()
{
    auto area = body();
    area.removeFromTop (kTitleBlock + 16);
    auto list = area.withTrimmedTop (28).reduced (1);
    viewport.setBounds (list);
    const int total = int (rows.size()) * 32;
    listHolder.setSize (list.getWidth() - (total > list.getHeight() ? 10 : 0), juce::jmax (total, list.getHeight()));
    int y = 0;
    for (auto& r : rows) { r->setBounds (0, y, listHolder.getWidth(), 32); y += 32; }

    auto footer = getLocalBounds().removeFromBottom (Dine::Metric::footer).reduced (Dine::Metric::padX, 0);
    const int cw = juce::jmax (110, continueButton.idealWidth());
    continueButton.setBounds (footer.removeFromRight (cw).withSizeKeepingCentre (cw, Dine::Metric::button));
    const int bw = juce::jmax (72, backButton.idealWidth());
    backButton.setBounds (footer.removeFromLeft (bw).withSizeKeepingCentre (bw, Dine::Metric::button));
    footer.removeFromLeft (8);
    const int clw = juce::jmax (64, clearButton.idealWidth());
    clearButton.setBounds (footer.removeFromLeft (clw).withSizeKeepingCentre (clw, Dine::Metric::button));
}

// ============================================================================ PurposePage

class PurposePage::Tile : public juce::Button
{
public:
    Tile (const juce::String& title, const juce::String& line, Dine::Icon i, bool big)
        : juce::Button (title), heading (title), detail (line), icon (i), large (big) {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto b = getLocalBounds().toFloat();
        Dine::fillRounded (g, b, on ? Dine::accent.withAlpha (0.10f) : over ? juce::Colour (0xff2a2c30) : Dine::card, Dine::Radius::card);
        Dine::hairlineRounded (g, b, on ? Dine::accent.withAlpha (0.75f) : Dine::hair, Dine::Radius::card);

        auto r = getLocalBounds().reduced (14, large ? 16 : 14);
        auto top = r.removeFromTop (large ? 20 : 18);
        Dine::drawIcon (g, icon, top.removeFromLeft (large ? 18 : 17).toFloat().withSizeKeepingCentre (large ? 18.0f : 17.0f, large ? 18.0f : 17.0f),
                        on ? Dine::accent : Dine::glyph);
        top.removeFromLeft (8);
        if (on)
        {
            Dine::drawIcon (g, Dine::Icon::Check, top.removeFromRight (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f), Dine::accent);
            top.removeFromRight (6);
        }
        g.setColour (Dine::ink);
        g.setFont (Dine::text (large ? 15.0f : 13.5f, 600));
        g.drawText (heading, top, juce::Justification::centredLeft, true);
        r.removeFromTop (8);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (large ? 12.5f : 12.0f));
        g.drawFittedText (detail, r, juce::Justification::topLeft, 5);
    }

    juce::String heading, detail;
    Dine::Icon icon;
    bool large;
};

PurposePage::PurposePage (MixController& c) : controller (c)
{
    const char* purposeLines[] = {
        "Loudness for TV and platform broadcast, true peaks held under -2 dB.",
        "YouTube, Facebook and church streaming platforms; a little louder, peaks under -1 dB.",
        "A clean mix to keep; loudness left natural for editing later.",
        "Rehearsal or a listening feed; the mix, not the delivery, is the point."
    };
    const Dine::Icon purposeIcons[] = { Dine::Icon::Device, Dine::Icon::Waveform, Dine::Icon::List, Dine::Icon::Room };
    for (int i = 0; i < int (MixPurpose::Count); ++i)
    {
        auto t = std::make_unique<Tile> (mixPurposeName (MixPurpose (i)), purposeLines[i], purposeIcons[i], false);
        t->setClickingTogglesState (false);
        t->onClick = [this, i] { controller.setPurpose (MixPurpose (i)); refresh(); };
        addAndMakeVisible (*t);
        purposeTiles.push_back (std::move (t));
    }
    const char* soundLines[] = {
        "Deep controlled low end, forward defined attack, dense but breathing dynamics, smooth top, room for keys, bass and a choir.",
        "A documented variation of Modern Gospel: guitars and pads closer to the voices, drums a little tighter, more hall on the backing vocals."
    };
    for (int i = 0; i < int (StyleProfileId::Count); ++i)
    {
        auto t = std::make_unique<Tile> (styleProfileName (StyleProfileId (i)), soundLines[i], Dine::Icon::Waveform, true);
        t->setClickingTogglesState (false);
        t->onClick = [this, i] { controller.setProfile (StyleProfileId (i)); refresh(); };
        addAndMakeVisible (*t);
        soundTiles.push_back (std::move (t));
    }
    addAndMakeVisible (continueButton);
    addAndMakeVisible (backButton);
    continueButton.onClick = [this] { if (onContinue) onContinue(); };
    backButton.onClick = [this] { if (onBack) onBack(); };
    refresh();
}

PurposePage::~PurposePage() = default;

void PurposePage::refresh()
{
    for (int i = 0; i < int (purposeTiles.size()); ++i) purposeTiles[size_t (i)]->setToggleState (int (controller.getSession().purpose) == i, juce::dontSendNotification);
    for (int i = 0; i < int (soundTiles.size()); ++i) soundTiles[size_t (i)]->setToggleState (int (controller.getSession().profile) == i, juce::dontSendNotification);
    repaint();
}

juce::Rectangle<int> PurposePage::body() const
{
    auto r = getLocalBounds().withTrimmedBottom (Dine::Metric::footer).reduced (Dine::Metric::padX, 0);
    r.removeFromTop (Dine::Metric::padY);
    return r;
}

void PurposePage::paint (juce::Graphics& g)
{
    auto area = body();
    drawPageTitle (g, area.removeFromTop (kTitleBlock), "Purpose and sound",
                   "Purpose sets the loudness and the peaks. Sound sets the character.");
    area.removeFromTop (18 + 124 + 22);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.0f, 600));
    g.drawText ("SOUND", area.removeFromTop (16), juce::Justification::centredLeft);

    auto footer = getLocalBounds().removeFromBottom (Dine::Metric::footer);
    g.setColour (Dine::window.brighter (0.02f));
    g.fillRect (footer);
    Dine::drawRule (g, footer.withHeight (1), Dine::hair);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.0f));
    auto note = footer.reduced (Dine::Metric::padX, 0);
    note.removeFromRight (continueButton.getWidth() + 12);
    g.drawText ("Both can be changed later without losing the mix.", note, juce::Justification::centredRight, true);
}

void PurposePage::resized()
{
    auto area = body();
    area.removeFromTop (kTitleBlock + 18);
    auto row = area.removeFromTop (124);
    const int gap = 12;
    const int w = (row.getWidth() - gap * (int (purposeTiles.size()) - 1)) / int (purposeTiles.size());
    for (auto& t : purposeTiles) { t->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
    area.removeFromTop (22 + 16 + 10);
    auto row2 = area.removeFromTop (juce::jmin (128, area.getHeight()));
    if (row2.getWidth() > 820) row2 = row2.withWidth (820);
    const int w2 = (row2.getWidth() - gap) / 2;
    for (auto& t : soundTiles) { t->setBounds (row2.removeFromLeft (w2)); row2.removeFromLeft (gap); }

    auto footer = getLocalBounds().removeFromBottom (Dine::Metric::footer).reduced (Dine::Metric::padX, 0);
    const int cw = juce::jmax (130, continueButton.idealWidth());
    continueButton.setBounds (footer.removeFromRight (cw).withSizeKeepingCentre (cw, Dine::Metric::button));
    const int bw = juce::jmax (72, backButton.idealWidth());
    backButton.setBounds (footer.removeFromLeft (bw).withSizeKeepingCentre (bw, Dine::Metric::button));
}

} // namespace livemix
