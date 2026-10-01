#include "TestFramework.h"
#include "native/MixController.h"
#include <chrono>
#include <thread>

using namespace livemix;
namespace
{
struct PlanningFixture
{
    MixController controller;
    std::array<std::array<float, 256>, 2> input {}, output {};
    int position = 0;
    PlanningFixture()
    {
        MixSession session;
        session.inputs = {{"Lead", ChannelRole::LeadVocal, 0, -1}, {"Keys", ChannelRole::Piano, 1, -1}};
        controller.setSession (session); controller.prepare (48000, 256);
    }
    void feed()
    {
        for (int i = 0; i < 256; ++i)
        {
            const float t = float (position + i) / 48000;
            const float env = 0.1f + 0.9f * std::fabs (std::sin (t * 9));
            input[0][size_t (i)] = env * 0.2f * std::sin (t * 1382.3f);
            input[1][size_t (i)] = env * 0.1f * std::sin (t * 1646.2f);
        }
        const float* ip[] {input[0].data(), input[1].data()};
        float* op[] {output[0].data(), output[1].data()};
        controller.process (ip, 2, op, 2, 256); position += 256;
        std::this_thread::sleep_for (std::chrono::microseconds (400));
    }
    bool wait (MixController::Stage wanted)
    {
        for (int i = 0; i < 12000; ++i)
        {
            controller.poll();
            if (controller.getStage() == wanted) return true;
            feed();
        }
        return false;
    }
    void begin()
    {
        controller.startTuneMix ({1.0f, -200.0f, 0.0f});
        REQUIRE (wait (MixController::Stage::Planning));
    }
};
}
TEST_CASE ("MeasuredPlanning: background planning presents a checked proposal and keeps exact AFTER")
{
    PlanningFixture f; f.begin();
    REQUIRE (f.wait (MixController::Stage::Preview));
    REQUIRE (f.controller.hasPlan());
    CHECK (f.controller.getLastListen().replay != nullptr);
    bool measured = false;
    for (const auto& note : f.controller.getPlan()->notes) if (note.find ("Measured the proposed chain") != std::string::npos) measured = true;
    CHECK (measured);
    const auto after = f.controller.getPlan()->proposed;
    f.controller.keepPlan();
    CHECK (MixPlanner::countParameterChanges (after, f.controller.getKept()) == 0);
}
TEST_CASE ("MeasuredPlanning: cancellation discards late worker results without changing the console")
{
    PlanningFixture f;
    const auto before = f.controller.getKept(); f.begin();
    f.controller.abortTuneMix();
    for (int i = 0; i < 100; ++i) { f.controller.poll(); f.feed(); }
    CHECK (f.controller.getStage() != MixController::Stage::Preview);
    CHECK (MixPlanner::countParameterChanges (before, f.controller.getKept()) == 0);
}
TEST_CASE ("MeasuredPlanning: a hand edit during planning is never overwritten by stale results")
{
    PlanningFixture f; f.begin();
    f.controller.setStripFader (1, -12);
    const auto edited = f.controller.getKept();
    REQUIRE (f.wait (MixController::Stage::Ready));
    CHECK (MixPlanner::countParameterChanges (edited, f.controller.getKept()) == 0);
    CHECK (! f.controller.hasPlan());
}
