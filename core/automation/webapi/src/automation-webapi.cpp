#include "automation-webapi.h"

#include <cstdlib>
#include <drogon/HttpController.h>
#include <drogon/WebSocketController.h>

#include <fstream>
#include <future>
#include <json/json.h>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include "common/threadhelper.h"
#include "common/companion_helper.h"
#include "common/request_gates.h"
#include "common/session_helper.h"
#include "emulator/coordinator/machineop.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/companionregistry.h"
#include "emulator/monitoring/clientstats.h"
#include "common/processstats.h"
#include "emulator_api.h"        // Triggers auto-registration for API handlers
#include "emulator_websocket.h"  // Triggers auto-registration for WebSocket handlers
#include "api-websocket.h"       // /api/v1/ws
#include "companion-event-hub.h"
#include "debug-event-hub.h"
#include "ws-hub.h"            // the topics and the surface topic of /api/v1/ws
#include "hello_world_api.h"     // Triggers auto-registration for API handlers
#include "interpreter_api.h"     // Triggers auto-registration for Lua/Python API handlers
#include "common/upload_helper.h"   // MAX_UPLOAD_BODY_SIZE constant

// Socket includes for port availability checking
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// Helper function to check if a port is available
// CRITICAL: This prevents drogon from calling exit() when port is already in use
static bool isPortAvailable(int port)
{
#ifdef _WIN32
    // Initialize Winsock if needed
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "Failed to initialize Winsock for port availability check" << std::endl;
        return false;
    }

    SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET)
    {
        std::cerr << "Failed to create test socket for port availability check" << std::endl;
        WSACleanup();
        return false;
    }

    // Set SO_REUSEADDR to match drogon's behavior
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    bool available = bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR;
    closesocket(sockfd);
    WSACleanup();

    return available;
#else
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
#endif
}

// Helper function to load HTML file from resources
static std::string loadHtmlFile(const std::string& filename)
{
    // Try multiple possible locations for the HTML resources
    std::vector<std::string> searchPaths = {
        // Development/Build paths (cmake-build-*/bin/ -> source)
        "../../core/automation/webapi/resources/html/",  // From bin/ to source (typical cmake build)
        "../../../core/automation/webapi/resources/html/", // From nested bin/
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

namespace webapi_gate
{
/// The sync advice of every request (CORS preflight, companions, sessions, the automation gate); the WebSocket's
/// calls (api-websocket.cpp) pass it too, since a forwarded request skips the sync advices
drogon::HttpResponsePtr PassRequestGates(const drogon::HttpRequestPtr& req)
{
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
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With, X-Unreal-Session, X-Unreal-Source");
        }

        // Add caching for preflight requests (24 hours)
        resp->addHeader("Access-Control-Max-Age", "86400");

        return resp;
    }

    // Companion clients (docs/inprogress/2026-10-16-companion-client): a paired device acts as the user's hands.
    // Its changes open no session - no remote control at the GUI, no lock - within the scopes the user granted;
    // while a session holds the instance it is refused like the GUI is locked, and may take control back
    if (const std::optional<std::string> token = webapi_companion::BearerOf(req))
    {
        auto refuse = [](drogon::HttpStatusCode status, const Json::Value& body) {
            auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
            resp->setStatusCode(status);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            return resp;
        };
        if (req->getHeader("X-Unreal-Source") == "mcp")
            return refuse(drogon::HttpStatusCode::k403Forbidden,
                          webapi_companion::ErrorBody("companion_not_for_agents",
                                                      "An MCP agent does not act as a companion: take control "
                                                      "(control_execution take_control) instead"));
        if (webapi_session::SessionOf(req))
            return refuse(drogon::HttpStatusCode::k400BadRequest,
                          webapi_companion::ErrorBody("companion_session",
                                                      "A companion acts as the user and carries no session: drop "
                                                      "the X-Unreal-Session header"));
        const std::optional<companion::CompanionInfo> companion =
            companion::CompanionRegistry::Instance().Authenticate(*token);
        if (!companion)
            return refuse(drogon::HttpStatusCode::k401Unauthorized,
                          webapi_companion::ErrorBody("companion_unknown",
                                                      "Unknown or revoked companion token: pair again"));
        req->attributes()->insert(webapi_companion::kAttribute, *companion);

        static const std::string kPrefix = "/api/v1/emulator/";
        const std::string& path = req->path();
        if (path.rfind(kPrefix, 0) != 0)
            return {};
        const size_t idEnd = path.find('/', kPrefix.size());
        const std::string subpath = idEnd == std::string::npos ? std::string() : path.substr(idEnd);
        if (!automation_gate::ChangesMachine(req->methodString(), subpath))
            return {};
        std::shared_ptr<Emulator> emulator = api::v1::getEmulatorByIdOrIndex(
            path.substr(kPrefix.size(), idEnd == std::string::npos ? std::string::npos : idEnd - kPrefix.size()));
        EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
        if (!context)
            return {};  // the handler answers
        const EmulatorContext::RunControlState control = context->GetRunControlState();
        const uint32_t required = automation_gate::CompanionScopeOf(req->methodString(), subpath);
        const companion::ActionCheck check = companion::CheckAction(
            *companion, required, control.claimed, webapi_companion::HolderName(control),
            "POST /control/take-back", automation_gate::CompanionNeedsPause(subpath),
            emulator->GetState() == StatePaused);
        if (check.allowed)
            return {};  // the user's own action: no session
        if (check.error == "companion_forbidden")
            return refuse(drogon::HttpStatusCode::k403Forbidden, webapi_companion::ForbiddenBody(required));
        if (check.error == "held_by_session")
            return refuse(drogon::HttpStatusCode::k409Conflict, webapi_companion::HeldBody(control, *companion));
        return refuse(drogon::HttpStatusCode::k409Conflict, webapi_companion::ErrorBody(check.error, check.message));
    }

    // Automation sessions (execution arbiter design §6a): a request carrying the session the user took control back
    // from is answered at once, final - the client stops, it does not retry. A live session's request renews its
    // idle lease. Ending a taken-back session (DELETE .../control/session) is let through: it is a no-op
    if (const std::optional<unreal::UUID> session = webapi_session::SessionOf(req))
    {
        const bool ending = req->method() == drogon::HttpMethod::Delete &&
                            req->path().find("/control/session") != std::string::npos;
        EmulatorManager* manager = EmulatorManager::GetInstance();
        for (const std::string& emulatorId : manager->GetEmulatorIds())
        {
            std::shared_ptr<Emulator> emulator = manager->GetEmulator(emulatorId);
            EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
            if (!context)
                continue;
            if (context->IsRunControlRevoked(*session) && !ending)
            {
                auto resp = drogon::HttpResponse::newHttpJsonResponse(webapi_session::RevokedBody());
                resp->setStatusCode(drogon::HttpStatusCode::k409Conflict);
                return resp;
            }
            context->TouchRunControl(*session);
        }
        return {};
    }

    // The automation gate (owner decision 2026-10-11, automationgate.h): a request that changes a machine without
    // a session opens one implicitly - the GUI shows the remote control and locks, the user can take control back.
    // The request then goes on as the session's (its id in the header: the handlers' own checks see the holder);
    // the reply says so and advises taking control explicitly (the post-handling advice below)
    static const std::string kInstancePrefix = "/api/v1/emulator/";
    const std::string& path = req->path();
    if (path.rfind(kInstancePrefix, 0) != 0)
        return {};
    const size_t idEnd = path.find('/', kInstancePrefix.size());
    const std::string id = path.substr(kInstancePrefix.size(),
                                       idEnd == std::string::npos ? std::string::npos : idEnd - kInstancePrefix.size());
    const std::string subpath = idEnd == std::string::npos ? std::string() : path.substr(idEnd);
    if (!automation_gate::ChangesMachine(req->methodString(), subpath))
        return {};
    std::shared_ptr<Emulator> emulator = api::v1::getEmulatorByIdOrIndex(id);
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context)
        return {};  // not an instance ("start", "create", a wrong id): the handler answers
    const webapi_session::GateClient client = webapi_session::GateClientOf(req);
    const EmulatorContext::ImplicitControl gate = context->EnterImplicitRunControl(
        client.surface, client.identity, client.client,
        std::chrono::duration_cast<std::chrono::milliseconds>(automation_gate::kImplicitLease), client.peer);
    if (!gate.allowed)
    {
        Json::Value body = gate.error == "revoked_by_user" ? webapi_session::RevokedBody() : Json::Value();
        if (gate.error != "revoked_by_user")
        {
            body["error"] = gate.error;
            body["retry"] = false;
            body["message"] = gate.message;
        }
        body["advice"] = automation_gate::kAdvice;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(drogon::HttpStatusCode::k409Conflict);
        return resp;
    }
    const std::string sessionId = gate.owner.toString();
    req->addHeader(webapi_session::kHeader, sessionId);
    req->attributes()->insert(webapi_session::kImplicitSessionAttribute, sessionId);
    req->attributes()->insert(webapi_session::kImplicitOpenedAttribute, gate.opened);
    return {};
}
}  // namespace webapi_gate

/// region <Methods>
void AutomationWebAPI::start()
{
    stop();

    _stopThread = false;

    // Debugger events for /api/v1/websocket subscribers
    DebugEventHub::Instance().Start();
    // Companion events for /api/v1/companions/events: the same view as GET /api/v1/companions/me
    CompanionEventHub::Instance().SetSnapshot(&api::v1::CompanionMeJson);
    CompanionEventHub::Instance().Start();
    // The topics of /api/v1/ws and its surface topic: contexts, companions, instances, run state, debugger events.
    // `instances` is the events socket's view; the backpressure limits may be moved for tests
    WsHub::Instance().SetSnapshot(&api::v1::CompanionMeJson);
    if (const char* bytes = getenv("UNREAL_WS_HIGH_WATER_BYTES"))
    {
        const long long parsed = strtoll(bytes, nullptr, 10);
        if (parsed > 0)
            WsHub::Instance().SetHighWaterMark(static_cast<uint64_t>(parsed));
    }
    if (const char* ms = getenv("UNREAL_WS_SLOW_CONSUMER_MS"))
    {
        const long long parsed = strtoll(ms, nullptr, 10);
        if (parsed > 0)
            WsHub::Instance().SetSlowConsumerLimit(std::chrono::milliseconds(parsed));
    }
    WsHub::Instance().Start();

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
            if (_thread && _thread->joinable())
            {
                _thread->join();
            }
        });

        // Wait up to 1000ms for the thread to finish
        // Drogon should stop quickly after quit() is called
        if (joinFuture.wait_for(std::chrono::milliseconds(1000)) == std::future_status::timeout)
        {
            std::cerr << "WARNING: WebAPI thread did not stop within 1000ms, detaching" << std::endl;
            if (_thread && _thread->joinable())
            {
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

    DebugEventHub::Instance().Stop();
    CompanionEventHub::Instance().Stop();
    WsHub::Instance().Stop();

    std::cout << "AutomationWebAPI stopped" << std::endl;
}
/// endregion </Methods>

void AutomationWebAPI::threadFunc(AutomationWebAPI* webApi)
{
    ThreadHelper::setThreadName("automation-webapi");

    // CRITICAL: Check port availability BEFORE drogon initialization
    // This prevents drogon from calling exit() on bind failure
    // The listen port can be moved with the UNREAL_WEBAPI_PORT env var
    // (1-65535) so a second instance can run beside one that already
    // owns the default port - e.g. diagnostics against a live server
    int port = 8090;
    if (const char* portEnv = getenv("UNREAL_WEBAPI_PORT"))
    {
        const long parsed = strtol(portEnv, nullptr, 10);
        if (parsed >= 1 && parsed <= 65535)
            port = static_cast<int>(parsed);
    }
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
        std::cerr << "  - Set UNREAL_WEBAPI_PORT to a free port and restart" << std::endl;
        std::cerr << "========================================" << std::endl;
        std::cerr << std::endl;

        // Exit thread gracefully - application continues running
        return;
    }

    // Log startup info
    LOG_INFO << "Starting server on port " << port << ".";
    LOG_INFO << "API Documentation: http://localhost:" << port << "/";
    LOG_INFO << "Emulator API: http://localhost:" << port << "/api/v1/emulator";
    LOG_INFO << "OpenAPI Spec: http://localhost:" << port << "/api/v1/openapi.json";
    LOG_INFO << "WebSocket: ws://localhost:" << port << "/api/v1/ws (calls, topics, surface), /api/v1/websocket (debugger events)";

    drogon::HttpAppFramework& app = drogon::app();

    // host.stats: the first sample now, so the first reading already has a CPU % and the uptime counts from here
    (void)ProcessStatsSampler::Instance().Read();

    // Setup global CORS support
    // Handle CORS preflight (OPTIONS) requests
    app.registerSyncAdvice([](const drogon::HttpRequestPtr& req) { return webapi_gate::PassRequestGates(req); });

    // The operation log names who asked (machine state coordinator): a handler runs on the thread this advice ran
    // on, so the request marks its thread; the MCP server calls the WebAPI and says so in X-Unreal-Source
    app.registerPreHandlingAdvice([](const drogon::HttpRequestPtr& req) {
        // Monitoring (host.clients): POST /mcp is an MCP request; an MCP tool's own calls back here are not clients
        // A WebSocket `call` is counted on its socket (ws.requests)
        const bool mcpLoopback = req->getHeader("X-Unreal-Source") == "mcp";
        if (!req->attributes()->find("unreal.ws_call"))
        {
            if (req->path().rfind("/mcp", 0) == 0)
                monitoring::ClientStats::Instance().Request(monitoring::ClientPlane::Mcp);
            else if (!mcpLoopback)
                monitoring::ClientStats::Instance().Request(monitoring::ClientPlane::Rest);
        }
        coordinator::SetThreadSource(req->attributes()->find(webapi_companion::kAttribute)
                                         ? coordinator::OpSource::Companion
                                     : req->getHeader("X-Unreal-Source") == "mcp" ? coordinator::OpSource::Mcp
                                                                                  : coordinator::OpSource::WebApi);
    });

    // Add CORS headers to all responses
    app.registerPostHandlingAdvice([](const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) -> void {
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers",
                        "Content-Type, Authorization, X-Requested-With, X-Unreal-Session, X-Unreal-Source");
        resp->addHeader("Access-Control-Expose-Headers",
                        "X-Unreal-Session, X-Unreal-Session-Implicit, X-Unreal-Session-Advice");

        // The automation gate opened (or went on in) an implicit session for this request: say so and advise
        // taking control explicitly - in headers, and in a JSON object reply as "automation_session"
        const auto& attributes = req->attributes();
        if (!attributes->find(webapi_session::kImplicitSessionAttribute))
            return;
        const std::string sessionId = attributes->get<std::string>(webapi_session::kImplicitSessionAttribute);
        const bool opened = attributes->get<bool>(webapi_session::kImplicitOpenedAttribute);
        resp->addHeader(webapi_session::kHeader, sessionId);
        resp->addHeader("X-Unreal-Session-Implicit", "true");
        resp->addHeader("X-Unreal-Session-Advice", automation_gate::kAdvice);
        // The reply's JSON object, as sent (handlers build it in different ways: parse what goes out)
        if (resp->contentType() != drogon::CT_APPLICATION_JSON)
            return;
        const std::string_view text = resp->body();
        Json::Value body;
        Json::CharReaderBuilder reader;
        std::string errors;
        const std::unique_ptr<Json::CharReader> parser(reader.newCharReader());
        if (!parser->parse(text.data(), text.data() + text.size(), &body, &errors) || !body.isObject())
            return;
        Json::Value session;
        session["id"] = sessionId;
        session["implicit"] = true;
        session["opened"] = opened;
        session["advice"] = automation_gate::kAdvice;
        body["automation_session"] = session;
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        resp->setBody(Json::writeString(writer, body));
    });

    // Set max body size for file uploads (covers snapshots, disks, tapes)
    app.setClientMaxBodySize(api::v1::MAX_UPLOAD_BODY_SIZE);
    // Also set max memory body size so bodies are kept in memory, not temp files
    app.setClientMaxMemoryBodySize(api::v1::MAX_UPLOAD_BODY_SIZE);
    // A long call answers when it is done (record/frames, the window recording's stop on a loaded host): drogon's
    // default drops a connection silent for 60 s and the answer with it, so idle connections are never closed here
    app.setIdleConnectionTimeout(0);

    // Custom 404 page that guides users to API documentation
    // Load from external HTML file for easy maintenance
    std::string notFoundHtml = loadHtmlFile("404.html");

    auto notFoundResp = drogon::HttpResponse::newHttpResponse();
    notFoundResp->setBody(notFoundHtml);
    notFoundResp->setContentTypeCode(drogon::ContentType::CT_TEXT_HTML);
    notFoundResp->setStatusCode(drogon::HttpStatusCode::k404NotFound);

    app.setCustom404Page(notFoundResp);

    // Swagger UI documentation page
    // Served at /api/v1/docs - loads embedded Swagger UI bundle from resources
    std::string swaggerHtml = loadHtmlFile("docs.html");
    app.registerHandler(
        "/api/v1/docs",
        [swaggerHtml](const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setBody(swaggerHtml);
            resp->setContentTypeCode(drogon::ContentType::CT_TEXT_HTML);
            resp->addHeader("Cache-Control", "public, max-age=86400");
            resp->setExpiredTime(86400);  // Cache for 24 hours
            callback(resp);
        },
        {drogon::Get});

    LOG_INFO << "Swagger UI Documentation: http://localhost:" << port << "/api/v1/docs";

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
        .addListener("0.0.0.0", static_cast<uint16_t>(port))
        .setThreadNum(2);

    // Companion events: what changes with time alone (a re-take cooldown running out, the first recorded frame of
    // time travel) posts no notification, so the views are compared once a second too; only changes are sent
    app.getLoop()->runEvery(1.0, [] { CompanionEventHub::Instance().Refresh(); });

    // The /api/v1/ws hub runs on the main loop: producers post to it, one timer (armed while somebody subscribes)
    // drives the readouts and topics. No thread of its own
    WsHub::Timer timer;
    timer.arm = [](double seconds, WsHub::Task tick) -> uint64_t {
        return drogon::app().getLoop()->runEvery(seconds, std::move(tick));
    };
    timer.disarm = [](uint64_t id) { drogon::app().getLoop()->invalidateTimer(id); };
    WsHub::Instance().SetLoop([](WsHub::Task task) { drogon::app().getLoop()->queueInLoop(std::move(task)); },
                              std::move(timer));

    try
    {
        app.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR << "WebAPI server failed to start: " << e.what();
        LOG_ERROR << "Port " << port << " may already be in use. WebAPI will be disabled.";
        // Don't exit - just let the thread end gracefully
    }
}