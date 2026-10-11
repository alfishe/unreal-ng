/// @file operationlog_test.cpp
/// @brief The operation log: sequence numbers, the ring, reading since a point, readers on other threads.

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "emulator/coordinator/operationlog.h"

using namespace coordinator;

namespace
{
OperationLogEntry Entry(uint64_t frame)
{
    OperationLogEntry e;
    e.time.frame = frame;
    e.summary = std::to_string(frame);
    return e;
}
}  // namespace

TEST(OperationLog_Test, EmptyAtFirst)
{
    OperationLog log;
    EXPECT_EQ(log.LastSeq(), 0u);
    EXPECT_TRUE(log.Entries().empty());
}

TEST(OperationLog_Test, SequenceNumbersFollowAppendOrder)
{
    OperationLog log;
    EXPECT_EQ(log.Append(Entry(10)), 1u);
    EXPECT_EQ(log.Append(Entry(11)), 2u);
    const auto all = log.Entries();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].seq, 1u);
    EXPECT_EQ(all[0].time.frame, 10u);
    EXPECT_EQ(all[1].seq, 2u);
    const auto since = log.Entries(1);
    ASSERT_EQ(since.size(), 1u);
    EXPECT_EQ(since[0].seq, 2u);
}

TEST(OperationLog_Test, TheRingKeepsTheNewestOldestFirst)
{
    OperationLog log;
    const size_t total = OperationLog::kCapacity * 2 + 5;
    for (size_t i = 1; i <= total; ++i)
        log.Append(Entry(i));
    const auto kept = log.Entries();
    ASSERT_EQ(kept.size(), OperationLog::kCapacity);
    EXPECT_EQ(kept.front().seq, total - OperationLog::kCapacity + 1);
    EXPECT_EQ(kept.back().seq, total);
    for (size_t i = 1; i < kept.size(); ++i)
        EXPECT_EQ(kept[i].seq, kept[i - 1].seq + 1);
    EXPECT_EQ(log.LastSeq(), total);
    EXPECT_EQ(log.Entries(total - 3).size(), 3u);
}

TEST(OperationLog_Test, ReadersOnOtherThreadsSeeWholeEntries)
{
    OperationLog log;
    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::thread reader([&]() {
        while (!stop.load())
            for (const OperationLogEntry& e : log.Entries())
                if (e.summary != std::to_string(e.time.frame))
                    ++bad;
    });
    for (uint64_t i = 1; i <= 3000; ++i)
        log.Append(Entry(i));
    stop = true;
    reader.join();
    EXPECT_EQ(bad.load(), 0);
}

// M6: a script's pokes are one tool edit after another - they count in the newest entry (under the new seq, so a
// poller sees it), never pushing the other operations out of the ring; anything between them starts a new entry
TEST(OperationLog_Test, TheSameToolEditAgainCountsInTheNewestEntry)
{
    OperationLog log;
    OperationLogEntry reset;
    reset.kind = OpKind::Reset;
    log.Append(reset);
    OperationLogEntry poke;
    poke.kind = OpKind::ToolEdit;
    poke.summary = "lua poke";
    for (size_t i = 0; i < OperationLog::kCapacity * 2; ++i)
        log.Append(poke);
    auto entries = log.Entries();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].kind, OpKind::Reset);
    EXPECT_EQ(entries[1].repeats, OperationLog::kCapacity * 2);
    EXPECT_EQ(entries[1].seq, log.LastSeq());
    EXPECT_EQ(log.Entries(entries[1].seq - 1).size(), 1u) << "a poller since the previous poke sees it";

    OperationLogEntry other = poke;
    other.summary = "webapi memory";
    log.Append(other);
    log.Append(poke);
    entries = log.Entries();
    ASSERT_EQ(entries.size(), 4u);
    EXPECT_EQ(entries[3].repeats, 1u);
}
