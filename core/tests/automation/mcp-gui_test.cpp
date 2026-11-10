// MCP tools of the GUI control plane (gui-control-plane design §4.10): one tool per interface of the catalogue the
// program registered, gui_list and gui_state; each becomes a /api/v1/gui route, the text carries the answer (an agent
// may see only the text). Without a catalogue there is no GUI tool.

#include <gtest/gtest.h>

#include <json/json.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mcp-gui.h"

namespace
{
class RecordingCaller : public mcp::IApiCaller
{
public:
    struct Recorded
    {
        std::string method;
        std::string path;
        Json::Value body;
    };
    std::vector<Recorded> calls;
    std::map<std::string, std::pair<int, Json::Value>> routes;

    void Call(const std::string& method, const std::string& path, const Json::Value* body, ApiCallback callback) override
    {
        calls.push_back({method, path, body ? *body : Json::Value()});
        auto it = routes.find(method + " " + path);
        if (it != routes.end())
            callback(it->second.first, it->second.second);
        else
            callback(404, Json::Value());
    }
    void CallRaw(const std::string& method, const std::string& path, const std::vector<uint8_t>&,
                 const std::map<std::string, std::string>&, ApiCallback callback) override
    {
        calls.push_back({method, path, Json::Value()});
        callback(200, Json::Value());
    }
};

const char* const kCatalogue = R"({
  "format": "unreal-ng-gui-catalogue", "version": 1,
  "interfaces": [
    {"name": "memory", "level": "view", "description": "A memory view.", "state": ["top", "cursor"],
     "commands": [
       {"method": "memory.goto", "label": "Go to", "description": "Show an address.", "repeats": false,
        "params": [{"name": "addr", "kind": "address", "optional": false, "description": "the address"}]},
       {"method": "memory.page", "label": "Page", "description": "Scroll by pages.", "repeats": true,
        "params": [{"name": "n", "kind": "int", "optional": true, "description": "pages, signed"}]}]},
    {"name": "screen", "level": "view", "description": "A screen.", "state": ["zoom"],
     "commands": [
       {"method": "screen.fullscreen", "label": "Full screen", "description": "Full screen.", "repeats": false,
        "params": [{"name": "on", "kind": "bool", "optional": true, "description": "absent: flip"}]}]},
    {"name": "empty", "level": "host", "description": "No commands.", "state": [], "commands": []}
  ]})";

mcp::ToolResult RunTool(mcp::ToolRegistry& registry, const std::string& name, Json::Value args, mcp::IApiCaller& caller)
{
    const mcp::ToolDefinition* tool = registry.Find(name);
    if (!tool)
        return mcp::ToolResult::Error(name + " not registered");
    mcp::ToolResult result;
    tool->handler(args, caller, [&result](mcp::ToolResult value) { result = std::move(value); },
                  mcp::ProgressFn([](double, double, const std::string&) {}));
    return result;
}

Json::Value Ok(const Json::Value& body)
{
    Json::Value reply;
    reply["ok"] = true;
    reply["body"] = body;
    return reply;
}
}  // namespace

TEST(McpGui_Test, OneToolPerInterfaceWithItsCommandsAndParameters)
{
    mcp::ToolRegistry registry;
    mcp::RegisterGui(registry, kCatalogue);
    const std::vector<std::string> names = registry.ToolNames();
    EXPECT_EQ(names, (std::vector<std::string>{"gui_list", "gui_memory", "gui_screen", "gui_state"}));

    const mcp::ToolDefinition* memory = registry.Find("gui_memory");
    ASSERT_NE(memory, nullptr);
    const Json::Value& properties = memory->inputSchema["properties"];
    ASSERT_EQ(properties["action"]["enum"].size(), 2u);
    EXPECT_EQ(properties["action"]["enum"][0].asString(), "goto");
    EXPECT_EQ(properties["action"]["enum"][1].asString(), "page");
    EXPECT_TRUE(properties.isMember("target"));
    EXPECT_EQ(properties["addr"]["type"][0].asString(), "string");  // an address: text or a number
    EXPECT_EQ(properties["n"]["type"].asString(), "integer");
    EXPECT_NE(properties["n"]["description"].asString().find("page: pages, signed (optional)"), std::string::npos);
    EXPECT_EQ(memory->inputSchema["required"][0].asString(), "action");
    EXPECT_NE(memory->description.find("goto: Show an address. (addr)"), std::string::npos);
    EXPECT_NE(memory->description.find("State (gui_state): top cursor"), std::string::npos);
    EXPECT_EQ(registry.Find("gui_screen")->inputSchema["properties"]["on"]["type"].asString(), "boolean");
}

TEST(McpGui_Test, NoCatalogueNoTool)
{
    mcp::ToolRegistry registry;
    mcp::RegisterGui(registry, "");
    mcp::RegisterGui(registry, "not json");
    mcp::RegisterGui(registry, R"({"interfaces": 5})");
    EXPECT_TRUE(registry.ToolNames().empty());

    // The full registry of a program without the plane (core-tests registers none) has no GUI tool
    auto caller = std::make_shared<RecordingCaller>();
    for (const std::string& name : mcp::BuildFullRegistry(caller)->ToolNames())
        EXPECT_NE(name.rfind("gui_", 0), 0u) << name;
}

TEST(McpGui_Test, ACommandIsItsRouteWithTheParametersAndTarget)
{
    mcp::ToolRegistry registry;
    mcp::RegisterGui(registry, kCatalogue);
    RecordingCaller caller;
    Json::Value body;
    body["address"] = "0x6000";
    caller.routes["POST /api/v1/gui/memory.goto"] = {200, Ok(body)};

    Json::Value args;
    args["action"] = "goto";
    args["addr"] = "0x6000";
    args["target"] = "debugger/memory";
    const mcp::ToolResult result = RunTool(registry, "gui_memory", args, caller);
    ASSERT_FALSE(result.isError) << result.text;
    ASSERT_EQ(caller.calls.size(), 1u);
    EXPECT_EQ(caller.calls[0].method, "POST");
    EXPECT_EQ(caller.calls[0].body["addr"].asString(), "0x6000");
    EXPECT_EQ(caller.calls[0].body["target"].asString(), "debugger/memory");
    EXPECT_FALSE(caller.calls[0].body.isMember("action"));
    EXPECT_NE(result.text.find("0x6000"), std::string::npos);  // the body is in the text
    EXPECT_TRUE(result.structured["ok"].asBool());
}

TEST(McpGui_Test, RefusalsAndUnknownActionsAreErrorsAnAgentCanRead)
{
    mcp::ToolRegistry registry;
    mcp::RegisterGui(registry, kCatalogue);
    RecordingCaller caller;
    Json::Value refused;
    refused["ok"] = false;
    refused["error"] = "ambiguous";
    refused["message"] = "two memory views";
    refused["body"]["candidates"].append("debugger/memory");
    caller.routes["POST /api/v1/gui/memory.page"] = {200, refused};

    Json::Value args;
    args["action"] = "page";
    mcp::ToolResult result = RunTool(registry, "gui_memory", args, caller);
    EXPECT_TRUE(result.isError);
    EXPECT_NE(result.text.find("memory.page: ambiguous: two memory views"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("debugger/memory"), std::string::npos) << result.text;
    EXPECT_EQ(result.structured["error"].asString(), "ambiguous");

    args["action"] = "scroll";
    result = RunTool(registry, "gui_memory", args, caller);
    EXPECT_TRUE(result.isError);
    EXPECT_NE(result.text.find("goto, page"), std::string::npos) << result.text;
    EXPECT_EQ(caller.calls.size(), 1u);  // refused before any call

    Json::Value noGui;
    noGui["ok"] = false;
    noGui["error"] = "no_gui";
    noGui["message"] = "this program has no GUI control plane";
    caller.routes["POST /api/v1/gui/screen.fullscreen"] = {404, noGui};
    args["action"] = "fullscreen";
    result = RunTool(registry, "gui_screen", args, caller);
    EXPECT_TRUE(result.isError);
    EXPECT_NE(result.text.find("no_gui"), std::string::npos) << result.text;
}

TEST(McpGui_Test, ListAndStateAreTheirRoutes)
{
    mcp::ToolRegistry registry;
    mcp::RegisterGui(registry, kCatalogue);
    RecordingCaller caller;
    Json::Value hosts;
    hosts["hosts"].append(Json::Value(Json::objectValue));
    hosts["hosts"][0]["program"] = "unreal-qt";
    caller.routes["GET /api/v1/gui"] = {200, Ok(hosts)};
    Json::Value state;
    state["target"] = "main/screen";
    state["state"]["zoom"] = 2;
    caller.routes["GET /api/v1/gui/state?target=main%2Fscreen"] = {200, Ok(state)};

    mcp::ToolResult result = RunTool(registry, "gui_list", Json::Value(Json::objectValue), caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("unreal-qt"), std::string::npos);

    Json::Value args;
    args["target"] = "main/screen";
    result = RunTool(registry, "gui_state", args, caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("\"zoom\" : 2"), std::string::npos) << result.text;

    result = RunTool(registry, "gui_state", Json::Value(Json::objectValue), caller);
    EXPECT_TRUE(result.isError);
}
