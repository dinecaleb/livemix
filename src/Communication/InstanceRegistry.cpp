#include "InstanceRegistry.h"
#include <algorithm>

namespace livemix
{

InstanceRegistry& InstanceRegistry::get()
{
    static InstanceRegistry registry;
    return registry;
}

uint64_t InstanceRegistry::registerInstance (const InstanceInfo& info)
{
    std::lock_guard<std::mutex> lock (mutex);
    InstanceInfo copy = info;
    copy.id = nextId++;
    instances.push_back (copy);
    return copy.id;
}

void InstanceRegistry::updateInstance (uint64_t id, const InstanceInfo& info)
{
    std::lock_guard<std::mutex> lock (mutex);
    for (auto& i : instances)
        if (i.id == id) { i = info; i.id = id; return; }
}

void InstanceRegistry::unregisterInstance (uint64_t id)
{
    std::lock_guard<std::mutex> lock (mutex);
    instances.erase (std::remove_if (instances.begin(), instances.end(),
                                     [id] (const InstanceInfo& i) { return i.id == id; }),
                     instances.end());
}

std::vector<InstanceInfo> InstanceRegistry::snapshot() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return instances;
}

std::vector<InstanceInfo> InstanceRegistry::membersOfGroup (const std::string& group) const
{
    std::lock_guard<std::mutex> lock (mutex);
    std::vector<InstanceInfo> out;
    for (const auto& i : instances)
        if (i.group == group) out.push_back (i);
    return out;
}

size_t InstanceRegistry::size() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return instances.size();
}

} // namespace livemix
