#pragma once

namespace livemix
{

// Non-owning view of a multichannel float buffer. Cheap to copy; never allocates.
struct AudioBlockView
{
    float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;

    float* channel (int index) const noexcept { return channels[index]; }
};

} // namespace livemix
