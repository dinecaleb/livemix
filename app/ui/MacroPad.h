#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include "AppTheme.h"
#include "Mix/MixMacros.h"

namespace livemix
{

// The overview controls on TUNE, as two two-axis pads and one ribbon.
//
// A pad is two macros on one square: one across, one up, the plan (50 / 50) at dead centre
// under a dashed ring. The puck goes where the pointer goes - a press anywhere jumps it there
// and the drag is absolute, so a volunteer never has to find a handle - and a double-click
// puts that pad back at the plan. Three snaps under it are named places on the square
// ("Speech", "Choir", "Plan"); a snap eases the puck there over a beat so the move is seen,
// and reads as chosen only while the puck sits exactly on its point. The pad never owns the
// values: every change goes out through `onChange` to the controller, and what the
// controller has comes back through `setValues` every tick.
//
// Under LIVE SAFE the controller fences each macro to `macroRange()`; the pad is told the
// fence (`setLimits`), draws the fenced bands hatched, keeps the reason in its tooltip and
// never asks for a value that would be clamped.
//
// Keyboard: the pad takes focus (a ring says so), the arrows nudge one, shift-arrows five,
// and the accessibility handler is told "<macro> <value>" on every change.
class MacroPad : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    struct Snap { const char* name; float x, y; };
    struct Corners { const char* tl; const char* tr; const char* bl; const char* br; };

    MacroPad (const juce::String& title, MixMacro across, MixMacro up, Corners, std::vector<Snap>,
              std::function<void (MixMacro, float)> onChange);
    ~MacroPad() override;

    static constexpr int kMinPad = 150, kMaxPad = 236;
    static constexpr int kHead = 16, kHeadGap = 8, kSnapGap = 10;
    // The head is the title and nothing else: the values are read inside the square, and only
    // once the puck has left the plan (centre is the plan, so at the plan there is nothing to say).
    static int headFor (int) { return kHead; }
    static int heightFor (int padSize, bool compact)
    {
        return headFor (padSize) + kHeadGap + padSize + (compact ? 0 : kSnapGap + Dine::Metric::control);
    }

    void setValues (float across, float up);          // what the controller has: instant
    void settleTo (float across, float up);           // a named move: written at once, the puck eases there
    void setLimits (float lo, float hi, const juce::String& reason);
    void setPadSize (int px);
    void setCompact (bool);                            // drops the snap row
    bool isCompact() const noexcept { return compact; }

    bool isActive() const noexcept { return dragging; }
    bool isOffCentre() const noexcept { return std::abs (x - 50.0f) >= 0.5f || std::abs (y - 50.0f) >= 0.5f; }
    const juce::String& getTitle() const noexcept { return title; }
    MixMacro getAcross() const noexcept { return xm; }
    MixMacro getUp() const noexcept { return ym; }
    float getAcrossValue() const noexcept { return x; }
    float getUpValue() const noexcept { return y; }
    juce::String describeValues() const;               // "BASS 62  ·  VOCALS 48"

    std::function<void()> onActiveChanged;             // press / release, so a panel can say "HOLDING"

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class Value;
    void apply (float across, float up);               // clamps, rounds, writes both axes
    void pointerTo (juce::Point<float>);
    void refreshSnaps();
    void refreshTooltip();
    void announce();
    juce::Rectangle<int> square() const noexcept { return pad; }
    juce::Rectangle<int> travel() const noexcept { return pad.reduced (kInset); }
    juce::Point<float> puckAt (float across, float up) const noexcept;
    void timerCallback() override;

    static constexpr int kInset = 12;                  // the puck's centre stays this far inside the square
    static constexpr int kPuck = 24;
    static constexpr double kEaseMs = 140.0;

    juce::String title;
    MixMacro xm, ym;
    Corners corners;
    std::vector<Snap> snaps;
    std::vector<std::unique_ptr<DineButton>> snapButtons;
    std::function<void (MixMacro, float)> changed;
    float x = 50.0f, y = 50.0f;                        // the controller's values
    float shownX = 50.0f, shownY = 50.0f;              // where the puck is drawn (differs while easing)
    float easeFromX = 50.0f, easeFromY = 50.0f;
    double easeStart = 0.0;
    float lo = 0.0f, hi = 100.0f;
    juce::String limitReason;
    bool compact = false, dragging = false;
    int padSize = kMinPad;
    juce::Rectangle<int> head, pad, snapRow;
};

// One macro on a horizontal ribbon: the plan at the middle, the fill growing from it towards
// the word it leans to. Same rules as the pad - absolute drag, double-click for the plan,
// the LIVE SAFE fence hatched, arrows on the keyboard.
class MacroRibbon : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    MacroRibbon (MixMacro, std::function<void (float)> onChange);
    ~MacroRibbon() override;

    static constexpr int kHeight = 26;

    void setValue (float);                             // the controller's value: instant
    void settleTo (float);                             // written at once, the cap eases there
    void setLimits (float lo, float hi, const juce::String& reason);
    bool isOffCentre() const noexcept { return std::abs (value - 50.0f) >= 0.5f; }
    float getValue() const noexcept { return value; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class Value;
    void apply (float);
    void refreshTooltip();
    void announce();
    void timerCallback() override;

    static constexpr double kEaseMs = 140.0;

    MixMacro macro;
    std::function<void (float)> changed;
    float value = 50.0f, shown = 50.0f, easeFrom = 50.0f;
    double easeStart = 0.0;
    float lo = 0.0f, hi = 100.0f;
    juce::String limitReason;
    bool dragging = false;
    juce::Rectangle<int> label, low, high, track, readout;
};

} // namespace livemix
