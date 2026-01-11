#include "automation-webapi.h"

#include <drogon/HttpController.h>
#include <drogon/WebSocketController.h>

#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include "emulator_api.h"        // Triggers auto-registration for API handlers
#include "emulator_websocket.h"  // Triggers auto-registration for WebSocket handlers
#include "hello_world_api.h"     // Triggers auto-registration for API handlers

// Helper function to load HTML file from resources
static std::string loadHtmlFile(const std::string& filename)
{
    // Try multiple possible locations for the HTML resources
    std::vector<std::string> searchPaths = {
        // Development/Build paths
        "./resources/html/",                         // In current directory (from bin when running)
        "../resources/html/",                        // One level up (from bin to build root)
        "./core/automation/webapi/resources/html/",  // Development/build from project root
        "../../resources/html/",                     // Two levels up

        // macOS .app bundle path
        "../Resources/html/",     // From MacOS folder to Resources folder in .app bundle
        "../../Resources/html/",  // Alternative .app bundle structure

        // Standard installation paths
        "/usr/local/share/unreal-speccy/resources/html/",  // Unix standard install
        "/usr/share/unreal-speccy/resources/html/",        // Unix system install
        "./share/unreal-speccy/resources/html/",           // Relative to install prefix
    };

    for (const auto& basePath : searchPaths)
    {
        std::string fullPath = basePath + filename;
        std::ifstream file(fullPath);

        if (file.is_open())
        {
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }

    // Fallback: return a minimal HTML page if file not found
    return "<!DOCTYPE html><html><head><title>Error</title></head><body>"
           "<h1>Resource Not Found</h1>"
           "<p>Could not load HTML resource: " +
           filename +
           "</p>"
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
        mbstowcs_s(&retval, wname, threadName, sizeof(threadName) / sizeof(threadName[0]), &conversion);
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
        mbsrtowcs_s(&retval, wname, (size_t)(sizeof(wname) / sizeof(wname[0])), &threadName,
                    (size_t)(sizeof(threadName) / sizeof(threadName[0])), &conversion);
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

    // Setup global CORS support
    // Handle CORS preflight (OPTIONS) requests
    app.registerSyncAdvice([](const drogon::HttpRequestPtr& req) -> drogon::HttpResponsePtr {
        if (req->method() == drogon::HttpMethod::Options)
        {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::HttpStatusCode::k204NoContent);

            // Set Access-Control-Allow-Origin
            resp->addHeader("Access-Control-Allow-Origin", "*");

            // Set Access-Control-Allow-Methods
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");

            // Set Access-Control-Allow-Headers - include what the client requests plus our defaults
            const auto& requestHeaders = req->getHeader("Access-Control-Request-Headers");
            if (!requestHeaders.empty())
            {
                resp->addHeader("Access-Control-Allow-Headers", requestHeaders);
            }
            else
            {
                resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
            }

            // Add caching for preflight requests (24 hours)
            resp->addHeader("Access-Control-Max-Age", "86400");

            return resp;
        }
        return {};
    });

    // Add CORS headers to all responses
    app.registerPostHandlingAdvice([](const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) -> void {
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
    });

    // Custom 404 page that guides users to API documentation
    // Load from external HTML file for easy maintenance
    std::string notFoundHtml = loadHtmlFile("404.html");

    auto notFoundResp = drogon::HttpResponse::newHttpResponse();
    notFoundResp->setBody(notFoundHtml);
    notFoundResp->setContentTypeCode(drogon::ContentType::CT_TEXT_HTML);
    notFoundResp->setStatusCode(drogon::HttpStatusCode::k404NotFound);

    app.setCustom404Page(notFoundResp);

    // Create a writable log directory in the user's home folder
    std::string logPath;
#ifdef __APPLE__
    // On macOS, use ~/Library/Logs/UnrealNG/
    const char* homeDir = getenv("HOME");
    if (homeDir)
    {
        logPath = std::string(homeDir) + "/Library/Logs/UnrealNG";
        // Create the directory if it doesn't exist
        std::filesystem::create_directories(logPath);
    }
    else
    {
        // Fallback to temporary directory if HOME is not available
        logPath = "/tmp/UnrealNG";
        std::filesystem::create_directories(logPath);
    }
#else
    // On other platforms, use the current directory
    logPath = "./";
#endif

    LOG_INFO << "Using log path: " << logPath;

    app.setLogPath(logPath)
        .setLogLevel(trantor::Logger::kNumberOfLogLevels)
        .disableSigtermHandling()  // SIGTERM is handled by the main application (unreal-qt or testclient)
        .addListener("0.0.0.0", 8090)
        .setThreadNum(2);

    try
    {
        app.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR << "WebAPI server failed to start: " << e.what();
        LOG_ERROR << "Port 8090 may already be in use. WebAPI will be disabled.";
        // Don't exit - just let the thread end gracefully
    }
}