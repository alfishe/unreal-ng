#include "automation-lua.h"
#include "emulator/lua_emulator.h"
#include "hooks/luahookhost.h"

#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>

#include "common/threadhelper.h"
#include "emulator/coordinator/machineop.h"

/// region <Methods>
void AutomationLua::start()
{
    stop();
    // Frame and sync script callbacks run in a Lua hook state of each emulator (docs/features/script-callbacks.md)
    SetScriptHookHostFactory(&LuaHookHost::Create);

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
    // Guard: If thread is already stopped/nullptr, nothing to do.
    // This prevents double-stop race conditions when stop() is called
    // from both Automation::stop() and destructors.
    if (!_thread)
    {
        return;
    }

    // Set under the queue lock: the thread is either before its wait (and sees the flag) or in it (and gets the
    // notification), never between the two
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _stopThread = true;
    }
    _queueCV.notify_one();
    
    // CRITICAL: Join thread FIRST before deleting objects it references
    // The thread may still be executing and using _lua/_luaEmulator
    _thread->join();
    delete _thread;
    _thread = nullptr;

    // NOTE: Do NOT reset _stopThread = false here!
    // The thread function may still be in its final cleanup phase
    // and reading this flag. Leave it as true after stop.

    // CRITICAL DESTRUCTION ORDER:
    // Delete _lua FIRST, THEN _luaEmulator.
    // The sol::state has registered lambdas (via registerType) that capture
    // the LuaEmulator* pointer. When _lua is destroyed, Lua's GC runs and
    // cleans up these lambdas. If we deleted _luaEmulator first, the GC
    // would try to access a dangling pointer causing SIGSEGV.
    if (_lua)
    {
        // Run GC to clean up registered userdata before state destruction
        _lua->collect_garbage();
        _lua->collect_garbage();
        
        delete _lua;
        _lua = nullptr;
    }

    if (_luaEmulator)
    {
        delete _luaEmulator;
        _luaEmulator = nullptr;
    }

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
            // Every argument as Lua's print shows it, nil included
            lua["print"] = [&output, tostring_func](sol::variadic_args args) {
                bool first = true;
                for (auto arg : args) {
                    if (!first) {
                        output += "\t"; // Lua's default print separator
                    }
                    first = false;
                    output += tostring_func(sol::object(arg)).get<std::string>();
                }
                output += "\n"; // Lua's default print newline
            };

            // Protected call: an error the script raises (a nil index, error(), a syntax error) comes back as a
            // result. lua.script() calls unprotected: the error reaches the panic handler, whose C++ throw cannot
            // unwind through Lua's C frames and terminates the process
            const sol::protected_function_result result = lua.safe_script(code, sol::script_pass_on_error);

            // Restore original print function (the capture refers to this frame's output)
            lua["print"] = original_print;

            if (!result.valid())
            {
                sol::error error = result;
                throw error;
            }

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
    ThreadHelper::setThreadName("automation-lua");
    coordinator::SetThreadSource(coordinator::OpSource::Lua);   // every script runs here: the operation log names it

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
    // CRITICAL: Wire the sol::state into LuaEmulator BEFORE registering bindings.
    // Several bindings (get_registers, mem_read_block, memory_info, disasm, ...)
    // dereference LuaEmulator::_lua to create result tables; leaving it nullptr
    // causes a SIGSEGV on the first call.
    lua->_luaEmulator->setLuaState(lua->_lua);
    lua->_luaEmulator->registerType(*lua->_lua);
    // A callback's ring went from empty to not empty: the loop below runs the callbacks
    lua->_luaEmulator->SetCallbackWake([lua]() {
        {
            std::lock_guard<std::mutex> lock(lua->_queueMutex);
            lua->_callbacksPending = true;
        }
        lua->_queueCV.notify_one();
    });
    
    // Signal that initialization is complete
    {
        std::lock_guard<std::mutex> lock(lua->_initMutex);
        lua->_initialized = true;
    }
    lua->_initCV.notify_one();

    lua->onStart();

    // The wait blocks until a task or the stop arrives: no sleep after a task, a script waits for nothing
    while (!lua->_stopThread)
    {
        // Process pending tasks from the queue
        {
            std::unique_lock<std::mutex> lock(lua->_queueMutex);
            
            // Wait for tasks or timeout
            lua->_queueCV.wait_for(lock, std::chrono::milliseconds(100), [lua]() {
                return !lua->_taskQueue.empty() || lua->_callbacksPending || lua->_stopThread;
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

            // Script callbacks: the functions run here, between the tasks, never on the emulation thread
            if (lua->_callbacksPending)
            {
                lua->_callbacksPending = false;
                lock.unlock();
                const bool more = lua->_luaEmulator->DrainCallbacks();
                lock.lock();
                if (more)
                    lua->_callbacksPending = true;
            }
        }
    }

    lua->_luaEmulator->ForgetCallbacks();
    lua->onFinish();
}
