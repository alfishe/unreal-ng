#pragma once

/// @file api-websocket.h
/// @brief /api/v1/ws, the WebSocket plane (docs/emulator/design/control-interfaces/websocket-interface.md,
/// docs/guidelines/automation-planes-guidelines.md §3). Calls: every REST route is a call that runs the same handler
/// with the same gates and answers a `result`. Every other op (subscribe / unsubscribe topics, the surface topic's
/// join / action / bye) goes to the shared hub, WsHub (ws-hub.h), which owns topics, readouts and SurfaceHub.
///
///   → {"op":"call","rid":7,"method":"POST","path":"/api/v1/controllers/actions/run.pause_toggle","body":{...}}
///   ← {"op":"result","t":"call","rid":7,"status":200,"body":{...}}
///
/// Auth: a companion token on the handshake (Authorization: Bearer) or {"op":"auth","token":"unc_..."}; each call
/// carries it, so the companion gate decides per call as on HTTP. {"session": "<id>"} in a call carries an automation
/// session (X-Unreal-Session). Threads: messages arrive on the WebAPI's event loops; a call is handed to the same
/// handler drogon runs for HTTP (the slow ones move to their worker pools or the instance's actions worker) and its
/// result is sent back from whichever thread finishes it. No thread per connection.

#include <drogon/WebSocketController.h>

namespace api
{
namespace v1
{
class ApiWebSocket : public drogon::WebSocketController<ApiWebSocket>
{
public:
    /// Calls in flight per connection; one more is answered `busy` (backpressure without queueing)
    static constexpr int kMaxCallsInFlight = 64;

    void handleNewMessage(const drogon::WebSocketConnectionPtr& wsConnPtr, std::string&& message,
                          const drogon::WebSocketMessageType& type) override;

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& wsConnPtr) override;

    void handleNewConnection(const drogon::HttpRequestPtr& req, const drogon::WebSocketConnectionPtr& wsConnPtr) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/v1/ws", drogon::Get);
    WS_PATH_LIST_END
};
}  // namespace v1
}  // namespace api
