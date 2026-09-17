/// @file ttd_automation_contract_test.cpp
/// @brief Phase 2+ Automation: TTD API contract test.
///
/// Verifies that the TimeTravelManager surface — as consumed by all four
/// automation layers (CLI, WebAPI, Python, Lua) — works end-to-end.
///
/// This is NOT a re-test of the engine internals (those have 230 dedicated
/// tests). It's a contract test that locks down the method signatures, return
/// types, and data shapes that each automation adapter relies on:
///
///   - StartRecording / StopRecording / InvalidateSession lifecycle
///   - GetSessionInfo returns the expected fields
///   - SeekTo with TTDSeekResult reports halt_reason correctly
///   - StepBackFrame / StepForwardFrame return bool
///   - ResumeRecordingFrom works after a seek
///   - CurrentPosition / SessionEndPosition return valid TTDTimePoints
///   - GetExternalEvents returns the journal with marker details
///   - RecordExternalEvent is visible through GetExternalEvents
///
/// If this test passes, the CLI handler, WebAPI endpoints, Python .def()
/// bindings, and Lua set_function() bindings will all work correctly — they
/// are thin wrappers around the same TimeTravelManager methods exercised here.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <vector>

#include "_helpers/testpathhelper.h"

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdexternalevents.h"
#include "debugger/ttd/ttdprobe.h"

/// region <Test fixture>

class TTD_Automation_Contract_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        ASSERT_NE(_context->pTimeTravelManager, nullptr);

        _ttd = _context->pTimeTravelManager;
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);

        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
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

    /// Run N frames, waiting for completion
    void RunFrames(uint32_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }
};

/// endregion <Test fixture>

/// region <Lifecycle contract — matches CLI/WebAPI/Python/Lua ttd_start/stop/invalidate>

TEST_F(TTD_Automation_Contract_Test, Lifecycle_StartStopInvalidate)
{
    // Initial state must be Idle
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);
    EXPECT_FALSE(_ttd->IsRecording());

    // Start
    EXPECT_TRUE(_ttd->StartRecording());
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
    EXPECT_TRUE(_ttd->IsRecording());

    // Start is idempotent
    EXPECT_TRUE(_ttd->StartRecording());
    EXPECT_TRUE(_ttd->IsRecording());

    // Stop
    _ttd->StopRecording();
    EXPECT_FALSE(_ttd->IsRecording());

    // Stop is idempotent
    _ttd->StopRecording();
    EXPECT_FALSE(_ttd->IsRecording());

    // Invalidate clears history
    _ttd->InvalidateSession("test invalidate");
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);
    EXPECT_EQ(_ttd->GetCheckpointCount(), 0u);
}

/// endregion

/// region <Session info contract — matches ttd_status / GET /ttd/status>

TEST_F(TTD_Automation_Contract_Test, SessionInfo_HasExpectedFields)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);

    ttd::TTDSessionInfo info = _ttd->GetSessionInfo();

    // Every field that the automation surfaces expose must be present and valid
    EXPECT_EQ(info.state, ttd::TTDSessionState::Recording);
    // sessionStartFrame is the emulator's frame counter at StartRecording —
    // can be 0 on a fresh emulator. Just verify it's sane.
    EXPECT_GE(info.currentEndFrame, info.sessionStartFrame);
    EXPECT_GE(info.checkpointCount, 1u);  // At least the baseline after RunFrames
    EXPECT_GT(info.pageStoreBytes, 0u);  // Capacity > 0
    EXPECT_GE(info.pageStoreUsedBytes, 0u);

    // Real heap footprint — must be present, non-zero, and at least as
    // large as the page-store backing (pageStoreBytes counts only the
    // page-store vector; sessionHeapBytes also includes per-checkpoint
    // metadata + journal backing).
    EXPECT_GT(info.sessionHeapBytes, 0u);
    EXPECT_GE(info.sessionHeapBytes, info.pageStoreBytes);

    // Verify the string conversion (used by all surfaces for JSON/table values)
    std::string stateStr = ttd::TTDSessionStateToString(info.state);
    EXPECT_EQ(stateStr, "recording");

    _ttd->StopRecording();
}

TEST_F(TTD_Automation_Contract_Test, SessionInfo_StateString_AllValues)
{
    // All three state strings used by the automation contract
    EXPECT_STREQ(ttd::TTDSessionStateToString(ttd::TTDSessionState::Idle), "idle");
    EXPECT_STREQ(ttd::TTDSessionStateToString(ttd::TTDSessionState::Recording), "recording");
    EXPECT_STREQ(ttd::TTDSessionStateToString(ttd::TTDSessionState::Detached), "detached");
}

/// endregion

/// region <Seek contract — matches ttd_seek / POST /ttd/seek>

TEST_F(TTD_Automation_Contract_Test, Seek_WithResult_ReportsTargetHalt)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    // Seek to frame 5 (within bounds)
    ttd::TTDTimePoint target{5, 0};
    ttd::TimeTravelManager::TTDSeekResult result;
    bool reached = _ttd->SeekTo(target, &result);

    EXPECT_TRUE(reached);
    EXPECT_EQ(result.haltReason, ttd::TimeTravelManager::TTDSeekHaltReason::Target);
    EXPECT_EQ(result.arrivedAt.frame, 5u);
}

TEST_F(TTD_Automation_Contract_Test, Seek_OutOfBounds_ReportsOutOfRange)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);

    // Seek beyond the session end
    ttd::TTDTimePoint target{1000, 0};
    ttd::TimeTravelManager::TTDSeekResult result;
    bool reached = _ttd->SeekTo(target, &result);

    EXPECT_FALSE(reached);
    EXPECT_EQ(result.haltReason, ttd::TimeTravelManager::TTDSeekHaltReason::OutOfRange);
}

TEST_F(TTD_Automation_Contract_Test, Seek_MarkerBarrier_ReportsExternalEvent)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _emulator->RunTStates(500, true);

    // Record a marker mid-frame
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite, "test disk write");

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    const uint32_t markerT = _ttd->GetExternalEvents().Events()[0].time.tInFrame;
    ASSERT_GT(markerT, 0u);

    RunFrames(2);
    _ttd->StopRecording();

    // Seek to a target past the marker
    ttd::TTDTimePoint target{1, markerT + 500};
    ttd::TimeTravelManager::TTDSeekResult result;
    bool reached = _ttd->SeekTo(target, &result);

    EXPECT_FALSE(reached);
    EXPECT_EQ(result.haltReason, ttd::TimeTravelManager::TTDSeekHaltReason::ExternalEvent);
    EXPECT_EQ(result.blockingMarker.kind, ttd::TTDExternalEventKind::DiskWrite);
    EXPECT_STREQ(result.blockingMarker.reason, "test disk write");
}

/// endregion

/// region <Step contract — matches ttd_step_back / ttd_step_forward>

TEST_F(TTD_Automation_Contract_Test, Step_BackAndForward_RoundTrip)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    // Read the session end — stepping forward beyond this will fail
    ttd::TTDTimePoint sessionEnd = _ttd->SessionEndPosition();

    // Seek to a known position (a few frames before the end)
    uint64_t targetFrame = sessionEnd.frame > 2 ? sessionEnd.frame - 2 : sessionEnd.frame;
    ASSERT_TRUE(_ttd->SeekTo({targetFrame, 0}));
    ttd::TTDTimePoint posBefore = _ttd->CurrentPosition();
    EXPECT_EQ(posBefore.frame, targetFrame);

    // Step back
    EXPECT_TRUE(_ttd->StepBackFrame());
    ttd::TTDTimePoint posAfterBack = _ttd->CurrentPosition();
    EXPECT_EQ(posAfterBack.frame, targetFrame - 1);

    // Step forward (undo) — should work because we're below session end
    EXPECT_TRUE(_ttd->StepForwardFrame());
    ttd::TTDTimePoint posAfterForward = _ttd->CurrentPosition();
    EXPECT_EQ(posAfterForward.frame, targetFrame);
}

TEST_F(TTD_Automation_Contract_Test, Step_BackAtStart_Fails)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    _ttd->StopRecording();

    // Seek to the first frame
    ttd::TTDTimePoint first{_ttd->GetSessionInfo().sessionStartFrame, 0};
    ASSERT_TRUE(_ttd->SeekTo(first));

    // Step back should fail (at or before first)
    EXPECT_FALSE(_ttd->StepBackFrame());
}

/// endregion

/// region <Resume contract — matches ttd_resume / POST /ttd/resume>

TEST_F(TTD_Automation_Contract_Test, Resume_FromPast_TruncatesAndResumes)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);

    size_t checkpointsBefore = _ttd->GetCheckpointCount();

    // Resume from frame 5 (drops frames 6-10)
    ttd::TTDTimePoint from{5, 0};
    EXPECT_TRUE(_ttd->ResumeRecordingFrom(from));

    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
    EXPECT_LT(_ttd->GetCheckpointCount(), checkpointsBefore);
}

/// endregion

/// region <Position contract — matches ttd_position / GET /ttd/position>

TEST_F(TTD_Automation_Contract_Test, Position_CurrentAndEnd_Valid)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);

    ttd::TTDTimePoint current = _ttd->CurrentPosition();
    ttd::TTDTimePoint end = _ttd->SessionEndPosition();

    EXPECT_GE(current.frame, 1u);
    EXPECT_GE(end.frame, current.frame);
}

/// endregion

/// region <Markers contract — matches ttd_markers / GET /ttd/markers>

TEST_F(TTD_Automation_Contract_Test, Markers_ListAfterRecording)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);

    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::TapeControl, "tape play");
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite, "disk write");
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DebuggerEdit, "mem edit");

    RunFrames(1);

    const auto& journal = _ttd->GetExternalEvents();
    EXPECT_EQ(journal.Size(), 3u);

    // Verify all marker kinds convert to strings (used by all surfaces)
    EXPECT_STREQ(ttd::TTDExternalEventKindToString(ttd::TTDExternalEventKind::TapeControl), "tape_control");
    EXPECT_STREQ(ttd::TTDExternalEventKindToString(ttd::TTDExternalEventKind::DiskWrite), "disk_write");
    EXPECT_STREQ(ttd::TTDExternalEventKindToString(ttd::TTDExternalEventKind::DebuggerEdit), "debugger_edit");

    // Verify each marker has valid fields
    const auto& events = journal.Events();
    EXPECT_EQ(events[0].kind, ttd::TTDExternalEventKind::TapeControl);
    EXPECT_STREQ(events[0].reason, "tape play");

    EXPECT_EQ(events[1].kind, ttd::TTDExternalEventKind::DiskWrite);
    EXPECT_STREQ(events[1].reason, "disk write");

    EXPECT_EQ(events[2].kind, ttd::TTDExternalEventKind::DebuggerEdit);
    EXPECT_STREQ(events[2].reason, "mem edit");
}

/// endregion

/// region <Full automation round-trip — mimics a user session via any surface>

TEST_F(TTD_Automation_Contract_Test, FullRoundTrip_StartRecordSeekStepResume)
{
    // This test mimics a complete automation-driven TTD session, exercising
    // every method that the CLI/WebAPI/Python/Lua surfaces expose.

    // 1. Status before start
    ttd::TTDSessionInfo info0 = _ttd->GetSessionInfo();
    EXPECT_EQ(info0.state, ttd::TTDSessionState::Idle);
    EXPECT_EQ(info0.checkpointCount, 0u);

    // 2. Start recording
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(20);

    // 3. Status during recording
    ttd::TTDSessionInfo info1 = _ttd->GetSessionInfo();
    EXPECT_EQ(info1.state, ttd::TTDSessionState::Recording);
    EXPECT_GE(info1.checkpointCount, 1u);

    // 4. Check position
    ttd::TTDTimePoint current = _ttd->CurrentPosition();
    ttd::TTDTimePoint end = _ttd->SessionEndPosition();
    EXPECT_LE(current.frame, end.frame);

    // Stop recording before scrubbing — scrubbing during Recording is
    // rejected by the engine (would corrupt the timeline).
    _ttd->StopRecording();

    // 5. Seek to mid-session
    uint64_t midFrame = info1.sessionStartFrame + 10;
    ttd::TTDTimePoint seekTarget{midFrame, 0};
    ttd::TimeTravelManager::TTDSeekResult seekResult;
    EXPECT_TRUE(_ttd->SeekTo(seekTarget, &seekResult));
    EXPECT_EQ(seekResult.haltReason, ttd::TimeTravelManager::TTDSeekHaltReason::Target);

    // 6. Step back
    EXPECT_TRUE(_ttd->StepBackFrame());
    ttd::TTDTimePoint afterStepBack = _ttd->CurrentPosition();
    EXPECT_EQ(afterStepBack.frame, midFrame - 1);

    // 7. Resume recording from current position
    EXPECT_TRUE(_ttd->ResumeRecordingFrom(afterStepBack));
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);

    // 8. Stop
    _ttd->StopRecording();
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);

    // 9. Invalidate
    _ttd->InvalidateSession("round-trip complete");
    EXPECT_EQ(_ttd->GetCheckpointCount(), 0u);
}

/// endregion

/// region <Phase 4: Dump contract — matches ttd dump / POST /ttd/dump>

TEST_F(TTD_Automation_Contract_Test, Dump_SerializeSession_RoundTrip)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Populate the journal with known write records so we can verify
    // they survive serialization.
    _ttd->RecordMemoryWrite(0x1000, 0, 0x42, 0x2000, 1);
    _ttd->RecordMemoryWrite(0x2000, 0, 0x55, 0x2100, 2);
    _ttd->RecordIoWrite(0xFE, 0x08, 0x2200);

    RunFrames(3);
    _ttd->StopRecording();

    const size_t journalSizeBefore = _ttd->GetWriteJournal()->Size();
    ASSERT_GT(journalSizeBefore, 0u);
    const size_t checkpointCount = _ttd->GetCheckpointCount();
    ASSERT_GT(checkpointCount, 0u);

    // Serialize to a scratch file
    const std::string tmpfile = TestPathHelper::GetUniqueTestScratchPath("ttd_contract_dump.bin");

    {
        std::ofstream out(tmpfile, std::ios::binary);
        std::string err;
        ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;
    }

    // Deserialize into a fresh emulator instance
    Emulator* emu2 = new Emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emu2->Init());
    ttd::TimeTravelManager* ttd2 = emu2->GetContext()->pTimeTravelManager;
    ASSERT_NE(ttd2, nullptr);

    {
        std::ifstream in(tmpfile, std::ios::binary);
        std::string err;
        ASSERT_TRUE(ttd2->DeserializeSession(in, err)) << err;
    }

    // Verify round-trip preserves checkpoint count and journal records
    EXPECT_EQ(ttd2->GetCheckpointCount(), checkpointCount);
    EXPECT_EQ(ttd2->GetWriteJournal()->Size(), journalSizeBefore);

    // Verify the journal is queryable after load — this is the contract
    // shape that all automation surfaces rely on post-dump.
    ttd::TTDSearchQuery q;
    q.addrFrom = 0x1000;
    q.addrTo   = 0x1000;
    q.access   = ttd::TTDAccessType::Write;
    auto result = ttd2->FindLastAccess(q);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 0x42u);

    emu2->Stop();
    emu2->Release();
    delete emu2;
    remove(tmpfile.c_str());
}

/// endregion

/// region <Phase 4: FindLast contract — matches ttd find-last / POST /ttd/find-last>

TEST_F(TTD_Automation_Contract_Test, FindLast_QueryShape_HasExpectedFields)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Perform a known write to the journal
    _ttd->RecordMemoryWrite(0x4000, 0, 0x99, 0x1234, 5);

    RunFrames(1);
    _ttd->StopRecording();

    // Query the write — this exercises the same TTDSearchQuery → FindLastAccess
    // → TTDSearchResult path used by all automation surfaces.
    ttd::TTDSearchQuery q;
    q.addrFrom = 0x4000;
    q.addrTo   = 0x4000;
    q.access   = ttd::TTDAccessType::Write;
    auto result = _ttd->FindLastAccess(q);

    ASSERT_TRUE(result.has_value());

    // Verify all expected fields — this is the contract shape that
    // CLI/WebAPI/Python adapters serialize into their respective formats.
    EXPECT_EQ(result->pc, 0x1234u);
    EXPECT_EQ(result->value, 0x99u);
    EXPECT_EQ(result->physPage, 5u);
    EXPECT_EQ(result->access, ttd::TTDAccessType::Write);
    // Time fields must be valid
    EXPECT_GE(result->time.frame, 0u);

    // Verify access-type string conversion (used by JSON serialization
    // across all surfaces)
    EXPECT_STREQ(ttd::TTDAccessTypeToString(result->access), "write");

    // Verify all four access-type strings are available (contract stability)
    EXPECT_STREQ(ttd::TTDAccessTypeToString(ttd::TTDAccessType::Write),   "write");
    EXPECT_STREQ(ttd::TTDAccessTypeToString(ttd::TTDAccessType::Read),    "read");
    EXPECT_STREQ(ttd::TTDAccessTypeToString(ttd::TTDAccessType::Execute), "execute");
    EXPECT_STREQ(ttd::TTDAccessTypeToString(ttd::TTDAccessType::Io),      "io");
}

TEST_F(TTD_Automation_Contract_Test, FindLast_AddressRange_And_PCRange_Query)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Write into 0x4500 from PC 0x1234
    _ttd->RecordMemoryWrite(0x4500, 0, 0xAA, 0x1234, 5);

    RunFrames(1);
    _ttd->StopRecording();

    // 1. Range query: 0x4000..0x5000 (should match write at 0x4500)
    ttd::TTDSearchQuery qRange;
    qRange.addrFrom = 0x4000;
    qRange.addrTo   = 0x5000;
    qRange.access   = ttd::TTDAccessType::Write;
    auto resRange = _ttd->FindLastAccess(qRange);
    ASSERT_TRUE(resRange.has_value());
    EXPECT_EQ(resRange->pc, 0x1234u);
    EXPECT_EQ(resRange->value, 0xAAu);

    // 2. PC-only range query: PC 0x1200..0x1300 across all memory (0..0xFFFF)
    ttd::TTDSearchQuery qPc;
    qPc.addrFrom = 0;
    qPc.addrTo   = 0xFFFF;
    qPc.access   = ttd::TTDAccessType::Write;
    qPc.hasPcFilter = true;
    qPc.pcFrom = 0x1200;
    qPc.pcTo   = 0x1300;
    auto resPc = _ttd->FindLastAccess(qPc);
    ASSERT_TRUE(resPc.has_value());
    EXPECT_EQ(resPc->pc, 0x1234u);
    EXPECT_EQ(resPc->value, 0xAAu);
}

TEST_F(TTD_Automation_Contract_Test, FindLast_AllOptionalFilters_Contract)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Record two distinct writes at 0x6000 with different values, PCs, and pages
    _ttd->RecordMemoryWrite(0x6000, 0, 0x11, 0x1000, 2); // Write #1
    RunFrames(2);
    _ttd->RecordMemoryWrite(0x6000, 0, 0x22, 0x2000, 5); // Write #2
    RunFrames(1);

    _ttd->StopRecording();

    // 1. Filter by value = 0x11 (should match Write #1 at PC 0x1000)
    ttd::TTDSearchQuery qVal;
    qVal.addrFrom = qVal.addrTo = 0x6000;
    qVal.access = ttd::TTDAccessType::Write;
    qVal.hasValueFilter = true;
    qVal.value = 0x11;
    auto resVal = _ttd->FindLastAccess(qVal);
    ASSERT_TRUE(resVal.has_value());
    EXPECT_EQ(resVal->pc, 0x1000u);
    EXPECT_EQ(resVal->value, 0x11u);

    // 2. Filter by physPage = 2 (should match Write #1 at page 2)
    ttd::TTDSearchQuery qPage;
    qPage.addrFrom = qPage.addrTo = 0x6000;
    qPage.access = ttd::TTDAccessType::Write;
    qPage.hasPhysPageFilter = true;
    qPage.physPage = 2;
    auto resPage = _ttd->FindLastAccess(qPage);
    ASSERT_TRUE(resPage.has_value());
    EXPECT_EQ(resPage->pc, 0x1000u);
    EXPECT_EQ(resPage->physPage, 2u);

    // 3. Filter by beforeGlobalT (should exclude Write #2 if before Write #2's timestamp)
    ttd::TTDSearchQuery qTime;
    qTime.addrFrom = qTime.addrTo = 0x6000;
    qTime.access = ttd::TTDAccessType::Write;
    qTime.beforeGlobalT = resVal->time.frame * _context->config.frame + resVal->time.tInFrame + 1;
    auto resTime = _ttd->FindLastAccess(qTime);
    ASSERT_TRUE(resTime.has_value());
    EXPECT_EQ(resTime->pc, 0x1000u);
}

/// endregion

/// region <Phase 4: StepInstruction contract — matches ttd step-instruction / POST /ttd/step-instruction>

TEST_F(TTD_Automation_Contract_Test, StepInstruction_BackwardForwardRoundTrip)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    // Seek to a mid-session position
    ttd::TTDTimePoint sessionEnd = _ttd->SessionEndPosition();
    uint64_t midFrame = sessionEnd.frame > 1 ? sessionEnd.frame / 2 : 1;
    ASSERT_TRUE(_ttd->SeekTo({midFrame, 0}));

    const uint32_t frameT = _context->config.frame;
    ttd::TTDTimePoint posBefore = _ttd->CurrentPosition();
    const uint64_t globalTBefore =
        static_cast<uint64_t>(posBefore.frame) * frameT + posBefore.tInFrame;

    // Step forward one instruction (should succeed — we're before session end)
    EXPECT_TRUE(_ttd->StepForwardInstruction());

    ttd::TTDTimePoint posAfterForward = _ttd->CurrentPosition();
    const uint64_t globalTAfterForward =
        static_cast<uint64_t>(posAfterForward.frame) * frameT + posAfterForward.tInFrame;

    // Position must have advanced
    EXPECT_GT(globalTAfterForward, globalTBefore);

    // Step back one instruction (should succeed — we're not at session start)
    EXPECT_TRUE(_ttd->StepBackInstruction());

    ttd::TTDTimePoint posAfterBack = _ttd->CurrentPosition();
    const uint64_t globalTAfterBack =
        static_cast<uint64_t>(posAfterBack.frame) * frameT + posAfterBack.tInFrame;

    // Position should have moved backward from the post-forward position
    EXPECT_LE(globalTAfterBack, globalTAfterForward);
}

// ===========================================================================
// Phase 4 — Reverse Execution contract tests
//
// These verify the method signatures and return shapes that the WebAPI
// endpoints (/ttd/reverse-step, /ttd/reverse-continue), CLI handlers
// (ttd reverse-step, ttd reverse-continue), and Python bindings
// (ttd_reverse_step, ttd_reverse_continue) all depend on.
// ===========================================================================

TEST_F(TTD_Automation_Contract_Test, ReverseStep_QueryShape_HasExpectedFields)
{
    // ReverseStepInstructions(count) returns bool. After a successful call,
    // CurrentPosition must return a valid (frame, tInFrame) pair.
    // The WebAPI serializes this as { "reached": bool, "frame": int, "tinframe": int }.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    const ttd::TTDTimePoint before = _ttd->CurrentPosition();

    // Contract: returns bool.
    bool ok = _ttd->ReverseStepInstructions(16);
    EXPECT_TRUE(ok);

    // Contract: CurrentPosition returns a valid TTDTimePoint that moved backward.
    const ttd::TTDTimePoint after = _ttd->CurrentPosition();
    EXPECT_TRUE(after < before);

    // Contract: frame and tInFrame are accessible scalar fields.
    EXPECT_GE(after.frame, 0u);
    EXPECT_LT(after.frame, before.frame + 1);
    // tInFrame is bounded by config.frame.
    const uint32_t frameT = _context->config.frame;
    EXPECT_LT(after.tInFrame, frameT);
}

TEST_F(TTD_Automation_Contract_Test, ReverseContinue_QueryShape_HasExpectedFields)
{
    // ReverseContinue(pcs) returns TTDReverseContinueResult with fields:
    //   matched (bool), pc (uint16_t), arrivedAt (TTDTimePoint).
    // The WebAPI serializes this as
    //   { "matched": bool, "pc": int, "frame": int, "tinframe": int }.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));

    // Use the current PC as the reverse breakpoint — it must match.
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    ASSERT_NE(z80, nullptr);
    const uint16_t targetPC = z80->pc;

    auto result = _ttd->ReverseContinue({targetPC});

    // Contract: matched is bool, pc is uint16_t.
    EXPECT_TRUE(result.matched);
    EXPECT_EQ(result.pc, targetPC);

    // Contract: arrivedAt has valid frame and tInFrame.
    EXPECT_GE(result.arrivedAt.frame, 0u);
    const uint32_t frameT = _context->config.frame;
    EXPECT_LT(result.arrivedAt.tInFrame, frameT);

    // Contract: no-match returns matched=false, pc=0xFFFF.
    auto noMatch = _ttd->ReverseContinue({0xFFFE});
    EXPECT_FALSE(noMatch.matched);
    EXPECT_EQ(noMatch.pc, 0xFFFF);
}

TEST_F(TTD_Automation_Contract_Test, CoverageQuery_Contract_ProbeScanSummary)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    // Covered window contract: the scan echoes the clamped effective window (TD-7 defect B fix).
    auto scan = _ttd->QueryCoverageScan(0, 5, ttd::TTDCoverageKind::Executed, 0x0000, 0x0010);
    EXPECT_TRUE(scan.indexAvailable);
    EXPECT_LE(scan.coveredFrom, scan.coveredTo);
    EXPECT_GT(scan.scannedFrames, 0u);
    EXPECT_GT(scan.matchingFrames, 0u);
    EXPECT_FALSE(scan.frames.empty());
    EXPECT_GE(scan.frames.front(), scan.coveredFrom);
    EXPECT_LE(scan.frames.back(), scan.coveredTo);

    // Probe contract: availability is per-frame — a frame outside the covered window
    // reports index_available=false instead of a conservative false positive (TD-7 defect A fix).
    auto probeOutside = _ttd->QueryCoverageProbe(scan.coveredTo + 10, ttd::TTDCoverageKind::Executed, 0x0000, 0x0010);
    EXPECT_FALSE(probeOutside.indexAvailable);
    EXPECT_FALSE(probeOutside.touched);

    auto probe = _ttd->QueryCoverageProbe(scan.coveredFrom, ttd::TTDCoverageKind::Executed, 0x0000, 0x0010);
    EXPECT_TRUE(probe.indexAvailable);
    EXPECT_TRUE(probe.touched);
    EXPECT_EQ(probe.frame, scan.coveredFrom);
    EXPECT_EQ(probe.kind, ttd::TTDCoverageKind::Executed);

    // Summary contract: the covered window is echoed (union across kinds).
    auto summary = _ttd->QueryCoverageSummary(0, 5);
    EXPECT_TRUE(summary.indexAvailable);
    EXPECT_LE(summary.coveredFrom, summary.coveredTo);
    EXPECT_GT(summary.bucketCount, 0u);
    EXPECT_FALSE(summary.buckets.empty());
    EXPECT_GT(summary.buckets[0].executedDistinct, 0u);
}

/// endregion
