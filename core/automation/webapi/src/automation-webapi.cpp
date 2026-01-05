#include "automation-webapi.h"

#include <thread>
#include <fstream>
#include <sstream>
#include <vector>
#include <drogon/HttpController.h>
#include <drogon/WebSocketController.h>
#include "hello_world_api.h"        // Triggers auto-registration for API handlers
#include "emulator_api.h"           // Triggers auto-registration for API handlers
#include "emulator_websocket.h"     // Triggers auto-registration for WebSocket handlers

// Helper function to load HTML file from resources
static std::string loadHtmlFile(const std::string& filename) {
    // Try multiple possible locations for the HTML resources
    std::vector<std::string> searchPaths = {
        // Development/Build paths
        "./resources/html/",                         // In current directory (from bin when running)
        "../resources/html/",                        // One level up (from bin to build root)
        "./core/automation/webapi/resources/html/",  // Development/build from project root
        "../../resources/html/",                     // Two levels up
        
        // macOS .app bundle path
        "../Resources/html/",                        // From MacOS folder to Resources folder in .app bundle
        "../../Resources/html/",                     // Alternative .app bundle structure
        
        // Standard installation paths
        "/usr/local/share/unreal-speccy/resources/html/",  // Unix standard install
        "/usr/share/unreal-speccy/resources/html/",        // Unix system install
        "./share/unreal-speccy/resources/html/",           // Relative to install prefix
    };
    
    for (const auto& basePath : searchPaths) {
        std::string fullPath = basePath + filename;
        std::ifstream file(fullPath);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }
    
    // Fallback: return a minimal HTML page if file not found
    return "<!DOCTYPE html><html><head><title>Error</title></head><body>"
           "<h1>Resource Not Found</h1>"
           "<p>Could not load HTML resource: " + filename + "</p>"
           "<p>Searched paths: development builds, macOS .app bundle, standard installations</p>"
           "<p>Please ensure resources are properly installed.</p>"
           "</body></html>";
}

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
    LOG_INFO << "API Documentation: http://localhost:8090/";
    LOG_INFO << "Emulator API: http://localhost:8090/api/v1/emulator";
    LOG_INFO << "OpenAPI Spec: http://localhost:8090/api/v1/openapi.json";
    LOG_INFO << "WebSocket: ws://localhost:8090/api/v1/websocket";

    drogon::HttpAppFramework& app = drogon::app();

    // Custom 404 page that guides users to API documentation
    // Load from external HTML file for easy maintenance
    std::string notFoundHtml = loadHtmlFile("404.html");
    
    auto notFoundResp = drogon::HttpResponse::newHttpResponse();
    notFoundResp->setBody(notFoundHtml);
    notFoundResp->setContentTypeCode(drogon::ContentType::CT_TEXT_HTML);
    notFoundResp->setStatusCode(drogon::HttpStatusCode::k404NotFound);
    
    app.setCustom404Page(notFoundResp);

    app.setLogPath("./")
        .setLogLevel(trantor::Logger::kNumberOfLogLevels)
        .disableSigtermHandling() // SIGTERM is handled by the main application (unreal-qt or testclient)
        .addListener("0.0.0.0", 8090)
        .setThreadNum(2)
        .run();
}