#include "TestFramework.h"
#include <cstring>

namespace testfw
{
    std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
    static int failures = 0;
    void reportFailure (const char* file, int line, const std::string& message)
    {
        ++failures;
        std::printf ("    %s:%d: %s\n", file, line, message.c_str());
    }
}

int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedCases = 0;
    for (auto& t : testfw::registry())
    {
        if (filter != nullptr && std::strstr (t.name, filter) == nullptr) continue;
        const int before = testfw::failures;
        try { t.fn(); }
        catch (testfw::RequireFailed&) {}
        catch (std::exception& e) { testfw::reportFailure ("<exception>", 0, e.what()); }
        ++run;
        const bool failed = testfw::failures != before;
        if (failed) ++failedCases;
        std::printf ("[%s] %s\n", failed ? "FAIL" : " OK ", t.name);
    }
    std::printf ("\n%d test cases run, %d failed, %d assertion failures\n", run, failedCases, testfw::failures);
    return failedCases == 0 ? 0 : 1;
}
