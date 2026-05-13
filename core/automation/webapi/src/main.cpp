#include "hello_world_api.h"
#include "emulator_api.h"

int main(int argc, char **argv)
{
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

    // Note: Drogon automatically register controllers like EmulatorWebSocket
    // if they use macros like WS_PATH_ADD and .cpp file linked.
    app.setLogPath("") // Empty path disables logging
        .setMaxFiles(5)
        .setLogLevel(trantor::Logger::kNumberOfLogLevels)
        .addListener("0.0.0.0", 8090)
        .setThreadNum(8)
        .run();
}