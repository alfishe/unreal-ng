/// @file ttd_coverage_integration_test.cpp
/// @brief End-to-end guards for the coverage-index-accelerated reverse search.
///
/// The coverage index makes reverse search skip frames without replaying them.
/// That is a correctness-critical optimisation: a wrong "this frame cannot
/// match" silently deletes the answer a user was searching for, and the search
/// still returns successfully — just with the wrong result, or none.
///
/// So the central test here is differential. Every query is run twice, once
/// with pruning enabled and once with it disabled, and the two answers must be
/// identical. Any pruning bug — a bad key, a stale covered-range, a wrapped
/// address interval — shows up as a mismatch rather than as a plausible-looking
/// wrong answer.
///
/// Also covered, because they share the same machinery and all have to keep
/// working: seeking in both directions, reverse stepping, reverse-continue to a
/// PC, and sessions round-tripped through a file (which carry a timeline but no
/// coverage index, so pruning must switch itself off).

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "debugger/ttd/ttd_coverage_index.h"
#include "debugger/ttd/ttd_probe.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{

/// Comparable shape of a search result, so a mismatch prints something useful.
struct SearchAnswer
{
    bool     found = false;
    uint64_t frame = 0;
    uint32_t tInFrame = 0;
    uint16_t pc = 0;
    uint8_t  value = 0;
    uint8_t  physPage = 0;

    bool operator==(const SearchAnswer& o) const
    {
        return found == o.found && frame == o.frame && tInFrame == o.tInFrame &&
               pc == o.pc && value == o.value && physPage == o.physPage;
    }
};

std::ostream& operator<<(std::ostream& os, const SearchAnswer& a)
{
    if (!a.found)
        return os << "{no match}";
    return os << "{frame=" << a.frame << " tin=" << a.tInFrame
              << " pc=0x" << std::hex << a.pc << std::dec
              << " value=" << static_cast<int>(a.value)
              << " page=" << static_cast<int>(a.physPage) << "}";
}

class TTD_CoverageIntegration_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);

        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        _memory = _context->pMemory;
        ASSERT_NE(_ttd, nullptr);
        ASSERT_NE(_memory, nullptr);

        FeatureManager* fm = _emulator->GetFeatureManager();
        ASSERT_NE(fm, nullptr);
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    /// Record a session by actually running the machine, so the coverage index
    /// fills from real execution rather than from hand-fed keys.
    void RecordSession(unsigned frames)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        ASSERT_GT(_ttd->GetCheckpointCount(), 1u) << "session captured nothing";
    }

    SearchAnswer Run(const ttd::TTDSearchQuery& q)
    {
        SearchAnswer a;
        auto r = _ttd->FindLastAccess(q);
        if (r)
        {
            a.found = true;
            a.frame = r->time.frame;
            a.tInFrame = r->time.tInFrame;
            a.pc = r->pc;
            a.value = r->value;
            a.physPage = r->physPage;
        }
        return a;
    }

    /// Park the emulator at a fixed point so a search starts from the same
    /// place every time. FindLastAccess defaults its upper bound to the current
    /// position, and a previous search leaves the machine wherever its replay
    /// ended — without this, two runs of the "same" query are not the same
    /// query at all.
    void ParkAtSessionEnd()
    {
        // SessionEndPosition rather than "last checkpoint, t=0": the exact end
        // is where the interesting boundary cases live. A frame-aligned parking
        // spot hid a real defect once — coverage sealed one frame late still
        // produced correct answers from a frame boundary, because the shifted
        // set happened to contain the loop PC anyway.
        ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    }

    /// The differential check: pruning must be invisible in the results.
    ///
    /// Note the index is re-recorded between the two runs: disabling coverage
    /// clears it, so the "with" run needs a session that still has one.
    void ExpectPruningIsTransparent(const ttd::TTDSearchQuery& q, const char* what)
    {
        // With the index (recorded by the fixture).
        ParkAtSessionEnd();
        const SearchAnswer with = Run(q);

        // Without it.
        _ttd->SetEnableCoverageIndex(false);
        ParkAtSessionEnd();
        const SearchAnswer without = Run(q);
        _ttd->SetEnableCoverageIndex(true);

        EXPECT_EQ(with, without)
            << what << ": the coverage index changed the answer. "
            << "with-index " << with << " vs without-index " << without;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Differential: the index must never change an answer
// ---------------------------------------------------------------------------

TEST_F(TTD_CoverageIntegration_Test, ExecuteSearchIsUnchangedByPruning)
{
    RecordSession(30);

    // Sweep addresses across the ROM/RAM boundary and inside RAM, so the sweep
    // includes addresses that were executed, never executed, and executed only
    // in early frames.
    for (uint16_t addr : {uint16_t(0x0000), uint16_t(0x0038), uint16_t(0x1234),
                          uint16_t(0x4000), uint16_t(0x8000), uint16_t(0xC000),
                          uint16_t(0xFFFF)})
    {
        ttd::TTDSearchQuery q;
        q.addrFrom = q.addrTo = addr;
        q.access = ttd::TTDAccessType::Execute;
        ExpectPruningIsTransparent(q, "execute search");
    }
}

TEST_F(TTD_CoverageIntegration_Test, ReadSearchIsUnchangedByPruning)
{
    RecordSession(30);

    for (uint16_t addr : {uint16_t(0x4000), uint16_t(0x5800), uint16_t(0x8000),
                          uint16_t(0xC000), uint16_t(0xDEAD)})
    {
        ttd::TTDSearchQuery q;
        q.addrFrom = q.addrTo = addr;
        q.access = ttd::TTDAccessType::Read;
        ExpectPruningIsTransparent(q, "read search");
    }
}

/// Ranges are where an over-eager offset mapping would go wrong: a span that
/// crosses a 16 KB boundary has offsets in two disjoint intervals.
TEST_F(TTD_CoverageIntegration_Test, RangeQueriesAreUnchangedByPruning)
{
    RecordSession(30);

    const std::vector<std::pair<uint16_t, uint16_t>> ranges = {
        {0x4000, 0x4010},  // narrow, inside one page
        {0x3FF0, 0x4010},  // crosses a page boundary — offsets wrap
        {0x0000, 0xFFFF},  // whole address space
        {0xC000, 0xFFFF},  // exactly one page
        {0x8000, 0xC000},  // spans a boundary by one byte
    };

    for (const auto& [lo, hi] : ranges)
    {
        ttd::TTDSearchQuery q;
        q.addrFrom = lo;
        q.addrTo = hi;
        q.access = ttd::TTDAccessType::Execute;
        ExpectPruningIsTransparent(q, "range execute search");

        q.access = ttd::TTDAccessType::Read;
        ExpectPruningIsTransparent(q, "range read search");
    }
}

TEST_F(TTD_CoverageIntegration_Test, PageFilteredSearchIsUnchangedByPruning)
{
    RecordSession(30);

    for (uint8_t page : {uint8_t(0), uint8_t(2), uint8_t(5), uint8_t(7)})
    {
        ttd::TTDSearchQuery q;
        q.addrFrom = q.addrTo = 0xC000;
        q.access = ttd::TTDAccessType::Read;
        q.hasPhysPageFilter = true;
        q.physPage = page;
        ExpectPruningIsTransparent(q, "page-filtered read search");
    }
}

TEST_F(TTD_CoverageIntegration_Test, BoundedSearchIsUnchangedByPruning)
{
    RecordSession(30);

    const uint64_t lastFrame = _ttd->GetCheckpoint(_ttd->GetCheckpointCount() - 1)->time.frame;
    const uint32_t frameT = _context->config.frame;

    // Same query, cut off at several points in history.
    for (uint64_t f : {lastFrame / 4, lastFrame / 2, lastFrame})
    {
        ttd::TTDSearchQuery q;
        q.addrFrom = q.addrTo = 0x4000;
        q.access = ttd::TTDAccessType::Read;
        q.beforeGlobalT = f * frameT;
        ExpectPruningIsTransparent(q, "bounded read search");
    }
}

// ---------------------------------------------------------------------------
// Sessions loaded from disk carry no coverage index
// ---------------------------------------------------------------------------

/// The coverage index travels with the session. Without it a loaded recording
/// searches correctly but slowly — reverse queries fall back to replaying
/// frames, which costs ~32 s of rebuild per ten minutes of history if done
/// eagerly, or 200x slower queries if not done at all.
TEST_F(TTD_CoverageIntegration_Test, LoadedSessionStillSearchesCorrectly)
{
    RecordSession(20);

    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = 0x4000;
    q.access = ttd::TTDAccessType::Read;

    ParkAtSessionEnd();
    const SearchAnswer beforeRoundTrip = Run(q);

    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;

    std::istringstream in(out.str(), std::ios::binary);
    ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;

    // The index came back with the session.
    EXPECT_GT(_ttd->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed), 0u)
        << "the coverage index did not survive the round trip";

    uint64_t first = 0, last = 0;
    EXPECT_TRUE(_ttd->GetCoverageIndex().CoveredRange(
        ttd::TTDCoverageKind::Executed, first, last))
        << "loaded index reports no covered range";

    ParkAtSessionEnd();
    const SearchAnswer afterRoundTrip = Run(q);
    EXPECT_EQ(afterRoundTrip, beforeRoundTrip)
        << "search over a loaded session disagreed with the live one: "
        << afterRoundTrip << " vs " << beforeRoundTrip;
}

/// Frames outside the index's observed range must never be pruned.
TEST_F(TTD_CoverageIntegration_Test, FramesOutsideTheCoveredRangeAreNotPruned)
{
    const ttd::TTDCoverageIndex& index = _ttd->GetCoverageIndex();

    // Nothing recorded yet: no frame is covered, so nothing may be pruned.
    EXPECT_FALSE(index.CoversFrame(ttd::TTDCoverageKind::Executed, 0));
    EXPECT_TRUE(index.FrameMayContain(ttd::TTDCoverageKind::Executed, 0,
                                      0, 0x3FFF, false, 0))
        << "an unwatched frame was reported as provably empty";

    RecordSession(10);

    const uint64_t last = _ttd->GetCheckpoint(_ttd->GetCheckpointCount() - 1)->time.frame;
    EXPECT_TRUE(index.FrameMayContain(ttd::TTDCoverageKind::Executed, last + 1000,
                                      0, 0x3FFF, false, 0))
        << "a frame past the end of the index was reported as provably empty";
}

// ---------------------------------------------------------------------------
// Seeking, stepping and reverse breakpoints keep working
// ---------------------------------------------------------------------------

TEST_F(TTD_CoverageIntegration_Test, SeekJumpsBothDirectionsLandWhereAsked)
{
    RecordSession(40);

    const size_t count = _ttd->GetCheckpointCount();
    ASSERT_GT(count, 8u);

    const uint64_t firstFrame = _ttd->GetCheckpoint(0)->time.frame;
    const uint64_t lastFrame  = _ttd->GetCheckpoint(count - 1)->time.frame;
    const uint64_t midFrame   = _ttd->GetCheckpoint(count / 2)->time.frame;

    // Deliberately bounce around rather than walking monotonically: a seek that
    // only works when moving one way is a real failure mode.
    const std::vector<uint64_t> itinerary = {
        midFrame, firstFrame, lastFrame, midFrame, firstFrame, lastFrame, midFrame};

    for (uint64_t target : itinerary)
    {
        ttd::TTDTimePoint tp;
        tp.frame = target;
        tp.tInFrame = 0;

        ASSERT_TRUE(_ttd->SeekTo(tp)) << "seek to frame " << target << " failed";
        EXPECT_EQ(_ttd->CurrentPosition().frame, target)
            << "seek landed on the wrong frame";
    }
}

/// Seeking to the same point twice must produce the same machine, or replay is
/// not deterministic and every reverse operation built on it is unsound.
TEST_F(TTD_CoverageIntegration_Test, RepeatedSeekToTheSamePointIsDeterministic)
{
    RecordSession(30);

    const size_t count = _ttd->GetCheckpointCount();
    const uint64_t target = _ttd->GetCheckpoint(count / 2)->time.frame;
    const uint64_t other  = _ttd->GetCheckpoint(count - 1)->time.frame;

    ttd::TTDTimePoint tp;
    tp.frame = target;
    tp.tInFrame = 0;

    ASSERT_TRUE(_ttd->SeekTo(tp));
    Z80* z80 = _context->pCore->GetZ80();
    const uint16_t pc1 = z80->pc;
    const uint16_t sp1 = z80->sp;
    const uint16_t af1 = z80->af;

    // Go somewhere else, then come back.
    ttd::TTDTimePoint away;
    away.frame = other;
    away.tInFrame = 0;
    ASSERT_TRUE(_ttd->SeekTo(away));
    ASSERT_TRUE(_ttd->SeekTo(tp));

    EXPECT_EQ(z80->pc, pc1) << "PC differed after returning to the same point";
    EXPECT_EQ(z80->sp, sp1) << "SP differed after returning to the same point";
    EXPECT_EQ(z80->af, af1) << "AF differed after returning to the same point";
}

TEST_F(TTD_CoverageIntegration_Test, ReverseStepThenForwardStepReturnsToStart)
{
    RecordSession(20);

    const uint64_t target = _ttd->GetCheckpoint(_ttd->GetCheckpointCount() / 2)->time.frame;
    ttd::TTDTimePoint tp;
    tp.frame = target;
    tp.tInFrame = 100;
    ASSERT_TRUE(_ttd->SeekTo(tp));

    // Compare positions on the timeline, not PCs: ROM contains self-looping
    // instructions where the previous instruction sits at the same address, so
    // an unchanged PC does not mean the step failed.
    const ttd::TTDTimePoint startPos = _ttd->CurrentPosition();

    ASSERT_TRUE(_ttd->StepBackInstruction());
    const ttd::TTDTimePoint backPos = _ttd->CurrentPosition();
    EXPECT_TRUE(backPos < startPos)
        << "stepping back did not move earlier in time: frame " << backPos.frame
        << "/t" << backPos.tInFrame << " vs " << startPos.frame << "/t" << startPos.tInFrame;

    ASSERT_TRUE(_ttd->StepForwardInstruction());
    const ttd::TTDTimePoint forwardPos = _ttd->CurrentPosition();
    EXPECT_EQ(forwardPos.frame, startPos.frame)
        << "back-then-forward landed in a different frame";
    EXPECT_EQ(forwardPos.tInFrame, startPos.tInFrame)
        << "back-then-forward did not return to the starting position";
}

/// Reverse-continue to a PC that definitely executed must find it, and must
/// land at that PC.
TEST_F(TTD_CoverageIntegration_Test, ReverseContinueFindsAnExecutedPc)
{
    RecordSession(30);

    // Take a PC the machine really executed: seek somewhere mid-session and
    // read the live PC.
    const uint64_t probeFrame = _ttd->GetCheckpoint(_ttd->GetCheckpointCount() / 2)->time.frame;
    ttd::TTDTimePoint tp;
    tp.frame = probeFrame;
    tp.tInFrame = 0;
    ASSERT_TRUE(_ttd->SeekTo(tp));

    Z80* z80 = _context->pCore->GetZ80();
    const uint16_t knownPc = z80->pc;

    // Move forward, then run backwards looking for it.
    ttd::TTDTimePoint later;
    later.frame = _ttd->GetCheckpoint(_ttd->GetCheckpointCount() - 1)->time.frame;
    later.tInFrame = 0;
    ASSERT_TRUE(_ttd->SeekTo(later));

    auto result = _ttd->ReverseContinue({knownPc});
    EXPECT_TRUE(result.matched)
        << "reverse-continue did not find PC 0x" << std::hex << knownPc
        << " even though the machine executed it";
    if (result.matched)
        EXPECT_EQ(result.pc, knownPc) << "landed on a different PC than requested";
}

/// A PC that was never executed must report no match rather than stopping
/// somewhere arbitrary.
TEST_F(TTD_CoverageIntegration_Test, ReverseContinueReportsNoMatchForUnexecutedPc)
{
    RecordSession(20);

    ttd::TTDTimePoint tp;
    tp.frame = _ttd->GetCheckpoint(_ttd->GetCheckpointCount() - 1)->time.frame;
    tp.tInFrame = 0;
    ASSERT_TRUE(_ttd->SeekTo(tp));

    // 0xFFFF is the last byte of the address space; the ROM/demo does not
    // execute there. If this ever becomes flaky the workload changed, not the
    // engine.
    auto result = _ttd->ReverseContinue({uint16_t(0xFFFF)});
    EXPECT_FALSE(result.matched)
        << "reverse-continue claimed to find a PC that never executed";
}

// ---------------------------------------------------------------------------
// The index has to actually be doing something
// ---------------------------------------------------------------------------

/// A guard against the whole feature silently disabling itself: after running a
/// real workload the index must hold coverage for the frames that were
/// recorded.
TEST_F(TTD_CoverageIntegration_Test, RecordingPopulatesTheIndex)
{
    RecordSession(20);

    const ttd::TTDCoverageIndex& index = _ttd->GetCoverageIndex();

    EXPECT_GT(index.SealedFrameCount(ttd::TTDCoverageKind::Executed), 0u)
        << "no execution coverage was collected — the M1 hook is not firing";
    EXPECT_GT(index.EncodedBytes(ttd::TTDCoverageKind::Executed), 0u);

    // Written coverage comes from a different hook; both have to work.
    EXPECT_GT(index.SealedFrameCount(ttd::TTDCoverageKind::Written), 0u)
        << "no write coverage was collected — the write hook is not firing";
}

/// Coverage must not accumulate across sessions: stale frames would prune the
/// wrong things in the next recording.
TEST_F(TTD_CoverageIntegration_Test, InvalidatingSessionClearsTheIndex)
{
    RecordSession(10);
    ASSERT_GT(_ttd->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed), 0u);

    _ttd->InvalidateSession("test");

    EXPECT_EQ(_ttd->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed), 0u)
        << "coverage survived session invalidation";
    EXPECT_FALSE(_ttd->GetCoverageIndex().CoversFrame(ttd::TTDCoverageKind::Executed, 0))
        << "stale covered-range survived session invalidation";
}

// ---------------------------------------------------------------------------
// Reverse breakpoints
// ---------------------------------------------------------------------------
//
// ReverseContinue consults the coverage index to pick candidate frames instead
// of replaying the whole session. The same differential rule applies as for
// searches: the index may only change how long the answer takes, never what it
// is.
//
// This is not hypothetical. Wiring the index in initially reported "no match"
// for a PC the machine had plainly executed, because coverage was sealed under
// the wrong frame number — OnFrameBoundary runs after the frame counter has
// advanced, so every set landed one frame in the future. Nothing else in the
// suite noticed.

TEST_F(TTD_CoverageIntegration_Test, ReverseContinueIsUnchangedByTheIndex)
{
    RecordSession(30);

    // Collect PCs the machine really executed, sampled across the session so
    // the set spans early, middle and late frames.
    std::vector<uint16_t> executedPcs;
    for (size_t i = 1; i < _ttd->GetCheckpointCount(); i += _ttd->GetCheckpointCount() / 5 + 1)
    {
        ttd::TTDTimePoint tp;
        tp.frame = _ttd->GetCheckpoint(i)->time.frame;
        tp.tInFrame = 0;
        ASSERT_TRUE(_ttd->SeekTo(tp));
        executedPcs.push_back(_context->pCore->GetZ80()->pc);
    }
    ASSERT_FALSE(executedPcs.empty());

    // The PC at the session's exact end. This is the case that catches a
    // coverage set sealed under the wrong frame number: the match must come
    // from strictly before the current position, so an off-by-one in the frame
    // labelling sends the search to a frame that cannot contain it.
    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    executedPcs.push_back(_context->pCore->GetZ80()->pc);

    // Plus one that certainly never ran, so the "no match" path is compared too.
    executedPcs.push_back(0xFFFF);

    for (uint16_t pc : executedPcs)
    {
        _ttd->SetEnableCoverageIndex(true);
        ParkAtSessionEnd();
        const auto withIndex = _ttd->ReverseContinue({pc});

        _ttd->SetEnableCoverageIndex(false);
        ParkAtSessionEnd();
        const auto withoutIndex = _ttd->ReverseContinue({pc});
        _ttd->SetEnableCoverageIndex(true);

        EXPECT_EQ(withIndex.matched, withoutIndex.matched)
            << "PC 0x" << std::hex << pc
            << ": indexed reverse-continue disagreed on whether there is a match";

        if (withIndex.matched && withoutIndex.matched)
        {
            EXPECT_EQ(withIndex.pc, withoutIndex.pc) << "different PC reported";
            EXPECT_EQ(withIndex.arrivedAt.frame, withoutIndex.arrivedAt.frame)
                << "PC 0x" << std::hex << pc << ": landed in a different frame ("
                << std::dec << withIndex.arrivedAt.frame << " vs "
                << withoutIndex.arrivedAt.frame << ")";
            EXPECT_EQ(withIndex.arrivedAt.tInFrame, withoutIndex.arrivedAt.tInFrame)
                << "landed at a different t-state within the frame";
        }
    }
}

/// A breakpoint set large enough that the index stops pruning must still give
/// the same answer — the degenerate case has its own code path through the
/// per-frame loop and is easy to get wrong.
TEST_F(TTD_CoverageIntegration_Test, ReverseContinueWithManyBreakpointsAgrees)
{
    RecordSession(20);

    ttd::TTDTimePoint tp;
    tp.frame = _ttd->GetCheckpoint(_ttd->GetCheckpointCount() / 2)->time.frame;
    tp.tInFrame = 0;
    ASSERT_TRUE(_ttd->SeekTo(tp));
    const uint16_t realPc = _context->pCore->GetZ80()->pc;

    // One PC that executed, buried in a crowd that did not.
    std::vector<uint16_t> bps;
    bps.reserve(64);
    for (uint16_t i = 0; i < 63; ++i)
        bps.push_back(static_cast<uint16_t>(0xE000 + i * 7));
    bps.push_back(realPc);

    _ttd->SetEnableCoverageIndex(true);
    ParkAtSessionEnd();
    const auto withIndex = _ttd->ReverseContinue(bps);

    _ttd->SetEnableCoverageIndex(false);
    ParkAtSessionEnd();
    const auto withoutIndex = _ttd->ReverseContinue(bps);
    _ttd->SetEnableCoverageIndex(true);

    EXPECT_EQ(withIndex.matched, withoutIndex.matched);
    if (withIndex.matched && withoutIndex.matched)
    {
        EXPECT_EQ(withIndex.pc, withoutIndex.pc);
        EXPECT_EQ(withIndex.arrivedAt.frame, withoutIndex.arrivedAt.frame);
        EXPECT_EQ(withIndex.arrivedAt.tInFrame, withoutIndex.arrivedAt.tInFrame);
    }
}

/// The session's opening frames are not covered by the index: coverage is
/// sealed at frame boundaries, so the first recorded frame has no entry. Those
/// frames must be replayed rather than assumed empty.
TEST_F(TTD_CoverageIntegration_Test, HitsInTheUnindexedOpeningFramesAreFound)
{
    RecordSession(15);

    uint64_t first = 0, last = 0;
    ASSERT_TRUE(_ttd->GetCoverageIndex().CoveredRange(
        ttd::TTDCoverageKind::Executed, first, last));

    const uint64_t sessionStart = _ttd->GetCheckpoint(0)->time.frame;
    EXPECT_GE(first, sessionStart)
        << "test premise: the index is expected to start no earlier than the session";

    // Take a PC from the very first frame, which sits in the unindexed prefix
    // whenever first > sessionStart.
    ttd::TTDTimePoint tp;
    tp.frame = sessionStart;
    tp.tInFrame = 0;
    ASSERT_TRUE(_ttd->SeekTo(tp));
    const uint16_t earlyPc = _context->pCore->GetZ80()->pc;

    _ttd->SetEnableCoverageIndex(true);
    ParkAtSessionEnd();
    const auto withIndex = _ttd->ReverseContinue({earlyPc});

    _ttd->SetEnableCoverageIndex(false);
    ParkAtSessionEnd();
    const auto withoutIndex = _ttd->ReverseContinue({earlyPc});
    _ttd->SetEnableCoverageIndex(true);

    EXPECT_EQ(withIndex.matched, withoutIndex.matched)
        << "a hit in the unindexed opening frames was lost";
    if (withIndex.matched && withoutIndex.matched)
        EXPECT_EQ(withIndex.arrivedAt.frame, withoutIndex.arrivedAt.frame);
}

/// The frame numbering invariant, asserted directly rather than through a
/// symptom.
///
/// If the machine executed PC at frame F, then the index must report that frame
/// F may contain it. Sealing coverage under the wrong frame number breaks
/// exactly this, and it is worth testing on its own because the end-to-end
/// symptom is workload-dependent: a PC that executes in every frame still finds
/// a candidate under a one-frame shift and returns the right answer by luck.
TEST_F(TTD_CoverageIntegration_Test, CoverageFrameLabelsMatchExecutionFrames)
{
    RecordSession(20);

    const ttd::TTDCoverageIndex& index = _ttd->GetCoverageIndex();
    uint64_t coverFirst = 0, coverLast = 0;
    ASSERT_TRUE(index.CoveredRange(ttd::TTDCoverageKind::Executed, coverFirst, coverLast));

    // Ground truth from the unindexed path: where did this PC actually execute?
    _ttd->SetEnableCoverageIndex(false);

    size_t checked = 0;
    size_t disagreements = 0;
    for (size_t i = 1; i < _ttd->GetCheckpointCount(); i += 3)
    {
        ttd::TTDTimePoint probe;
        probe.frame = _ttd->GetCheckpoint(i)->time.frame;
        probe.tInFrame = 0;
        ASSERT_TRUE(_ttd->SeekTo(probe));
        const uint16_t pc = _context->pCore->GetZ80()->pc;

        ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
        const auto truth = _ttd->ReverseContinue({pc});
        if (!truth.matched)
            continue;

        const uint64_t execFrame = truth.arrivedAt.frame;
        if (execFrame < coverFirst || execFrame > coverLast)
            continue;  // Outside the indexed range: nothing to assert.

        const uint16_t offset = static_cast<uint16_t>(pc & 0x3FFF);
        ++checked;
        if (!index.FrameMayContain(ttd::TTDCoverageKind::Executed, execFrame,
                                   offset, offset, /*hasPage=*/false, 0))
        {
            ++disagreements;
            ADD_FAILURE()
                << "PC 0x" << std::hex << pc << std::dec
                << " executed at frame " << execFrame
                << " but the coverage index says that frame cannot contain it — "
                << "frame labelling is off (covered range " << coverFirst
                << ".." << coverLast << ")";
        }
    }

    _ttd->SetEnableCoverageIndex(true);

    ASSERT_GT(checked, 0u) << "no executions landed inside the indexed range; "
                           << "the test proved nothing";
    EXPECT_EQ(disagreements, 0u);
}

/// The same invariant stated where it is unambiguous: at the range boundaries.
///
/// Coverage is sealed once per frame boundary for the frame that just ended, so
/// the first sealed label must be the session's first frame. Sealing under the
/// post-increment counter instead shifts the whole range forward by one, which
/// is invisible in most end-to-end checks — a PC from a loop executes in
/// neighbouring frames too, so a shifted set still contains it and the search
/// returns the right answer by luck. Comparing the range against the timeline
/// removes that luck.
TEST_F(TTD_CoverageIntegration_Test, CoveredRangeAlignsWithTheTimeline)
{
    RecordSession(20);

    uint64_t coverFirst = 0, coverLast = 0;
    ASSERT_TRUE(_ttd->GetCoverageIndex().CoveredRange(
        ttd::TTDCoverageKind::Executed, coverFirst, coverLast));

    const uint64_t firstCheckpointFrame = _ttd->GetCheckpoint(0)->time.frame;
    const uint64_t lastCheckpointFrame =
        _ttd->GetCheckpoint(_ttd->GetCheckpointCount() - 1)->time.frame;

    EXPECT_EQ(coverFirst, firstCheckpointFrame)
        << "coverage starts at frame " << coverFirst << " but the session starts at "
        << firstCheckpointFrame
        << " — frame labels are shifted, so every set describes the wrong frame";

    // The final boundary seals the frame before it, so coverage ends one frame
    // short of the last checkpoint.
    EXPECT_LT(coverLast, lastCheckpointFrame + 1)
        << "coverage claims frames the session never reached";
    EXPECT_GE(coverLast + 1, lastCheckpointFrame)
        << "coverage stops more than one frame short of the session end";
}

/// A file written without a coverage section must still load and search — the
/// index is derived data, and its absence may not turn into wrong answers.
TEST_F(TTD_CoverageIntegration_Test, SessionWithoutCoverageSectionStillSearches)
{
    RecordSession(20);

    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = 0x4000;
    q.access = ttd::TTDAccessType::Read;

    ParkAtSessionEnd();
    const SearchAnswer expected = Run(q);

    // Disabling coverage clears the index, so the session serializes without
    // the section — the shape of a file produced by a build or configuration
    // that never collected one.
    _ttd->SetEnableCoverageIndex(false);

    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;

    std::istringstream in(out.str(), std::ios::binary);
    ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;

    EXPECT_EQ(_ttd->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed), 0u)
        << "test premise: this file should carry no coverage section";

    ParkAtSessionEnd();
    const SearchAnswer got = Run(q);
    _ttd->SetEnableCoverageIndex(true);

    EXPECT_EQ(got, expected)
        << "a session without a coverage index returned a different answer: "
        << got << " vs " << expected;
}

/// The persisted index must describe the same frames it did before the round
/// trip. A range that drifts would authorise pruning frames the index never
/// observed.
TEST_F(TTD_CoverageIntegration_Test, PersistedCoverageRangeSurvivesRoundTrip)
{
    RecordSession(25);

    uint64_t firstBefore = 0, lastBefore = 0;
    ASSERT_TRUE(_ttd->GetCoverageIndex().CoveredRange(
        ttd::TTDCoverageKind::Executed, firstBefore, lastBefore));
    const size_t framesBefore =
        _ttd->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed);

    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;

    std::istringstream in(out.str(), std::ios::binary);
    ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;

    uint64_t firstAfter = 0, lastAfter = 0;
    ASSERT_TRUE(_ttd->GetCoverageIndex().CoveredRange(
        ttd::TTDCoverageKind::Executed, firstAfter, lastAfter));

    EXPECT_EQ(firstAfter, firstBefore) << "covered range start moved across the round trip";
    EXPECT_EQ(lastAfter, lastBefore) << "covered range end moved across the round trip";
    EXPECT_EQ(_ttd->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed),
              framesBefore)
        << "frame count changed across the round trip";
}
