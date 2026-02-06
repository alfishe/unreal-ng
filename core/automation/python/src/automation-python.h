#pragma once

#include <thread>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <pybind11/embed.h>

namespace py = pybind11;

class AutomationPython
{
/// region <Fields>
protected:
    std::thread* _thread = nullptr;
    volatile bool _stopThread = false;
    std::atomic<unsigned long> _pythonThreadId{0};

    // Task queue for thread-safe execution
    std::queue<std::function<void()>> _taskQueue;
    std::mutex _queueMutex;
    std::condition_variable _queueCondition;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    AutomationPython() = default;
    virtual ~AutomationPython()
    {
        if (_thread)
        {
            stop();

            delete _thread;
            _thread = nullptr;
        }
    }
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    void start();
    void stop();

    // Remote interpreter control methods
    bool executeCode(const std::string& code, std::string& errorMessage, std::string& capturedOutput);
    bool executeFile(const std::string& path, std::string& errorMessage, std::string& capturedOutput);
    std::string getStatusString() const;

    void processPython();
    bool executePython(const std::string& code);
    void interruptPythonExecution();

    // Thread-safe task dispatch (synchronous execution in Python thread)
    template<typename Func>
    auto dispatchSync(Func&& func) -> decltype(func()) {
        using ReturnType = decltype(func());
        auto promise = std::make_shared<std::promise<ReturnType>>();
        auto future = promise->get_future();

        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _taskQueue.push([promise, f = std::forward<Func>(func)]() {
                try {
                    if constexpr (std::is_void_v<ReturnType>) {
                        f();
                        promise->set_value();
                    } else {
                        promise->set_value(f());
                    }
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            });
        }
        _queueCondition.notify_one();

        // Wait for result with timeout
        if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
            throw std::runtime_error("Python dispatch timeout");
        }

        return future.get();
    }
    /// endregion </Methods>

protected:
    static void threadFunc(AutomationPython* python);
};
