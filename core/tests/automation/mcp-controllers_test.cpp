// MCP controllers tool: its action table is a copy of ControllerControl's (MCP is a WebAPI client and does not link
// the core); this keeps the two equal and checks the routes the actions become

#include <gtest/gtest.h>

#include "emulator/io/controllers/controllercontrol.h"
#include "mcp-controllers.h"

#include <json/json.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

TEST(McpControllers_Test, ActionsAndOptionsMatchControllerControl)
{
    std::vector<std::string> verbs;
    for (const auto& [verb, options] : mcp::ControllerToolActions())
    {
        verbs.push_back(verb);
        EXPECT_EQ(options, controllers::ControllerControl::OptionsFor(verb)) << "options of " << verb;
    }
    EXPECT_EQ(verbs, controllers::ControllerControl::Verbs()) << "the same verbs, in the same order";
}

namespace
{
    /// Records the WebAPI calls the tool makes and answers from a script
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
                callback(200, Json::Value());
        }
        void CallRaw(const std::string& method, const std::string& path, const std::vector<uint8_t>&,
                     const std::map<std::string, std::string>&, ApiCallback callback) override
        {
            calls.push_back({method, path, Json::Value()});
            callback(200, Json::Value());
        }
    };

    mcp::ToolResult RunControllersTool(mcp::ToolRegistry& registry, Json::Value args, mcp::IApiCaller& caller)
    {
        const mcp::ToolDefinition* tool = registry.Find("controllers");
        if (!tool)
            return mcp::ToolResult::Error("controllers tool not registered");
        mcp::ToolResult result;
        tool->handler(args, caller, [&result](mcp::ToolResult value) { result = std::move(value); },
                      mcp::ProgressFn([](double, double, const std::string&) {}));
        return result;
    }

    Json::Value OneEmulator()
    {
        Json::Value emulator;
        emulator["id"] = "emu-1";
        emulator["state"] = "paused";
        Json::Value body;
        body["emulators"].append(emulator);
        return body;
    }
}  // namespace

/// plug becomes a POST on the connector with the options in the body; auto reads or re-runs the decision
TEST(McpControllers_Test, ActionsBecomeControllerRoutes)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);
    caller->routes["GET /api/v1/emulator"] = {200, OneEmulator()};

    Json::Value ok;
    ok["ok"] = true;
    ok["connector"] = "sinclair-1";
    caller->routes["POST /api/v1/emulator/emu-1/controllers/sinclair-1/plug"] = {200, ok};
    Json::Value plug;
    plug["action"] = "plug";
    plug["connector"] = "sinclair-1";
    plug["device"] = "sinclair-joystick";
    plug["port"] = 2;
    const mcp::ToolResult result = RunControllersTool(*registry, plug, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    const auto& call = caller->calls.back();
    EXPECT_EQ(call.method, "POST");
    EXPECT_EQ(call.body["device"].asString(), "sinclair-joystick");
    EXPECT_EQ(call.body["port"].asInt(), 2);
    EXPECT_FALSE(call.body.isMember("action")) << "the action is the route, not an option";

    Json::Value decided;
    decided["ok"] = true;
    decided["decision"]["decisive"] = true;
    decided["decision"]["label"] = "Kempston joystick";
    decided["decision"]["reason"] = "#1F every frame";
    decided["evidence"]["active"] = false;
    decided["evidence"]["frames_observed"] = 50;
    caller->routes["GET /api/v1/emulator/emu-1/controllers/auto"] = {200, decided};
    Json::Value automatic;
    automatic["action"] = "auto";
    const mcp::ToolResult decision = RunControllersTool(*registry, automatic, *caller);
    ASSERT_FALSE(decision.isError) << decision.text;
    EXPECT_NE(decision.text.find("Kempston joystick"), std::string::npos) << "the text carries the decision";

    automatic["apply"] = true;
    RunControllersTool(*registry, automatic, *caller);
    EXPECT_EQ(caller->calls.back().method, "POST");
    EXPECT_EQ(caller->calls.back().path, "/api/v1/emulator/emu-1/controllers/auto/apply");

    Json::Value refused;
    refused["ok"] = false;
    refused["error"] = "not_found";
    refused["message"] = "no connector 'nope'";
    caller->routes["GET /api/v1/emulator/emu-1/controllers/nope"] = {404, refused};
    Json::Value get;
    get["action"] = "get";
    get["connector"] = "nope";
    const mcp::ToolResult missing = RunControllersTool(*registry, get, *caller);
    EXPECT_TRUE(missing.isError);
    EXPECT_NE(missing.text.find("[not_found]"), std::string::npos) << missing.text;
}
