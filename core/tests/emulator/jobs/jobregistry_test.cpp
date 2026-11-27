// The job registry (PLAN #122 S2, core/src/emulator/jobs/jobregistry.h): lifecycle, lanes, cancel, retention,
// the long-poll watch and concurrent waiters. Each test has a registry of its own (not Instance) and counts its
// notifications instead of posting readouts.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "_helpers/testwaithelper.h"
#include "emulator/jobs/jobcontrol.h"
#include "emulator/jobs/jobregistry.h"

using namespace std::chrono_literals;

namespace
{
/// A gate the test opens: the work blocks on it (polling its cancel too)
struct Gate
{
    std::atomic<bool> open{false};
    std::atomic<int> entered{0};
};

jobs::Spec GatedSpec(const std::shared_ptr<Gate>& gate, const std::string& lane = "lane", bool cancellable = true)
{
    jobs::Spec spec;
    spec.kind = "test.gated";
    spec.emulatorId = "emu-1";
    spec.lane = lane;
    spec.summary = "gated";
    spec.cancellable = cancellable;
    spec.work = [gate](jobs::Context& context) {
        ++gate->entered;
        while (!gate->open.load() && !context.CancelRequested())
            std::this_thread::yield();
        jobs::Outcome outcome;
        outcome.cancelled = context.CancelRequested();
        outcome.text = outcome.cancelled ? "stopped" : "finished";
        outcome.result = StateNode::Object();
        outcome.result["value"] = uint64_t{42};
        return outcome;
    };
    return spec;
}

jobs::Spec InstantSpec(const std::string& text, size_t resultBytes = 0)
{
    jobs::Spec spec;
    spec.kind = "test.instant";
    spec.emulatorId = "emu-1";
    spec.summary = text;
    spec.work = [text, resultBytes](jobs::Context&) {
        jobs::Outcome outcome;
        outcome.text = text;
        outcome.result = StateNode::Object();
        outcome.result["data"] = std::string(resultBytes, 'x');
        return outcome;
    };
    return spec;
}

class JobRegistry_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _registry = std::make_unique<jobs::Registry>();
        _registry->SetNotifier([this](const std::string&) { ++_notified; });
    }
    void TearDown() override { _registry.reset(); }

    uint64_t Submit(jobs::Spec spec)
    {
        std::string error;
        const uint64_t id = _registry->Submit(std::move(spec), error);
        EXPECT_NE(id, 0u) << error;
        return id;
    }
    jobs::JobState StateOf(uint64_t id) const
    {
        const auto info = _registry->Get(id);
        return info ? info->state : jobs::JobState::Failed;
    }
    std::optional<jobs::Info> WaitEnd(uint64_t id) { return _registry->Wait(id, TestWait::Scaled(2000ms)); }

    std::unique_ptr<jobs::Registry> _registry;
    std::atomic<int> _notified{0};
};
}  // namespace

TEST_F(JobRegistry_Test, JobRunsAndEndsDoneWithItsResult)
{
    auto gate = std::make_shared<Gate>();
    const uint64_t id = Submit(GatedSpec(gate));
    ASSERT_TRUE(TestWait::For([&] { return gate->entered.load() == 1; }));
    EXPECT_EQ(StateOf(id), jobs::JobState::Running);
    gate->open = true;
    const auto info = WaitEnd(id);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, jobs::JobState::Done);
    EXPECT_EQ(info->outcome.text, "finished");
    ASSERT_NE(info->outcome.result.find("value"), nullptr);
    EXPECT_EQ(info->outcome.result.find("value")->i, 42);
    EXPECT_GT(info->startedMs, 0);
    EXPECT_GE(info->endedMs, info->startedMs);
    StateNode node = info->ToNode();
    EXPECT_EQ(node["state"].s, "done");
    EXPECT_EQ(node["kind"].s, "test.gated");
    EXPECT_NE(info->Text().find("done"), std::string::npos) << info->Text();
    // The waiter wakes before the ended notification goes out: wait for the third instead of reading it at once
    EXPECT_TRUE(TestWait::For([&] { return _notified.load() >= 3; })) << "queued, running and ended each tell the readouts";
}

TEST_F(JobRegistry_Test, OneLaneRunsInOrderLanesRunSideBySide)
{
    auto first = std::make_shared<Gate>();
    auto second = std::make_shared<Gate>();
    auto other = std::make_shared<Gate>();
    const uint64_t a = Submit(GatedSpec(first, "lane-a"));
    const uint64_t b = Submit(GatedSpec(second, "lane-a"));
    const uint64_t c = Submit(GatedSpec(other, "lane-b"));
    ASSERT_TRUE(TestWait::For([&] { return first->entered.load() == 1 && other->entered.load() == 1; }))
        << "the jobs of two lanes run at once";
    EXPECT_EQ(StateOf(b), jobs::JobState::Queued) << "the second of a lane waits for the first";
    EXPECT_EQ(_registry->Get(b)->queuePosition, 0u);
    EXPECT_EQ(second->entered.load(), 0);
    first->open = true;
    ASSERT_TRUE(TestWait::For([&] { return second->entered.load() == 1; }));
    EXPECT_EQ(StateOf(a), jobs::JobState::Done);
    second->open = true;
    other->open = true;
    EXPECT_EQ(WaitEnd(b)->state, jobs::JobState::Done);
    EXPECT_EQ(WaitEnd(c)->state, jobs::JobState::Done);
}

TEST_F(JobRegistry_Test, QueuedJobCancelledNeverRuns)
{
    auto blocker = std::make_shared<Gate>();
    auto queued = std::make_shared<Gate>();
    std::atomic<int> skipped{0};
    const uint64_t a = Submit(GatedSpec(blocker));
    jobs::Spec spec = GatedSpec(queued);
    spec.onSkipped = [&skipped] { ++skipped; };
    const uint64_t b = Submit(std::move(spec));
    ASSERT_TRUE(TestWait::For([&] { return blocker->entered.load() == 1; }));
    EXPECT_EQ(_registry->Cancel(b), jobs::Registry::CancelResult::Cancelled);
    EXPECT_EQ(StateOf(b), jobs::JobState::Cancelled);
    EXPECT_EQ(skipped.load(), 1);
    blocker->open = true;
    EXPECT_EQ(WaitEnd(a)->state, jobs::JobState::Done);
    EXPECT_EQ(queued->entered.load(), 0) << "a job cancelled while queued never runs";
    EXPECT_EQ(_registry->Cancel(b), jobs::Registry::CancelResult::Finished);
    EXPECT_EQ(_registry->Cancel(999), jobs::Registry::CancelResult::NotFound);
}

TEST_F(JobRegistry_Test, RunningJobIsAskedToStop)
{
    auto gate = std::make_shared<Gate>();
    std::atomic<int> told{0};
    jobs::Spec spec = GatedSpec(gate);
    spec.onCancel = [&told] { ++told; };
    const uint64_t id = Submit(std::move(spec));
    ASSERT_TRUE(TestWait::For([&] { return gate->entered.load() == 1; }));
    EXPECT_EQ(_registry->Cancel(id), jobs::Registry::CancelResult::Requested);
    const auto info = WaitEnd(id);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, jobs::JobState::Cancelled);
    EXPECT_TRUE(info->cancelRequested);
    EXPECT_EQ(info->outcome.text, "stopped");
    EXPECT_EQ(told.load(), 1);
}

TEST_F(JobRegistry_Test, WorkThatCannotStopRefusesTheCancel)
{
    auto gate = std::make_shared<Gate>();
    const uint64_t id = Submit(GatedSpec(gate, "lane", false));
    ASSERT_TRUE(TestWait::For([&] { return gate->entered.load() == 1; }));
    EXPECT_EQ(_registry->Cancel(id), jobs::Registry::CancelResult::NotCancellable);
    gate->open = true;
    EXPECT_EQ(WaitEnd(id)->state, jobs::JobState::Done);
}

TEST_F(JobRegistry_Test, FailureAndExceptionEndFailed)
{
    jobs::Spec refused = InstantSpec("x");
    refused.work = [](jobs::Context&) {
        jobs::Outcome outcome;
        outcome.ok = false;
        outcome.status = 409;
        outcome.error = "not now";
        return outcome;
    };
    jobs::Spec thrown = InstantSpec("y");
    thrown.work = [](jobs::Context&) -> jobs::Outcome { throw std::runtime_error("boom"); };
    const uint64_t a = Submit(std::move(refused));
    const uint64_t b = Submit(std::move(thrown));
    const auto ra = WaitEnd(a);
    const auto rb = WaitEnd(b);
    ASSERT_TRUE(ra && rb);
    EXPECT_EQ(ra->state, jobs::JobState::Failed);
    EXPECT_EQ(ra->outcome.status, 409);
    EXPECT_EQ(ra->outcome.error, "not now");
    EXPECT_EQ(rb->state, jobs::JobState::Failed);
    EXPECT_EQ(rb->outcome.status, 500);
    EXPECT_NE(rb->outcome.error.find("boom"), std::string::npos);
}

TEST_F(JobRegistry_Test, FinishedJobsAreBoundedByCount)
{
    jobs::Limits limits;
    limits.keepFinished = 2;
    _registry->SetLimits(limits);
    std::vector<uint64_t> ids;
    for (int i = 0; i < 4; ++i)
    {
        ids.push_back(Submit(InstantSpec("job " + std::to_string(i))));
        ASSERT_TRUE(WaitEnd(ids.back()).has_value());
    }
    EXPECT_FALSE(_registry->Get(ids[0]).has_value());
    EXPECT_FALSE(_registry->Get(ids[1]).has_value());
    EXPECT_TRUE(_registry->Get(ids[2]).has_value());
    EXPECT_TRUE(_registry->Get(ids[3]).has_value());
    EXPECT_EQ(_registry->List().size(), 2u);
}

TEST_F(JobRegistry_Test, FinishedJobsAreBoundedByAge)
{
    jobs::Limits limits;
    limits.keepMs = 1;
    _registry->SetLimits(limits);
    const uint64_t old = Submit(InstantSpec("old"));
    ASSERT_TRUE(WaitEnd(old).has_value());
    const auto ended = _registry->Get(old)->endedMs;
    // The wall clock moves past the age limit (a poll of the clock, not a sleep)
    ASSERT_TRUE(TestWait::For([&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count() > ended + 2;
    }));
    const uint64_t fresh = Submit(InstantSpec("fresh"));
    ASSERT_TRUE(WaitEnd(fresh).has_value());
    EXPECT_FALSE(_registry->Get(old).has_value()) << "a job ended longer ago than keepMs is dropped";
}

TEST_F(JobRegistry_Test, ResultsOverTheBudgetLoseTheirPayloadOldestFirst)
{
    jobs::Limits limits;
    limits.keepResultBytes = 6000;
    _registry->SetLimits(limits);
    const uint64_t a = Submit(InstantSpec("a", 4000));
    ASSERT_TRUE(WaitEnd(a).has_value());
    const uint64_t b = Submit(InstantSpec("b", 4000));
    ASSERT_TRUE(WaitEnd(b).has_value());
    const auto ra = _registry->Get(a);
    const auto rb = _registry->Get(b);
    ASSERT_TRUE(ra && rb);
    EXPECT_TRUE(ra->resultDropped);
    EXPECT_EQ(ra->outcome.text, "a") << "the text stays";
    EXPECT_FALSE(rb->resultDropped);
    ASSERT_NE(rb->outcome.result.find("data"), nullptr);
    EXPECT_EQ(rb->outcome.result.find("data")->s.size(), 4000u);
}

TEST_F(JobRegistry_Test, QueueIsBounded)
{
    jobs::Limits limits;
    limits.workers = 1;
    limits.maxQueued = 1;
    _registry->SetLimits(limits);
    auto gate = std::make_shared<Gate>();
    const uint64_t a = Submit(GatedSpec(gate));
    ASSERT_TRUE(TestWait::For([&] { return gate->entered.load() == 1; }));
    const uint64_t b = Submit(GatedSpec(gate));
    std::string error;
    EXPECT_EQ(_registry->Submit(GatedSpec(gate), error), 0u);
    EXPECT_FALSE(error.empty());
    gate->open = true;
    EXPECT_EQ(WaitEnd(a)->state, jobs::JobState::Done);
    EXPECT_EQ(WaitEnd(b)->state, jobs::JobState::Done);
}

TEST_F(JobRegistry_Test, WatchFiresOnceWhenTheJobEnds)
{
    auto gate = std::make_shared<Gate>();
    const uint64_t id = Submit(GatedSpec(gate));
    std::atomic<int> fired{0};
    std::atomic<int> state{-1};
    uint64_t token = 0;
    ASSERT_EQ(_registry->Watch(id, [&](const jobs::Info& info) { ++fired, state = static_cast<int>(info.state); }, token),
              jobs::Registry::WatchResult::Watching);
    EXPECT_EQ(_registry->WatchCount(), 1u);
    gate->open = true;
    ASSERT_TRUE(TestWait::ForAtLeast(fired, 1));
    EXPECT_EQ(state.load(), static_cast<int>(jobs::JobState::Done));
    EXPECT_FALSE(_registry->Unwatch(id, token)) << "the watch fired: nothing to take back";
    EXPECT_EQ(_registry->WatchCount(), 0u);
    uint64_t again = 0;
    EXPECT_EQ(_registry->Watch(id, [](const jobs::Info&) {}, again), jobs::Registry::WatchResult::Ended);
    EXPECT_EQ(_registry->Watch(999, [](const jobs::Info&) {}, again), jobs::Registry::WatchResult::NotFound);
    EXPECT_EQ(fired.load(), 1);
}

TEST_F(JobRegistry_Test, UnwatchedLongPollIsNotCalledAndWatchesAreBounded)
{
    jobs::Limits limits;
    limits.maxWatches = 1;
    _registry->SetLimits(limits);
    auto gate = std::make_shared<Gate>();
    const uint64_t id = Submit(GatedSpec(gate));
    std::atomic<int> fired{0};
    uint64_t token = 0;
    ASSERT_EQ(_registry->Watch(id, [&](const jobs::Info&) { ++fired; }, token), jobs::Registry::WatchResult::Watching);
    uint64_t other = 0;
    EXPECT_EQ(_registry->Watch(id, [&](const jobs::Info&) { ++fired; }, other), jobs::Registry::WatchResult::Busy);
    EXPECT_TRUE(_registry->Unwatch(id, token)) << "the timeout took the watch back";
    gate->open = true;
    ASSERT_EQ(WaitEnd(id)->state, jobs::JobState::Done);
    EXPECT_EQ(fired.load(), 0);
}

TEST_F(JobRegistry_Test, ConcurrentWaitersAllSeeTheEnd)
{
    auto gate = std::make_shared<Gate>();
    const uint64_t id = Submit(GatedSpec(gate));
    ASSERT_TRUE(TestWait::For([&] { return gate->entered.load() == 1; }));
    constexpr int kWaiters = 4;
    std::atomic<int> started{0};
    std::atomic<int> sawDone{0};
    std::vector<std::thread> waiters;
    for (int i = 0; i < kWaiters; ++i)
        waiters.emplace_back([&] {
            ++started;
            const auto info = _registry->Wait(id, TestWait::Scaled(2000ms));
            if (info && info->state == jobs::JobState::Done)
                ++sawDone;
        });
    ASSERT_TRUE(TestWait::For([&] { return started.load() == kWaiters; }));
    EXPECT_EQ(sawDone.load(), 0) << "no waiter returns before the job ends";
    gate->open = true;
    for (auto& t : waiters)
        t.join();
    EXPECT_EQ(sawDone.load(), kWaiters);
}

TEST_F(JobRegistry_Test, WaitTimesOutWithTheJobStillRunning)
{
    auto gate = std::make_shared<Gate>();
    const uint64_t id = Submit(GatedSpec(gate));
    ASSERT_TRUE(TestWait::For([&] { return gate->entered.load() == 1; }));
    const auto info = _registry->Wait(id, 1ms);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, jobs::JobState::Running);
    EXPECT_FALSE(_registry->Wait(999, 1ms).has_value());
    gate->open = true;
    EXPECT_EQ(WaitEnd(id)->state, jobs::JobState::Done);
}

TEST_F(JobRegistry_Test, ProgressIsStoredAtOnceAndToldThrottled)
{
    std::atomic<bool> reported{false};
    auto gate = std::make_shared<Gate>();
    jobs::Spec spec = GatedSpec(gate);
    spec.work = [gate, &reported](jobs::Context& context) {
        EXPECT_EQ(jobs::Current(), &context) << "the thread's current job";
        for (uint64_t i = 1; i <= 2000; ++i)
            jobs::CurrentProgress(i, 4000, "frames");
        reported = true;
        while (!gate->open.load())
            std::this_thread::yield();
        return jobs::Outcome{};
    };
    const int before = _notified.load();
    const uint64_t id = Submit(std::move(spec));
    ASSERT_TRUE(TestWait::For([&] { return reported.load(); }));
    const auto info = _registry->Get(id);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->done, 2000u);
    EXPECT_EQ(info->total, 4000u);
    EXPECT_EQ(info->unit, "frames");
    EXPECT_EQ(info->Percent(), 50u);
    EXPECT_LT(_notified.load() - before, 20) << "2000 reports, a handful of notifications";
    EXPECT_EQ(jobs::Current(), nullptr) << "not a job's thread";
    gate->open = true;
    EXPECT_EQ(WaitEnd(id)->state, jobs::JobState::Done);
}

TEST_F(JobRegistry_Test, CancelInstanceEndsItsQueuedAndStopsItsRunning)
{
    auto running = std::make_shared<Gate>();
    auto queued = std::make_shared<Gate>();
    auto otherInstance = std::make_shared<Gate>();
    const uint64_t a = Submit(GatedSpec(running));
    const uint64_t b = Submit(GatedSpec(queued));
    jobs::Spec other = GatedSpec(otherInstance, "other");
    other.emulatorId = "emu-2";
    const uint64_t c = Submit(std::move(other));
    ASSERT_TRUE(TestWait::For([&] { return running->entered.load() == 1 && otherInstance->entered.load() == 1; }));
    _registry->CancelInstance("emu-1");
    EXPECT_EQ(WaitEnd(a)->state, jobs::JobState::Cancelled);
    EXPECT_EQ(StateOf(b), jobs::JobState::Cancelled);
    EXPECT_EQ(StateOf(c), jobs::JobState::Running) << "another instance's job runs on";
    otherInstance->open = true;
    EXPECT_EQ(WaitEnd(c)->state, jobs::JobState::Done);
    EXPECT_EQ(_registry->List("emu-2").size(), 1u);
}

TEST_F(JobRegistry_Test, CancelInstanceNeverStartsAQueuedJobOfTheLaneItFrees)
{
    auto running = std::make_shared<Gate>();
    auto queued = std::make_shared<Gate>();
    queued->open = true;  // if it does start, it ends at once
    uint64_t a = 0;
    uint64_t b = 0;
    jobs::Spec spec = GatedSpec(running);
    // Holds the removal right after the running job is asked to stop, until that job ended and its lane is free: the
    // moment a worker would pick the next job up
    spec.onCancel = [this, &a, &b] {
        (void)_registry->Wait(a, TestWait::Scaled(2000ms));
        (void)TestWait::For([&] { return StateOf(b) != jobs::JobState::Queued; }, 100ms);
    };
    a = Submit(std::move(spec));
    b = Submit(GatedSpec(queued));
    ASSERT_TRUE(TestWait::For([&] { return running->entered.load() == 1; }));
    _registry->CancelInstance("emu-1");
    EXPECT_EQ(StateOf(b), jobs::JobState::Cancelled);
    EXPECT_EQ(queued->entered.load(), 0);
}

TEST_F(JobRegistry_Test, DestroyedRegistryEndsQueuedJobs)
{
    jobs::Limits limits;
    limits.workers = 1;
    _registry->SetLimits(limits);
    auto running = std::make_shared<Gate>();
    auto queued = std::make_shared<Gate>();
    std::atomic<int> skipped{0};
    Submit(GatedSpec(running));
    jobs::Spec spec = GatedSpec(queued, "lane-2");
    spec.onSkipped = [&skipped] { ++skipped; };
    Submit(std::move(spec));
    ASSERT_TRUE(TestWait::For([&] { return running->entered.load() == 1; }));
    _registry.reset();  // the running job is asked to stop (its gate polls the cancel), the queued one skipped
    EXPECT_EQ(skipped.load(), 1);
    EXPECT_EQ(queued->entered.load(), 0);
}

// jobs::Execute, the verbs every plane calls: refusals and the shapes of list / status / cancel
TEST(JobControl_Test, ActionsRefuseBadInput)
{
    EXPECT_EQ(jobs::Execute(nullptr, "nope").error, jobs::Error::BadRequest);
    EXPECT_EQ(jobs::Execute(nullptr, "status").error, jobs::Error::BadRequest) << "job is required";
    EXPECT_EQ(jobs::Execute(nullptr, "status", {{"job", "abc"}}).error, jobs::Error::BadRequest);
    EXPECT_EQ(jobs::Execute(nullptr, "status", {{"job", "999999999"}}).error, jobs::Error::NotFound);
    EXPECT_EQ(jobs::Execute(nullptr, "wait", {{"job", "1"}, {"wait_ms", "60001"}}).error, jobs::Error::BadRequest)
        << "the long-poll cap is refused, not clamped";
    EXPECT_EQ(jobs::Execute(nullptr, "list", {{"bogus", "1"}}).error, jobs::Error::BadRequest);
    EXPECT_EQ(jobs::Execute(nullptr, "list", {{"state", "sleeping"}}).error, jobs::Error::BadRequest);
    EXPECT_EQ(jobs::Execute(nullptr, "start", {{"domain", "ttd"}, {"verb", "dump"}}).error, jobs::Error::NotFound)
        << "a job runs on an instance";
    EXPECT_EQ(jobs::Execute(nullptr, "start", {{"domain", "disk"}}).error, jobs::Error::BadRequest);
    bool async = false;
    EXPECT_TRUE(jobs::ParseAsync("", async) && async);
    EXPECT_TRUE(jobs::ParseAsync("off", async) && !async);
    EXPECT_FALSE(jobs::ParseAsync("maybe", async));
}

TEST(JobControl_Test, StatusListAndCancelOfARegistryJob)
{
    std::string error;
    auto gate = std::make_shared<Gate>();
    jobs::Spec spec = GatedSpec(gate, "jobcontrol-test-lane");
    spec.emulatorId = "jobcontrol-test-instance";
    const uint64_t id = jobs::Registry::Instance().Submit(std::move(spec), error);
    ASSERT_NE(id, 0u) << error;
    ASSERT_TRUE(TestWait::For([&] { return gate->entered.load() == 1; }));
    const std::string job = std::to_string(id);

    jobs::Reply list = jobs::Execute(nullptr, "list", {{"instance", "jobcontrol-test-instance"}});
    ASSERT_TRUE(list.Ok()) << list.message;
    EXPECT_EQ(list.body["count"].i, 1);
    ASSERT_EQ(list.body["jobs"].items.size(), 1u);
    EXPECT_EQ(list.body["jobs"].items[0]["job"].i, static_cast<int64_t>(id));
    EXPECT_NE(list.text.find(job), std::string::npos) << list.text;
    EXPECT_EQ(jobs::ProgressTopic("jobcontrol-test-instance")["count"].i, 1);

    jobs::Reply wait = jobs::Execute(nullptr, "wait", {{"job", job}, {"wait_ms", "1"}});
    ASSERT_TRUE(wait.Ok());
    EXPECT_FALSE(wait.body["ended"].b);
    EXPECT_NE(wait.text.find("wait again"), std::string::npos) << wait.text;

    jobs::Reply cancel = jobs::Execute(nullptr, "cancel", {{"job", "#" + job}});
    ASSERT_TRUE(cancel.Ok()) << cancel.message;
    jobs::Reply status = jobs::Execute(nullptr, "wait", {{"job", job}, {"wait_ms", "2000"}});
    ASSERT_TRUE(status.Ok());
    EXPECT_EQ(status.body["state"].s, "cancelled");
    EXPECT_TRUE(status.body["ended"].b);
    EXPECT_EQ(jobs::DoneTopic("jobcontrol-test-instance")["last_job"].i, static_cast<int64_t>(id));
    EXPECT_EQ(jobs::Execute(nullptr, "cancel", {{"job", job}}).error, jobs::Error::Conflict) << "it ended already";
}

// CLI `jobs`: its words give the action and options jobs::Execute takes (cli-jobs.h)
#include "../../../automation/cli/src/commands/cli-jobs.h"

TEST(JobControl_Test, CliWordsGiveTheActionAndOptions)
{
    std::string action;
    std::map<std::string, std::string> options;
    bool json = false;
    std::string error;
    ASSERT_TRUE(CliJobs::ParseArgs({}, action, options, json, error));
    EXPECT_EQ(action, "list");
    options.clear();
    ASSERT_TRUE(CliJobs::ParseArgs({"wait", "12", "60000", "--json"}, action, options, json, error));
    EXPECT_EQ(action, "wait");
    EXPECT_EQ(options["job"], "12");
    EXPECT_EQ(options["wait_ms"], "60000");
    EXPECT_TRUE(json);
    options.clear();
    ASSERT_TRUE(CliJobs::ParseArgs({"start", "ttd", "dump", "path=/tmp/a.ttd"}, action, options, json, error));
    EXPECT_EQ(options["domain"], "ttd");
    EXPECT_EQ(options["verb"], "dump");
    EXPECT_EQ(options["path"], "/tmp/a.ttd");
    options.clear();
    ASSERT_TRUE(CliJobs::ParseArgs({"list", "all", "state=active"}, action, options, json, error));
    EXPECT_EQ(options["instance"], "all");
    EXPECT_FALSE(CliJobs::ParseArgs({"sleep"}, action, options, json, error));
    EXPECT_FALSE(CliJobs::ParseArgs({"cancel", "1", "2"}, action, options, json, error));
}
