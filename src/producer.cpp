
#include "shared_memory.hpp"
#include "ring_buffer.hpp"
#include "named_semaphore.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace realtime;
using Clock = std::chrono::steady_clock;

constexpr std::uint64_t FPS = 30;
constexpr auto RunDuration = std::chrono::seconds(10);

// Calculate the frame index from elapsed time.
std::uint64_t calculateFrameIndex(
    Clock::time_point start,
    std::uint64_t fps)
{
    const auto elapsed = Clock::now() - start;

    const auto elapsedNs =
        std::chrono::duration_cast<
            std::chrono::nanoseconds
        >(elapsed).count();

    return 1 + static_cast<std::uint64_t>(
        (static_cast<long double>(elapsedNs) * fps)
        / 1'000'000'000.0L
    );
}

int main()
{
    shm_unlink("/video_ring");
    sem_unlink("/video_ready");

    NamedSemaphore readySemaphore(
        "/video_ready",
        true
    );

    SharedMemory shm(
        "/video_ring",
        sizeof(RingBuffer),
        true
    );

    auto* ring = std::construct_at(
        static_cast<RingBuffer*>(shm.data())
    );

    ring->header.ready.store(
        true,
        std::memory_order_release
    );

    readySemaphore.post();

    std::cout << "Producer is ready.\n";
    std::cout << "Frame rate: " << FPS << " FPS\n";

    const auto start = Clock::now();
    const auto end = start + RunDuration;

    std::uint64_t lastProducedIndex = 0;
    std::uint64_t producedCount = 0;
    std::uint64_t skippedCount = 0;

    while (Clock::now() < end)
    {
        const auto targetIndex =
            calculateFrameIndex(start, FPS);

        // Already produced the frame for this time.
        if (targetIndex <= lastProducedIndex)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1)
            );
            continue;
        }

        // Detect skipped frame indices.
        if (lastProducedIndex != 0 &&
            targetIndex > lastProducedIndex + 1)
        {
            const auto skipped =
                targetIndex - lastProducedIndex - 1;

            skippedCount += skipped;

            std::cout
                << "Skipped " << skipped
                << " frames\n";
        }

        Frame frame{};
        frame.index = targetIndex;

        frame.data.fill(
            std::byte{
                static_cast<unsigned char>(
                    targetIndex % 256
                )
            }
        );

        writeFrame(*ring, frame);

        lastProducedIndex = targetIndex;
        ++producedCount;
    }


    ring->header.producerDone.store(
    true,
    std::memory_order_release
);

    std::cout << "\nProduction finished.\n";
    std::cout << "Frames produced: "
              << producedCount << '\n';
    std::cout << "Frames skipped: "
              << skippedCount << '\n';
    std::cout << "Latest frame index: "
              << lastProducedIndex << '\n';

    std::cout << "Press Enter to exit..."
              << std::flush;

    std::string input;
    std::getline(std::cin, input);

    return 0;
}
