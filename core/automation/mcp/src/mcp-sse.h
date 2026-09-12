#pragma once

// SSE framing + MCP progress notifications (drogon-free)
//
// The Streamable HTTP transport allows POST /mcp answers as Server-Sent
// Events: every JSON-RPC message becomes one SSE event, and long-running
// tools may interleave notifications/progress frames before the final
// response. Kept drogon-free so core-tests can exercise the framing.

#include "mcp-protocol.h"

#include <json/json.h>

#include <string>

namespace mcp
{

/// region <SSE framing>

/// SSE comment frame used as GET-stream keepalive (ignored by all SSE parsers)
constexpr const char* kSseKeepalive = ": keepalive\n\n";

/// Frames one JSON-RPC message as a Server-Sent Event ("message" is the only
/// event name MCP uses on the stream)
inline std::string EncodeSseEvent(const Json::Value& message, const std::string& event = "message")
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    const std::string json = Json::writeString(builder, message);
    return "event: " + event + "\ndata: " + json + "\n\n";
}

/// endregion </SSE framing>

/// region <Progress notifications>

/// JSON-RPC notification object for notifications/progress: params echo the
/// client's progressToken and carry a monotonically increasing progress
/// number plus an optional total and human-readable message (both omitted
/// when 0 / empty, per the 2025-03-26 spec).
inline Json::Value MakeProgressNotification(const Json::Value& progressToken, double progress, double total,
                                            const std::string& message)
{
    Json::Value params;
    params["progressToken"] = progressToken;
    params["progress"] = progress;
    if (total > 0.0)
    {
        params["total"] = total;
    }
    if (!message.empty())
    {
        params["message"] = message;
    }

    Json::Value notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "notifications/progress";
    notification["params"] = std::move(params);
    return notification;
}

/// endregion </Progress notifications>

} // namespace mcp
