// MCP streamdeck tool (Stream Deck integration daemon.md §7): each action becomes its /api/v1/streamdeck route with
// the same body; the text carries what an agent needs (an agent may see only the text); the verbs are the core's

#include <gtest/gtest.h>

#include <json/json.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "emulator/companions/streamdeckapi.h"
#include "mcp-streamdeck.h"

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

mcp::ToolResult RunTool(mcp::ToolRegistry& registry, Json::Value args, mcp::IApiCaller& caller)
{
    const mcp::ToolDefinition* tool = registry.Find("streamdeck");
    if (!tool)
        return mcp::ToolResult::Error("streamdeck tool not registered");
    mcp::ToolResult result;
    tool->handler(args, caller, [&result](mcp::ToolResult value) { result = std::move(value); },
                  mcp::ProgressFn([](double, double, const std::string&) {}));
    return result;
}
} // namespace

TEST(McpStreamDeck_Test, TheVerbsAreTheCores)
{
    EXPECT_EQ(mcp::StreamDeckToolVerbs(), companion::streamdeck::Verbs());
}

TEST(McpStreamDeck_Test, EachActionIsItsRouteAndTheTextSaysWhatHappened)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);

    Json::Value status;
    status["ok"] = true;
    status["status"]["state"] = "running";
    status["status"]["mode"] = "deck";
    status["status"]["activity"] = "flash";
    status["status"]["pid"] = 4711;
    status["status"]["own"] = true;
    status["status"]["api_port"] = 37053;
    status["status"]["deck_attached"] = 1;
    status["status"]["restarts"] = 0;
    status["status"]["daemon"]["version"] = "1.0";
    status["status"]["daemon"]["decks"] = 1;
    status["status"]["daemon"]["viewers"] = 2;
    status["status"]["daemon"]["surface"] = "connected";
    status["status"]["daemon"]["pairing"]["state"] = "paired";
    Json::Value deck;
    deck["serial"] = "A1B2C3";
    deck["model"] = "Stream Deck + XL";
    deck["brightness"] = 70;
    deck["real"] = true;
    status["status"]["decks"].append(deck);
    caller->routes["GET /api/v1/streamdeck/status"] = {200, status};
    mcp::ToolResult result = RunTool(*registry, Json::Value(Json::objectValue), *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("Stream Deck daemon running"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("pid 4711 (ours)"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("deck A1B2C3 Stream Deck + XL brightness 70 (USB)"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("pairing paired"), std::string::npos) << result.text;

    Json::Value settings;
    settings["ok"] = true;
    settings["settings"]["mode"] = "always";
    caller->routes["PUT /api/v1/streamdeck/settings"] = {200, settings};
    Json::Value change;
    change["action"] = "settings";
    change["mode"] = "always";
    result = RunTool(*registry, change, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_EQ(caller->calls.back().body["mode"].asString(), "always");
    EXPECT_NE(result.text.find("mode always"), std::string::npos);

    Json::Value pairing;
    pairing["ok"] = true;
    pairing["pairing"]["state"] = "pending";
    pairing["pairing"]["code"] = "418205";
    caller->routes["POST /api/v1/streamdeck/pair"] = {200, pairing};
    Json::Value pair;
    pair["action"] = "pair";
    result = RunTool(*registry, pair, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("code 418205 - the user approves it"), std::string::npos) << result.text;

    Json::Value bright;
    bright["ok"] = true;
    bright["deck"]["serial"] = "A1B2C3";
    bright["deck"]["brightness"] = 40;
    caller->routes["PUT /api/v1/streamdeck/decks/A1B2C3/brightness"] = {200, bright};
    Json::Value set;
    set["action"] = "brightness";
    set["deck"] = "A1B2C3";
    set["value"] = 40;
    result = RunTool(*registry, set, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_EQ(caller->calls.back().body["value"].asInt(), 40);
    EXPECT_NE(result.text.find("brightness 40"), std::string::npos);

    Json::Value unavailable;
    unavailable["ok"] = false;
    unavailable["error"] = "supervisor_unavailable";
    unavailable["message"] = "no unreal-qt";
    caller->routes["POST /api/v1/streamdeck/start"] = {503, unavailable};
    Json::Value start;
    start["action"] = "start";
    result = RunTool(*registry, start, *caller);
    EXPECT_TRUE(result.isError);
    EXPECT_NE(result.text.find("503"), std::string::npos) << result.text;

    Json::Value bad;
    bad["action"] = "explode";
    EXPECT_TRUE(RunTool(*registry, bad, *caller).isError);
}
