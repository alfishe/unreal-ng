// AutomationLua::executeCode: an error the script raises (a nil index, error(), a syntax error) answers an error
// with Lua's message and leaves the interpreter running; an unprotected call used to terminate the process

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "automation-lua.h"  // its LuaEmulator declares the Automation singleton
#include "_helpers/testpathhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

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

// snapshot_load(path, {switch_model}) as every surface: a 128K .z80 on a 48K is refused with the machine named unless
// switch_model = true asks for the switch (a new instance: its id comes back)
TEST_F(AutomationLua_Test, SnapshotLoadSwitchesTheModelOnlyWhenAsked)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto spectrum48 = manager->CreateEmulatorWithModel("lua-switch", "48K", LoggerLevel::LogError);
    ASSERT_TRUE(spectrum48);
    manager->SetSelectedEmulatorId(spectrum48->GetId());
    const std::string path = TestPathHelper::GetTestDataPath("loaders/z80/libspectrum/synth-128.z80");

    std::string message;
    std::string output;
    ASSERT_TRUE(_lua.executeCode("local ok, reason = snapshot_load('" + path + "', {switch_model = false}); print(ok, reason)",
                                 message, output))
        << message;
    EXPECT_EQ(output.rfind("false\t", 0), 0u) << output;
    EXPECT_NE(output.find("128k"), std::string::npos) << "the machine it needs: " << output;

    output.clear();
    ASSERT_TRUE(_lua.executeCode("local ok, reason, id = snapshot_load('" + path + "', {switch_model = true}); print(ok, id)",
                                 message, output))
        << message;
    EXPECT_EQ(output.rfind("true\t", 0), 0u) << output;
    const std::string id = output.substr(5, output.size() - 6);
    ASSERT_NE(id, spectrum48->GetId()) << "a new instance";
    auto switched = manager->GetEmulator(id);
    ASSERT_TRUE(switched) << output;
    EXPECT_EQ(switched->GetContext()->config.mem_model, MM_SPECTRUM128);

    output.clear();
    ASSERT_TRUE(_lua.executeCode("print((snapshot_load('" + path + "', 'legacy')))", message, output)) << message;
    EXPECT_EQ(output, "true\n") << "a string is still the commit (on the 128K now selected)";

    for (const auto& emulatorId : manager->GetEmulatorIds())
        manager->RemoveEmulator(emulatorId);
}
