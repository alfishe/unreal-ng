#pragma once

// MCP protocol constants and JSON-RPC 2.0 response builders
// Shared by the dispatcher, tools and the HTTP adapter (automation-mcp.cpp)

#include <json/json.h>

#include <string>

namespace mcp
{

/// region <Constants>

/// MCP protocol revision implemented by this server (Streamable HTTP transport): 2025-06-18 brings structured tool
/// output (structuredContent, no outputSchema here), the MCP-Protocol-Version header on every HTTP request after
/// initialize (an unsupported one is a 400) and no JSON-RPC batching (refused since 2025-03-26)
constexpr const char* kProtocolVersion = "2025-06-18";

/// Protocol revisions accepted from clients during initialize negotiation and in the MCP-Protocol-Version header
constexpr const char* kSupportedProtocolVersions[] = {"2024-11-05", "2025-03-26", "2025-06-18"};

/// The HTTP header a client sends on every request after initialize (2025-06-18); without it the server assumes
/// 2025-03-26
constexpr const char* kProtocolVersionHeader = "MCP-Protocol-Version";

/// An Origin header naming this machine: http(s)://localhost, 127.0.0.1 or [::1], any port. A web page elsewhere
/// (DNS rebinding) and the opaque "null" origin are not local
inline bool IsLocalOrigin(const std::string& origin)
{
    const size_t scheme = origin.find("://");
    if (scheme == std::string::npos)
        return false;
    const std::string kind = origin.substr(0, scheme);
    if (kind != "http" && kind != "https")
        return false;
    std::string host = origin.substr(scheme + 3);
    host = host.substr(0, host.find('/'));
    if (!host.empty() && host[0] == '[')
        host = host.substr(0, host.find(']') + 1);  // [::1]:port
    else
        host = host.substr(0, host.find(':'));
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

inline bool IsSupportedProtocolVersion(const std::string& version)
{
    for (const char* supported : kSupportedProtocolVersions)
        if (version == supported)
            return true;
    return false;
}

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
