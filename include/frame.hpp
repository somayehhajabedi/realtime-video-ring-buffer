#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace realtime {

constexpr std::size_t FrameSize = 4096;

struct Frame {
    std::uint64_t index{};
    std::array<std::byte, FrameSize> data{};
};

} // namespace realtime
