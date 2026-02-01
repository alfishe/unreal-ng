#include "automation-lua.h"
#include "emulator/lua_emulator.h"

#include <chrono>
#include <thread>
#include <iostream>

/// region <Methods>
void AutomationLua::start()
{
    stop();

    _stopThread = false;

    // Create a new thread and run Lua in it
    _thread = new std::thread(&AutomationLua::threadFunc, this);

    _lua = new sol::state();

    _luaEmulator = new LuaEmulator();
    _luaEmulator->setLuaState(_lua);
    _luaEmulator->registerType(*_lua);
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

    _stopThread = true;

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

    lua->onStart();

    using namespace std::chrono_literals;

    while (!lua->_stopThread)
    {
        std::this_thread::sleep_for(10ms);
    }

    lua->onFinish();
}
