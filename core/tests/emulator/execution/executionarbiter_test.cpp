// The execution arbiter without a machine (executionarbiter.h; docs/inprogress/2026-10-09-execution-arbiter/design.md
// §3, §8.5): one rule per test, a fake main loop where the rule needs one

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "_helpers/executionarbitertesthelpers.h"
#include "_helpers/testthread.h"
#include "_helpers/testwaithelper.h"
#include "emulator/execution/executionarbiter.h"

using namespace execution;
using namespace arbitertest;
using namespace std::chrono_literals;


TEST(ExecutionArbiter_Test, ForeignPauseSurvivesCommandEnd)
{
    // The bug: a park-mutate-restore on one thread, the automation's pause and run_frames on another
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    FakeLoop loop(arbiter, monitor);
    ASSERT_EQ(arbiter.SubmitIntent(Intent::Run, kGui).Wait(1s).outcome, Outcome::Applied);

    Gate journal;
    TestThread adopt([&]() { arbiter.Execute(kTtd, "ttd.journal", journal.Body(monitor)); },
                     [&] { journal.open = true; });
    ASSERT_TRUE(TestWait::For([&]() { return journal.entered.load(); }));

    Ticket pause = arbiter.SubmitIntent(Intent::Pause, kApi);
    std::atomic<int> frames{0};
    std::thread automation([&]() {
        CommandOptions options;
        options.drivesZ80 = true;
        arbiter.Execute(kApi, "run_frames", [&](CommandContext&) {
            for (int i = 0; i < 50; ++i)
            {
                monitor.Enter();
                frames.fetch_add(1);
                monitor.Leave();
            }
        }, options);
    });

    journal.open = true;
    adopt.Join();
    automation.join();
    EXPECT_EQ(pause.Wait(1s).outcome, Outcome::Applied);
    EXPECT_EQ(frames.load(), 50);
    EXPECT_EQ(arbiter.CurrentIntent(), Intent::Pause) << "the end of the journal command lifted the pause";
    EXPECT_TRUE(arbiter.WaitParked(Clock::now() + 1s));
    EXPECT_EQ(monitor.violations.load(), 0) << "two drivers";
}

TEST(ExecutionArbiter_Test, IntentsCoalesceBehindACommandAndTheLastWins)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    std::vector<IntentEdge> edges;
    arbiter.SetEdgeListener([&](const IntentEdge& e) { edges.push_back(e); });
    Gate gate;
    TestThread holder([&]() { arbiter.Execute(kTtd, "hold", gate.Body(monitor)); }, [&] { gate.open = true; });
    ASSERT_TRUE(TestWait::For([&]() { return gate.entered.load(); }));

    Ticket a = arbiter.SubmitIntent(Intent::Run, kGui);
    Ticket b = arbiter.SubmitIntent(Intent::Pause, kApi);
    Ticket c = arbiter.SubmitIntent(Intent::Run, kGui);
    EXPECT_FALSE(a.Done());
    gate.open = true;
    holder.Join();

    EXPECT_EQ(a.Wait(1s).outcome, Outcome::Coalesced);
    EXPECT_EQ(b.Wait(1s).outcome, Outcome::Coalesced);
    const Result last = c.Wait(1s);
    EXPECT_EQ(last.outcome, Outcome::Applied);
    EXPECT_EQ(last.intent, Intent::Run);
    EXPECT_EQ(a.Peek().intent, Intent::Run) << "a coalesced ticket returns the intent that won";
    ASSERT_EQ(edges.size(), 1u) << "one edge, not three";
    EXPECT_EQ(edges[0].intent, Intent::Run);
}

TEST(ExecutionArbiter_Test, ACommandIsABarrier)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    arbiter.SetDecisionLog(true);
    Gate gate;
    TestThread holder([&]() { arbiter.Execute(kTtd, "hold", gate.Body(monitor)); }, [&] { gate.open = true; });
    ASSERT_TRUE(TestWait::For([&]() { return gate.entered.load(); }));

    Ticket run = arbiter.SubmitIntent(Intent::Run, kGui);
    Ticket reset = arbiter.Post(kApi, "reset", [](CommandContext&) {});
    Ticket pause = arbiter.SubmitIntent(Intent::Pause, kGui);
    gate.open = true;
    holder.Join();
    ASSERT_EQ(reset.Wait(1s).outcome, Outcome::Applied);
    EXPECT_EQ(run.Wait(1s).outcome, Outcome::Applied) << "not coalesced across the reset";
    EXPECT_EQ(pause.Wait(1s).outcome, Outcome::Applied);

    // The order in the log: Run applied, reset started and finished, Pause applied
    std::vector<std::string> order;
    for (const Decision& d : arbiter.TakeDecisions())
        if (d.kind == Decision::Kind::IntentApplied || d.kind == Decision::Kind::CommandStarted)
            order.push_back(d.tag);
    const std::vector<std::string> expected{"hold", "run", "reset", "pause"};
    EXPECT_EQ(order, expected);
}

TEST(ExecutionArbiter_Test, RunDuringADrivingCommandStartsTheLoopAfterIt)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    FakeLoop loop(arbiter, monitor);
    Gate step;
    CommandOptions options;
    options.drivesZ80 = true;
    options.pauseFirst = true;
    TestThread stepper([&]() { arbiter.Execute(kApi, "run_frames", step.Body(monitor), options); },
                       [&] { step.open = true; });
    ASSERT_TRUE(TestWait::For([&]() { return step.entered.load(); }));

    Ticket run = arbiter.SubmitIntent(Intent::Run, kGui);
    const uint64_t before = loop.Instructions();
    EXPECT_FALSE(run.Done());
    EXPECT_EQ(loop.Instructions(), before) << "the loop ran during the step";
    step.open = true;
    stepper.Join();
    EXPECT_EQ(run.Wait(1s).outcome, Outcome::Applied);
    EXPECT_TRUE(TestWait::For([&]() { return loop.Instructions() > before; }));
    EXPECT_EQ(monitor.violations.load(), 0);
}

TEST(ExecutionArbiter_Test, PauseFromTheDriverSupersedesQueuedIntents)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    FakeLoop loop(arbiter, monitor);
    ASSERT_EQ(arbiter.SubmitIntent(Intent::Run, kGui).Wait(1s).outcome, Outcome::Applied);
    arbiter.SetThrottle(1000ms);
    Ticket queued = arbiter.SubmitIntent(Intent::Run, kApi);

    std::atomic<bool> fired{false};
    loop.hook = [&]() {
        arbiter.PauseFromDriver(Requester{Origin::Debugger, {}}, "breakpoint");
        fired = true;
    };
    loop.hookPending = true;
    ASSERT_TRUE(TestWait::For([&]() { return fired.load(); }));
    EXPECT_EQ(queued.Wait(1s).outcome, Outcome::Coalesced);
    EXPECT_EQ(arbiter.CurrentIntent(), Intent::Pause);
    EXPECT_TRUE(arbiter.WaitParked(Clock::now() + 1s));
    EXPECT_EQ(arbiter.GetSnapshot().intentReason, "breakpoint");
}

TEST(ExecutionArbiter_Test, AbortEndsALongDrivingCommand)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    CommandOptions options;
    options.drivesZ80 = true;
    std::atomic<bool> running{false};
    Result result;
    TestThread stepper([&]() {  // without the abort, the body's wait times out
        result = arbiter.Execute(kApi, "run_frames 1500", [&](CommandContext& ctx) {
            running = true;
            TestWait::For([&]() { return ctx.IsAborted(); });
        }, options);
    });
    ASSERT_TRUE(TestWait::For([&]() { return running.load(); }));
    EXPECT_TRUE(arbiter.AbortCurrent(kGdb, "interrupt"));
    stepper.Join();
    EXPECT_EQ(result.outcome, Outcome::Aborted);
    EXPECT_FALSE(arbiter.AbortCurrent(kGdb, "interrupt")) << "nothing served now";
}

TEST(ExecutionArbiter_Test, StopRefusesTheQueueAndAbortsTheServedCommand)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    FakeLoop loop(arbiter, monitor);
    Gate gate;
    Result held;
    TestThread holder([&]() { held = arbiter.Execute(kTtd, "hold", gate.Body(monitor)); }, [&] { loop.Stop(); });
    ASSERT_TRUE(TestWait::For([&]() { return gate.entered.load(); }));
    Ticket queuedIntent = arbiter.SubmitIntent(Intent::Run, kGui);
    Ticket queuedCommand = arbiter.Post(kApi, "media", [](CommandContext&) {});

    loop.Stop();
    holder.Join();
    EXPECT_EQ(held.outcome, Outcome::Aborted);
    EXPECT_EQ(queuedIntent.Wait(1s).outcome, Outcome::Refused);
    EXPECT_EQ(queuedCommand.Wait(1s).outcome, Outcome::Refused);
    EXPECT_EQ(arbiter.CurrentPhase(), Phase::Stopped);
    // Stopped: nothing drives, a command runs at once
    EXPECT_EQ(arbiter.Execute(kApi, "save", [](CommandContext&) {}).outcome, Outcome::Applied);
}

TEST(ExecutionArbiter_Test, ClaimRefusesRunAndSteppingFromOthersButNotPause)
{
    ExecutionArbiter arbiter;
    ASSERT_TRUE(arbiter.TakeClaim(kGdb));
    EXPECT_FALSE(arbiter.TakeClaim(kDezog));
    EXPECT_EQ(arbiter.SubmitIntent(Intent::Run, kGui).Wait(1s).outcome, Outcome::Refused);
    CommandOptions driving;
    driving.drivesZ80 = true;
    EXPECT_EQ(arbiter.Execute(kApi, "step", [](CommandContext&) {}, driving).outcome, Outcome::Refused);
    EXPECT_EQ(arbiter.Execute(kApi, "media", [](CommandContext&) {}).outcome, Outcome::Applied);
    EXPECT_EQ(arbiter.SubmitIntent(Intent::Pause, kGui).Wait(1s).outcome, Outcome::Applied);
    EXPECT_EQ(arbiter.SubmitIntent(Intent::Run, kGdb).Wait(1s).outcome, Outcome::Applied);
    arbiter.ReleaseClaim(kGdb);
    EXPECT_EQ(arbiter.SubmitIntent(Intent::Pause, kGui).Wait(1s).outcome, Outcome::Applied);
    EXPECT_EQ(arbiter.SubmitIntent(Intent::Run, kGui).Wait(1s).outcome, Outcome::Applied);
}

TEST(ExecutionArbiter_Test, OwnerOnlyRunLiftsOnlyItsOwnPause)
{
    ExecutionArbiter arbiter;
    IntentOptions ownOnly;
    ownOnly.onlyIfOwner = true;
    ASSERT_EQ(arbiter.SubmitIntent(Intent::Run, kGui).Wait(1s).outcome, Outcome::Applied);

    // The user's pause: DeZog's close does not lift it
    ASSERT_EQ(arbiter.SubmitIntent(Intent::Pause, kGui).Wait(1s).outcome, Outcome::Applied);
    EXPECT_EQ(arbiter.SubmitIntent(Intent::Run, kDezog, ownOnly).Wait(1s).outcome, Outcome::Refused);
    EXPECT_EQ(arbiter.CurrentIntent(), Intent::Pause);

    // DeZog's own pause: lifted. A second pause by someone else does not take the ownership over
    ASSERT_EQ(arbiter.SubmitIntent(Intent::Run, kGui).Wait(1s).outcome, Outcome::Applied);
    ASSERT_EQ(arbiter.SubmitIntent(Intent::Pause, kDezog).Wait(1s).outcome, Outcome::Applied);
    ASSERT_EQ(arbiter.SubmitIntent(Intent::Pause, kGui).Wait(1s).outcome, Outcome::Applied);
    EXPECT_EQ(arbiter.SubmitIntent(Intent::Run, kDezog, ownOnly).Wait(1s).outcome, Outcome::Applied);
    EXPECT_EQ(arbiter.CurrentIntent(), Intent::Run);
}

TEST(ExecutionArbiter_Test, CommandsInsideCommandsRunInline)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    FakeLoop loop(arbiter, monitor);
    std::vector<int> order;
    const Result outer = arbiter.Execute(kApi, "snapshot.load", [&](CommandContext&) {
        order.push_back(1);
        EXPECT_TRUE(arbiter.HoldsCpu());
        const Result inner = arbiter.Execute(kApi, "reset", [&](CommandContext&) { order.push_back(2); });
        EXPECT_EQ(inner.outcome, Outcome::Applied);
        order.push_back(3);
    });
    EXPECT_EQ(outer.outcome, Outcome::Applied);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(ExecutionArbiter_Test, ACommandFromTheLoopThreadRunsInline)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    FakeLoop loop(arbiter, monitor);
    ASSERT_EQ(arbiter.SubmitIntent(Intent::Run, kGui).Wait(1s).outcome, Outcome::Applied);
    std::atomic<bool> ranOnLoop{false};
    std::atomic<bool> done{false};
    loop.hook = [&]() {
        const Result r = arbiter.Execute(kTtd, "frame task", [&](CommandContext&) {
            ranOnLoop = std::this_thread::get_id() == loop.Id();
        });
        done = r.outcome == Outcome::Applied;
    };
    loop.hookPending = true;
    ASSERT_TRUE(TestWait::For([&]() { return done.load(); }));
    EXPECT_TRUE(ranOnLoop.load());
}

TEST(ExecutionArbiter_Test, WaitingOnOwnQueuedRequestIsBusyNotADeadlock)
{
    ExecutionArbiter arbiter;
    Result waited;
    arbiter.Execute(kApi, "outer", [&](CommandContext&) {
        Ticket t = arbiter.SubmitIntent(Intent::Run, kApi);
        waited = t.Wait(1s);
    });
    EXPECT_EQ(waited.outcome, Outcome::Busy);
    EXPECT_NE(waited.detail.find("self-wait"), std::string::npos);
    EXPECT_EQ(arbiter.CurrentIntent(), Intent::Run) << "still applied after the outer command";
}

TEST(ExecutionArbiter_Test, PostedCommandRunsOnTheParkedLoop)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    FakeLoop loop(arbiter, monitor);
    std::atomic<bool> onLoop{false};
    Ticket t = arbiter.Post(kApi, "media.insert", [&](CommandContext&) {
        onLoop = std::this_thread::get_id() == loop.Id();
    });
    EXPECT_EQ(t.Wait(1s).outcome, Outcome::Applied);
    EXPECT_TRUE(onLoop.load());
}

TEST(ExecutionArbiter_Test, BeforeStartCommandsRunAtOnceAndPostedOnThePoster)
{
    ExecutionArbiter arbiter;
    EXPECT_EQ(arbiter.CurrentPhase(), Phase::NotStarted);
    EXPECT_EQ(arbiter.CurrentIntent(), Intent::Pause);
    bool ran = false;
    EXPECT_EQ(arbiter.Execute(kApi, "media", [&](CommandContext&) { ran = true; }).outcome, Outcome::Applied);
    EXPECT_TRUE(ran);
    std::thread::id where;
    EXPECT_EQ(arbiter.Post(kApi, "posted", [&](CommandContext&) { where = std::this_thread::get_id(); })
                  .Wait(1s)
                  .outcome,
              Outcome::Applied);
    EXPECT_EQ(where, std::this_thread::get_id());
    EXPECT_TRUE(arbiter.IsParked());
}

TEST(ExecutionArbiter_Test, PairedIntentFollowsTheCommandWithNothingBetween)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    arbiter.SetDecisionLog(true);
    Gate gate;
    TestThread holder([&]() { arbiter.Execute(kTtd, "hold", gate.Body(monitor)); }, [&] { gate.open = true; });
    ASSERT_TRUE(TestWait::For([&]() { return gate.entered.load(); }));
    CommandOptions loadAndRun;
    loadAndRun.thenIntent = Intent::Run;
    Ticket load = arbiter.Post(kGui, "autostart", [](CommandContext&) {}, loadAndRun);
    Ticket pause = arbiter.SubmitIntent(Intent::Pause, kApi);
    gate.open = true;
    holder.Join();
    EXPECT_EQ(load.Wait(1s).outcome, Outcome::Applied);
    EXPECT_EQ(pause.Wait(1s).outcome, Outcome::Applied);
    EXPECT_EQ(arbiter.CurrentIntent(), Intent::Pause) << "the pause submitted after the pair wins";
    bool runApplied = false;
    for (const Decision& d : arbiter.TakeDecisions())
        if (d.kind == Decision::Kind::IntentApplied && d.tag == "autostart" && d.intentAfter == Intent::Run)
            runApplied = true;
    EXPECT_TRUE(runApplied);
}

TEST(ExecutionArbiter_Test, SnapshotShowsServedAndQueued)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    Gate gate;
    TestThread holder([&]() { arbiter.Execute(kTtd, "ttd.journal", gate.Body(monitor)); }, [&] { gate.open = true; });
    ASSERT_TRUE(TestWait::For([&]() { return gate.entered.load(); }));
    arbiter.SubmitIntent(Intent::Run, kGui);
    const Snapshot s = arbiter.GetSnapshot();
    ASSERT_TRUE(s.serving.has_value());
    EXPECT_EQ(s.serving->tag, "ttd.journal");
    EXPECT_EQ(s.serving->requester.origin, Origin::Ttd);
    ASSERT_EQ(s.queue.size(), 1u);
    EXPECT_FALSE(s.queue[0].command);
    EXPECT_FALSE(s.mayRun);
    gate.open = true;
    holder.Join();
}

TEST(ExecutionArbiter_Test, HoldIsTheSilentPairAndKeepsAForeignPause)
{
    // The compat form of Pause(false) ... Resume(false): the bug once more, with holds
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    FakeLoop loop(arbiter, monitor);
    ASSERT_EQ(arbiter.SubmitIntent(Intent::Run, kGui).Wait(1s).outcome, Outcome::Applied);
    HoldToken journal = arbiter.BeginHold(kTtd, "ttd.journal", Clock::now() + 1s);
    ASSERT_TRUE(journal.Valid());
    EXPECT_TRUE(arbiter.IsParked());
    EXPECT_EQ(arbiter.CurrentIntent(), Intent::Run) << "a hold does not change the intent";
    const uint64_t frozen = loop.Instructions();

    Ticket pause = arbiter.SubmitIntent(Intent::Pause, kApi);
    EXPECT_EQ(pause.Wait(Clock::now() + 2ms).outcome, Outcome::Busy) << "this thread holds: a self-wait";
    Result fromOther;
    std::thread other([&]() { fromOther = pause.Wait(Clock::now() + 2ms); });
    other.join();
    EXPECT_EQ(fromOther.outcome, Outcome::Pending) << "waits behind the hold, not lost";
    EXPECT_NE(fromOther.detail.find("ttd.journal"), std::string::npos) << fromOther.detail;
    EXPECT_EQ(loop.Instructions(), frozen);
    arbiter.EndHold(journal);
    EXPECT_EQ(pause.Wait(1s).outcome, Outcome::Applied);
    EXPECT_EQ(arbiter.CurrentIntent(), Intent::Pause);
    EXPECT_EQ(monitor.violations.load(), 0);
}

TEST(ExecutionArbiter_Test, NestedHoldsAndHoldsFromTheLoopAreInline)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    FakeLoop loop(arbiter, monitor);
    HoldToken outer = arbiter.BeginHold(kTtd, "outer", Clock::now() + 1s);
    ASSERT_TRUE(outer.Valid());
    HoldToken inner = arbiter.BeginHold(kTtd, "inner", Clock::now() + 1s);
    ASSERT_TRUE(inner.Valid());
    EXPECT_TRUE(inner.inlined);
    arbiter.EndHold(inner);
    EXPECT_TRUE(arbiter.HoldsCpu());
    arbiter.EndHold(outer);
    EXPECT_FALSE(arbiter.HoldsCpu());

    ASSERT_EQ(arbiter.SubmitIntent(Intent::Run, kGui).Wait(1s).outcome, Outcome::Applied);
    std::atomic<bool> inlined{false};
    loop.hook = [&]() {
        HoldToken t = arbiter.BeginHold(kTtd, "frame hook", Clock::now() + 1s);
        inlined = t.inlined;
        arbiter.EndHold(t);
    };
    loop.hookPending = true;
    ASSERT_TRUE(TestWait::For([&]() { return inlined.load(); }));
}

TEST(ExecutionArbiter_Test, HoldEndedByAnotherThreadAndAbortedByStop)
{
    ExecutionArbiter arbiter;
    DriveMonitor monitor;
    FakeLoop loop(arbiter, monitor);
    HoldToken hold = arbiter.BeginHold(kGui, "model switch", Clock::now() + 1s);
    ASSERT_TRUE(hold.Valid());
    loop.Stop();  // the loop exits although the hold is served
    EXPECT_EQ(arbiter.CurrentPhase(), Phase::Stopped);
    std::thread other([&]() { arbiter.EndHold(hold); });
    other.join();
    EXPECT_FALSE(arbiter.GetSnapshot().serving.has_value());
    EXPECT_EQ(arbiter.Execute(kApi, "after", [](CommandContext&) {}).outcome, Outcome::Applied);
}

TEST(ExecutionArbiter_Test, AnExpectedLoopCountsAsParkedUntilItRuns)
{
    ExecutionArbiter arbiter;
    arbiter.ExpectMainLoop();
    ASSERT_EQ(arbiter.SubmitIntent(Intent::Run, Requester{Origin::Lifecycle, {}}).Wait(1s).outcome, Outcome::Applied);
    EXPECT_TRUE(arbiter.IsParked()) << "the thread does not exist yet";
    EXPECT_FALSE(arbiter.ShouldYield());
    // A command granted now runs before the loop's first instruction
    EXPECT_EQ(arbiter.Execute(kApi, "media", [](CommandContext&) {}).outcome, Outcome::Applied);
    std::atomic<bool> ran{false};
    std::thread loop([&]() {
        arbiter.AttachMainLoop();
        if (arbiter.LoopWait())
            ran = true;
        arbiter.BeginStop();
        arbiter.LoopWait();
        arbiter.DetachMainLoop();
    });
    loop.join();
    EXPECT_TRUE(ran.load());
    EXPECT_EQ(arbiter.CurrentPhase(), Phase::Stopped);
}
