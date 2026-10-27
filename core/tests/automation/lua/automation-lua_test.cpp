// AutomationLua::executeCode: an error the script raises (a nil index, error(), a syntax error) answers an error
// with Lua's message and leaves the interpreter running; an unprotected call used to terminate the process.
// Script callbacks (docs/features/script-callbacks.md): on_tap's function runs on the Lua thread with the hit

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "automation-lua.h"  // its LuaEmulator declares the Automation singleton
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

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
    ASSERT_TRUE(_lua.executeCode("print('all', nil, 3, 4, 5, 6, 7)", message, output)) << message;
    EXPECT_EQ(output, "all\tnil\t3\t4\t5\t6\t7\n") << "every argument, nil included, as Lua's print";
}

namespace
{
/// The Lua thread's emulator binding, for a test emulator the manager does not know
class BoundLua : public AutomationLua
{
public:
    void Bind(Emulator* emulator)
    {
        dispatchSync([this, emulator](sol::state&) { _luaEmulator->setEmulator(emulator); });
    }
};

/// Each frame (the ROM's interrupt wakes the HALT): LD A,(#9000); DEC A; LD (#9000),A; INC A; LD (#9000),A
const uint8_t kWrites[] = {0xFB, 0x76, 0x3A, 0x00, 0x90, 0x3D, 0x32, 0x00, 0x90,
                           0x3C, 0x32, 0x00, 0x90, 0xC3, 0x00, 0x80};
}  // namespace

TEST(AutomationLuaCallbacks_Test, OnTapRunsTheFunctionOnTheLuaThreadAndGoesWithTheScript)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    for (size_t i = 0; i < sizeof(kWrites); i++)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), kWrites[i]);
    context->pMemory->DirectWriteToZ80Memory(0x9000, 5);
    context->pCore->GetZ80()->pc = 0x8000;
    context->pCore->GetZ80()->sp = 0x7F00;
    context->pCore->GetZ80()->iy = 0x5C3A;  // the ROM's frame interrupt keeps its system variables there
    ScriptCallbackManager* callbacks = context->pDebugManager->GetScriptCallbacks();

    {
        BoundLua lua;
        lua.start();
        lua.Bind(emulator);
        std::string message;
        std::string output;
        ASSERT_TRUE(lua.executeCode(R"(
            values = {}
            tap = on_tap({write = 0x9000, ["if"] = "MDT < 5"}, function(hit) values[#values + 1] = hit.value end,
                         {label = "lives"})
            bad = on_tap({write = 0x9000}, function(hit) error("boom") end)
            refused, why = on_tap({write = 0x9000, ["if"] = "RD == 1"}, function() end)
            print(tap, bad, refused, why ~= nil)
        )", message, output)) << message;
        EXPECT_EQ(output, "1\t2\t-1\ttrue\n");

        emulator->RunNFrames(3, false);
        EXPECT_TRUE(TestWait::For([&lua]() { return lua.dispatchSync([](sol::state& state) {
            return state["values"].get<sol::table>().size() >= 2;
        }); })) << "the wake runs the function without a task";
        ASSERT_TRUE(lua.executeCode("callbacks_run(); for _, v in ipairs(values) do assert(v == 4) end", message, output))
            << message;

        CallbackInfo info;
        ASSERT_TRUE(callbacks->Get(2, info));
        EXPECT_EQ(info.owner, "lua");
        EXPECT_GE(info.stats.errors, 2u);
        EXPECT_NE(info.stats.lastError.find("boom"), std::string::npos) << info.stats.lastError;
        ASSERT_TRUE(callbacks->Get(1, info));
        EXPECT_EQ(info.label, "lives");
        EXPECT_EQ(info.stats.errors, 0u);
        EXPECT_EQ(info.stats.calls, info.stats.hits);

        ASSERT_TRUE(lua.executeCode("local list = callbacks(); assert(#list == 2 and list[1].label == 'lives')"
                                    " assert(callback_remove(2)) assert(not callback_remove(2))",
                                    message, output))
            << message;
        lua.stop();
    }
    EXPECT_TRUE(callbacks->List().empty()) << "the script's callbacks go with its interpreter";
    EmulatorTestHelper::CleanupEmulator(emulator);
}
