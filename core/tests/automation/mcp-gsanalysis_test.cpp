// MCP gs_analysis tool (mcp-gsanalysis.h): the switch becomes POST /control/audio/gs/analysis with its body, the reads
// GET /state/audio/gs/coverage|heatmap with their query; the text is the report's (an agent may see only the text)

#include <gtest/gtest.h>

#include <json/json.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mcp-gsanalysis.h"

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
    const mcp::ToolDefinition* tool = registry.Find("gs_analysis");
    if (!tool)
        return mcp::ToolResult::Error("gs_analysis tool not registered");
    args["target"] = "e1";
    mcp::ToolResult result;
    tool->handler(args, caller, [&result](mcp::ToolResult value) { result = std::move(value); },
                  mcp::ProgressFn([](double, double, const std::string&) {}));
    return result;
}

Json::Value Reply(const std::string& text)
{
    Json::Value reply;
    reply["available"] = true;
    reply["text"] = text;
    return reply;
}
} // namespace

TEST(McpGsAnalysis_Test, TheSwitchPostsItsActionAndTheReadsCarryTheirQuery)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);
    caller->routes["GET /api/v1/emulator/e1"] = {200, Json::Value(Json::objectValue)};
    caller->routes["POST /api/v1/emulator/e1/control/audio/gs/analysis"] = {200, Reply("General Sound (gs): analysis on\n")};

    Json::Value on;
    on["action"] = "on";
    on["cpu"] = "ms";
    on["decay"] = 4;
    mcp::ToolResult result = RunTool(*registry, on, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_EQ(caller->calls.back().method, "POST");
    EXPECT_EQ(caller->calls.back().body["action"].asString(), "on");
    EXPECT_EQ(caller->calls.back().body["cpu"].asString(), "ms");
    EXPECT_EQ(caller->calls.back().body["decay"].asInt(), 4);
    EXPECT_EQ(result.text, "General Sound (gs): analysis on\n");

    caller->routes["GET /api/v1/emulator/e1/state/audio/gs/coverage?space=ram&page=3"] = {200, Reply("RAM (32 pages)\n")};
    Json::Value coverage;
    coverage["action"] = "coverage";
    coverage["space"] = "ram";
    coverage["page"] = 3;
    result = RunTool(*registry, coverage, *caller);
    ASSERT_FALSE(result.isError) << result.text << " " << caller->calls.back().path;
    EXPECT_EQ(result.text, "RAM (32 pages)\n");

    caller->routes["GET /api/v1/emulator/e1/state/audio/gs/heatmap?recent=dac%2Cwrite&frames=10&format=none"] = {
        200, Reply("heat map\n")};
    Json::Value heat;
    heat["action"] = "heatmap";
    heat["recent"] = "dac,write";
    heat["frames"] = 10;
    result = RunTool(*registry, heat, *caller);
    ASSERT_FALSE(result.isError) << result.text << " " << caller->calls.back().path;
    EXPECT_EQ(result.text, "heat map\n") << "the grid left out unless asked";

    Json::Value bad;
    bad["action"] = "nope";
    EXPECT_TRUE(RunTool(*registry, bad, *caller).isError);
}

TEST(McpGsAnalysis_Test, ARefusalCarriesTheReason)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);
    caller->routes["GET /api/v1/emulator/e1"] = {200, Json::Value(Json::objectValue)};
    Json::Value refusal;
    refusal["error"] = "Bad Request";
    refusal["message"] = "not available: the General Sound (lightweight) has no card CPU";
    caller->routes["POST /api/v1/emulator/e1/control/audio/gs/analysis"] = {400, refusal};
    Json::Value on;
    on["action"] = "on";
    const mcp::ToolResult result = RunTool(*registry, on, *caller);
    ASSERT_TRUE(result.isError);
    EXPECT_NE(result.text.find("not available"), std::string::npos) << result.text;
}
