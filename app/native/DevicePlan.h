#pragma once
#include <juce_core/juce_core.h>

namespace livemix
{

// What to open when a session opens. The session remembers the console it was recorded on;
// the console is a preference, the session is the document, and a document must open on a
// Mac that does not have the console plugged in - at home, on Monday, on the laptop the
// recording was copied to. So the plan is: the session's own devices when they are here;
// otherwise whatever is open already; otherwise an output alone, so a recording still plays;
// otherwise nothing, and the Audio device page. Every case that is not the first carries a
// sentence for the toast, so the engineer knows which device is missing and where to fix it.
// Pure: no device is touched here, which is what makes it testable without one.
struct DevicePlan
{
    enum class Action { OpenBoth, OpenOutputOnly, KeepOpen, None };
    Action action = Action::None;
    juce::String input, output;
    juce::String note;          // empty when the session's own devices open as they were
};

inline DevicePlan planDevicesForSession (const juce::String& wantedInput, const juce::String& wantedOutput, bool sessionHasAudio,
                                         const juce::StringArray& inputs, const juce::StringArray& outputs,
                                         const juce::String& openInput, const juce::String& openOutput, bool somethingOpen)
{
    DevicePlan p;
    auto hasInput = [&] (const juce::String& n) { return n.isNotEmpty() && inputs.contains (n); };
    auto hasOutput = [&] (const juce::String& n) { return n.isNotEmpty() && outputs.contains (n); };
    auto whereToFix = juce::String (" Pick it under Audio device when it is back.");

    if (hasInput (wantedInput))
    {
        p.action = DevicePlan::Action::OpenBoth;
        p.input = wantedInput;
        if (hasOutput (wantedOutput)) p.output = wantedOutput;
        else
        {
            // The same device, else the first one there is; either way the output it used is
            // missing and the toast says so, because the broadcast is going somewhere else.
            p.output = hasOutput (wantedInput) ? wantedInput : (outputs.isEmpty() ? wantedInput : outputs[0]);
            if (wantedOutput.isNotEmpty() && wantedOutput != p.output)
                p.note = "The output it used, " + wantedOutput + ", is not connected; the mix is going out of " + p.output + " instead.";
        }
        return p;
    }

    const juce::String missing = wantedInput.isNotEmpty()
        ? "This session was recorded on " + wantedInput + ", which is not connected."
        : juce::String();

    if (somethingOpen)
    {
        p.action = DevicePlan::Action::KeepOpen;
        p.input = openInput;
        p.output = openOutput;
        if (missing.isNotEmpty())
            p.note = missing + " It is open on " + (openInput.isNotEmpty() ? openInput : openOutput) + "." + whereToFix;
        return p;
    }

    if (sessionHasAudio || wantedInput.isNotEmpty())
    {
        const juce::String out = hasOutput (wantedOutput) ? wantedOutput : (outputs.isEmpty() ? juce::String() : outputs[0]);
        if (out.isNotEmpty())
        {
            p.action = DevicePlan::Action::OpenOutputOnly;
            p.output = out;
            p.note = (missing.isNotEmpty() ? missing + " " : juce::String())
                   + "Playback is on " + out + "." + (missing.isNotEmpty() ? whereToFix : juce::String());
            return p;
        }
    }

    p.action = DevicePlan::Action::None;
    p.note = (missing.isNotEmpty() ? missing + " " : juce::String ("No audio device is open. ")) + "Pick one under Audio device.";
    return p;
}

} // namespace livemix
