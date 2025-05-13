#include "hello_world_api.h"

int main(int argc, char **argv)
{
    LOG_INFO << "Starting server on port 8090.";
    LOG_INFO << "HTTP API example: http://localhost:8090/api/v1/HelloWorld";
    LOG_INFO << "WebSocket endpoint: ws://localhost:8090/api/v1/websocket";


    // Note: Drogon automatically registers controllers like EmulatorWebSocket
    // if they use macros like WS_PATH_ADD and linked.
    drogon::app().setLogPath("./")
        .setLogLevel(trantor::Logger::kNumberOfLogLevels)
        .addListener("0.0.0.0", 8090)
        .setThreadNum(8)
        .run();
}