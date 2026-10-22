// MCP audio_mixer tool (mcp-audio.h): each action becomes its WebAPI route (/audio/mixer, /audio/mixer/{source},
// /audio/levels, /settings) with the same body; the text carries what an agent needs (an agent may see only the text)

#include <gtest/gtest.h>

#include <json/json.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mcp-audio.h"

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
    const mcp::ToolDefinition* tool = registry.Find("audio_mixer");
    if (!tool)
        return mcp::ToolResult::Error("audio_mixer tool not registered");
    args["target"] = "e1";
    mcp::ToolResult result;
    tool->handler(args, caller, [&result](mcp::ToolResult value) { result = std::move(value); },
                  mcp::ProgressFn([](double, double, const std::string&) {}));
    return result;
}

Json::Value Mixer()
{
    Json::Value mixer;
    mixer["master"]["muted"] = false;
    mixer["master"]["sample_rate_hz"] = 44100;
    Json::Value beeper;
    beeper["source"] = "beeper";
    beeper["name"] = "Beeper";
    beeper["muted"] = false;
    beeper["solo"] = false;
    beeper["audible"] = true;
    beeper["volume"] = 1.0;
    beeper["gain_db"] = 0.0;
    beeper["peak"] = 0.25;
    mixer["devices"].append(beeper);
    Json::Value ay;
    ay["source"] = "ay1";
    ay["name"] = "AY";
    ay["muted"] = false;
    ay["solo"] = false;
    ay["audible"] = true;
    ay["volume"] = 0.5;
    ay["gain_db"] = -6.0;
    ay["peak"] = 0.1;
    for (const char* key : {"ay1.a", "ay1.b", "ay1.c"})
    {
        Json::Value channel;
        channel["key"] = key;
        channel["name"] = std::string(key).substr(4);
        channel["muted"] = std::string(key) == "ay1.b";
        channel["solo"] = false;
        channel["audible"] = std::string(key) != "ay1.b";
        channel["volume"] = 1.0;
        ay["channels"].append(channel);
    }
    mixer["devices"].append(ay);
    return mixer;
}
} // namespace

TEST(McpAudio_Test, GetListsTheRowsAndOneSourceItsChannels)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);
    caller->routes["GET /api/v1/emulator/e1"] = {200, Json::Value(Json::objectValue)};
    caller->routes["GET /api/v1/emulator/e1/audio/mixer"] = {200, Mixer()};

    mcp::ToolResult result = RunTool(*registry, Json::Value(Json::objectValue), *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("master: on, 44100 Hz"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("beeper (Beeper): volume 1.00 (0.0 dB) peak 0.250"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("ay1 (AY): volume 0.50 (-6.0 dB) peak 0.100 [3 channels]"), std::string::npos)
        << result.text;
    EXPECT_EQ(result.text.find("ay1.b"), std::string::npos) << "the rows alone";

    Json::Value args;
    args["source"] = "ay1";
    result = RunTool(*registry, args, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("  ay1.b (b): muted silent volume 1.00"), std::string::npos) << result.text;
    EXPECT_EQ(result.text.find("beeper"), std::string::npos) << "only the row asked for";
}

TEST(McpAudio_Test, SetPutsTheChangeOnTheSourcesRouteAndSettingsUseTheSettingsRoutes)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);
    caller->routes["GET /api/v1/emulator/e1"] = {200, Json::Value(Json::objectValue)};
    caller->routes["PUT /api/v1/emulator/e1/audio/mixer/ay1.b"] = {200, Mixer()};

    Json::Value args;
    args["action"] = "set";
    args["source"] = "ay1.b";
    args["muted"] = true;
    args["volume"] = 0.75;
    mcp::ToolResult result = RunTool(*registry, args, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_EQ(caller->calls.back().method, "PUT");
    EXPECT_TRUE(caller->calls.back().body["muted"].asBool());
    EXPECT_DOUBLE_EQ(caller->calls.back().body["volume"].asDouble(), 0.75);
    EXPECT_NE(result.text.find("set ay1.b"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("  ay1.b (b): muted"), std::string::npos) << result.text;

    Json::Value missing;
    missing["action"] = "set";
    EXPECT_TRUE(RunTool(*registry, missing, *caller).isError) << "set takes source";

    Json::Value settings;
    settings["settings"]["audio"]["ay_stereo"] = "abc";
    settings["settings"]["audio"]["fm_trim_db"] = Json::Value();
    caller->routes["GET /api/v1/emulator/e1/settings"] = {200, settings};
    Json::Value read;
    read["action"] = "settings";
    result = RunTool(*registry, read, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("ay_stereo = abc"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("fm_trim_db = unavailable"), std::string::npos) << result.text;

    Json::Value changed;
    changed["name"] = "ay_stereo";
    changed["value"] = "mono";
    changed["message"] = "ay_stereo set (applied at the next frame boundary)";
    caller->routes["PUT /api/v1/emulator/e1/settings/ay_stereo"] = {200, changed};
    Json::Value set;
    set["action"] = "set_setting";
    set["name"] = "ay_stereo";
    set["value"] = "mono";
    result = RunTool(*registry, set, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_EQ(caller->calls.back().body["value"].asString(), "mono");
    EXPECT_NE(result.text.find("ay_stereo = mono"), std::string::npos) << result.text;

    Json::Value bad;
    bad["action"] = "nope";
    EXPECT_TRUE(RunTool(*registry, bad, *caller).isError);
}

TEST(McpAudio_Test, LevelsShowEveryChannel)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);
    caller->routes["GET /api/v1/emulator/e1"] = {200, Json::Value(Json::objectValue)};
    Json::Value levels = Mixer();
    levels["solo"] = true;
    caller->routes["GET /api/v1/emulator/e1/audio/levels"] = {200, levels};
    Json::Value args;
    args["action"] = "levels";
    const mcp::ToolResult result = RunTool(*registry, args, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("solo active"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("  ay1.c (c):"), std::string::npos) << result.text;
}
