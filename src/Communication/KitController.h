#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "InstanceRegistry.h"
#include "Analysis/KitAnalysis.h"
#include "Core/StyleId.h"

namespace livemix
{

// Drives ANALYZE KIT for one group from any instance. Message thread only.
// Instances that vanish mid-analysis are simply dropped from the result.
class KitController
{
public:
    enum class State : int { Idle = 0, Analyzing, Complete };

    void startKitAnalyze (const std::string& group);
    void abort();

    // Call periodically (e.g. from a timer). Returns true when the state changed.
    bool poll (StyleProfileId style);

    State getState() const noexcept { return state; }
    const KitRecommendationResult& getResult() const noexcept { return result; }
    std::vector<InstanceInfo> getGroupMembers (const std::string& group) const;

    // Apply helpers (message thread). Skip members that no longer exist.
    void applyAllSafeChanges (const std::string& group);
    void applyBalance (const std::string& group);

private:
    IKitEndpoint* find (uint64_t id, const std::string& group) const;

    State state = State::Idle;
    std::string activeGroup;
    std::vector<uint64_t> pending;
    KitRecommendationResult result;
};

} // namespace livemix
