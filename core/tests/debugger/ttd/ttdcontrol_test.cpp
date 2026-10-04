/// @file ttdcontrol_test.cpp
/// @brief Phase 5, Step 1 (layer 3): TTDControl is the one implementation of the
/// time-travel verbs behind WebAPI, CLI, Lua and Python. These tests pin what
/// every surface now gets from it: verb and option checks, value parsing, the
/// refusals and their texts, the reply envelope and the HTTP status mapping.

#include <gtest/gtest.h>

#include <string>

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
