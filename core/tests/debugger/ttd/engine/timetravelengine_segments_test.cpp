/// @file timetravelengine_segments_test.cpp
/// @brief History in segments (D41, phase-4 TDD §5.3): each segment starts
/// with a baseline that stores every piece whole; the ring keeps the window
/// and drops the oldest segment whole; the growable list keeps everything.
/// A file written while a ring records still holds the whole session.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/ttdsyntheticsession.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelengine.h"

using namespace ttd;

using namespace ttdtest;


/// 100 frames, window 25, segments of 10: the ring holds the last 25-35
/// frames, each restores as in a session that keeps everything, the older
/// ones are gone, and memory holds far fewer versions
TEST(TimeTravelEngineSegments_Test, TheRingKeepsTheWindow)
{
    Session ring(Ring(25, 10));
    Session all(Growable(10));
    for (int i = 0; i < 100; ++i)
    {
        ring.Frame();
        all.Frame();
    }
    const size_t first = ring.engine.FirstCheckpoint();
    const size_t held = ring.engine.CheckpointCount() - first;
    EXPECT_EQ(ring.engine.CheckpointCount(), 100u);
    EXPECT_GE(held, 25u);
    EXPECT_LE(held, 35u);
    EXPECT_TRUE(ring.engine.Checkpoint(first)->baseline) << "a ring always starts at a baseline";
    EXPECT_EQ(ring.engine.Checkpoint(first - 1), nullptr);
    std::vector<uint8_t> out(4 * kTTDPieceSize);
    EXPECT_FALSE(ring.engine.RestoreRegion(first - 1, 0, out.data()).Ok());
    for (size_t i = first; i < 100; ++i)
        ExpectSameCheckpoint(ring.engine, i, all.engine, i);
    EXPECT_LT(ring.engine.PieceStore().LiveVersions() * 2, all.engine.PieceStore().LiveVersions());

    // Events and their payloads before the window are gone with it
    ASSERT_GT(ring.engine.Events().Count(), 0u);
    EXPECT_GE(ring.engine.Events().At(0).machineTime, ring.engine.Checkpoint(first)->start);
    EXPECT_LT(ring.engine.Payloads().LiveCount(), all.engine.Payloads().LiveCount());
}

/// Growable keeps every frame; every segment starts with whole pieces only
TEST(TimeTravelEngineSegments_Test, GrowableKeepsEverySegment)
{
    Session all(Growable(10));
    for (int i = 0; i < 100; ++i)
        all.Frame();
    EXPECT_EQ(all.engine.FirstCheckpoint(), 0u);
    ASSERT_EQ(all.engine.Segments().size(), 10u);
    for (const TTDSegmentInfo& s : all.engine.Segments())
    {
        const TTDEngineCheckpoint* cp = all.engine.Checkpoint(s.firstCheckpoint);
        ASSERT_NE(cp, nullptr);
        EXPECT_TRUE(cp->baseline);
        EXPECT_EQ(cp->position.frame, s.firstFrame);
        for (uint32_t p = 0; p < 4; ++p)
        {
            const uint32_t id = all.engine.VersionAt(s.firstCheckpoint, 0, p);
            EXPECT_FALSE(TTDPieceStore::IsDifference(all.engine.PieceStore().EncodingOf(id)))
                << "segment at frame " << s.firstFrame << ", piece " << p;
        }
    }
    // Inside a segment the pieces are differences again (a few bytes a frame)
    const uint32_t id = all.engine.VersionAt(15, 0, 15 % 4);
    EXPECT_TRUE(TTDPieceStore::IsDifference(all.engine.PieceStore().EncodingOf(id)));
}

/// The ring's held history saves and loads; written while recording, the
/// file holds the whole session, the dropped segments included
TEST(TimeTravelEngineSegments_Test, FilesAndTheRing)
{
    Session ring(Ring(25, 10));
    Session all(Growable(10));
    TTDMemorySink sink;
    TTDSessionWriter writer;
    TTDSessionSaveParams params;
    params.checkpointsPerPart = 5;
    std::string error;
    ring.Frame();
    all.Frame();
    ASSERT_TRUE(writer.Begin(ring.engine, sink, params, error)) << error;
    for (int i = 1; i < 100; ++i)
    {
        ring.Frame();
        all.Frame();
        ASSERT_TRUE(writer.Collect(ring.engine)) << writer.Error();
    }
    ASSERT_TRUE(writer.Finish(ring.engine)) << writer.Error();

    TimeTravelEngine whole;
    whole.SetHistoryPolicy(Growable(10));
    TTDMemorySource source(sink.bytes);
    TTDSessionLoadReport report;
    ASSERT_TRUE(TTDSessionFile::Load(whole, source, error, &report)) << error;
    ASSERT_EQ(whole.CheckpointCount(), 100u) << "the file holds the whole session";
    for (size_t i = 0; i < 100; ++i)
        ExpectSameCheckpoint(whole, i, all.engine, i);

    // Saving what the ring holds: the window, from its baseline
    TTDMemorySink held;
    ASSERT_TRUE(TTDSessionFile::Save(ring.engine, held, error, params)) << error;
    TimeTravelEngine window;
    TTDMemorySource heldSource(held.bytes);
    ASSERT_TRUE(TTDSessionFile::Load(window, heldSource, error)) << error;
    const size_t first = ring.engine.FirstCheckpoint();
    ASSERT_EQ(window.CheckpointCount(), 100 - first);
    for (size_t j = 0; j < window.CheckpointCount(); ++j)
    {
        ExpectSameCheckpoint(window, j, ring.engine, first + j);
        // A replay reads the bus journal from the checkpoint's position: the same record
        TTDPortRecord a, b;
        ASSERT_TRUE(window.BusReads().Get(window.Checkpoint(j)->busReadCursor, a));
        ASSERT_TRUE(ring.engine.BusReads().Get(ring.engine.Checkpoint(first + j)->busReadCursor, b));
        ASSERT_TRUE(a.SameAccess(b) && a.value == b.value) << "bus position at checkpoint " << j;
    }
}
