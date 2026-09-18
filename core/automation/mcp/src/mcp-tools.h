#pragma once

// MCP tool registry and shared tool plumbing
//
// A tool = name + description + JSON inputSchema + async handler. Handlers get
// the raw MCP arguments and an IApiCaller for loopback WebAPI calls, and finish
// with a ToolResult (human-readable text + machine-readable structuredContent).
//
// This header (and mcp-tools.cpp / mcp-router.cpp) is drogon-free by design.

#include "webapi-client.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mcp
{

/// region <ToolResult>

/// Result of a tool invocation — the dual-content MCP pattern
struct ToolResult
{
    std::string text;             // Human/LLM-readable summary (content[0].text)
    Json::Value structured = Json::Value(); // Machine-readable payload (structuredContent), null if none
    bool isError = false;

    static ToolResult Ok(const std::string& text, Json::Value structured = Json::Value())
    {
        ToolResult result;
        result.text = text;
        result.structured = std::move(structured);
        return result;
    }

    static ToolResult Error(const std::string& text)
    {
        ToolResult result;
        result.text = text;
        result.isError = true;
        return result;
    }
};

/// endregion </ToolResult>

/// region <ToolRegistry>

using ToolCallback = std::function<void(ToolResult result)>;

/// Per-call progress sink handed to tool handlers. `progress` must increase
/// monotonically within one call; `total` (0 = unknown) and `message` are
/// optional decorations. Reaches the client as an MCP notifications/progress
/// frame only when the request carried a _meta.progressToken and the
/// transport can carry server-initiated messages (SSE) — otherwise silently
/// dropped.
using ProgressFn = std::function<void(double progress, double total, const std::string& message)>;

/// Handler receives the MCP tool arguments object (may be empty).
///
/// Lifetime contract: `args` stays valid until `done` is invoked, even though
/// the handler itself returns before its asynchronous work completes — the
/// dispatcher keeps the arguments alive for the duration of the call. Handlers
/// must invoke `done` exactly once (success or ToolResult::Error) and must not
/// touch `args` afterwards. `progress` is always callable; handlers that do no
/// multi-step work simply ignore it.
using ToolHandler =
    std::function<void(const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn& progress)>;

struct ToolDefinition
{
    std::string name;
    std::string description;
    Json::Value inputSchema;
    ToolHandler handler;
};

class ToolRegistry
{
public:
    /// Registers a tool; schema must be a JSON Schema object for the tool input
    void Register(const std::string& name, const std::string& description, Json::Value inputSchema, ToolHandler handler);

    const ToolDefinition* Find(const std::string& name) const;

    /// Payload for the tools/list MCP method
    Json::Value ToolListJson() const;

    /// Sorted tool names (for tests)
    std::vector<std::string> ToolNames() const;

private:
    std::map<std::string, ToolDefinition> _tools;
};

/// endregion </ToolRegistry>

/// region <Registry composition>

/// Builds the full registry: 5 core smart tools (Phase 1), Phase-2 smart tools
/// (manage_symbols, debug_code, analyze_performance, capture_media) and the two
/// router tools (search_api, invoke_api).
std::unique_ptr<ToolRegistry> BuildFullRegistry(IApiCaller::Ptr caller);

/// endregion </Registry composition>

} // namespace mcp
