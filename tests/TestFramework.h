#pragma once
// Minimal dependency-free test framework: TEST_CASE / CHECK / CHECK_NEAR / REQUIRE.
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace testfw
{
    struct TestCase { const char* name; std::function<void()> fn; };
    std::vector<TestCase>& registry();
    void reportFailure (const char* file, int line, const std::string& message);
    struct Registrar { Registrar (const char* name, std::function<void()> fn) { registry().push_back ({ name, std::move (fn) }); } };
    struct RequireFailed {};
}

#define TESTFW_CONCAT_INNER(a, b) a##b
#define TESTFW_CONCAT(a, b) TESTFW_CONCAT_INNER (a, b)

#define TEST_CASE(name) \
    static void TESTFW_CONCAT (testfw_fn_, __LINE__)(); \
    static testfw::Registrar TESTFW_CONCAT (testfw_reg_, __LINE__) (name, TESTFW_CONCAT (testfw_fn_, __LINE__)); \
    static void TESTFW_CONCAT (testfw_fn_, __LINE__)()

#define CHECK(expr) \
    do { if (! (expr)) testfw::reportFailure (__FILE__, __LINE__, "CHECK failed: " #expr); } while (0)

#define REQUIRE(expr) \
    do { if (! (expr)) { testfw::reportFailure (__FILE__, __LINE__, "REQUIRE failed: " #expr); throw testfw::RequireFailed {}; } } while (0)

#define CHECK_NEAR(a, b, tol) \
    do { const double va_ = double (a), vb_ = double (b), vt_ = double (tol); \
         if (std::fabs (va_ - vb_) > vt_) testfw::reportFailure (__FILE__, __LINE__, \
             std::string ("CHECK_NEAR failed: " #a " = ") + std::to_string (va_) + ", " #b " = " + std::to_string (vb_) + ", tol = " + std::to_string (vt_)); } while (0)
