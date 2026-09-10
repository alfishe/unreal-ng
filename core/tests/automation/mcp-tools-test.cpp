/// @file mcp-tools-test.cpp
/// @brief Unit tests for the MCP smart tools' action→endpoint mapping (M9).
///
/// Each tool is driven through a synchronous FakeApiCaller that records every
/// HTTP call, so the tests assert exactly which WebAPI endpoint each action
/// hits and with which body. Covered:
///   - emulator_manage action routing (list/status/start/destroy/create)
///   - load_software extension detection (.sna/.tap+.play/.trd+drive) and rejects
///   - control_execution step/run_frames/breakpoint mapping and body fields
///   - inspect_state aspect fan-out (machine+registers hit two endpoints)
///   - type_input → /keyboard/type body
///   - Phase-2 tools spot checks (assemble, frame_cost, labels, screen digest)
///   - TargetResolver: 0 instances auto-creates, 1 uses it, >1 refuses
///   - dual content shape for a forwarded result

#include <gtest/gtest.h>

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

#include "mcp-tools.h"
#include "target-resolver.h"

namespace
{

class FakeApiCaller : public mcp::IApiCaller
{
public:
    struct RecordedCall
    {
        std::string method;
        std::string path;
        Json::Value body;
    };

    std::map<std::string, std::pair<int, Json::Value>> routes;
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

    const RecordedCall* Last(const std::string& method, const std::string& path) const
    {
        for (auto it = calls.rbegin(); it != calls.rend(); ++it)
        {
            if (it->method == method && it->path == path)
            {
                return &*it;
            }
        }
        return nullptr;
    }
};

mcp::ToolResult RunTool(mcp::ToolRegistry& registry, const std::string& name, Json::Value args, mcp::IApiCaller& caller,
                        const mcp::ProgressFn& progress = mcp::ProgressFn())
{
    const mcp::ToolDefinition* tool = registry.Find(name);
    if (!tool)
    {
        return mcp::ToolResult::Error("tool not registered: " + name);
    }
    const mcp::ProgressFn progressSink = progress ? progress : mcp::ProgressFn([](double, double, const std::string&) {});
    mcp::ToolResult result;
    tool->handler(args, caller, [&result](mcp::ToolResult value) { result = std::move(value); }, progressSink);
    return result;
}

Json::Value EmulatorList(const std::vector<std::string>& ids)
{
    Json::Value emulators(Json::arrayValue);
    for (const auto& id : ids)
    {
        Json::Value emulator;
        emulator["id"] = id;
        emulator["state"] = "paused";
        emulators.append(emulator);
    }
    Json::Value body;
    body["emulators"] = emulators;
    return body;
}

class McpTools_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _caller = std::make_shared<FakeApiCaller>();
        _registry = mcp::BuildFullRegistry(_caller);

        // Default: exactly one emulator → target:"auto" resolves to emu-1
        _caller->routes["GET /api/v1/emulator"] = {200, EmulatorList({"emu-1"})};
    }

    std::shared_ptr<FakeApiCaller> _caller;
    std::unique_ptr<mcp::ToolRegistry> _registry;
};

} // namespace

// ===========================================================================
// emulator_manage
// ===========================================================================

TEST_F(McpTools_Test, EmulatorManage_List_GetsInstanceCollection)
{
    Json::Value args;
    args["action"] = "list";
    mcp::ToolResult result = RunTool(*_registry, "emulator_manage", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator"));
    // Dual content: human text plus machine payload
    EXPECT_FALSE(result.text.empty());
    EXPECT_TRUE(result.structured.isObject());
}

TEST_F(McpTools_Test, EmulatorManage_Status_UsesResolvedId)
{
    _caller->routes["GET /api/v1/emulator/emu-1"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "status";
    RunTool(*_registry, "emulator_manage", args, *_caller);

    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1"));
}

TEST_F(McpTools_Test, EmulatorManage_StartStopDestroy_VerbAndPath)
{
    _caller->routes["POST /api/v1/emulator/emu-1/start"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "start";
    RunTool(*_registry, "emulator_manage", args, *_caller);
    EXPECT_TRUE(_caller->Saw("POST", "/api/v1/emulator/emu-1/start"));

    args["action"] = "destroy";
    _caller->routes["DELETE /api/v1/emulator/emu-1"] = {200, Json::Value(Json::objectValue)};
    RunTool(*_registry, "emulator_manage", args, *_caller);
    EXPECT_TRUE(_caller->Saw("DELETE", "/api/v1/emulator/emu-1"));
}

TEST_F(McpTools_Test, EmulatorManage_Create_PostsStartWithDefaultModel)
{
    Json::Value created;
    created["id"] = "emu-9";
    _caller->routes["POST /api/v1/emulator/start"] = {201, created};

    Json::Value args;
    args["action"] = "create";
    mcp::ToolResult result = RunTool(*_registry, "emulator_manage", args, *_caller);

    ASSERT_FALSE(result.isError);
    const FakeApiCaller::RecordedCall* call = _caller->Last("POST", "/api/v1/emulator/start");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->body["model"].asString(), mcp::TargetResolver::kDefaultAutoCreateModel);
}

// ===========================================================================
// load_software
// ===========================================================================

TEST_F(McpTools_Test, LoadSoftware_Snapshot_PostsSnapshotLoadWithPath)
{
    _caller->routes["POST /api/v1/emulator/emu-1/snapshot/load"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["path"] = "/games/harrier.sna";
    mcp::ToolResult result = RunTool(*_registry, "load_software", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("POST", "/api/v1/emulator/emu-1/snapshot/load"));
    EXPECT_EQ(_caller->Last("POST", "/api/v1/emulator/emu-1/snapshot/load")->body["path"].asString(),
              "/games/harrier.sna");
}

TEST_F(McpTools_Test, LoadSoftware_TapeWithPlay_LoadsThenPlays)
{
    _caller->routes["POST /api/v1/emulator/emu-1/tape/load"] = {200, Json::Value(Json::objectValue)};
    _caller->routes["POST /api/v1/emulator/emu-1/tape/play"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["path"] = "game.tzx";
    args["play"] = true;
    mcp::ToolResult result = RunTool(*_registry, "load_software", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("POST", "/api/v1/emulator/emu-1/tape/load"));
    EXPECT_TRUE(_caller->Saw("POST", "/api/v1/emulator/emu-1/tape/play"));
}

TEST_F(McpTools_Test, LoadSoftware_Disk_UsesDriveParameter)
{
    _caller->routes["POST /api/v1/emulator/emu-1/disk/B/insert"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["path"] = "disk.trd";
    args["drive"] = "B";
    RunTool(*_registry, "load_software", args, *_caller);

    EXPECT_TRUE(_caller->Saw("POST", "/api/v1/emulator/emu-1/disk/B/insert"));
}

TEST_F(McpTools_Test, LoadSoftware_UnknownExtension_IsError)
{
    Json::Value args;
    args["path"] = "movie.mp4";
    mcp::ToolResult result = RunTool(*_registry, "load_software", args, *_caller);

    EXPECT_TRUE(result.isError);
    EXPECT_NE(result.text.find("Unsupported file type"), std::string::npos);
    EXPECT_TRUE(_caller->calls.empty()); // rejected before any HTTP traffic
}

TEST_F(McpTools_Test, LoadSoftware_MissingPath_IsError)
{
    Json::Value args;
    mcp::ToolResult result = RunTool(*_registry, "load_software", args, *_caller);
    EXPECT_TRUE(result.isError);
    EXPECT_NE(result.text.find("Missing 'path'"), std::string::npos);
}

// ===========================================================================
// control_execution
// ===========================================================================

TEST_F(McpTools_Test, ControlExecution_Step_PostsStepEndpoint)
{
    _caller->routes["POST /api/v1/emulator/emu-1/step"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "step";
    RunTool(*_registry, "control_execution", args, *_caller);
    EXPECT_TRUE(_caller->Saw("POST", "/api/v1/emulator/emu-1/step"));
}

TEST_F(McpTools_Test, ControlExecution_RunFrames_SendsFrameCount)
{
    _caller->routes["POST /api/v1/emulator/emu-1/run_frames"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "run_frames";
    args["frames"] = 5;
    RunTool(*_registry, "control_execution", args, *_caller);

    const FakeApiCaller::RecordedCall* call = _caller->Last("POST", "/api/v1/emulator/emu-1/run_frames");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->body["frames"].asUInt(), 5u);
}

TEST_F(McpTools_Test, ControlExecution_BreakpointAdd_PostsBreakpointsWithAddress)
{
    _caller->routes["POST /api/v1/emulator/emu-1/breakpoints"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "bp_add";
    args["address"] = "0x8000";
    RunTool(*_registry, "control_execution", args, *_caller);

    EXPECT_TRUE(_caller->Saw("POST", "/api/v1/emulator/emu-1/breakpoints"));
}

// ===========================================================================
// inspect_state
// ===========================================================================

TEST_F(McpTools_Test, InspectState_RegistersAspect_ReadsRegisters)
{
    Json::Value registers;
    registers["pc"] = 32768;
    _caller->routes["GET /api/v1/emulator/emu-1/registers"] = {200, registers};

    Json::Value args;
    Json::Value aspects(Json::arrayValue);
    aspects.append("registers");
    args["aspects"] = aspects;
    mcp::ToolResult result = RunTool(*_registry, "inspect_state", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/registers"));
    EXPECT_TRUE(result.structured.isMember("registers"));
}

TEST_F(McpTools_Test, InspectState_TwoAspects_FanOutToBothEndpoints)
{
    _caller->routes["GET /api/v1/emulator/emu-1"] = {200, Json::Value(Json::objectValue)};
    _caller->routes["GET /api/v1/emulator/emu-1/registers"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    Json::Value aspects(Json::arrayValue);
    aspects.append("machine");
    aspects.append("registers");
    args["aspects"] = aspects;
    mcp::ToolResult result = RunTool(*_registry, "inspect_state", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1"));
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/registers"));
}

TEST_F(McpTools_Test, InspectState_EmitsOneProgressNotificationPerAspect)
{
    _caller->routes["GET /api/v1/emulator/emu-1"] = {200, Json::Value(Json::objectValue)};
    _caller->routes["GET /api/v1/emulator/emu-1/registers"] = {200, Json::Value(Json::objectValue)};

    struct ProgressEvent
    {
        double progress;
        double total;
        std::string message;
    };
    std::vector<ProgressEvent> events;

    Json::Value args;
    Json::Value aspects(Json::arrayValue);
    aspects.append("machine");
    aspects.append("registers");
    aspects.append("disasm");
    args["aspects"] = aspects;
    mcp::ToolResult result = RunTool(*_registry, "inspect_state", args, *_caller,
                                     [&events](double progress, double total, const std::string& message) {
                                         events.push_back({progress, total, message});
                                     });

    ASSERT_FALSE(result.isError);
    ASSERT_EQ(events.size(), 3u); // one per aspect, in request order
    EXPECT_EQ(events[0].progress, 1.0);
    EXPECT_EQ(events[0].total, 3.0);
    EXPECT_EQ(events[0].message, "machine");
    EXPECT_EQ(events[1].progress, 2.0);
    EXPECT_EQ(events[1].message, "registers");
    EXPECT_EQ(events[2].progress, 3.0);
    EXPECT_EQ(events[2].message, "disasm");
}

// ===========================================================================
// type_input
// ===========================================================================

TEST_F(McpTools_Test, TypeInput_Type_PostsKeyboardTypeWithText)
{
    _caller->routes["POST /api/v1/emulator/emu-1/keyboard/type"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "type";
    args["text"] = "LOAD \"\"";
    RunTool(*_registry, "type_input", args, *_caller);

    EXPECT_TRUE(_caller->Saw("POST", "/api/v1/emulator/emu-1/keyboard/type"));
    EXPECT_EQ(_caller->Last("POST", "/api/v1/emulator/emu-1/keyboard/type")->body["text"].asString(), "LOAD \"\"");
}

// ===========================================================================
// Phase-2 smart tools — endpoint spot checks
// ===========================================================================

TEST_F(McpTools_Test, DebugCode_Assemble_PostsCodeAndAddress)
{
    Json::Value response;
    response["bytes_assembled"] = 4;
    response["start_address"] = "0x8000";
    _caller->routes["POST /api/v1/emulator/emu-1/assemble"] = {200, response};

    Json::Value args;
    args["action"] = "assemble";
    args["code"] = "nop\nld a,1";
    args["address"] = "0x8000";
    mcp::ToolResult result = RunTool(*_registry, "debug_code", args, *_caller);

    ASSERT_FALSE(result.isError);
    const FakeApiCaller::RecordedCall* call = _caller->Last("POST", "/api/v1/emulator/emu-1/assemble");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->body["code"].asString(), "nop\nld a,1");
    EXPECT_EQ(call->body["address"].asString(), "0x8000");
    EXPECT_TRUE(call->body["write"].asBool());
}

TEST_F(McpTools_Test, AnalyzePerformance_FrameCost_GetsEndpoint)
{
    _caller->routes["GET /api/v1/emulator/emu-1/frame_cost"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "frame_cost";
    RunTool(*_registry, "analyze_performance", args, *_caller);

    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/frame_cost"));
}

TEST_F(McpTools_Test, ManageSymbols_List_GetsLabels)
{
    _caller->routes["GET /api/v1/emulator/emu-1/labels"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "list";
    RunTool(*_registry, "manage_symbols", args, *_caller);

    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/labels"));
}

TEST_F(McpTools_Test, CaptureMedia_ScreenDigest_GetsDigestEndpoint)
{
    Json::Value response;
    response["combined"] = "0x1234567890ABCDEF";
    _caller->routes["GET /api/v1/emulator/emu-1/state/screen/digest"] = {200, response};

    Json::Value args;
    args["action"] = "screen_digest";
    mcp::ToolResult result = RunTool(*_registry, "capture_media", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/state/screen/digest"));
}

TEST_F(McpTools_Test, CaptureMedia_BoundedEveryNthRecording_ReportsCapturedFramesProgress)
{
    // One route covers start/pause/resume/stop — FakeApiCaller routes by method+path
    _caller->routes["POST /api/v1/emulator/emu-1/video/record"] = {200, Json::Value(Json::objectValue)};
    _caller->routes["POST /api/v1/emulator/emu-1/run_frames"] = {200, Json::Value(Json::objectValue)};

    std::vector<std::pair<double, std::string>> events;

    Json::Value args;
    args["action"] = "record_start";
    args["frames"] = 3;
    args["every_nth"] = 2; // skip-cycle: capture 1, skip 1 — 3 cycles total
    mcp::ToolResult result = RunTool(*_registry, "capture_media", args, *_caller,
                                     [&events](double progress, double total, const std::string& message) {
                                         events.push_back({progress / total, message});
                                     });

    ASSERT_FALSE(result.isError) << result.text;
    ASSERT_EQ(events.size(), 3u); // one per captured frame (small count → no throttling)
    double previous = 0.0;
    for (const auto& event : events)
    {
        EXPECT_GT(event.first, previous); // monotonically increasing per MCP spec
        EXPECT_NE(event.second.find("captured"), std::string::npos);
        previous = event.first;
    }
    EXPECT_EQ(events.back().first, 1.0); // final report reaches 100%
}

// ===========================================================================
// TargetResolver
// ===========================================================================

TEST_F(McpTools_Test, TargetResolver_SingleInstance_ResolvesToIt)
{
    struct Outcome
    {
        bool ok = false;
        std::string id;
    } outcome;

    mcp::TargetResolver::Resolve("auto", *_caller, [&outcome](bool ok, const std::string& idOrError) {
        outcome.ok = ok;
        outcome.id = idOrError;
    });

    EXPECT_TRUE(outcome.ok);
    EXPECT_EQ(outcome.id, "emu-1");
}

TEST_F(McpTools_Test, TargetResolver_ZeroInstances_AutoCreatesDefault)
{
    _caller->routes["GET /api/v1/emulator"] = {200, EmulatorList({})};
    Json::Value created;
    created["id"] = "emu-auto";
    _caller->routes["POST /api/v1/emulator/start"] = {201, created};

    bool ok = false;
    std::string id;
    mcp::TargetResolver::Resolve("auto", *_caller, [&](bool resolved, const std::string& idOrError) {
        ok = resolved;
        id = idOrError;
    });

    EXPECT_TRUE(ok);
    EXPECT_EQ(id, "emu-auto");
    const FakeApiCaller::RecordedCall* call = _caller->Last("POST", "/api/v1/emulator/start");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->body["model"].asString(), mcp::TargetResolver::kDefaultAutoCreateModel);
}

TEST_F(McpTools_Test, TargetResolver_MultipleInstances_RefusesWithCandidateList)
{
    _caller->routes["GET /api/v1/emulator"] = {200, EmulatorList({"emu-a", "emu-b"})};

    bool ok = true;
    std::string error;
    mcp::TargetResolver::Resolve("auto", *_caller, [&](bool resolved, const std::string& idOrError) {
        ok = resolved;
        error = idOrError;
    });

    EXPECT_FALSE(ok);
    EXPECT_NE(error.find("emu-a, emu-b"), std::string::npos);
}

TEST_F(McpTools_Test, TargetResolver_ExplicitId_ValidatedAgainstInstance)
{
    _caller->routes["GET /api/v1/emulator/emu-1"] = {200, Json::Value(Json::objectValue)};

    bool ok = false;
    std::string id;
    mcp::TargetResolver::Resolve("emu-1", *_caller, [&](bool resolved, const std::string& idOrError) {
        ok = resolved;
        id = idOrError;
    });

    EXPECT_TRUE(ok);
    EXPECT_EQ(id, "emu-1");
}
