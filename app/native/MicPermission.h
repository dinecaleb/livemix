#pragma once
#include <functional>

namespace livemix
{

// WHAT macOS SAYS ABOUT THE MICROPHONE.
//
// DINE reads a console, not a microphone, but macOS does not know the difference: every
// audio input goes through the same privacy switch. Asking the system what it thinks - rather
// than inferring it from a stream that would not open - is the difference between "System
// Settings > Privacy & Security > Microphone" and a sentence that sends somebody to the wrong
// screen. Enumerating devices needs none of this and is never gated by it.
//
// macOS only. Everywhere else every call answers Granted, because there is nothing to ask.
namespace MicPermission
{
    enum class State
    {
        Granted = 0,     // yes, or the platform has no such question
        Undetermined,    // never asked: the next attempt to listen will put up the prompt
        Denied,          // asked and refused, or switched off later
        Restricted       // the machine is managed and the person cannot change it
    };

    State check();                                   // never prompts
    // Puts the prompt up once, if it has never been asked. The callback comes back on the
    // message thread. Called when the user chooses an input device - the moment at which a
    // prompt makes sense - never at launch.
    void request (std::function<void (bool granted)> done);
    bool denied();                                   // Denied or Restricted: no point retrying
}

} // namespace livemix
