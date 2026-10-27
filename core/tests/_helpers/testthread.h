#pragma once

/// @file testthread.h
/// @brief A test's helper thread that is stopped and joined on every exit path. A plain `std::thread` still joinable
/// when an `ASSERT_*` returns from the test body calls `std::terminate` and takes the whole test process down (every
/// later test of the run or shard is lost, BUGS.md 2026-10-11 #3). Here a failed check fails one test:
///
///     std::atomic<bool> done{false};
///     TestThread reader([&] { while (!done) { ... } }, [&] { done = true; });
///     ASSERT_TRUE(...);   // returns early: the destructor sets done and joins
///     reader.Join();      // the normal path, where the test wants the thread finished before its checks

#include <functional>
#include <thread>
#include <utility>

class TestThread
{
public:
    /// @param body what the thread runs
    /// @param stop makes `body` return (sets the loop's flag); called once, before the join
    TestThread(std::function<void()> body, std::function<void()> stop) : _stop(std::move(stop)), _thread(std::move(body))
    {
    }

    /// @param body what the thread runs; it ends by itself (a bounded run, a wait with a timeout), the join waits for it
    explicit TestThread(std::function<void()> body) : TestThread(std::move(body), nullptr)
    {
    }

    TestThread(const TestThread&) = delete;
    TestThread& operator=(const TestThread&) = delete;

    ~TestThread()
    {
        Join();
    }

    /// Stop and join (idempotent)
    void Join()
    {
        if (!_thread.joinable())
            return;
        if (_stop)
            _stop();
        _thread.join();
    }

private:
    std::function<void()> _stop;
    std::thread _thread;
};
