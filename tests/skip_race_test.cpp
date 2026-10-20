#include "ring_buffer.hpp"

#include <atomic>
#include <cassert>
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

    std::atomic<bool> firstReadDone{false};
    std::atomic<bool> skipDone{false};

    std::thread consumer([&] {
        const auto before =
            ring.header.validFromIndex.load(
                std::memory_order_seq_cst);

        assert(before <= 99);

        Frame output{};
        const bool success = readFrame(ring, 99, output);

        assert(success);

        firstReadDone.store(true, std::memory_order_release);

        while (!skipDone.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        const auto after =
            ring.header.validFromIndex.load(
                std::memory_order_seq_cst);

        assert(after == 105);
        assert(99 < after);

        std::cout << "Skip detected during validation!\n";
    });

    std::thread producer([&] {
        while (!firstReadDone.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        Frame next{};
        next.index = 105;
        next.data.fill(std::byte{105});

        writeFrame(ring, next);

        skipDone.store(true, std::memory_order_release);
    });

    consumer.join();
    producer.join();

    std::cout << "Skip race test passed!\n";
}
