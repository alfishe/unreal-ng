#pragma once

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

/// @brief Deterministic replacement for fixed `sleep_for` waits in tests.
///
/// A test that posts asynchronous work and then sleeps a fixed amount before
/// asserting is wrong in both directions: it wastes the whole interval when the
/// work lands immediately (the normal case), and it fails spuriously on a loaded
/// machine when the work takes longer than the guessed interval. Polling a
/// predicate fixes both - it returns as soon as the condition holds, and the
/// timeout only bounds the failure case.
///
/// Poll granularity is deliberately fine (250us). The waits guarded here settle
/// in well under a millisecond in practice, so a coarse 10ms tick would dominate
/// the measured time even though the work was already done.
namespace TestWait
{
inline constexpr std::chrono::milliseconds kDefaultTimeout{2000};
inline constexpr std::chrono::microseconds kDefaultPollInterval{250};

/// @brief Factor every wait's timeout is multiplied by: `UNREAL_TEST_TIME_SCALE` (default 1). Under valgrind the
/// tests run 20-50 times slower and a timeout sized for a native run reports a hang that is not there; a passing wait
/// returns as soon as its predicate holds, so a larger factor costs nothing on the normal path.
inline double TimeScale()
{
    static const double scale = [] {
        const char* v = std::getenv("UNREAL_TEST_TIME_SCALE");
        const double parsed = v ? std::atof(v) : 1.0;
        return parsed >= 1.0 ? parsed : 1.0;
    }();
    return scale;
}

/// @brief `timeout` times TimeScale()
inline std::chrono::milliseconds Scaled(std::chrono::milliseconds timeout)
{
    return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(timeout.count() * TimeScale()));
}

/// @brief Block until `predicate` returns true or `timeout` (times TimeScale()) elapses.
/// @return true if the predicate became true, false on timeout
template <typename Predicate>
bool For(Predicate predicate, std::chrono::milliseconds timeout = kDefaultTimeout,
         std::chrono::microseconds pollInterval = kDefaultPollInterval)
{
    const auto deadline = std::chrono::steady_clock::now() + Scaled(timeout);

    while (true)
    {
        if (predicate())
            return true;

        if (std::chrono::steady_clock::now() >= deadline)
            return predicate();  // final check - the last poll may have raced the deadline

        std::this_thread::sleep_for(pollInterval);
    }
}

/// @brief Wait until an atomic counter reaches at least `expected`.
template <typename T>
bool ForAtLeast(const std::atomic<T>& counter, T expected,
                std::chrono::milliseconds timeout = kDefaultTimeout)
{
    return For([&counter, expected] { return counter.load() >= expected; }, timeout);
}

/// @brief Wait for an expected count to be reached, then give any surplus work a
///        brief chance to arrive so that "exactly N" assertions stay meaningful.
///
/// Use where a test asserts an exact count and must also prove nothing extra was
/// delivered. The settle window only costs its full length when the expected
/// count never arrives, which is a failing test anyway.
template <typename T>
bool ForExactly(const std::atomic<T>& counter, T expected,
                std::chrono::milliseconds timeout = kDefaultTimeout,
                std::chrono::milliseconds settle = std::chrono::milliseconds(5))
{
    if (!ForAtLeast(counter, expected, timeout))
        return false;

    // Surplus deliveries would push the counter past `expected`; give them a
    // short, bounded window to show up rather than asserting on a racing value.
    std::this_thread::sleep_for(settle);
    return counter.load() == expected;
}
}  // namespace TestWait
