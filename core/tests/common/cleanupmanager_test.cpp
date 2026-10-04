// CleanupManager (common/cleanupmanager.h): due steps by interval with the
// last run kept in a state file, every step isolated from the others'
// failures, a background run that start-up never waits for, and a stop that
// a running step sees.

#include <gtest/gtest.h>

#include <atomic>
#include <stdexcept>
#include <string>

#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "common/cleanupmanager.h"
#include "common/filehelper.h"

namespace
{
    struct Manager
    {
        CleanupManager manager;
        int64_t now = 1'000'000;

        explicit Manager(const std::string& stateName)
        {
            const std::string state = TestPathHelper::GetUniqueTestScratchPath(stateName);
            std::remove(state.c_str());
            manager.SetStatePath(state);
            manager.SetClock([this]() { return now; });
        }
    };

    CleanupStep CountingStep(const std::string& name, int& runs)
    {
        CleanupStep step;
        step.name = name;
        step.interval = std::chrono::hours(24 * 7);
        step.run = [&runs](CleanupContext& context) {
            ++runs;
            context.Removed("item");
        };
        return step;
    }
}  // namespace

/// A step runs when it never ran, then not again until its interval passed
/// (the last run survives in the state file, as across emulator starts)
TEST(CleanupManager_Test, AStepRunsOncePerInterval)
{
    Manager m("cleanup-interval.txt");
    int runs = 0;
    m.manager.AddStep(CountingStep("a", runs));

    auto reports = m.manager.RunNow();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_TRUE(reports[0].ran);
    EXPECT_TRUE(reports[0].completed);
    EXPECT_EQ(reports[0].removed.size(), 1u);

    m.now += 6 * 24 * 3600;   // six days later: not due
    reports = m.manager.RunNow();
    EXPECT_FALSE(reports[0].ran);
    EXPECT_EQ(runs, 1);

    // A new manager (the next start) reads the same state
    CleanupManager next;
    next.SetStatePath(TestPathHelper::GetUniqueTestScratchPath("cleanup-interval.txt"));
    int64_t later = m.now + 2 * 24 * 3600;   // eight days after the first run: due
    next.SetClock([&later]() { return later; });
    next.AddStep(CountingStep("a", runs));
    reports = next.RunNow();
    EXPECT_TRUE(reports[0].ran);
    EXPECT_EQ(runs, 2);
}

/// A step that throws is reported and does not stop the next one; its run is
/// not recorded, so it is tried again at the next start
TEST(CleanupManager_Test, AFailingStepDoesNotStopTheOthers)
{
    Manager m("cleanup-failing.txt");
    CleanupStep bad;
    bad.name = "bad";
    bad.run = [](CleanupContext&) { throw std::runtime_error("disk gone"); };
    int runs = 0;
    m.manager.AddStep(bad);
    m.manager.AddStep(CountingStep("good", runs));

    auto reports = m.manager.RunNow();
    ASSERT_EQ(reports.size(), 2u);
    EXPECT_TRUE(reports[0].ran);
    EXPECT_FALSE(reports[0].completed);
    EXPECT_EQ(reports[0].error, "disk gone");
    EXPECT_TRUE(reports[1].completed);
    EXPECT_EQ(runs, 1);

    reports = m.manager.RunNow();
    EXPECT_TRUE(reports[0].ran) << "not recorded as done: due again";
    EXPECT_FALSE(reports[1].ran);
}

/// RunAsync returns while the step still runs; Stop() is seen by the step and waits for it
TEST(CleanupManager_Test, RunsInTheBackgroundAndStops)
{
    Manager m("cleanup-async.txt");
    std::atomic<bool> started{false};
    std::atomic<bool> sawStop{false};
    CleanupStep slow;
    slow.name = "slow";
    slow.run = [&](CleanupContext& context) {
        started = true;
        TestWait::For([&context]() { return context.StopRequested(); });
        sawStop = context.StopRequested();
    };
    m.manager.AddStep(slow);

    m.manager.RunAsync();
    ASSERT_TRUE(TestWait::For([&started]() { return started.load(); }));
    m.manager.Stop();
    EXPECT_TRUE(sawStop);
    const auto reports = m.manager.LastReports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_FALSE(reports[0].completed) << "stopped early: due again next time";
}

/// The same name replaces a step instead of adding a second one
TEST(CleanupManager_Test, AddingTheSameNameReplaces)
{
    Manager m("cleanup-replace.txt");
    int first = 0, second = 0;
    m.manager.AddStep(CountingStep("x", first));
    m.manager.AddStep(CountingStep("x", second));
    EXPECT_EQ(m.manager.RunNow().size(), 1u);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
}
