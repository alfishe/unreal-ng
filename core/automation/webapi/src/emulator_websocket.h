#pragma once

#include <drogon/WebSocketController.h>

namespace api
{
namespace v1
{
/// /api/v1/websocket: the debugger events channel (protocol §5). Every text message is a subscribe /
/// unsubscribe request handled by DebugEventHub, which also sends this connection its events
class EmulatorWebSocket : public drogon::WebSocketController<EmulatorWebSocket>
{
public:
    void handleNewMessage(const drogon::WebSocketConnectionPtr& wsConnPtr, std::string&& message,
                          const drogon::WebSocketMessageType& type) override;

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& wsConnPtr) override;

    void handleNewConnection(const drogon::HttpRequestPtr& req, const drogon::WebSocketConnectionPtr& wsConnPtr) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/v1/websocket", drogon::Get);
    WS_PATH_LIST_END
};
}  // namespace v1
}  // namespace api
