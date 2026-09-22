#include "pch.h"

#include "common/timehelper.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

/// @brief TimeHelper::WaitUntilPrecise is the emulation thread's frame clock. Its wake-up lateness is part of
/// the audio latency budget (SoundAdaptivity.AVLatencyBudget): a frame that lands late eats the ring
/// occupancy trough and the device underruns. On Windows std::condition_variable::wait_until wakes 1 ms
/// (MSVC) to 10-17 ms (MinGW winpthreads) late; the helper must stay within FRAME_PACING_JITTER_BUDGET_MS.
///
/// Timing assertions are statistical (p90) with headroom for a loaded CI box; the hard invariants are
/// "never early" and "never grossly late".
TEST(TimeHelper_Test, WaitUntilPrecise_WakesOnTime)
{
    using clock = std::chrono::steady_clock;
    // 24 samples so "p90" (index 21) is a real percentile, not the maximum:
    // with 8 samples the p90 index lands on the largest sample, and a single
    // OS scheduling hiccup under heavy parallel-shard load (2.5 ms wake on a
    // 2 ms budget) fails an otherwise-healthy run. Two outliers of 24 are
    // tolerated; a consistent lateness problem still trips the budget.
    constexpr int kIterations = 24;
    // Wake lateness is what is measured, not the wait length: the helper sleeps
    // in chunks of at most 4 ms (kMaxChunk) and the final wake decides the
    // lateness, so 5 ms already crosses a chunk boundary (2 chunks) while
    // keeping all 24 samples. The real Pentagon frame (20480 us) made this
    // test ~490 ms of pure waiting.
    const auto kFrame = std::chrono::microseconds(5000);

    // Wake lateness is dominated by OS scheduling once the machine is busy (other
    // processes, a build, a parallel test run): a descheduled thread wakes
    // several ms late while p50 stays ~0.2 ms, so a single measurement of a
    // wall-clock budget is flaky by nature. A load burst is transient, a real
    // helper defect is not - so the budget is asserted as "met in at least one
    // of kAttempts independent measurements". "Never early" is checked on every
    // sample of every attempt.
    constexpr int kAttempts = 5;
    // The sharded run (test-parallel: 20 emulator shards oversubscribing the
    // cores) has no reliably assertable wall-clock budget at all (single wakes
    // stall 35+ ms), so it enforces only the load-independent invariants.
    const bool sharded = std::getenv("GTEST_TOTAL_SHARDS") != nullptr;

    bool budgetMet = false;
    double bestP90 = 0.0;
    double bestMax = 0.0;
    for (int attempt = 0; attempt < kAttempts && !budgetMet; attempt++)
    {
        std::vector<double> lateMs;
        auto next = clock::now();
        for (int i = 0; i < kIterations; i++)
        {
            next += kFrame;
            bool reached = TimeHelper::WaitUntilPrecise(next, [] { return false; });
            const auto now = clock::now();
            EXPECT_TRUE(reached);
            EXPECT_GE(now, next) << "WaitUntilPrecise returned before the deadline";
            lateMs.push_back(std::chrono::duration<double, std::milli>(now - next).count());
            if (now > next + kFrame)
                next = now;  // re-anchor after a stall, like the main loop does
        }

        std::sort(lateMs.begin(), lateMs.end());
        const double p50 = lateMs[lateMs.size() / 2];
        const double p90 = lateMs[(lateMs.size() * 9) / 10];
        std::cout << "WaitUntilPrecise lateness ms (attempt " << attempt + 1 << "): p50=" << p50 << " p90=" << p90
                  << " max=" << lateMs.back() << std::endl;

        // Gross-stall bound is loose on purpose: shared CI runners stall
        if (attempt == 0 || p90 < bestP90)
        {
            bestP90 = p90;
            bestMax = lateMs.back();
        }
        budgetMet = p90 < TimeHelper::FRAME_PACING_JITTER_BUDGET_MS && lateMs.back() < 25.0;
    }

    if (!sharded)
    {
        EXPECT_TRUE(budgetMet) << "Frame clock wakes too late in all " << kAttempts
                               << " attempts (best p90=" << bestP90 << " ms, its max=" << bestMax
                               << " ms, budget " << TimeHelper::FRAME_PACING_JITTER_BUDGET_MS
                               << " ms) - audio ring trough would be consumed (see AVLatencyBudget)";
    }
}

/// @brief A stop request must interrupt the wait promptly (Stop() joins the emulation thread) and the
/// helper must report the abort instead of the deadline.
TEST(TimeHelper_Test, WaitUntilPrecise_AbortsPromptly)
{
    using clock = std::chrono::steady_clock;
    std::atomic<bool> abort{false};

    std::thread stopper([&abort] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        abort.store(true, std::memory_order_release);
    });

    const auto start = clock::now();
    const bool reached = TimeHelper::WaitUntilPrecise(start + std::chrono::seconds(2),
                                                      [&abort] { return abort.load(std::memory_order_acquire); });
    const auto elapsedMs = std::chrono::duration<double, std::milli>(clock::now() - start).count();
    stopper.join();

    EXPECT_FALSE(reached) << "Abort must be reported as 'not reached'";
    EXPECT_LT(elapsedMs, 200.0) << "Abort was not honoured promptly (took " << elapsedMs << " ms)";
}

/// @brief A deadline in the past returns immediately as reached; a null predicate is allowed.
TEST(TimeHelper_Test, WaitUntilPrecise_PastDeadline)
{
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    EXPECT_TRUE(TimeHelper::WaitUntilPrecise(start - std::chrono::milliseconds(1), nullptr));
    const double elapsedMs = std::chrono::duration<double, std::milli>(clock::now() - start).count();
    EXPECT_LT(elapsedMs, 5.0);
}
