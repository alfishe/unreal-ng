#pragma once

// MCP resources — reference data served to AI agents
//
// Five static resources (keyboard layout, BASIC reference, Z80 ISA, TR-DOS
// commands, memory map) plus the dynamic emulator-state overview. Static
// content is embedded as markdown so no runtime file lookup is needed.

#include "webapi-client.h"

#include <functional>
#include <string>

namespace mcp
{

/// region <McpResources>

class McpResources
{
public:
    /// Payload for the resources/list MCP method
    static Json::Value ListJson();

    /// Reads one resource by URI.
    /// done(ok, contents): ok=true → contents is the {contents:[{uri,mimeType,text}]} payload;
    /// ok=false → contents is an error message for a JSON-RPC -32602 response
    using ReadCallback = std::function<void(bool ok, Json::Value contents)>;

    static void Read(const std::string& uri, IApiCaller& caller, ReadCallback done);
};

/// endregion </McpResources>

} // namespace mcp
