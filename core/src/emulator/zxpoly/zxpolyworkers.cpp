#include "zxpolyworkers.h"

#include <utility>

ZXPolyWorkers::ZXPolyWorkers(size_t count) : _workers(count)
{
    for (size_t i = 0; i < count; i++)
        _workers[i].thread = std::thread([this, i]() { WorkerLoop(i); });
}

ZXPolyWorkers::~ZXPolyWorkers()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
    }
    _wake.notify_all();
    for (Worker& worker : _workers)
    {
        if (worker.thread.joinable())
            worker.thread.join();
    }
}

void ZXPolyWorkers::Post(size_t index, std::function<void()> job)
{
    {
        std::unique_lock<std::mutex> lock(_mutex);
        Worker& worker = _workers[index];
        _idle.wait(lock, [&worker]() { return !worker.busy; });
        worker.job = std::move(job);
        worker.busy = true;
    }
    _wake.notify_all();
}

void ZXPolyWorkers::WaitAll()
{
    std::unique_lock<std::mutex> lock(_mutex);
    _idle.wait(lock, [this]() {
        for (const Worker& worker : _workers)
        {
            if (worker.busy)
                return false;
        }
        return true;
    });
    if (_failure)
    {
        std::exception_ptr failure = std::exchange(_failure, nullptr);
        lock.unlock();
        std::rethrow_exception(failure);
    }
}

bool ZXPolyWorkers::IsBusy()
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const Worker& worker : _workers)
    {
        if (worker.busy)
            return true;
    }
    return false;
}

void ZXPolyWorkers::WorkerLoop(size_t index)
{
    Worker& worker = _workers[index];
    std::unique_lock<std::mutex> lock(_mutex);
    while (true)
    {
        _wake.wait(lock, [this, &worker]() { return _stop || worker.busy; });
        if (_stop)
            return;

        std::function<void()> job = std::move(worker.job);
        lock.unlock();
        std::exception_ptr failure;
        try
        {
            job();
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        lock.lock();

        if (failure && !_failure)
            _failure = failure;
        worker.busy = false;
        _idle.notify_all();
    }
}
