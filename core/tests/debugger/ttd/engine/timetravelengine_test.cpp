/// @file timetravelengine_test.cpp
/// @brief TimeTravelEngine unit tests: sessions, capture, restore, parents and
/// accounting, on hand-made frame inputs (no emulator).

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "debugger/ttd/timetravelengine.h"

using namespace ttd;

namespace
{

TTDRegionDesc Region(TTDRegionId id, const char* name, uint32_t pieces)
{
    TTDRegionDesc r;
    r.id = id;
    r.name = name;
    r.pieces = pieces;
    r.bytes = pieces * kTTDPieceSize;
    return r;
}

std::vector<uint8_t> Piece(uint8_t fill)
{
    return std::vector<uint8_t>(kTTDPieceSize, fill);
}

TTDFrameInput Frame(uint64_t frame)
{
    TTDFrameInput in;
    in.position.frame = frame;
    in.start = frame * 71680;
    return in;
}

}  // namespace

TEST(TimeTravelEngine_Test, RestoresEachCheckpointsMemory)
{
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 4)}, err)) << err;

    const auto a = Piece(0x11), b = Piece(0x22), c = Piece(0x33), zero = Piece(0);
    TTDFrameInput f0 = Frame(100);
    f0.changed = {{0, 0, a.data()}, {0, 1, b.data()}, {0, 2, zero.data()}};
    ASSERT_TRUE(engine.CaptureFrame(f0, err)) << err;
    TTDFrameInput f1 = Frame(101);
    f1.changed = {{0, 1, c.data()}};
    ASSERT_TRUE(engine.CaptureFrame(f1, err)) << err;
    ASSERT_TRUE(engine.CaptureFrame(Frame(102), err)) << err;   // nothing changed

    std::vector<uint8_t> out(4 * kTTDPieceSize, 0xEE);
    std::vector<uint8_t> present;
    ASSERT_TRUE(engine.RestoreRegion(0, 0, out.data(), &present).Ok());
    EXPECT_EQ(present, (std::vector<uint8_t>{1, 1, 1, 0})) << "piece 3 was never seen";
    EXPECT_EQ(out[0], 0x11);
    EXPECT_EQ(out[kTTDPieceSize], 0x22);
    EXPECT_EQ(out[2 * kTTDPieceSize], 0x00);
    EXPECT_EQ(out[3 * kTTDPieceSize], 0xEE) << "an unseen piece is left untouched";

    ASSERT_TRUE(engine.RestoreRegion(2, 0, out.data(), &present).Ok());
    EXPECT_EQ(out[kTTDPieceSize], 0x33) << "the change of frame 101 holds in frame 102";
    ASSERT_TRUE(engine.RestoreRegion(0, 0, out.data(), &present).Ok());
    EXPECT_EQ(out[kTTDPieceSize], 0x22) << "going back restores the older content";
}

TEST(TimeTravelEngine_Test, CheckpointsLinkTheirParent_AndFindTheirPosition)
{
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1)}, err));
    for (uint64_t f = 7; f < 10; ++f)
        ASSERT_TRUE(engine.CaptureFrame(Frame(f), err)) << err;

    EXPECT_EQ(engine.Checkpoint(0)->parent, TTDEngineCheckpoint::kNoParent);
    EXPECT_EQ(engine.Checkpoint(2)->parent, 1u);
    EXPECT_EQ(engine.CheckpointIndexOf({0, 9, 0}), 2);
    EXPECT_EQ(engine.CheckpointIndexOf({0, 10, 0}), -1);
    EXPECT_EQ(engine.CheckpointIndexOf({1, 9, 0}), -1) << "no branch recorded yet";
}

TEST(TimeTravelEngine_Test, RefusesBadInput)
{
    TimeTravelEngine engine;
    std::string err;
    EXPECT_FALSE(engine.CaptureFrame(Frame(1), err)) << "no session yet";
    EXPECT_FALSE(engine.BeginSession({}, err));
    EXPECT_FALSE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1), Region(TTDRegionId::MachineRam, "x", 1)},
                                     err))
        << "a region id is listed once";
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 2)}, err));

    const auto a = Piece(1);
    TTDFrameInput bad = Frame(1);
    bad.changed = {{0, 2, a.data()}};
    EXPECT_FALSE(engine.CaptureFrame(bad, err)) << "piece beyond the region";
    bad.changed = {{1, 0, a.data()}};
    EXPECT_FALSE(engine.CaptureFrame(bad, err)) << "unknown region";

    ASSERT_TRUE(engine.CaptureFrame(Frame(5), err));
    EXPECT_FALSE(engine.CaptureFrame(Frame(5), err)) << "frames must increase";
    TTDFrameInput branch = Frame(6);
    branch.position.branch = 1;
    EXPECT_FALSE(engine.CaptureFrame(branch, err)) << "Phase 1 records the trunk only";
    EXPECT_EQ(engine.CheckpointCount(), 1u);
}

TEST(TimeTravelEngine_Test, UnchangedFramesShareTheReferenceTable)
{
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 256)}, err));
    const auto a = Piece(9);
    TTDFrameInput f0 = Frame(0);
    f0.changed = {{0, 0, a.data()}};
    ASSERT_TRUE(engine.CaptureFrame(f0, err));
    const size_t tablesAfterOne = engine.HeapBreakdown().referenceTables;
    for (uint64_t f = 1; f < 50; ++f)
        ASSERT_TRUE(engine.CaptureFrame(Frame(f), err));
    EXPECT_EQ(engine.HeapBreakdown().referenceTables, tablesAfterOne)
        << "frames that change nothing add no reference table";
    EXPECT_EQ(engine.Checkpoint(49)->regionTables[0].get(), engine.Checkpoint(0)->regionTables[0].get());
}

TEST(TimeTravelEngine_Test, OptionalStreamsRunAtEveryCapturedFrameWhenOn)
{
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1)}, err));
    std::vector<uint64_t> seen;
    ASSERT_TRUE(engine.Streams().Register(0, "screenshot", [&](const TTDPosition& at) { seen.push_back(at.frame); }));
    ASSERT_TRUE(engine.CaptureFrame(Frame(1), err));
    ASSERT_TRUE(engine.Streams().SetEnabled(0, true));
    ASSERT_TRUE(engine.CaptureFrame(Frame(2), err));
    EXPECT_EQ(seen, (std::vector<uint64_t>{2}));
}

TEST(TimeTravelEngine_Test, ArbitraryContentRoundTrips_AndBadRequestsAreReported)
{
    // Content that is neither constant nor zero comes back byte for byte; a restore
    // of something that does not exist reports it instead of handing back wrong memory
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1)}, err));
    std::vector<uint8_t> p(kTTDPieceSize);
    for (size_t i = 0; i < p.size(); ++i)
        p[i] = static_cast<uint8_t>(i * 7);
    TTDFrameInput f0 = Frame(0);
    f0.changed = {{0, 0, p.data()}};
    ASSERT_TRUE(engine.CaptureFrame(f0, err));
    std::vector<uint8_t> out(kTTDPieceSize);
    EXPECT_TRUE(engine.RestoreRegion(0, 0, out.data()).Ok());
    EXPECT_EQ(out, p);
    EXPECT_EQ(engine.RestoreRegion(1, 0, out.data()).status, TTDRestoreStatus::Damaged) << "no such checkpoint";
}
