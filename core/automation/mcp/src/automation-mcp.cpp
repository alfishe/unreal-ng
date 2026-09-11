// AutomationMCP — module glue between Automation and the MCP layer
//
// Owns the dispatcher + loopback WebAPI client and registers the /mcp
// endpoint on the shared drogon app instance. This is the only drogon-aware
// file of the MCP layer besides webapi-client.cpp.

#include "automation-mcp.h"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>

#include <json/json.h>

#include <iostream>
#include <memory>
#include <string>

#include "mcp-dispatcher.h"
#include "mcp-protocol.h"
#include "mcp-sse.h"
#include "webapi-client.h"

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

/// region <Port availability>

// Helper function to check if a port is available (same pattern as
// automation-webapi.cpp — fails soft instead of letting drogon exit()).
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

/// endregion </Port availability>

/// region <Methods>

void AutomationMCP::start()
{
    if (_started)
    {
        return;
    }

    const int port = 8092;

    // CRITICAL: Check port availability BEFORE drogon initialization.
    // A busy port only disables MCP — the application continues.
    if (!isPortAvailable(port))
    {
        std::cerr << std::endl;
        std::cerr << "========================================" << std::endl;
        std::cerr << "WARNING: MCP server cannot start" << std::endl;
        std::cerr << "========================================" << std::endl;
        std::cerr << "Port " << port << " is already in use." << std::endl;
        std::cerr << std::endl;
        std::cerr << "The application will continue without MCP tooling." << std::endl;
        std::cerr << std::endl;
        std::cerr << "To use MCP, stop other instances using port " << port << std::endl;
        std::cerr << "========================================" << std::endl;
        std::cerr << std::endl;
        return;
    }

    _caller = std::make_shared<mcp::WebApiClient>();
    _dispatcher = std::make_shared<mcp::McpDispatcher>(mcp::BuildFullRegistry(_caller));

    // Streamable HTTP endpoint, stateless: every request is self-contained
    // (no Mcp-Session-Id). Notifications are acknowledged with 202 + empty
    // body. The global CORS advice registered by WebAPI applies here too.
    //
    // POST answers as an SSE stream only when the client opted in on both
    // ends: Accept includes text/event-stream AND the request carries a
    // params._meta.progressToken (the only case with mid-flight progress
    // worth streaming). Everything else — curl, existing clients, tests —
    // keeps the plain application/json response.
    auto dispatcher = _dispatcher;
    auto caller = _caller;
    drogon::app().registerHandler(
        "/mcp",
        [dispatcher, caller](const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            // Parse the JSON-RPC request body
            Json::Value request;
            std::string parseErrors;
            {
                auto body = req->getBody();
                Json::CharReaderBuilder builder;
                std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
                if (!reader->parse(body.data(), body.data() + body.size(), &request, &parseErrors))
                {
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(mcp::MakeRpcError(
                        Json::Value(), mcp::kParseError,
                        "Parse error" + (parseErrors.empty() ? std::string() : ": " + parseErrors)));
                    resp->setStatusCode(drogon::HttpStatusCode::k400BadRequest);
                    callback(resp);
                    return;
                }
            }

            const bool wantsEventStream =
                req->getHeader("Accept").find("text/event-stream") != std::string::npos;
            const bool hasProgressToken = request.isMember("params") && request["params"].isObject() &&
                                          request["params"].isMember("_meta") &&
                                          request["params"]["_meta"].isObject() &&
                                          request["params"]["_meta"].isMember("progressToken");

            if (wantsEventStream && hasProgressToken)
            {
                // SSE answer: one event per notification, the final event is
                // the JSON-RPC response, then the stream closes. The kickoff
                // timeout stays off — the final frame arrives only after the
                // tool completes.
                auto resp = drogon::HttpResponse::newAsyncStreamResponse(
                    [dispatcher, caller,
                     request = std::move(request)](drogon::ResponseStreamPtr stream) mutable {
                        // shared_ptr: both the notify and the done sink need
                        // the stream; it closes when the last holder dies
                        auto streamShared =
                            std::shared_ptr<drogon::ResponseStream>(std::move(stream));
                        dispatcher->Dispatch(
                            request, *caller,
                            [streamShared](Json::Value response) {
                                if (!response.isNull())
                                {
                                    streamShared->send(mcp::EncodeSseEvent(response));
                                }
                                streamShared->close();
                            },
                            [streamShared](Json::Value notification) {
                                // false on client disconnect — further sends
                                // are no-ops, close() comes with done
                                streamShared->send(mcp::EncodeSseEvent(notification));
                            });
                    },
                    true /* disableKickoffTimeout */);
                resp->setContentTypeString("text/event-stream");
                resp->addHeader("Cache-Control", "no-cache");
                callback(resp);
                return;
            }

            dispatcher->Dispatch(request, *caller,
                                 [callback = std::move(callback)](Json::Value response) mutable {
                                     if (response.isNull())
                                     {
                                         // Notification: accepted, no response body
                                         auto resp = drogon::HttpResponse::newHttpResponse();
                                         resp->setStatusCode(drogon::HttpStatusCode::k202Accepted);
                                         callback(resp);
                                         return;
                                     }
                                     callback(drogon::HttpResponse::newHttpJsonResponse(std::move(response)));
                                 });
        },
        {drogon::Post});

    // GET /mcp — server-initiated event stream, keepalive only in this
    // iteration (reserved for emulator lifecycle events). One ": keepalive"
    // comment every 15 s; the runAfter chain stops itself as soon as send()
    // reports the client gone.
    //
    // HEAD arrives here too — drogon rewrites HEAD to GET before routing and
    // strips the body on send (previousMethod_ keeps isHead() true). Answering
    // 405 tells spec-conformant clients this server has no session lifecycles
    // to probe. DELETE is routed normally below.
    drogon::app().registerHandler(
        "/mcp",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (req->isHead())
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::HttpStatusCode::k405MethodNotAllowed);
                resp->addHeader("Allow", "GET, POST");
                callback(resp);
                return;
            }
            auto resp = drogon::HttpResponse::newAsyncStreamResponse(
                [](drogon::ResponseStreamPtr stream) {
                    auto streamShared = std::shared_ptr<drogon::ResponseStream>(std::move(stream));
                    streamShared->send(mcp::kSseKeepalive); // establish liveness immediately
                    auto ping = std::make_shared<std::function<void()>>();
                    *ping = [streamShared, ping]() {
                        if (streamShared->send(mcp::kSseKeepalive))
                        {
                            drogon::app().getLoop()->runAfter(15.0, [ping]() { (*ping)(); });
                        }
                        else
                        {
                            streamShared->close(); // disconnect — let the chain lapse
                        }
                    };
                    drogon::app().getLoop()->runAfter(15.0, [ping]() { (*ping)(); });
                },
                true /* disableKickoffTimeout */);
            resp->setContentTypeString("text/event-stream");
            resp->addHeader("Cache-Control", "no-cache");
            callback(resp);
        },
        {drogon::Get});

    // Stateless server: no sessions to delete — 405 keeps spec-conformant
    // clients from expecting Mcp-Session-Id behavior
    drogon::app().registerHandler(
        "/mcp",
        [](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::HttpStatusCode::k405MethodNotAllowed);
            resp->addHeader("Allow", "GET, POST");
            callback(resp);
        },
        {drogon::Delete});

    // The listener MUST be added before drogon::app().run() — guaranteed by
    // the Automation::start() ordering (startMCP before startWebAPI).
    // Documented side effect: /mcp is also reachable on the WebAPI port :8090.
    drogon::app().addListener("0.0.0.0", port);

    std::cout << "AutomationMCP started: POST http://localhost:" << port << "/mcp"
              << " (+ GET event stream, protocol " << mcp::kProtocolVersion << ", "
              << _dispatcher->GetRegistry().ToolNames().size() << " tools)" << std::endl;

    _started = true;
}

void AutomationMCP::stop()
{
    if (!_started)
    {
        return;
    }

    // The drogon loop itself belongs to WebAPI (Automation::stop() stops it
    // first). Here we only drop our objects; the registered /mcp handler
    // keeps its own shared copies alive until process exit.
    _dispatcher.reset();
    _caller.reset();
    _started = false;

    std::cout << "AutomationMCP stopped" << std::endl;
}

/// endregion </Methods>
