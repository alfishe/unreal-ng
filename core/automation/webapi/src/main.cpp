#include "hello_world_api.h"

int main(int argc, char **argv)
{
    LOG_INFO << "Starting server on port 8090.";
    LOG_INFO << "HTTP API example: http://localhost:8090/api/v1/HelloWorld";
    LOG_INFO << "WebSocket endpoint: ws://localhost:8090/api/v1/websocket";


    // Note: Drogon automatically register controllers like EmulatorWebSocket
    // if they use macros like WS_PATH_ADD and .cpp file linked.
    drogon::app()
        // .setLogPath("./")
        .setLogPath("") // Empty path disables logging
        .setMaxFiles(5)
        .setLogLevel(trantor::Logger::kNumberOfLogLevels)
        .addListener("0.0.0.0", 8090)
        .setThreadNum(8)
        .run();
}