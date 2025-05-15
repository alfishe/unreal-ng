#include "automation-webapi.h"

#include <thread>
#include <drogon/HttpController.h>
#include <drogon/WebSocketController.h>
#include "hello_world_api.h"        // Triggers auto-registration for API handlers
#include "emulator_api.h"           // Triggers auto-registration for API handlers
#include "emulator_websocket.h"     // Triggers auto-registration for WebSocket handlers

/// region <Methods>
void AutomationWebAPI::start()
{
    stop();

    _stopThread = false;

    // Create a new thread and run HTTP server in it
    _thread = new std::thread(&AutomationWebAPI::threadFunc, this);
}

void AutomationWebAPI::stop()
{
    _stopThread = true;

    if (_thread)
    {
        // Stop all internal drogon loops/handlers and quit
        drogon::app().quit();

        _thread->join();
        _stopThread = false;

        delete _thread;
        _thread = nullptr;
    }

    std::cout << "AutomationWebAPI stopped" << std::endl;
}
/// endregion </Methods>

void AutomationWebAPI::threadFunc(AutomationWebAPI* webApi)
{
    /// region <Make thread named for easy reading in debuggers>
    const char* threadName = "automation_webapi";

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
        mbstate_t conversion;
        mbstowcs_s(&retval, wname, threadName, sizeof (threadName) / sizeof (threadName[0]), &conversion);
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


    LOG_INFO << "Starting server on port 8090.";
    LOG_INFO << "HTTP API example: http://localhost:8090/api/v1/HelloWorld";
    LOG_INFO << "HTTP Emulator API: http://localhost:8090/api/v1/EmulatorAPI";
    LOG_INFO << "WebSocket endpoint: ws://localhost:8090/api/v1/websocket";

    drogon::HttpAppFramework& app = drogon::app();

    app.setLogPath("./")
        .setLogLevel(trantor::Logger::kNumberOfLogLevels)
        .disableSigtermHandling() // SIGTERM is handled by the main application (unreal-qt or testclient)
        .addListener("0.0.0.0", 8090)
        .setThreadNum(2)
        .run();
}