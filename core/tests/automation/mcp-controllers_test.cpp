// MCP controllers tool: its action table is a copy of ControllerControl's (MCP is a WebAPI client and does not link
// the core); this keeps the two equal and checks the routes the actions become

#include <gtest/gtest.h>

#include "emulator/io/controllers/controllercontrol.h"
#ifdef ENABLE_CONTROLLERS
#include "hostinputcontrol.h"
#endif
#include "mcp-controllers.h"

#include <json/json.h>

#include <algorithm>
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
    std::vector<std::string> host = controllers::HostInputControl::Verbs();
    host.erase(std::remove(host.begin(), host.end(), "learn"), host.end());  // a person presses the controls
    EXPECT_EQ(verbs, host) << "the host verbs but learn, in the same order";
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

/// C10: role, pin, identify, database_import are POSTs with a body; probe and database GETs with the query; the text
/// carries what the reply says (an agent may see only the text)
TEST(McpControllers_Test, HostDeviceVerbsProbeAndDatabase)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);

    Json::Value done;
    done["ok"] = true;
    done["message"] = "Pad 1 is a debugger-remote (its debug bindings only, no connector)";
    caller->routes["POST /api/v1/controllers/role"] = {200, done};
    Json::Value role;
    role["action"] = "role";
    role["device"] = "fake:1";
    role["role"] = "debugger-remote";
    mcp::ToolResult result = RunControllersTool(*registry, role, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_EQ(result.text, done["message"].asString());
    EXPECT_EQ(caller->calls.back().body["role"].asString(), "debugger-remote");

    Json::Value pin;
    pin["action"] = "pin";
    pin["device"] = "fake:1";
    pin["pin"] = "pinned:emu-2";
    RunControllersTool(*registry, pin, *caller);
    EXPECT_EQ(caller->calls.back().method, "POST");
    EXPECT_EQ(caller->calls.back().path, "/api/v1/controllers/pin");
    EXPECT_EQ(caller->calls.back().body["pin"].asString(), "pinned:emu-2");

    Json::Value probed;
    probed["ok"] = true;
    Json::Value backend;
    backend["name"] = "iohid";
    backend["running"] = true;
    backend["devices"] = 1;
    Json::Value found;
    found["id"] = "usb:2dc8:6001:0100:x";
    found["name"] = "SN30 Pro";
    found["family"] = "8bitdo-sn30-pro";
    found["match"]["step"] = "vid-pid";
    found["match"]["score"] = 80;
    backend["found"].append(found);
    probed["backends"].append(backend);
    Json::Value missing;
    missing["name"] = "gameinput";
    missing["running"] = false;
    missing["reason"] = "no GameInput runtime";
    missing["devices"] = 0;
    probed["backends"].append(missing);
    probed["user_devices"] = "/u/devices.user.json";
    caller->routes["GET /api/v1/controllers/probe"] = {200, probed};
    Json::Value probe;
    probe["action"] = "probe";
    result = RunControllersTool(*registry, probe, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("iohid: running, 1 device(s)\n  usb:2dc8:6001:0100:x: SN30 Pro (8bitdo-sn30-pro, "
                               "database vid-pid 80)"),
              std::string::npos)
        << result.text;
    EXPECT_NE(result.text.find("gameinput: not running: no GameInput runtime"), std::string::npos) << result.text;

    Json::Value looked;
    looked["ok"] = true;
    looked["message"] = "045e:0b00 is Xbox Elite Wireless Controller Series 2 (xbox-elite-2, vid-pid, score 80)";
    looked["match"]["layout"] = "xbox";
    looked["match"]["role"] = "player";
    caller->routes["GET /api/v1/controllers/database?vidpid=045e%3A0b00"] = {200, looked};
    Json::Value database;
    database["action"] = "database";
    database["vidpid"] = "045e:0b00";
    result = RunControllersTool(*registry, database, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_EQ(caller->calls.back().method, "GET");
    EXPECT_NE(result.text.find("is Xbox Elite"), std::string::npos) << result.text;

    Json::Value imported;
    imported["ok"] = true;
    imported["message"] = "gamecontrollerdb: 2 controls of Pad, saved as user-12345678";
    imported["map"]["south"] = "button.0";
    imported["gamecontrollerdb"] = "03000000341200007856000000010000,Pad,a:b0,";
    caller->routes["POST /api/v1/controllers/database_import"] = {200, imported};
    Json::Value import;
    import["action"] = "database_import";
    import["text"] = "03000000341200007856000000010000,Pad,a:b0,b:b1,platform:Mac OS X,";
    result = RunControllersTool(*registry, import, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_NE(result.text.find("saved as user-12345678\nmap {\"south\":\"button.0\"}"), std::string::npos)
        << result.text;
    EXPECT_EQ(caller->calls.back().body["text"].asString(), import["text"].asString());

    Json::Value learn;
    learn["action"] = "learn";
    learn["device"] = "fake:1";
    EXPECT_TRUE(RunControllersTool(*registry, learn, *caller).isError) << "no learn on MCP";
}

/// aliases is a read (a GET) whose text names each old id -> new id; unalias a POST with the old id; the WebAPI
/// verbs and the MCP actions stay the same set (ActionsAndOptionsMatchControllerControl)
TEST(McpControllers_Test, AliasesTextAndUnaliasRoute)
{
    auto caller = std::make_shared<RecordingCaller>();
    auto registry = mcp::BuildFullRegistry(caller);

    Json::Value listed;
    listed["ok"] = true;
    Json::Value alias;
    alias["from"] = "gc:dualsense:1";
    alias["to"] = "bt:054c:0ce6:1";
    alias["connected"] = true;
    alias["name"] = "DualSense";
    listed["aliases"].append(alias);
    caller->routes["GET /api/v1/controllers/aliases"] = {200, listed};
    Json::Value args;
    args["action"] = "aliases";
    const mcp::ToolResult result = RunControllersTool(*registry, args, *caller);
    ASSERT_FALSE(result.isError) << result.text;
    EXPECT_EQ(caller->calls.back().method, "GET");
    EXPECT_NE(result.text.find("gc:dualsense:1 -> bt:054c:0ce6:1 (DualSense, connected)"), std::string::npos)
        << result.text;

    Json::Value done;
    done["ok"] = true;
    done["message"] = "removed the alias gc:dualsense:1 -> bt:054c:0ce6:1";
    caller->routes["POST /api/v1/controllers/unalias"] = {200, done};
    Json::Value unalias;
    unalias["action"] = "unalias";
    unalias["id"] = "gc:dualsense:1";
    const mcp::ToolResult removed = RunControllersTool(*registry, unalias, *caller);
    ASSERT_FALSE(removed.isError) << removed.text;
    EXPECT_EQ(caller->calls.back().method, "POST");
    EXPECT_EQ(caller->calls.back().body["id"].asString(), "gc:dualsense:1");
    EXPECT_NE(removed.text.find("removed the alias"), std::string::npos);

    Json::Value missing;
    missing["ok"] = false;
    missing["error"] = "unknown-alias";
    missing["message"] = "no device alias 'gc:none'";
    caller->routes["POST /api/v1/controllers/unalias"] = {404, missing};
    unalias["id"] = "gc:none";
    const mcp::ToolResult failed = RunControllersTool(*registry, unalias, *caller);
    EXPECT_TRUE(failed.isError);
    EXPECT_NE(failed.text.find("[unknown-alias]"), std::string::npos) << failed.text;
}
