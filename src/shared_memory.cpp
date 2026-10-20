#include "shared_memory.hpp"

#include <cerrno>
#include <system_error>

namespace realtime {

SharedMemory::SharedMemory(
    const std::string& name,
    std::size_t size,
    bool create)
    : size_(size)
{
    const int flags = create
        ? O_CREAT | O_EXCL | O_RDWR
        : O_RDWR;

    fd_ = shm_open(name.c_str(), flags, 0600);

    if (fd_ == -1) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "shm_open failed"
        );
    }

    if (create) {
        if (ftruncate(fd_, static_cast<off_t>(size_)) == -1) {
            const int error = errno;
            close(fd_);

            throw std::system_error(
                error,
                std::generic_category(),
                "ftruncate failed"
            );
        }
    }

    address_ = mmap(
        nullptr,
        size_,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd_,
        0
    );

    if (address_ == MAP_FAILED) {
        const int error = errno;
        address_ = nullptr;
        close(fd_);

        throw std::system_error(
            error,
            std::generic_category(),
            "mmap failed"
        );
    }
}
SharedMemory::~SharedMemory() {
    if (address_ != nullptr) {
        munmap(address_, size_);
    }

    if (fd_ != -1) {
        close(fd_);
    }
}

} // namespace realtime
