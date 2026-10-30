#pragma once

// MCP server automation module (see core/automation/mcp/README.md)
//
// Shares the process-global drogon app instance with WebAPI: registers the
// POST /mcp Streamable-HTTP endpoint and adds a dedicated listener on :8092.
// Automation::start() MUST call startMCP() before startWebAPI() — drogon
// listeners must exist before run(). The MCP "thread" is WebAPI's thread; if
// WebAPI is disabled, this module is disabled too (see core/automation/CMakeLists.txt).
//
// Header stays drogon/jsoncpp free so the automation library can include it
// without those include paths.

#include <functional>
#include <memory>
#include <string>

namespace mcp
{
class McpDispatcher;
class WebApiClient;
}

class AutomationMCP
{
    /// region <Fields>
protected:
    bool _started = false;
    std::shared_ptr<mcp::McpDispatcher> _dispatcher;
    std::shared_ptr<mcp::WebApiClient> _caller;
    std::function<void(const std::string& tool, const std::string& action)> _toolCallObserver;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    AutomationMCP() = default;

    // Delete copy and move constructors/operators
    AutomationMCP(const AutomationMCP&) = delete;
    AutomationMCP& operator=(const AutomationMCP&) = delete;
    AutomationMCP(AutomationMCP&&) = delete;
    AutomationMCP& operator=(AutomationMCP&&) = delete;

    ~AutomationMCP() = default;
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    /// Sees every tool call (name, "action" argument); set before start (McpDispatcher::SetToolCallObserver)
    void SetToolCallObserver(std::function<void(const std::string& tool, const std::string& action)> observer)
    {
        _toolCallObserver = std::move(observer);
    }
    void start();
    void stop();
    /// endregion </Methods>
};
