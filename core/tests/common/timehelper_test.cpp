#include "pch.h"

#include "common/timehelper.h"

#include <algorithm>
#include <atomic>
#include <chrono>
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
    constexpr int kIterations = 60;
    const auto kFrame = std::chrono::microseconds(20480);  // Pentagon frame

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
    std::cout << "WaitUntilPrecise lateness ms: p50=" << p50 << " p90=" << p90 << " max=" << lateMs.back() << std::endl;

    EXPECT_LT(p90, TimeHelper::FRAME_PACING_JITTER_BUDGET_MS)
        << "Frame clock wakes too late - audio ring trough would be consumed (see AVLatencyBudget)";
    EXPECT_LT(lateMs.back(), 25.0) << "Gross wake-up stall (bound is loose on purpose: shared CI runners stall)";
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
