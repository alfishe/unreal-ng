// The video write log (PLAN #42 phase 3, videowritelog.h): the latches in
// force at any T of the current and previous frame.

#include <gtest/gtest.h>

#include "emulator/video/map/videowritelog.h"

using namespace videomap;

namespace
{
VideoLatches Mode(uint8_t mode)
{
    VideoLatches l;
    l.mode = mode;
    return l;
}
} // namespace

TEST(VideoWriteLog_Test, StateAtIsTheLastWriteAtOrBeforeT)
{
    VideoWriteLog log;
    log.BeginFrame(10, Mode(1));
    log.Record(1000, Mode(2));
    log.Record(5000, Mode(3));

    const VideoFrameLog& f = log.Current();
    EXPECT_EQ(f.frame, 10u);
    EXPECT_EQ(f.StateAt(0).mode, 1) << "before any write: the frame start";
    EXPECT_EQ(f.StateAt(999).mode, 1);
    EXPECT_EQ(f.StateAt(1000).mode, 2) << "a write is in force from its own T";
    EXPECT_EQ(f.StateAt(4999).mode, 2);
    EXPECT_EQ(f.StateAt(70000).mode, 3);
}

TEST(VideoWriteLog_Test, UnchangedLatchesAreNotLogged)
{
    VideoWriteLog log;
    log.BeginFrame(1, Mode(1));
    log.Record(100, Mode(1));  // e.g. OUT #FE with the same border: nothing new
    log.Record(200, Mode(2));
    log.Record(300, Mode(2));
    EXPECT_EQ(log.Current().writes.size(), 1u);
}

TEST(VideoWriteLog_Test, FullLogIsMarkedPartial)
{
    VideoWriteLog log;
    log.BeginFrame(1, Mode(0));
    for (size_t i = 0; i < VideoWriteLog::kCapacity + 10; ++i)
        log.Record(static_cast<uint32_t>(i), Mode(static_cast<uint8_t>(1 + i % 2)));
    EXPECT_EQ(log.Current().writes.size(), VideoWriteLog::kCapacity);
    EXPECT_TRUE(log.Current().partial);
}

TEST(VideoWriteLog_Test, FrameStartPublishesTheFinishedFrame)
{
    VideoWriteLog log;
    EXPECT_FALSE(log.Published().valid) << "nothing completed yet";
    log.BeginFrame(5, Mode(1));
    log.Record(700, Mode(4));
    log.BeginFrame(6, Mode(4));

    EXPECT_EQ(log.Previous().frame, 5u);
    const VideoFrameLog published = log.Published();
    ASSERT_TRUE(published.valid);
    EXPECT_EQ(published.frame, 5u);
    EXPECT_EQ(published.StateAt(800).mode, 4);
    EXPECT_EQ(log.Current().frame, 6u);
    EXPECT_TRUE(log.Current().writes.empty());
}

TEST(VideoWriteLog_Test, NoRecordBeforeTheFirstFrame)
{
    VideoWriteLog log;
    log.Record(10, Mode(2));
    EXPECT_FALSE(log.Current().valid);
    EXPECT_TRUE(log.Current().writes.empty());
}

// The family block (Sprinter RGMOD ... frame height) is part of the latches: a change is logged
TEST(VideoWriteLog_Test, FamilyLatchesAreCompared)
{
    VideoWriteLog log;
    VideoLatches l;
    log.BeginFrame(1, l);
    l.rgMod = 1;
    EXPECT_TRUE(log.Changes(l));
    log.Record(100, l, 0x8123);
    l.frameLines = 312;
    log.Record(200, l, 0x8130);
    ASSERT_EQ(log.Current().writes.size(), 2u);
    EXPECT_EQ(log.Current().writes[0].pc, 0x8123);
    EXPECT_EQ(log.Current().StateAt(150).frameLines, 0);
    EXPECT_EQ(log.Current().StateAt(250).frameLines, 312);
}

// Table writes (palettes, the Sprinter's mode table) are counted per frame with the first and the last
TEST(VideoWriteLog_Test, TableWritesAreCountedPerFrame)
{
    VideoWriteLog log;
    log.RecordTable(VideoTable::Palette, 5, 0x10, 0x8000);  // before the first frame: ignored
    log.BeginFrame(1, VideoLatches{});
    log.RecordTable(VideoTable::Palette, 100, 0x3E0, 0x8000);
    log.RecordTable(VideoTable::Palette, 140, 0x3E2, 0x8004);
    log.RecordTable(VideoTable::ModeTable, 160, 0x700, 0x8010);
    const VideoTableWrites& palette = log.Current().tables[static_cast<size_t>(VideoTable::Palette)];
    EXPECT_EQ(palette.count, 2u);
    EXPECT_EQ(palette.firstT, 100u);
    EXPECT_EQ(palette.lastT, 140u);
    EXPECT_EQ(palette.lastAddress, 0x3E2u);
    EXPECT_EQ(palette.lastPc, 0x8004);
    EXPECT_EQ(log.Current().tables[static_cast<size_t>(VideoTable::ModeTable)].count, 1u);

    log.BeginFrame(2, VideoLatches{});
    EXPECT_EQ(log.Current().tables[static_cast<size_t>(VideoTable::Palette)].count, 0u) << "a new frame starts at 0";
    EXPECT_EQ(log.Previous().tables[static_cast<size_t>(VideoTable::Palette)].count, 2u);
}
