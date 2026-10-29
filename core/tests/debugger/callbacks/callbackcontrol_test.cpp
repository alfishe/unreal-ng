/// @file callbackcontrol_test.cpp
/// @brief The remote planes' view of the script callbacks (callbackcontrol.h): the report has the Lua table's fields,
/// the text line says the state and the statistics, and resume / remove / policy answer the statuses REST passes on.

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/callbacks/callbackcontrol.h"
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

class CallbackControl_Test : public ::testing::Test
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

    uint32_t WriteTap(CallbackPolicy policy = CallbackPolicy::Suspend)
    {
        CallbackSpec spec;
        spec.trigger = CallbackTrigger::Tap;
        spec.tap.type = BRK_MEMORY;
        spec.tap.access = BRK_MEM_WRITE;
        spec.tap.address = 0x5C78;
        spec.tap.condition = "MDT == 0";
        spec.owner = "lua";
        spec.label = "frames-wrap";
        spec.policy = policy;
        std::string error;
        const uint32_t id = _callbacks->Register(spec, error);
        EXPECT_NE(id, 0u) << error;
        return id;
    }

    uint32_t GameEvent()
    {
        CallbackSpec spec;
        spec.trigger = CallbackTrigger::GameEvent;
        spec.event = "died";
        spec.owner = "python";
        std::string error;
        const uint32_t id = _callbacks->Register(spec, error);
        EXPECT_NE(id, 0u) << error;
        return id;
    }

    static std::string Text(const StateNode& node, const char* key)
    {
        const StateNode* value = node.find(key);
        return value ? value->s : std::string("<absent>");
    }
};

TEST_F(CallbackControl_Test, TheReportHasTheLuaTablesFields)
{
    const uint32_t tap = WriteTap();
    const uint32_t event = GameEvent();
    _callbacks->Report(tap, 2500, true, "attempt to index a nil value");

    const StateNode list = callback_control::ListNode(_callbacks->List());
    ASSERT_NE(list.find("count"), nullptr);
    EXPECT_EQ(list.find("count")->i, 2);
    const StateNode& items = *list.find("callbacks");
    ASSERT_EQ(items.size(), 2u);
    const StateNode& first = items.items[0];
    for (const char* key : {"id", "on", "owner", "label", "target", "breakpoint", "policy", "state", "budget_us", "ring",
                            "pending", "hits", "dropped", "calls", "errors", "overruns", "total_us", "max_us", "p99_us",
                            "last_overrun_frame", "last_overrun_us", "last_error"})
        EXPECT_NE(first.find(key), nullptr) << key;
    EXPECT_EQ(first.find("id")->i, tap);
    EXPECT_EQ(Text(first, "on"), "tap");
    EXPECT_EQ(Text(first, "target"), "tap write #5C78 if MDT == 0");
    EXPECT_EQ(Text(first, "last_error"), "attempt to index a nil value");
    EXPECT_EQ(first.find("errors")->i, 1);
    EXPECT_EQ(first.find("event"), nullptr) << "absent as in the Lua table";

    const StateNode& second = items.items[1];
    EXPECT_EQ(second.find("id")->i, event);
    EXPECT_EQ(Text(second, "on"), "game_event");
    EXPECT_EQ(Text(second, "event"), "died");
    EXPECT_EQ(second.find("last_error"), nullptr);
    EXPECT_EQ(second.find("last_overrun_frame"), nullptr) << "no overrun yet";

    const std::string text = callback_control::FormatList(list);
    EXPECT_EQ(text.rfind("Script callbacks: 2\n#", 0), 0u) << text;
    EXPECT_NE(text.find("tap write #5C78 if MDT == 0 (lua \"frames-wrap\") active, policy suspend"), std::string::npos)
        << text;
    EXPECT_NE(text.find("errors 1, overruns 1, max 2500 us"), std::string::npos) << text;
    EXPECT_NE(text.find("; last error: attempt to index a nil value"), std::string::npos) << text;
    EXPECT_NE(text.find("event died (python) active"), std::string::npos) << text;
}

TEST_F(CallbackControl_Test, ResumeRemoveAndPolicyAnswerTheStatusesRestPassesOn)
{
    const uint32_t tap = WriteTap();
    const std::string id = std::to_string(tap);
    for (int call = 0; call < 3; call++)
        _callbacks->Report(tap, 1500, false);
    CallbackInfo info;
    ASSERT_TRUE(_callbacks->Get(tap, info));
    ASSERT_EQ(info.state, CallbackState::Suspended);

    callback_control::Outcome outcome = callback_control::Resume(_callbacks, id);
    EXPECT_EQ(outcome.status, 200) << outcome.message;
    ASSERT_NE(outcome.body.find("callback"), nullptr);
    EXPECT_EQ(Text(*outcome.body.find("callback"), "state"), "active");
    EXPECT_EQ(outcome.body.find("callback")->find("overruns")->i, 0) << "fresh statistics";

    outcome = callback_control::SetPolicy(_callbacks, id, "ignore");
    EXPECT_EQ(outcome.status, 200);
    EXPECT_EQ(Text(*outcome.body.find("callback"), "policy"), "ignore");
    EXPECT_EQ(callback_control::SetPolicy(_callbacks, id, "sometimes").status, 400);
    EXPECT_EQ(callback_control::SetPolicy(_callbacks, "99", "ignore").status, 404);

    EXPECT_EQ(callback_control::Resume(_callbacks, "abc").status, 400);
    EXPECT_EQ(callback_control::Resume(_callbacks, "0").status, 400);
    EXPECT_EQ(callback_control::Resume(_callbacks, "99").status, 404);
    EXPECT_EQ(callback_control::Resume(nullptr, id).status, 503);

    // Disabled by the policy: in the list, cannot be resumed
    const uint32_t disabled = WriteTap(CallbackPolicy::Disable);
    _callbacks->Report(disabled, 20000, false);
    outcome = callback_control::Resume(_callbacks, std::to_string(disabled));
    EXPECT_EQ(outcome.status, 409);
    EXPECT_NE(outcome.message.find("is disabled"), std::string::npos) << outcome.message;
    EXPECT_EQ(Text(outcome.body, "error"), "Conflict");

    BreakpointManager* breakpoints = _emulator->GetContext()->pDebugManager->GetBreakpointsManager();
    ASSERT_TRUE(_callbacks->Get(tap, info));
    const uint16_t tapBreakpoint = info.breakpointId;
    ASSERT_NE(breakpoints->GetBreakpointById(tapBreakpoint), nullptr);
    outcome = callback_control::Remove(_callbacks, id);
    EXPECT_EQ(outcome.status, 200);
    EXPECT_TRUE(outcome.body.find("removed")->b);
    EXPECT_FALSE(_callbacks->Get(tap, info));
    EXPECT_EQ(breakpoints->GetBreakpointById(tapBreakpoint), nullptr) << "the hidden tap goes with it";
    EXPECT_EQ(callback_control::Remove(_callbacks, id).status, 404);
}
