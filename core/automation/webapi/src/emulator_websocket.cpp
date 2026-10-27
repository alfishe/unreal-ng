#include "emulator_websocket.h"

#include <drogon/HttpAppFramework.h>

#include "common/companion_helper.h"
#include "companion-event-hub.h"
#include "emulator/monitoring/clientstats.h"
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
    monitoring::ClientStats::Instance().Closed(monitoring::ClientPlane::DebugWs);
    DebugEventHub::Instance().RemoveClient(wsConnPtr.get());
}

void EmulatorWebSocket::handleNewConnection(const drogon::HttpRequestPtr& req, const drogon::WebSocketConnectionPtr&)
{
    monitoring::ClientStats::Instance().Opened(monitoring::ClientPlane::DebugWs);  // host.clients
    LOG_INFO << "WebSocket connection from " << req->getPeerAddr().toIpPort();
}

void CompanionWebSocket::handleNewMessage(const drogon::WebSocketConnectionPtr& wsConnPtr, std::string&& message,
                                          const drogon::WebSocketMessageType& type)
{
    if (type != drogon::WebSocketMessageType::Text)
        return;
    CompanionEventHub::Instance().HandleMessage(wsConnPtr.get(), message);
}

void CompanionWebSocket::handleConnectionClosed(const drogon::WebSocketConnectionPtr& wsConnPtr)
{
    monitoring::ClientStats::Instance().Closed(monitoring::ClientPlane::Events);
    CompanionEventHub::Instance().Remove(wsConnPtr.get());
}

void CompanionWebSocket::handleNewConnection(const drogon::HttpRequestPtr& req,
                                             const drogon::WebSocketConnectionPtr& wsConnPtr)
{
    // host.clients: counted before any refusal, as the close that follows one is counted
    monitoring::ClientStats::Instance().Opened(monitoring::ClientPlane::Events);
    // An agent never acts as a companion (as on HTTP: companion_not_for_agents)
    if (req->getHeader("X-Unreal-Source") == "mcp")
    {
        wsConnPtr->send(R"({"op":"error","error":"companion_not_for_agents","message":"An MCP agent does not )"
                        R"(act as a companion"})");
        wsConnPtr->forceClose();
        return;
    }
    // Weak references: the hub does not keep a closed connection alive
    std::weak_ptr<drogon::WebSocketConnection> weak = wsConnPtr;
    CompanionEventHub::Instance().Open(
        wsConnPtr.get(), webapi_companion::BearerOf(req).value_or(""),
        [weak](const std::string& text) {
            if (auto connection = weak.lock(); connection && connection->connected())
                connection->send(text);
        },
        [weak] {
            if (auto connection = weak.lock())
                connection->shutdown(drogon::CloseCode::kNormalClosure);
        });
}
}  // namespace v1
}  // namespace api
