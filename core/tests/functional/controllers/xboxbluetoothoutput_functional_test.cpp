/// @file xboxbluetoothoutput_functional_test.cpp
/// @brief The RumbleWriter's write interval against the real steady clock (the writer waits out the interval on it).
/// Functional: UNREAL_FUNCTIONAL_TESTS=1, filter *_Functional.* (core/tests/README.md, Functional tests). The rest of the
/// writer's behaviour (interval 0) is in controllers/xboxbluetoothoutput_test.cpp

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

#include "_helpers/testtier.h"
#include "_helpers/testwaithelper.h"
#include "hid/drivers/xboxbluetoothoutput.h"

using namespace controllers;

TEST(XboxBluetoothOutput_Functional, Writer_WritesAnIntervalApart)
{
    SKIP_UNLESS_FUNCTIONAL();
    constexpr auto kInterval = std::chrono::milliseconds(15);
    std::mutex mutex;
    std::vector<std::chrono::steady_clock::time_point> at;
    RumbleWriter writer(
        [&](const RumbleLevels&) {
            std::lock_guard<std::mutex> lock(mutex);
            at.push_back(std::chrono::steady_clock::now());
            return true;
        },
        kInterval);
    RumbleLevels levels;
    levels.low = 0.3f;
    writer.Set(levels);
    ASSERT_TRUE(TestWait::For([&] { return writer.Written() == 1; }));
    levels.low = 0.7f;
    writer.Set(levels);
    ASSERT_TRUE(TestWait::For([&] { return writer.Written() == 2; }));
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(at.size(), 2u);
    EXPECT_GE(at[1] - at[0], kInterval);
}
