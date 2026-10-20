

#include "shared_memory.hpp"
#include "ring_buffer.hpp"
#include "named_semaphore.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>

using namespace realtime;

enum class WaitResult {
    Success,
    Overwritten,
    Timeout,
    ProducerFinished
};

// Wait for a frame with a timeout.
// The producer is never blocked.

WaitResult waitForFrame(
    const RingBuffer& ring,
    std::uint64_t requestedIndex,
    Frame& output,
    std::chrono::milliseconds timeout)
{
    using namespace std::chrono;

    const auto deadline = steady_clock::now() + timeout;

    while (true) {
        const auto status =
            checkFrameStatus(ring, requestedIndex, output);

        if (status == ReadStatus::Success)
            return WaitResult::Success;

        if (status == ReadStatus::Overwritten)
            return WaitResult::Overwritten;

        if (status == ReadStatus::NotYetProduced) {
            if (ring.header.producerDone.load(
                    std::memory_order_acquire)) {
                return WaitResult::ProducerFinished;
            }

            if (steady_clock::now() >= deadline)
                return WaitResult::Timeout;

            std::this_thread::sleep_for(milliseconds(1));
            continue;
        }

        // Retry: a concurrent write may be in progress.
        // Retry promptly, but don't spin forever.
        if (steady_clock::now() >= deadline)
            return WaitResult::Timeout;

        std::this_thread::yield();
    }
}

int main()
{
    // Open existing named semaphore.
    NamedSemaphore readySemaphore(
        "/video_ready",
        false
    );

    // Open existing shared memory.
    SharedMemory shm(
        "/video_ring",
        sizeof(RingBuffer),
        false
    );

    auto* ring = static_cast<RingBuffer*>(shm.data());

    // The one-shot semaphore wait remains disabled.
    // readySemaphore.wait();

    if (!ring->header.ready.load(
            std::memory_order_acquire)) {
        std::cerr << "RingBuffer is not ready\n";
        return 1;
    }

    std::cout << "RingBuffer is ready!\n";
    std::cout << "Consumer connected\n";

    const auto latest =
        ring->header.latestFrameIndex.load(
            std::memory_order_acquire
        );

    std::cout << "Latest frame index: "
              << latest << '\n';

    // Frames to test.
    const std::uint64_t requestedFrames[] = {290};


    for (const auto index : requestedFrames)
    {
        Frame frame{};

        const auto status = waitForFrame(
            *ring,
            index,
            frame,
            std::chrono::milliseconds(100)
        );

        std::cout << "Frame " << index << ": ";

        switch (status) {
            case WaitResult::Success:
                std::cout << "Success, index = "
                        << frame.index << '\n';
                break;
            case WaitResult::Overwritten:
                std::cout << "Overwritten\n";
                break;

            case WaitResult::Timeout:
                std::cout << "Timeout\n";
                break;

            case WaitResult::ProducerFinished:
                std::cout << "ProducerFinished\n";
                break;
        }
    }

    return 0;
}
