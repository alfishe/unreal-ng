// MCP bus_analyzer tool (docs/inprogress/2026-10-11-bus-analyzer/technical-design.md §7): actions become /bus routes, the
// text carries every field (an agent may see only content[] text)

#include <gtest/gtest.h>

#include <json/json.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mcp-bus.h"
#include "mcp-tools.h"

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
            callback(200, Json::Value());
    }
    void CallRaw(const std::string& method, const std::string& path, const std::vector<uint8_t>&,
                 const std::map<std::string, std::string>&, ApiCallback callback) override
    {
        calls.push_back({method, path, Json::Value()});
        callback(200, Json::Value());
    }
};

mcp::ToolResult RunBusTool(mcp::ToolRegistry& registry, Json::Value args, mcp::IApiCaller& caller)
{
    const mcp::ToolDefinition* tool = registry.Find("bus_analyzer");
    if (!tool)
        return mcp::ToolResult::Error("bus_analyzer not registered");
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

Json::Value Header()
{
    Json::Value bus;
    bus["available"] = true;
    bus["id"] = "zc.spi";
    bus["protocol"] = "spi";
    bus["label"] = "Z-Controller SPI";
    bus["byte_time_ns"] = 607.1;
    bus["trace"]["enabled"] = true;
    bus["trace"]["size"] = 3;
    bus["trace"]["capacity"] = 65536;
    bus["checks"]["enabled"] = true;
    bus["checks"]["findings"] = 1;
    bus["checks"]["by_rule"]["dma_collision"] = 1;
    Json::Value dev;
    dev["slot"] = 0;
    dev["name"] = "sd";
    dev["sd"]["present"] = true;
    dev["sd"]["type"] = "SDHC";
    dev["sd"]["state"] = "ready";
    dev["sd"]["timing"] = "ideal";
    dev["sd"]["seed"] = 1;
    bus["devices"].append(dev);
    return bus;
}
}  // namespace

TEST(McpBus_Test, ActionsBecomeBusRoutes)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);
    caller->routes["GET /api/v1/emulator"] = {200, OneEmulator()};

    Json::Value args;
    args["action"] = "enable";
    ASSERT_FALSE(RunBusTool(*registry, args, *caller).isError);
    EXPECT_EQ(caller->calls.back().method, "POST");
    EXPECT_EQ(caller->calls.back().path, "/api/v1/emulator/emu-1/bus/all/trace-journal") << "enable without a bus: every bus";
    EXPECT_TRUE(caller->calls.back().body["enabled"].asBool());
    EXPECT_TRUE(caller->calls.back().body["checks"].asBool()) << "checks on by default";

    args = Json::Value();
    args["action"] = "events";
    args["bus"] = "zc.spi";
    args["kinds"] = "sd";
    args["since"] = 10;
    RunBusTool(*registry, args, *caller);
    EXPECT_EQ(caller->calls.back().path, "/api/v1/emulator/emu-1/bus/zc.spi/events?kinds=sd&since=10&limit=60");

    args = Json::Value();
    args["action"] = "sd_timing";
    args["bus"] = "zc.spi";
    args["profile"] = "slow";
    RunBusTool(*registry, args, *caller);
    EXPECT_EQ(caller->calls.back().method, "POST");
    EXPECT_EQ(caller->calls.back().path, "/api/v1/emulator/emu-1/bus/zc.spi/sd-timing");
    EXPECT_EQ(caller->calls.back().body["profile"].asString(), "slow");

    args = Json::Value();
    args["action"] = "export";
    args["bus"] = "zc.spi";
    args["path"] = "scratch/a.vcd";
    RunBusTool(*registry, args, *caller);
    EXPECT_EQ(caller->calls.back().path, "/api/v1/emulator/emu-1/bus/zc.spi/export?format=vcd&path=scratch%2Fa.vcd");
}

TEST(McpBus_Test, MissingArgumentsAreToldNotSent)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);
    Json::Value args;
    args["action"] = "events";
    mcp::ToolResult r = RunBusTool(*registry, args, *caller);
    EXPECT_TRUE(r.isError);
    EXPECT_NE(r.text.find("bus: required"), std::string::npos) << r.text;
    args["action"] = "export";
    args["bus"] = "zc.spi";
    r = RunBusTool(*registry, args, *caller);
    EXPECT_TRUE(r.isError);
    EXPECT_NE(r.text.find("path: required"), std::string::npos) << r.text;
    args["action"] = "sniff";
    EXPECT_TRUE(RunBusTool(*registry, args, *caller).isError);
    EXPECT_TRUE(caller->calls.empty()) << "nothing reached the WebAPI";
}

TEST(McpBus_Test, TheTextCarriesTheData)
{
    Json::Value reply = Header();
    Json::Value e;
    e["seq"] = 7;
    e["frame"] = 12;
    e["t"] = 3456;
    e["pc"] = "0x8000";
    e["source"] = "dma";
    e["slot"] = 0;
    e["kind"] = "byte";
    e["mosi"] = "0xFF";
    e["miso"] = "0xFE";
    e["read"] = true;
    reply["events"].append(e);
    const std::string events = mcp::BusAnalyzerText("events", reply);
    EXPECT_NE(events.find("zc.spi (spi): Z-Controller SPI"), std::string::npos) << events;
    EXPECT_NE(events.find("checks on findings=1 (dma_collision=1)"), std::string::npos) << events;
    EXPECT_NE(events.find("slot 0: sd sd present=yes type=SDHC state=ready timing=ideal"), std::string::npos) << events;
    EXPECT_NE(events.find("#7 f12 t3456 pc=0x8000 dma slot=0 byte mosi=0xFF miso=0xFE (read)"), std::string::npos) << events;

    reply = Header();
    Json::Value f;
    f["seq"] = 1;
    f["event_seq"] = 9;
    f["frame"] = 3;
    f["t"] = 100;
    f["pc"] = "0x6012";
    f["source"] = "cpu";
    f["slot"] = 0;
    f["rule"] = "dma_collision";
    f["severity"] = "error";
    f["value"] = "0xFF";
    f["detail"] = "the CPU accessed the SPI ports while an SPI DMA transfer was running";
    reply["findings"].append(f);
    const std::string findings = mcp::BusAnalyzerText("findings", reply);
    EXPECT_NE(findings.find("1 finding(s)"), std::string::npos) << findings;
    EXPECT_NE(findings.find("#1 error dma_collision f3 t100 pc=0x6012 cpu slot=0 value=0xFF event=#9 - the CPU accessed"),
              std::string::npos)
        << findings;
}
