#include "automation-lua.h"
#include "emulator/lua_emulator.h"

#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>

/// region <Methods>
void AutomationLua::start()
{
    stop();

    _stopThread = false;
    _initialized = false;

    // Create a new thread and run Lua in it
    _thread = new std::thread(&AutomationLua::threadFunc, this);

    // Wait for Lua state to be initialized on the Lua thread
    {
        std::unique_lock<std::mutex> lock(_initMutex);
        _initCV.wait(lock, [this]() { return _initialized; });
    }
}

void AutomationLua::stop()
{
    _stopThread = true;

    if (_thread)
    {
        // Notify the thread to wake up and check _stopThread
        _queueCV.notify_one();
        
        // Wait for thread to finish (it will clean up Lua state)
        _thread->join();
        _stopThread = false;

        delete _thread;
        _thread = nullptr;
    }
    
    // Clean up pointers (actual resources cleaned up in thread)
    _luaEmulator = nullptr;
    _lua = nullptr;

    std::cout << "Lua interpreter stopped" << std::endl;
}

bool AutomationLua::executeCode(const std::string& code, std::string& errorMessage, std::string& capturedOutput)
{
    if (!_lua)
    {
        errorMessage = "Lua interpreter not initialized";
        capturedOutput = "";
        return false;
    }

    try
    {
        capturedOutput = dispatchSync([&code](sol::state& lua) -> std::string {
            std::string output;

            // Store original print function and tostring
            sol::function original_print = lua["print"];
            sol::function tostring_func = lua["tostring"];

            // Override print function to capture output
            lua["print"] = [&output, tostring_func](sol::object a, sol::object b, sol::object c, sol::object d, sol::object e) {
                std::vector<sol::object> args = {a, b, c, d, e};
                bool first = true;
                for (auto& arg : args) {
                    if (arg == sol::lua_nil) break; // Stop at nil arguments
                    if (!first) {
                        output += "\t"; // Lua's default print separator
                    }
                    first = false;
                    output += tostring_func(arg);
                }
                output += "\n"; // Lua's default print newline
            };

            // Execute the code
            lua.script(code);

            // Restore original print function
            lua["print"] = original_print;

            return output;
        });
        return true;
    }
    catch (const sol::error& e)
    {
        errorMessage = e.what();
        capturedOutput = "";
        return false;
    }
    catch (const std::exception& e)
    {
        errorMessage = std::string("C++ error: ") + e.what();
        return false;
    }
}

bool AutomationLua::executeFile(const std::string& path, std::string& errorMessage, std::string& capturedOutput)
{
    // File loading will be implemented in CLI handler with FileHelper
    // This method receives already-read file content
    return executeCode(path, errorMessage, capturedOutput);
}

std::string AutomationLua::getStatusString() const
{
    std::ostringstream oss;
    
    if (_lua)
    {
        oss << "State: Running\n";
        oss << "Thread: " << (_thread ? "Active" : "Inactive") << "\n";
        
        // Format thread ID as hex (decimal) if thread exists
        if (_thread)
        {
            auto threadId = _thread->get_id();
            oss << "Thread ID: 0x" << std::hex << std::uppercase << std::hash<std::thread::id>{}(threadId)
                << std::dec << " (" << std::hash<std::thread::id>{}(threadId) << ")\n";
        }
        
        oss << "Interpreter: Initialized\n";
        
        // Query Lua version
        if (_lua && _thread->joinable())
        {
            try
            {
                std::string version = const_cast<AutomationLua*>(this)->eval("_VERSION");
                oss << "\nLua Version: " << version << "\n";
            }
            catch (const std::exception& e)
            {
                oss << "\n[Unable to query Lua version: " << e.what() << "]\n";
            }
        }
        else
        {
            oss << "\n[Lua state not initialized]\n";
        }
    }
    else
    {
        oss << "State: Not Initialized\n";
    }
    
    return oss.str();
}
/// endregion </Methods>

void AutomationLua::threadFunc(AutomationLua* lua)
{
    /// region <Make thread named for easy reading in debuggers>
    const char* threadName = "automation_lua";

#ifdef __APPLE__
#include <pthread.h>
    pthread_setname_np(threadName);
#endif
#ifdef __linux__
    #include <pthread.h>
	pthread_setname_np(pthread_self(), threadName);
#endif
#if defined _WIN32 && defined MSVC
    static auto setThreadDescription = reinterpret_cast<HRESULT(WINAPI*)(HANDLE, PCWSTR)>(
        GetProcAddress(GetModuleHandle("kernelbase.dll"), "SetThreadDescription"));
    if (setThreadDescription != nullptr)
    {
	    wchar_t wname[24];
	    size_t retval;
        mbstowcs_s(&retval, wname, threadName, sizeof (threadName) / sizeof (threadName[0]));
        setThreadDescription(GetCurrentThread(), wname);
    }
#endif

#if defined _WIN32 && defined __GNUC__
    static auto setThreadDescription = reinterpret_cast<HRESULT(WINAPI*)(HANDLE, PCWSTR)>(
            GetProcAddress(GetModuleHandle("kernelbase.dll"), "SetThreadDescription"));
    if (setThreadDescription != nullptr)
    {
        wchar_t wname[24];
        size_t retval;
        mbstate_t conversion;
        mbsrtowcs_s(&retval, wname, (size_t)(sizeof (wname) / sizeof (wname[0])), &threadName, (size_t)(sizeof (threadName) / sizeof (threadName[0])), &conversion);
        setThreadDescription(GetCurrentThread(), wname);
    }
#endif
    /// endregion </Make thread named for easy reading in debuggers>

    // Initialize Lua state on this dedicated thread (single-threaded ownership)
    lua->_lua = new sol::state();
    
    // Enable all standard Lua libraries (os, io, math, string, table, etc.)
    // This allows users to use os.execute, file I/O, and other standard Lua functionality
    lua->_lua->open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::coroutine,
        sol::lib::string,
        sol::lib::os,
        sol::lib::math,
        sol::lib::table,
        sol::lib::debug,
        sol::lib::bit32,
        sol::lib::io,
        sol::lib::utf8
    );
    
    lua->_luaEmulator = new LuaEmulator();
    lua->_luaEmulator->registerType(*lua->_lua);
    
    // Signal that initialization is complete
    {
        std::lock_guard<std::mutex> lock(lua->_initMutex);
        lua->_initialized = true;
    }
    lua->_initCV.notify_one();

    lua->onStart();

    using namespace std::chrono_literals;

    while (!lua->_stopThread)
    {
        // Process pending tasks from the queue
        {
            std::unique_lock<std::mutex> lock(lua->_queueMutex);
            
            // Wait for tasks or timeout
            lua->_queueCV.wait_for(lock, std::chrono::milliseconds(100), [lua]() {
                return !lua->_taskQueue.empty() || lua->_stopThread;
            });
            
            // Process all pending tasks
            while (!lua->_taskQueue.empty())
            {
                auto task = std::move(lua->_taskQueue.front());
                lua->_taskQueue.pop();
                lock.unlock();
                
                task(); // Execute task in Lua thread context
                
                lock.lock();
            }
        }
        
        // Small sleep to avoid busy-waiting
        std::this_thread::sleep_for(10ms);
    }

    lua->onFinish();
}
