#include "KitController.h"

namespace livemix
{

std::vector<InstanceInfo> KitController::getGroupMembers (const std::string& group) const
{
    return InstanceRegistry::get().membersOfGroup (group);
}

IKitEndpoint* KitController::find (uint64_t id, const std::string& group) const
{
    for (const auto& m : InstanceRegistry::get().membersOfGroup (group))
        if (m.id == id) return m.endpoint;
    return nullptr;
}

void KitController::startKitAnalyze (const std::string& group)
{
    activeGroup = group;
    pending.clear();
    for (const auto& m : InstanceRegistry::get().membersOfGroup (group))
        if (m.endpoint != nullptr)
        {
            m.endpoint->kitStartAnalyze();
            pending.push_back (m.id);
        }
    state = pending.empty() ? State::Idle : State::Analyzing;
}

void KitController::abort()
{
    pending.clear();
    state = State::Idle;
}

bool KitController::poll (StyleProfileId style)
{
    if (state != State::Analyzing) return false;
    for (uint64_t id : pending)
        if (auto* ep = find (id, activeGroup))
            if (ep->kitIsAnalyzing()) return false;

    std::vector<KitMember> members;
    for (uint64_t id : pending)
        if (auto* ep = find (id, activeGroup)) members.push_back (ep->kitGetMember());
    result = KitAnalysis::analyze (members, style);
    pending.clear();
    state = State::Complete;
    return true;
}

void KitController::applyAllSafeChanges (const std::string& group)
{
    for (const auto& m : InstanceRegistry::get().membersOfGroup (group))
        if (m.endpoint != nullptr) m.endpoint->kitApplySafeChanges();
}

void KitController::applyBalance (const std::string& group)
{
    if (! result.valid) return;
    for (const auto& b : result.balance)
        if (auto* ep = find (b.instanceId, group))
            if (b.outputTrimDeltaDb != 0.0f) ep->kitApplyOutputTrimDelta (b.outputTrimDeltaDb);
}

} // namespace livemix
