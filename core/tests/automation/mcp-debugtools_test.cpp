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
    groups["groups"][0]["total"] = 2;
    groups["groups"][0]["active"] = 1;
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

/// analysis: one tool, the action picks the route; the core answer's text is the tool's text (content[] carries it)
TEST_F(McpDebugTools_Test, Analysis_ActionsGoToTheirRoutesAndCarryTheText)
{
    Json::Value explainBody;
    explainBody["text"] = "$80BC [RAM 2]: OpcodeFetch\n  frames 1..2";
    explainBody["kinds"].append("OpcodeFetch");
    _caller->routes["GET /api/v1/emulator/emu-1/analysis/explain?address=%2480BC&page=ram%3A1"] = {200, explainBody};

    Json::Value args;
    args["action"] = "explain";
    args["address"] = "$80BC";
    args["page"] = "ram:1";
    mcp::ToolResult result = Run("analysis", args);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("OpcodeFetch\n  frames 1..2"), std::string::npos) << result.text;
    EXPECT_EQ(result.structured["kinds"][0].asString(), "OpcodeFetch");

    args = Json::Value();
    args["action"] = "explain";
    args["address"] = 32956;  // an integer works too
    Run("analysis", args);
    EXPECT_NE(_caller->Last("GET", "/api/v1/emulator/emu-1/analysis/explain?address=32956"), nullptr);

    args = Json::Value();
    args["action"] = "start";
    args["level"] = "facts";
    args["mode"] = "physical";
    ASSERT_FALSE(Run("analysis", args).isError);
    const FakeCaller::RecordedCall* start = _caller->Last("POST", "/api/v1/emulator/emu-1/analysis/start");
    ASSERT_NE(start, nullptr);
    EXPECT_EQ(start->body["level"].asString(), "facts");
    EXPECT_EQ(start->body["mode"].asString(), "physical");

    args = Json::Value();
    args["action"] = "status";
    ASSERT_FALSE(Run("analysis", args).isError);
    EXPECT_NE(_caller->Last("GET", "/api/v1/emulator/emu-1/analysis/status"), nullptr);
    args["action"] = "stop";
    ASSERT_FALSE(Run("analysis", args).isError);
    EXPECT_NE(_caller->Last("POST", "/api/v1/emulator/emu-1/analysis/stop"), nullptr);
}

/// analysis build / cancel / explain what=routine (P2): the long build waits 20 s by default, record goes to start
TEST_F(McpDebugTools_Test, Analysis_BuildCancelAndRoutineExplainGoToTheirRoutes)
{
    Json::Value buildBody;
    buildBody["job"] = 3;
    buildBody["state"] = "done";
    buildBody["text"] = "L1 build job 3: replaying 50 recorded frames on an analysis instance; done";
    _caller->routes["POST /api/v1/emulator/emu-1/analysis/build"] = {200, buildBody};

    Json::Value args;
    args["action"] = "start";
    args["record"] = true;
    ASSERT_FALSE(Run("analysis", args).isError);
    const FakeCaller::RecordedCall* start = _caller->Last("POST", "/api/v1/emulator/emu-1/analysis/start");
    ASSERT_NE(start, nullptr);
    EXPECT_TRUE(start->body["record"].asBool());

    args = Json::Value();
    args["action"] = "build";
    args["frames"] = 100;
    mcp::ToolResult result = Run("analysis", args);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("L1 build job 3"), std::string::npos) << result.text;
    const FakeCaller::RecordedCall* build = _caller->Last("POST", "/api/v1/emulator/emu-1/analysis/build");
    ASSERT_NE(build, nullptr);
    EXPECT_EQ(build->body["frames"].asUInt64(), 100u);
    EXPECT_EQ(build->body["wait"].asUInt64(), 20000u) << "one call answers the result";

    args = Json::Value();
    args["action"] = "cancel";
    Run("analysis", args);
    EXPECT_NE(_caller->Last("POST", "/api/v1/emulator/emu-1/analysis/cancel"), nullptr);

    args = Json::Value();
    args["action"] = "explain";
    args["address"] = "$8000";
    args["what"] = "routine";
    Run("analysis", args);
    EXPECT_NE(_caller->Last("GET", "/api/v1/emulator/emu-1/analysis/explain?address=%248000&what=routine"), nullptr);
}

/// analysis query / export (P3): the query's options go into the URL, the export's into the body; export needs a format
TEST_F(McpDebugTools_Test, Analysis_QueryAndExportGoToTheirRoutes)
{
    Json::Value queryBody;
    queryBody["count"] = 1;
    queryBody["text"] = "8000-80FF code           code-map       1000 observed";
    _caller->routes["GET /api/v1/emulator/emu-1/analysis/query?kind=code&range=8000-80FF&limit=5"] = {200, queryBody};

    Json::Value args;
    args["action"] = "query";
    args["kind"] = "code";
    args["range"] = "8000-80FF";
    args["limit"] = 5;
    mcp::ToolResult result = Run("analysis", args);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("code-map"), std::string::npos) << result.text;

    args = Json::Value();
    args["action"] = "query";
    Run("analysis", args);
    EXPECT_NE(_caller->Last("GET", "/api/v1/emulator/emu-1/analysis/query"), nullptr);

    args = Json::Value();
    args["action"] = "export";
    args["format"] = "skool";
    args["path"] = "/tmp/x.skool";
    args["ranges"] = "8000-FFFF";
    args["width"] = 16;
    ASSERT_FALSE(Run("analysis", args).isError);
    const FakeCaller::RecordedCall* exported = _caller->Last("POST", "/api/v1/emulator/emu-1/analysis/export");
    ASSERT_NE(exported, nullptr);
    EXPECT_EQ(exported->body["format"].asString(), "skool");
    EXPECT_EQ(exported->body["path"].asString(), "/tmp/x.skool");
    EXPECT_EQ(exported->body["ranges"].asString(), "8000-FFFF");
    EXPECT_EQ(exported->body["width"].asUInt64(), 16u);

    args = Json::Value();
    args["action"] = "export";
    EXPECT_TRUE(Run("analysis", args).isError) << "export needs a format";
}

/// analysis gallery (P4): the options go into the body; the PNG comes back as an image content block, not as base64
/// in the structured answer
TEST_F(McpDebugTools_Test, Analysis_GalleryAnswersAnImage)
{
    Json::Value galleryBody;
    galleryBody["png"] = "iVBORw0KGgo=";
    galleryBody["width"] = 70;
    galleryBody["text"] = "L2 gallery $8200-$82FF sprite 16x16x8: 8 frames, 290x70 PNG, 400 bytes";
    _caller->routes["POST /api/v1/emulator/emu-1/analysis/gallery"] = {200, galleryBody};

    Json::Value args;
    args["action"] = "gallery";
    args["address"] = "$8200";
    args["scale"] = 4;
    args["kind"] = "sprite";
    const mcp::ToolResult result = Run("analysis", args);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("16x16x8"), std::string::npos) << result.text;
    EXPECT_EQ(result.imageData, "iVBORw0KGgo=");
    EXPECT_EQ(result.imageMimeType, "image/png");
    EXPECT_EQ(result.structured["png"].asString().find("iVBOR"), std::string::npos);
    const FakeCaller::RecordedCall* gallery = _caller->Last("POST", "/api/v1/emulator/emu-1/analysis/gallery");
    ASSERT_NE(gallery, nullptr);
    EXPECT_EQ(gallery->body["address"].asString(), "$8200");
    EXPECT_EQ(gallery->body["scale"].asUInt64(), 4u);
    EXPECT_EQ(gallery->body["kind"].asString(), "sprite");

    // The list: no image
    galleryBody = Json::Value();
    galleryBody["count"] = 2;
    galleryBody["text"] = "L2 gallery: 2 graphics regions";
    _caller->routes["POST /api/v1/emulator/emu-1/analysis/gallery"] = {200, galleryBody};
    args = Json::Value();
    args["action"] = "gallery";
    const mcp::ToolResult list = Run("analysis", args);
    ASSERT_FALSE(list.isError) << list.text;
    EXPECT_TRUE(list.imageData.empty());
    EXPECT_NE(list.text.find("2 graphics regions"), std::string::npos) << list.text;
}

/// analysis L3 (P5): findings, review, experiment, cover go to their POST routes with every option as given; an
/// experiment spec object reaches the body as an object (the WebAPI serializes it for the core)
TEST_F(McpDebugTools_Test, Analysis_ReviewActionsForwardTheirOptions)
{
    // The dispatcher refuses an argument the schema does not name: every option the L3 actions forward is in it
    const mcp::ToolDefinition* definition = _registry->Find("analysis");
    ASSERT_NE(definition, nullptr);
    for (const char* name : {"op", "id", "verdict", "label", "title", "comment", "by", "experiment", "status", "claim",
                             "evidence", "spec", "finding", "parallel", "budget", "per_branch"})
        EXPECT_TRUE(definition->inputSchema["properties"].isMember(name)) << name;

    Json::Value reviewBody;
    reviewBody["text"] = "L3 review F7 (bug): ...";
    _caller->routes["POST /api/v1/emulator/emu-1/analysis/review"] = {200, reviewBody};
    Json::Value args;
    args["action"] = "review";
    args["op"] = "resolve";
    args["id"] = "F7";
    args["verdict"] = "ok";
    args["label"] = "MainLoop";
    args["experiment"]["frames"] = 2;
    const mcp::ToolResult review = Run("analysis", args);
    ASSERT_FALSE(review.isError) << review.text;
    EXPECT_NE(review.text.find("L3 review F7"), std::string::npos) << review.text;
    const FakeCaller::RecordedCall* resolved = _caller->Last("POST", "/api/v1/emulator/emu-1/analysis/review");
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->body["op"].asString(), "resolve");
    EXPECT_EQ(resolved->body["id"].asString(), "F7");
    EXPECT_EQ(resolved->body["verdict"].asString(), "ok");
    EXPECT_EQ(resolved->body["label"].asString(), "MainLoop");
    EXPECT_TRUE(resolved->body["experiment"].isObject());

    args = Json::Value();
    args["action"] = "experiment";
    args["spec"]["poke"]["$8100"] = 5;
    args["spec"]["expect"]["screen"] = "changed";
    args["finding"] = "F3";
    args["parallel"] = 2;
    Run("analysis", args);
    const FakeCaller::RecordedCall* experiment = _caller->Last("POST", "/api/v1/emulator/emu-1/analysis/experiment");
    ASSERT_NE(experiment, nullptr);
    EXPECT_EQ(experiment->body["spec"]["poke"]["$8100"].asInt(), 5);
    EXPECT_EQ(experiment->body["finding"].asString(), "F3");
    EXPECT_EQ(experiment->body["parallel"].asInt(), 2);

    args = Json::Value();
    args["action"] = "experiment";
    EXPECT_TRUE(Run("analysis", args).isError) << "experiment needs a spec or a path";

    args = Json::Value();
    args["action"] = "findings";
    args["format"] = "markdown";
    args["kind"] = "bug";
    Run("analysis", args);
    const FakeCaller::RecordedCall* findings = _caller->Last("POST", "/api/v1/emulator/emu-1/analysis/findings");
    ASSERT_NE(findings, nullptr);
    EXPECT_EQ(findings->body["format"].asString(), "markdown");
    EXPECT_EQ(findings->body["kind"].asString(), "bug");

    args = Json::Value();
    args["action"] = "cover";
    args["budget"] = 16;
    args["per_branch"] = 4;
    Run("analysis", args);
    const FakeCaller::RecordedCall* cover = _caller->Last("POST", "/api/v1/emulator/emu-1/analysis/cover");
    ASSERT_NE(cover, nullptr);
    EXPECT_EQ(cover->body["budget"].asInt(), 16);
    EXPECT_EQ(cover->body["per_branch"].asInt(), 4);
}

TEST_F(McpDebugTools_Test, Analysis_RefusesWithoutAddressOrOnAnHttpError)
{
    Json::Value args;
    args["action"] = "explain";
    EXPECT_TRUE(Run("analysis", args).isError) << "explain needs an address";

    Json::Value conflict;
    conflict["error"] = "Conflict";
    conflict["message"] = "no access facts: the tracker level is counters";
    _caller->routes["GET /api/v1/emulator/emu-1/analysis/explain?address=0x8000"] = {409, conflict};
    args["address"] = "0x8000";
    const mcp::ToolResult result = Run("analysis", args);
    EXPECT_TRUE(result.isError);
    EXPECT_NE(result.text.find("409"), std::string::npos) << result.text;
}
