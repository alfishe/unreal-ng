/// @file timehelper_functional_test.cpp
/// @brief TimeHelper::WaitUntilPrecise against the real steady clock: it never returns before the deadline. Functional:
/// UNREAL_FUNCTIONAL_TESTS=1, filter *_Functional.* (core/tests/README.md, The functional suite)

#include <gtest/gtest.h>

#include <chrono>

#include "_helpers/testtier.h"
#include "common/timehelper.h"

TEST(TimeHelper_Functional, WaitUntilPrecise_NeverReturnsBeforeTheDeadline)
{
    SKIP_UNLESS_FUNCTIONAL();
    using clock = std::chrono::steady_clock;
    // Two chunks of the wait (it sleeps in chunks of at most 4 ms), two frames in a row
    auto next = clock::now();
    for (int i = 0; i < 2; i++)
    {
        next += std::chrono::microseconds(5000);
        const bool reached = TimeHelper::WaitUntilPrecise(next, [] { return false; });
        EXPECT_TRUE(reached);
        EXPECT_GE(clock::now(), next) << "WaitUntilPrecise returned before the deadline";
    }
}
