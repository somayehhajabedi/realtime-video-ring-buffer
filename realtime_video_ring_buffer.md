# Realtime Video Ring Buffer — Technical Design & Progress

> **Status:** Work in progress (C++20 / Linux). This document describes the implementation discussed and tested so far, not a production-ready system.

## 1. Goal

Build a two-process video-frame pipeline:

- **Producer:** generates frames and writes them to a bounded circular buffer without waiting for the consumer.
- **Consumer:** requests a frame by its monotonically increasing index.
- **Transport:** POSIX shared memory mapped into both processes.
- **Overwrite policy:** when the buffer wraps, a newer frame replaces an older frame in the same slot.
- **Safety goal:** detect a slot changing while it is being read, and distinguish future frames from overwritten frames.

Synthetic frame payloads are used during development; no real video capture/decoding is implemented yet.

## 2. Project layout

```text
realtime-video-ring-buffer/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── frame.hpp
│   ├── ring_buffer.hpp
│   ├── shared_memory.hpp
│   └── named_semaphore.hpp
├── src/
│   ├── shared_memory.cpp
│   ├── named_semaphore.cpp
│   ├── producer.cpp
│   └── consumer.cpp
├── tests/
├── benchmarks/
└── build/
```

## 3. Architecture

```text
┌───────────────────────┐           ┌────────────────────────┐
│ Producer process      │           │ Consumer process       │
│ Frame(index, payload) │           │ readFrame(requestedId) │
└───────────┬───────────┘           └────────────┬───────────┘
            │ write                             │ read
            ▼                                   ▼
      ┌─────────────────────────────────────────────┐
      │ POSIX shared memory: /video_ring            │
      │ SharedHeader                                │
      │  - ready                                    │
      │  - latestFrameIndex                         │
      │ RingBuffer: 64 Slots                        │
      │  - version                                  │
      │  - frameIndex                               │
      │  - data[4096]                               │
      └─────────────────────────────────────────────┘

Startup semaphore: /video_ready (current approach needs redesign).
```

## 4. Data structures

### Frame (`include/frame.hpp`)

```cpp
constexpr std::size_t FrameSize = 4096;

struct Frame {
    std::uint64_t index{};
    std::array<std::byte, FrameSize> data{};
};
```

- `index` identifies the frame; `data` holds 4096 bytes (4 KiB) of synthetic payload.
- `sizeof(Frame)` is typically 4104 bytes on the current target, but verify with `sizeof` for each platform.

### Slot and RingBuffer (`include/ring_buffer.hpp`)

```cpp
struct Slot {
    std::atomic<std::uint64_t> version{0};
    std::atomic<std::uint64_t> frameIndex{0};
    std::array<std::atomic<std::byte>, FrameSize> data{};
};

constexpr std::size_t RingCapacity = 64;

struct SharedHeader {
    std::atomic<bool> ready{false};
    std::atomic<std::uint64_t> latestFrameIndex{0};
};

struct RingBuffer {
    SharedHeader header;
    std::array<Slot, RingCapacity> slots{};
};
```

The mapping from frame index to slot is:

```cpp
slotIndex = frame.index % RingCapacity;
```

For example, frames 1 and 65 both map to slot 1. After writing frames 1 through 100000 sequentially, only frames **99937 through 100000** remain in the 64-slot buffer.

**Memory use:** the payload alone occupies 64 × 4096 = 262144 bytes (256 KiB). The actual shared-memory object is larger because of slot metadata, atomics, and alignment.

## 5. Writing a frame

The current design writes a slot with a version counter:

```cpp
inline void writeFrame(Slot& slot, const Frame& frame) {
    slot.version.fetch_add(1, std::memory_order_seq_cst); // odd: write underway

    for (std::size_t i = 0; i < FrameSize; ++i)
        slot.data[i].store(frame.data[i], std::memory_order_seq_cst);

    slot.frameIndex.store(frame.index, std::memory_order_seq_cst);
    slot.version.fetch_add(1, std::memory_order_seq_cst); // even: write complete
}
```

The ring-level function writes the slot, **then** publishes the last completed frame index:

```cpp
inline void writeFrame(RingBuffer& ring, const Frame& frame) {
    const auto slotIndex = frame.index % RingCapacity;
    writeFrame(ring.slots[slotIndex], frame);
    ring.header.latestFrameIndex.store(frame.index, std::memory_order_release);
}
```

The producer is a single writer. This is important: multiple concurrent writers would require additional coordination for the version protocol.

## 6. Reading a frame

The current slot read uses a version check:

1. Read `versionBefore`.
2. Reject an unwritten slot (`version == 0`) or an odd version (write in progress).
3. Copy the frame index and payload.
4. Read `versionAfter`.
5. Reject if the version changed.
6. At ring level, verify that the stored index equals the requested index.

The ring-level API currently returns `bool`. `false` may mean:

- The requested frame has **not been produced yet**.
- The frame was **overwritten**.
- The slot was **being updated during the read**.

The next planned API should return a richer status:

```cpp
enum class ReadStatus {
    Success,
    NotYetProduced,
    Overwritten,
    Retry
};
```

**Important:** the enum is planned; it has not yet been confirmed as integrated into the implementation.

`latestFrameIndex` helps classify cases, but checking it alone is not enough: the producer can advance while the consumer reads. The version and frame index must still be checked.

## 7. Shared memory and process startup

`SharedMemory` is an RAII wrapper around:

- `shm_open` — create/open a named POSIX shared-memory object.
- `ftruncate` — size it when created.
- `mmap(..., MAP_SHARED, ...)` — map the same backing storage into both processes.
- `munmap` and `close` — release mapping and descriptor in the destructor.

The producer constructs the shared `RingBuffer` using `std::construct_at` and publishes `header.ready = true` with release ordering. The consumer checks `ready` with acquire ordering.

A `NamedSemaphore` wrapper uses `sem_open`, `sem_post`, `sem_wait`, and `sem_close`. Its wait retries on `EINTR`.

### Known startup synchronization issue

The producer posts `/video_ready` **once**. Each `sem_wait` consumes a semaphore token. Therefore, a second consumer invocation can block indefinitely even though the shared memory is already initialized. During testing, `readySemaphore.wait()` was **temporarily commented out** and the producer was started first.

This is **not** a finished startup protocol. A robust design must also handle consumer-before-producer startup, restarts, crashes, and stale shared-memory objects. The current producer calls `shm_unlink` and `sem_unlink` on startup for development convenience; doing this unconditionally is unsafe for multiple instances or restarts.

## 8. Synthetic producer workload

The producer generates 100000 frames:

```cpp
for (std::uint64_t index = 1; index <= 100000; ++index) {
    Frame frame{};
    frame.index = index;
    frame.data.fill(std::byte{static_cast<unsigned char>(index % 256)});
    writeFrame(*ring, frame);
}
```

`fill()` sets **all 4096 bytes of one frame** to the same test value. For frame 100000, the expected byte value is `100000 % 256 = 160`.

For concurrency experiments, a temporary `sleep_for(1ms)` after each frame was discussed/used. It slows production and is **not** part of the final low-latency design.

## 9. Observed test results

| Test | Observed outcome | Interpretation |
|---|---|---|
| Write frame 1, then frame 65 | Frame 1 unavailable; frame 65 readable, first byte 85 | Both map to slot 1; overwrite behavior observed |
| Produce 100000 frames; read frame 100000 | Index 100000, first byte 160 | Cross-process shared-memory read succeeded |
| Consumer requests frames 100–1000 after completion | All unavailable | Those old frames have been overwritten |
| Consumer reads `latestFrameIndex` during production | `12817`; frames 99997–100000 unavailable | Those requested frames were still in the future |
| Consumer reads after production | `latestFrameIndex = 100000`; frames 99997–100000 found | Header publication and recent-frame reads succeeded |
| Re-run consumer with one-shot semaphore wait | Consumer could block | One-shot semaphore token had been consumed |
| Temporarily skip semaphore wait | Four most recent frames found | Confirms the earlier blocking issue was in startup synchronization |

These are manual functional tests, **not** proof of race freedom or a stress-tested implementation.

## 10. Build and run

From the project root:

```bash
cmake -S . -B build
cmake --build build -j
```

In terminal 1:

```bash
./build/producer
```

Wait for `100000 frames written` and keep the producer alive at its `Press Enter to exit...` prompt.

In terminal 2:

```bash
./build/consumer
```

For the latest completed-producer test, the consumer is expected to print:

```text
RingBuffer is ready!
Consumer connected
Latest frame index: 100000
Frame 99997 found
Frame 99998 found
Frame 99999 found
Frame 100000 found
```

**Caution:** the startup semaphore workaround is temporary; running the consumer repeatedly with `readySemaphore.wait()` enabled may block.

## 11. Outstanding work

1. **Read status:** implement `Success`, `NotYetProduced`, `Overwritten`, and `Retry` with race-aware classification.
2. **Future-frame waiting:** block only when the requested frame is in the future; avoid busy waiting and avoid blocking the producer.
3. **Overwrite-during-read stress tests:** run producer/consumer concurrently, deliberately force wraparound, and verify no inconsistent frame is accepted.
4. **Payload verification:** validate every byte against `frame.index % 256`, not only the first byte.
5. **Startup/lifecycle:** replace the one-shot semaphore readiness scheme; handle restart, cleanup, stale objects, and process termination.
6. **Timing model:** add monotonic-clock frame scheduling and support skipping obsolete frame indices when behind schedule.
7. **Metadata:** add real video metadata (e.g., timestamp, dimensions, pixel format, payload length) as needed.
8. **Performance:** benchmark throughput, p50/p95/p99 latency, CPU use, and the overhead of 4096 atomic byte stores per frame.
9. **Portability/correctness review:** verify cross-process `std::atomic` behavior on target Linux/compiler/architecture; ISO C++ alone does not guarantee interprocess atomic synchronization. Confirm object lifetime, alignment, and lock-free assumptions.
10. **Automated tests:** add unit, integration, concurrency, and restart tests under `tests/`.

## 12. Key design constraints

- Producer must **never wait for consumer progress**.
- There is **one producer** and **one consumer process** in the initial design.
- Requested frame indices are monotonic identifiers; the consumer may request arbitrary indices.
- Old data may be overwritten by design; consumers must be told whether a requested frame is available.
- Detecting concurrent overwrite matters more than returning a stale or partially updated payload.

---

*Documentation snapshot: October 2026. Based on the code and terminal results shared during development. Update as implementation evolves.*
