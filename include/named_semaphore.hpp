#pragma once

#include <semaphore.h>
#include <string>

namespace realtime {

class NamedSemaphore {
public:
    NamedSemaphore(const std::string& name, bool create);

    ~NamedSemaphore();

    NamedSemaphore(const NamedSemaphore&) = delete;
    NamedSemaphore& operator=(const NamedSemaphore&) = delete;

    NamedSemaphore(NamedSemaphore&&) = delete;
    NamedSemaphore& operator=(NamedSemaphore&&) = delete;

    void post();
    void wait();

private:
    sem_t* sem_{SEM_FAILED};
};

} // namespace realtime