/// @file ttdcontrol_test.cpp
/// @brief Phase 5, Step 1 (layer 3): TTDControl is the one implementation of the
/// time-travel verbs behind WebAPI, CLI, Lua and Python. These tests pin what
/// every surface now gets from it: verb and option checks, value parsing, the
/// refusals and their texts, the reply envelope and the HTTP status mapping.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdexternalevents.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"
#include "debugger/ttd/ttdcheckpoint.h"

using ttd::TTDControl;
using ttd::TTDControlError;
using ttd::TTDReply;

/// Every test runs on both backends (Phase 5, C5): v1's TimeTravelManager and the
/// engine's TimeTravelController, wired the way the switch wires it (hooks, write
/// sink, EmulatorContext::pTimeTravelController)
class TTDControl_Test : public ::testing::TestWithParam<bool>
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::ITimeTravelHooks* _ttd = nullptr;
    std::unique_ptr<ttd::TimeTravelController> _controller;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context->pTimeTravelManager, nullptr);
        if (GetParam())
        {
            _controller = std::make_unique<ttd::TimeTravelController>(_context);
            _context->pTimeTravelHooks = _controller.get();
            _context->ttdWriteSink = _controller.get();
            _context->pTimeTravelController = _controller.get();
        }
        _ttd = _context->pTimeTravelHooks;
    }

    void TearDown() override
    {
        if (_controller)
        {
            _controller->StopRecording();
            _context->pTimeTravelHooks = _context->pTimeTravelManager;
            _context->ttdWriteSink = _context->pTimeTravelManager;
            _context->pTimeTravelController = nullptr;
            _controller.reset();
        }
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    ttd::TTDSessionInfo Info() const
    {
        return _controller ? _controller->GetSessionInfo() : _context->pTimeTravelManager->GetSessionInfo();
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

TEST_P(TTDControl_Test, UnknownVerbsAndOptionsFailTheSameWayEverywhere)
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

TEST_P(TTDControl_Test, WithoutTimeTravelStatusAnswersIdleAndEveryOtherVerbIsNotAvailable)
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

TEST_P(TTDControl_Test, StartStopAndStatusReportTheSession)
{
    TTDReply r = Run("start", {{"journal", "true"}, {"history_limit_frames", "500"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "started"));
    EXPECT_FALSE(Bool(r, "already_active"));
    EXPECT_EQ(Str(r, "state"), "recording");
    EXPECT_TRUE(Bool(r, "write_journal_enabled"));
    EXPECT_EQ(Int(r, "history_limit_frames"), 500);
    // The verbs drove this instance's session: the controller's in the Controller run
    EXPECT_TRUE(_ttd->IsRecording());
    if (_controller)
        EXPECT_FALSE(_context->pTimeTravelManager->IsRecording()) << "v1 stays idle";

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
    // D12: the earliest position kept, where "jump to start" goes
    const TTDReply status = Run("status");
    const StateNode* earliest = status.body.find("earliest");
    ASSERT_NE(earliest, nullptr);
    if (Int(status, "checkpoint_count") > 0)
        EXPECT_EQ(earliest->find("frame")->i, Int(status, "session_start_frame"));
}

TEST_P(TTDControl_Test, StartWithoutTheJournalOptionKeepsTheChoice)
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

TEST_P(TTDControl_Test, ValuesThatDoNotParseAreBadRequests)
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

TEST_P(TTDControl_Test, InvalidateIsRefusedWhileRecordingAndKeepsItsReasonOtherwise)
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
    EXPECT_EQ(Info().lastDropReason, "test");
}

TEST_P(TTDControl_Test, JournalBuildIsRefusedWhileRecordingWithTheStateInTheEnvelope)
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

TEST_P(TTDControl_Test, ABodyThatNamesItsOwnErrorKeepsIt)
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

/// v1 refuses to move in the timeline while it records. The engine's controller
/// pauses the recording instead (D8): the move happens, the state is detached
/// with recording_paused, and the recording is kept
TEST_P(TTDControl_Test, MovingInTheTimelineWhileRecording)
{
    for (const auto& [verb, options] : std::vector<std::pair<std::string, std::map<std::string, std::string>>>{
             {"seek", {{"frame", "1"}}},
             {"step-back", {}},
             {"step-instruction", {{"dir", "back"}}},
             {"reverse-step", {{"count", "1"}}}})
    {
        SCOPED_TRACE(verb);
        Record(4);
        const TTDReply r = Run(verb, options);
        if (!_controller)
        {
            EXPECT_EQ(r.error, TTDControlError::Conflict);
            EXPECT_EQ(Str(r, "state"), "recording");
            EXPECT_TRUE(_ttd->IsRecording());
        }
        else
        {
            EXPECT_TRUE(r.Ok()) << r.message;
            EXPECT_FALSE(_ttd->IsRecording());
            const TTDReply status = Run("status");
            EXPECT_EQ(Str(status, "state"), "detached");
            EXPECT_TRUE(Bool(status, "recording_paused"));
            EXPECT_GT(Int(status, "checkpoint_count"), 0);
        }
        ASSERT_TRUE(Run("stop").Ok());
        EXPECT_FALSE(Bool(Run("status"), "recording_paused")) << "a stop ends the paused recording";
        ASSERT_TRUE(Run("invalidate").Ok());
    }
}

TEST_P(TTDControl_Test, SeekTargetsAreCheckedBeforeAnythingMoves)
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

TEST_P(TTDControl_Test, SeekStepAndResumeReportWhereTheMachineIs)
{
    Record(6);
    ASSERT_TRUE(Run("stop").Ok());
    const TTDReply end = Run("position");
    ASSERT_TRUE(end.Ok());
    const int64_t last = end.body.find("session_end")->find("frame")->i;
    const int64_t first = static_cast<int64_t>(Info().sessionStartFrame);
    ASSERT_GT(last, first + 2);

    // The frame's start (without a T-state the engine lands at its end, D13)
    TTDReply r = Run("seek", {{"frame", std::to_string(first + 2)}, {"tinframe", "0"}});
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

TEST_P(TTDControl_Test, InstructionStepsCheckTheirArguments)
{
    Record(4);
    ASSERT_TRUE(Run("stop").Ok());
    ASSERT_TRUE(Run("seek", {{"frame", std::to_string(Info().sessionStartFrame + 2)}}).Ok());

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

// ---------------------------------------------------------------------------
// Group 3: markers and bookmarks
// ---------------------------------------------------------------------------

TEST_P(TTDControl_Test, MarkersListTheReplayBarriers)
{
    Record(2);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::TapeControl, "play");
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    const TTDReply r = Run("markers");
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(Int(r, "count"), 1);
    const StateNode& marker = r.body.find("markers")->items.at(0);
    EXPECT_EQ(marker.find("kind")->s, ttd::TTDExternalEventKindToString(ttd::TTDExternalEventKind::TapeControl));
    EXPECT_EQ(marker.find("reason")->s, "play");
}

TEST_P(TTDControl_Test, BookmarksAreAddedListedAndDeletedByLabel)
{
    Record(4);
    ASSERT_TRUE(Run("stop").Ok());
    const uint64_t frame = Info().sessionStartFrame + 1;

    TTDReply r = Run("bookmark-add", {{"label", "entry"}, {"frame", std::to_string(frame)}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.HttpStatus(), 201);
    EXPECT_TRUE(Bool(r, "added"));
    EXPECT_EQ(Int(r, "frame"), static_cast<int64_t>(frame));

    EXPECT_EQ(Run("bookmark-add", {{"label", "entry"}}).error, TTDControlError::Conflict);  // duplicate
    EXPECT_EQ(Run("bookmark-add", {{"label", ""}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("bookmark-add", {{"label", std::string(64, 'x')}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("bookmark-add", {{"label", "late"}, {"frame", "99999999"}}).error, TTDControlError::Conflict);

    r = Run("bookmarks");
    ASSERT_TRUE(r.Ok());
    EXPECT_EQ(Int(r, "count"), 1);
    EXPECT_EQ(r.body.find("bookmarks")->items.at(0).find("label")->s, "entry");

    r = Run("seek", {{"bookmark", "entry"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.body.find("arrived_at")->find("frame")->i, static_cast<int64_t>(frame));
    EXPECT_EQ(Str(r, "bookmark"), "entry");

    EXPECT_TRUE(Run("bookmark-delete", {{"label", "entry"}}).Ok());
    const TTDReply gone = Run("bookmark-delete", {{"label", "entry"}});
    EXPECT_EQ(gone.error, TTDControlError::NotFound);
    EXPECT_EQ(gone.message, "Unknown bookmark label: entry");
}

// ---------------------------------------------------------------------------
// Group 4a: reverse queries (find-last, reverse-continue) and port events
// ---------------------------------------------------------------------------

TEST_P(TTDControl_Test, ReverseQueriesCheckTheirCriteriaAndAddresses)
{
    Record(4);
    ASSERT_TRUE(Run("stop").Ok());

    EXPECT_EQ(Run("find-last").error, TTDControlError::BadRequest);  // no criteria
    EXPECT_EQ(Run("find-last", {{"addr", "0x10000"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("find-last", {{"addr", "#5C00"}, {"access", "poke"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("find-last", {{"value", "256"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("find-last", {{"addr", "$5C00"}, {"phys_page", "300"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("reverse-continue").error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("reverse-continue", {{"pcs", "0x38,"}}).error, TTDControlError::BadRequest);

    // Every number form an address field takes
    for (const char* addr : {"23552", "0x5C00", "#5C00", "$5C00"})
    {
        const TTDReply r = Run("find-last", {{"addr", addr}});
        ASSERT_TRUE(r.Ok()) << addr << ": " << r.message;
        ASSERT_NE(r.body.find("found"), nullptr) << addr;
    }
    const TTDReply rc = Run("reverse-continue", {{"pcs", "0x38, #0D6B"}});
    ASSERT_TRUE(rc.Ok()) << rc.message;
    ASSERT_NE(rc.body.find("matched"), nullptr);
}

/// Reverse queries while recording: refused by v1, a pause on the controller (D8)
TEST_P(TTDControl_Test, ReverseQueriesWhileRecording)
{
    Record(2);
    const TTDReply findLast = Run("find-last", {{"addr", "0x5C00"}});
    if (!_controller)
    {
        EXPECT_EQ(findLast.error, TTDControlError::Conflict);
        EXPECT_EQ(Run("reverse-continue", {{"pcs", "0x38"}}).error, TTDControlError::Conflict);
        return;
    }
    EXPECT_NE(findLast.error, TTDControlError::Conflict) << findLast.message;
    EXPECT_TRUE(Bool(Run("status"), "recording_paused"));
    EXPECT_NE(Run("reverse-continue", {{"pcs", "0x38"}}).error, TTDControlError::Conflict);
}

TEST_P(TTDControl_Test, PortEventsNeedAnEventAndKnownOptions)
{
    Record(2);
    ASSERT_TRUE(Run("stop").Ok());
    TTDReply r = Run("port-events");
    EXPECT_EQ(r.error, TTDControlError::BadRequest);
    EXPECT_NE(r.message.find("'event' is required"), std::string::npos) << r.message;
    EXPECT_EQ(Run("port-events", {{"event", "teleport"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("port-events", {{"event", "border"}, {"colour", "2"}}).error, TTDControlError::BadRequest);
}

// ---------------------------------------------------------------------------
// Group 4b: coverage queries
// ---------------------------------------------------------------------------

TEST_P(TTDControl_Test, CoverageQueriesCheckEveryValue)
{
    EXPECT_EQ(Run("coverage-probe").error, TTDControlError::BadRequest);  // frame is required
    EXPECT_EQ(Run("coverage-probe", {{"frame", "1"}, {"kind", "jumped"}}).error, TTDControlError::BadRequest);
    const TTDReply range = Run("coverage-scan", {{"addr_from", "0x8000"}, {"addr_to", "0x4000"}});
    EXPECT_EQ(range.error, TTDControlError::BadRequest);
    EXPECT_EQ(range.message, "addr_from (0x8000) must not exceed addr_to (0x4000)");
    EXPECT_EQ(Run("coverage-scan", {{"limit", "0"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("coverage-scan", {{"phys_page", "256"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("coverage-summary", {{"bucket_size", "x"}}).error, TTDControlError::BadRequest);
}

TEST_P(TTDControl_Test, CoverageQueriesAnswerWithoutTimeTravel)
{
    TTDControl control(nullptr);
    const TTDReply probe = control.Execute({"coverage-probe", {{"frame", "5"}}});
    ASSERT_TRUE(probe.Ok()) << probe.message;
    EXPECT_FALSE(Bool(probe, "index_available"));
    EXPECT_FALSE(Bool(probe, "touched"));
    EXPECT_EQ(Str(probe, "addr_to"), "0xFFFF");
    const TTDReply scan = control.Execute({"coverage-scan", {}});
    ASSERT_TRUE(scan.Ok()) << scan.message;
    EXPECT_TRUE(scan.body.find("frames")->isArray());
    const TTDReply summary = control.Execute({"coverage-summary", {}});
    ASSERT_TRUE(summary.Ok()) << summary.message;
    EXPECT_EQ(Int(summary, "bucket_size"), 0);
}

TEST_P(TTDControl_Test, CoverageScanFindsTheFramesThatRanTheRom)
{
    Record(6);
    ASSERT_TRUE(Run("stop").Ok());
    const TTDReply r = Run("coverage-scan", {{"kind", "executed"}, {"addr_from", "0"}, {"addr_to", "0x3FFF"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    if (!Bool(r, "index_available"))
        GTEST_SKIP() << "coverage index off in this configuration";
    EXPECT_GT(Int(r, "matching_frames"), 0);
    EXPECT_EQ(static_cast<int64_t>(r.body.find("frames")->items.size()), Int(r, "matching_frames"));
}

// ---------------------------------------------------------------------------
// Group 5: files
// ---------------------------------------------------------------------------

TEST_P(TTDControl_Test, ASessionDumpedIsDescribedAndLoadedThroughTheVerbs)
{
    Record(3);
    ASSERT_TRUE(Run("stop").Ok());
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("ttdcontrol-dump.ttd");

    TTDReply r = Run("dump", {{"path", path}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_GT(Int(r, "bytes"), 0);

    // file-info needs no instance at all
    r = TTDControl(nullptr).Execute({"file-info", {{"path", path}}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "ok"));
    EXPECT_GT(Int(r, "checkpoint_count"), 0);
    EXPECT_TRUE(r.body.find("machine")->isObject());

    ASSERT_TRUE(Run("invalidate").Ok());
    r = Run("load", {{"path", path}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_GT(Int(r, "checkpoint_count"), 0);
    EXPECT_EQ(Str(r, "state"), "idle");
    std::remove(path.c_str());
}

TEST_P(TTDControl_Test, FileVerbsReportMissingAndUnreadableFiles)
{
    const std::string missing = TestPathHelper::GetUniqueTestScratchPath("no-such-session.ttd");
    TTDReply r = TTDControl(nullptr).Execute({"file-info", {{"path", missing}}});
    EXPECT_EQ(r.error, TTDControlError::NotFound);
    EXPECT_FALSE(Bool(r, "ok"));
    EXPECT_EQ(Str(r, "path"), missing);
    EXPECT_EQ(TTDControl(nullptr).Execute({"file-info", {}}).error, TTDControlError::BadRequest);

    EXPECT_EQ(Run("load", {{"path", missing}}).error, TTDControlError::NotFound);
    EXPECT_EQ(Run("dump", {{"path", missing + "/inside/a/file"}}).error, TTDControlError::Conflict);
    EXPECT_EQ(Run("export-clip", {{"from", "0"}, {"to", "2"}}).error, TTDControlError::BadRequest);  // no path
    Record(2);
    EXPECT_EQ(Run("export-clip", {{"from", "0"}, {"to", "1"}, {"path", missing}}).error, TTDControlError::Conflict);
}

/// D13: a seek to a frame without a T-state lands at the frame's end on the
/// engine - the state of {N+1, 0} and the frame's final picture - and is named
/// {N, length of N}; v1 keeps the frame's start. With a T-state nothing changes
TEST_P(TTDControl_Test, AFrameWithoutATStateIsItsEnd)
{
    // A program that changes the screen all the time: DI; LD A,2; OUT (#FE),A;
    // again: LD HL,#5800; loop: INC (HL); INC L; JR NZ,loop; JR again
    const uint8_t program[] = {0xF3, 0x3E, 0x02, 0xD3, 0xFE, 0x21, 0x00, 0x58, 0x34, 0x2C, 0x20, 0xFC, 0x18, 0xF7};
    for (size_t i = 0; i < sizeof(program); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pCore->GetZ80()->pc = 0x8000;

    auto picture = [&]() {
        uint32_t* buffer = nullptr;
        size_t size = 0;
        _context->pScreen->GetFramebufferData(&buffer, &size);
        return std::vector<uint32_t>(buffer, buffer + size / sizeof(uint32_t));
    };
    auto cpu = [&]() { return ttd::CaptureCpuState(*_context->pCore->GetZ80()); };

    ASSERT_TRUE(Run("start").Ok());
    std::vector<std::vector<uint32_t>> pictureAtEnd;   // the final picture of each recorded frame
    const uint64_t first = _context->emulatorState.frame_counter;
    for (int f = 0; f < 8; ++f)
    {
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        pictureAtEnd.push_back(picture());
    }
    _emulator->RunTStates(30000, /*skipBreakpoints=*/true);
    if (!GetParam())
        ASSERT_TRUE(Run("stop").Ok()) << "v1 browses a stopped session only";
    const uint64_t n = first + 4;

    // The frame's start, asked for explicitly: unchanged on both
    TTDReply r = Run("seek", {{"frame", std::to_string(n + 1)}, {"tinframe", "0"}});
    ASSERT_TRUE(Bool(r, "reached")) << r.message;
    const ttd::TTDCpuState startOfNext = cpu();
    const std::vector<uint32_t> pictureOfNext = picture();

    r = Run("seek", {{"frame", std::to_string(n)}});
    ASSERT_TRUE(Bool(r, "reached")) << r.message;
    const StateNode* at = r.body.find("arrived_at");
    ASSERT_NE(at, nullptr);
    if (!GetParam())
    {
        EXPECT_EQ(at->find("frame")->i, int64_t(n)) << "v1: the frame's start";
        EXPECT_EQ(at->find("tinframe")->i, 0);
        return;
    }
    EXPECT_EQ(at->find("frame")->i, int64_t(n)) << "named in frame N";
    EXPECT_GT(at->find("tinframe")->i, 60000) << "at its length";
    const ttd::TTDCpuState end = cpu();
    EXPECT_EQ(std::memcmp(&end, &startOfNext, sizeof(end)), 0) << "{N} is {N+1, 0}";
    EXPECT_TRUE(picture() == pictureOfNext);
    EXPECT_TRUE(picture() == pictureAtEnd[n - first]) << "the frame's final picture";
    EXPECT_FALSE(picture() == pictureAtEnd[n - first - 1]) << "the screen changes every frame";

    // The last frame: the history ends inside it (the recording paused at the present)
    const uint64_t last = first + 8;
    r = Run("seek", {{"frame", std::to_string(last)}});
    ASSERT_TRUE(Bool(r, "reached")) << r.message;
    EXPECT_EQ(r.body.find("arrived_at")->find("frame")->i, int64_t(last));
    EXPECT_GT(r.body.find("arrived_at")->find("tinframe")->i, 0) << "where the history ends";
    // Past the history
    EXPECT_FALSE(Bool(Run("seek", {{"frame", std::to_string(last + 5)}}), "reached"));
}

INSTANTIATE_TEST_SUITE_P(Backends, TTDControl_Test, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "Controller" : "V1"; });
