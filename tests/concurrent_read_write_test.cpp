#include "ring_buffer.hpp"

#include <cassert>
#include <iostream>

int main()
{
    using namespace realtime;

    RingBuffer ring{};

    Frame frame{};
    frame.index = 1;
    frame.data.fill(std::byte{42});

    writeFrame(ring, frame);

    // Simulate a writer that has started updating the slot.
    Slot& slot = ring.slots[1];

    slot.version.fetch_add(
        1,
        std::memory_order_seq_cst
    );

    Frame output{};

    const bool success = readFrame(ring, 1, output);

    assert(!success);

    std::cout << "Concurrent write detected!\n";
}
