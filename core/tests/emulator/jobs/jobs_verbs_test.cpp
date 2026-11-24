// The long verbs as jobs (PLAN #122 S2): TTD dump / memory-series and the analysis verbs with async=true answer a
// job at once (HTTP 202); the job ends with the answer the verb gives without async. A registered instance (the job
// looks it up in EmulatorManager and leases its context), removed at the end.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "debugger/analyzers/analysis/analysiscontrol.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/jobs/jobcontrol.h"
#include "emulator/jobs/jobregistry.h"

using namespace std::chrono_literals;

class JobsVerbs_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_NE(_context->pTimeTravelController, nullptr);
    }

    void TearDown() override
    {
        if (_context && _context->pTimeTravelController)
            _context->pTimeTravelController->StopRecording();
        EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    ttd::TTDReply Run(const std::string& verb, std::map<std::string, std::string> options = {})
    {
        return ttd::TTDControl(_context).Execute({verb, std::move(options)});
    }

    void Record(int frames)
    {
        ASSERT_TRUE(Run("start").Ok());
        _emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
        ASSERT_TRUE(Run("stop").Ok());
    }

    static uint64_t JobOf(const StateNode& body)
    {
        const StateNode* job = body.find("job");
        return job != nullptr ? static_cast<uint64_t>(job->i) : 0;
    }
};

TEST_F(JobsVerbs_Test, AsyncDumpAnswersAJobThatEndsWithTheDump)
{
    Record(3);
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("jobs-async-dump.ttd");
    const ttd::TTDReply r = Run("dump", {{"path", path}, {"async", "true"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(r.accepted);
    EXPECT_EQ(r.HttpStatus(), 202);
    const uint64_t id = JobOf(r.body);
    ASSERT_NE(id, 0u);
    ASSERT_NE(r.body.find("status_url"), nullptr);
    EXPECT_NE(r.body.find("text")->s.find(std::to_string(id)), std::string::npos) << "the text names the job";

    const auto info = jobs::Registry::Instance().Wait(id, TestWait::Scaled(5000ms));
    ASSERT_TRUE(info.has_value());
    ASSERT_EQ(info->state, jobs::JobState::Done) << info->outcome.error;
    EXPECT_EQ(info->kind, "ttd.dump");
    EXPECT_EQ(info->emulatorId, _emulator->GetId());
    EXPECT_FALSE(info->cancellable) << "a dump runs to its end";
    const StateNode* bytes = info->outcome.result.find("bytes");
    ASSERT_NE(bytes, nullptr);
    EXPECT_GT(bytes->i, 0);
    EXPECT_TRUE(std::filesystem::exists(path));
    std::remove(path.c_str());
}

TEST_F(JobsVerbs_Test, AsyncMemorySeriesHoldsTheSameAnswerAsTheSyncCall)
{
    Record(4);
    const std::map<std::string, std::string> options = {{"ranges", "0x5C00:4"}, {"format", "hex"}};
    const ttd::TTDReply sync = Run("memory-series", options);
    ASSERT_TRUE(sync.Ok()) << sync.message;

    std::map<std::string, std::string> asyncOptions = options;
    asyncOptions["async"] = "1";
    const ttd::TTDReply r = Run("memory-series", asyncOptions);
    ASSERT_TRUE(r.Ok()) << r.message;
    ASSERT_TRUE(r.accepted);
    const jobs::Reply waited =
        jobs::Execute(nullptr, "wait", {{"job", std::to_string(JobOf(r.body))}, {"wait_ms", "5000"}});
    ASSERT_TRUE(waited.Ok()) << waited.message;
    EXPECT_EQ(waited.body.find("state")->s, "done");
    const StateNode* result = waited.body.find("result");
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->find("rows"), nullptr);
    EXPECT_EQ(result->find("rows")->items.size(), sync.body.find("rows")->items.size());
    EXPECT_TRUE(jobs::Registry::Instance().Get(JobOf(r.body))->cancellable) << "memory-series stops on a cancel";
}

TEST_F(JobsVerbs_Test, AsyncIsRefusedOnShortVerbsAndBadValues)
{
    EXPECT_EQ(Run("status", {{"async", "true"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("dump", {{"path", "/x.ttd"}, {"async", "perhaps"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_TRUE(Run("status", {{"async", "false"}}).Ok()) << "async=false: the call as before";
    EXPECT_EQ(analysis::Execute(*_emulator, "status", {{"async", "true"}}).error, analysis::Error::BadRequest);
}

TEST_F(JobsVerbs_Test, JobsStartRunsAVerbOfEitherDomain)
{
    Record(3);
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("jobs-start-dump.ttd");
    const jobs::Reply started =
        jobs::Execute(_emulator, "start", {{"domain", "ttd"}, {"verb", "dump"}, {"path", path}});
    ASSERT_TRUE(started.Ok()) << started.message;
    EXPECT_EQ(started.HttpStatus(), 202);
    const uint64_t id = JobOf(started.body);
    const auto info = jobs::Registry::Instance().Wait(id, TestWait::Scaled(5000ms));
    ASSERT_TRUE(info && info->state == jobs::JobState::Done);
    std::remove(path.c_str());

    // The analysis domain: a refusal of the verb comes back at once (no memory session)
    const jobs::Reply refused =
        jobs::Execute(_emulator, "start", {{"domain", "analysis"}, {"verb", "explain"}, {"address", "x"}});
    EXPECT_FALSE(refused.Ok());

    // The instance's jobs list has the dump
    const jobs::Reply list = jobs::Execute(_emulator, "list", {{"instance", "this"}});
    ASSERT_TRUE(list.Ok());
    bool found = false;
    for (const StateNode& job : list.body.find("jobs")->items)
        found = found || static_cast<uint64_t>(job.find("job")->i) == id;
    EXPECT_TRUE(found) << list.text;
}

TEST_F(JobsVerbs_Test, AnAsyncAnalysisTraceIsAJobOfTheAnalysisLane)
{
    Record(2);
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("jobs-trace.json");
    const analysis::Reply r = analysis::Execute(*_emulator, "trace", {{"path", path}, {"async", "true"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.HttpStatus(), 202);
    const uint64_t id = JobOf(r.body);
    const auto info = jobs::Registry::Instance().Wait(id, TestWait::Scaled(5000ms));
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(jobs::IsFinal(info->state));
    EXPECT_EQ(info->kind, "analysis.trace");
    // Done or failed as the sync trace would be: the job carries the same answer either way
    const analysis::Reply sync = analysis::Execute(*_emulator, "trace", {{"path", path}});
    EXPECT_EQ(info->state == jobs::JobState::Done, sync.Ok()) << info->outcome.error << " / " << sync.message;
    std::remove(path.c_str());
}

TEST_F(JobsVerbs_Test, RemovingTheInstanceEndsItsQueuedJobs)
{
    Record(3);
    // A job queued behind a gated one of the same lane: the removal cancels it before it runs
    auto open = std::make_shared<std::atomic<bool>>(false);
    auto entered = std::make_shared<std::atomic<bool>>(false);
    jobs::Spec blocker;
    blocker.kind = "test.blocker";
    blocker.emulatorId = _emulator->GetId();
    blocker.lane = "ttd@" + _emulator->GetId();
    blocker.work = [open, entered](jobs::Context& context) {
        *entered = true;
        while (!open->load() && !context.CancelRequested())
            std::this_thread::yield();
        return jobs::Outcome{};
    };
    std::string error;
    const uint64_t first = jobs::Registry::Instance().Submit(std::move(blocker), error);
    ASSERT_NE(first, 0u) << error;
    ASSERT_TRUE(TestWait::For([&] { return entered->load(); }));
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("jobs-removed.ttd");
    const ttd::TTDReply r = Run("dump", {{"path", path}, {"async", "true"}});
    ASSERT_TRUE(r.accepted) << r.message;
    const uint64_t queued = JobOf(r.body);
    EXPECT_EQ(jobs::Registry::Instance().Get(queued)->state, jobs::JobState::Queued);

    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;
    _context = nullptr;
    EXPECT_EQ(jobs::Registry::Instance().Get(queued)->state, jobs::JobState::Cancelled);
    const auto ended = jobs::Registry::Instance().Wait(first, TestWait::Scaled(2000ms));
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->state, jobs::JobState::Done) << "the running one was asked and stopped";
    EXPECT_FALSE(std::filesystem::exists(path));
}
