// MCP actions tool (control surfaces actions.md §6): list and run become the /api/v1/controllers/actions routes; the
// text carries what an agent needs (an agent may see only the text)

#include <gtest/gtest.h>

#include <json/json.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mcp-actions.h"

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
            callback(404, Json::Value());
    }
    void CallRaw(const std::string& method, const std::string& path, const std::vector<uint8_t>&,
                 const std::map<std::string, std::string>&, ApiCallback callback) override
    {
        calls.push_back({method, path, Json::Value()});
        callback(200, Json::Value());
    }
};

mcp::ToolResult RunActions(mcp::ToolRegistry& registry, Json::Value args, mcp::IApiCaller& caller)
{
    const mcp::ToolDefinition* tool = registry.Find("actions");
    if (!tool)
        return mcp::ToolResult::Error("actions tool not registered");
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

TEST(McpActions_Test, ListAndRunBecomeTheActionsRoutes)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);
    caller->routes["GET /api/v1/emulator"] = {200, OneEmulator()};

    Json::Value list;
    list["ok"] = true;
    list["instance"] = "emu-1";
    Json::Value step;
    step["id"] = "run.step_in";
    step["label"] = "Step in";
    step["group"] = "Run";
    step["kind"] = "press";
    step["scope"] = "debug";
    step["available"] = false;
    step["error"] = "not_now";
    step["reason"] = "needs a pause";
    list["actions"].append(step);
    caller->routes["GET /api/v1/controllers/actions?instance=emu-1"] = {200, list};
    Json::Value args;
    args["action"] = "list";
    const mcp::ToolResult listed = RunActions(*registry, args, *caller);
    ASSERT_FALSE(listed.isError) << listed.text;
    EXPECT_NE(listed.text.find("run.step_in"), std::string::npos);
    EXPECT_NE(listed.text.find("needs a pause"), std::string::npos) << "the reason is in the text";

    Json::Value ran;
    ran["ok"] = true;
    ran["action"] = "ttd.jog_frames";
    ran["message"] = "";
    ran["result"]["state"] = "paused";
    caller->routes["POST /api/v1/controllers/actions/ttd.jog_frames"] = {200, ran};
    args["action"] = "run";
    args["id"] = "ttd.jog_frames";
    args["value"] = -10;
    const mcp::ToolResult result = RunActions(*registry, args, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    const auto& call = caller->calls.back();
    EXPECT_EQ(call.method, "POST");
    EXPECT_EQ(call.body["instance"].asString(), "emu-1");
    EXPECT_EQ(call.body["value"].asInt(), -10);
    EXPECT_NE(result.text.find("ttd.jog_frames: ok"), std::string::npos) << result.text;

    Json::Value refused;
    refused["ok"] = false;
    refused["error"] = "Run-control held";
    refused["message"] = "Run-control held by gdb. Use that surface, or take control back.";
    caller->routes["POST /api/v1/controllers/actions/run.step_in"] = {409, refused};
    args["id"] = "run.step_in";
    args.removeMember("value");
    const mcp::ToolResult held = RunActions(*registry, args, *caller);
    EXPECT_TRUE(held.isError);
    EXPECT_NE(held.text.find("Run-control held by gdb"), std::string::npos) << held.text;

    Json::Value missing;
    missing["action"] = "run";
    EXPECT_TRUE(RunActions(*registry, missing, *caller).isError) << "run needs an id";
}
