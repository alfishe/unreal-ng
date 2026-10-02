// MCP media tool: its action table is a copy of MediaControl's (MCP is a WebAPI
// client and does not link the core); this keeps the two equal

#include <gtest/gtest.h>

#include "emulator/media/mediacontrol.h"
#include "loaders/tape/loader_tape.h"
#include "mcp-slots.h"

#include <json/json.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

TEST(McpSlots_Test, ActionsAndOptionsMatchMediaControl)
{
    const auto& actions = mcp::MediaToolActions();
    std::vector<std::string> verbs;
    for (const auto& [verb, options] : actions)
    {
        verbs.push_back(verb);
        EXPECT_EQ(options, MediaControl::OptionsFor(verb)) << "options of " << verb;
    }
    EXPECT_EQ(verbs, MediaControl::Verbs()) << "the same verbs, in the same order";
}

TEST(McpSlots_Test, TapeExtensionsMatchTheTapeLoaders)
{
    std::vector<std::string> mcp = mcp::TapeExtensions();
    std::vector<std::string> core = TapeLoaderRegistry::Instance().SupportedExtensions();
    std::sort(mcp.begin(), mcp.end());
    std::sort(core.begin(), core.end());
    EXPECT_EQ(mcp, core) << "load_software offers what the tape loaders read";
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

    mcp::ToolResult RunMediaTool(mcp::ToolRegistry& registry, Json::Value args, mcp::IApiCaller& caller)
    {
        const mcp::ToolDefinition* tool = registry.Find("media");
        if (!tool)
            return mcp::ToolResult::Error("media tool not registered");
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

/// Actions become /media routes; the slot is URL-encoded, options go in the body
TEST(McpSlots_Test, ActionsBecomeMediaRoutes)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);
    caller->routes["GET /api/v1/emulator"] = {200, OneEmulator()};

    Json::Value ok;
    ok["ok"] = true;
    ok["slot"] = "fdd.b";
    ok["pending"] = false;
    caller->routes["POST /api/v1/emulator/emu-1/media/tag%3Afloppy%2Bboot/swap"] = {200, ok};

    Json::Value args;
    args["action"] = "swap";
    args["slot"] = "tag:floppy+boot";
    args["path"] = "games/elite-2.trd";
    args["save"] = true;
    const mcp::ToolResult result = RunMediaTool(*registry, args, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    const auto& call = caller->calls.back();
    EXPECT_EQ(call.method, "POST");
    EXPECT_EQ(call.path, "/api/v1/emulator/emu-1/media/tag%3Afloppy%2Bboot/swap");
    EXPECT_EQ(call.body["path"].asString(), "games/elite-2.trd");
    EXPECT_TRUE(call.body["save"].asBool());
    EXPECT_FALSE(call.body.isMember("action")) << "the action is the route, not an option";

    // An error reply carries the code and the message to the agent
    Json::Value dirty;
    dirty["ok"] = false;
    dirty["error"] = "dirty";
    dirty["message"] = "slot 'fdd.a' has 1 unsaved changes";
    caller->routes["POST /api/v1/emulator/emu-1/media/A/eject"] = {409, dirty};
    Json::Value eject;
    eject["action"] = "eject";
    eject["slot"] = "A";
    const mcp::ToolResult refused = RunMediaTool(*registry, eject, *caller);
    EXPECT_TRUE(refused.isError);
    EXPECT_NE(refused.text.find("[dirty]"), std::string::npos) << refused.text;

    Json::Value list;
    list["action"] = "list";
    RunMediaTool(*registry, list, *caller);
    EXPECT_EQ(caller->calls.back().method, "GET");
    EXPECT_EQ(caller->calls.back().path, "/api/v1/emulator/emu-1/media");
}

/// targets is a GET with the path in the query; the summary names the targets and the default
TEST(McpSlots_Test, TargetsBecomesAQuery)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);
    caller->routes["GET /api/v1/emulator"] = {200, OneEmulator()};

    Json::Value reply;
    reply["ok"] = true;
    reply["file"]["kinds"].append("sdcard");
    reply["file"]["kinds"].append("hdd");
    reply["file"]["format"] = "fat";
    reply["file"]["evidence"].append("FAT boot sector at sector 0");
    Json::Value zc;
    zc["action"] = "insert";
    zc["slot"] = "sd.zc";
    zc["label"] = "SD card (Z-Controller)";
    Json::Value ngs = zc;
    ngs["slot"] = "sd.ngs";
    ngs["label"] = "SD card (NeoGS)";
    reply["targets"].append(zc);
    reply["targets"].append(ngs);
    reply["default"] = Json::Value();
    reply["refusal"] = Json::Value();
    caller->routes["GET /api/v1/emulator/emu-1/media/targets?path=cards%2Fnedo%20os.img"] = {200, reply};

    Json::Value args;
    args["action"] = "targets";
    args["path"] = "cards/nedo os.img";
    const mcp::ToolResult result = RunMediaTool(*registry, args, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_EQ(caller->calls.back().method, "GET");
    EXPECT_EQ(caller->calls.back().path, "/api/v1/emulator/emu-1/media/targets?path=cards%2Fnedo%20os.img");
    EXPECT_NE(result.text.find("file: sdcard hdd (fat)"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("sd.ngs - SD card (NeoGS)"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("several targets: ask the user"), std::string::npos) << result.text;
}
