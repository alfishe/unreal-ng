/// @file gameevents_test.cpp
/// @brief The game's events log (gameevents.h, breakpoint taps README.md §7): the counters and values per name, the
/// recent fired events with their sequence numbers, the revision a poller or the game.events topic follows, and a
/// new definition starting over.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "emulator/io/controllers/gameevents.h"

using namespace controllers;

namespace
{
GameEventInfo Info(const char* name, bool level)
{
    GameEventInfo info;
    info.name = name;
    info.level = level;
    return info;
}

GameEventFrame Frame(uint64_t number, uint32_t fired, int32_t lives)
{
    GameEventFrame frame;
    frame.frame = number;
    frame.fired = fired;
    for (unsigned index = 0; index < 2; index++)
        frame.level[index] = (fired >> index) & 1u ? 1.0f : 0.0f;
    frame.value[0] = -1;
    frame.value[1] = lives;
    return frame;
}
}  // namespace

TEST(GameEventLog_Test, CountsKeepsTheRecentAndFollowsTheRevision)
{
    GameEventLog log;
    EXPECT_FALSE(log.Read().defined);
    log.Record(Frame(1, 1, 3));
    EXPECT_TRUE(log.Read().events.empty()) << "nothing defined: nothing kept";

    log.Define({Info("jump", false), Info("lives", true)}, "game/test");
    const uint64_t defined = log.Revision();
    log.Record(Frame(10, 0, 3));
    const uint64_t first = log.Revision();
    EXPECT_NE(first, defined) << "the first values";
    log.Record(Frame(11, 0, 3));
    EXPECT_EQ(log.Revision(), first) << "a frame with nothing new keeps the revision";

    log.Record(Frame(12, 1, 3));
    log.Record(Frame(14, 1, 2));
    EXPECT_NE(log.Revision(), first);
    GameEventLog::Snapshot snapshot = log.Read();
    EXPECT_TRUE(snapshot.defined);
    EXPECT_EQ(snapshot.profile, "game/test");
    EXPECT_EQ(snapshot.frame, 14u);
    ASSERT_EQ(snapshot.events.size(), 2u);
    EXPECT_EQ(snapshot.events[0].fires, 2u);
    EXPECT_EQ(snapshot.events[0].lastFrame, 14u);
    EXPECT_EQ(snapshot.events[0].value, -1);
    EXPECT_EQ(snapshot.events[1].value, 2) << "the watched value";
    EXPECT_EQ(snapshot.events[1].fires, 0u);
    ASSERT_EQ(snapshot.recent.size(), 2u);
    EXPECT_EQ(snapshot.recent[0].seq, 1u);
    EXPECT_EQ(snapshot.recent[0].frame, 12u);
    EXPECT_EQ(snapshot.recent[1].seq, 2u);
    EXPECT_EQ(snapshot.recent[1].name, "jump");
    EXPECT_EQ(snapshot.seq, 2u);

    // A level above 0 counts its frames but is no stream of events
    log.Record(Frame(15, 2, 2));
    snapshot = log.Read();
    EXPECT_EQ(snapshot.events[1].fires, 1u);
    EXPECT_EQ(snapshot.recent.size(), 2u);

    // The recent list drops the oldest; the sequence keeps counting
    for (uint64_t frame = 20; frame < 20 + GameEventLog::kRecent + 6; frame++)
        log.Record(Frame(frame, 1, 2));
    snapshot = log.Read();
    ASSERT_EQ(snapshot.recent.size(), GameEventLog::kRecent);
    EXPECT_EQ(snapshot.seq, 2u + GameEventLog::kRecent + 6);
    EXPECT_EQ(snapshot.recent.back().seq, snapshot.seq);
    EXPECT_EQ(snapshot.recent.front().seq, snapshot.seq - GameEventLog::kRecent + 1);

    // A new definition starts over; Clear leaves nothing
    log.Define({Info("died", false)}, "game/other");
    snapshot = log.Read();
    EXPECT_EQ(snapshot.profile, "game/other");
    EXPECT_EQ(snapshot.seq, 0u);
    EXPECT_TRUE(snapshot.recent.empty());
    ASSERT_EQ(snapshot.events.size(), 1u);
    log.Clear();
    EXPECT_FALSE(log.Read().defined);
    EXPECT_TRUE(log.Read().profile.empty());
}
