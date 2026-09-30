#include "HttpJson.h"
#include <algorithm>
#include <mutex>
#include <vector>

namespace livemix::http
{

namespace
{
    // The streams being read right now, by the flag that cancels them. A stream is taken out
    // under the same lock before it is destroyed, so abort() never reaches a dead one.
    struct Live { const std::atomic<bool>* flag; juce::WebInputStream* stream; };
    std::mutex liveLock;
    std::vector<Live> live;

    struct Registered
    {
        Registered (const std::atomic<bool>* f, juce::InputStream* s)
            : stream (dynamic_cast<juce::WebInputStream*> (s))
        {
            if (f == nullptr || stream == nullptr) { stream = nullptr; return; }
            std::lock_guard<std::mutex> l (liveLock);
            live.push_back ({ f, stream });
        }
        ~Registered()
        {
            if (stream == nullptr) return;
            std::lock_guard<std::mutex> l (liveLock);
            live.erase (std::remove_if (live.begin(), live.end(), [this] (const Live& x) { return x.stream == stream; }), live.end());
        }
        juce::WebInputStream* stream;
    };
}

void abort (const std::atomic<bool>* shouldCancel)
{
    if (shouldCancel == nullptr) return;
    std::lock_guard<std::mutex> l (liveLock);
    for (const auto& x : live)
        if (x.flag == shouldCancel) x.stream->cancel();
}

Result postJson (const juce::String& url, const juce::String& jsonBody, const juce::String& bearerToken,
                 int timeoutSeconds, const std::atomic<bool>* shouldCancel, const juce::String& extraHeaders)
{
    Result result;
    const auto cancelled = [shouldCancel] { return shouldCancel != nullptr && shouldCancel->load(); };

    // Not parsed: a parsed URL's query ("?on_conflict=...") is moved into the body of a POST,
    // in front of the JSON, and the server sees neither.
    auto target = juce::URL::createWithoutParsing (url).withPOSTData (jsonBody);
    juce::String headers = "Content-Type: application/json\r\n";
    if (bearerToken.trim().isNotEmpty()) headers += "Authorization: Bearer " + bearerToken.trim() + "\r\n";
    headers += extraHeaders;

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

    // Read the body in chunks so a cancel during a slow transfer is honoured too - between reads
    // by the flag, and in the middle of one by abort().
    const Registered registered (shouldCancel, stream.get());
    if (cancelled()) { result.error = "cancelled"; return result; }
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
