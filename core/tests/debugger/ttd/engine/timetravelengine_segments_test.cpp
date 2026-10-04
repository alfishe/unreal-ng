/// @file timetravelengine_segments_test.cpp
/// @brief History in segments (D41, phase-4 TDD §5.3): each segment starts
/// with a baseline that stores every piece whole; the ring keeps the window
/// and drops the oldest segment whole; the growable list keeps everything.
/// A file written while a ring records still holds the whole session.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelengine.h"

using namespace ttd;

namespace
{
/// Four pieces, one changing a few bytes per frame (differences, not whole
/// pieces), a bus read every frame, a marker with a payload every 7th frame
struct Session
{
    TimeTravelEngine engine;
    std::vector<uint8_t> memory = std::vector<uint8_t>(4 * kTTDPieceSize);
    uint64_t frame = 0;

    explicit Session(const TTDHistoryPolicy& policy)
    {
        engine.SetHistoryPolicy(policy);
        TTDRegionDesc ram;
        ram.name = "ram";
        ram.pieces = 4;
        ram.bytes = 4 * kTTDPieceSize;
        std::string error;
        EXPECT_TRUE(engine.BeginSession({ram}, {}, error)) << error;
        for (size_t i = 0; i < memory.size(); ++i)
            memory[i] = static_cast<uint8_t>(i * 13 + i / 512);
    }

    void Frame()
    {
        const uint32_t piece = static_cast<uint32_t>(frame % 4);
        uint8_t* p = memory.data() + size_t(piece) * kTTDPieceSize;
        for (size_t i = 0; i < kTTDPieceSize; i += 401)
            p[i] = static_cast<uint8_t>(p[i] + frame + 1);
        TTDFrameInput input;
        input.position.frame = frame;
        input.start = frame * 69888;
        if (frame == 0)
            for (uint32_t k = 0; k < 4; ++k)
                input.changed.push_back({0, k, memory.data() + size_t(k) * kTTDPieceSize});
        else
            input.changed.push_back({0, piece, p});
        std::string error;
        ASSERT_TRUE(engine.CaptureFrame(input, error)) << error;
        engine.AppendBusRead({frame, 1000, 0xFE, 0x8000, static_cast<uint8_t>(frame)});
        if (frame % 7 == 3)
        {
            TTDEvent ev;
            ev.kind = TTDEventKind::OtherMarker;
            const uint8_t reason[] = {'x', static_cast<uint8_t>(frame)};
            ev.payload = engine.Payloads().Store(reason, sizeof(reason));
            engine.AppendEvent(frame, 2000, ev);
        }
        ++frame;
    }
};

TTDHistoryPolicy Ring(uint32_t window, uint32_t segment)
{
    return {TTDHistoryMode::Ring, window, segment};
}

TTDHistoryPolicy Growable(uint32_t segment)
{
    return {TTDHistoryMode::Growable, 0, segment};
}

/// Checkpoint @p i of @p a restores as checkpoint @p j of @p b
void ExpectSameCheckpoint(const TimeTravelEngine& a, size_t i, const TimeTravelEngine& b, size_t j)
{
    ASSERT_NE(a.Checkpoint(i), nullptr);
    ASSERT_NE(b.Checkpoint(j), nullptr);
    ASSERT_EQ(a.Checkpoint(i)->position, b.Checkpoint(j)->position);
    std::vector<uint8_t> x(4 * kTTDPieceSize), y(4 * kTTDPieceSize);
    ASSERT_TRUE(a.RestoreRegion(i, 0, x.data()).Ok());
    ASSERT_TRUE(b.RestoreRegion(j, 0, y.data()).Ok());
    ASSERT_TRUE(x == y) << "checkpoint " << i;
}
}  // namespace

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
        ExpectSameCheckpoint(window, j, ring.engine, first + j);
}
