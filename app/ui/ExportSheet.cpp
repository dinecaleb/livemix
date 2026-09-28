#include "ExportSheet.h"

namespace livemix
{

namespace
{
    constexpr int kRowH = 64;

    juce::String clock (juce::int64 samples, double sr)
    {
        const int total = int (double (samples) / juce::jmax (1.0, sr));
        return juce::String (total / 60).paddedLeft ('0', 2) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }
}

ExportSheet::ExportSheet (MixController& c, AppServices& s) : controller (c), services (s)
{
    const char* names[2] = { "WAV", "MP3" };
    const char* tips[2] = { "The whole quality, and a big file: what an editor wants.",
                            "Smaller, and good enough to send: what a phone wants." };
    for (int i = 0; i < 2; ++i)
    {
        formatTabs[size_t (i)] = std::make_unique<DineButton> (names[i], DineButton::Style::Segment);
        formatTabs[size_t (i)]->setTooltip (tips[i]);
        formatTabs[size_t (i)]->onClick = [this, i] { format = i; refresh(); repaint(); };
        addAndMakeVisible (*formatTabs[size_t (i)]);
    }
    formatTabs[0]->setToggleState (true, juce::dontSendNotification);

    addAndMakeVisible (rangePicker);
    rangePicker.setTooltip ("Which part of the recording to write out.");
    rangePicker.onClick = [this]
    {
        juce::PopupMenu m;
        for (size_t i = 0; i < ranges.size(); ++i)
            m.addItem (int (i) + 1, ranges[i].name, true, int (i) == range);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rangePicker),
                         [this] (int r) { if (r > 0) { range = r - 1; refresh(); repaint(); } });
    };

    addAndMakeVisible (exportButton);
    exportButton.setTooltip ("Render the mix and write the file. It runs in the background; you can carry on mixing.");
    exportButton.onClick = [this]
    {
        if (ranges.empty() || onExport == nullptr) return;
        const auto& r = ranges[size_t (juce::jlimit (0, int (ranges.size()) - 1, range))];
        onExport (format == 1 ? AppServices::ExportFormat::Mp3 : AppServices::ExportFormat::Wav, r.from, r.to);
    };

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onClose) onClose(); };

    rebuildRanges();
    refresh();
}

ExportSheet::~ExportSheet() = default;

// EVERY PART OF THE SERVICE THAT CAN BE ASKED FOR: the whole recording, then the stretch after
// each marker. The markers are the ones on the timeline, so "the sermon" is whatever somebody
// called it while the service was running.
void ExportSheet::rebuildRanges()
{
    ranges.clear();
    const auto& project = services.daw().getProject();
    const auto end = project.lengthSamples();

    ranges.push_back ({ "The whole recording", 0, 0 });

    auto markers = project.markers;
    std::sort (markers.begin(), markers.end(), [] (const Marker& a, const Marker& b) { return a.position < b.position; });
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto from = markers[i].position;
        const auto to = i + 1 < markers.size() ? markers[i + 1].position : end;
        if (to <= from) continue;
        ranges.push_back ({ "From \"" + markers[i].name + "\"", from, to });
    }
    range = juce::jlimit (0, juce::jmax (0, int (ranges.size()) - 1), range);
}

juce::String ExportSheet::lengthText (const Range& r) const
{
    const auto& project = services.daw().getProject();
    const double sr = juce::jmax (1.0, project.sampleRate);
    const auto to = r.to > 0 ? r.to : project.lengthSamples();
    if (to <= r.from) return "nothing recorded yet";
    return clock (to - r.from, sr) + " long";
}

juce::String ExportSheet::summary() const
{
    if (ranges.empty()) return "There is nothing recorded yet.";
    const auto& r = ranges[size_t (juce::jlimit (0, int (ranges.size()) - 1, range))];
    const auto loud = controller.getMasterLoudness();
    juce::String s = "A stereo " + juce::String (format == 1 ? "MP3" : "WAV") + ", " + lengthText (r)
                   + ", through the mix as it is now";
    if (loud.known && loud.integratedLufs > -100.0f)
        s += ", at " + juce::String (loud.integratedLufs, 1) + " LUFS";
    return s + ".";
}

void ExportSheet::refresh()
{
    rebuildRanges();
    for (int i = 0; i < 2; ++i)
        formatTabs[size_t (i)]->setToggleState (format == i, juce::dontSendNotification);
    if (! ranges.empty())
        rangePicker.setValue (ranges[size_t (juce::jlimit (0, int (ranges.size()) - 1, range))].name);
    exportButton.setEnabled (services.daw().getProject().hasAudio());
}

juce::Rectangle<int> ExportSheet::cardBounds() const
{
    auto r = getLocalBounds();
    const int w = juce::jmin (r.getWidth() - 80, 720);
    const int h = juce::jmin (r.getHeight() - 80, 404);
    return r.withSizeKeepingCentre (juce::jmax (380, w), juce::jmax (300, h));
}

void ExportSheet::paint (juce::Graphics& g)
{
    g.setColour (Dine::scrim.withAlpha (0.55f));
    g.fillRect (getLocalBounds());
    const auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), Dine::Radius::card);

    auto r = card.reduced (26, 24);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (19.0f, 600));
    g.drawText ("Export", r.removeFromTop (26), juce::Justification::centredLeft);
    g.setColour (Dine::ink3);
    g.setFont (Dine::Type::bodySmall());
    g.drawText ("The recorded timeline, through the mix as it is now.", r.removeFromTop (18),
                juce::Justification::centredLeft, true);

    auto row = [&] (juce::Rectangle<int> area, const juce::String& caption, const juce::String& note)
    {
        if (area.isEmpty()) return;
        g.setColour (Dine::ink4);
        g.setFont (Dine::Type::labelSection());
        g.drawText (caption, area.removeFromTop (14), juce::Justification::centredLeft);
        if (note.isNotEmpty())
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::Type::caption());
            g.drawText (note, area.removeFromBottom (14), juce::Justification::centredLeft, true);
        }
    };

    row (rangeRow, "HOW MUCH OF IT", ranges.empty() ? juce::String()
                                                    : lengthText (ranges[size_t (juce::jlimit (0, int (ranges.size()) - 1, range))]));
    row (formatRow, "FORMAT", format == 1 ? "Smaller, and good enough to send."
                                          : juce::String ("The whole quality: what an editor wants."));

    // DELIVERY LOUDNESS. Read-only here on purpose: it is a property of the session's purpose,
    // and changing it in an export dialog would change the mix somebody already approved.
    if (! loudnessRow.isEmpty())
    {
        auto area = loudnessRow;
        g.setColour (Dine::ink4);
        g.setFont (Dine::Type::labelSection());
        g.drawText ("DELIVERY LOUDNESS", area.removeFromTop (14), juce::Justification::centredLeft);
        const auto loud = controller.getMasterLoudness();
        const bool known = loud.known && loud.integratedLufs > -100.0f;
        g.setColour (Dine::ink2);
        g.setFont (Dine::Type::body());
        g.drawText (juce::String (loud.targetLufs, 1) + " LUFS  " + juce::String (Glyph::dot()) + "  from the session's purpose",
                    area.removeFromTop (20), juce::Justification::centredLeft, true);
        g.setColour (! known ? Dine::ink4 : loud.onTarget() ? Dine::ok : Dine::warn);
        g.setFont (Dine::Type::caption());
        g.drawText (! known ? "Nothing measured yet."
                            : loud.onTarget() ? "The mix is on target."
                                              : "The mix is " + juce::String (std::abs (loud.deltaLu()), 1) + " LU "
                                                    + (loud.deltaLu() > 0.0f ? "louder" : "quieter")
                                                    + " than that. Mix > Raise Loudness to Target sets it.",
                    area, juce::Justification::topLeft, true);
    }

    // What is about to be written, in one sentence, before anybody waits ten minutes for it.
    if (! sentenceRow.isEmpty())
    {
        Dine::fillRounded (g, sentenceRow.toFloat(), Dine::item, Dine::Radius::chip);
        g.setColour (Dine::ink2);
        g.setFont (Dine::Type::bodySmall());
        g.drawFittedText (summary(), sentenceRow.reduced (14, 8), juce::Justification::centredLeft, 2);
    }
}

void ExportSheet::resized()
{
    auto card = cardBounds().reduced (26, 24);
    card.removeFromTop (26 + 18 + 18);

    auto foot = card.removeFromBottom (Dine::Metric::button);
    const int ew = juce::jmax (110, exportButton.idealWidth());
    exportButton.setBounds (foot.removeFromRight (ew).withSizeKeepingCentre (ew, Dine::Metric::button));
    foot.removeFromRight (10);
    const int cw = juce::jmax (84, closeButton.idealWidth());
    closeButton.setBounds (foot.removeFromRight (cw).withSizeKeepingCentre (cw, Dine::Metric::button));
    card.removeFromBottom (16);

    sentenceRow = card.removeFromBottom (46);
    card.removeFromBottom (18);

    rangeRow = card.removeFromTop (kRowH);
    {
        auto area = rangeRow.withTrimmedTop (18).withTrimmedBottom (16);
        rangePicker.setBounds (area.removeFromLeft (juce::jmin (area.getWidth(), 320)));
    }
    formatRow = card.removeFromTop (kRowH);
    {
        auto area = formatRow.withTrimmedTop (18).withTrimmedBottom (16);
        for (int i = 0; i < 2; ++i)
        {
            const int w = juce::jmax (72, formatTabs[size_t (i)]->idealWidth());
            formatTabs[size_t (i)]->setBounds (area.removeFromLeft (w));
        }
    }
    loudnessRow = card.removeFromTop (juce::jmin (card.getHeight(), 72));
}

void ExportSheet::mouseUp (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition()) && onClose) onClose();
}

} // namespace livemix
