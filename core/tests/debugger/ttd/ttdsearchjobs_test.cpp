// find-last and reverse-continue as jobs (RQ-1, docs/inprogress/2026-10-28-ttd-analysis-feedback/
// replay-queries-2026-11-27.md): with async=true the search answers a job at once, the job ends with the answer the
// call gives without async, its progress counts the frames of the window, and a cancel ends the walk at a frame
// boundary with the hits so far, the machine and the session as they stood.
//
// The program of ttdanalysisqueries_test.cpp records 30 frames on a Pentagon: 0x8100 is written with a new value on
// every loop pass (about 20 passes a frame), 0x8101 is set to 5 every pass, 0x8103 flips every 16 passes, every 64
// passes the routine at 0x8200 runs, every pass reads 0x0000 in the ROM.
//
// The cancel "in the middle" is deterministic without a hook: the test's own job (a jobs::Spec in the instance's TTD
// lane) waits on its worker for the cancel, then runs the verb there, as a verb's job does. The walk never stops
// before its first frame, so it walks exactly one frame and ends cancelled. The cancel of a verb's own job while it
// walks (TTDControl's job, the registry's cancel) is checked live: tools/verification/mcp/test_search_jobs.py.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/jobs/jobcontrol.h"
#include "emulator/jobs/jobregistry.h"
#include "emulator/memory/memory.h"

using namespace std::chrono_literals;

namespace
{
constexpr unsigned kFrames = 30;
using Options = std::map<std::string, std::string>;

class TTDSearchJobs_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kDebugMode, true);
        _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _emulator->EnableTurboMode();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);

        Memory* memory = _context->pMemory;
        const std::vector<uint8_t> loop = {
            0xF3,                    // 8000 di
            0x0E, 0x00,              // 8001 ld c,0
            0x3E, 0x11,              // 8003 ld a,#11
            0x32, 0xF0, 0x80,        // 8005 ld (#80F0),a
            0x06, 0x00,              // 8008 loop: ld b,0
            0x10, 0xFE,              // 800A djnz $ (about 3,300 T)
            0x0C,                    // 800C inc c
            0x79,                    // 800D ld a,c
            0x32, 0x00, 0x81,        // 800E ld (#8100),a      every pass a new value
            0x3E, 0x05,              // 8011 ld a,5
            0x32, 0x01, 0x81,        // 8013 ld (#8101),a      always 5
            0x79,                    // 8016 ld a,c
            0x0F, 0x0F, 0x0F, 0x0F,  // 8017 rrca x4
            0xE6, 0x01,              // 801B and 1
            0x32, 0x03, 0x81,        // 801D ld (#8103),a      flips every 16 passes
            0x3A, 0x00, 0x00,        // 8020 ld a,(#0000)      a ROM read
            0x79,                    // 8023 ld a,c
            0xE6, 0x3F,              // 8024 and #3F
            0xCC, 0x00, 0x82,        // 8026 call z,#8200
            0x18, 0xDD,              // 8029 jr loop
        };
        const std::vector<uint8_t> routine = {
            0x00,                    // 8200 nop
            0xC9,                    // 8201 ret
        };
        for (size_t i = 0; i < loop.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), loop[i]);
        for (size_t i = 0; i < routine.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8200 + i), routine[i]);
        for (uint16_t a : {0x8100, 0x8101, 0x8103})
            memory->DirectWriteToZ80Memory(a, 0);
        _context->pCore->GetZ80()->pc = 0x8000;
        _context->pCore->GetZ80()->sp = 0xBF00;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _ttd->StopRecording();
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
    }

    /// A stopped recording of kFrames frames, the machine at its end
    void Record()
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(kFrames, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        ToEnd();
    }

    void ToEnd() { ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition())); }

    ttd::TTDReply Run(const std::string& verb, Options options = {})
    {
        return ttd::TTDControl(_context).Execute({verb, std::move(options)});
    }

    /// The verb as a job, waited for: its final info (the test fails when it was not queued or did not end)
    jobs::Info RunJob(const std::string& verb, Options options)
    {
        options["async"] = "true";
        const ttd::TTDReply accepted = Run(verb, std::move(options));
        EXPECT_TRUE(accepted.Ok()) << accepted.message;
        EXPECT_TRUE(accepted.accepted);
        EXPECT_EQ(accepted.HttpStatus(), 202);
        const StateNode* job = accepted.body.find("job");
        if (job == nullptr)
            return {};
        const auto info = jobs::Registry::Instance().Wait(static_cast<uint64_t>(job->i), TestWait::Scaled(10000ms));
        EXPECT_TRUE(info.has_value());
        return info.value_or(jobs::Info{});
    }

    /// The verb run by a job that was asked to stop before its walk began: the job's final info. The work waits for
    /// the cancel on its worker, then calls the verb as TTDControl's own job does (the reply is the result, a
    /// cancelled reply ends the job cancelled)
    jobs::Info RunCancelled(const std::string& verb, const Options& options)
    {
        auto entered = std::make_shared<std::atomic<bool>>(false);
        EmulatorContext* context = _context;
        jobs::Spec spec;
        spec.kind = "test." + verb;
        spec.emulatorId = _emulator->GetId();
        spec.lane = "ttd@" + _emulator->GetId();
        spec.work = [entered, context, verb, options](jobs::Context& job) {
            *entered = true;
            while (!job.CancelRequested())
                std::this_thread::yield();
            const ttd::TTDReply reply = ttd::TTDControl(context).Execute({verb, options});
            jobs::Outcome outcome;
            outcome.ok = reply.Ok();
            outcome.status = reply.HttpStatus();
            outcome.error = reply.message;
            outcome.result = reply.ToValue();
            outcome.cancelled = Flag(reply.body, "cancelled");
            return outcome;
        };
        std::string error;
        const uint64_t id = jobs::Registry::Instance().Submit(std::move(spec), error);
        EXPECT_NE(id, 0u) << error;
        EXPECT_TRUE(TestWait::For([&] { return entered->load(); }));
        EXPECT_EQ(jobs::Registry::Instance().Cancel(id), jobs::Registry::CancelResult::Requested);
        const auto info = jobs::Registry::Instance().Wait(id, TestWait::Scaled(10000ms));
        EXPECT_TRUE(info.has_value());
        return info.value_or(jobs::Info{});
    }

    static std::string Json(const StateNode& value) { return jobs::ResultText(200, value, SIZE_MAX); }

    static uint64_t U64(const StateNode& body, const char* name)
    {
        const StateNode* node = body.find(name);
        EXPECT_NE(node, nullptr) << name;
        return node != nullptr ? static_cast<uint64_t>(node->i) : 0;
    }

    static bool Flag(const StateNode& body, const char* name)
    {
        const StateNode* node = body.find(name);
        return node != nullptr && node->b;
    }

    /// The hits' machine times, in the answer's order
    static std::vector<uint64_t> HitTimes(const StateNode& body)
    {
        std::vector<uint64_t> times;
        if (const StateNode* hits = body.find("hits"))
            for (const StateNode& hit : hits->items)
                times.push_back(U64(hit, "t"));
        return times;
    }

    /// The job's answer is the synchronous call's, from the same position, and both leave the machine at the same
    /// moment
    void ExpectJobEqualsSync(const std::string& verb, const Options& options)
    {
        SCOPED_TRACE(jobs::Summary(verb, options));
        ToEnd();
        const ttd::TTDReply sync = Run(verb, options);
        ASSERT_TRUE(sync.Ok()) << sync.message;
        const ttd::TTDTimePoint afterSync = _ttd->CurrentPosition();

        ToEnd();
        const jobs::Info job = RunJob(verb, options);
        ASSERT_EQ(job.state, jobs::JobState::Done) << job.outcome.error;
        EXPECT_EQ(job.kind, "ttd." + verb);
        EXPECT_TRUE(job.cancellable);
        EXPECT_EQ(job.outcome.status, 200);
        EXPECT_EQ(Json(job.outcome.result), Json(sync.ToValue()));
        EXPECT_EQ(_ttd->CurrentPosition(), afterSync);
        EXPECT_LE(job.done, job.total);
    }
};

// The queries that never match walk the whole window; the others end at their hit or their list
const Options kReadNever = {{"addr", "0"}, {"access", "read"}, {"value", "0x77"}};          // every frame replayed
const Options kConditionNever = {{"pcs", "0x800C"}, {"all", "true"}, {"condition", "FRAME > 100000000"}};
}  // namespace

TEST_F(TTDSearchJobs_Test, FindLastAsAJobAnswersAsTheCall)
{
    Record();
    ExpectJobEqualsSync("find-last", {{"addr", "0"}, {"access", "read"}});                    // a read: the newest
    ExpectJobEqualsSync("find-last", kReadNever);                                             // a read: none
    ExpectJobEqualsSync("find-last", {{"addr", "0x8200"}, {"access", "execute"}});            // an instruction start
    ExpectJobEqualsSync("find-last", {{"addr", "0x8200"}, {"access", "execute"}, {"all", "true"}});
    ExpectJobEqualsSync("find-last", {{"addr", "0x8103"}});                                   // a write, no journal
    ExpectJobEqualsSync("find-last", {{"addr", "0x8100"}, {"all", "true"}, {"limit", "50"}});   // cut at the limit
    ExpectJobEqualsSync("find-last", {{"addr", "0x8103"}, {"all", "true"}, {"changes_only", "true"}});
    ExpectJobEqualsSync("find-last", {{"addr", "0x8101"}, {"changes_only", "true"}});
    ExpectJobEqualsSync("find-last", {{"addr", "0x8100"}, {"all", "true"}, {"from_frame", "20"}, {"limit", "1000"}});
}

TEST_F(TTDSearchJobs_Test, ReverseContinueAsAJobAnswersAsTheCall)
{
    Record();
    ExpectJobEqualsSync("reverse-continue", {{"pcs", "0x8200"}, {"all", "true"}});
    ExpectJobEqualsSync("reverse-continue", {{"pcs", "0x8200,0x800C,0x9000"}, {"first_per_pc", "true"}});
    ExpectJobEqualsSync("reverse-continue", kConditionNever);
    ExpectJobEqualsSync("reverse-continue", {{"pcs", "0x800C"}, {"all", "true"}, {"condition", "C == 7"}});
    // The single forms are the same walk ending at its first hit: the job moves the machine as the call does
    ExpectJobEqualsSync("reverse-continue", {{"pcs", "0x8200"}, {"condition", "1"}});
    ExpectJobEqualsSync("reverse-continue", {{"pcs", "0x8200"}});
    ExpectJobEqualsSync("reverse-continue", {{"pcs", "0x9000"}});
}

TEST_F(TTDSearchJobs_Test, ProgressCountsTheFramesOfTheWindow)
{
    Record();
    for (const auto& [verb, options] : std::vector<std::pair<std::string, Options>>{
             {"find-last", kReadNever}, {"reverse-continue", kConditionNever}})
    {
        SCOPED_TRACE(verb);
        ToEnd();
        const jobs::Info job = RunJob(verb, options);
        ASSERT_EQ(job.state, jobs::JobState::Done) << job.outcome.error;
        EXPECT_EQ(job.unit, "frames");
        EXPECT_EQ(job.done, job.total) << "the walk reached the session's start";
        EXPECT_GE(job.total, uint64_t{kFrames - 1});
        EXPECT_LE(job.total, uint64_t{kFrames + 1});
        EXPECT_TRUE(job.hasHits);
        EXPECT_EQ(job.hits, 0u);
        const StateNode node = job.ToNode();
        ASSERT_NE(node.find("progress"), nullptr);
        EXPECT_EQ(U64(*node.find("progress"), "hits"), 0u);
        EXPECT_EQ(U64(*node.find("progress"), "percent"), 100u);
    }

    // A window from a frame on: its frames only; a list reports its hits
    ToEnd();
    const jobs::Info list =
        RunJob("find-last", {{"addr", "0x8100"}, {"all", "true"}, {"from_frame", "20"}, {"limit", "1000"}});
    ASSERT_EQ(list.state, jobs::JobState::Done) << list.outcome.error;
    EXPECT_EQ(list.done, list.total);
    EXPECT_GE(list.total, 10u);
    EXPECT_LE(list.total, 12u);
    EXPECT_EQ(list.hits, U64(list.outcome.result, "count"));
    EXPECT_GT(list.hits, 100u);
}

TEST_F(TTDSearchJobs_Test, ACancelledFindLastKeepsItsHitsAndTheMachine)
{
    Record();
    // Back from inside the last whole frame, so the first frame the walk covers holds writes
    const Options everyWrite = {{"addr", "0x8100"},
                                {"all", "true"},
                                {"limit", "10000"},
                                {"before_frame", std::to_string(_ttd->SessionEndPosition().frame - 1)},
                                {"before_tin", "60000"}};
    const ttd::TTDReply whole = Run("find-last", everyWrite);
    ASSERT_TRUE(whole.Ok()) << whole.message;
    const std::vector<uint64_t> wholeTimes = HitTimes(whole.body);
    ASSERT_GT(wholeTimes.size(), 400u);

    const ttd::TTDTimePoint before = _ttd->CurrentPosition();
    const uint16_t pcBefore = _context->pCore->GetZ80()->pc;
    const std::string stateBefore = Run("status").body.find("state")->s;
    const jobs::Info job = RunCancelled("find-last", everyWrite);
    ASSERT_EQ(job.state, jobs::JobState::Cancelled) << job.outcome.error;
    EXPECT_EQ(job.done, 1u) << "the walk stops before its second frame";
    EXPECT_GE(job.total, uint64_t{kFrames - 1});
    EXPECT_EQ(job.outcome.status, 200);

    // What it got: the hits of the frame it walked, where it stopped, how to go on
    const StateNode& result = job.outcome.result;
    EXPECT_TRUE(Flag(result, "cancelled"));
    EXPECT_TRUE(Flag(result, "truncated"));
    const std::vector<uint64_t> got = HitTimes(result);
    ASSERT_FALSE(got.empty());
    ASSERT_LT(got.size(), wholeTimes.size());
    EXPECT_EQ(job.hits, got.size());
    EXPECT_EQ(U64(result, "count"), got.size());
    EXPECT_EQ(U64(result, "covered_to"), U64(whole.body, "covered_to"));
    EXPECT_GT(U64(result, "covered_from"), U64(whole.body, "covered_from"));

    // The machine and the session as before the call
    EXPECT_EQ(_ttd->CurrentPosition(), before);
    EXPECT_EQ(_context->pCore->GetZ80()->pc, pcBefore);
    EXPECT_EQ(Run("status").body.find("state")->s, stateBefore);

    // The rest from next_before, synchronously: both parts are the whole list, no hit lost or twice
    Options rest = everyWrite;
    rest.erase("before_frame");
    rest.erase("before_tin");
    rest["before"] = std::to_string(U64(result, "next_before"));
    const ttd::TTDReply tail = Run("find-last", rest);
    ASSERT_TRUE(tail.Ok()) << tail.message;
    std::vector<uint64_t> joined = got;
    const std::vector<uint64_t> tailTimes = HitTimes(tail.body);
    joined.insert(joined.end(), tailTimes.begin(), tailTimes.end());
    EXPECT_EQ(joined, wholeTimes);
    EXPECT_EQ(Json(Run("find-last", everyWrite).ToValue()), Json(whole.ToValue())) << "the same query again";
}

TEST_F(TTDSearchJobs_Test, ACancelledSingleFindLastFindsNothingAndStays)
{
    Record();
    const ttd::TTDTimePoint before = _ttd->CurrentPosition();
    const jobs::Info job = RunCancelled("find-last", kReadNever);
    ASSERT_EQ(job.state, jobs::JobState::Cancelled) << job.outcome.error;
    EXPECT_EQ(job.done, 1u);
    EXPECT_TRUE(Flag(job.outcome.result, "cancelled"));
    EXPECT_FALSE(Flag(job.outcome.result, "found"));
    EXPECT_NE(job.outcome.result.find("next_before"), nullptr);
    EXPECT_EQ(_ttd->CurrentPosition(), before) << "the replay of the walked frame is undone";

    // The same search without the value filter, synchronously: the newest read, as on an untouched session
    const ttd::TTDReply read = Run("find-last", {{"addr", "0"}, {"access", "read"}});
    ASSERT_TRUE(read.Ok()) << read.message;
    EXPECT_TRUE(Flag(read.body, "found"));
    EXPECT_EQ(U64(read.body, "pc"), 0x8020u);
}

TEST_F(TTDSearchJobs_Test, ACancelledReverseContinueKeepsItsHitsAndTheMachine)
{
    Record();
    const Options everyPass = {{"pcs", "0x800C"}, {"all", "true"}, {"limit", "10000"}};
    const ttd::TTDReply whole = Run("reverse-continue", everyPass);
    ASSERT_TRUE(whole.Ok()) << whole.message;
    const std::vector<uint64_t> wholeTimes = HitTimes(whole.body);
    ASSERT_GT(wholeTimes.size(), 400u);

    const ttd::TTDTimePoint before = _ttd->CurrentPosition();
    const uint16_t pcBefore = _context->pCore->GetZ80()->pc;
    const jobs::Info job = RunCancelled("reverse-continue", everyPass);
    ASSERT_EQ(job.state, jobs::JobState::Cancelled) << job.outcome.error;
    EXPECT_EQ(job.done, 1u);
    const StateNode& result = job.outcome.result;
    EXPECT_TRUE(Flag(result, "cancelled"));
    EXPECT_TRUE(Flag(result, "truncated"));
    const std::vector<uint64_t> got = HitTimes(result);
    ASSERT_FALSE(got.empty());
    ASSERT_LT(got.size(), wholeTimes.size());
    EXPECT_EQ(job.hits, got.size());
    EXPECT_EQ(_ttd->CurrentPosition(), before);
    EXPECT_EQ(_context->pCore->GetZ80()->pc, pcBefore);

    Options rest = everyPass;
    rest["before"] = std::to_string(U64(result, "next_before"));
    const ttd::TTDReply tail = Run("reverse-continue", rest);
    ASSERT_TRUE(tail.Ok()) << tail.message;
    std::vector<uint64_t> joined = got;
    const std::vector<uint64_t> tailTimes = HitTimes(tail.body);
    joined.insert(joined.end(), tailTimes.begin(), tailTimes.end());
    EXPECT_EQ(joined, wholeTimes);

    // The single form with a condition that never holds: cancelled, no hit, the machine stays
    const jobs::Info single =
        RunCancelled("reverse-continue", {{"pcs", "0x800C"}, {"condition", "FRAME > 100000000"}});
    ASSERT_EQ(single.state, jobs::JobState::Cancelled) << single.outcome.error;
    EXPECT_TRUE(Flag(single.outcome.result, "cancelled"));
    EXPECT_FALSE(Flag(single.outcome.result, "matched"));
    EXPECT_EQ(_ttd->CurrentPosition(), before);
    EXPECT_EQ(Json(Run("reverse-continue", everyPass).ToValue()), Json(whole.ToValue())) << "the same query again";
}

TEST_F(TTDSearchJobs_Test, ASearchJobPausesARecordingAsTheCallDoes)
{
    // D8: browsing the past while recording pauses the recording; the job is the same call on a worker
    for (const auto& [verb, options] : std::vector<std::pair<std::string, Options>>{
             {"find-last", {{"addr", "0x8100"}, {"all", "true"}}}, {"reverse-continue", {{"pcs", "0x8200"}, {"all", "true"}}}})
    {
        SCOPED_TRACE(verb);
        ASSERT_TRUE(Run("start").Ok());
        _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
        ASSERT_FALSE(Flag(Run("status").body, "recording_paused"));
        const jobs::Info job = RunJob(verb, options);
        ASSERT_EQ(job.state, jobs::JobState::Done) << job.outcome.error;
        EXPECT_TRUE(Flag(job.outcome.result, "found") || Flag(job.outcome.result, "matched"));
        EXPECT_TRUE(Flag(Run("status").body, "recording_paused"));
        ASSERT_TRUE(Run("stop").Ok());
    }
}

TEST_F(TTDSearchJobs_Test, ABadRequestIsRefusedBeforeAJobIsMade)
{
    Record();
    const size_t jobsBefore = jobs::Registry::Instance().List(_emulator->GetId()).size();
    const std::vector<std::pair<std::string, Options>> bad = {
        {"find-last", {}},                                                        // no criteria
        {"find-last", {{"addr", "0x10000"}}},
        {"find-last", {{"addr", "0"}, {"access", "read"}, {"changes_only", "true"}}},
        {"find-last", {{"addr", "0"}, {"limit", "0"}}},
        {"reverse-continue", {}},                                                 // no pcs
        {"reverse-continue", {{"pcs", "zz"}}},
        {"reverse-continue", {{"pcs", "0x38"}, {"condition", "A =="}}},           // does not compile
        {"reverse-continue", {{"pcs", "0x38"}, {"condition", "A@prev == 1"}}},    // no "before" in a backward walk
        {"reverse-continue", {{"pcs", "0x38"}, {"max_frames", "5"}}},             // a card's option
        {"reverse-continue", {{"cpu", "gs"}}},                                    // a card's move is not a search
    };
    for (const auto& [verb, options] : bad)
    {
        SCOPED_TRACE(jobs::Summary(verb, options));
        Options async = options;
        async["async"] = "true";
        const ttd::TTDReply r = Run(verb, async);
        EXPECT_EQ(r.error, ttd::TTDControlError::BadRequest) << r.message;
        EXPECT_FALSE(r.accepted);
    }
    EXPECT_EQ(jobs::Registry::Instance().List(_emulator->GetId()).size(), jobsBefore);
    // The same refusals as the call without async gives
    EXPECT_EQ(Run("reverse-continue", {{"pcs", "0x38"}, {"condition", "A =="}, {"async", "true"}}).message,
              Run("reverse-continue", {{"pcs", "0x38"}, {"condition", "A =="}}).message);
    EXPECT_EQ(Run("find-last", {{"async", "true"}}).message, Run("find-last").message);
}
