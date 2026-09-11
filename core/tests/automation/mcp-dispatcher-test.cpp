/// @file mcp-dispatcher-test.cpp
/// @brief Unit tests for the MCP JSON-RPC dispatcher (M9).
///
/// The dispatcher, tools, router and resources are drogon-free by design
/// (plan decision 3) — these tests drive them through a synchronous
/// FakeApiCaller without any HTTP machinery. Covered:
///   - initialize handshake (version echo + fallback, capabilities, serverInfo)
///   - ping, notifications (null response)
///   - tools/list: all 11 tools present with valid schema shape
///   - tools/call routing through a real tool handler (dual content)
///   - unknown tool (-32602), unknown method (-32601), batch rejection
///     (-32600), malformed request shape (-32600)
///   - resources/list (6 resources) and resources/read (embedded + dynamic + unknown)
///   - prompts/list empty, prompts/get error

#include <gtest/gtest.h>

#include <json/json.h>

#include <map>
#include <string>

#include "mcp-dispatcher.h"
#include "mcp-protocol.h"

namespace
{

/// Synchronous IApiCaller fake: routes "METHOD path" → scripted response,
/// records every call for endpoint-mapping assertions
class FakeApiCaller : public mcp::IApiCaller
{
public:
    struct RecordedCall
    {
        std::string method;
        std::string path;
        Json::Value body;
    };

    std::map<std::string, std::pair<int, Json::Value>> routes; // "METHOD path" → {status, body}
    std::pair<int, Json::Value> fallback{200, Json::Value()};
    std::vector<RecordedCall> calls;

    void Call(const std::string& method, const std::string& path, const Json::Value* body,
              ApiCallback callback) override
    {
        calls.push_back({method, path, body ? *body : Json::Value()});
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
            if (call.method == method && call.path == path)
            {
                return true;
            }
        }
        return false;
    }
};

Json::Value Rpc(const std::string& method, Json::Value params = Json::Value(), Json::Value id = 1)
{
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["id"] = std::move(id);
    request["method"] = method;
    if (!params.isNull())
    {
        request["params"] = std::move(params);
    }
    return request;
}

/// Dispatches synchronously and returns the full JSON-RPC response
Json::Value DispatchSync(mcp::McpDispatcher& dispatcher, mcp::IApiCaller& caller, const Json::Value& request)
{
    Json::Value response;
    dispatcher.Dispatch(request, caller, [&response](Json::Value value) { response = std::move(value); });
    return response;
}

class McpDispatcher_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _caller = std::make_shared<FakeApiCaller>();
        auto registry = mcp::BuildFullRegistry(_caller);
        _dispatcher = std::make_unique<mcp::McpDispatcher>(std::move(registry));

        // Default emulator list — target:"auto" resolves to a single instance.
        // Shape must match WebAPI: an object wrapping the instances array.
        Json::Value emulators(Json::arrayValue);
        Json::Value emulator;
        emulator["id"] = "emu-1";
        emulator["state"] = "paused";
        emulators.append(emulator);
        Json::Value listBody;
        listBody["emulators"] = std::move(emulators);
        _caller->routes["GET /api/v1/emulator"] = {200, std::move(listBody)};
    }

    std::shared_ptr<FakeApiCaller> _caller;
    std::unique_ptr<mcp::McpDispatcher> _dispatcher;
};

} // namespace

// ===========================================================================
// initialize / handshake
// ===========================================================================

TEST_F(McpDispatcher_Test, Initialize_EchoesSupportedClientVersion)
{
    Json::Value params;
    params["protocolVersion"] = "2024-11-05";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("initialize", params));

    ASSERT_TRUE(response.isObject());
    EXPECT_EQ(response["id"].asInt(), 1);
    EXPECT_EQ(response["result"]["protocolVersion"].asString(), "2024-11-05");
}

TEST_F(McpDispatcher_Test, Initialize_FallsBackForUnsupportedVersion)
{
    Json::Value params;
    params["protocolVersion"] = "1999-01-01";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("initialize", params));

    EXPECT_EQ(response["result"]["protocolVersion"].asString(), mcp::kProtocolVersion);
}

TEST_F(McpDispatcher_Test, Initialize_AdvertisesToolsAndResources)
{
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("initialize"));

    EXPECT_TRUE(response["result"]["capabilities"]["tools"]["listChanged"].isBool());
    EXPECT_TRUE(response["result"]["capabilities"]["resources"]["subscribe"].isBool());
    EXPECT_EQ(response["result"]["serverInfo"]["name"].asString(), mcp::kServerName);
    EXPECT_FALSE(response["result"]["instructions"].asString().empty());
}

// ===========================================================================
// ping / notifications / request validation
// ===========================================================================

TEST_F(McpDispatcher_Test, Ping_ReturnsEmptyObjectResult)
{
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("ping"));

    EXPECT_EQ(response["result"], Json::Value(Json::objectValue));
}

TEST_F(McpDispatcher_Test, Notification_ProducesNullResponse)
{
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["method"] = "notifications/initialized";

    Json::Value response = DispatchSync(*_dispatcher, *_caller, request);
    EXPECT_TRUE(response.isNull());
}

TEST_F(McpDispatcher_Test, BatchArray_IsRejected)
{
    Json::Value batch(Json::arrayValue);
    batch.append(Rpc("ping"));

    Json::Value response = DispatchSync(*_dispatcher, *_caller, batch);
    EXPECT_EQ(response["error"]["code"].asInt(), mcp::kInvalidRequest);
}

TEST_F(McpDispatcher_Test, MalformedRequest_MissingJsonRpc_IsInvalidRequest)
{
    Json::Value request;
    request["id"] = 7;
    request["method"] = "ping";

    Json::Value response = DispatchSync(*_dispatcher, *_caller, request);
    EXPECT_EQ(response["error"]["code"].asInt(), mcp::kInvalidRequest);
    EXPECT_EQ(response["id"].asInt(), 7);
}

TEST_F(McpDispatcher_Test, UnknownMethod_IsMethodNotFound)
{
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("resources/subscribe"));
    EXPECT_EQ(response["error"]["code"].asInt(), mcp::kMethodNotFound);
}

// ===========================================================================
// tools/list and tools/call
// ===========================================================================

TEST_F(McpDispatcher_Test, ToolsList_ContainsAllElevenTools)
{
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/list"));

    const Json::Value& tools = response["result"]["tools"];
    ASSERT_TRUE(tools.isArray());
    EXPECT_EQ(tools.size(), 11u);

    const char* expected[] = {"emulator_manage",  "load_software",     "control_execution", "inspect_state",
                              "type_input",       "manage_symbols",    "debug_code",
                              "analyze_performance", "capture_media",  "search_api",        "invoke_api"};
    for (const char* name : expected)
    {
        bool found = false;
        for (const auto& tool : tools)
        {
            if (tool["name"].asString() == name)
            {
                found = true;
                EXPECT_FALSE(tool["description"].asString().empty()) << name;
                EXPECT_TRUE(tool["inputSchema"].isObject()) << name;
                EXPECT_EQ(tool["inputSchema"]["type"].asString(), "object") << name;
                break;
            }
        }
        EXPECT_TRUE(found) << "tool missing from tools/list: " << name;
    }
}

TEST_F(McpDispatcher_Test, ToolsCall_RoutesThroughToolHandlerWithDualContent)
{
    Json::Value params;
    params["name"] = "emulator_manage";
    Json::Value arguments;
    arguments["action"] = "list";
    params["arguments"] = arguments;

    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));

    // The handler must have hit the WebAPI emulator-list endpoint
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator"));

    ASSERT_TRUE(response["result"].isObject());
    EXPECT_EQ(response["result"]["content"][0]["type"].asString(), "text");
    EXPECT_FALSE(response["result"]["content"][0]["text"].asString().empty());
    EXPECT_TRUE(response["result"].isMember("structuredContent"));
    EXPECT_FALSE(response["result"].isMember("isError"));
}

TEST_F(McpDispatcher_Test, ToolsCall_UnknownTool_IsInvalidParams)
{
    Json::Value params;
    params["name"] = "no_such_tool";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));
    EXPECT_EQ(response["error"]["code"].asInt(), mcp::kInvalidParams);
}

// ===========================================================================
// resources
// ===========================================================================

TEST_F(McpDispatcher_Test, ResourcesList_ContainsSixResources)
{
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("resources/list"));

    const Json::Value& resources = response["result"]["resources"];
    ASSERT_TRUE(resources.isArray());
    EXPECT_EQ(resources.size(), 6u);

    const char* expectedUris[] = {"unreal://keyboard-layout", "unreal://basic-reference", "unreal://z80-isa",
                                  "unreal://trdos-commands",  "unreal://memory-map",      "unreal://emulator-state"};
    for (const char* uri : expectedUris)
    {
        bool found = false;
        for (const auto& resource : resources)
        {
            if (resource["uri"].asString() == uri)
            {
                found = true;
                EXPECT_FALSE(resource["name"].asString().empty()) << uri;
                EXPECT_FALSE(resource["mimeType"].asString().empty()) << uri;
                break;
            }
        }
        EXPECT_TRUE(found) << "resource missing: " << uri;
    }
}

TEST_F(McpDispatcher_Test, ResourcesRead_EmbeddedResource_ReturnsMarkdownText)
{
    Json::Value params;
    params["uri"] = "unreal://z80-isa";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("resources/read", params));

    const Json::Value& contents = response["result"]["contents"];
    ASSERT_TRUE(contents.isArray());
    EXPECT_EQ(contents[0]["uri"].asString(), "unreal://z80-isa");
    EXPECT_EQ(contents[0]["mimeType"].asString(), "text/markdown");
    EXPECT_FALSE(contents[0]["text"].asString().empty());
}

TEST_F(McpDispatcher_Test, ResourcesRead_DynamicResource_FetchesEmulatorList)
{
    Json::Value params;
    params["uri"] = "unreal://emulator-state";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("resources/read", params));

    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator"));
    EXPECT_FALSE(response["result"]["contents"][0]["text"].asString().empty());
}

TEST_F(McpDispatcher_Test, ResourcesRead_UnknownUri_IsInvalidParams)
{
    Json::Value params;
    params["uri"] = "unreal://does-not-exist";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("resources/read", params));
    EXPECT_EQ(response["error"]["code"].asInt(), mcp::kInvalidParams);
}

TEST_F(McpDispatcher_Test, ResourcesRead_MissingUri_IsInvalidParams)
{
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("resources/read"));
    EXPECT_EQ(response["error"]["code"].asInt(), mcp::kInvalidParams);
}

// ===========================================================================
// progress notifications (params._meta.progressToken → notify sink)
// ===========================================================================

namespace
{

/// Registry with one tool that reports two progress steps before finishing
std::unique_ptr<mcp::ToolRegistry> ProgressTestRegistry()
{
    auto registry = std::make_unique<mcp::ToolRegistry>();
    registry->Register(
        "progress_tool", "emits two progress notifications, then a result", Json::Value(Json::objectValue),
        [](const Json::Value& args, mcp::IApiCaller& caller, mcp::ToolCallback done, const mcp::ProgressFn& progress) {
            (void)args;
            (void)caller;
            progress(1.0, 3.0, "step one");
            progress(2.0, 3.0, "step two");
            done(mcp::ToolResult::Ok("finished"));
        });
    return registry;
}

} // namespace

TEST(McpDispatcher_Progress_Test, ProgressToken_PlumbsNotificationsToSink)
{
    mcp::McpDispatcher dispatcher(ProgressTestRegistry());
    FakeApiCaller caller;

    Json::Value params;
    params["name"] = "progress_tool";
    params["_meta"]["progressToken"] = 42;

    std::vector<Json::Value> notifications;
    Json::Value response;
    dispatcher.Dispatch(
        Rpc("tools/call", params), caller, [&response](Json::Value value) { response = std::move(value); },
        [&notifications](Json::Value notification) { notifications.push_back(std::move(notification)); });

    ASSERT_EQ(notifications.size(), 2u);
    EXPECT_EQ(notifications[0]["method"].asString(), "notifications/progress");
    EXPECT_EQ(notifications[0]["params"]["progressToken"].asInt(), 42); // token echoed verbatim
    EXPECT_EQ(notifications[0]["params"]["progress"].asDouble(), 1.0);
    EXPECT_EQ(notifications[0]["params"]["total"].asDouble(), 3.0);
    EXPECT_EQ(notifications[0]["params"]["message"].asString(), "step one");
    EXPECT_EQ(notifications[1]["params"]["progress"].asDouble(), 2.0);

    // Notifications never replace the final result
    EXPECT_EQ(response["result"]["content"][0]["text"].asString(), "finished");
    EXPECT_FALSE(response["result"].isMember("isError"));
}

TEST(McpDispatcher_Progress_Test, WithoutProgressToken_EmitsNothing)
{
    mcp::McpDispatcher dispatcher(ProgressTestRegistry());
    FakeApiCaller caller;

    Json::Value params;
    params["name"] = "progress_tool";

    std::vector<Json::Value> notifications;
    Json::Value response;
    dispatcher.Dispatch(
        Rpc("tools/call", params), caller, [&response](Json::Value value) { response = std::move(value); },
        [&notifications](Json::Value notification) { notifications.push_back(std::move(notification)); });

    EXPECT_TRUE(notifications.empty()); // tool still ran to completion
    EXPECT_EQ(response["result"]["content"][0]["text"].asString(), "finished");
}

TEST(McpDispatcher_Progress_Test, LegacyDispatchWithoutSink_StillWorks)
{
    mcp::McpDispatcher dispatcher(ProgressTestRegistry());
    FakeApiCaller caller;

    Json::Value params;
    params["name"] = "progress_tool";
    params["_meta"]["progressToken"] = 7;

    Json::Value response = DispatchSync(dispatcher, caller, Rpc("tools/call", params));

    // No notify sink → progress dropped, result delivered as before
    EXPECT_EQ(response["result"]["content"][0]["text"].asString(), "finished");
}

// ===========================================================================
// prompts
// ===========================================================================

TEST_F(McpDispatcher_Test, PromptsList_IsEmpty)
{
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("prompts/list"));
    EXPECT_EQ(response["result"]["prompts"], Json::Value(Json::arrayValue));
}

TEST_F(McpDispatcher_Test, PromptsGet_IsError)
{
    Json::Value params;
    params["name"] = "anything";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("prompts/get", params));
    EXPECT_EQ(response["error"]["code"].asInt(), mcp::kInvalidParams);
}
