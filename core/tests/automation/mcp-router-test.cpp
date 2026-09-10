/// @file mcp-router-test.cpp
/// @brief Unit tests for the MCP router tools search_api / invoke_api (M9).
///
/// Drives the drogon-free router against a scripted FakeApiCaller serving a
/// small OpenAPI fixture. Covered:
///   - search_api keyword scoring and ranking (path-segment hits rank first)
///   - method filter narrows results
///   - matches carry method/path/summary and a ready-to-use body example
///   - auto_invoke executes a unique strong match in one round-trip
///   - invoke_api {id} substitution from the resolved target
///   - invoke_api error propagation (non-2xx → isError with body preserved)

#include <gtest/gtest.h>

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

#include "mcp-router.h"
#include "mcp-tools.h"

namespace
{

class FakeApiCaller : public mcp::IApiCaller
{
public:
    std::map<std::string, std::pair<int, Json::Value>> routes;
    std::pair<int, Json::Value> fallback{200, Json::Value()};
    std::vector<std::pair<std::string, std::string>> calls; // (method, path)

    void Call(const std::string& method, const std::string& path, const Json::Value* body,
              ApiCallback callback) override
    {
        (void)body;
        calls.emplace_back(method, path);
        auto it = routes.find(method + " " + path);
        if (it != routes.end())
        {
            callback(it->second.first, it->second.second);
        }
        else
        {
            callback(fallback.first, fallback.second);
        }
    }

    bool Saw(const std::string& method, const std::string& path) const
    {
        for (const auto& call : calls)
        {
            if (call.first == method && call.second == path)
            {
                return true;
            }
        }
        return false;
    }
};

/// Minimal OpenAPI fixture covering the operations the tests search for
Json::Value MakeOpenApiSpec()
{
    Json::Value spec;
    spec["openapi"] = "3.0.0";
    Json::Value& paths = spec["paths"];

    {
        Json::Value op;
        op["summary"] = "List all running emulator instances";
        op["tags"].append("emulator");
        paths["/api/v1/emulator"]["get"] = op;
    }
    {
        Json::Value op;
        op["summary"] = "Start a paused emulator instance";
        op["tags"].append("emulator");
        paths["/api/v1/emulator/{id}/start"]["post"] = op;
    }
    {
        Json::Value op;
        op["summary"] = "Stop a running emulator instance";
        op["tags"].append("emulator");
        paths["/api/v1/emulator/{id}/stop"]["post"] = op;
    }
    {
        Json::Value op;
        op["summary"] = "Insert a tape and start playback";
        op["tags"].append("tape");
        Json::Value params(Json::arrayValue);
        Json::Value param;
        param["name"] = "turbo";
        params.append(param);
        op["parameters"] = params;
        paths["/api/v1/emulator/{id}/tape/play"]["post"] = op;
    }
    {
        Json::Value op;
        op["summary"] = "Create a new emulator instance";
        op["tags"].append("emulator");
        Json::Value schema;
        schema["type"] = "object";
        schema["example"]["model"] = "128k";
        op["requestBody"]["content"]["application/json"]["schema"] = schema;
        paths["/api/v1/emulator/create"]["post"] = op;
    }
    return spec;
}

/// Runs a tool handler synchronously and returns its result
mcp::ToolResult RunTool(mcp::ToolRegistry& registry, const std::string& name, Json::Value args,
                        mcp::IApiCaller& caller)
{
    const mcp::ToolDefinition* tool = registry.Find(name);
    if (!tool)
    {
        return mcp::ToolResult::Error("tool not registered: " + name);
    }
    mcp::ToolResult result;
    tool->handler(args, caller, [&result](mcp::ToolResult value) { result = std::move(value); },
                  [](double, double, const std::string&) {});
    return result;
}

class McpRouter_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _caller = std::make_shared<FakeApiCaller>();
        _caller->routes["GET /api/v1/openapi.json"] = {200, MakeOpenApiSpec()};

        // Single emulator instance — target:"auto" resolves to emu-1
        // (body shape must match WebAPI: object wrapping the instances array)
        Json::Value emulators(Json::arrayValue);
        Json::Value emulator;
        emulator["id"] = "emu-1";
        emulators.append(emulator);
        Json::Value listBody;
        listBody["emulators"] = std::move(emulators);
        _caller->routes["GET /api/v1/emulator"] = {200, listBody};

        mcp::RegisterRouterTools(_registry, _caller);
    }

    std::shared_ptr<FakeApiCaller> _caller;
    mcp::ToolRegistry _registry;
};

} // namespace

// ===========================================================================
// search_api
// ===========================================================================

TEST_F(McpRouter_Test, SearchApi_RanksPathSegmentMatchFirst)
{
    Json::Value args;
    args["query"] = "emulator start";
    mcp::ToolResult result = RunTool(_registry, "search_api", args, *_caller);

    ASSERT_FALSE(result.isError);
    const Json::Value& matches = result.structured["matches"];
    ASSERT_TRUE(matches.isArray());
    ASSERT_GE(matches.size(), 1u);
    // "start" appears as a path segment → must outrank summary-only matches
    EXPECT_EQ(matches[0]["path"].asString(), "/api/v1/emulator/{id}/start");
    EXPECT_EQ(matches[0]["method"].asString(), "POST");
}

TEST_F(McpRouter_Test, SearchApi_MethodFilterExcludesOtherVerbs)
{
    Json::Value args;
    args["query"] = "emulator";
    args["method"] = "GET";
    mcp::ToolResult result = RunTool(_registry, "search_api", args, *_caller);

    ASSERT_FALSE(result.isError);
    const Json::Value& matches = result.structured["matches"];
    ASSERT_TRUE(matches.isArray());
    ASSERT_GE(matches.size(), 1u);
    for (const auto& match : matches)
    {
        EXPECT_EQ(match["method"].asString(), "GET");
    }
    EXPECT_EQ(matches[0]["path"].asString(), "/api/v1/emulator");
}

TEST_F(McpRouter_Test, SearchApi_NoMatches_ReportsEmpty)
{
    Json::Value args;
    args["query"] = "flux capacitor";
    mcp::ToolResult result = RunTool(_registry, "search_api", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_NE(result.text.find("No WebAPI endpoints match"), std::string::npos);
}

TEST_F(McpRouter_Test, SearchApi_BodyExampleBuiltFromSchemaExample)
{
    Json::Value args;
    args["query"] = "create emulator";
    mcp::ToolResult result = RunTool(_registry, "search_api", args, *_caller);

    ASSERT_FALSE(result.isError);
    const Json::Value& matches = result.structured["matches"];
    bool foundCreate = false;
    for (const auto& match : matches)
    {
        if (match["path"].asString() == "/api/v1/emulator/create")
        {
            foundCreate = true;
            EXPECT_EQ(match["body_example"]["model"].asString(), "128k");
            break;
        }
    }
    EXPECT_TRUE(foundCreate);
}

TEST_F(McpRouter_Test, SearchApi_LimitTruncatesResults)
{
    Json::Value args;
    args["query"] = "emulator";
    args["limit"] = 1;
    mcp::ToolResult result = RunTool(_registry, "search_api", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_EQ(result.structured["matches"].size(), 1u);
    EXPECT_GE(result.structured["total_matches"].asUInt(), 2u);
}

TEST_F(McpRouter_Test, SearchApi_AutoInvokeExecutesUniqueMatch)
{
    // "tape playback" matches only the tape/play operation
    Json::Value args;
    args["query"] = "tape playback";
    args["auto_invoke"] = true;

    mcp::ToolResult result = RunTool(_registry, "search_api", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("POST", "/api/v1/emulator/emu-1/tape/play"));
    EXPECT_TRUE(result.structured.isMember("match"));
    EXPECT_TRUE(result.structured.isMember("invocation"));
    EXPECT_NE(result.text.find("auto-invoked"), std::string::npos);
}

// ===========================================================================
// invoke_api
// ===========================================================================

TEST_F(McpRouter_Test, InvokeApi_SubstitutesTargetIdInPath)
{
    _caller->routes["GET /api/v1/emulator/emu-1/registers"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["method"] = "GET";
    args["path"] = "/api/v1/emulator/{id}/registers";
    mcp::ToolResult result = RunTool(_registry, "invoke_api", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/registers"));
}

TEST_F(McpRouter_Test, InvokeApi_Non2xx_BecomesToolErrorWithBody)
{
    Json::Value errorBody;
    errorBody["error"] = "Not Found";
    errorBody["message"] = "Emulator with specified ID not found";
    _caller->routes["GET /api/v1/emulator/emu-1/badpath"] = {404, errorBody};

    Json::Value args;
    args["method"] = "GET";
    args["path"] = "/api/v1/emulator/{id}/badpath";
    mcp::ToolResult result = RunTool(_registry, "invoke_api", args, *_caller);

    EXPECT_TRUE(result.isError);
    EXPECT_NE(result.text.find("404"), std::string::npos);
}

TEST_F(McpRouter_Test, InvokeApi_MissingMethodOrPath_IsError)
{
    Json::Value args;
    args["path"] = "/api/v1/emulator";
    mcp::ToolResult result = RunTool(_registry, "invoke_api", args, *_caller);
    EXPECT_TRUE(result.isError);
}

// ===========================================================================
// OpenApiCache
// ===========================================================================

TEST_F(McpRouter_Test, OpenApiCache_FetchesOncePerTtl)
{
    mcp::OpenApiCache cache(_caller, std::chrono::hours(1));

    int fetches = 0;
    for (const auto& call : _caller->calls)
    {
        if (call.second == "/api/v1/openapi.json")
        {
            fetches++;
        }
    }

    Json::Value first;
    cache.Get([&first](bool ok, Json::Value spec) { if (ok) first = std::move(spec); });
    Json::Value second;
    cache.Get([&second](bool ok, Json::Value spec) { if (ok) second = std::move(spec); });

    // Recount after the two Get calls
    int fetchesAfter = 0;
    for (const auto& call : _caller->calls)
    {
        if (call.second == "/api/v1/openapi.json")
        {
            fetchesAfter++;
        }
    }
    EXPECT_EQ(fetchesAfter - fetches, 1); // second Get served from cache
    EXPECT_FALSE(first.isNull());
    EXPECT_FALSE(second.isNull());
}

TEST_F(McpRouter_Test, OpenApiCache_InvalidateForcesRefetch)
{
    mcp::OpenApiCache cache(_caller, std::chrono::hours(1));

    cache.Get([](bool, Json::Value) {});
    cache.Invalidate();
    cache.Get([](bool, Json::Value) {});

    int fetches = 0;
    for (const auto& call : _caller->calls)
    {
        if (call.second == "/api/v1/openapi.json")
        {
            fetches++;
        }
    }
    EXPECT_EQ(fetches, 2);
}
