// The execution arbiter against the real clock: a throttle window and a deadline are measured in elapsed time.
// Functional: UNREAL_FUNCTIONAL_TESTS=1, filter *_Functional.* (core/tests/README.md, Functional tests). The arbitration
// rules that do not depend on time are in emulator/execution/executionarbiter_test.cpp

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "_helpers/executionarbitertesthelpers.h"
#include "_helpers/testtier.h"
#include "_helpers/testthread.h"
#include "_helpers/testwaithelper.h"
#include "emulator/execution/executionarbiter.h"

using namespace execution;
using namespace arbitertest;
using namespace std::chrono_literals;

TEST(ExecutionArbiter_Functional, ThrottleWindowGathersIntentsWhileRunning)
{
    SKIP_UNLESS_FUNCTIONAL();
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    FakeLoop loop(arbiter, monitor);
    ASSERT_EQ(arbiter.SubmitIntent(Intent::Run, kGui).Wait(1s).outcome, Outcome::Applied);
    int edges = 0;
    arbiter.SetEdgeListener([&](const IntentEdge&) { ++edges; });
    arbiter.SetThrottle(20ms);  // within the 50 ms test budget

    Ticket flap1 = arbiter.SubmitIntent(Intent::Pause, kGui);
    Ticket flap2 = arbiter.SubmitIntent(Intent::Run, kGui);
    EXPECT_FALSE(flap1.Done()) << "gathered for the window";
    EXPECT_EQ(flap1.Wait(1s).outcome, Outcome::Coalesced);
    EXPECT_EQ(flap2.Wait(1s).outcome, Outcome::Applied);
    EXPECT_EQ(edges, 0) << "a pause-run flap inside the window is no change";
    EXPECT_EQ(arbiter.CurrentIntent(), Intent::Run);
}

TEST(ExecutionArbiter_Functional, DeadlinePassedIsBusyAndNamesTheHolder)
{
    SKIP_UNLESS_FUNCTIONAL();
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    Gate gate;
    TestThread holder([&]() { arbiter.Execute(kTtd, "ttd.journal", gate.Body(monitor)); }, [&] { gate.open = true; });
    ASSERT_TRUE(TestWait::For([&]() { return gate.entered.load(); }));
    const Result r = arbiter.Execute(kApi, "poke", [](CommandContext&) {}, {}, Clock::now() + 5ms);
    EXPECT_EQ(r.outcome, Outcome::Busy);
    EXPECT_NE(r.detail.find("ttd.journal"), std::string::npos) << r.detail;
    EXPECT_TRUE(arbiter.GetSnapshot().queue.empty()) << "withdrawn";
    gate.open = true;
    holder.Join();
}

