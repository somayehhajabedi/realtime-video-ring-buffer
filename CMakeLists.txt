cmake_minimum_required(VERSION 3.20)

project(RealtimeVideoRingBuffer
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(realtime_core STATIC
    src/shared_memory.cpp
    src/named_semaphore.cpp
)

target_include_directories(realtime_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(realtime_core
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(producer
    src/producer.cpp
)

target_link_libraries(producer
    PRIVATE realtime_core
)

add_executable(consumer
    src/consumer.cpp
)

target_link_libraries(consumer
    PRIVATE realtime_core
)

enable_testing()

add_executable(concurrent_read_write_test
    tests/concurrent_read_write_test.cpp
)

target_link_libraries(concurrent_read_write_test
    PRIVATE realtime_core
)

add_test(
    NAME ConcurrentReadWriteTest
    COMMAND concurrent_read_write_test
)
add_executable(concurrent_stress_test
    tests/concurrent_stress_test.cpp
)

target_link_libraries(concurrent_stress_test
    PRIVATE realtime_core
)

add_test(
    NAME ConcurrentStressTest
    COMMAND concurrent_stress_test
)
