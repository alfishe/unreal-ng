/// @file mcp-preset-test.cpp
/// @brief The preset machine verb (MachinePreset, RE API efficiency V3) on MCP and in the WebAPI's manifest: emulator_manage
/// action preset forwards its arguments to POST /api/v1/emulator/preset, answers the core's text (an agent may see only the
/// text) and keeps the new control session as its own; the route is registered and every argument is described.
/// MachinePreset_Test checks the verb itself; the drogon code is not linked into core-tests, so the sources are read.

#include <gtest/gtest.h>

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/preset/machinepreset.h"
#include "mcp-tool-utils.h"
#include "mcp-tools.h"

namespace
{

/// Answers every call with one scripted reply and records the call and the session the tool sets
class PresetApiCaller : public mcp::IApiCaller
{
public:
    int status = 201;
    Json::Value reply;
    std::string method, path, session;
    Json::Value body;
    int calls = 0;

    void Call(const std::string& callMethod, const std::string& callPath, const Json::Value* callBody,
              ApiCallback callback) override
    {
        calls++;
        method = callMethod;
        path = callPath;
        body = callBody ? *callBody : Json::Value();
        callback(status, reply);
    }

    void CallRaw(const std::string&, const std::string&, const std::vector<uint8_t>&,
                 const std::map<std::string, std::string>&, ApiCallback callback) override
    {
        calls++;
        callback(status, reply);
    }

    void SetSession(const std::string& sessionId) override { session = sessionId; }
    std::string Session() const override { return session; }
};

mcp::ToolResult RunManage(mcp::ToolRegistry& registry, const Json::Value& args, mcp::IApiCaller& caller)
{
    const mcp::ToolDefinition* tool = registry.Find("emulator_manage");
    if (!tool)
        return mcp::ToolResult::Error("emulator_manage is not registered");
    mcp::ToolResult result;
    tool->handler(args, caller, [&result](mcp::ToolResult value) { result = std::move(value); },
                  [](double, double, const std::string&) {});
    return result;
}

std::string ReadAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(McpPreset_Test, PresetForwardsItsArgumentsAndAnswersTheText)
{
    auto caller = std::make_shared<PresetApiCaller>();
    const std::unique_ptr<mcp::ToolRegistry> registry = mcp::BuildFullRegistry(caller);

    caller->reply["id"] = "emu-preset";
    caller->reply["text"] = "Preset machine emu-preset ready: 48K (48 KB), RAM zero, paused at frame 0 t 3.\n"
                            "Cards: edge.1 = kempston.\nRun control: held by you (mcp, session s-1).";
    caller->reply["control"]["held"] = true;
    caller->reply["control"]["session"] = "s-1";

    Json::Value args;
    args["action"] = "preset";
    args["model"] = "48K";
    args["cards"].append("edge.1=kempston");
    args["features"]["kempstonmouse"] = false;
    args["replace"] = true;
    args["_mcp_client"] = "re-agent";
    args["_peer"] = "10.0.0.7:5555";
    const mcp::ToolResult result = RunManage(*registry, args, *caller);

    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_EQ(caller->calls, 1) << "one call: no target lookup, no polling";
    EXPECT_EQ(caller->method, "POST");
    EXPECT_EQ(caller->path, "/api/v1/emulator/preset");
    EXPECT_EQ(caller->body["model"].asString(), "48K");
    EXPECT_EQ(caller->body["cards"][0].asString(), "edge.1=kempston");
    EXPECT_FALSE(caller->body["features"]["kempstonmouse"].asBool());
    EXPECT_TRUE(caller->body["replace"].asBool());
    EXPECT_FALSE(caller->body.isMember("muted")) << "an argument not given is left to the verb's default";
    EXPECT_EQ(caller->body["surface"].asString(), "mcp");
    EXPECT_EQ(caller->body["client"].asString(), "re-agent");
    EXPECT_EQ(caller->body["peer"].asString(), "10.0.0.7:5555");
    EXPECT_FALSE(caller->body.isMember("action"));

    // The text is readable on its own; the session is this client's from now on
    EXPECT_EQ(result.text.rfind("Preset machine emu-preset ready: 48K (48 KB), RAM zero, paused at frame 0 t 3.", 0), 0u)
        << result.text;
    EXPECT_NE(result.text.find("Cards: edge.1 = kempston."), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("release_control ends the session"), std::string::npos) << result.text;
    EXPECT_EQ(caller->session, "s-1");
    EXPECT_EQ(result.structured["id"].asString(), "emu-preset");

    // The schema names the action and its arguments (the dispatcher refuses an argument the schema lacks)
    const Json::Value& schema = registry->Find("emulator_manage")->inputSchema;
    bool listed = false;
    for (const Json::Value& action : schema["properties"]["action"]["enum"])
        listed = listed || action.asString() == "preset";
    EXPECT_TRUE(listed);
    for (const char* property : {"model", "ram", "cards", "features", "muted", "paused", "fast_tape", "take_control", "replace"})
        EXPECT_TRUE(schema["properties"].isMember(property)) << property;
}

TEST(McpPreset_Test, ARefusalIsTheCoresMessageAndTakesNoSession)
{
    auto caller = std::make_shared<PresetApiCaller>();
    const std::unique_ptr<mcp::ToolRegistry> registry = mcp::BuildFullRegistry(caller);
    caller->status = 400;
    caller->reply["message"] = "features: unknown feature 'kempstonmice'; the features: contention, kempstonmouse";

    Json::Value args;
    args["action"] = "preset";
    args["model"] = "48K";
    args["features"]["kempstonmice"] = false;
    const mcp::ToolResult result = RunManage(*registry, args, *caller);
    EXPECT_TRUE(result.isError);
    EXPECT_NE(result.text.find("preset refused (HTTP 400): features: unknown feature 'kempstonmice'"), std::string::npos)
        << result.text;
    EXPECT_NE(result.text.find("kempstonmouse"), std::string::npos) << "the valid names are in the text";
    EXPECT_TRUE(caller->session.empty());

    // Without take_control the answer carries no session: none is set
    caller->status = 201;
    caller->reply = Json::Value();
    caller->reply["text"] = "Preset machine emu-2 ready: 48K.\nRun control: not taken.";
    caller->reply["control"]["held"] = false;
    args["features"] = Json::Value(Json::objectValue);
    args["take_control"] = false;
    const mcp::ToolResult plain = RunManage(*registry, args, *caller);
    ASSERT_FALSE(plain.isError) << plain.text;
    EXPECT_FALSE(caller->body["take_control"].asBool());
    EXPECT_TRUE(caller->session.empty());
    EXPECT_EQ(plain.text, "Preset machine emu-2 ready: 48K.\nRun control: not taken.");
}

TEST(McpPreset_Test, TheRouteAndEveryArgumentAreInTheManifest)
{
    const std::filesystem::path webapi = TestPathHelper::FindProjectRoot() / "core" / "automation" / "webapi" / "src";
    const std::string routes = ReadAll(webapi / "emulator_api.h");
    EXPECT_NE(routes.find("\"/api/v1/emulator/preset\", drogon::Post"), std::string::npos);

    const std::string manifest = ReadAll(webapi / "openapi" / "openapi_lifecycle.inc");
    EXPECT_NE(manifest.find("paths[\"/api/v1/emulator/preset\"][\"post\"]"), std::string::npos);
    for (const std::string& argument : MachinePreset::ArgumentNames())
        EXPECT_NE(manifest.find("properties[\"" + argument + "\"][\"description\"]"), std::string::npos)
            << "argument " << argument << " is not described";
}
