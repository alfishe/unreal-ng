#pragma once

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

/// A fixed set of worker threads for the ZX-Poly slaves: one per slave, kept
/// for the life of the group. The group posts each slave's frame work to its
/// worker and waits for all of them (at once, or one frame later while the
/// slaves are pipelined). Saves starting three threads every frame.
class ZXPolyWorkers
{
public:
    explicit ZXPolyWorkers(size_t count);
    ~ZXPolyWorkers();

    ZXPolyWorkers(const ZXPolyWorkers&) = delete;
    ZXPolyWorkers& operator=(const ZXPolyWorkers&) = delete;

    /// Hands `job` to worker `index`; waits first if that worker is still busy
    void Post(size_t index, std::function<void()> job);

    /// Returns once every worker is idle. Rethrows the first exception a job
    /// threw since the last wait. Never call it from a job
    void WaitAll();

    /// True while any worker has a job
    bool IsBusy();

private:
    void WorkerLoop(size_t index);

    struct Worker
    {
        std::thread thread;
        std::function<void()> job;
        bool busy = false;
    };

    std::mutex _mutex;
    std::condition_variable _wake;    // a job was posted (or stop)
    std::condition_variable _idle;    // a worker finished its job
    std::vector<Worker> _workers;
    std::exception_ptr _failure;
    bool _stop = false;
};
