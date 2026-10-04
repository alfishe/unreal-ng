/// @file ttdcontrol_test.cpp
/// @brief Phase 5, Step 1 (layer 3): TTDControl is the one implementation of the
/// time-travel verbs behind WebAPI, CLI, Lua and Python. These tests pin what
/// every surface now gets from it: verb and option checks, value parsing, the
/// refusals and their texts, the reply envelope and the HTTP status mapping.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

using ttd::TTDControl;
using ttd::TTDControlError;
using ttd::TTDReply;

class TTDControl_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    TTDReply Run(const std::string& verb, std::map<std::string, std::string> options = {})
    {
        return TTDControl(_context).Execute({verb, std::move(options)});
    }

    void Record(int frames)
    {
        ASSERT_TRUE(Run("start").Ok());
        _emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
    }

    static std::string Str(const TTDReply& r, const char* key)
    {
        const StateNode* n = r.body.find(key);
        return n ? n->s : std::string("<missing>");
    }
    static bool Bool(const TTDReply& r, const char* key)
    {
        const StateNode* n = r.body.find(key);
        return n && n->b;
    }
    static int64_t Int(const TTDReply& r, const char* key)
    {
        const StateNode* n = r.body.find(key);
        return n ? n->i : -1;
    }
};

TEST_F(TTDControl_Test, UnknownVerbsAndOptionsFailTheSameWayEverywhere)
{
    TTDReply r = Run("rewind");
    EXPECT_EQ(r.error, TTDControlError::BadRequest);
    EXPECT_NE(r.message.find("verbs: status"), std::string::npos) << r.message;

    r = Run("history-limit", {{"framez", "10"}});
    EXPECT_EQ(r.error, TTDControlError::BadRequest);
    EXPECT_NE(r.message.find("options: frames, bytes"), std::string::npos) << r.message;

    r = Run("stop", {{"now", ""}});
    EXPECT_EQ(r.error, TTDControlError::BadRequest);
    EXPECT_NE(r.message.find("takes none"), std::string::npos) << r.message;

    // Verbs are not case sensitive, as media verbs
    EXPECT_TRUE(Run("STATUS").Ok());
}

TEST_F(TTDControl_Test, WithoutTimeTravelStatusAnswersIdleAndEveryOtherVerbIsNotAvailable)
{
    TTDControl control(nullptr);
    TTDReply status = control.Execute({"status", {}});
    ASSERT_TRUE(status.Ok());
    EXPECT_EQ(Str(status, "state"), "idle");
    EXPECT_FALSE(Bool(status, "ttd_available"));

    TTDReply start = control.Execute({"start", {}});
    EXPECT_EQ(start.error, TTDControlError::NotAvailable);
    EXPECT_EQ(start.HttpStatus(), 501);
    EXPECT_EQ(start.message, "TTD engine not available in this build");
}

TEST_F(TTDControl_Test, StartStopAndStatusReportTheSession)
{
    TTDReply r = Run("start", {{"journal", "true"}, {"history_limit_frames", "500"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "started"));
    EXPECT_FALSE(Bool(r, "already_active"));
    EXPECT_EQ(Str(r, "state"), "recording");
    EXPECT_TRUE(Bool(r, "write_journal_enabled"));
    EXPECT_EQ(Int(r, "history_limit_frames"), 500);

    r = Run("start");
    ASSERT_TRUE(r.Ok());
    EXPECT_FALSE(Bool(r, "started"));
    EXPECT_TRUE(Bool(r, "already_active"));

    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    r = Run("status");
    ASSERT_TRUE(r.Ok());
    EXPECT_TRUE(Bool(r, "ttd_available"));
    EXPECT_GT(Int(r, "checkpoint_count"), 0);
    ASSERT_NE(r.body.find("machine"), nullptr);
    EXPECT_TRUE(r.body.find("machine")->isObject());
    ASSERT_NE(r.body.find("write_journal_build"), nullptr);

    r = Run("stop");
    ASSERT_TRUE(r.Ok());
    EXPECT_TRUE(Bool(r, "stopped"));
    EXPECT_EQ(Str(r, "state"), "idle");
    EXPECT_FALSE(Bool(Run("stop"), "stopped"));
}

TEST_F(TTDControl_Test, StartWithoutTheJournalOptionKeepsTheChoice)
{
    ASSERT_TRUE(Run("journal", {{"enabled", "true"}}).Ok());
    TTDReply r = Run("start");
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "write_journal_enabled"));
    ASSERT_TRUE(Run("stop").Ok());
    r = Run("start", {{"journal", "false"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_FALSE(Bool(r, "write_journal_enabled"));
}

TEST_F(TTDControl_Test, ValuesThatDoNotParseAreBadRequests)
{
    EXPECT_EQ(Run("start", {{"journal", "maybe"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("start", {{"history_limit_bytes", "-1"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("history-limit", {{"frames", "1.5"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("history-limit", {{"bytes", "99999999999999999999999"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("journal", {{"enabled", "2"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("journal-build", {{"from_frame", "x"}}).error, TTDControlError::BadRequest);
    // A refused request changed nothing
    EXPECT_FALSE(_ttd->IsRecording());

    // Accepted forms: decimal, 0x hex, a flag without a value
    TTDReply r = Run("history-limit", {{"frames", "0x100"}, {"bytes", "4096"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(Int(r, "history_limit_frames"), 256);
    EXPECT_EQ(Int(r, "history_limit_bytes"), 4096);
    r = Run("journal", {{"enabled", ""}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "write_journal_setting"));
    EXPECT_FALSE(Bool(r, "write_journal_enabled"));  // nothing records it without a session
}

TEST_F(TTDControl_Test, InvalidateIsRefusedWhileRecordingAndKeepsItsReasonOtherwise)
{
    Record(2);
    TTDReply r = Run("invalidate", {{"reason", "test"}});
    EXPECT_EQ(r.error, TTDControlError::Conflict);
    EXPECT_EQ(r.HttpStatus(), 409);
    EXPECT_EQ(r.message, _ttd->RecordingGuard(ttd::TTDGuardedAction::Invalidate));
    EXPECT_TRUE(_ttd->IsRecording());

    ASSERT_TRUE(Run("stop").Ok());
    r = Run("invalidate", {{"reason", "test"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(Str(r, "reason"), "test");
    EXPECT_EQ(_ttd->GetSessionInfo().lastDropReason, "test");
}

TEST_F(TTDControl_Test, JournalBuildIsRefusedWhileRecordingWithTheStateInTheEnvelope)
{
    Record(2);
    const TTDReply r = Run("journal-build");
    ASSERT_EQ(r.error, TTDControlError::Conflict);
    const StateNode value = r.ToValue();
    ASSERT_NE(value.find("error"), nullptr);
    EXPECT_EQ(value.find("error")->s, "Conflict");
    ASSERT_NE(value.find("message"), nullptr);
    EXPECT_EQ(value.find("message")->s, r.message);
    ASSERT_NE(value.find("state"), nullptr);
    EXPECT_EQ(value.find("state")->s, "recording");
}

TEST_F(TTDControl_Test, ABodyThatNamesItsOwnErrorKeepsIt)
{
    TTDReply r;
    r.error = TTDControlError::Conflict;
    r.message = "the build failed";
    r.body["ok"] = false;
    r.body["error"] = "frame 12 is not replayable";
    const StateNode value = r.ToValue();
    EXPECT_EQ(value.find("error")->s, "frame 12 is not replayable");
    EXPECT_EQ(value.find("message")->s, "the build failed");
    EXPECT_FALSE(value.find("ok")->b);
}

// ---------------------------------------------------------------------------
// Group 2: positions, seeks, steps, resume
// ---------------------------------------------------------------------------

TEST_F(TTDControl_Test, MovingInTheTimelineIsRefusedWhileRecording)
{
    Record(4);
    for (const auto& [verb, options] : std::vector<std::pair<std::string, std::map<std::string, std::string>>>{
             {"seek", {{"frame", "1"}}},
             {"step-back", {}},
             {"step-forward", {}},
             {"step-instruction", {}},
             {"reverse-step", {{"count", "1"}}}})
    {
        const TTDReply r = Run(verb, options);
        EXPECT_EQ(r.error, TTDControlError::Conflict) << verb;
        EXPECT_EQ(Str(r, "state"), "recording") << verb;
    }
    EXPECT_TRUE(_ttd->IsRecording());
}

TEST_F(TTDControl_Test, SeekTargetsAreCheckedBeforeAnythingMoves)
{
    Record(6);
    ASSERT_TRUE(Run("stop").Ok());
    EXPECT_EQ(Run("seek").error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("seek", {{"frame", "-3"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("seek", {{"frame", "2"}, {"tinframe", "4294967296"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("seek", {{"bookmark", ""}}).error, TTDControlError::BadRequest);
    const TTDReply unknown = Run("seek", {{"bookmark", "nowhere"}});
    EXPECT_EQ(unknown.error, TTDControlError::NotFound);
    EXPECT_EQ(unknown.HttpStatus(), 404);
    EXPECT_EQ(unknown.message, "Unknown bookmark label: nowhere");
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);  // nothing moved
}

TEST_F(TTDControl_Test, SeekStepAndResumeReportWhereTheMachineIs)
{
    Record(6);
    ASSERT_TRUE(Run("stop").Ok());
    const TTDReply end = Run("position");
    ASSERT_TRUE(end.Ok());
    const int64_t last = end.body.find("session_end")->find("frame")->i;
    const int64_t first = static_cast<int64_t>(_ttd->GetSessionInfo().sessionStartFrame);
    ASSERT_GT(last, first + 2);

    TTDReply r = Run("seek", {{"frame", std::to_string(first + 2)}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "reached"));
    EXPECT_EQ(Str(r, "halt_reason"), "target");
    EXPECT_EQ(r.body.find("arrived_at")->find("frame")->i, first + 2);
    EXPECT_EQ(Str(r, "state"), "detached");

    r = Run("step-forward");
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "stepped"));
    EXPECT_EQ(Int(r, "frame"), first + 3);
    r = Run("step-back");
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(Int(r, "frame"), first + 2);
    EXPECT_EQ(Run("position").body.find("current")->find("frame")->i, first + 2);

    EXPECT_EQ(Run("resume", {{"tinframe", "5"}}).error, TTDControlError::BadRequest);
    r = Run("resume");
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "resumed"));
    EXPECT_EQ(Int(r, "frame"), first + 2);
    EXPECT_EQ(Str(r, "state"), "recording");
}

TEST_F(TTDControl_Test, InstructionStepsCheckTheirArguments)
{
    Record(4);
    ASSERT_TRUE(Run("stop").Ok());
    ASSERT_TRUE(Run("seek", {{"frame", std::to_string(_ttd->GetSessionInfo().sessionStartFrame + 2)}}).Ok());

    EXPECT_EQ(Run("step-instruction", {{"dir", "sideways"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("reverse-step").error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("reverse-step", {{"count", "1"}, {"tstates", "4"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("reverse-step", {{"count", "4294967296"}}).error, TTDControlError::BadRequest);

    TTDReply r = Run("step-instruction", {{"dir", "back"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "stepped"));
    EXPECT_EQ(Str(r, "dir"), "back");
    r = Run("reverse-step", {{"count", "2"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "reached"));
    EXPECT_EQ(Str(r, "mode"), "count");
}
