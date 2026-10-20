
#include "ring_buffer.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <thread>

using namespace realtime;

int main()
{
    RingBuffer ring{};

    Frame frame{};
    frame.index = 99;
    frame.data.fill(std::byte{99});
    writeFrame(ring, frame);

    std::atomic<bool> frameRead{false};
    std::atomic<bool> skipPublished{false};

    std::thread consumer([&] {
        Frame output{};

        // Step 1: Read the old frame.
        assert(readFrame(ring, 99, output));
        frameRead.store(true, std::memory_order_release);

        // Step 3: Wait for invalidation.
        while (!skipPublished.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        // Step 4: Validate again after the skip.
        const auto status = checkFrameStatus(ring, 99, output);

        assert(status == ReadStatus::Overwritten);
    });

    std::thread producer([&] {
        // Step 2: Wait until consumer has read frame 99.
        while (!frameRead.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        // Skip from frame 99 to frame 105.
        Frame next{};
        next.index = 105;
        next.data.fill(std::byte{105});
        writeFrame(ring, next);

        skipPublished.store(true, std::memory_order_release);
    });

    consumer.join();
    producer.join();

    std::cout << "Concurrent skip test passed!\n";
}

