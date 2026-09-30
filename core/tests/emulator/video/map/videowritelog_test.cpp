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
