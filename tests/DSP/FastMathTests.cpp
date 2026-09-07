#include "TestFramework.h"
#include "Core/FastMath.h"
#include "Core/DbUtils.h"
#include <cmath>

using namespace livemix;

TEST_CASE ("FastMath: dB conversions are accurate enough for dynamics")
{
    float maxErr = 0.0f;
    for (float db = -100.0f; db <= 24.0f; db += 0.37f)
    {
        const float g = dbToGain (db);
        maxErr = std::max (maxErr, std::fabs (fastmath::gainToDb (g) - db));
    }
    CHECK (maxErr < 0.02f);

    float maxRel = 0.0f;
    for (float db = -100.0f; db <= 24.0f; db += 0.41f)
    {
        const float exact = dbToGain (db);
        const float fast = fastmath::dbToGain (db);
        maxRel = std::max (maxRel, std::fabs (fast - exact) / exact);
    }
    CHECK (maxRel < 0.002f);

    CHECK (fastmath::gainToDb (0.0f) <= -120.0f);
    CHECK_NEAR (fastmath::dbToGain (-200.0f), 0.0f, 1e-9f);
}
