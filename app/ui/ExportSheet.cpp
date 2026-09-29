#include "ExportSheet.h"
#include "UI/Widgets.h"

namespace livemix
{

namespace
{
    constexpr int kCardW = 620, kPadX = 30, kPadY = 26;
    constexpr int kRowGap = 20, kCapH = 16, kCapGap = 6;

    juce::String clock (double seconds)
    {
        if (seconds < 0.0) seconds = 0.0;
        const int total = int (seconds);
        return juce::String (total / 60).paddedLeft ('0', 2) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }
}

ExportSheet::ExportSheet (MixController& c, AppServices& s) : controller (c), services (s)
{
    const char* ranges[3] = { "Whole session", "Loop", "Between markers" };
    for (int i = 0; i < 3; ++i)
    {
        rangeTabs[size_t (i)] = std::make_unique<DineButton> (ranges[i], DineButton::Style::Segment);
        rangeTabs[size_t (i)]->setFontPx (12.0f);
        rangeTabs[size_t (i)]->onClick = [this, i] { range = Range (i); updateControls(); repaint(); };
        rangeTrack.addAndMakeVisible (*rangeTabs[size_t (i)]);
    }
    const char* formats[2] = { "WAV", "MP3 320" };
    for (int i = 0; i < 2; ++i)
    {
        formatTabs[size_t (i)] = std::make_unique<DineButton> (formats[i], DineButton::Style::Segment);
        formatTabs[size_t (i)]->setFontPx (12.0f);
        formatTabs[size_t (i)]->onClick = [this, i] { format = i; updateControls(); repaint(); };
        formatTrack.addAndMakeVisible (*formatTabs[size_t (i)]);
    }
    addAndMakeVisible (rangeTrack);
    addAndMakeVisible (formatTrack);

    folder = services.sessionFolder() != juce::File() ? services.sessionFolder().getChildFile ("Exports")
                                                      : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    folderButton.setFontPx (12.0f);
    folderButton.setTooltip ("Where the file goes. The session's own Exports folder is made for you.");
    folderButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Where should the export go?", folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto picked = fc.getResult();
                                  if (picked == juce::File()) return;
                                  folder = picked;
                                  repaint();
                              });
    };
    addAndMakeVisible (folderButton);

    cancelButton.setFontPx (12.5f);
    cancelButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (cancelButton);

    exportButton.setFontPx (12.5f);
    exportButton.onClick = [this]
    {
        if (onExport) onExport (destination(), format == 1 ? AppServices::ExportFormat::Mp3 : AppServices::ExportFormat::Wav,
                                fromSample(), toSample());
        if (onClose) onClose();
    };
    addAndMakeVisible (exportButton);

    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);
    updateControls();
}

ExportSheet::~ExportSheet() = default;

void ExportSheet::refresh() { updateControls(); }

bool ExportSheet::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (key == juce::KeyPress::returnKey && exportButton.isEnabled()) { exportButton.onClick(); return true; }
    return false;
}

juce::Rectangle<int> ExportSheet::cardBounds() const
{
    const int w = juce::jmin (kCardW, getWidth() - 60);
    const int h = kPadY + 28 + 2 + 18 + 22
                + (kCapH + kCapGap + Dine::Metric::control + kRowGap) * 2
                + kCapH + kCapGap + 20 + kRowGap
                + 18 + Dine::Metric::button + kPadY;
    return juce::Rectangle<int> (w, juce::jmin (h, getHeight() - 40)).withCentre (getLocalBounds().getCentre());
}

juce::int64 ExportSheet::fromSample() const
{
    const auto& project = services.daw().getProject();
    if (range == Range::Loop) return project.loopStart;
    if (range == Range::BetweenMarkers && project.markers.size() >= 2)
    {
        auto sorted = project.markers;
        std::sort (sorted.begin(), sorted.end(), [] (const Marker& a, const Marker& b) { return a.position < b.position; });
        return sorted.front().position;
    }
    return 0;
}

juce::int64 ExportSheet::toSample() const
{
    const auto& project = services.daw().getProject();
    if (range == Range::Loop) return project.loopEnd;
    if (range == Range::BetweenMarkers && project.markers.size() >= 2)
    {
        auto sorted = project.markers;
        std::sort (sorted.begin(), sorted.end(), [] (const Marker& a, const Marker& b) { return a.position < b.position; });
        return sorted.back().position;
    }
    return 0;      // to the end of the recording
}

juce::String ExportSheet::rangeNote() const
{
    const auto& project = services.daw().getProject();
    if (range == Range::Loop) return project.loopEnd > project.loopStart ? juce::String ("The marked loop")
                                                                         : juce::String ("No loop is set yet");
    if (range == Range::BetweenMarkers)
    {
        if (project.markers.size() < 2) return "Fewer than two markers";
        auto sorted = project.markers;
        std::sort (sorted.begin(), sorted.end(), [] (const Marker& a, const Marker& b) { return a.position < b.position; });
        return juce::String (sorted.front().name) + " " + Glyph::arrow() + " " + juce::String (sorted.back().name);
    }
    return "Everything that was recorded";
}

juce::File ExportSheet::destination() const
{
    juce::String name = services.currentSessionName().isNotEmpty() ? services.currentSessionName() : "DLIVE mix";
    if (range == Range::BetweenMarkers)
    {
        const auto& project = services.daw().getProject();
        if (project.markers.size() >= 2)
        {
            auto sorted = project.markers;
            std::sort (sorted.begin(), sorted.end(), [] (const Marker& a, const Marker& b) { return a.position < b.position; });
            name = juce::String (sorted.front().name);
        }
    }
    return folder.getChildFile (juce::File::createLegalFileName (name)).withFileExtension (format == 1 ? "mp3" : "wav");
}

void ExportSheet::updateControls()
{
    for (int i = 0; i < 3; ++i) rangeTabs[size_t (i)]->setToggleState (int (range) == i, juce::dontSendNotification);
    for (int i = 0; i < 2; ++i) formatTabs[size_t (i)]->setToggleState (format == i, juce::dontSendNotification);

    const auto& project = services.daw().getProject();
    rangeTabs[1]->setEnabled (project.loopEnd > project.loopStart);
    rangeTabs[2]->setEnabled (project.markers.size() >= 2);
    if ((range == Range::Loop && ! rangeTabs[1]->isEnabled())
        || (range == Range::BetweenMarkers && ! rangeTabs[2]->isEnabled()))
        range = Range::Whole;

    const auto length = toSample() > fromSample() ? toSample() - fromSample()
                                                  : juce::jmax ((juce::int64) 0, project.lengthSamples() - fromSample());
    exportButton.setEnabled (length > 0);
    resized();
}

void ExportSheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.86f));
    auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), Dine::Radius::card);

    auto r = card.reduced (kPadX, kPadY);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (22.0f, 600));
    Dine::drawText (g, "Export", r.removeFromTop (28), juce::Justification::centredLeft, true);
    r.removeFromTop (2);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (13.0f));
    Dine::drawText (g, "Bounced offline from the kept mix. What the room and the stream hear is not touched.",
                    r.removeFromTop (18), juce::Justification::centredLeft, true);
    r.removeFromTop (22);

    auto caption = [&g, &r] (const juce::String& text)
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        Dine::drawText (g, text, r.removeFromTop (kCapH), juce::Justification::centredLeft, true);
        r.removeFromTop (kCapGap);
    };

    caption ("Range");
    {
        auto row = r.removeFromTop (Dine::Metric::control);
        row.removeFromLeft (rangeTrack.getWidth() + 16);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.0f, 500));
        Dine::drawText (g, rangeNote(), row, juce::Justification::centredLeft, true);
        r.removeFromTop (kRowGap);
    }

    caption ("Format");
    r.removeFromTop (Dine::Metric::control + kRowGap);

    caption ("Save to");
    {
        auto row = r.removeFromTop (20);
        row.removeFromRight (folderButton.getWidth() + 14);
        g.setColour (Dine::ink2);
        g.setFont (Dine::mono (11.0f, 500));
        Dine::drawText (g, destination().getFullPathName(), row, juce::Justification::centredLeft, true);
        r.removeFromTop (kRowGap);
    }

    // How long the render will take, because "about twenty seconds" is the difference
    // between waiting and thinking it has hung.
    {
        const auto& project = services.daw().getProject();
        const double rate = juce::jmax (1.0, project.sampleRate);
        const auto length = toSample() > fromSample() ? toSample() - fromSample()
                                                      : juce::jmax ((juce::int64) 0, project.lengthSamples() - fromSample());
        const double seconds = double (length) / rate;
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawText (g, length <= 0 ? juce::String ("There is nothing recorded in that range.")
                                       : clock (seconds) + " of audio  " + Glyph::dot() + "  about "
                                             + juce::String (juce::jmax (1, int (std::ceil (seconds / 12.0))))
                                             + (juce::jmax (1, int (std::ceil (seconds / 12.0))) == 1 ? " second" : " seconds"),
                        r.removeFromTop (18), juce::Justification::centredLeft, true);
    }
}

void ExportSheet::resized()
{
    auto card = cardBounds();
    auto r = card.reduced (kPadX, kPadY);
    r.removeFromTop (28 + 2 + 18 + 22);

    r.removeFromTop (kCapH + kCapGap);
    {
        auto row = r.removeFromTop (Dine::Metric::control);
        int total = 0, widths[3] {};
        for (int i = 0; i < 3; ++i) { widths[i] = juce::jmax (104, rangeTabs[size_t (i)]->idealWidth()); total += widths[i]; }
        rangeTrack.setBounds (row.removeFromLeft (total + 4).expanded (0, 2));
        auto track = rangeTrack.getLocalBounds().reduced (2, 2);
        for (int i = 0; i < 3; ++i) rangeTabs[size_t (i)]->setBounds (track.removeFromLeft (widths[i]));
        r.removeFromTop (kRowGap);
    }

    r.removeFromTop (kCapH + kCapGap);
    {
        auto row = r.removeFromTop (Dine::Metric::control);
        int total = 0, widths[2] {};
        for (int i = 0; i < 2; ++i) { widths[i] = juce::jmax (80, formatTabs[size_t (i)]->idealWidth()); total += widths[i]; }
        formatTrack.setBounds (row.removeFromLeft (total + 4).expanded (0, 2));
        auto track = formatTrack.getLocalBounds().reduced (2, 2);
        for (int i = 0; i < 2; ++i) formatTabs[size_t (i)]->setBounds (track.removeFromLeft (widths[i]));
        r.removeFromTop (kRowGap);
    }

    r.removeFromTop (kCapH + kCapGap);
    {
        auto row = r.removeFromTop (20);
        const int w = juce::jmax (84, folderButton.idealWidth());
        folderButton.setBounds (row.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::control));
    }

    auto foot = card.reduced (kPadX, kPadY).removeFromBottom (Dine::Metric::button);
    const int ew = juce::jmax (84, exportButton.idealWidth());
    exportButton.setBounds (foot.removeFromRight (ew));
    foot.removeFromRight (10);
    const int cw = juce::jmax (80, cancelButton.idealWidth());
    cancelButton.setBounds (foot.removeFromRight (cw));
}

} // namespace livemix
