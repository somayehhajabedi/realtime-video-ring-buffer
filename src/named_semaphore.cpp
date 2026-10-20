#include "named_semaphore.hpp"

#include <cerrno>
#include <fcntl.h>
#include <system_error>

namespace realtime {

NamedSemaphore::NamedSemaphore(
    const std::string& name,
    bool create)
{
    const int flags = create
        ? O_CREAT | O_EXCL
        : 0;

    sem_ = sem_open(
        name.c_str(),
        flags,
        0600,
        0
    );

    if (sem_ == SEM_FAILED) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "sem_open failed"
        );
    }
}

void NamedSemaphore::post() {
    if (sem_post(sem_) == -1) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "sem_post failed"
        );
    }
}

void NamedSemaphore::wait() {
    while (sem_wait(sem_) == -1) {
        if (errno == EINTR)
            continue;

        throw std::system_error(
            errno,
            std::generic_category(),
            "sem_wait failed"
        );
    }
}

NamedSemaphore::~NamedSemaphore() {
    if (sem_ != SEM_FAILED) {
        sem_close(sem_);
    }
}

} // namespace realtime