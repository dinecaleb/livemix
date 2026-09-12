#include "HttpJson.h"

namespace livemix::http
{

Result postJson (const juce::String& url, const juce::String& jsonBody, const juce::String& bearerToken,
                 int timeoutSeconds, const std::atomic<bool>* shouldCancel)
{
    Result result;
    const auto cancelled = [shouldCancel] { return shouldCancel != nullptr && shouldCancel->load(); };

    juce::URL target (url);
    target = target.withPOSTData (jsonBody);
    juce::String headers = "Content-Type: application/json\r\n";
    if (bearerToken.trim().isNotEmpty()) headers += "Authorization: Bearer " + bearerToken.trim() + "\r\n";

    // The progress callback is polled (about every 1 ms) until the response headers arrive,
    // which is where a slow model spends its time; returning false aborts the connection.
    auto stream = target.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                .withExtraHeaders (headers)
                                                .withConnectionTimeoutMs (juce::jmax (15, timeoutSeconds) * 1000)
                                                .withStatusCode (&result.status)
                                                .withHttpRequestCmd ("POST")
                                                .withProgressCallback ([cancelled] (int, int) { return ! cancelled(); }));
    if (cancelled()) { result.error = "cancelled"; return result; }
    if (stream == nullptr)
    {
        result.error = "could not connect to " + juce::URL (url).getDomain();
        return result;
    }

    // Read the body in chunks so a cancel during a slow transfer is honoured too.
    juce::MemoryOutputStream out;
    juce::HeapBlock<char> chunk (8192);
    while (! stream->isExhausted())
    {
        if (cancelled())
        {
            if (auto* web = dynamic_cast<juce::WebInputStream*> (stream.get())) web->cancel();
            result.error = "cancelled";
            return result;
        }
        const int n = stream->read (chunk, 8192);
        if (n <= 0) break;
        out.write (chunk, size_t (n));
    }
    result.body = out.toString();
    return result;
}

} // namespace livemix::http
