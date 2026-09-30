#pragma once

/// @file threadpool.h
/// @brief A fixed set of worker threads that stay alive between jobs.
///
/// Two ways to use it:
///   Submit(f)             - run f on a worker, get its result through a future
///   ParallelFor(n, k, f)  - split [0, n) into k contiguous ranges, run
///                           f(begin, end) for each (the caller works too), return
///                           when all are done
///
/// Starting threads for every parallel stage costs time the pool does not
/// (ZX DLSS runs several stages per emulated frame, ~50 times a second); on a
/// loaded machine the start-up latency of new threads was a large share of it.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

class ThreadPool
{
public:
    enum class Priority
    {
        Normal,       ///< the platform's default scheduling
        Interactive,  ///< ThreadHelper::setInteractivePriority - the UI's level (macOS QoS user-interactive), never real-time
    };

    /// @param threads worker threads (0 = none: everything runs on the caller)
    /// @param name    thread name prefix (debuggers, profilers): "<name>-<index>"
    explicit ThreadPool(size_t threads, const std::string& name = "pool", Priority priority = Priority::Normal);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Worker threads (the caller of ParallelFor is one more)
    size_t Size() const { return _workers.size(); }

    /// Run f() on a worker thread
    template <class F>
    auto Submit(F&& f) -> std::future<std::invoke_result_t<F>>
    {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> result = task->get_future();
        Enqueue([task] { (*task)(); });
        return result;
    }

    /// f(begin, end) for [0, n) split into `chunks` contiguous ranges of
    /// ceil(n / chunks) (the last one shorter): the same ranges whatever the
    /// number of threads, so results that depend on the split do not change.
    /// The caller runs ranges too; returns when every range is done.
    void ParallelFor(int n, int chunks, const std::function<void(int, int)>& f);

private:
    void Enqueue(std::function<void()> task);
    void WorkerLoop(size_t index);

    std::string _name;
    Priority _priority;
    std::vector<std::thread> _workers;
    std::deque<std::function<void()>> _tasks;
    std::mutex _mutex;
    std::condition_variable _wake;
    bool _stop = false;
};
