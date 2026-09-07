#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace livemix
{

// Vertical level meter on the design's 60 dB scale: green body shading to
// amber and red near 0 dBFS, 2 px peak-hold line, latching clip lamp on top.
// Horizontal variant used by the kit rows.
class MeterComponent : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Orientation { Vertical, Horizontal };
    explicit MeterComponent (Orientation o = Orientation::Vertical) : orientation (o) {}

    void setLevels (float peakDb, float holdDb, bool clipped);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override { clipLatched = false; repaint(); }

    static float normFor (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (juce::jmin (0.0f, db) + 60.0f) / 60.0f); }
    static float yFor (float db, float height) noexcept { return height * (1.0f - normFor (db)); }

private:
    Orientation orientation;
    float peakDb = -120.0f, holdDb = -120.0f;
    bool clipLatched = false;
};

} // namespace livemix
