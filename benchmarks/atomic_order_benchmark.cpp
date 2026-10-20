
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>

constexpr std::size_t FrameSize = 4096;
constexpr int Iterations = 10000;

template<std::memory_order Order>
void benchmark(const char* name)
{
    std::array<std::atomic<std::byte>, FrameSize> data{};

    const auto start = std::chrono::steady_clock::now();

    for (int n = 0; n < Iterations; ++n)
    {
        for (std::size_t i = 0; i < FrameSize; ++i)
        {
            data[i].store(
                std::byte{static_cast<unsigned char>(n)},
                Order
            );
        }
    }

    const auto end = std::chrono::steady_clock::now();

    const auto elapsed =
        std::chrono::duration_cast<
            std::chrono::microseconds
        >(end - start);

    std::cout << name << ": "
              << elapsed.count()
              << " us\n";
}

int main()
{
    benchmark<std::memory_order_relaxed>("Relaxed");
    benchmark<std::memory_order_seq_cst>("SeqCst");
}

