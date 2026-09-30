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
    bool accepted() const noexcept { return error.isEmpty() && status >= 200 && status < 300; }   // 201 / 204 count too
};

// `extraHeaders`: whole "Name: value\r\n" lines, for a service that wants more than a bearer
// token (Supabase's apikey and Prefer - app/native/Telemetry).
Result postJson (const juce::String& url, const juce::String& jsonBody, const juce::String& bearerToken,
                 int timeoutSeconds, const std::atomic<bool>* shouldCancel,
                 const juce::String& extraHeaders = {});

// From any other thread: every request running under `shouldCancel` stops now. The flag alone is
// only looked at between reads, and a read waits for the server - up to the idle timeout - so a
// quit or a cancel that set the flag and joined the worker could wait fifteen seconds or more, or
// have JUCE kill the thread. Set the flag first, then call this.
void abort (const std::atomic<bool>* shouldCancel);

} // namespace livemix::http
