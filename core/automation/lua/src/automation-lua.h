#pragma once

#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <functional>
#include <future>
#include <sol/sol.hpp>
#include "emulator/lua_emulator.h"

class AutomationLua
{
    /// region <Fields>
protected:
    std::thread* _thread = nullptr;
    volatile bool _stopThread = false;

    sol::state* _lua = nullptr;
    LuaEmulator* _luaEmulator = nullptr;
    
    // Task queue for thread-safe execution
    std::queue<std::function<void()>> _taskQueue;
    std::mutex _queueMutex;
    std::condition_variable _queueCV;
    
    // Initialization synchronization
    std::mutex _initMutex;
    std::condition_variable _initCV;
    bool _initialized = false;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    AutomationLua() = default;
    virtual ~AutomationLua()
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
    
    // Query a Lua expression and return its value as a string
    template<typename T = std::string>
    T eval(const std::string& expression) {
        return dispatchSync([expression](sol::state& lua) -> T {
            return lua[expression];
        });
    }
    
    // Thread-safe task dispatch (synchronous execution in Lua thread)
    template<typename Func>
    auto dispatchSync(Func&& func) -> decltype(func(std::declval<sol::state&>())) {
        using ReturnType = decltype(func(std::declval<sol::state&>()));
        auto promise = std::make_shared<std::promise<ReturnType>>();
        auto future = promise->get_future();
        
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _taskQueue.push([this, promise, f = std::forward<Func>(func)]() {
                try {
                    if constexpr (std::is_void_v<ReturnType>) {
                        f(*_lua);
                        promise->set_value();
                    } else {
                        promise->set_value(f(*_lua));
                    }
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            });
        }
        
        _queueCV.notify_one();
        return future.get(); // Wait for result
    }
    /// endregion </Methods>

    /// region <Handlers>
protected:
    static void threadFunc(AutomationLua* lua);

    void onStart()
    {
        // Open some common libraries
        _lua->open_libraries(sol::lib::base, sol::lib::package);
        _lua->script("print('AutomationLua::onStart - lua test script executed!\\n');print(_VERSION);");
    }

    void onFinish()
    {
        // Clean up Lua state on the Lua thread (single-threaded ownership)
        if (_luaEmulator)
        {
            delete _luaEmulator;
            _luaEmulator = nullptr;
        }

        if (_lua)
        {
            delete _lua;
            _lua = nullptr;
        }
    }

    /// endregion </Handlers>
};
