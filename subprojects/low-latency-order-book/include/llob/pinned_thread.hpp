#pragma once
#include <thread>
#include <pthread.h>
#include <cassert>

namespace llob
{

// Wraps std::thread with pthread_setaffinity_np.
// Asserts on startup that affinity was successfully set.

class PinnedThread
{
public:
    PinnedThread(int core_id, std::invocable auto&& fn) {
        thread_ = std::thread([core_id, fn = std::forward<decltype(fn)>(fn)]() {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(core_id, &cpuset);
            [[maybe_unused]] auto ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
            assert(ret == 0);
            fn();
        });
    }

    PinnedThread(const PinnedThread& o) = delete;
    PinnedThread(PinnedThread&& o) = default;
    PinnedThread& operator=(PinnedThread&& o) = default;

    ~PinnedThread() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
private:
    std::thread thread_;
};

} // namespace llob
