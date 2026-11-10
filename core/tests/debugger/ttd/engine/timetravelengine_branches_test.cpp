/// @file timetravelengine_branches_test.cpp
/// @brief Branch lines in the engine (docs/inprogress/2026-11-08-ttd-branches/architecture.md §4): a fork keeps
/// the old future on its line, the new line continues like a truncated session, a switch back continues the old
/// one, versions are shared and released exactly. Cases T-E1...T-E5 of test-strategy.md §2.2.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/ttdsyntheticsession.h"
#include "debugger/ttd/timetravelengine.h"

using namespace ttd;
using namespace ttdtest;

namespace
{
/// Every checkpoint of @p ref from @p from on restores the same on @p a, with the same bus cursor record
void ExpectSameFrom(const TimeTravelEngine& a, const TimeTravelEngine& ref, size_t from)
{
    ASSERT_EQ(a.CheckpointCount(), ref.CheckpointCount());
    for (size_t i = from; i < ref.CheckpointCount(); ++i)
    {
        ExpectSameCheckpoint(a, i, ref, i);
        TTDPortRecord x, y;
        ASSERT_TRUE(a.BusReads().Get(a.Checkpoint(i)->busReadCursor, x)) << i;
        ASSERT_TRUE(ref.BusReads().Get(ref.Checkpoint(i)->busReadCursor, y)) << i;
        ASSERT_TRUE(x.SameAccess(y) && x.value == y.value) << "bus position at checkpoint " << i;
    }
}

/// The events of @p a from machine time @p t on equal @p ref's
void ExpectSameEventsFrom(const TimeTravelEngine& a, const TimeTravelEngine& ref, TTDMachineTime t)
{
    const size_t i = a.Events().CursorAt(t), j = ref.Events().CursorAt(t);
    ASSERT_EQ(a.Events().Count() - i, ref.Events().Count() - j);
    for (size_t k = 0; i + k < a.Events().Count(); ++k)
    {
        EXPECT_EQ(a.Events().At(i + k).machineTime, ref.Events().At(j + k).machineTime) << k;
        EXPECT_EQ(a.Payloads().Bytes(a.Events().At(i + k).payload),
                  ref.Payloads().Bytes(ref.Events().At(j + k).payload))
            << k;
    }
}
}  // namespace

/// T-E1: 60 frames, a fork inside frame 33 (its bus read kept), 30 frames on the branch, a switch back to the
/// trunk's end and 10 more frames there: the branch equals a session that recorded frames 0-33 and the new
/// ones, the trunk equals one that never forked
TEST(TimeTravelEngineBranches_Test, AForkKeepsTheOldFutureAndBothContinue)
{
    Session s(Growable(10));
    Session trunkRef(Growable(10));
    Session branchRef(Growable(10));
    for (int i = 0; i < 60; ++i)
    {
        s.Frame();
        trunkRef.Frame();
    }
    for (int i = 0; i < 34; ++i)
        branchRef.Frame();
    const std::vector<uint8_t> trunkMemory = s.memory;
    const uint64_t trunkFrame = s.frame;

    std::string error;
    uint32_t line = 0;
    ASSERT_TRUE(s.engine.ForkAt(33, {0, 33, 1500}, line, error)) << error;
    EXPECT_EQ(line, 1u);
    EXPECT_EQ(s.engine.ActiveLine(), 1u);
    EXPECT_EQ(s.engine.LineParent(1), 0u);
    EXPECT_EQ(s.engine.LineForkCheckpoint(1), 33u);
    EXPECT_EQ(s.engine.CheckpointCount(), 34u) << "the branch holds the fork checkpoint";
    EXPECT_EQ(s.engine.FirstCheckpoint(), 33u);
    EXPECT_EQ(s.engine.BusReads().Size(), 1u) << "the fork frame's read at (33, 1000) is before the cut";

    s.memory = branchRef.memory;
    s.frame = branchRef.frame;
    for (int i = 0; i < 30; ++i)
    {
        s.Frame();
        branchRef.Frame();
    }
    ExpectSameFrom(s.engine, branchRef.engine, 33);
    ExpectSameEventsFrom(s.engine, branchRef.engine, s.engine.Checkpoint(33)->start);

    // Back to the trunk's end: its 60 checkpoints as they were, and it continues
    ASSERT_TRUE(s.engine.SwitchLine(0, 59, error)) << error;
    EXPECT_EQ(s.engine.CheckpointCount(), 60u);
    s.memory = trunkMemory;
    s.frame = trunkFrame;
    for (int i = 0; i < 10; ++i)
    {
        s.Frame();
        trunkRef.Frame();
    }
    ExpectSameFrom(s.engine, trunkRef.engine, 0);
    ExpectSameEventsFrom(s.engine, trunkRef.engine, 0);
    EXPECT_FALSE(s.engine.SwitchLine(1, 20, error)) << "checkpoint 20 is the trunk's, not the branch's own";
    EXPECT_FALSE(s.engine.SwitchLine(7, 0, error));
}

/// T-E2: a fork shares every version; a few frames on the branch add only what they change; releasing the
/// branch returns the store and the tables to where they stood
TEST(TimeTravelEngineBranches_Test, VersionsAreSharedAndReleasedExactly)
{
    Session s(Growable(10));
    for (int i = 0; i < 40; ++i)
        s.Frame();
    std::string error;
    ASSERT_TRUE(s.engine.SwitchLine(0, 39, error)) << error;   // the head as a switch leaves it
    const size_t versions = s.engine.PieceStore().LiveVersions();
    const size_t payloads = s.engine.Payloads().LiveCount();

    uint32_t line = 0;
    ASSERT_TRUE(s.engine.ForkAt(25, {0, 25, 0}, line, error)) << error;
    EXPECT_EQ(s.engine.PieceStore().LiveVersions(), versions) << "a fork stores no piece";
    s.frame = 26;
    for (int i = 0; i < 5; ++i)
        s.Frame();
    EXPECT_LE(s.engine.PieceStore().LiveVersions(), versions + 5) << "one piece a frame";

    ASSERT_TRUE(s.engine.SwitchLine(0, 39, error)) << error;
    EXPECT_FALSE(s.engine.ReleaseLine(0, error)) << "the trunk stays";
    ASSERT_TRUE(s.engine.ReleaseLine(line, error)) << error;
    EXPECT_FALSE(s.engine.HasLine(line));
    EXPECT_EQ(s.engine.PieceStore().LiveVersions(), versions);
    EXPECT_EQ(s.engine.Payloads().LiveCount(), payloads);
    EXPECT_FALSE(s.engine.ReleaseLine(line, error)) << "already gone";
}

/// T-E3: a fork at a frame boundary keeps none of the frame's records; a fork of a fork; release order
TEST(TimeTravelEngineBranches_Test, ForkAtABoundaryAndForksOfForks)
{
    Session s(Growable(10));
    for (int i = 0; i < 30; ++i)
        s.Frame();
    std::string error;
    uint32_t one = 0, two = 0;
    ASSERT_TRUE(s.engine.ForkAt(20, {0, 20, 0}, one, error)) << error;
    EXPECT_EQ(s.engine.BusReads().Size(), 0u) << "the read at (20, 1000) lies after the fork";
    EXPECT_FALSE(s.engine.ForkAt(19, {0, 19, 0}, two, error)) << "checkpoint 19 is not the branch's own";
    EXPECT_FALSE(s.engine.ForkAt(20, {0, 21, 0}, two, error)) << "the fork lies in the checkpoint's frame";
    s.frame = 21;
    for (int i = 0; i < 10; ++i)
        s.Frame();
    ASSERT_TRUE(s.engine.ForkAt(25, {0, 25, 3000}, two, error)) << error;
    EXPECT_EQ(s.engine.LineParent(two), one);
    EXPECT_EQ(s.engine.LineForkCheckpoint(two), 25u);
    ASSERT_TRUE(s.engine.SwitchLine(0, 29, error)) << error;
    EXPECT_FALSE(s.engine.ReleaseLine(one, error)) << "a line forks from it";
    ASSERT_TRUE(s.engine.ReleaseLine(two, error)) << error;
    ASSERT_TRUE(s.engine.ReleaseLine(one, error)) << error;
    EXPECT_EQ(s.engine.LineIds(), 3u) << "ids are not reused";
}

/// T-E4: the session's end releases every line; the next session starts with the trunk alone
TEST(TimeTravelEngineBranches_Test, EndSessionReleasesEveryLine)
{
    {
        Session s(Growable(10));
        for (int i = 0; i < 20; ++i)
            s.Frame();
        std::string error;
        uint32_t line = 0;
        ASSERT_TRUE(s.engine.ForkAt(10, {0, 10, 500}, line, error)) << error;
        s.frame = 11;
        for (int i = 0; i < 5; ++i)
            s.Frame();
        s.engine.EndSession();
        EXPECT_EQ(s.engine.PieceStore().LiveVersions(), 0u);
        EXPECT_EQ(s.engine.Payloads().LiveCount(), 0u);
        EXPECT_EQ(s.engine.LineIds(), 1u);
        EXPECT_EQ(s.engine.ActiveLine(), 0u);
    }
}

/// T-E5: viewing a line reads its checkpoints and journals without touching the capture head; a capture after
/// the view moved back continues from the line's end; a fork while viewing the parent forks from the parent
TEST(TimeTravelEngineBranches_Test, ViewingALineAndForkingFromIt)
{
    Session s(Growable(10));
    Session trunkRef(Growable(10));
    for (int i = 0; i < 40; ++i)
    {
        s.Frame();
        trunkRef.Frame();
    }
    const std::vector<uint8_t> trunkMemory = s.memory;
    std::string error;
    uint32_t one = 0;
    ASSERT_TRUE(s.engine.ForkAt(30, {0, 30, 1500}, one, error)) << error;
    s.frame = 31;
    for (int i = 0; i < 5; ++i)
        s.Frame();
    std::vector<uint8_t> branchMemory = s.memory;
    const uint64_t branchFrame = s.frame;
    const size_t versions = s.engine.PieceStore().LiveVersions();

    // The parent's part of the lineage: viewed, read, nothing stored
    ASSERT_TRUE(s.engine.ViewLine(0));
    EXPECT_EQ(s.engine.ActiveLine(), 0u);
    EXPECT_EQ(s.engine.CheckpointCount(), 40u);
    for (size_t i = 0; i < 40; ++i)
        ExpectSameCheckpoint(s.engine, i, trunkRef.engine, i);
    EXPECT_EQ(s.engine.PieceStore().LiveVersions(), versions);
    EXPECT_FALSE(s.engine.ViewLine(9));

    // Back on the branch: its capture continues where it stood (the head is rebuilt from its end)
    ASSERT_TRUE(s.engine.ViewLine(one));
    s.memory = branchMemory;
    s.frame = branchFrame;
    s.Frame();
    EXPECT_EQ(s.engine.CheckpointCount(), 37u);
    std::vector<uint8_t> restored(4 * kTTDPieceSize);
    ASSERT_TRUE(s.engine.RestoreRegion(36, 0, restored.data()).Ok());
    EXPECT_TRUE(restored == s.memory) << "the capture after the view went back is the branch's next frame";

    // A fork while viewing the trunk leaves the trunk
    ASSERT_TRUE(s.engine.ViewLine(0));
    uint32_t two = 0;
    ASSERT_TRUE(s.engine.ForkAt(10, {0, 10, 0}, two, error)) << error;
    EXPECT_EQ(s.engine.LineParent(two), 0u);
    s.memory = trunkMemory;
    ASSERT_TRUE(s.engine.ViewLine(0));
    ASSERT_TRUE(s.engine.ViewLine(two));
    EXPECT_EQ(s.engine.CheckpointCount(), 11u);
}

/// T-E6: media versions follow the line: a disk written on the trunk after the fork is not the branch's
TEST(TimeTravelEngineBranches_Test, MediaVersionsFollowTheLine)
{
    Session s(Growable(10));
    for (int i = 0; i < 10; ++i)
    {
        if (i == 2 || i == 7)
            s.engine.NoteMediaVersion("fdd.a", "trd", true, {11, static_cast<uint64_t>(i)});
        s.Frame();
    }
    std::string error;
    uint32_t line = 0;
    ASSERT_TRUE(s.engine.ForkAt(5, {0, 5, 0}, line, error)) << error;
    TTDMediaVersion v;
    ASSERT_TRUE(s.engine.MediaVersionAt(5, 0, v));
    EXPECT_EQ(v.version, 2u);
    s.frame = 6;
    s.Frame();
    ASSERT_TRUE(s.engine.MediaVersionAt(6, 0, v));
    EXPECT_EQ(v.version, 2u) << "the trunk's write at frame 7 is not the branch's";
    s.engine.NoteMediaVersion("fdd.a", "trd", true, {11, 99});
    s.Frame();
    ASSERT_TRUE(s.engine.MediaVersionAt(7, 0, v));
    EXPECT_EQ(v.version, 99u);

    ASSERT_TRUE(s.engine.ViewLine(0));
    ASSERT_TRUE(s.engine.MediaVersionAt(8, 0, v));
    EXPECT_EQ(v.version, 7u) << "the trunk keeps its own";
}
