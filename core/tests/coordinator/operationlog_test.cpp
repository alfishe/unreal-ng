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
