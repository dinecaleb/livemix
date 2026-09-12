#pragma once
#include <atomic>
#include <juce_core/juce_core.h>

namespace livemix::http
{

// One JSON POST, with a bearer token and a cancel flag that is actually honoured - while the
// connection is being made, while the server is thinking and while the body is being read.
// The only place in DLIVE that talks to a network, so there is one place to audit, one place
// that must never be given a key to log, and one place that must never be called from the
// message thread or the audio thread.
struct Result
{
    int status = 0;
    juce::String body;
    juce::String error;      // transport-level only; an HTTP error arrives as a status with a body

    bool ok() const noexcept { return error.isEmpty() && status == 200; }
};

Result postJson (const juce::String& url, const juce::String& jsonBody, const juce::String& bearerToken,
                 int timeoutSeconds, const std::atomic<bool>* shouldCancel);

} // namespace livemix::http
