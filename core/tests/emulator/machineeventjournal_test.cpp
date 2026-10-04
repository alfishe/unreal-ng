// MachineEventJournal (machineeventjournal.h): order, the TTD rewind rule, epochs, the ring, filters

#include <gtest/gtest.h>

#include "emulator/machineeventjournal.h"

namespace
{
MachineEvent Event(const char* kind, uint64_t frame, uint32_t t)
{
    MachineEvent e;
    e.kind = kind;
    e.frame = frame;
    e.t = t;
    return e;
}
}  // namespace

TEST(MachineEventJournal_Test, KeepsOrderAndFilters)
{
    MachineEventJournal j;
    j.Append(Event("cnf", 10, 100));
    j.Append(Event("port_1ffd", 10, 200));
    j.Append(Event("cnf", 12, 5));

    MachineEventJournal::Filter all;
    const auto snap = j.Read(all);
    ASSERT_EQ(snap.events.size(), 3u);
    EXPECT_EQ(snap.events[0].seq, 1u);
    EXPECT_EQ(snap.events[2].seq, 3u);

    MachineEventJournal::Filter cnf;
    cnf.kinds = MachineEventJournal::SplitKinds("cnf, all_mode");
    EXPECT_EQ(j.Read(cnf).events.size(), 2u);

    MachineEventJournal::Filter since;
    since.sinceSeq = 2;
    EXPECT_EQ(j.Read(since).events.size(), 1u);

    MachineEventJournal::Filter frame;
    frame.frameFrom = frame.frameTo = 10;
    frame.limit = 1;
    const auto one = j.Read(frame);
    ASSERT_EQ(one.events.size(), 1u);
    EXPECT_EQ(one.matched, 2u);
    EXPECT_STREQ(one.events[0].kind, "port_1ffd") << "the newest of the matches";
}

// Back in time in one epoch (a TTD seek, then live): the events after that moment go; a reset starts an epoch
TEST(MachineEventJournal_Test, RewindsTheFutureButNotAcrossResets)
{
    MachineEventJournal j;
    j.Append(Event("cnf", 10, 100));
    j.Append(Event("cnf", 20, 100));
    j.Append(Event("cnf", 30, 100));
    j.Append(Event("rgmod", 15, 0));
    auto snap = j.Read({});
    ASSERT_EQ(snap.events.size(), 2u);
    EXPECT_EQ(snap.events[1].frame, 15u);
    EXPECT_EQ(snap.rewound, 2u);

    j.NextEpoch();
    j.Append(Event("reset", 0, 0));
    snap = j.Read({});
    ASSERT_EQ(snap.events.size(), 3u) << "frame 0 of the next epoch keeps the history";
    EXPECT_EQ(snap.events[2].epoch, 1u);
}

TEST(MachineEventJournal_Test, DropsTheOldestWhenFullAndOffAppendsNothing)
{
    MachineEventJournal j;
    for (size_t i = 0; i < MachineEventJournal::kCapacity + 5; i++)
        j.Append(Event("cnf", i, 0));
    auto snap = j.Read(MachineEventJournal::Filter{{}, 0, -1, -1, 0});
    EXPECT_EQ(snap.held, MachineEventJournal::kCapacity);
    EXPECT_EQ(snap.dropped, 5u);
    EXPECT_EQ(snap.events.front().frame, 5u);

    j.SetEnabled(false);
    j.Append(Event("cnf", 1000000, 0));
    EXPECT_EQ(j.Read({}).appended, MachineEventJournal::kCapacity + 5);
    j.Clear();
    EXPECT_EQ(j.Read({}).held, 0u);
}
