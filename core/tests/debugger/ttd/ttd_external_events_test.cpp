/// @file ttd_external_events_test.cpp
/// @brief Phase 2 Item 6 — External-event markers tests.
///
/// Per parent TDD §5.1 (External-Event Markers) and §717 (halt_reason contract).
///
/// Three test suites:
///   1. TTD_ExternalEvents_Test — pure journal data structure (no emulator):
///      Record / Size / Events / Clear / DropAfter / FirstMarkerInInterval
///      + TTDExternalEventKindToString stability for automation contract.
///
///   2. TTD_ExternalEvents_API_Test — capture integration through
///      TimeTravelManager::RecordExternalEvent: state guard, time capture,
///      reason truncation, lifecycle (StartRecording / InvalidateSession /
///      ResumeRecordingFrom all clip the journal correctly).
///
///   3. TTD_ExternalEvents_Seek_Test — marker-blocks-seek behavior via the
///      barrier-aware SeekTo(target, TTDSeekResult*) overload. Frame-aligned
///      targets never trigger the barrier check; intra-frame targets with a
///      marker strictly inside (cp.time, target] stop at the marker with
///      haltReason=ExternalEvent and the blocking marker surfaced.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/ttd_external_events.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

// TTDSeekHaltReason / TTDSeekResult are nested inside TimeTravelManager
// (defined in timetravelmanager.h). Pull them into convenient aliases so the
// test bodies read like the parent TDD's pseudocode.
using SeekHaltReason = ttd::TimeTravelManager::TTDSeekHaltReason;
using SeekResult     = ttd::TimeTravelManager::TTDSeekResult;

// ===========================================================================
// Suite 1 — pure journal unit tests (no emulator, no manager).
// ===========================================================================

class TTD_ExternalEvents_Test : public ::testing::Test
{
protected:
    ttd::TTDExternalEventJournal _journal;

    ttd::TTDExternalEvent MakeEv(uint64_t frame,
                                 uint32_t tInFrame,
                                 ttd::TTDExternalEventKind kind,
                                 const char* reason)
    {
        ttd::TTDExternalEvent ev;
        ev.time.frame    = frame;
        ev.time.tInFrame = tInFrame;
        ev.kind          = kind;
        if (reason)
        {
            std::strncpy(ev.reason, reason, sizeof(ev.reason) - 1);
            ev.reason[sizeof(ev.reason) - 1] = '\0';
        }
        return ev;
    }
};

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Test, InitiallyEmpty)
{
    EXPECT_TRUE(_journal.IsEmpty());
    EXPECT_EQ(_journal.Size(), 0u);
    EXPECT_TRUE(_journal.Events().empty());
}

// ---------------------------------------------------------------------------
// Record
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Test, Record_AppendsAndPreservesFields)
{
    _journal.Record(MakeEv(5, 1000, ttd::TTDExternalEventKind::TapeControl, "Tape play"));
    EXPECT_EQ(_journal.Size(), 1u);
    EXPECT_FALSE(_journal.IsEmpty());

    const auto& events = _journal.Events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].time.frame,    5u);
    EXPECT_EQ(events[0].time.tInFrame, 1000u);
    EXPECT_EQ(events[0].kind, ttd::TTDExternalEventKind::TapeControl);
    EXPECT_STREQ(events[0].reason, "Tape play");
}

TEST_F(TTD_ExternalEvents_Test, Record_MultipleEvents_AppendedInOrder)
{
    _journal.Record(MakeEv(1, 0,    ttd::TTDExternalEventKind::TapeControl,  "play"));
    _journal.Record(MakeEv(1, 100,  ttd::TTDExternalEventKind::DiskWrite,    "wd1793 cmd F8"));
    _journal.Record(MakeEv(2, 0,    ttd::TTDExternalEventKind::DebuggerEdit, "set PC=0x8000"));
    _journal.Record(MakeEv(5, 5000, ttd::TTDExternalEventKind::Other,        "custom"));

    EXPECT_EQ(_journal.Size(), 4u);
    EXPECT_EQ(_journal.Events()[0].time.frame, 1u);
    EXPECT_EQ(_journal.Events()[3].time.frame, 5u);
    EXPECT_EQ(_journal.Events()[1].kind, ttd::TTDExternalEventKind::DiskWrite);
}

TEST_F(TTD_ExternalEvents_Test, Record_NullReason_StoredAsEmptyString)
{
    // The journal layer itself just stores what it's given. TimeTravelManager
    // is the one that nulls out reasons, but the journal Record() must still
    // cope with an empty reason buffer.
    auto ev = MakeEv(1, 0, ttd::TTDExternalEventKind::Other, nullptr);
    ev.reason[0] = '\0';
    _journal.Record(ev);
    EXPECT_STREQ(_journal.Events()[0].reason, "");
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Test, Clear_ResetsToEmpty)
{
    _journal.Record(MakeEv(1, 0, ttd::TTDExternalEventKind::TapeControl, "a"));
    _journal.Record(MakeEv(2, 0, ttd::TTDExternalEventKind::DiskWrite,   "b"));
    ASSERT_EQ(_journal.Size(), 2u);

    _journal.Clear();
    EXPECT_TRUE(_journal.IsEmpty());
    EXPECT_EQ(_journal.Size(), 0u);
    EXPECT_TRUE(_journal.Events().empty());
}

// ---------------------------------------------------------------------------
// FirstMarkerInInterval — the predicate is "strictly inside (from, to]"
// (TDD §5.1). Markers at `from` are NOT crossing; markers at `to` ARE.
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Test, FirstMarkerInInterval_EmptyJournal_ReturnsNull)
{
    EXPECT_EQ(_journal.FirstMarkerInInterval({0, 0}, {10, 0}), nullptr);
}

TEST_F(TTD_ExternalEvents_Test, FirstMarkerInInterval_NoMarkerInInterval_ReturnsNull)
{
    _journal.Record(MakeEv(1, 0,    ttd::TTDExternalEventKind::TapeControl, "x"));
    _journal.Record(MakeEv(20, 0,   ttd::TTDExternalEventKind::DiskWrite,   "y"));
    // Interval (5, 0)..(10, 0) is empty of markers.
    EXPECT_EQ(_journal.FirstMarkerInInterval({5, 0}, {10, 0}), nullptr);
}

TEST_F(TTD_ExternalEvents_Test, FirstMarkerInInterval_MarkerInMiddle_ReturnsIt)
{
    _journal.Record(MakeEv(1, 0,   ttd::TTDExternalEventKind::TapeControl, "before"));
    _journal.Record(MakeEv(5, 100, ttd::TTDExternalEventKind::DiskWrite,   "inside"));
    _journal.Record(MakeEv(10, 0,  ttd::TTDExternalEventKind::Other,       "after"));

    const auto* m = _journal.FirstMarkerInInterval({2, 0}, {8, 0});
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->time.frame,    5u);
    EXPECT_EQ(m->time.tInFrame, 100u);
    EXPECT_EQ(m->kind, ttd::TTDExternalEventKind::DiskWrite);
}

TEST_F(TTD_ExternalEvents_Test, FirstMarkerInInterval_MarkerAtFrom_NotIncluded)
{
    // Strict-inside predicate: from < m.time. Marker exactly at `from` is
    // considered "already happened at the restore point" — replay picks up
    // its effect from the restored checkpoint.
    _journal.Record(MakeEv(5, 0, ttd::TTDExternalEventKind::TapeControl, "at-from"));

    EXPECT_EQ(_journal.FirstMarkerInInterval({5, 0}, {10, 0}), nullptr);
}

TEST_F(TTD_ExternalEvents_Test, FirstMarkerInInterval_MarkerAtTo_Included)
{
    // Closed upper bound: m.time <= to. A marker exactly at `to` is in the
    // interval — replay would have to cross it to land at `to`, which is
    // exactly the case the barrier is supposed to catch.
    _journal.Record(MakeEv(5, 1000, ttd::TTDExternalEventKind::DiskWrite, "at-to"));

    const auto* m = _journal.FirstMarkerInInterval({5, 0}, {5, 1000});
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->time.tInFrame, 1000u);
}

TEST_F(TTD_ExternalEvents_Test, FirstMarkerInInterval_MultipleMatches_ReturnsFirst)
{
    // Linear scan returns the first match in storage order. Since the
    // journal is append-ordered by ascending time, "first stored" == earliest.
    _journal.Record(MakeEv(3, 0,   ttd::TTDExternalEventKind::TapeControl,  "first"));
    _journal.Record(MakeEv(4, 0,   ttd::TTDExternalEventKind::DiskWrite,    "second"));
    _journal.Record(MakeEv(5, 0,   ttd::TTDExternalEventKind::DebuggerEdit, "third"));

    const auto* m = _journal.FirstMarkerInInterval({2, 0}, {10, 0});
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->time.frame, 3u);
    EXPECT_STREQ(m->reason, "first");
}

TEST_F(TTD_ExternalEvents_Test, FirstMarkerInInterval_SameFrameDifferentTInFrame)
{
    // Lexical ordering: (5, 100) < (5, 200) < (5, 300). A marker at (5, 200)
    // is inside the interval (5, 100)..(5, 250); markers at (5, 50) and
    // (5, 400) are outside.
    _journal.Record(MakeEv(5, 50,  ttd::TTDExternalEventKind::Other, "low"));
    _journal.Record(MakeEv(5, 200, ttd::TTDExternalEventKind::Other, "mid"));
    _journal.Record(MakeEv(5, 400, ttd::TTDExternalEventKind::Other, "high"));

    const auto* m = _journal.FirstMarkerInInterval({5, 100}, {5, 250});
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->time.tInFrame, 200u);
    EXPECT_STREQ(m->reason, "mid");
}

// ---------------------------------------------------------------------------
// DropAfter (Resume-from-past truncation hook)
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Test, DropAfter_RemovesEventsStrictlyAfterThreshold)
{
    _journal.Record(MakeEv(1, 0, ttd::TTDExternalEventKind::TapeControl, "a"));
    _journal.Record(MakeEv(2, 0, ttd::TTDExternalEventKind::DiskWrite,   "b"));
    _journal.Record(MakeEv(3, 0, ttd::TTDExternalEventKind::Other,       "c"));
    _journal.Record(MakeEv(4, 0, ttd::TTDExternalEventKind::Other,       "d"));

    _journal.DropAfter({2, 0});

    EXPECT_EQ(_journal.Size(), 2u);
    EXPECT_EQ(_journal.Events()[0].time.frame, 1u);
    EXPECT_EQ(_journal.Events()[1].time.frame, 2u);
}

TEST_F(TTD_ExternalEvents_Test, DropAfter_KeepsEventsAtExactThreshold)
{
    // Strict-greater rule: events at the threshold are part of the past.
    _journal.Record(MakeEv(1, 100, ttd::TTDExternalEventKind::Other, "a"));
    _journal.Record(MakeEv(1, 200, ttd::TTDExternalEventKind::Other, "b"));
    _journal.Record(MakeEv(1, 200, ttd::TTDExternalEventKind::Other, "c"));  // dup time
    _journal.Record(MakeEv(1, 300, ttd::TTDExternalEventKind::Other, "d"));

    _journal.DropAfter({1, 200});

    EXPECT_EQ(_journal.Size(), 3u);  // (1,100), (1,200), (1,200)
    EXPECT_STREQ(_journal.Events()[2].reason, "c");
}

TEST_F(TTD_ExternalEvents_Test, DropAfter_OnEmptyJournal_IsNoOp)
{
    _journal.DropAfter({100, 0});
    EXPECT_TRUE(_journal.IsEmpty());
}

TEST_F(TTD_ExternalEvents_Test, DropAfter_WithFutureThreshold_KeepsEverything)
{
    _journal.Record(MakeEv(1, 0, ttd::TTDExternalEventKind::Other, "a"));
    _journal.Record(MakeEv(2, 0, ttd::TTDExternalEventKind::Other, "b"));

    _journal.DropAfter({1000, 0});
    EXPECT_EQ(_journal.Size(), 2u);
}

TEST_F(TTD_ExternalEvents_Test, DropAfter_WithPastThreshold_ClearsAll)
{
    _journal.Record(MakeEv(5, 0, ttd::TTDExternalEventKind::Other, "a"));
    _journal.Record(MakeEv(6, 0, ttd::TTDExternalEventKind::Other, "b"));

    _journal.DropAfter({0, 0});
    EXPECT_TRUE(_journal.IsEmpty());
}

// ---------------------------------------------------------------------------
// TTDExternalEventKindToString — stable for automation (TDD §10.4)
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Test, KindToString_AllKindsStable)
{
    using K = ttd::TTDExternalEventKind;
    EXPECT_STREQ(ttd::TTDExternalEventKindToString(K::TapeControl),  "tape_control");
    EXPECT_STREQ(ttd::TTDExternalEventKindToString(K::DiskWrite),    "disk_write");
    EXPECT_STREQ(ttd::TTDExternalEventKindToString(K::DebuggerEdit), "debugger_edit");
    EXPECT_STREQ(ttd::TTDExternalEventKindToString(K::Other),        "other");
}

// ===========================================================================
// Suite 2 — capture integration via TimeTravelManager::RecordExternalEvent.
// ===========================================================================

class TTD_ExternalEvents_API_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _memory = _context->pMemory;
        ASSERT_NE(_memory, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);

        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
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

    void RunFrames(size_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }
};

TEST_F(TTD_ExternalEvents_API_Test, NotRecording_IsNoOp)
{
    // Idle state — no capture. Same defensive guard as RecordInputEvent.
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::TapeControl, "play");
    EXPECT_EQ(_ttd->GetExternalEvents().Size(), 0u);
}

TEST_F(TTD_ExternalEvents_API_Test, WhileRecording_CapturesMarker)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);

    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite, "wd1793 write");

    EXPECT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    const auto& ev = _ttd->GetExternalEvents().Events()[0];
    EXPECT_EQ(ev.kind, ttd::TTDExternalEventKind::DiskWrite);
    EXPECT_STREQ(ev.reason, "wd1793 write");
}

TEST_F(TTD_ExternalEvents_API_Test, Capture_CurrentTime_FromFrameCounterAndZ80T)
{
    // After RunFrames(2), frame_counter == 2 and z80.t is small (frame
    // boundary residual). After RunTStates(500), frame_counter still == 2
    // but z80.t has advanced. RunTStates may add interrupt-contention
    // cycles, so we read the actual z80.t rather than assuming == 500.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    _emulator->RunTStates(500, /*skipBreakpoints=*/true);

    Z80* z80 = _context->pCore->GetZ80();
    ASSERT_NE(z80, nullptr);
    const uint32_t actualT = z80->t;
    ASSERT_GT(actualT, 0u);

    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "mid-frame");

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    const auto& ev = _ttd->GetExternalEvents().Events()[0];
    EXPECT_EQ(ev.time.frame,    2u);
    EXPECT_EQ(ev.time.tInFrame, actualT);
}

TEST_F(TTD_ExternalEvents_API_Test, NullReason_StoredAsEmptyString)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::TapeControl, nullptr);

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    EXPECT_STREQ(_ttd->GetExternalEvents().Events()[0].reason, "");
}

TEST_F(TTD_ExternalEvents_API_Test, LongReason_TruncatedAt63Chars)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Build a 200-char reason; only the first 63 should land in the buffer
    // (the 64th byte is the NUL terminator).
    const std::string longReason(200, 'X');
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DebuggerEdit, longReason.c_str());

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    const auto& ev = _ttd->GetExternalEvents().Events()[0];
    EXPECT_EQ(std::strlen(ev.reason), 63u);
    EXPECT_EQ(ev.reason[63], '\0');
}

TEST_F(TTD_ExternalEvents_API_Test, StartRecording_ClearsJournal)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::TapeControl, "first");
    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);

    // StartRecording is idempotent while already Recording — we must stop
    // first to begin a fresh session that clears prior history.
    _ttd->StopRecording();
    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_TRUE(_ttd->GetExternalEvents().IsEmpty());
}

TEST_F(TTD_ExternalEvents_API_Test, InvalidateSession_ClearsJournal)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite, "x");
    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);

    _ttd->InvalidateSession("test");
    EXPECT_TRUE(_ttd->GetExternalEvents().IsEmpty());
}

TEST_F(TTD_ExternalEvents_API_Test, ResumeRecordingFrom_KeepsMarkerAtResumePoint)
{
    // Strict-greater truncation rule: markers AT the resume TTDTimePoint are
    // kept (they're part of the past). To land a marker at exactly (5, 0)
    // we force z80.t = 0 before capture — RunFrames leaves a small residual.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);  // → frame 5

    Z80* z80 = _context->pCore->GetZ80();
    ASSERT_NE(z80, nullptr);
    z80->t = 0;
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "at-resume");  // (5, 0)

    RunFrames(2);  // → frame 7
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "after-resume");  // (7, T>0)
    RunFrames(1);  // → frame 8 (so resume from (5,0) is in range)
    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 2u);

    ASSERT_TRUE(_ttd->ResumeRecordingFrom({5, 0}));
    EXPECT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    EXPECT_EQ(_ttd->GetExternalEvents().Events()[0].time.frame,    5u);
    EXPECT_EQ(_ttd->GetExternalEvents().Events()[0].time.tInFrame, 0u);
}

TEST_F(TTD_ExternalEvents_API_Test, ResumeRecordingFrom_DropsStrictlyFutureMarkers)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);

    _emulator->RunTStates(100, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "past");   // (5, 100)
    RunFrames(2);  // → frame 7

    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "future");  // (7, 0)
    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 2u);

    // Resume from (5, 0). Marker at (5, 100) is strictly after (5, 0) and
    // must be dropped; marker at (7, 0) is also dropped.
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({5, 0}));
    EXPECT_TRUE(_ttd->GetExternalEvents().IsEmpty());
}

// ===========================================================================
// Suite 3 — marker-blocks-seek behavior via SeekTo(target, TTDSeekResult*).
// ===========================================================================

class TTD_ExternalEvents_Seek_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _memory = _context->pMemory;
        ASSERT_NE(_memory, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);

        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
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

    void RunFrames(size_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }
};

// ---------------------------------------------------------------------------
// Baseline: no markers in the timeline
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Seek_Test, NoMarkers_SeekToFrameAligned_ReturnsTarget)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    SeekResult r;
    EXPECT_TRUE(_ttd->SeekTo({2, 0}, &r));
    EXPECT_TRUE(r.reached);
    EXPECT_EQ(r.arrivedAt.frame, 2u);
    EXPECT_EQ(r.arrivedAt.tInFrame, 0u);
    EXPECT_EQ(r.haltReason, SeekHaltReason::Target);
}

TEST_F(TTD_ExternalEvents_Seek_Test, NoMarkers_SeekToIntraFrame_ReturnsTarget)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    _ttd->StopRecording();

    SeekResult r;
    EXPECT_TRUE(_ttd->SeekTo({1, 500}, &r));
    EXPECT_TRUE(r.reached);
    EXPECT_EQ(r.arrivedAt.frame,    1u);
    EXPECT_EQ(r.arrivedAt.tInFrame, 500u);
    EXPECT_EQ(r.haltReason, SeekHaltReason::Target);
}

// ---------------------------------------------------------------------------
// Frame-aligned targets never trigger the barrier check — the chosen
// checkpoint already reflects markers at or before that frame boundary.
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Seek_Test, FrameAligned_MarkerAtSameFrame_NotABarrier)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    // Marker captured at frame 2 boundary (after RunFrames(2) the
    // emulator is at frame 2; RecordExternalEvent captures (2, 0)).
    RunFrames(0);  // no-op, just here for readability
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::TapeControl, "play");
    RunFrames(1);  // advance to frame 3 so the timeline extends past the marker

    SeekResult r;
    EXPECT_TRUE(_ttd->SeekTo({2, 0}, &r))
        << "Frame-aligned target must not be blocked by a marker at the same frame";
    EXPECT_EQ(r.haltReason, SeekHaltReason::Target);
}

// ---------------------------------------------------------------------------
// Intra-frame targets with a marker strictly inside (cp.time, target]
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Seek_Test, IntraFrame_MarkerInInterval_StopsAtMarker)
{
    // Build a timeline where:
    //   - Checkpoint exists at (1, 0)
    //   - Marker is at (1, M) where M > 0 — strictly inside (1, 0)..(1, target)
    //   - Target (1, M+k) is reachable in principle but the marker blocks
    //
    // RunTStates cycle counting isn't exact (interrupt contention adds a
    // few cycles), so we read the actual marker time from the journal and
    // pick a target strictly past it.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);                       // → frame 1 boundary; cp at (1, 0)
    _emulator->RunTStates(500, true);   // advance within frame 1
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite, "wd1793 write");
    RunFrames(2);                       // extend timeline so intra-frame seek is in range
    _ttd->StopRecording();

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    const uint32_t markerT = _ttd->GetExternalEvents().Events()[0].time.tInFrame;
    ASSERT_GT(markerT, 0u);
    const uint32_t target = markerT + 500;  // strictly past the marker

    SeekResult r;
    EXPECT_FALSE(_ttd->SeekTo({1, target}, &r))
        << "Marker at (1, " << markerT << ") must block seek to (1, " << target << ")";
    EXPECT_FALSE(r.reached);
    EXPECT_EQ(r.haltReason, SeekHaltReason::ExternalEvent);
    EXPECT_EQ(r.arrivedAt.frame,    1u);
    EXPECT_EQ(r.arrivedAt.tInFrame, markerT);
    EXPECT_EQ(r.blockingMarker.kind, ttd::TTDExternalEventKind::DiskWrite);
    EXPECT_STREQ(r.blockingMarker.reason, "wd1793 write");
}

TEST_F(TTD_ExternalEvents_Seek_Test, IntraFrame_MarkerAtRestorePoint_NotABarrier)
{
    // Marker at the restore checkpoint's TTDTimePoint (cp.time itself) is
    // NOT in the strict-inside interval — its effect is in the checkpoint.
    //
    // RunFrames leaves z80.t with a small residual (not exactly 0), so to
    // land a marker at exactly (1, 0) we force z80.t = 0 before capture.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);                       // → cp at (1, 0)

    Z80* z80 = _context->pCore->GetZ80();
    ASSERT_NE(z80, nullptr);
    z80->t = 0;
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "at-cp");  // (1, 0)

    RunFrames(2);                       // extend timeline
    _ttd->StopRecording();

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    EXPECT_EQ(_ttd->GetExternalEvents().Events()[0].time.tInFrame, 0u);

    SeekResult r;
    EXPECT_TRUE(_ttd->SeekTo({1, 500}, &r))
        << "Marker at cp.time must NOT block — its effect is in the checkpoint";
    EXPECT_EQ(r.haltReason, SeekHaltReason::Target);
    EXPECT_EQ(r.arrivedAt.tInFrame, 500u);
}

TEST_F(TTD_ExternalEvents_Seek_Test, IntraFrame_MarkerAtTarget_BlocksAtTarget)
{
    // Closed upper bound: m.time <= to. Marker exactly at target blocks.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _emulator->RunTStates(1000, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "at-target");
    RunFrames(2);
    _ttd->StopRecording();

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    const uint32_t markerT = _ttd->GetExternalEvents().Events()[0].time.tInFrame;
    ASSERT_GT(markerT, 0u);

    SeekResult r;
    EXPECT_FALSE(_ttd->SeekTo({1, markerT}, &r))
        << "Marker exactly at target must block (closed upper bound)";
    EXPECT_EQ(r.haltReason, SeekHaltReason::ExternalEvent);
    EXPECT_EQ(r.arrivedAt.tInFrame, markerT);
}

TEST_F(TTD_ExternalEvents_Seek_Test, IntraFrame_MarkerPastTarget_NotABarrier)
{
    // Marker at (1, 2000) is outside the interval (1, 0)..(1, 1000); replay
    // to (1, 1000) does not cross it.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _emulator->RunTStates(2000, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "past-target");
    RunFrames(2);
    _ttd->StopRecording();

    SeekResult r;
    EXPECT_TRUE(_ttd->SeekTo({1, 1000}, &r));
    EXPECT_EQ(r.haltReason, SeekHaltReason::Target);
    EXPECT_EQ(r.arrivedAt.tInFrame, 1000u);
}

TEST_F(TTD_ExternalEvents_Seek_Test, MultipleMarkers_StopsAtEarliest)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _emulator->RunTStates(200, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::TapeControl, "first");
    _emulator->RunTStates(300, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite,   "second");
    _emulator->RunTStates(500, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other,       "third");
    RunFrames(2);
    _ttd->StopRecording();

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 3u);
    const uint32_t firstT = _ttd->GetExternalEvents().Events()[0].time.tInFrame;
    ASSERT_GT(firstT, 0u);

    SeekResult r;
    EXPECT_FALSE(_ttd->SeekTo({1, firstT + 5000}, &r));
    EXPECT_EQ(r.arrivedAt.tInFrame, firstT);
    EXPECT_STREQ(r.blockingMarker.reason, "first");
}

// ---------------------------------------------------------------------------
// State transition + emulation state after a blocked seek
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Seek_Test, BlockedSeek_TransitionsToDetached)
{
    // Same contract as a successful SeekTo: state ends up Detached so the
    // user can inspect, single-step, or resume from the barrier point.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _emulator->RunTStates(500, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "block");
    RunFrames(2);
    _ttd->StopRecording();

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    const uint32_t markerT = _ttd->GetExternalEvents().Events()[0].time.tInFrame;
    ASSERT_GT(markerT, 0u);

    SeekResult r;
    _ttd->SeekTo({1, markerT + 500}, &r);
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);
}

TEST_F(TTD_ExternalEvents_Seek_Test, BlockedSeek_ReplayStopsAtMarkerTInFrame)
{
    // The emulator should not have advanced past the barrier. After the
    // blocked seek, z80.t must be near the marker's tInFrame and well below
    // the requested target.
    //
    // We don't assert z80.t == markerT exactly: ReplayWithinFrame calls
    // RunTStates, which can overshoot by a few cycles (interrupt
    // contention, instruction-boundary rounding). The behavioral guarantee
    // is "replay halted before reaching the target", not an exact cycle
    // count.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _emulator->RunTStates(500, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "block");
    RunFrames(2);
    _ttd->StopRecording();

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    const uint32_t markerT = _ttd->GetExternalEvents().Events()[0].time.tInFrame;
    ASSERT_GT(markerT, 0u);
    const uint32_t target = markerT + 500;

    SeekResult r;
    ASSERT_FALSE(_ttd->SeekTo({1, target}, &r));
    ASSERT_EQ(r.arrivedAt.tInFrame, markerT);

    Z80* z80 = _context->pCore->GetZ80();
    ASSERT_NE(z80, nullptr);
    EXPECT_GE(z80->t, markerT)
        << "Replay should have reached at least the marker's frame position";
    EXPECT_LT(z80->t, target)
        << "Replay must stop near the marker, not advance to the target";
}

// ---------------------------------------------------------------------------
// Out-of-range and idle failures default to OutOfRange
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Seek_Test, IdleState_OutResultDefaultsToOutOfRange)
{
    SeekResult r;
    EXPECT_FALSE(_ttd->SeekTo({0, 0}, &r));
    EXPECT_FALSE(r.reached);
    EXPECT_EQ(r.haltReason, SeekHaltReason::OutOfRange);
}

TEST_F(TTD_ExternalEvents_Seek_Test, TargetBeyondSessionEnd_HaltReasonOutOfRange)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);  // timeline ends at (2, 0)

    SeekResult r;
    EXPECT_FALSE(_ttd->SeekTo({99, 0}, &r));
    EXPECT_FALSE(r.reached);
    EXPECT_EQ(r.haltReason, SeekHaltReason::OutOfRange);
}

// ---------------------------------------------------------------------------
// Backward-compat wrapper (nullptr outResult) still behaves like the old API
// ---------------------------------------------------------------------------

TEST_F(TTD_ExternalEvents_Seek_Test, NullOutResult_MarkerBarrier_StillReturnsFalse)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _emulator->RunTStates(500, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "block");
    RunFrames(2);
    _ttd->StopRecording();

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    const uint32_t markerT = _ttd->GetExternalEvents().Events()[0].time.tInFrame;
    ASSERT_GT(markerT, 0u);

    // The inline wrapper passes nullptr for outResult. The bool return is
    // the only signal — callers that don't care about the barrier details
    // just see "couldn't reach target" and can fall back to frame-aligned.
    EXPECT_FALSE(_ttd->SeekTo({1, markerT + 500}));
}

TEST_F(TTD_ExternalEvents_Seek_Test, StepBackFrame_FrameAligned_NotAffectedByMarkers)
{
    // StepBackFrame composes SeekTo with (frame-1, tInFrame). The frame-
    // aligned seek (tInFrame=0 case) never hits the barrier check, so a
    // marker in the middle of a frame does not interfere with frame-aligned
    // navigation.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _emulator->RunTStates(500, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "mid-frame");
    RunFrames(3);  // → frame 4
    _ttd->StopRecording();

    EXPECT_TRUE(_ttd->StepBackFrame());
    EXPECT_EQ(_ttd->CurrentPosition().frame, 3u);
}
