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
///   - mouse_input move/status routing, click pre-move sequencing, missing-arg rejects
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

// P0-2: a strict 400 from the WebAPI (non-creatable model) must surface as a
// tool error carrying the reason - the agent learns WHY the model is missing
// instead of silently ending up on a wrong machine.
TEST_F(McpTools_Test, EmulatorManage_Create_FailureSurfacesReason)
{
    Json::Value failure;
    failure["error"] = "Bad Request";
    failure["message"] = "model 'ATM710' is not supported by this build (PortDecoder::GetPortDecoderForModel - unknown model 6)";
    failure["requested_model"] = "ATM710";
    _caller->routes["POST /api/v1/emulator/start"] = {400, failure};

    Json::Value args;
    args["action"] = "create";
    args["model"] = "ATM710";
    mcp::ToolResult result = RunTool(*_registry, "emulator_manage", args, *_caller);

    EXPECT_TRUE(result.isError);
    EXPECT_NE(result.text.find("ATM710"), std::string::npos) << "text was: " << result.text;
    EXPECT_NE(result.text.find("not supported by this build"), std::string::npos) << "text was: " << result.text;
}

// P0-1: the machine identity fields the WebAPI now attaches to every
// lifecycle response must arrive intact in the tool's structured payload.
TEST_F(McpTools_Test, EmulatorManage_List_PassesThroughMachineIdentity)
{
    Json::Value emulators(Json::arrayValue);
    Json::Value instance;
    instance["id"] = "emu-1";
    instance["state"] = "running";
    instance["model"] = "PENTAGON";
    instance["model_full_name"] = "Pentagon";
    instance["ram_kb"] = 128;
    instance["video_mode"] = "Standard";
    instance["speed_multiplier"] = 1;
    instance["config_folder"] = "pentagon128k";
    emulators.append(instance);
    Json::Value body;
    body["emulators"] = emulators;
    _caller->routes["GET /api/v1/emulator"] = {200, body};

    Json::Value args;
    args["action"] = "list";
    mcp::ToolResult result = RunTool(*_registry, "emulator_manage", args, *_caller);

    ASSERT_FALSE(result.isError);
    ASSERT_TRUE(result.structured.isObject());
    ASSERT_TRUE(result.structured.isMember("emulators"));
    ASSERT_TRUE(result.structured["emulators"].isArray());
    ASSERT_EQ(result.structured["emulators"].size(), 1u);
    EXPECT_EQ(result.structured["emulators"][0]["model"].asString(), "PENTAGON");
    EXPECT_EQ(result.structured["emulators"][0]["ram_kb"].asInt(), 128);
    EXPECT_EQ(result.structured["emulators"][0]["config_folder"].asString(), "pentagon128k");
}

// Parity rule: 'server' exposes the same build fingerprint + models_creatable
// block that GET /api/v1/emulator/status serves and the CLI 'status' command
// prints - MCP is an equally important consumer, not a subordinate one.
TEST_F(McpTools_Test, EmulatorManage_Server_ForwardsStatusBlock)
{
    Json::Value status;
    status["server"]["version"] = "1.0.0";
    status["server"]["git_branch"] = "master";
    status["server"]["git_commit"] = "5719e27e";
    status["server"]["build_type"] = "Release";
    status["models_creatable"].append("48K");
    status["models_creatable"].append("PENTAGON");
    _caller->routes["GET /api/v1/emulator/status"] = {200, status};

    Json::Value args;
    args["action"] = "server";
    mcp::ToolResult result = RunTool(*_registry, "emulator_manage", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/status"));
    ASSERT_TRUE(result.structured.isObject());
    EXPECT_EQ(result.structured["server"]["git_branch"].asString(), "master");
    EXPECT_EQ(result.structured["server"]["git_commit"].asString(), "5719e27e");
    ASSERT_TRUE(result.structured["models_creatable"].isArray());
    ASSERT_EQ(result.structured["models_creatable"].size(), 2u);
    EXPECT_EQ(result.structured["models_creatable"][0].asString(), "48K");
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

// TD-3 Phase 1: the memory_map aspect hits GET /memory/map with the
// view/min_run/max_blocks args and the summary names the model + blocks
TEST_F(McpTools_Test, InspectState_MemoryMapAspect_FetchesSparseMap)
{
    Json::Value block;
    block["address"] = "0x0000";
    block["type"] = "rom0";
    block["status"] = "data";
    block["size"] = 16384;
    block["non_zero"] = 16384;
    block["hash"] = "00ABCDEF12345678";
    Json::Value mapBody;
    mapBody["model"] = "Spectrum 48K";
    mapBody["view"] = "address";
    mapBody["total_size"] = 65536;
    mapBody["non_zero_bytes"] = 16384;
    mapBody["block_count"] = 1;
    mapBody["blocks"] = Json::Value(Json::arrayValue);
    mapBody["blocks"].append(block);
    _caller->routes["GET /api/v1/emulator/emu-1/memory/map?view=address&min_run=64&max_blocks=48"] = {200, mapBody};

    Json::Value args;
    Json::Value aspects(Json::arrayValue);
    aspects.append("memory_map");
    args["aspects"] = aspects;
    mcp::ToolResult result = RunTool(*_registry, "inspect_state", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/memory/map?view=address&min_run=64&max_blocks=48"));
    ASSERT_TRUE(result.structured.isMember("memory_map"));
    EXPECT_EQ(result.structured["memory_map"]["block_count"].asUInt(), 1u);
    EXPECT_NE(result.text.find("[memory_map] Spectrum 48K address view: 1 block(s)"), std::string::npos)
        << "text was: " << result.text;
    EXPECT_NE(result.text.find("rom0 data, size 16384"), std::string::npos) << "text was: " << result.text;
}

// TD-3: the memory aspect forwards format=hexdump by default (~80% token cut)
TEST_F(McpTools_Test, InspectState_MemoryAspect_DefaultsToHexdump)
{
    Json::Value memBody;
    memBody["address"] = 0;
    memBody["length"] = 64;
    memBody["format"] = "hexdump";
    memBody["hexdump"] = "0x0000: 3E 21 00  |..!|\n";
    _caller->routes["GET /api/v1/emulator/emu-1/memory/0?len=64&format=hexdump"] = {200, memBody};

    Json::Value args;
    Json::Value aspects(Json::arrayValue);
    aspects.append("memory");
    args["aspects"] = aspects;
    args["address"] = 0;
    mcp::ToolResult result = RunTool(*_registry, "inspect_state", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/memory/0?len=64&format=hexdump"));
    EXPECT_NE(result.text.find("0x0000: 3E 21 00"), std::string::npos) << "text was: " << result.text;
}

// TD-3: the stack aspect still machine-parses data[] via format=full after
// the hexdump default flip on /memory/{addr}
TEST_F(McpTools_Test, InspectState_StackAspect_RequestsFullFormat)
{
    Json::Value registers;
    registers["special"]["sp"] = 0xFF40;
    _caller->routes["GET /api/v1/emulator/emu-1/registers"] = {200, registers};

    Json::Value memory;
    memory["data"] = Json::Value(Json::arrayValue);
    memory["data"].append(0x34);
    memory["data"].append(0x12);
    _caller->routes["GET /api/v1/emulator/emu-1/memory/65344?len=32&format=full"] = {200, memory};

    Json::Value args;
    Json::Value aspects(Json::arrayValue);
    aspects.append("stack");
    args["aspects"] = aspects;
    mcp::ToolResult result = RunTool(*_registry, "inspect_state", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/memory/65344?len=32&format=full"));
    ASSERT_TRUE(result.structured.isMember("stack"));
    ASSERT_TRUE(result.structured["stack"]["words"].isArray());
    ASSERT_EQ(result.structured["stack"]["words"].size(), 1u);
    EXPECT_EQ(result.structured["stack"]["words"][0]["value"].asUInt(), 0x1234u);
}

TEST_F(McpTools_Test, InspectState_DeviceAspects_FetchOverviewAndChips)
{
    // audio_fm: overview, then every chip's full report; fdc: one endpoint.
    // The summaries name the keyed channel and the controller state.
    Json::Value fmOverview;
    fmOverview["available"] = true;
    fmOverview["board"]["selected_chip"] = 1;
    fmOverview["board"]["fm_enabled"] = true;
    fmOverview["chips"] = Json::Value(Json::arrayValue);
    fmOverview["chips"].append(Json::Value(Json::objectValue));
    fmOverview["chips"].append(Json::Value(Json::objectValue));
    Json::Value chip0;
    chip0["mode"]["channel3_mode"] = "extended";
    chip0["keyed_channels"] = 1;
    chip0["sounding_channels"] = 1;
    Json::Value ch;
    ch["sounding"] = true;
    ch["key_on"] = true;
    ch["key_on_mask"] = 8;
    ch["algorithm"] = 7;
    ch["frequency_hz"] = 228.5;
    chip0["channels"] = Json::Value(Json::arrayValue);
    chip0["channels"].append(Json::Value(Json::objectValue));
    chip0["channels"].append(Json::Value(Json::objectValue));
    chip0["channels"].append(ch);
    Json::Value chip1 = chip0;
    chip1["keyed_channels"] = 0;
    chip1["sounding_channels"] = 0;
    chip1["channels"][2]["sounding"] = false;
    chip1["channels"][2]["key_on"] = false;
    _caller->routes["GET /api/v1/emulator/emu-1/state/audio/fm"] = {200, fmOverview};
    _caller->routes["GET /api/v1/emulator/emu-1/state/audio/fm/0"] = {200, chip0};
    _caller->routes["GET /api/v1/emulator/emu-1/state/audio/fm/1"] = {200, chip1};

    Json::Value fdc;
    fdc["available"] = true;
    fdc["fsm_state"] = "S_IDLE";
    fdc["last_command"] = "restore";
    fdc["registers"]["status"] = 0x24;
    fdc["registers"]["track"] = 0;
    fdc["registers"]["sector"] = 1;
    fdc["selected_drive"] = 0;
    fdc["side"] = 0;
    fdc["density"] = "MFM";
    Json::Value driveA;
    driveA["present"] = true;
    driveA["inserted"] = true;
    driveA["letter"] = "A";
    driveA["path"] = "/tmp/disk.trd";
    driveA["track"] = 3;
    driveA["motor_on"] = true;
    driveA["write_protected"] = false;
    fdc["drives"] = Json::Value(Json::arrayValue);
    fdc["drives"].append(driveA);
    _caller->routes["GET /api/v1/emulator/emu-1/state/fdc"] = {200, fdc};

    Json::Value args;
    Json::Value aspects(Json::arrayValue);
    aspects.append("audio_fm");
    aspects.append("fdc");
    args["aspects"] = aspects;
    mcp::ToolResult result = RunTool(*_registry, "inspect_state", args, *_caller);

    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/state/audio/fm"));
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/state/audio/fm/0"));
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/state/audio/fm/1"));
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/state/fdc"));
    ASSERT_TRUE(result.structured.isMember("audio_fm"));
    EXPECT_EQ(result.structured["audio_fm"]["chip_details"].size(), 2u);
    EXPECT_EQ(result.structured["audio_fm"]["chip_details"][0]["keyed_channels"].asUInt(), 1u);
    ASSERT_TRUE(result.structured.isMember("fdc"));
    EXPECT_EQ(result.structured["fdc"]["fsm_state"].asString(), "S_IDLE");
    EXPECT_NE(result.text.find("[audio_fm] board chip 1, FM on"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("ch2 key-on mask 8 alg 7 228.5 Hz"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("[fdc] S_IDLE, last restore"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("A: /tmp/disk.trd track 3 motor on"), std::string::npos) << result.text;
}

TEST_F(McpTools_Test, InspectState_DeviceAspect_UnavailableIsReportedNotFatal)
{
    Json::Value err;
    err["message"] = "TurboSound slot device is not TSFM";
    _caller->routes["GET /api/v1/emulator/emu-1/state/audio/fm"] = {404, err};

    Json::Value args;
    Json::Value aspects(Json::arrayValue);
    aspects.append("audio_fm");
    args["aspects"] = aspects;
    mcp::ToolResult result = RunTool(*_registry, "inspect_state", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_FALSE(result.structured["audio_fm"]["available"].asBool());
    EXPECT_NE(result.text.find("[audio_fm] TurboSound slot device is not TSFM"), std::string::npos) << result.text;
}

TEST_F(McpTools_Test, InspectState_MouseAspect_FetchesMouseStatus)
{
    // One endpoint; the summary names fitment, counters and the routing answer (D-2)
    Json::Value mouse;
    mouse["present"] = true;
    mouse["x"] = 31;
    mouse["y"] = 85;
    mouse["wheel_enabled"] = false;
    mouse["routing"]["ports_decoded"] = true;
    mouse["routing"]["note"] = "decoded (standard Kempston address decode)";
    _caller->routes["GET /api/v1/emulator/emu-1/mouse/status"] = {200, mouse};

    Json::Value args;
    Json::Value aspects(Json::arrayValue);
    aspects.append("mouse");
    args["aspects"] = aspects;
    mcp::ToolResult result = RunTool(*_registry, "inspect_state", args, *_caller);

    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/mouse/status"));
    ASSERT_TRUE(result.structured.isMember("mouse"));
    EXPECT_EQ(result.structured["mouse"]["x"].asInt(), 31);
    EXPECT_NE(result.text.find("[mouse] fitted, x 31 y 85"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("ports decoded"), std::string::npos) << result.text;
}

TEST_F(McpTools_Test, InspectState_MouseAspect_ShadowedRoutingInSummary)
{
    // ports_decoded=false keeps the aspect successful - triage info, not an error
    Json::Value mouse;
    mouse["present"] = true;
    mouse["x"] = 0;
    mouse["y"] = 0;
    mouse["wheel_enabled"] = false;
    mouse["routing"]["ports_decoded"] = false;
    mouse["routing"]["note"] = "TR-DOS ports accessible (CF_DOSPORTS): only Beta Disk operations answer";
    _caller->routes["GET /api/v1/emulator/emu-1/mouse/status"] = {200, mouse};

    Json::Value args;
    Json::Value aspects(Json::arrayValue);
    aspects.append("mouse");
    args["aspects"] = aspects;
    mcp::ToolResult result = RunTool(*_registry, "inspect_state", args, *_caller);

    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("ports shadowed"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("CF_DOSPORTS"), std::string::npos) << result.text;
}

TEST_F(McpTools_Test, InspectState_AudioAyAspect_FetchesEveryChip)
{
    Json::Value overview;
    overview["available"] = true;
    overview["description"] = "TurboSound (dual AY-3-8912)";
    overview["chips"] = Json::Value(Json::arrayValue);
    overview["chips"].append(Json::Value(Json::objectValue));
    overview["chips"].append(Json::Value(Json::objectValue));
    Json::Value chip;
    Json::Value a;
    a["name"] = "A";
    a["volume"] = 15;
    a["tone_enabled"] = true;
    a["noise_enabled"] = false;
    a["envelope_enabled"] = false;
    a["frequency_hz"] = 1003.4;
    chip["channels"] = Json::Value(Json::arrayValue);
    chip["channels"].append(a);
    _caller->routes["GET /api/v1/emulator/emu-1/state/audio/ay"] = {200, overview};
    _caller->routes["GET /api/v1/emulator/emu-1/state/audio/ay/0"] = {200, chip};
    _caller->routes["GET /api/v1/emulator/emu-1/state/audio/ay/1"] = {200, chip};

    Json::Value args;
    Json::Value aspects(Json::arrayValue);
    aspects.append("audio_ay");
    args["aspects"] = aspects;
    mcp::ToolResult result = RunTool(*_registry, "inspect_state", args, *_caller);

    ASSERT_FALSE(result.isError);
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/state/audio/ay/1"));
    EXPECT_EQ(result.structured["audio_ay"]["chip_details"].size(), 2u);
    EXPECT_NE(result.text.find("chip 0: A=15T@1003Hz"), std::string::npos) << result.text;
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
// mouse_input
// ===========================================================================

TEST_F(McpTools_Test, MouseInput_Move_PostsDxDy)
{
    _caller->routes["POST /api/v1/emulator/emu-1/mouse/move"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "move";
    args["dx"] = 10;
    args["dy"] = -5;
    mcp::ToolResult result = RunTool(*_registry, "mouse_input", args, *_caller);

    ASSERT_FALSE(result.isError) << result.text;
    const FakeApiCaller::RecordedCall* call = _caller->Last("POST", "/api/v1/emulator/emu-1/mouse/move");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->body["dx"].asInt(), 10);
    EXPECT_EQ(call->body["dy"].asInt(), -5);
}

TEST_F(McpTools_Test, MouseInput_ClickWithPreMove_MovesThenClicks)
{
    _caller->routes["POST /api/v1/emulator/emu-1/mouse/move"] = {200, Json::Value(Json::objectValue)};
    _caller->routes["POST /api/v1/emulator/emu-1/mouse/click"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "click";
    args["button"] = "left";
    args["frames"] = 3;
    args["dx"] = 32;
    args["dy"] = 16;
    mcp::ToolResult result = RunTool(*_registry, "mouse_input", args, *_caller);

    ASSERT_FALSE(result.isError) << result.text;
    std::vector<const FakeApiCaller::RecordedCall*> posts;
    for (const auto& call : _caller->calls)
    {
        if (call.method == "POST")
        {
            posts.push_back(&call);
        }
    }
    ASSERT_EQ(posts.size(), 2u);
    EXPECT_EQ(posts[0]->path, "/api/v1/emulator/emu-1/mouse/move");
    EXPECT_EQ(posts[0]->body["dx"].asInt(), 32);
    EXPECT_EQ(posts[0]->body["dy"].asInt(), 16);
    EXPECT_EQ(posts[1]->path, "/api/v1/emulator/emu-1/mouse/click");
    EXPECT_EQ(posts[1]->body["button"].asString(), "left");
    EXPECT_EQ(posts[1]->body["frames"].asInt(), 3);
}

TEST_F(McpTools_Test, MouseInput_ClickWithPreMove_StopsOnMoveError)
{
    Json::Value error;
    error["error"] = "Bad Request";
    error["message"] = "dx must be in -127..127";
    _caller->routes["POST /api/v1/emulator/emu-1/mouse/move"] = {400, error};
    _caller->routes["POST /api/v1/emulator/emu-1/mouse/click"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "click";
    args["button"] = "left";
    args["dx"] = 200;
    mcp::ToolResult result = RunTool(*_registry, "mouse_input", args, *_caller);

    EXPECT_TRUE(result.isError);
    EXPECT_TRUE(_caller->Saw("POST", "/api/v1/emulator/emu-1/mouse/move"));
    EXPECT_FALSE(_caller->Saw("POST", "/api/v1/emulator/emu-1/mouse/click"));
}

TEST_F(McpTools_Test, MouseInput_Press_RequiresButton)
{
    Json::Value args;
    args["action"] = "press";
    mcp::ToolResult result = RunTool(*_registry, "mouse_input", args, *_caller);

    EXPECT_TRUE(result.isError);
    EXPECT_EQ(result.text, "press requires 'button'");
    EXPECT_TRUE(_caller->calls.empty());
}

TEST_F(McpTools_Test, MouseInput_Status_GetsStatus)
{
    _caller->routes["GET /api/v1/emulator/emu-1/mouse/status"] = {200, Json::Value(Json::objectValue)};

    Json::Value args;
    args["action"] = "status";
    mcp::ToolResult result = RunTool(*_registry, "mouse_input", args, *_caller);

    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_TRUE(_caller->Saw("GET", "/api/v1/emulator/emu-1/mouse/status"));
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
