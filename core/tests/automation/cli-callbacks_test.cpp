/// @file cli-callbacks_test.cpp
/// @brief CLI `callback list | resume | remove | policy` (cli-callbacks.h) over the script callbacks of an emulator: the
/// list's lines, each verb's answer and its refusals, and the automation gate's view of the command.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../automation/cli/src/commands/cli-callbacks.h"
#include "_helpers/emulatortesthelper.h"
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"

class CliCallbacks_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    ScriptCallbackManager* _callbacks = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _callbacks = _emulator->GetContext()->pDebugManager->GetScriptCallbacks();
        ASSERT_NE(_callbacks, nullptr);
    }

    void TearDown() override
    {
        if (_callbacks)
            _callbacks->RemoveAll();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint32_t PortTap()
    {
        CallbackSpec spec;
        spec.trigger = CallbackTrigger::Tap;
        spec.tap.type = BRK_IO;
        spec.tap.access = BRK_IO_OUT;
        spec.tap.address = 0xFE;
        spec.tap.portMask = 0x00FF;
        spec.owner = "python";
        spec.label = "border";
        std::string error;
        const uint32_t id = _callbacks->Register(spec, error);
        EXPECT_NE(id, 0u) << error;
        return id;
    }

    std::string Run(const std::vector<std::string>& args, bool* changed = nullptr)
    {
        return CliCallbacks::Run(_callbacks, args, changed);
    }
};

TEST_F(CliCallbacks_Test, ListShowsEveryCallbackOnALine)
{
    EXPECT_EQ(Run({}), "Script callbacks: 0");
    const uint32_t id = PortTap();
    const std::string text = Run({"list"});
    EXPECT_EQ(text, Run({})) << "list is the default";
    EXPECT_EQ(text.rfind("Script callbacks: 1\n#" + std::to_string(id) + " tap out #00FE (python \"border\") active, "
                         "policy suspend, hits 0",
                         0),
              0u)
        << text;
    EXPECT_NE(text.find("budget 1000 us, ring 256"), std::string::npos) << text;
}

TEST_F(CliCallbacks_Test, ResumeRemoveAndPolicyAnswerOrRefuse)
{
    const uint32_t id = PortTap();
    const std::string cb = std::to_string(id);
    for (int call = 0; call < 3; call++)
        _callbacks->Report(id, 1500, false);
    EXPECT_NE(Run({"list"}).find(" suspended, "), std::string::npos);
    EXPECT_NE(Run({"list"}).find("; reason: 3 calls over 1000 us within 50 frames"), std::string::npos);

    bool changed = false;
    std::string text = Run({"resume", cb}, &changed);
    EXPECT_TRUE(changed);
    EXPECT_EQ(text.rfind("Callback " + cb + " resumed: #" + cb + " tap out #00FE", 0), 0u) << text;
    EXPECT_NE(text.find(" active, "), std::string::npos) << text;

    text = Run({"policy", cb, "disable"}, &changed);
    EXPECT_TRUE(changed);
    EXPECT_NE(text.find("policy disable"), std::string::npos) << text;

    text = Run({"policy", cb, "maybe"}, &changed);
    EXPECT_FALSE(changed);
    EXPECT_EQ(text, "Error: The policy is suspend, disable, ignore or unpaced, not 'maybe'");
    EXPECT_EQ(Run({"resume", "77"}), "Error: No callback 77");
    EXPECT_EQ(Run({"resume", "x"}).rfind("Error: The callback id is a number", 0), 0u);
    EXPECT_EQ(Run({"resume"}), CliCallbacks::kUsage);
    EXPECT_EQ(Run({"policy", cb}), CliCallbacks::kUsage);
    EXPECT_EQ(Run({"frobnicate"}), CliCallbacks::kUsage);

    EXPECT_EQ(Run({"remove", cb}, &changed), "Callback " + cb + " removed");
    EXPECT_TRUE(changed);
    EXPECT_EQ(Run({"list"}), "Script callbacks: 0");
    EXPECT_EQ(CliCallbacks::Run(nullptr, {"list"}), "Script callbacks are not available on this emulator");
}

/// Like the breakpoint commands, the changing verbs need run control; the list only reads
TEST(CliCallbacksGate_Test, OnlyTheChangingVerbsPassTheGate)
{
    for (const char* command : {"callback", "callbacks"})
    {
        EXPECT_FALSE(automation_gate::CliCommandChangesMachine(command, {}));
        EXPECT_FALSE(automation_gate::CliCommandChangesMachine(command, {"list"}));
        EXPECT_TRUE(automation_gate::CliCommandChangesMachine(command, {"resume", "1"}));
        EXPECT_TRUE(automation_gate::CliCommandChangesMachine(command, {"remove", "1"}));
        EXPECT_TRUE(automation_gate::CliCommandChangesMachine(command, {"policy", "1", "ignore"}));
    }
}
