/// @file mcp-debugtools_test.cpp
/// @brief The MCP side of the debug tools' plane gaps: control_execution's breakpoint selections, groups, notes,
/// keyboard breakpoints and set_register; analyze_performance's profiler / gs_trace / aylog / pchist / analyzers and
/// the gs_porttrace route. A synchronous fake caller records every WebAPI call, so each test asserts the route and the
/// body an action sends.

#include <gtest/gtest.h>

#include <json/json.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mcp-tool-utils.h"
#include "mcp-tools.h"

namespace
{

class FakeCaller : public mcp::IApiCaller
{
public:
    struct RecordedCall
    {
        std::string method;
        std::string path;
        Json::Value body;
    };

    std::map<std::string, std::pair<int, Json::Value>> routes;
    std::vector<RecordedCall> calls;

    void Call(const std::string& method, const std::string& path, const Json::Value* body, ApiCallback callback) override
    {
        calls.push_back({method, path, body ? *body : Json::Value()});
        auto it = routes.find(method + " " + path);
        if (it != routes.end())
            callback(it->second.first, it->second.second);
        else
            callback(200, Json::Value(Json::objectValue));
    }

    void CallRaw(const std::string& method, const std::string& path, const std::vector<uint8_t>&,
                 const std::map<std::string, std::string>&, ApiCallback callback) override
    {
        Call(method, path, nullptr, std::move(callback));
    }

    const RecordedCall* Last(const std::string& method, const std::string& path) const
    {
        for (auto it = calls.rbegin(); it != calls.rend(); ++it)
            if (it->method == method && it->path == path)
                return &*it;
        return nullptr;
    }
};

class McpDebugTools_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _caller = std::make_shared<FakeCaller>();
        _registry = mcp::BuildFullRegistry(_caller);
        Json::Value emulator;
        emulator["id"] = "emu-1";
        emulator["state"] = "paused";
        Json::Value list;
        list["emulators"].append(emulator);
        _caller->routes["GET /api/v1/emulator"] = {200, list};
    }

    mcp::ToolResult Run(const std::string& tool, const Json::Value& args)
    {
        const mcp::ToolDefinition* definition = _registry->Find(tool);
        if (definition == nullptr)
            return mcp::ToolResult::Error("no tool " + tool);
        mcp::ToolResult result;
        definition->handler(args, *_caller, [&result](mcp::ToolResult value) { result = std::move(value); },
                            [](double, double, const std::string&) {});
        return result;
    }

    std::shared_ptr<FakeCaller> _caller;
    std::unique_ptr<mcp::ToolRegistry> _registry;
};

}  // namespace

TEST_F(McpDebugTools_Test, ControlExecution_SelectionsGoToTheBulkRoutes)
{
    Json::Value args;
    args["action"] = "bp_disable";
    args["select"] = "group:loop";
    ASSERT_FALSE(Run("control_execution", args).isError);
    const FakeCaller::RecordedCall* put = _caller->Last("PUT", "/api/v1/emulator/emu-1/breakpoints/active");
    ASSERT_NE(put, nullptr);
    EXPECT_EQ(put->body["select"].asString(), "group:loop");
    EXPECT_FALSE(put->body["active"].asBool());

    Json::Value toggle;
    toggle["action"] = "bp_toggle";
    ASSERT_FALSE(Run("control_execution", toggle).isError);
    put = _caller->Last("PUT", "/api/v1/emulator/emu-1/breakpoints/active");
    ASSERT_NE(put, nullptr);
    EXPECT_EQ(put->body["select"].asString(), "all") << "bp_toggle without a selection toggles all";
    EXPECT_EQ(put->body["active"].asString(), "toggle");

    Json::Value remove;
    remove["action"] = "bp_remove";
    remove["select"] = "nextreg";
    ASSERT_FALSE(Run("control_execution", remove).isError);
    const FakeCaller::RecordedCall* post = _caller->Last("POST", "/api/v1/emulator/emu-1/breakpoints/remove");
    ASSERT_NE(post, nullptr);
    EXPECT_EQ(post->body["select"].asString(), "nextreg");

    Json::Value byId;
    byId["action"] = "bp_enable";
    byId["bp_id"] = "3";
    ASSERT_FALSE(Run("control_execution", byId).isError);
    EXPECT_NE(_caller->Last("PUT", "/api/v1/emulator/emu-1/breakpoints/3/enable"), nullptr) << "an id keeps its route";
}

TEST_F(McpDebugTools_Test, ControlExecution_GroupsNotesAndKeyboard)
{
    Json::Value groups;
    groups["groups"][0]["name"] = "loop";
    groups["groups"][0]["count"] = 2;
    groups["groups"][0]["enabled"] = 1;
    _caller->routes["GET /api/v1/emulator/emu-1/breakpoints/groups"] = {200, groups};
    Json::Value args;
    args["action"] = "bp_groups";
    const mcp::ToolResult listed = Run("control_execution", args);
    ASSERT_FALSE(listed.isError);
    EXPECT_NE(listed.text.find("loop: 1 of 2 on"), std::string::npos) << listed.text;

    Json::Value group;
    group["action"] = "bp_group_set";
    group["bp_id"] = "4";
    group["group"] = "loop";
    ASSERT_FALSE(Run("control_execution", group).isError);
    const FakeCaller::RecordedCall* put = _caller->Last("PUT", "/api/v1/emulator/emu-1/breakpoints/4/group");
    ASSERT_NE(put, nullptr);
    EXPECT_EQ(put->body["group"].asString(), "loop");

    Json::Value note;
    note["action"] = "bp_note";
    note["bp_id"] = "4";
    EXPECT_TRUE(Run("control_execution", note).isError) << "bp_note needs a note";

    Json::Value key;
    key["action"] = "bp_add";
    key["type"] = "keyboard";
    key["key"] = "space";
    key["on"] = "release";
    ASSERT_FALSE(Run("control_execution", key).isError) << "a keyboard breakpoint needs no address";
    const FakeCaller::RecordedCall* add = _caller->Last("POST", "/api/v1/emulator/emu-1/breakpoints");
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(add->body["type"].asString(), "keyboard");
    EXPECT_EQ(add->body["key"].asString(), "space");
    EXPECT_EQ(add->body["on"].asString(), "release");
}

TEST_F(McpDebugTools_Test, ControlExecution_SetRegisterPutsTheValue)
{
    Json::Value args;
    args["action"] = "set_register";
    args["name"] = "pc";
    args["value"] = "0x8000";
    ASSERT_FALSE(Run("control_execution", args).isError);
    const FakeCaller::RecordedCall* put = _caller->Last("PUT", "/api/v1/emulator/emu-1/registers/pc");
    ASSERT_NE(put, nullptr);
    EXPECT_EQ(put->body["value"].asString(), "0x8000");

    Json::Value missing;
    missing["action"] = "set_register";
    missing["name"] = "pc";
    EXPECT_TRUE(Run("control_execution", missing).isError);
}

TEST_F(McpDebugTools_Test, AnalyzePerformance_ProfilersTracesAnalyzers)
{
    Json::Value args;
    args["action"] = "profiler";
    args["profiler"] = "opcode";
    args["op"] = "pause";
    ASSERT_FALSE(Run("analyze_performance", args).isError);
    EXPECT_NE(_caller->Last("POST", "/api/v1/emulator/emu-1/profiler/opcode/pause"), nullptr);

    args["profiler"] = "all";
    args["op"] = "status";
    ASSERT_FALSE(Run("analyze_performance", args).isError);
    EXPECT_NE(_caller->Last("GET", "/api/v1/emulator/emu-1/profiler/status"), nullptr);

    args["op"] = "jump";
    EXPECT_TRUE(Run("analyze_performance", args).isError);

    Json::Value gs;
    gs["action"] = "gs_trace";
    gs["op"] = "clear";
    ASSERT_FALSE(Run("analyze_performance", gs).isError);
    const FakeCaller::RecordedCall* control = _caller->Last("POST", "/api/v1/emulator/emu-1/control/audio/gs/porttrace");
    ASSERT_NE(control, nullptr);
    EXPECT_EQ(control->body["action"].asString(), "clear");

    Json::Value ay;
    ay["action"] = "aylog";
    ay["op"] = "start";
    ay["capacity"] = 4096;
    ASSERT_FALSE(Run("analyze_performance", ay).isError);
    const FakeCaller::RecordedCall* log = _caller->Last("POST", "/api/v1/emulator/emu-1/ay/log");
    ASSERT_NE(log, nullptr);
    EXPECT_EQ(log->body["action"].asString(), "start");
    EXPECT_EQ(log->body["capacity"].asInt(), 4096);

    Json::Value pc;
    pc["action"] = "pchist";
    pc["op"] = "on";
    ASSERT_FALSE(Run("analyze_performance", pc).isError);
    const FakeCaller::RecordedCall* arm = _caller->Last("POST", "/api/v1/emulator/emu-1/debug/pchist");
    ASSERT_NE(arm, nullptr);
    EXPECT_TRUE(arm->body["enabled"].asBool());

    Json::Value analyzer;
    analyzer["action"] = "analyzers";
    analyzer["op"] = "enable";
    analyzer["name"] = "trdos";
    ASSERT_FALSE(Run("analyze_performance", analyzer).isError);
    const FakeCaller::RecordedCall* enable = _caller->Last("PUT", "/api/v1/emulator/emu-1/analyzer/trdos");
    ASSERT_NE(enable, nullptr);
    EXPECT_TRUE(enable->body["enabled"].asBool());
    analyzer.removeMember("name");
    EXPECT_TRUE(Run("analyze_performance", analyzer).isError) << "enable needs a name";
}

TEST_F(McpDebugTools_Test, AnalyzePerformance_GsPortTraceControlsThroughTheControlRoute)
{
    Json::Value args;
    args["action"] = "gs_porttrace";
    args["frames"] = 1;
    Run("analyze_performance", args);
    // It used to POST to the GET-only /state/audio/gs/porttrace: the start failed
    EXPECT_NE(_caller->Last("POST", "/api/v1/emulator/emu-1/control/audio/gs/porttrace"), nullptr);
    EXPECT_EQ(_caller->Last("POST", "/api/v1/emulator/emu-1/state/audio/gs/porttrace"), nullptr);
}
