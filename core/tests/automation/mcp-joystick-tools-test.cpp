/// @file mcp-joystick-tools-test.cpp
/// @brief MCP joystick_input (joystick TDD §5, JOY-13). The tool is driven through a caller that answers the
/// /joystick routes with the WebAPI's own logic (JoystickWeb::Handle) over a real ZX-Evo (ATM3) instance,
/// so the tool -> route -> body -> manager -> device chain is real and the errors are the shared ones.

#include <gtest/gtest.h>

#include <json/json.h>

#include <memory>
#include <string>
#include <vector>

#include "../../automation/webapi/src/common/joystickjson.h"
#include "_helpers/emulatortesthelper.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"
#include "mcp-tools.h"
#include "target-resolver.h"

namespace
{

class JoystickBridgeCaller : public mcp::IApiCaller
{
public:
    struct RecordedCall
    {
        std::string method;
        std::string path;
        Json::Value body;
    };

    DebugJoystickManager* manager = nullptr;
    std::vector<RecordedCall> calls;

    void Call(const std::string& method, const std::string& path, const Json::Value* body, ApiCallback callback) override
    {
        calls.push_back({method, path, body ? *body : Json::Value()});

        if (method == "GET" && path == "/api/v1/emulator")
        {
            Json::Value emulator;
            emulator["id"] = "emu-1";
            emulator["state"] = "paused";
            Json::Value list;
            list["emulators"].append(emulator);
            callback(200, list);
            return;
        }

        const std::string prefix = "/api/v1/emulator/emu-1/joystick";
        if (path.rfind(prefix, 0) != 0)
        {
            callback(404, Json::Value());
            return;
        }
        JoystickWeb::Reply reply;
        if (method == "GET" && path == prefix)
            reply = JoystickWeb::StatusReply(*manager, "emu-1");
        else if (method == "POST" && path.size() > prefix.size() + 1)
            reply = JoystickWeb::Handle(*manager, path.substr(prefix.size() + 1), body);
        else
            reply = JoystickWeb::Error(404, "Not Found", "no such route");
        callback(reply.status, reply.body);
    }

    void CallRaw(const std::string&, const std::string&, const std::vector<uint8_t>&,
                 const std::map<std::string, std::string>&, ApiCallback callback) override
    {
        callback(404, Json::Value());
    }

    const RecordedCall* LastJoystickCall() const
    {
        for (auto it = calls.rbegin(); it != calls.rend(); ++it)
            if (it->path.find("/joystick") != std::string::npos)
                return &*it;
        return nullptr;
    }
};

mcp::ToolResult RunJoystickTool(mcp::ToolRegistry& registry, Json::Value args, mcp::IApiCaller& caller)
{
    const mcp::ToolDefinition* tool = registry.Find("joystick_input");
    if (!tool)
        return mcp::ToolResult::Error("tool not registered: joystick_input");
    mcp::ToolResult result;
    tool->handler(args, caller, [&result](mcp::ToolResult value) { result = std::move(value); },
                  mcp::ProgressFn([](double, double, const std::string&) {}));
    return result;
}

}  // namespace

class McpJoystick_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _caller = std::make_shared<JoystickBridgeCaller>();
        _caller->manager = _context->pDebugManager->GetJoystickManager();
        ASSERT_NE(_caller->manager, nullptr);
        _registry = mcp::BuildFullRegistry(_caller);
    }

    void TearDown() override
    {
        _registry.reset();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    mcp::ToolResult Run(const std::string& action, const std::string& argsJson = "{}")
    {
        Json::Value args;
        Json::Reader reader;
        EXPECT_TRUE(reader.parse(argsJson, args));
        args["action"] = action;
        return RunJoystickTool(*_registry, args, *_caller);
    }

    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    std::shared_ptr<JoystickBridgeCaller> _caller;
    std::unique_ptr<mcp::ToolRegistry> _registry;
};

TEST_F(McpJoystick_Test, ToolIsRegisteredWithTheFiveActions)
{
    const mcp::ToolDefinition* tool = _registry->Find("joystick_input");
    ASSERT_NE(tool, nullptr);
    const Json::Value& actions = tool->inputSchema["properties"]["action"]["enum"];
    ASSERT_EQ(actions.size(), 5u);
    EXPECT_EQ(actions[0].asString(), "press");
    EXPECT_EQ(actions[4].asString(), "status");
}

TEST_F(McpJoystick_Test, Press_PostsButtonsAndHoldsThem)
{
    mcp::ToolResult result = Run("press", R"({"buttons":"up+fire"})");
    ASSERT_FALSE(result.isError) << result.text;
    const auto* call = _caller->LastJoystickCall();
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->method, "POST");
    EXPECT_EQ(call->path, "/api/v1/emulator/emu-1/joystick/press");
    EXPECT_EQ(call->body["buttons"].asString(), "up+fire");
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kUp | Joystick::kFire);
    EXPECT_EQ(result.structured["state"]["port_value"].asInt(), 0x18);
}

TEST_F(McpJoystick_Test, ReleaseSetTapAndStatus_MapToTheirRoutes)
{
    ASSERT_FALSE(Run("press", R"({"buttons":["up","fire","left"]})").isError);
    ASSERT_FALSE(Run("release", R"({"buttons":"up"})").isError);
    EXPECT_EQ(_caller->LastJoystickCall()->path, "/api/v1/emulator/emu-1/joystick/release");
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kFire | Joystick::kLeft);

    ASSERT_FALSE(Run("set", R"({"state":229})").isError);
    EXPECT_EQ(_caller->LastJoystickCall()->path, "/api/v1/emulator/emu-1/joystick/set");
    EXPECT_EQ(_context->pJoystick->State(), 0xE5);
    ASSERT_FALSE(Run("set", R"({"buttons":["down"]})").isError);
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kDown);

    ASSERT_FALSE(Run("tap", R"({"buttons":"fire","frames":4})").isError);
    const auto* tap = _caller->LastJoystickCall();
    EXPECT_EQ(tap->path, "/api/v1/emulator/emu-1/joystick/tap");
    EXPECT_EQ(tap->body["frames"].asInt(), 4);
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kDown | Joystick::kFire);
    EXPECT_EQ(_caller->manager->GetState().pendingTapFramesLeft, 4);

    mcp::ToolResult status = Run("status");
    ASSERT_FALSE(status.isError) << status.text;
    const auto* get = _caller->LastJoystickCall();
    EXPECT_EQ(get->method, "GET");
    EXPECT_EQ(get->path, "/api/v1/emulator/emu-1/joystick");
    EXPECT_TRUE(status.structured["wired"].asBool());
    EXPECT_EQ(status.structured["state"].asInt(), Joystick::kDown | Joystick::kFire);
}

/// JOY-13: the same errors as the CLI and the WebAPI
TEST_F(McpJoystick_Test, Errors_CarryTheSharedMessages)
{
    mcp::ToolResult unknown = Run("press", R"({"buttons":"jump"})");
    EXPECT_TRUE(unknown.isError);
    EXPECT_NE(unknown.text.find("unknown joystick button 'jump' (up, down, left, right, fire, b5, b6, b7)"),
              std::string::npos)
        << unknown.text;

    mcp::ToolResult range = Run("set", R"({"state":300})");
    EXPECT_TRUE(range.isError);
    EXPECT_NE(range.text.find("state=300 out of range 0..255"), std::string::npos) << range.text;

    mcp::ToolResult frames = Run("tap", R"({"buttons":"fire","frames":0})");
    EXPECT_TRUE(frames.isError);
    EXPECT_NE(frames.text.find("frames=0 out of range 1..65535"), std::string::npos) << frames.text;
    EXPECT_EQ(_context->pJoystick->State(), 0x00);

    _context->ttdReplayActive = true;
    mcp::ToolResult replay = Run("press", R"({"buttons":"up"})");
    _context->ttdReplayActive = false;
    EXPECT_TRUE(replay.isError);
    EXPECT_NE(replay.text.find("TTD replay in progress"), std::string::npos) << replay.text;
    EXPECT_EQ(_context->pJoystick->State(), 0x00);
}

TEST_F(McpJoystick_Test, MissingArguments_AreRejectedBeforeAnyCall)
{
    const size_t before = _caller->calls.size();
    mcp::ToolResult press = Run("press");
    EXPECT_TRUE(press.isError);
    EXPECT_EQ(press.text, "press requires 'buttons'");
    EXPECT_EQ(Run("release").text, "release requires 'buttons'");
    EXPECT_EQ(Run("tap").text, "tap requires 'buttons'");
    EXPECT_EQ(Run("set").text, "set requires 'state' or 'buttons'");
    EXPECT_EQ(Run("dance").text, "Unknown action 'dance'");
    EXPECT_EQ(_caller->calls.size(), before) << "nothing reached the WebAPI";
}
