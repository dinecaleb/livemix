#pragma once

namespace livemix
{

// AI assistance is switched off on purpose across every Dine product: no provider
// is installed, the AI ASSIST button and AI SETUP menu are hidden, and Tune is
// Standard (deterministic, offline) only. The code paths stay in place behind this
// constant so the feature can be brought back with one change.
inline constexpr bool kAIAssistAvailable = false;

} // namespace livemix
