#include "automation-webapi.h"

#include <thread>
#include <future>
#include <fstream>
#include <sstream>
#include <vector>
#include <drogon/HttpController.h>
#include <drogon/WebSocketController.h>
#include "hello_world_api.h"        // Triggers auto-registration for API handlers
#include "emulator_api.h"           // Triggers auto-registration for API handlers
#include "emulator_websocket.h"     // Triggers auto-registration for WebSocket handlers

// Socket includes for port availability checking
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

// Helper function to check if a port is available
// CRITICAL: This prevents drogon from calling exit() when port is already in use
static bool isPortAvailable(int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        std::cerr << "Failed to create test socket for port availability check" << std::endl;
        return false;
    }
    
    // Set SO_REUSEADDR to match drogon's behavior
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    bool available = bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) >= 0;
    close(sockfd);
    
    return available;
}

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

    if (_thread && _thread->joinable())
    {
        // Stop all internal drogon loops/handlers and quit
        drogon::app().quit();

        // Join with a timeout using std::async to avoid blocking indefinitely
        auto joinFuture = std::async(std::launch::async, [this]() {
            if (_thread && _thread->joinable()) {
                _thread->join();
            }
        });
        
        // Wait up to 1000ms for the thread to finish
        // Drogon should stop quickly after quit() is called
        if (joinFuture.wait_for(std::chrono::milliseconds(1000)) == std::future_status::timeout)
        {
            std::cerr << "WARNING: WebAPI thread did not stop within 1000ms, detaching" << std::endl;
            if (_thread && _thread->joinable()) {
                _thread->detach();
            }
        }
    }
    
    if (_thread)
    {
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


    // CRITICAL: Check port availability BEFORE drogon initialization
    // This prevents drogon from calling exit() on bind failure
    const int port = 8090;
    if (!isPortAvailable(port))
    {
        std::cerr << std::endl;
        std::cerr << "========================================" << std::endl;
        std::cerr << "ERROR: WebAPI cannot start" << std::endl;
        std::cerr << "========================================" << std::endl;
        std::cerr << "Port " << port << " is already in use." << std::endl;
        std::cerr << std::endl;
        std::cerr << "The application will continue without WebAPI functionality." << std::endl;
        std::cerr << std::endl;
        std::cerr << "To use WebAPI, either:" << std::endl;
        std::cerr << "  - Stop other instances using port " << port << std::endl;
        std::cerr << "  - Configure a different port (future enhancement)" << std::endl;
        std::cerr << "========================================" << std::endl;
        std::cerr << std::endl;
        
        // Exit thread gracefully - application continues running
        return;
    }

    LOG_INFO << "Starting server on port 8090.";
    LOG_INFO << "API Documentation: http://localhost:8090/";
    LOG_INFO << "Emulator API: http://localhost:8090/api/v1/emulator";
    LOG_INFO << "OpenAPI Spec: http://localhost:8090/api/v1/openapi.json";
    LOG_INFO << "WebSocket: ws://localhost:8090/api/v1/websocket";

    drogon::HttpAppFramework& app = drogon::app();

    // Setup global CORS support
    // Handle CORS preflight (OPTIONS) requests
    app.registerSyncAdvice([](const drogon::HttpRequestPtr &req) -> drogon::HttpResponsePtr {
        if (req->method() == drogon::HttpMethod::Options)
        {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::HttpStatusCode::k204NoContent);

            // Set Access-Control-Allow-Origin
            resp->addHeader("Access-Control-Allow-Origin", "*");

            // Set Access-Control-Allow-Methods
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");

            // Set Access-Control-Allow-Headers - include what the client requests plus our defaults
            const auto &requestHeaders = req->getHeader("Access-Control-Request-Headers");
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
    app.registerPostHandlingAdvice(
        [](const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &resp) -> void {
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

    app.setLogPath("./")
        .setLogLevel(trantor::Logger::kNumberOfLogLevels)
        .disableSigtermHandling() // SIGTERM is handled by the main application (unreal-qt or testclient)
        .addListener("0.0.0.0", 8090)
        .setThreadNum(2)
        .run();
}