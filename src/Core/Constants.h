#pragma once

namespace livemix
{

// The drum channel processor supports mono or stereo streams. Buffers for
// per-channel filter/detector state are sized statically from this.
inline constexpr int kMaxChannels = 2;

// Absolute floor used when converting near-silence to decibels.
inline constexpr float kSilenceDb = -120.0f;

} // namespace livemix
