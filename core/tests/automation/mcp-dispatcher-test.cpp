/// @file mcp-dispatcher-test.cpp
/// @brief Unit tests for the MCP JSON-RPC dispatcher (M9).
///
/// The dispatcher, tools, router and resources are drogon-free by design
/// (plan decision 3) — these tests drive them through a synchronous
/// FakeApiCaller without any HTTP machinery. Covered:
///   - initialize handshake (version echo + fallback, capabilities, serverInfo)
///   - ping, notifications (null response)
///   - tools/list: all 13 tools present with valid schema shape
///   - tools/call routing through a real tool handler (dual content)
///   - unknown tool (-32602), unknown method (-32601), batch rejection
///     (-32600), malformed request shape (-32600)
///   - resources/list (6 resources) and resources/read (embedded + dynamic + unknown)
///   - prompts/list empty, prompts/get error

#include <gtest/gtest.h>

#include <json/json.h>

#include <map>
#include <memory>
#include <utility>
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
    std::string session;          // the automation session the tools set (take_control / release_control)
    bool revokeOnNextCheck = false;  // the user takes control back during the next call

    void SetSession(const std::string& sessionId) override { session = sessionId; }
    std::string Session() const override { return session; }
    bool TakeRevoked() override { return std::exchange(revokeOnNextCheck, false); }
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

    void CallRaw(const std::string& method, const std::string& path, const std::vector<uint8_t>& body,
                 const std::map<std::string, std::string>& headers, ApiCallback callback) override
    {
        // Raw uploads are not exercised by the dispatcher tests - record and
        // answer from the same scripted routes as Call
        calls.push_back({method, path, Json::Value()});
        (void)body;
        (void)headers;
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

// 2025-06-18 (structured tool output, the MCP-Protocol-Version header) is the current revision and accepted
TEST_F(McpDispatcher_Test, Initialize_Speaks2025_06_18)
{
    Json::Value params;
    params["protocolVersion"] = "2025-06-18";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("initialize", params));

    EXPECT_EQ(response["result"]["protocolVersion"].asString(), "2025-06-18");
    EXPECT_STREQ(mcp::kProtocolVersion, "2025-06-18");
    EXPECT_TRUE(mcp::IsSupportedProtocolVersion("2025-03-26")) << "older clients keep working";
    EXPECT_FALSE(mcp::IsSupportedProtocolVersion("2099-01-01"));
}

// Origin check (DNS rebinding): only this machine's web origins may drive the server
TEST(McpProtocol_Test, LocalOrigins)
{
    for (const char* local : {"http://localhost", "http://localhost:3000", "https://127.0.0.1:8295", "http://[::1]:5173"})
        EXPECT_TRUE(mcp::IsLocalOrigin(local)) << local;
    for (const char* foreign : {"http://evil.example", "http://localhost.evil.example", "http://127.0.0.1.nip.io",
                                "null", "file://", "http://192.168.1.5:8080", "ftp://localhost"})
        EXPECT_FALSE(mcp::IsLocalOrigin(foreign)) << foreign;
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

TEST_F(McpDispatcher_Test, ToolsList_ContainsAllTwentyThreeTools)
{
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/list"));

    const Json::Value& tools = response["result"]["tools"];
    ASSERT_TRUE(tools.isArray());
    EXPECT_EQ(tools.size(), 23u);

    const char* expected[] = {"emulator_manage",  "load_software",     "control_execution", "inspect_state",
                              "type_input",       "mouse_input",       "joystick_input",    "time_travel",       "manage_symbols",    "asm_source",
                              "debug_code",       "analyze_performance", "capture_media",  "search_api",
                              "invoke_api",       "media",             "rzx_playback",      "memory_access",     "bus_analyzer",
                              "companion_manage", "controllers",       "actions",           "streamdeck"};
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

// By default content[] holds the human-readable text only; response_format "json" adds the structuredContent
// serialized as JSON text (MCP 2025-06-18) for clients that hand the model only content[] (claude.ai connectors)
TEST_F(McpDispatcher_Test, ToolsCall_DefaultReplyIsTextOnly)
{
    Json::Value params;
    params["name"] = "emulator_manage";
    params["arguments"]["action"] = "list";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));

    ASSERT_EQ(response["result"]["content"].size(), 1u) << response.toStyledString();
    EXPECT_TRUE(response["result"].isMember("structuredContent")) << "programs still get the structured result";
}

TEST_F(McpDispatcher_Test, ToolsCall_ResponseFormatJsonEchoesStructuredContentAsText)
{
    Json::Value params;
    params["name"] = "emulator_manage";
    params["arguments"]["action"] = "list";
    params["arguments"]["response_format"] = "json";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));

    const Json::Value& content = response["result"]["content"];
    ASSERT_EQ(content.size(), 2u);
    EXPECT_EQ(content[1]["type"].asString(), "text");
    Json::Value echoed;
    Json::CharReaderBuilder reader;
    std::string errors;
    const std::string text = content[1]["text"].asString();
    std::unique_ptr<Json::CharReader> parser(reader.newCharReader());
    ASSERT_TRUE(parser->parse(text.data(), text.data() + text.size(), &echoed, &errors)) << errors;
    EXPECT_EQ(echoed, response["result"]["structuredContent"]);
}

TEST_F(McpDispatcher_Test, ToolsCall_BadResponseFormatIsAToolError)
{
    Json::Value params;
    params["name"] = "emulator_manage";
    params["arguments"]["action"] = "list";
    params["arguments"]["response_format"] = "xml";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));

    ASSERT_TRUE(response["result"]["isError"].asBool()) << response.toStyledString();
    EXPECT_NE(response["result"]["content"][0]["text"].asString().find("response_format"), std::string::npos);
}

TEST_F(McpDispatcher_Test, ToolsList_EveryToolTakesResponseFormat)
{
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/list"));
    const Json::Value& tools = response["result"]["tools"];
    ASSERT_GT(tools.size(), 0u);
    for (const Json::Value& tool : tools)
        EXPECT_TRUE(tool["inputSchema"]["properties"].isMember("response_format")) << tool["name"].asString();
}

// A screenshot with include_image comes as an MCP image block (the model sees the picture); the JSON echo does not
// repeat the base64
TEST_F(McpDispatcher_Test, CaptureMediaScreenshot_PixelsAreAnImageBlock)
{
    Json::Value body;
    body["status"] = "success";
    body["format"] = "png";
    body["area"] = "full";
    body["width"] = 352;
    body["height"] = 288;
    body["size"] = 8;
    body["data"] = "iVBORw0KGgo=";
    _caller->routes["GET /api/v1/emulator/emu-1/capture/screen"] = {200, body};

    Json::Value params;
    params["name"] = "capture_media";
    params["arguments"]["action"] = "screenshot";
    params["arguments"]["include_image"] = true;
    params["arguments"]["response_format"] = "json";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));

    ASSERT_FALSE(response["result"].isMember("isError")) << response.toStyledString();
    const Json::Value& content = response["result"]["content"];
    ASSERT_EQ(content.size(), 3u) << response.toStyledString();
    EXPECT_EQ(content[1]["type"].asString(), "image");
    EXPECT_EQ(content[1]["mimeType"].asString(), "image/png");
    EXPECT_EQ(content[1]["data"].asString(), "iVBORw0KGgo=");
    EXPECT_EQ(content[2]["text"].asString().find("iVBORw0KGgo="), std::string::npos) << "no base64 in the text";
    EXPECT_EQ(response["result"]["structuredContent"]["data"].asString(), "iVBORw0KGgo=");
}

// debug_code disassemble writes the listing into the text itself (one line per instruction, labels, targets) and
// does not repeat the JSON
TEST_F(McpDispatcher_Test, DebugCodeDisassemble_ListingInTheText)
{
    Json::Value body;
    body["address"] = 0x7396;
    body["count"] = 3;
    Json::Value call;
    call["address"] = 0x7396;
    call["bytes"] = "CD0074";
    call["mnemonic"] = "call #7400";
    call["size"] = 3;
    call["label"] = "engine";
    call["target"] = 0x7400;
    call["targetLabel"] = "draw";
    body["instructions"].append(call);
    Json::Value load;
    load["address"] = 0x7399;
    load["bytes"] = "DD7E05";
    load["mnemonic"] = "ld a,(ix+5)";
    load["size"] = 3;
    load["effectiveAddress"] = 0x5C05;
    body["instructions"].append(load);
    Json::Value ret;
    ret["address"] = 0x739C;
    ret["bytes"] = "C9";
    ret["mnemonic"] = "ret";
    ret["size"] = 1;
    body["instructions"].append(ret);
    _caller->routes["GET /api/v1/emulator/emu-1/disasm?count=3&address=0x7396"] = {200, body};

    Json::Value params;
    params["name"] = "debug_code";
    params["arguments"]["action"] = "disassemble";
    params["arguments"]["address"] = "0x7396";
    params["arguments"]["count"] = 3;
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));

    ASSERT_FALSE(response["result"].isMember("isError")) << response.toStyledString();
    const Json::Value& content = response["result"]["content"];
    ASSERT_EQ(content.size(), 1u);
    EXPECT_EQ(content[0]["text"].asString(),
              "Disassembly [target emu-1]: 3 instruction(s) at 0x7396\n"
              "engine:\n"
              "7396  CD 00 74      call #7400            ; -> draw\n"
              "7399  DD 7E 05      ld a,(ix+5)           ; ea #5C05\n"
              "739C  C9            ret");
    EXPECT_EQ(response["result"]["structuredContent"], body);
}

// A WebAPI error stays an error with the WebAPI message (no listing, no echo)
TEST_F(McpDispatcher_Test, DebugCodeDisassemble_BadAddressIsAnError)
{
    Json::Value error;
    error["error"] = "Bad Request";
    error["message"] = "address must be 0-65535";
    _caller->routes["GET /api/v1/emulator/emu-1/disasm?count=16&address=zz"] = {400, error};

    Json::Value params;
    params["name"] = "debug_code";
    params["arguments"]["action"] = "disassemble";
    params["arguments"]["address"] = "zz";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));

    EXPECT_TRUE(response["result"]["isError"].asBool());
    EXPECT_NE(response["result"]["content"][0]["text"].asString().find("address must be"), std::string::npos);
}

// inspect_state takes the address as debug_code does: "0x11CB" threw "Value is not convertible to UInt". A mnemonic that
// names its target label itself gets no "-> label" note
TEST_F(McpDispatcher_Test, InspectStateDisasm_HexStringAddress)
{
    Json::Value body;
    body["address"] = 0x0005;
    Json::Value jump;
    jump["address"] = 0x0005;
    jump["bytes"] = "C3CB11";
    jump["mnemonic"] = "jp START-NEW (#11CB)";
    jump["size"] = 3;
    jump["target"] = 0x11CB;
    jump["targetLabel"] = "START-NEW";
    body["instructions"].append(jump);
    _caller->routes["GET /api/v1/emulator/emu-1/disasm?count=1&address=5"] = {200, body};

    Json::Value params;
    params["name"] = "inspect_state";
    params["arguments"]["aspects"].append("disasm");
    params["arguments"]["address"] = "0x0005";
    params["arguments"]["count"] = "1";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));

    ASSERT_FALSE(response["result"].isMember("isError")) << response.toStyledString();
    const std::string text = response["result"]["content"][0]["text"].asString();
    EXPECT_NE(text.find("0005  C3 CB 11      jp START-NEW (#11CB)"), std::string::npos) << text;
    EXPECT_EQ(text.find("; -> START-NEW"), std::string::npos) << text;

    params["arguments"]["address"] = "zz";
    response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));
    EXPECT_TRUE(response["result"]["isError"].asBool());
    EXPECT_NE(response["result"]["content"][0]["text"].asString().find("address (0-65535)"), std::string::npos);
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

TEST_F(McpDispatcher_Test, ResourcesList_ContainsNineResources)
{
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("resources/list"));

    const Json::Value& resources = response["result"]["resources"];
    ASSERT_TRUE(resources.isArray());
    EXPECT_EQ(resources.size(), 9u);

    const char* expectedUris[] = {"unreal://keyboard-layout", "unreal://basic-reference", "unreal://z80-isa",
                                  "unreal://trdos-commands",  "unreal://memory-map",      "unreal://emulator-state",
                                  "unreal://machine/profi",   "unreal://machine/tsconf",  "unreal://machine/sprinter"};
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

TEST_F(McpDispatcher_Test, ResourcesRead_MachineProfi_DescribesPortsAndVideoModes)
{
    Json::Value params;
    params["uri"] = "unreal://machine/profi";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("resources/read", params));

    const Json::Value& contents = response["result"]["contents"];
    ASSERT_TRUE(contents.isArray());
    ASSERT_EQ(contents.size(), 1u);
    const std::string text = contents[0]["text"].asString();
    EXPECT_NE(text.find("#DFFD"), std::string::npos);
    EXPECT_NE(text.find("PROFIHR"), std::string::npos);
}

TEST_F(McpDispatcher_Test, ResourcesRead_MachineTsConf_DescribesRegistersAndSpg)
{
    Json::Value params;
    params["uri"] = "unreal://machine/tsconf";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("resources/read", params));

    const Json::Value& contents = response["result"]["contents"];
    ASSERT_TRUE(contents.isArray());
    ASSERT_EQ(contents.size(), 1u);
    const std::string text = contents[0]["text"].asString();
    EXPECT_NE(text.find("#nnAF"), std::string::npos);
    EXPECT_NE(text.find(".spg"), std::string::npos);
}

TEST_F(McpDispatcher_Test, ResourcesRead_MachineSprinter_DescribesPortTableAndBios)
{
    Json::Value params;
    params["uri"] = "unreal://machine/sprinter";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("resources/read", params));

    const Json::Value& contents = response["result"]["contents"];
    ASSERT_TRUE(contents.isArray());
    ASSERT_EQ(contents.size(), 1u);
    const std::string text = contents[0]["text"].asString();
    EXPECT_NE(text.find("sprinter/bios"), std::string::npos) << "the BIOS is selected through the API";
    EXPECT_NE(text.find("sprinter_ports"), std::string::npos);
    EXPECT_NE(text.find("page #40"), std::string::npos);
    EXPECT_NE(text.find("sprinter_video"), std::string::npos);
    EXPECT_EQ(text.find("TTD refuses"), std::string::npos) << "stale: TTD records the Sprinter since S7";
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

/// Automation sessions (execution arbiter design §6a): take_control keeps the session for every later call
TEST_F(McpDispatcher_Test, TakeControlKeepsTheSessionAndReleaseControlDropsIt)
{
    Json::Value list;
    list["count"] = 1;
    Json::Value e;
    e["id"] = "e1";
    list["emulators"].append(e);
    _caller->routes["GET /api/v1/emulator"] = {200, list};
    Json::Value opened;
    opened["session"] = "11111111-2222-4333-8444-555555555555";
    _caller->routes["POST /api/v1/emulator/e1/control/session"] = {200, opened};

    Json::Value params;
    params["name"] = "control_execution";
    params["arguments"]["action"] = "take_control";
    params["arguments"]["client"] = "test agent";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));
    ASSERT_TRUE(response["result"].isObject());
    EXPECT_FALSE(response["result"].isMember("isError")) << response.toStyledString();
    EXPECT_EQ(_caller->session, "11111111-2222-4333-8444-555555555555");
    bool sawBody = false;
    for (const auto& call : _caller->calls)
        if (call.method == "POST" && call.path == "/api/v1/emulator/e1/control/session")
            sawBody = call.body["surface"].asString() == "mcp" && call.body["client"].asString() == "test agent";
    EXPECT_TRUE(sawBody);

    params["arguments"]["action"] = "release_control";
    DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));
    EXPECT_TRUE(_caller->session.empty());
}

/// The user takes control back while a tool runs: whatever the tool made of the 409, the agent gets one final error
/// that tells it to stop and not retry
TEST_F(McpDispatcher_Test, ARevokedSessionTurnsTheResultIntoAStopError)
{
    _caller->revokeOnNextCheck = true;
    Json::Value params;
    params["name"] = "emulator_manage";
    params["arguments"]["action"] = "list";
    Json::Value response = DispatchSync(*_dispatcher, *_caller, Rpc("tools/call", params));
    ASSERT_TRUE(response["result"].isObject());
    EXPECT_TRUE(response["result"]["isError"].asBool());
    const std::string text = response["result"]["content"][0]["text"].asString();
    EXPECT_NE(text.find("revoked_by_user"), std::string::npos) << text;
    EXPECT_NE(text.find("Do not retry"), std::string::npos) << text;
}
