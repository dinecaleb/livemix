#include "TestFramework.h"
#include "Communication/InstanceRegistry.h"

using namespace livemix;

TEST_CASE ("InstanceRegistry: register, group, update, unregister")
{
    auto& reg = InstanceRegistry::get();
    const size_t before = reg.size();
    InstanceInfo kick; kick.role = ChannelRole::KickIn; kick.group = "Drum Kit 1"; kick.displayName = "Kick";
    InstanceInfo snare; snare.role = ChannelRole::SnareTop; snare.group = "Drum Kit 1";
    InstanceInfo vox; vox.role = ChannelRole::DrumBus; vox.group = "Band";
    auto a = reg.registerInstance (kick);
    auto b = reg.registerInstance (snare);
    auto c = reg.registerInstance (vox);
    CHECK (a != b && b != c);
    CHECK (reg.size() == before + 3);
    CHECK (reg.membersOfGroup ("Drum Kit 1").size() == 2);
    snare.group = "Band";
    reg.updateInstance (b, snare);
    CHECK (reg.membersOfGroup ("Band").size() == 2);
    reg.unregisterInstance (a); reg.unregisterInstance (b); reg.unregisterInstance (c);
    CHECK (reg.size() == before);
}
