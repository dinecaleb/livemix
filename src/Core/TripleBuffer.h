#pragma once
#include <array>
#include <atomic>

namespace livemix
{

// Single-writer / single-reader publication of a whole value without locks or
// allocation. The writer (message thread) fills a spare slot and publishes it;
// the reader (audio thread) picks up the newest published slot when it asks.
// Neither side ever waits. T must be copy-assignable and must not allocate on
// copy (fixed-capacity structs only).
template <typename T>
class TripleBuffer
{
public:
    // Writer: the slot to fill. Its previous content is stale; assign the whole value.
    T& beginWrite() noexcept { return slots[size_t (writeIndex)]; }

    // Writer: make the filled slot visible and take the freed slot for the next write.
    void publish() noexcept
    {
        const int previous = back.exchange (writeIndex | kNewFlag, std::memory_order_acq_rel);
        writeIndex = previous & kIndexMask;
    }

    // Reader: true when a value published since the last acquire() is waiting.
    bool hasNew() const noexcept { return (back.load (std::memory_order_acquire) & kNewFlag) != 0; }

    // Reader: swap in the newest slot (if any) and return the current value.
    const T& acquire() noexcept
    {
        if (hasNew())
            readIndex = back.exchange (readIndex, std::memory_order_acq_rel) & kIndexMask;
        return slots[size_t (readIndex)];
    }

    const T& current() const noexcept { return slots[size_t (readIndex)]; }

private:
    static constexpr int kNewFlag = 4;
    static constexpr int kIndexMask = 3;

    std::array<T, 3> slots {};
    std::atomic<int> back { 0 };
    int writeIndex = 1;
    int readIndex = 2;
};

} // namespace livemix
