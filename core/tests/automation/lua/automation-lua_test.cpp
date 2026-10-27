// AutomationLua::executeCode: an error the script raises (a nil index, error(), a syntax error) answers an error
// with Lua's message and leaves the interpreter running; an unprotected call used to terminate the process

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "automation-lua.h"  // its LuaEmulator declares the Automation singleton

// The Lua bindings call the Automation singleton only for the videowall sync mode; automation.cpp brings every
// automation module (WebAPI, Drogon) along, so core-tests defines the two members here and no test calls them
Automation& Automation::GetInstance()
{
    std::abort();
}

bool Automation::SetVideowallSingleSyncMode(bool, const std::string&)
{
    return false;
}

namespace
{
class AutomationLua_Test : public ::testing::Test
{
protected:
    AutomationLua _lua;

    void SetUp() override { _lua.start(); }
    void TearDown() override { _lua.stop(); }

    const void* PrintFunction()
    {
        return _lua.dispatchSync([](sol::state& lua) { return lua["print"].get<sol::function>().pointer(); });
    }
};
} // namespace

TEST_F(AutomationLua_Test, AScriptErrorAnswersAnErrorAndTheInterpreterKeepsRunning)
{
    const struct
    {
        const char* code;
        const char* message;
    } errors[] = {
        {"local t = nil; return t.x", "attempt to index"},
        {"error('boom')", "boom"},
        {"print(", "unexpected symbol"},
    };
    const void* print = PrintFunction();
    for (const auto& error : errors)
    {
        std::string message;
        std::string output;
        EXPECT_FALSE(_lua.executeCode(error.code, message, output)) << error.code;
        EXPECT_NE(message.find(error.message), std::string::npos) << error.code << ": " << message;
        EXPECT_EQ(PrintFunction(), print) << error.code << ": print still captures";
    }

    std::string message;
    std::string output;
    ASSERT_TRUE(_lua.executeCode("print('alive', 1 + 1)", message, output)) << message;
    EXPECT_EQ(output, "alive\t2\n");
}
