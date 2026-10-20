#include "ring_buffer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>

int main()
{
    using namespace realtime;

    RingBuffer ring{};

    constexpr std::uint64_t TotalFrames = 10000;

    std::atomic<bool> producerDone{false};

    std::uint64_t successfulReads = 0;
    std::uint64_t overwrittenFrames = 0;
    std::uint64_t retries = 0;
    std::uint64_t corruptedFrames = 0;

    std::thread producer([&]()
    {
        for (std::uint64_t index = 1; index <= TotalFrames; ++index)
        {
            Frame frame{};
            frame.index = index;

            frame.data.fill(
                std::byte{
                    static_cast<unsigned char>(index % 256)
                }
            );

            writeFrame(ring, frame);
        }

        producerDone.store(
            true,
            std::memory_order_release
        );
    });

    std::thread consumer([&]()
    {
        std::uint64_t requestedIndex = 1;

        while (requestedIndex <= TotalFrames)
        {
            Frame frame{};

            const auto status =
                checkFrameStatus(ring, requestedIndex, frame);

            switch (status)
            {
                case ReadStatus::Success:
                {
                    const auto expected = std::byte{
                        static_cast<unsigned char>(
                            requestedIndex % 256
                        )
                    };

                    bool valid = frame.index == requestedIndex;

                    for (const auto byte : frame.data)
                    {
                        if (byte != expected)
                        {
                            valid = false;
                            break;
                        }
                    }

                    if (!valid)
                        ++corruptedFrames;
                    else
                        ++successfulReads;

                    ++requestedIndex;
                    break;
                }

                case ReadStatus::Overwritten:
                    ++overwrittenFrames;
                    ++requestedIndex;
                    break;

                case ReadStatus::NotYetProduced:
                    std::this_thread::yield();
                    break;

                case ReadStatus::Retry:
                    ++retries;
                    std::this_thread::yield();
                    break;
            }
        }
    });

    producer.join();
    consumer.join();

    std::cout << "Successful reads: "
              << successfulReads << '\n';

    std::cout << "Overwritten frames: "
              << overwrittenFrames << '\n';

    std::cout << "Retries: "
              << retries << '\n';

    std::cout << "Corrupted frames: "
              << corruptedFrames << '\n';

    if (corruptedFrames != 0)
    {
        std::cerr << "TEST FAILED: Data corruption detected\n";
        return 1;
    }

    if (successfulReads == 0)
    {
        std::cerr << "TEST FAILED: No frames were read\n";
        return 1;
    }

    std::cout << "Concurrent stress test PASSED\n";

    return 0;
}