#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "common/threadpool.h"

/// ThreadPool: ParallelFor splits [0, n) into the same contiguous ranges whatever
/// the number of threads (ZX DLSS merges per-range results in range order and
/// must stay bit-exact), runs each exactly once and returns when all are done;
/// Submit hands back results; a pool without workers runs everything inline.

namespace
{
std::vector<std::pair<int, int>> Ranges(ThreadPool& pool, int n, int chunks)
{
    std::mutex mutex;
    std::vector<std::pair<int, int>> ranges;
    pool.ParallelFor(n, chunks, [&](int b, int e) {
        std::lock_guard<std::mutex> lock(mutex);
        ranges.emplace_back(b, e);
    });
    std::sort(ranges.begin(), ranges.end());
    return ranges;
}
}  // namespace

TEST(ThreadPool_Test, ParallelForUsesTheSameRangesWithAnyNumberOfThreads)
{
    ThreadPool none(0), two(2), seven(7);
    for (auto [n, chunks] : {std::pair{288, 8}, std::pair{19, 8}, std::pair{5, 8}, std::pair{1, 3}, std::pair{100, 1}})
    {
        const auto expected = Ranges(none, n, chunks);
        EXPECT_EQ(Ranges(two, n, chunks), expected) << n << "/" << chunks;
        EXPECT_EQ(Ranges(seven, n, chunks), expected) << n << "/" << chunks;
        // contiguous, covering [0, n), ceil(n / chunks) long except the last
        int at = 0;
        const int size = (n + chunks - 1) / chunks;
        for (const auto& [b, e] : expected)
        {
            EXPECT_EQ(b, at);
            EXPECT_LE(e - b, size);
            at = e;
        }
        EXPECT_EQ(at, n);
    }
}

TEST(ThreadPool_Test, ParallelForRunsEveryIndexOnceAndWaits)
{
    ThreadPool pool(4, "test", ThreadPool::Priority::Interactive);
    for (int round = 0; round < 200; ++round)
    {
        std::vector<std::atomic<int>> hits(1000);
        pool.ParallelFor(1000, 8, [&](int b, int e) {
            for (int i = b; i < e; ++i)
                hits[i]++;
        });
        // Returned: every range is finished
        for (int i = 0; i < 1000; ++i)
            ASSERT_EQ(hits[i].load(), 1) << "index " << i << " round " << round;
    }
}

TEST(ThreadPool_Test, SubmitReturnsTheResult)
{
    ThreadPool pool(2);
    auto a = pool.Submit([] { return 6 * 7; });
    auto b = pool.Submit([] { return std::string("done"); });
    EXPECT_EQ(a.get(), 42);
    EXPECT_EQ(b.get(), "done");

    ThreadPool inline_(0);
    EXPECT_EQ(inline_.Submit([] { return 1; }).get(), 1);
    EXPECT_EQ(inline_.Size(), 0u);
}

TEST(ThreadPool_Test, EmptyRangeDoesNothing)
{
    ThreadPool pool(2);
    bool called = false;
    pool.ParallelFor(0, 4, [&](int, int) { called = true; });
    EXPECT_FALSE(called);
}
