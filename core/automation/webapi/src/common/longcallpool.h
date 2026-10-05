#pragma once

// Long run-control calls off the HTTP workers (debugger additions tdd §5, finding F4). The server has two drogon
// worker threads; a /skip_until of 700 million T-states on one of them stalled every other request. A long handler
// hands its work to this pool and returns; the pool thread answers through drogon's callback (drogon allows that
// from any thread), so the reply is the same as before and the workers stay free.

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class LongCallPool
{
public:
    static LongCallPool& Instance()
    {
        static LongCallPool pool;
        return pool;
    }

    /// Queue `work`; one of the pool threads runs it (a fifth concurrent call waits for a free thread)
    void Run(std::function<void()> work)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _queue.push_back(std::move(work));
        }
        _ready.notify_one();
    }

    ~LongCallPool()
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stopping = true;
        }
        _ready.notify_all();
        for (std::thread& thread : _threads)
            thread.join();
    }

    LongCallPool(const LongCallPool&) = delete;
    LongCallPool& operator=(const LongCallPool&) = delete;

private:
    static constexpr int kThreads = 4;

    LongCallPool()
    {
        for (int i = 0; i < kThreads; ++i)
            _threads.emplace_back([this]() { Loop(); });
    }

    void Loop()
    {
        for (;;)
        {
            std::function<void()> work;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _ready.wait(lock, [this]() { return _stopping || !_queue.empty(); });
                if (_queue.empty())
                    return;  // stopping and nothing left
                work = std::move(_queue.front());
                _queue.pop_front();
            }
            work();
        }
    }

    std::mutex _mutex;
    std::condition_variable _ready;
    std::deque<std::function<void()>> _queue;
    std::vector<std::thread> _threads;
    bool _stopping = false;
};
