#pragma once

// MCP protocol constants and JSON-RPC 2.0 response builders
// Shared by the dispatcher, tools and the HTTP adapter (automation-mcp.cpp)

#include <json/json.h>

#include <string>

namespace mcp
{

/// region <Constants>

/// MCP protocol revision implemented by this server (Streamable HTTP transport)
constexpr const char* kProtocolVersion = "2025-03-26";

/// Protocol revisions accepted from clients during initialize negotiation
constexpr const char* kSupportedProtocolVersions[] = {"2024-11-05", "2025-03-26"};

constexpr const char* kServerName = "unreal-ng";
constexpr const char* kServerVersion = "1.0.0";

// JSON-RPC 2.0 error codes (https://www.jsonrpc.org/specification)
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;

/// endregion </Constants>

/// region <Response builders>

inline Json::Value MakeRpcResult(const Json::Value& id, Json::Value result)
{
    Json::Value response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["result"] = std::move(result);
    return response;
}

inline Json::Value MakeRpcError(const Json::Value& id, int code, const std::string& message)
{
    Json::Value error;
    error["code"] = code;
    error["message"] = message;

    Json::Value response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["error"] = std::move(error);
    return response;
}

/// endregion </Response builders>

} // namespace mcp
