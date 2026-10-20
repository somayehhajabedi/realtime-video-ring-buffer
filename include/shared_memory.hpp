#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
namespace realtime {

class SharedMemory {
public:
     SharedMemory(const std::string& name,
             std::size_t size,
             bool create);
    ~SharedMemory();

    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&&) = delete;
    SharedMemory& operator=(SharedMemory&&) = delete;

    [[nodiscard]] void* data() const noexcept {
        return address_;
    }

private:
    int fd_{-1};
    void* address_{nullptr};
    std::size_t size_{0};
};

} // namespace realtime
