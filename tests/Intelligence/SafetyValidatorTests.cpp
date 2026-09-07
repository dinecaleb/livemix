#include "TestFramework.h"
#include "Intelligence/SafetyValidator.h"

using namespace livemix;

TEST_CASE ("SafetyValidator: clamps to spec bounds, drops forbidden and unknown ids")
{
    RecommendationResult in;
    in.valid = true;
    Recommendation r;
    r.what = "test";
    r.changes = {
        { "compThreshold", -200.0f },   // clamp to -60
        { "corrEq1Gain", 40.0f },       // clamp to 18 (standard) / 4 (AI)
        { "role", 3.0f },               // forbidden
        { "inputTrim", 12.0f },         // forbidden (capture gain is physical)
        { "nonsense", 1.0f },           // unknown
        { "gateOn", 0.7f }              // bool rounds to 1
    };
    r.safeToAutoApply = true;
    in.items.push_back (r);

    SafetyValidator::Report rep;
    auto out = SafetyValidator::validate (in, false, &rep);
    REQUIRE (out.items.size() == 1);
    REQUIRE (out.items[0].changes.size() == 3);
    CHECK_NEAR (out.items[0].changes[0].value, -60.0f, 1e-6f);
    CHECK_NEAR (out.items[0].changes[1].value, 18.0f, 1e-6f);
    CHECK_NEAR (out.items[0].changes[2].value, 1.0f, 1e-6f);
    CHECK (rep.changesDropped == 3);
    CHECK (rep.changesClamped == 2);
    CHECK (out.items[0].safeToAutoApply);

    auto ai = SafetyValidator::validate (in, true, &rep);
    REQUIRE (ai.items.size() == 1);
    CHECK_NEAR (ai.items[0].changes[1].value, 4.0f, 1e-6f);
    CHECK (! ai.items[0].safeToAutoApply); // AI items are never auto-applied
}

TEST_CASE ("SafetyValidator: AI item with only unsafe changes is removed; standard keeps explanation")
{
    RecommendationResult in;
    in.valid = true;
    Recommendation r; r.what = "x"; r.changes = { { "bypass", 1.0f } };
    in.items.push_back (r);
    CHECK (SafetyValidator::validate (in, true).items.empty());
    auto std_ = SafetyValidator::validate (in, false);
    REQUIRE (std_.items.size() == 1);
    CHECK (std_.items[0].changes.empty());
}
