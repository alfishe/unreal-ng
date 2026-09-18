#pragma once

// MCP router tools — search_api + invoke_api
//
// The Universal Router exposes the full WebAPI surface (100+ endpoints) without
// paying static context for it: search_api finds endpoints by keyword against
// the live OpenAPI spec; invoke_api executes any endpoint with automatic {id}
// injection from the resolved target.

#include "mcp-tools.h"

#include <chrono>
#include <mutex>

namespace mcp
{

/// region <OpenApiCache>

/// TTL cache around GET /api/v1/openapi.json (parsed once per interval)
class OpenApiCache
{
public:
    using SpecCallback = std::function<void(bool ok, Json::Value spec)>;

    explicit OpenApiCache(IApiCaller::Ptr caller, std::chrono::steady_clock::duration ttl = std::chrono::minutes(5));

    /// Delivers a copy of the spec, fetching a fresh one when the cache is cold/expired
    void Get(SpecCallback done);

    /// Drops the cached spec (next Get re-fetches)
    void Invalidate();

private:
    IApiCaller::Ptr _caller;
    std::chrono::steady_clock::duration _ttl;
    std::mutex _mutex;
    Json::Value _spec;
    std::chrono::steady_clock::time_point _fetchedAt;
    bool _hasSpec = false;
};

/// endregion </OpenApiCache>

/// region <Router tools>

/// Registers search_api and invoke_api into the registry
void RegisterRouterTools(ToolRegistry& registry, IApiCaller::Ptr caller);

/// endregion </Router tools>

} // namespace mcp
