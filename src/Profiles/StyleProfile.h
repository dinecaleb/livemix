#pragma once
#include "Profile.h"

namespace livemix
{

// Thin facade kept for existing callers (plugin, kit rules, tests). The data
// lives in ProfileData.cpp; see Profile.h.
namespace StyleProfile
{
    inline ChannelParameters baseline (ChannelRole role, StyleProfileId style) { return Profiles::baseline (style, role); }
    inline SourceTargets targets (ChannelRole role, StyleProfileId style)       { return Profiles::targets (style, role); }
}

} // namespace livemix
