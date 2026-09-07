#include "EqCurveComponent.h"
#include "LiveMixLookAndFeel.h"
#include "DSP/EqResponse.h"
#include <cmath>

namespace livemix
{

namespace
{
    constexpr int kPoints = 160;
    constexpr float kRangeDb = 15.0f;
}

float EqCurveComponent::xForFreq (float hz, float width) noexcept
{
    return width * std::log10 (juce::jmax (20.0f, hz) / 20.0f) / 3.0f;
}

void EqCurveComponent::update (const ChannelParameters& p, double sampleRate, float levelNorm)
{
    level = levelNorm;
    bool changed = curveDb.empty() || std::abs (sr - sampleRate) > 1.0;
    if (! changed)
    {
        const auto& a = current;
        changed = a.hpfEnabled != p.hpfEnabled || a.hpfHz != p.hpfHz || a.hpfSlope != p.hpfSlope
               || a.lpfEnabled != p.lpfEnabled || a.lpfHz != p.lpfHz || a.lpfSlope != p.lpfSlope
               || a.correctiveEqEnabled != p.correctiveEqEnabled || a.toneEqEnabled != p.toneEqEnabled;
        for (size_t i = 0; i < a.correctiveBands.size() && ! changed; ++i)
            changed = a.correctiveBands[i].enabled != p.correctiveBands[i].enabled || a.correctiveBands[i].freqHz != p.correctiveBands[i].freqHz
                   || a.correctiveBands[i].gainDb != p.correctiveBands[i].gainDb || a.correctiveBands[i].q != p.correctiveBands[i].q || a.correctiveBands[i].type != p.correctiveBands[i].type;
        for (size_t i = 0; i < a.toneBands.size() && ! changed; ++i)
            changed = a.toneBands[i].enabled != p.toneBands[i].enabled || a.toneBands[i].freqHz != p.toneBands[i].freqHz
                   || a.toneBands[i].gainDb != p.toneBands[i].gainDb || a.toneBands[i].q != p.toneBands[i].q || a.toneBands[i].type != p.toneBands[i].type;
    }
    if (changed)
    {
        current = p; sr = sampleRate;
        curveDb.resize (kPoints);
        for (int i = 0; i < kPoints; ++i)
        {
            const float hz = 20.0f * std::pow (10.0f, 3.0f * float (i) / float (kPoints - 1));
            curveDb[size_t (i)] = EqResponse::chainMagnitudeDb (p, sr, hz);
        }
    }
    repaint();
}

void EqCurveComponent::setFlag (float hz, const juce::String& title, const juce::String& sub)
{
    flagHz = hz; flagTitle = title; flagSub = sub;
    repaint();
}

void EqCurveComponent::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto area = getLocalBounds().toFloat();
    {
        juce::Path clip;
        clip.addRoundedRectangle (area, Tokens::Radius::control);
        g.reduceClipRegion (clip);
    }
    g.setColour (inset);
    g.fillRoundedRectangle (area, Tokens::Radius::control);
    const float w = area.getWidth(), h = area.getHeight();
    auto yForDb = [&] (float db) { return area.getY() + h * (0.5f - juce::jlimit (-0.5f, 0.5f, db / (2.0f * kRangeDb))); };

    g.setFont (LiveMixLookAndFeel::mono (9.0f, 400));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = area.getX() + xForFreq (hz, w);
        g.setColour (grid);
        g.drawVerticalLine (int (x), area.getY(), area.getBottom());
        g.setColour (textGrid);
        g.drawText (hz >= 1000.0f ? juce::String (int (hz / 1000)) + "k" : juce::String (int (hz)), int (x) + 3, int (area.getBottom()) - 15, 34, 10, juce::Justification::left);
    }
    {
        const float y = yForDb (0.0f);
        g.setColour (hair);
        const float dashes[] = { 3.0f, 4.0f };
        g.drawDashedLine (juce::Line<float> (area.getX(), y, area.getRight(), y), dashes, 2, 1.0f);
    }
    g.setColour (textGrid);
    g.drawText ("+" + juce::String (int (kRangeDb)), int (area.getX()) + 4, int (area.getY()) + 3, 30, 10, juce::Justification::left);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + juce::String (int (kRangeDb)), int (area.getX()) + 4, int (area.getBottom()) - 26, 30, 10, juce::Justification::left);

    // Faint input-level wash so the scope reads as live (a coarse stand-in for a spectrum).
    if (level > 0.01f)
    {
        g.setColour (accentStroke.withAlpha (0.10f * level));
        g.fillRect (area.getX(), area.getBottom() - h * 0.45f * level, w, h * 0.45f * level);
    }

    if (curveDb.size() < 2) return;
    juce::Path path;
    for (size_t i = 0; i < curveDb.size(); ++i)
    {
        const float x = area.getX() + w * float (i) / float (curveDb.size() - 1);
        const float y = yForDb (curveDb[i]);
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    g.setColour (accentStroke);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    auto drawMarker = [&] (const EQBandParams& b, bool flagged)
    {
        if (! b.enabled) return;
        const float x = area.getX() + xForFreq (b.freqHz, w), y = yForDb (b.gainDb);
        g.setColour (window);
        g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
        g.setColour (flagged ? warn : accentStroke);
        g.drawEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f, 1.0f);
    };
    if (current.toneEqEnabled)
        for (const auto& b : current.toneBands) drawMarker (b, flagHz > 0.0f && std::abs (std::log2 (b.freqHz / flagHz)) < 0.15f);
    if (current.correctiveEqEnabled)
        for (const auto& b : current.correctiveBands) drawMarker (b, flagHz > 0.0f && std::abs (std::log2 (b.freqHz / flagHz)) < 0.15f);

    if (flagHz > 0.0f)
    {
        const float x = area.getX() + xForFreq (flagHz, w);
        g.setColour (warn);
        const float dashes[] = { 4.0f, 4.0f };
        g.drawDashedLine (juce::Line<float> (x, area.getY() + 14.0f, x, area.getBottom() - 16.0f), dashes, 2, 1.0f);
        g.setFont (LiveMixLookAndFeel::mono (10.0f, 600));
        const bool left = x > area.getRight() - 230.0f;
        const int tx = left ? int (x) - 227 : int (x) + 7;
        g.drawText (flagTitle, tx, int (area.getY()) + 12, 220, 12, left ? juce::Justification::right : juce::Justification::left);
        g.drawText (flagSub, tx, int (area.getY()) + 25, 220, 12, left ? juce::Justification::right : juce::Justification::left);
    }
    g.setColour (hair);
    g.drawRoundedRectangle (area.reduced (0.5f), Tokens::Radius::control, 1.0f);
}

} // namespace livemix
