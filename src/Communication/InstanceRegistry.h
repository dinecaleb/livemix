#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "Core/ChannelRole.h"
#include "IKitEndpoint.h"

namespace livemix
{

// Process-local registry of live LiveMix instances. This is the seed of the
// Phase 3 kit intelligence: it lets instances discover each other and their
// group membership without any network. Message-thread only; never touched
// from the audio callback.
struct InstanceInfo
{
    uint64_t id = 0;
    ChannelRole role = ChannelRole::KickIn;
    std::string group = "Drum Kit 1";
    std::string displayName;
    IKitEndpoint* endpoint = nullptr; // optional; owned by the instance, cleared on unregister
};

class InstanceRegistry
{
public:
    static InstanceRegistry& get();

    uint64_t registerInstance (const InstanceInfo& info);
    void updateInstance (uint64_t id, const InstanceInfo& info);
    void unregisterInstance (uint64_t id);

    std::vector<InstanceInfo> snapshot() const;
    std::vector<InstanceInfo> membersOfGroup (const std::string& group) const;
    size_t size() const;

private:
    InstanceRegistry() = default;
    mutable std::mutex mutex;
    std::vector<InstanceInfo> instances;
    uint64_t nextId = 1;
};

} // namespace livemix
