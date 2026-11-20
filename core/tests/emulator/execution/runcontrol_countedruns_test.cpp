// Counted runs through RunControl (docs/inprogress/2026-11-12-re-api-efficiency, F1): run_frames, run_tstates and
// steps run every unit they are asked for or refuse before anything runs - never a silent cut - and the answer says
// what ran (count, frames_run, tstates_run, frame, t, stop, message). REST, WebSocket, MCP and the actions all take
// this reply.

#include "pch.h"

#include <cstdint>
#include <memory>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/runcontrol.h"

namespace
{

class RunControlCountedRuns_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _shared = EmulatorManager::GetInstance()->GetEmulator(_emulator->GetId());
        ASSERT_NE(_shared, nullptr);
        _control = std::make_unique<execution::RunControl>(_shared);
    }

    void TearDown() override
    {
        _control.reset();
        _shared.reset();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    int64_t Int(const execution::RunReply& reply, const char* key) const
    {
        const StateNode* node = reply.body.find(key);
        EXPECT_NE(node, nullptr) << key << " in " << reply.message;
        return node != nullptr ? node->i : -1;
    }

    std::string StopReason(const execution::RunReply& reply) const
    {
        const StateNode* stop = reply.body.find("stop");
        const StateNode* reason = stop != nullptr ? stop->find("reason") : nullptr;
        return reason != nullptr ? reason->s : std::string();
    }

    Emulator* _emulator = nullptr;
    std::shared_ptr<Emulator> _shared;
    std::unique_ptr<execution::RunControl> _control;
    const execution::Requester _requester{execution::Origin::WebApi, {}};
};

}  // namespace

/// Over the limit: 400 with count and max, the machine does not move (the WebAPI used to cut to 10000 silently)
TEST_F(RunControlCountedRuns_Test, OverTheLimitIsRefusedAndNothingRuns)
{
    EmulatorContext* context = _emulator->GetContext();
    const uint64_t frame = context->emulatorState.frame_counter;
    const uint32_t t = context->pCore->GetZ80()->t;

    const execution::RunReply frames = _control->RunFrames(_requester, execution::kMaxRunFrames + 1);
    EXPECT_EQ(frames.status, execution::RunReply::Status::BadRequest);
    EXPECT_EQ(frames.HttpStatus(), 400);
    EXPECT_EQ(Int(frames, "count"), static_cast<int64_t>(execution::kMaxRunFrames + 1));
    EXPECT_EQ(Int(frames, "max"), static_cast<int64_t>(execution::kMaxRunFrames));
    EXPECT_NE(frames.message.find("200000"), std::string::npos) << frames.message;

    EXPECT_EQ(_control->RunFrames(_requester, 0).status, execution::RunReply::Status::BadRequest);
    EXPECT_EQ(_control->RunTStates(_requester, execution::kMaxRunTStates + 1).status,
              execution::RunReply::Status::BadRequest);
    EXPECT_EQ(_control->Steps(_requester, execution::kMaxRunSteps + 1).status,
              execution::RunReply::Status::BadRequest);

    EXPECT_EQ(context->emulatorState.frame_counter, frame);
    EXPECT_EQ(context->pCore->GetZ80()->t, t);
}

/// A run that did it all: the answer and its one line name what ran and where it ended
TEST_F(RunControlCountedRuns_Test, TheAnswerSaysWhatRan)
{
    EmulatorContext* context = _emulator->GetContext();
    const uint64_t before = context->emulatorState.frame_counter;
    const execution::RunReply reply = _control->RunFrames(_requester, 3);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(Int(reply, "count"), 3);
    EXPECT_EQ(Int(reply, "frames_run"), 3);
    EXPECT_GE(Int(reply, "tstates_run"), 3ll * context->pCore->GetZ80()->_frameLimit);
    EXPECT_EQ(Int(reply, "frame"), static_cast<int64_t>(before + 3));
    EXPECT_EQ(Int(reply, "t"), static_cast<int64_t>(context->pCore->GetZ80()->t));
    EXPECT_EQ(StopReason(reply), "done");
    const std::string expected = "Ran 3 frames (frame " + std::to_string(before + 3) + ", t " +
                                 std::to_string(context->pCore->GetZ80()->t) + ")";
    EXPECT_EQ(reply.message, expected);
    const StateNode* message = reply.body.find("message");
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->s, expected);

    const execution::RunReply steps = _control->Steps(_requester, 5);
    ASSERT_TRUE(steps.Ok()) << steps.message;
    EXPECT_EQ(Int(steps, "executed"), 5);
    EXPECT_EQ(Int(steps, "instructions_run"), 5);
    EXPECT_EQ(StopReason(steps), "step");  // the debuggers read "step"
    EXPECT_EQ(steps.message.rfind("Ran 5 instructions", 0), 0u) << steps.message;

    const execution::RunReply tstates = _control->RunTStates(_requester, 700);
    ASSERT_TRUE(tstates.Ok()) << tstates.message;
    EXPECT_EQ(Int(tstates, "count"), 700);
    EXPECT_EQ(StopReason(tstates), "done");
    EXPECT_EQ(tstates.message.rfind("Ran 700 t-states", 0), 0u) << tstates.message;
}

/// breakpoints: true lets the first one end the run; the answer counts the frames that ran and names it
TEST_F(RunControlCountedRuns_Test, ABreakpointEndsTheRunAndTheAnswerNamesIt)
{
    EmulatorContext* context = _emulator->GetContext();
    _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    _emulator->GetFeatureManager()->setFeature(Features::kBreakpoints, true);
    BreakpointManager* breakpoints = context->pDebugManager->GetBreakpointsManager();
    ASSERT_NE(breakpoints, nullptr);
    // The frame interrupt's IM 1 entry: reached once a frame
    const uint16_t id = breakpoints->AddExecutionBreakpoint(0x0038);
    ASSERT_NE(id, BRK_INVALID);

    const execution::RunReply skipped = _control->RunFrames(_requester, 2);
    ASSERT_TRUE(skipped.Ok()) << skipped.message;
    EXPECT_EQ(Int(skipped, "frames_run"), 2) << "breakpoints are skipped by default";

    const execution::RunReply reply = _control->RunFrames(_requester, 100, /*breakpoints=*/true);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(StopReason(reply), "breakpoint");
    EXPECT_LT(Int(reply, "frames_run"), 100);
    const StateNode* stop = reply.body.find("stop");
    ASSERT_NE(stop, nullptr);
    ASSERT_NE(stop->find("breakpoint_id"), nullptr);
    EXPECT_EQ(stop->find("breakpoint_id")->i, id);
    EXPECT_NE(reply.message.find(" of 100 frames: stopped at breakpoint #" + std::to_string(id)), std::string::npos)
        << reply.message;
}
