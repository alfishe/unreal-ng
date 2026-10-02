/// @file joystickjson_test.cpp
/// @brief Kempston joystick WebAPI (joystick TDD §5, JOY-13): the routes' request parsing, state change,
/// status codes and JSON against a real ZX-Evo (ATM3) instance, plus the OpenAPI document of the routes.
/// The Drogon handlers in api/joystick_api.cpp only forward to JoystickWeb::Handle / StatusReply.

#include <gtest/gtest.h>
#include <json/json.h>

#include <set>
#include <string>

#include "../../../../automation/webapi/src/common/joystickjson.h"
#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"

namespace
{
Json::Value Parse(const std::string& text)
{
    Json::Value value;
    Json::Reader reader;
    EXPECT_TRUE(reader.parse(text, value)) << text;
    return value;
}
}  // namespace

class JoystickWeb_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    DebugJoystickManager* _manager = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _manager = _context->pDebugManager->GetJoystickManager();
        ASSERT_NE(_manager, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    JoystickWeb::Reply Post(const std::string& verb, const std::string& body)
    {
        const Json::Value json = Parse(body);
        return JoystickWeb::Handle(*_manager, verb, &json);
    }
};

TEST_F(JoystickWeb_Test, Press_StringListAndArrayHoldButtons)
{
    JoystickWeb::Reply reply = Post("press", R"({"buttons":"up+fire"})");
    EXPECT_EQ(reply.status, 200);
    EXPECT_TRUE(reply.body["success"].asBool());
    EXPECT_EQ(reply.body["message"].asString(), "Joystick pressed: up,fire");
    EXPECT_EQ(reply.body["buttons"].asString(), "up+fire");
    EXPECT_EQ(reply.body["state"]["state"].asInt(), 0x18);
    EXPECT_EQ(reply.body["state"]["port_value"].asInt(), 0x18);
    EXPECT_TRUE(reply.body["state"]["buttons"]["up"].asBool());
    EXPECT_FALSE(reply.body["state"]["buttons"]["left"].asBool());
    EXPECT_FALSE(reply.body.isMember("warning"));
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kUp | Joystick::kFire);

    Post("press", R"({"buttons":["left","down"]})");
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kUp | Joystick::kFire | Joystick::kLeft | Joystick::kDown);
    Post("release", R"({"button":"up"})");
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kFire | Joystick::kLeft | Joystick::kDown) << "'button' alias";
}

TEST_F(JoystickWeb_Test, Release_DropsOnlyTheNamedButtons)
{
    Post("press", R"({"buttons":"up,fire"})");
    JoystickWeb::Reply reply = Post("release", R"({"buttons":"up"})");
    EXPECT_EQ(reply.status, 200);
    EXPECT_EQ(reply.body["message"].asString(), "Joystick released: up");
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kFire);
}

TEST_F(JoystickWeb_Test, Set_TakesAStateOrAListAndReplacesEverything)
{
    JoystickWeb::Reply reply = Post("set", R"({"state":229})");
    EXPECT_EQ(reply.status, 200);
    EXPECT_EQ(reply.body["requested_state"].asInt(), 229);
    EXPECT_EQ(reply.body["state"]["state"].asInt(), 229);
    EXPECT_EQ(_context->pJoystick->State(), 0xE5) << "D5..D7 included";

    Post("set", R"({"buttons":["up","fire"]})");
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kUp | Joystick::kFire);
    Post("set", R"({"buttons":[]})");
    EXPECT_EQ(_context->pJoystick->State(), 0x00) << "an empty list releases everything";
}

TEST_F(JoystickWeb_Test, Tap_ReleasesAfterTheFrames)
{
    JoystickWeb::Reply reply = Post("tap", R"({"buttons":"fire","frames":3})");
    EXPECT_EQ(reply.status, 200);
    EXPECT_EQ(reply.body["message"].asString(), "Joystick tap: fire for 3 frames");
    EXPECT_EQ(reply.body["frames"].asInt(), 3);
    EXPECT_EQ(reply.body["state"]["pending_tap"]["frames_left"].asInt(), 3);
    EXPECT_EQ(reply.body["state"]["pending_tap"]["mask"].asInt(), Joystick::kFire);
    for (int i = 0; i < 3; i++)
        _manager->OnFrame();
    EXPECT_EQ(_context->pJoystick->State(), 0x00);

    Post("tap", R"({"buttons":"up"})");
    EXPECT_EQ(_manager->GetState().pendingTapFramesLeft, DebugJoystickManager::DEFAULT_TAP_FRAMES);
}

/// JOY-13: the messages of the CLI and the manager, as 400 / 409 / 500 here
TEST_F(JoystickWeb_Test, Errors_UseTheManagerMessagesAndTheMouseStatusCodes)
{
    auto expectBad = [&](const std::string& verb, const std::string& body, const std::string& message) {
        JoystickWeb::Reply reply = Post(verb, body);
        EXPECT_EQ(reply.status, 400) << body;
        EXPECT_EQ(reply.body["error"].asString(), "Bad Request");
        EXPECT_EQ(reply.body["message"].asString(), message) << body;
    };
    const std::string unknown = "unknown joystick button 'jump' (up, down, left, right, fire, b5, b6, b7)";
    expectBad("press", R"({"buttons":"jump"})", unknown);
    expectBad("set", R"({"buttons":["up","jump"]})",
              "unknown joystick button 'up,jump' (up, down, left, right, fire, b5, b6, b7)");
    expectBad("set", R"({"state":300})", "state=300 out of range 0..255");
    expectBad("set", R"({"state":-1})", "state=-1 out of range 0..255");
    expectBad("tap", R"({"buttons":"fire","frames":0})", "frames=0 out of range 1..65535");
    expectBad("tap", R"({"buttons":"fire","frames":-4})", "frames=-4 out of range 1..65535");
    expectBad("tap", R"({"buttons":"fire","frames":70000})", "frames=70000 out of range 1..65535");
    expectBad("tap", R"({"buttons":"fire","frames":18446744073709551615})", "frames=9223372036854775807 out of range 1..65535");
    expectBad("press", R"({})", "Missing 'buttons' field in request body");
    expectBad("press", R"({"buttons":5})", "'buttons' must be a string or an array of button names");
    expectBad("press", R"({"buttons":["up",3]})", "'buttons' must be a string or an array of button names");
    expectBad("set", R"({})", "Missing 'state' or 'buttons' field in request body");
    expectBad("set", R"({"state":"5"})", "'state' must be an integer");
    expectBad("set", R"({"state":1.5})", "'state' must be an integer");
    expectBad("tap", R"({"buttons":"fire","frames":"2"})", "'frames' must be an integer");
    EXPECT_EQ(_context->pJoystick->State(), 0x00) << "rejected requests change nothing";

    JoystickWeb::Reply noBody = JoystickWeb::Handle(*_manager, "press", nullptr);
    EXPECT_EQ(noBody.status, 400);
    EXPECT_EQ(noBody.body["message"].asString(), "Missing 'buttons' field in request body");

    EXPECT_EQ(Post("dance", "{}").status, 404);
}

TEST_F(JoystickWeb_Test, ReplayRefusesWith409)
{
    Post("press", R"({"buttons":"left"})");
    _context->ttdReplayActive = true;
    JoystickWeb::Reply reply = Post("press", R"({"buttons":"up"})");
    _context->ttdReplayActive = false;
    EXPECT_EQ(reply.status, 409);
    EXPECT_EQ(reply.body["error"].asString(), "Conflict");
    EXPECT_EQ(reply.body["message"].asString().rfind("TTD replay in progress", 0), 0u);
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kLeft);
}

TEST_F(JoystickWeb_Test, NotFittedAcceptsWithAWarning)
{
    _emulator->GetFeatureManager()->setFeature(Features::kKempstonJoystick, false);
    JoystickWeb::Reply reply = Post("press", R"({"buttons":"up"})");
    EXPECT_EQ(reply.status, 200);
    EXPECT_EQ(reply.body["warning"].asString(), "joystick not present: the guest reads 0x00 on the joystick port");
    EXPECT_FALSE(reply.body["state"]["present"].asBool());
    EXPECT_EQ(reply.body["state"]["port_value"].asInt(), 0);

    JoystickWeb::Reply status = JoystickWeb::StatusReply(*_manager, "emu-1");
    EXPECT_EQ(status.body["warning"].asString(), reply.body["warning"].asString());
}

TEST_F(JoystickWeb_Test, NoDeviceIs500WithTheManagerMessage)
{
    Joystick* saved = _context->pJoystick;
    _context->pJoystick = nullptr;
    JoystickWeb::Reply reply = Post("press", R"({"buttons":"up"})");
    EXPECT_EQ(reply.status, 500);
    EXPECT_EQ(reply.body["message"].asString(), "Joystick device not available");
    JoystickWeb::Reply status = JoystickWeb::StatusReply(*_manager, "emu-1");
    EXPECT_FALSE(status.body["available"].asBool());
    EXPECT_EQ(status.body["warning"].asString(), "Joystick device not available");
    _context->pJoystick = saved;
}

TEST_F(JoystickWeb_Test, Status_ReportsStateRoutingKeysAndTap)
{
    Post("press", R"({"buttons":"up"})");
    JoystickWeb::Reply reply = JoystickWeb::StatusReply(*_manager, "emu-1");
    EXPECT_EQ(reply.status, 200);
    const Json::Value& body = reply.body;
    EXPECT_EQ(body["emulator_id"].asString(), "emu-1");
    EXPECT_TRUE(body["available"].asBool());
    EXPECT_TRUE(body["present"].asBool());
    EXPECT_TRUE(body["wired"].asBool());
    EXPECT_EQ(body["state"].asInt(), 0x08);
    EXPECT_EQ(body["port_value"].asInt(), 0x08);
    ASSERT_EQ(body["pressed"].size(), 1u);
    EXPECT_EQ(body["pressed"][0].asString(), "up");
    EXPECT_EQ(body["button_names"].size(), 8u);
    EXPECT_EQ(body["buttons"].size(), 8u);
    EXPECT_NE(body["keys"].asString().find("up:kp_8"), std::string::npos);
    EXPECT_TRUE(body["pending_tap"].isNull());
    EXPECT_FALSE(body.isMember("warning"));
}

/// The OpenAPI document of the routes: every route is documented, every $ref resolves, and what the
/// schema promises is what the handler accepts
namespace
{
void BuildJoystickSpec(Json::Value& paths, Json::Value& schemas)
{
#include "../../../../automation/webapi/src/openapi/openapi_joystick.inc"
#include "../../../../automation/webapi/src/openapi/openapi_joystick_schemas.inc"
}
}  // namespace

TEST(JoystickOpenApi_Test, DocumentsEveryRouteAndEveryRefResolves)
{
    Json::Value paths(Json::objectValue);
    Json::Value schemas(Json::objectValue);
    BuildJoystickSpec(paths, schemas);

    const std::set<std::string> expected = {"/api/v1/emulator/{id}/joystick/press",
                                            "/api/v1/emulator/{id}/joystick/release",
                                            "/api/v1/emulator/{id}/joystick/set",
                                            "/api/v1/emulator/{id}/joystick/tap", "/api/v1/emulator/{id}/joystick"};
    std::set<std::string> actual;
    for (const std::string& name : paths.getMemberNames())
        actual.insert(name);
    EXPECT_EQ(actual, expected);

    for (const std::string& path : expected)
    {
        const char* method = (path == "/api/v1/emulator/{id}/joystick") ? "get" : "post";
        ASSERT_TRUE(paths[path].isMember(method)) << path;
        const Json::Value& op = paths[path][method];
        EXPECT_FALSE(op["summary"].asString().empty()) << path;
        EXPECT_EQ(op["tags"][0].asString(), "Joystick Injection");
        EXPECT_EQ(op["parameters"][0]["name"].asString(), "id");
        EXPECT_TRUE(op["responses"].isMember("200") && op["responses"].isMember("404") && op["responses"].isMember("500"));
        const std::string ref = op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"].asString();
        ASSERT_EQ(ref.rfind("#/components/schemas/", 0), 0u) << path;
        EXPECT_TRUE(schemas.isMember(ref.substr(21))) << ref;
        if (std::string(method) == "post")
        {
            EXPECT_TRUE(op["responses"].isMember("400") && op["responses"].isMember("409")) << path;
            EXPECT_TRUE(op.isMember("requestBody")) << path;
        }
    }
    const Json::Value& state = schemas["JoystickState"]["properties"];
    for (const char* field : {"available", "present", "wired", "state", "port_value", "buttons", "pressed",
                              "button_names", "keys", "pending_tap"})
        EXPECT_TRUE(state.isMember(field)) << field;
    EXPECT_EQ(schemas["JoystickStatus"]["allOf"][0]["$ref"].asString(), "#/components/schemas/JoystickState");
}

/// Every field the state schema lists is in the real StateToJson, and the request schemas' limits are the manager's
TEST_F(JoystickWeb_Test, OpenApiSchemaMatchesTheHandler)
{
    Json::Value paths(Json::objectValue);
    Json::Value schemas(Json::objectValue);
    BuildJoystickSpec(paths, schemas);

    const Json::Value actual = JoystickWeb::StateToJson(_manager->GetState());
    for (const std::string& field : schemas["JoystickState"]["properties"].getMemberNames())
        EXPECT_TRUE(actual.isMember(field)) << "documented but not returned: " << field;
    for (const std::string& field : actual.getMemberNames())
        EXPECT_TRUE(schemas["JoystickState"]["properties"].isMember(field)) << "returned but not documented: " << field;
    for (const std::string& name : schemas["JoystickState"]["properties"]["buttons"]["properties"].getMemberNames())
        EXPECT_TRUE(actual["buttons"].isMember(name)) << name;

    const Json::Value& tap = paths["/api/v1/emulator/{id}/joystick/tap"]["post"]["requestBody"]["content"]
                                 ["application/json"]["schema"]["properties"]["frames"];
    EXPECT_EQ(tap["minimum"].asInt(), 1);
    EXPECT_EQ(tap["maximum"].asUInt(), DebugJoystickManager::MAX_TAP_FRAMES);
    EXPECT_EQ(tap["default"].asInt(), DebugJoystickManager::DEFAULT_TAP_FRAMES);
    // The documented tap limits are accepted, the next value out is rejected
    EXPECT_EQ(Post("tap", R"({"buttons":"fire","frames":65535})").status, 200);
    EXPECT_EQ(Post("tap", R"({"buttons":"fire","frames":65536})").status, 400);
    const Json::Value& set = paths["/api/v1/emulator/{id}/joystick/set"]["post"]["requestBody"]["content"]
                                 ["application/json"]["schema"]["properties"]["state"];
    EXPECT_EQ(Post("set", R"({"state":255})").status, set["maximum"].asInt() == 255 ? 200 : 400);
    EXPECT_EQ(Post("set", R"({"state":256})").status, 400);
}
