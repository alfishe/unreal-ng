#pragma once

// JSON-RPC 2.0 / MCP method dispatcher (drogon-free)
//
// Routes one parsed JSON-RPC request object to the matching MCP method:
//   initialize, notifications/* (no-op), ping,
//   tools/list, tools/call, resources/list, resources/read,
//   prompts/list (empty), prompts/get (error)
// Batch arrays are rejected with -32600 (batching was removed in the
// 2025-03-26 spec revision). Notifications produce a null response —
// the HTTP adapter maps that to 202 + empty body.

#include "mcp-tools.h"

#include <functional>
#include <memory>

namespace mcp
{

/// region <McpDispatcher>

class McpDispatcher
{
public:
    /// done(response): full JSON-RPC response object, or null for notifications
    using ResponseCallback = std::function<void(Json::Value response)>;

    /// notify(notification): full JSON-RPC notification object emitted zero or
    /// more times before done — used for notifications/progress when the client
    /// supplied a _meta.progressToken and the transport streams (SSE)
    using NotifyCallback = std::function<void(Json::Value notification)>;

    explicit McpDispatcher(std::unique_ptr<ToolRegistry> registry);

    /// Dispatches one request; invokes done exactly once (async for tools/call
    /// and resources/read — the callback may fire after Dispatch returns).
    /// Progress notifications are dropped (no notify sink).
    void Dispatch(const Json::Value& request, IApiCaller& caller, ResponseCallback done);

    /// Same, with a sink for server-initiated notifications (progress)
    void Dispatch(const Json::Value& request, IApiCaller& caller, ResponseCallback done, const NotifyCallback& notify);

    const ToolRegistry& GetRegistry() const { return *_registry; }

private:
    std::unique_ptr<ToolRegistry> _registry;
};

/// endregion </McpDispatcher>

} // namespace mcp
