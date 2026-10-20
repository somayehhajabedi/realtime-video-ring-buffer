
#pragma once

#include "frame.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace realtime {

constexpr std::size_t RingCapacity = 64;

// Each slot stores one frame.
struct Slot {
    std::atomic<std::uint64_t> version{0};
    std::atomic<std::uint64_t> frameIndex{0};    
    std::array<std::atomic<std::byte>, FrameSize> data{};
};

// Shared metadata.
struct SharedHeader {
    std::atomic<bool> ready{false};
    std::atomic<bool> producerDone{false};
    std::atomic<std::uint64_t> latestFrameIndex{0};
    std::atomic<std::uint64_t> validFromIndex{1};
};
struct RingBuffer {
    SharedHeader header;
    std::array<Slot, RingCapacity> slots{};
};

enum class ReadStatus {
    Success,
    NotYetProduced,
    Overwritten,
    Retry
};

// --------------------------------------------------
// Write a frame into one slot.
// Single writer per slot.
// --------------------------------------------------
inline void writeFrame(Slot& slot, const Frame& frame)
{
    // Odd version: write in progress.
    slot.version.fetch_add(1, std::memory_order_seq_cst);

    for (std::size_t i = 0; i < FrameSize; ++i) {
        slot.data[i].store(
            frame.data[i],
            std::memory_order_seq_cst
        );
    }

    slot.frameIndex.store(
        frame.index,
        std::memory_order_seq_cst
    );

    // Even version: write complete.
    slot.version.fetch_add(1, std::memory_order_seq_cst);
}

// --------------------------------------------------
// Read a frame from one slot.
// --------------------------------------------------
inline bool readFrame(const Slot& slot, Frame& output)
{
    const auto versionBefore =
        slot.version.load(std::memory_order_seq_cst);

    if (versionBefore == 0 || (versionBefore & 1))
        return false;

    const auto frameIndex =
        slot.frameIndex.load(std::memory_order_seq_cst);

    Frame temp{};

    for (std::size_t i = 0; i < FrameSize; ++i) {
        temp.data[i] =
            slot.data[i].load(std::memory_order_seq_cst);
    }

    const auto versionAfter =
        slot.version.load(std::memory_order_seq_cst);

    if (versionBefore != versionAfter)
        return false;

    temp.index = frameIndex;
    output = temp;

    return true;
}

// --------------------------------------------------
// Write a frame into the ring buffer.
// Detect skipped frame indices.
// --------------------------------------------------
inline void writeFrame(RingBuffer& ring, const Frame& frame)
{
    const auto previousIndex =
        ring.header.latestFrameIndex.load(
            std::memory_order_seq_cst
        );

    // Producer indices must be strictly increasing.
    if (frame.index == 0 ||
        (previousIndex != 0 && frame.index <= previousIndex)) {
        return;
    }

    // Detect skipped frames.
    if (previousIndex != 0 &&
        frame.index - previousIndex > 1) {
        ring.header.validFromIndex.store(
            frame.index,
            std::memory_order_seq_cst
        );
    }

    const std::size_t slotIndex =
        frame.index % RingCapacity;

    writeFrame(ring.slots[slotIndex], frame);

    // Publish completed frame.
    ring.header.latestFrameIndex.store(
        frame.index,
        std::memory_order_seq_cst
    );
}

// --------------------------------------------------
// Read a requested frame from the ring buffer.
// --------------------------------------------------
inline bool readFrame(
    const RingBuffer& ring,
    std::uint64_t requestedIndex,
    Frame& output)
{
    const std::size_t slotIndex =
        requestedIndex % RingCapacity;

    Frame temp{};

    if (!readFrame(ring.slots[slotIndex], temp))
        return false;

    if (temp.index != requestedIndex)
        return false;

    output = temp;
    return true;
}


enum class SlotReadStatus {
    Success,
    Empty,
    Retry
};

inline SlotReadStatus tryReadSlot(
    const Slot& slot,
    Frame& output)
{
    const auto before =
        slot.version.load(std::memory_order_seq_cst);

    if (before == 0)
        return SlotReadStatus::Empty;

    if (before & 1)
        return SlotReadStatus::Retry;

    Frame temp{};

    temp.index =
        slot.frameIndex.load(std::memory_order_seq_cst);

    for (std::size_t i = 0; i < FrameSize; ++i) {
        temp.data[i] =
            slot.data[i].load(std::memory_order_seq_cst);
    }

    const auto after =
        slot.version.load(std::memory_order_seq_cst);

    if (before != after)
        return SlotReadStatus::Retry;

    output = temp;
    return SlotReadStatus::Success;
}
// --------------------------------------------------
// Determine the status of a requested frame.
// --------------------------------------------------

inline ReadStatus checkFrameStatus(
    const RingBuffer& ring,
    std::uint64_t requestedIndex,
    Frame& output)
{
    const auto validBefore =
        ring.header.validFromIndex.load(
            std::memory_order_seq_cst);

    if (requestedIndex < validBefore)
        return ReadStatus::Overwritten;

    const auto slotIndex = requestedIndex % RingCapacity;

    Frame temp{};
    const auto slotStatus =
        tryReadSlot(ring.slots[slotIndex], temp);

    const auto validAfter =
        ring.header.validFromIndex.load(
            std::memory_order_seq_cst);

    if (requestedIndex < validAfter)
        return ReadStatus::Overwritten;

    if (slotStatus == SlotReadStatus::Retry)
        return ReadStatus::Retry;

    if (slotStatus == SlotReadStatus::Success) {
        if (temp.index > requestedIndex)
            return ReadStatus::Overwritten;

        if (temp.index == requestedIndex) {
            output = temp;
            return ReadStatus::Success;
        }
    }

    const auto latest =
        ring.header.latestFrameIndex.load(
            std::memory_order_seq_cst);

    if (requestedIndex > latest)
        return ReadStatus::NotYetProduced;

    if (latest - requestedIndex >= RingCapacity)
        return ReadStatus::Overwritten;

    return ReadStatus::Retry;
}

}// namespace


