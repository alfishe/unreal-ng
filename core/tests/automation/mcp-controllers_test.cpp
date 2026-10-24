// MCP controllers tool: its action table is a copy of ControllerControl's (MCP is a WebAPI client and does not link
// the core); this keeps the two equal and checks the routes the actions become

#include <gtest/gtest.h>

#include "emulator/io/controllers/controllercontrol.h"
#ifdef ENABLE_CONTROLLERS
#include "hostinputcontrol.h"
#endif
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

#ifdef ENABLE_CONTROLLERS
    verbs.clear();
    for (const auto& [verb, options] : mcp::HostControllerToolActions())
    {
        verbs.push_back(verb);
        EXPECT_EQ(options, controllers::HostInputControl::OptionsFor(verb)) << "options of " << verb;
    }
    EXPECT_EQ(verbs, controllers::HostInputControl::Verbs()) << "the host verbs, in the same order";
#endif
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

/// A host action needs no emulator: reads become a GET with the options in the query, changes a POST with a body; an
/// object spec goes as its JSON text
TEST(McpControllers_Test, HostActionsBecomeHostRoutes)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);

    Json::Value listed;
    listed["ok"] = true;
    Json::Value profile;
    profile["id"] = "builtin/game/vradarks-revenge";
    profile["label"] = "Vradark's Revenge";
    profile["applied"] = true;
    profile["loaded"] = true;
    listed["profiles"].append(profile);
    caller->routes["GET /api/v1/controllers/profiles?set=game"] = {200, listed};
    Json::Value args;
    args["action"] = "profiles";
    args["set"] = "game";
    const mcp::ToolResult result = RunControllersTool(*registry, args, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_EQ(caller->calls.size(), 1u) << "no emulator lookup";
    EXPECT_NE(result.text.find("* builtin/game/vradarks-revenge"), std::string::npos) << result.text;

    Json::Value bind;
    bind["action"] = "bind";
    bind["control"] = "south";
    bind["family"] = "dualsense";
    bind["spec"]["key"] = "ENTER";
    RunControllersTool(*registry, bind, *caller);
    const auto& call = caller->calls.back();
    EXPECT_EQ(call.method, "POST");
    EXPECT_EQ(call.path, "/api/v1/controllers/bind");
    EXPECT_EQ(call.body["spec"].asString(), R"({"key":"ENTER"})");
    EXPECT_FALSE(call.body.isMember("action"));
}

/// An agent may see only the text: devices names each one's battery, whether its light can be set and its notice;
/// light is a POST with the device and the colour
TEST(McpControllers_Test, DevicesTextCarriesBatteryLightAndNotice)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);

    Json::Value listed;
    listed["ok"] = true;
    Json::Value pad;
    pad["id"] = "bt:054c:0ce6:1";
    pad["name"] = "DualSense";
    pad["family"] = "dualsense";
    pad["role"] = "player";
    pad["role_set"] = false;
    pad["layers"].append("play");
    pad["context"] = "";
    pad["battery"]["known"] = true;
    pad["battery"]["level"] = 80;
    pad["battery"]["text"] = "80 %";
    pad["light"]["available"] = true;
    listed["devices"].append(pad);
    Json::Value elite;
    elite["id"] = "bt:045e:0b22:2";
    elite["name"] = "Xbox Elite";
    elite["family"] = "xbox-elite-2";
    elite["role"] = "player";
    elite["role_set"] = false;
    elite["layers"].append("play");
    elite["context"] = "";
    elite["battery"]["known"] = false;
    elite["battery"]["text"] = "unknown";
    elite["light"]["available"] = false;
    elite["light"]["reason"] = "no light";
    elite["notice"] = "Install the udev rule";
    listed["devices"].append(elite);
    caller->routes["GET /api/v1/controllers/devices"] = {200, listed};
    Json::Value args;
    args["action"] = "devices";
    const mcp::ToolResult result = RunControllersTool(*registry, args, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("DualSense (dualsense) - role player (default), layers play, battery 80 %, light settable"),
              std::string::npos)
        << result.text;
    EXPECT_NE(result.text.find("battery unknown, light none\n  notice: Install the udev rule"), std::string::npos)
        << result.text;
    EXPECT_EQ(result.structured["devices"][1]["notice"].asString(), "Install the udev rule") << "and the JSON as is";

    Json::Value done;
    done["ok"] = true;
    done["message"] = "light #ff0000 on DualSense";
    caller->routes["POST /api/v1/controllers/light"] = {200, done};
    Json::Value light;
    light["action"] = "light";
    light["device"] = "bt:054c:0ce6:1";
    light["color"] = "#ff0000";
    const mcp::ToolResult set = RunControllersTool(*registry, light, *caller);
    ASSERT_FALSE(set.isError) << set.text;
    EXPECT_EQ(set.text, "light #ff0000 on DualSense");
    EXPECT_EQ(caller->calls.back().method, "POST");
    EXPECT_EQ(caller->calls.back().body["color"].asString(), "#ff0000");
    EXPECT_EQ(caller->calls.back().body["device"].asString(), "bt:054c:0ce6:1");
}

/// database is a GET with the lookup in the query and the match in one line of text; database-save a POST with the
/// entry as text
TEST(McpControllers_Test, DatabaseLookupAndSaveRoutes)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);

    Json::Value found;
    found["ok"] = true;
    found["families"] = 27;
    found["user"].append("xbox-elite-2");
    found["match"]["family"] = "xbox-elite-2";
    found["match"]["name"] = "Xbox Elite Wireless Controller Series 2";
    found["match"]["layout"] = "xbox";
    found["match"]["images"] = "my-elite";
    found["match"]["step"] = "exact";
    found["match"]["score"] = 100;
    found["match"]["user"] = true;
    caller->routes["GET /api/v1/controllers/database?bus=bt&vidpid=045e%3A0b22"] = {200, found};
    Json::Value args;
    args["action"] = "database";
    args["vidpid"] = "045e:0b22";
    args["bus"] = "bt";
    const mcp::ToolResult result = RunControllersTool(*registry, args, *caller);
    ASSERT_FALSE(result.isError) << result.text << " " << caller->calls.back().path;
    EXPECT_NE(result.text.find("xbox-elite-2: Xbox Elite Wireless Controller Series 2 (layout xbox, images my-elite, "
                               "exact match, score 100, from devices.user.json)"),
              std::string::npos)
        << result.text;
    EXPECT_NE(result.text.find("27 families; devices.user.json: xbox-elite-2"), std::string::npos) << result.text;

    Json::Value saved;
    saved["ok"] = true;
    saved["message"] = "saved xbox-elite-2";
    caller->routes["POST /api/v1/controllers/database-save"] = {200, saved};
    Json::Value save;
    save["action"] = "database-save";
    save["text"] = R"({"id": "xbox-elite-2", "images": "my-elite"})";
    const mcp::ToolResult put = RunControllersTool(*registry, save, *caller);
    ASSERT_FALSE(put.isError) << put.text;
    EXPECT_EQ(caller->calls.back().method, "POST");
    EXPECT_EQ(caller->calls.back().body["text"].asString(), R"({"id": "xbox-elite-2", "images": "my-elite"})");
}

/// probe and reports are GETs with the device in the query, their answers in text an agent can read; player keeps its
/// connector in the body (the host pad's connector, not a machine target); database-import a POST with the text
TEST(McpControllers_Test, ProbeReportsPlayerAndImportRoutes)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);

    Json::Value probed;
    probed["ok"] = true;
    Json::Value backend;
    backend["name"] = "iohid";
    backend["running"] = true;
    backend["devices"] = 1;
    probed["backends"].append(backend);
    Json::Value endpoint;
    endpoint["backend"] = "iohid";
    endpoint["role"] = "input";
    endpoint["name"] = "Xbox Wireless Controller";
    endpoint["controls"] = 18;
    endpoint["driver"] = "xbox gip";
    endpoint["rumble"] = true;
    endpoint["light"] = false;
    endpoint["battery"] = true;
    endpoint["detail"] = "";
    probed["endpoints"].append(endpoint);
    caller->routes["GET /api/v1/controllers/probe?device=bt%3A045e%3A0b22%3A1"] = {200, probed};
    Json::Value args;
    args["action"] = "probe";
    args["device"] = "bt:045e:0b22:1";
    const mcp::ToolResult probe = RunControllersTool(*registry, args, *caller);
    ASSERT_FALSE(probe.isError) << probe.text << " " << caller->calls.back().path;
    EXPECT_NE(probe.text.find("iohid ok, 1 devices"), std::string::npos) << probe.text;
    EXPECT_NE(probe.text.find("endpoint iohid (input): Xbox Wireless Controller, 18 controls via xbox gip, rumble, "
                              "battery"),
              std::string::npos)
        << probe.text;

    Json::Value reports;
    reports["ok"] = true;
    reports["message"] = "2 reports from bt:045e:0b22:1";
    Json::Value report;
    report["ms"] = 120;
    report["size"] = 3;
    report["hex"] = "01 00 80";
    reports["reports"].append(report);
    caller->routes["GET /api/v1/controllers/reports?device=bt%3A045e%3A0b22%3A1"] = {200, reports};
    args["action"] = "reports";
    const mcp::ToolResult raw = RunControllersTool(*registry, args, *caller);
    ASSERT_FALSE(raw.isError) << raw.text << " " << caller->calls.back().path;
    EXPECT_NE(raw.text.find("01 00 80 (3 B)"), std::string::npos) << raw.text;

    Json::Value player;
    player["ok"] = true;
    player["message"] = "bt:045e:0b22:1 drives sinclair-1 (chosen)";
    caller->routes["POST /api/v1/controllers/player"] = {200, player};
    Json::Value choose;
    choose["action"] = "player";
    choose["device"] = "bt:045e:0b22:1";
    choose["connector"] = "sinclair-1";
    const mcp::ToolResult chosen = RunControllersTool(*registry, choose, *caller);
    ASSERT_FALSE(chosen.isError) << chosen.text << " " << caller->calls.back().path;
    EXPECT_EQ(caller->calls.back().method, "POST");
    EXPECT_EQ(caller->calls.back().body["connector"].asString(), "sinclair-1");
    EXPECT_EQ(caller->calls.back().body["device"].asString(), "bt:045e:0b22:1");

    Json::Value imported;
    imported["ok"] = true;
    imported["message"] = "imported user-2dc86101 from gamecontrollerdb";
    imported["entry"]["id"] = "user-2dc86101";
    imported["skipped"].append("platform");
    caller->routes["POST /api/v1/controllers/database-import"] = {200, imported};
    Json::Value import;
    import["action"] = "database-import";
    import["text"] = "03000000c82d00000161000000000000,8BitDo SN30 Pro,a:b1,platform:Mac OS X,";
    const mcp::ToolResult taken = RunControllersTool(*registry, import, *caller);
    ASSERT_FALSE(taken.isError) << taken.text;
    EXPECT_EQ(caller->calls.back().method, "POST");
    EXPECT_NE(taken.text.find("user-2dc86101"), std::string::npos) << taken.text;
    EXPECT_NE(taken.text.find("not taken over: platform"), std::string::npos) << taken.text;
}
