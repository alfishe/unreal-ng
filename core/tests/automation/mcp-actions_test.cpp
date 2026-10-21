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

/// The surface verbs (Stream Deck targeting.md) become the /api/v1/surfaces routes; the text names the followed
/// instance, the sessions' targets, the readouts and the activity (an agent may see only the text)
TEST(McpActions_Test, SurfaceVerbsBecomeTheSurfacesRoutes)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);

    Json::Value target;
    target["instance"] = "emu-1";
    target["index"] = 0;
    target["model"] = "PENTAGON";
    target["ram_kb"] = 128;
    target["view"]["id"] = "v1";
    target["view"]["name"] = "Main window";
    target["pin"] = "follow";
    target["source"] = "view";
    Json::Value views;
    views["ok"] = true;
    views["followed"] = target;
    Json::Value view;
    view["id"] = "v1";
    view["name"] = "Main window";
    view["owner"] = "unreal-qt";
    view["active"] = "emu-1";
    view["position"] = 0;
    views["views"].append(view);
    caller->routes["GET /api/v1/surfaces/views"] = {200, views};
    Json::Value args;
    args["action"] = "views";
    const mcp::ToolResult listed = RunActions(*registry, args, *caller);
    ASSERT_FALSE(listed.isError) << listed.text;
    EXPECT_NE(listed.text.find("#0 PENTAGON 128K (emu-1) in v1 Main window"), std::string::npos) << listed.text;
    EXPECT_NE(listed.text.find("[unreal-qt] active emu-1"), std::string::npos) << listed.text;
    EXPECT_EQ(caller->calls.size(), 1u) << "no instance resolution for a process-wide verb";

    Json::Value pinned;
    pinned["ok"] = true;
    pinned["session"]["id"] = "s1";
    pinned["session"]["name"] = "Stream Deck + XL";
    pinned["session"]["target"] = target;
    pinned["session"]["target"]["pin"] = "view:v1";
    caller->routes["PUT /api/v1/surfaces/sessions/s1/pin"] = {200, pinned};
    args["action"] = "pin";
    args["session"] = "s1";
    args["pin"] = "view:v1";
    const mcp::ToolResult pin = RunActions(*registry, args, *caller);
    ASSERT_FALSE(pin.isError) << pin.text;
    EXPECT_EQ(caller->calls.back().method, "PUT");
    EXPECT_EQ(caller->calls.back().body["target"].asString(), "view:v1");
    EXPECT_NE(pin.text.find("s1 Stream Deck + XL -> #0 PENTAGON"), std::string::npos) << pin.text;

    Json::Value missing;
    missing["action"] = "pin";
    EXPECT_TRUE(RunActions(*registry, missing, *caller).isError) << "pin needs a session";

    Json::Value readouts;
    readouts["ok"] = true;
    readouts["instance"] = "emu-1";
    readouts["values"]["run.state"]["state"] = "paused";
    readouts["unknown"] = Json::Value(Json::arrayValue);
    caller->routes["GET /api/v1/surfaces/readouts?instance=emu-1&names=run.state"] = {200, readouts};
    Json::Value read;
    read["action"] = "readouts";
    read["target"] = "emu-1";
    read["names"] = "run.state";
    const mcp::ToolResult values = RunActions(*registry, read, *caller);
    ASSERT_FALSE(values.isError) << values.text << " " << caller->calls.back().path;
    EXPECT_NE(values.text.find("run.state = {\"state\":\"paused\"}"), std::string::npos) << values.text;

    Json::Value activity;
    activity["ok"] = true;
    activity["last"] = 7;
    activity["dropped"] = 0;
    Json::Value entry;
    entry["seq"] = 7;
    entry["id"] = "run.pause_toggle";
    entry["value"]["p"] = true;
    entry["origin"] = "host_device";
    entry["surface"] = "Xbox pad";
    entry["ok"] = true;
    activity["entries"].append(entry);
    caller->routes["GET /api/v1/surfaces/activity?since=3"] = {200, activity};
    Json::Value since;
    since["action"] = "activity";
    since["since"] = 3;
    const mcp::ToolResult feed = RunActions(*registry, since, *caller);
    ASSERT_FALSE(feed.isError) << feed.text << " " << caller->calls.back().path;
    EXPECT_NE(feed.text.find("7 run.pause_toggle {\"p\":true} by host_device (Xbox pad) ok"), std::string::npos)
        << feed.text;
}
