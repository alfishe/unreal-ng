#include "pch.h"

#include "common/timehelper.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

// TimeHelper::WaitUntilPrecise is the emulation thread's frame clock. How late it wakes is a speed of the host
// and of the OS scheduler, not a property of the code: it is measured by the benchmark
// core/benchmarks/common/timehelper_benchmark.cpp (BM_TimeHelper_WakeLateness, against FRAME_PACING_JITTER_BUDGET_MS),
// and no test here asserts on elapsed time. The tests pin the contract that holds on any host: it ends on the abort
// predicate and on the WakeSignal (that it never returns before the deadline is measured on the real clock:
// functional/common/timehelper_functional_test.cpp).

/// @brief An abort request ends the wait (Stop() joins the emulation thread) and the helper reports it instead of the
/// deadline. The predicate is polled by the wait itself: it says "abort" on its second poll, the deadline is an hour away
TEST(TimeHelper_Test, WaitUntilPrecise_AbortsOnThePredicate)
{
    using clock = std::chrono::steady_clock;
    int polls = 0;
    const bool reached = TimeHelper::WaitUntilPrecise(clock::now() + std::chrono::hours(1), [&polls] { return ++polls >= 2; });

    EXPECT_FALSE(reached) << "Abort must be reported as 'not reached'";
    EXPECT_GE(polls, 2);
}

/// @brief A deadline in the past returns as reached; a null predicate is allowed.
TEST(TimeHelper_Test, WaitUntilPrecise_PastDeadline)
{
    using clock = std::chrono::steady_clock;
    EXPECT_TRUE(TimeHelper::WaitUntilPrecise(clock::now() - std::chrono::milliseconds(1), nullptr));
}

/// @brief A WakeSignal notified before the wait is kept for it (the wait is not slept through): the main loop's wait
/// between frames gives the CPU to a park asked for meanwhile (a snapshot load). With the notification pending the first
/// poll is the predicate's, and it aborts: no thread, no timing
TEST(TimeHelper_Test, WaitUntilPrecise_AKeptWakeSignalReachesThePredicate)
{
    using clock = std::chrono::steady_clock;
    WakeSignal wake;
    wake.Notify();
    std::atomic<bool> abort{true};
    const bool reached = TimeHelper::WaitUntilPrecise(
        clock::now() + std::chrono::hours(1), [&] { return abort.load(std::memory_order_acquire); }, &wake);
    EXPECT_FALSE(reached);
}
