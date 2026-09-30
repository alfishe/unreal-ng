#include "threadpool.h"

#include <algorithm>

#include "threadhelper.h"

ThreadPool::ThreadPool(size_t threads, const std::string& name, Priority priority)
    : _name(name), _priority(priority)
{
    _workers.reserve(threads);
    for (size_t i = 0; i < threads; ++i)
        _workers.emplace_back(&ThreadPool::WorkerLoop, this, i);
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
    }
    _wake.notify_all();
    for (std::thread& t : _workers)
        t.join();
}

void ThreadPool::Enqueue(std::function<void()> task)
{
    if (_workers.empty())
    {
        task();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _tasks.push_back(std::move(task));
    }
    _wake.notify_one();
}

void ThreadPool::WorkerLoop(size_t index)
{
    const std::string threadName = _name + "-" + std::to_string(index);
    ThreadHelper::setThreadName(threadName.c_str());
    if (_priority == Priority::Interactive)
        ThreadHelper::setInteractivePriority();

    for (;;)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _wake.wait(lock, [this] { return _stop || !_tasks.empty(); });
            if (_tasks.empty())
                return;  // stopping, nothing left
            task = std::move(_tasks.front());
            _tasks.pop_front();
        }
        task();
    }
}

void ThreadPool::ParallelFor(int n, int chunks, const std::function<void(int, int)>& f)
{
    if (n <= 0)
        return;
    chunks = std::clamp(chunks, 1, n);
    const int size = (n + chunks - 1) / chunks;
    chunks = (n + size - 1) / size;  // ranges that are not empty
    if (chunks == 1 || _workers.empty())
    {
        for (int c = 0; c < chunks; ++c)
            f(c * size, std::min(n, (c + 1) * size));
        return;
    }

    // Ranges are claimed in order by whoever is free (workers and the caller);
    // the caller waits for the last one to finish
    struct Shared
    {
        std::atomic<int> next{0};
        std::atomic<int> left{0};
        std::mutex mutex;
        std::condition_variable done;
    };
    auto shared = std::make_shared<Shared>();
    shared->left.store(chunks, std::memory_order_relaxed);
    auto run = [shared, &f, n, size, chunks] {
        for (int c = shared->next.fetch_add(1); c < chunks; c = shared->next.fetch_add(1))
        {
            f(c * size, std::min(n, (c + 1) * size));
            if (shared->left.fetch_sub(1) == 1)
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                shared->done.notify_all();
            }
        }
    };
    const int helpers = std::min<int>(chunks - 1, static_cast<int>(_workers.size()));
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (int i = 0; i < helpers; ++i)
            _tasks.emplace_back(run);
    }
    if (helpers == 1)
        _wake.notify_one();
    else
        _wake.notify_all();
    run();
    std::unique_lock<std::mutex> lock(shared->mutex);
    shared->done.wait(lock, [&] { return shared->left.load() == 0; });
}
