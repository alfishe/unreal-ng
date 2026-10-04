#include "emulator_websocket.h"

#include <drogon/HttpAppFramework.h>

#include "debug-event-hub.h"

namespace api
{
namespace v1
{
void EmulatorWebSocket::handleNewMessage(const drogon::WebSocketConnectionPtr& wsConnPtr, std::string&& message,
                                         const drogon::WebSocketMessageType& type)
{
    if (type != drogon::WebSocketMessageType::Text)
        return;  // pings are answered by drogon; binary messages mean nothing here

    // The hub keeps the send function for the events: a weak reference, so a closed connection is not kept
    std::weak_ptr<drogon::WebSocketConnection> weak = wsConnPtr;
    DebugEventHub::Instance().HandleMessage(wsConnPtr.get(), message, [weak](const std::string& text) {
        if (auto connection = weak.lock(); connection && connection->connected())
            connection->send(text);
    });
}

void EmulatorWebSocket::handleConnectionClosed(const drogon::WebSocketConnectionPtr& wsConnPtr)
{
    DebugEventHub::Instance().RemoveClient(wsConnPtr.get());
}

void EmulatorWebSocket::handleNewConnection(const drogon::HttpRequestPtr& req, const drogon::WebSocketConnectionPtr&)
{
    LOG_INFO << "WebSocket connection from " << req->getPeerAddr().toIpPort();
}
}  // namespace v1
}  // namespace api
