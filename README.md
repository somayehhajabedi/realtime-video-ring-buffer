# Real-Time Video Ring Buffer

A C++20 implementation of a real-time video frame ring buffer using POSIX shared memory for low-latency communication between independent producer and consumer processes.

## Features

- Fixed-capacity, shared-memory ring buffer
- Single-producer, single-consumer architecture
- Non-blocking frame production
- Concurrent read/write detection using atomic version counters
- Frame overwrite detection
- Clock-based frame generation and skipped-frame invalidation
- Consumer support for requesting frames by index
- RAII wrappers for POSIX shared memory and named semaphores
- Concurrent and stress tests using CTest

## Architecture

The **Producer** creates and initializes the shared memory region, generates frames according to a monotonic clock, and writes them into a fixed-size ring buffer.

The **Consumer** maps the existing shared memory, requests frames by index, and checks whether each frame is available, overwritten, not yet produced, or being modified concurrently.

The producer never waits for the consumer.

## Technical Details

| Component | Implementation |
|---|---|
| Language | C++20 |
| Platform | Linux |
| IPC | POSIX shared memory (`shm_open`, `mmap`) |
| Synchronization | `std::atomic`, version counters |
| Resource management | RAII |
| Build system | CMake |
| Ring capacity | 64 frames |
| Frame payload | 4096 bytes |

Each ring-buffer slot maintains a version counter. The producer marks the slot as being updated before writing frame data and marks it complete afterward. The consumer checks the version before and after reading to detect concurrent modifications.

Frame data is stored using atomic bytes to avoid unsynchronized concurrent access.

## Build

Requirements: Linux, CMake 3.20 or later, and a C++20-compatible compiler.

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

Open two terminals.

**Terminal 1 — Producer**

```bash
./build/producer
```

**Terminal 2 — Consumer**

```bash
./build/consumer
```

The producer creates the shared memory region. The consumer attaches to it and attempts to read the requested frame indices.

## Testing

Run all registered tests:

```bash
ctest --test-dir build --output-on-failure
```

The test suite includes:

| Test | Purpose |
|---|---|
| ConcurrentReadWriteTest | Concurrent producer/consumer access |
| ConcurrentStressTest | Repeated concurrent frame operations |
| ConcurrentSkipTest | Concurrent behavior during skipped frames |
| SkipFrameTest | Frame skipping and validity handling |
| SkipRaceTest | Race scenarios involving skipped frames |

**Latest local result:** 5/5 tests passed.

## Project Structure

```text
realtime-video-ring-buffer/
├── include/       # Frame, ring buffer, and IPC interfaces
├── src/           # Producer, consumer, and IPC implementations
├── tests/         # Concurrency and frame-handling tests
├── benchmarks/    # Atomic memory-ordering benchmark
├── CMakeLists.txt
└── README.md
```

## Limitations

This is an educational and experimental implementation targeting Linux. Its cross-process atomic behavior depends on the platform implementation. Passing the included tests does not establish formal lock-free correctness or production readiness.

## Topics Demonstrated

Modern C++20, POSIX IPC, shared memory, atomic operations, memory ordering, concurrency, RAII, and real-time producer-consumer design.

