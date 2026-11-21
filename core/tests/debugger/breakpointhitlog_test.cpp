/// @file breakpointhitlog_test.cpp
/// @brief Counting breakpoints with per-hit frames (PLAN #122 V4, docs/inprogress/2026-11-12-re-api-efficiency): a
/// breakpoint that only counts records each hit's frame, T, PC, address and value, bounded (the first N or the newest
/// N). The oracle: the same machine run with a breakpoint that stops on every hit sees the same hits at the same
/// moments. Also the log on its own (paging, keep first / last, clear), the hit policy and the condition, the reset,
/// no record for hit_log 0, the refusals, the monitoring verb `breakpoint_hits` every plane forwards to.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointhitlog.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/memory/memory.h"
#include "emulator/monitoring/monitoringapi.h"

namespace
{
/// One pass: write #9000 and #9001, read #9000, then loop
const uint8_t kProgram[] = {
    0x3E, 0x05,        // 8000 LD A,5
    0x32, 0x00, 0x90,  // 8002 LD (#9000),A
    0x32, 0x01, 0x90,  // 8005 LD (#9001),A
    0x3A, 0x00, 0x90,  // 8008 LD A,(#9000)
    0x18, 0xF3,        // 800B JR #8000
};
constexpr uint16_t kStart = 0x8000;
constexpr uint16_t kStore = 0x8002;  // LD (#9000),A
constexpr unsigned kLoopInstructions = 5;
constexpr unsigned kPasses = 40;
// The loop starts this many T before the frame's end: the passes run across a frame boundary
constexpr unsigned kBeforeFrameEnd = 600;

BreakpointHitRecord Record(uint32_t hit)
{
    BreakpointHitRecord r{};
    r.hit = hit;
    r.frame = hit * 10;
    r.cpuTime = -1;
    r.value = -1;
    return r;
}

/// A breakpoint spec of the loop: execute #8002 or write #9000
BreakpointSpec Spec(uint8_t access, bool stop)
{
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = access;
    spec.address = access == BRK_MEM_EXECUTE ? kStore : 0x9000;
    spec.stop = stop;
    return spec;
}

/// The moment of a hit as both runs see it
struct Moment
{
    uint64_t frame;
    uint32_t t;
    uint16_t pc;
};
}  // namespace

class BreakpointHitLog_Test : public ::testing::Test
{
protected:
    std::vector<Emulator*> _emulators;

    void TearDown() override
    {
        for (Emulator* emulator : _emulators)
            EmulatorTestHelper::CleanupEmulator(emulator);
    }

    /// A 48K with the loop at #8000, interrupts off, kBeforeFrameEnd T before the frame's end
    Emulator* Machine()
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr);
        if (!emulator)
            return nullptr;
        _emulators.push_back(emulator);
        EmulatorContext* context = emulator->GetContext();
        for (size_t i = 0; i < sizeof(kProgram); i++)
            context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kStart + i), kProgram[i]);
        Z80* z80 = context->pCore->GetZ80();
        z80->pc = kStart;
        z80->sp = 0x7F00;
        z80->iff1 = z80->iff2 = 0;
        z80->t = context->config.frame - kBeforeFrameEnd;
        // A breakpoint that stops needs the debugger on (one that only counts runs without it too); on for every
        // machine here, so the oracle and the counting run take the same memory interface
        emulator->DebugOn();
        context->pFeatureManager->setFeature(Features::kDebugMode, true);
        context->pFeatureManager->setFeature(Features::kBreakpoints, true);
        context->pMemory->UpdateFeatureCache();
        return emulator;
    }

    static BreakpointManager& Breakpoints(Emulator* emulator)
    {
        return *emulator->GetContext()->pDebugManager->GetBreakpointsManager();
    }

    static uint16_t Add(Emulator* emulator, const BreakpointSpec& spec)
    {
        std::string error;
        const uint16_t id = Breakpoints(emulator).AddBreakpoint(spec, error);
        EXPECT_NE(id, BRK_INVALID) << error;
        return id;
    }

    /// The oracle: runs the loop's passes with one breakpoint that stops on every hit; the moment of each stop
    std::vector<Moment> StopEveryHit(uint8_t access)
    {
        Emulator* emulator = Machine();
        std::vector<Moment> moments;
        if (!emulator)
            return moments;
        EmulatorContext* context = emulator->GetContext();
        const Z80* z80 = context->pCore->GetZ80();
        Add(emulator, Spec(access, true));
        unsigned left = kPasses * kLoopInstructions;
        while (left > 0)
        {
            const unsigned ran = emulator->RunNCPUCycles(left, false);
            if (ran == left && Breakpoints(emulator).GetLastTriggeredBreakpointID() == BRK_INVALID)
                break;
            moments.push_back({context->emulatorState.frame_counter, z80->t, z80->m1_pc});
            Breakpoints(emulator).ClearLastTriggeredBreakpoint();
            if (access == BRK_MEM_EXECUTE)
            {
                // Stopped before #8002: step it, its own breakpoint passed
                emulator->StepPastBreakpointAtPc();
                left -= ran + 1;
            }
            else
                left -= ran;  // a watchpoint stops after the instruction
        }
        return moments;
    }
};

/// The log alone: keep first holds the first N and counts the rest as dropped; keep last the newest N; paging by
/// index with more / firstIndex; a clear empties it
TEST_F(BreakpointHitLog_Test, LogKeepsTheFirstOrTheNewestAndPages)
{
    BreakpointHitLog first(4, BreakpointHitLogKeep::First);
    BreakpointHitLog last(4, BreakpointHitLogKeep::Last);
    for (uint32_t n = 1; n <= 10; ++n)
    {
        first.Add(Record(n));
        last.Add(Record(n));
    }
    BreakpointHitLog::View f = first.Read(0, 100);
    EXPECT_EQ(f.total, 10u);
    EXPECT_EQ(f.stored, 4u);
    EXPECT_EQ(f.dropped, 6u);
    EXPECT_EQ(f.oldest, 0u);
    ASSERT_EQ(f.records.size(), 4u);
    EXPECT_EQ(f.records[0].hit, 1u);
    EXPECT_EQ(f.records[3].hit, 4u);
    EXPECT_FALSE(f.more);

    BreakpointHitLog::View l = last.Read(0, 3);
    EXPECT_EQ(l.oldest, 6u) << "hits 7..10 held as indexes 6..9";
    EXPECT_EQ(l.firstIndex, 6u) << "an index below the oldest starts at the oldest";
    ASSERT_EQ(l.records.size(), 3u);
    EXPECT_EQ(l.records[0].hit, 7u);
    EXPECT_EQ(l.records[2].hit, 9u);
    EXPECT_TRUE(l.more);
    BreakpointHitLog::View rest = last.Read(l.firstIndex + l.records.size(), 3);
    ASSERT_EQ(rest.records.size(), 1u);
    EXPECT_EQ(rest.records[0].hit, 10u);
    EXPECT_FALSE(rest.more);
    EXPECT_TRUE(last.Read(10, 5).records.empty()) << "past the end: nothing";

    last.Clear();
    EXPECT_EQ(last.Read(0, 10).total, 0u);
    last.Add(Record(1));
    BreakpointHitLog::View again = last.Read(0, 10);
    ASSERT_EQ(again.records.size(), 1u);
    EXPECT_EQ(again.oldest, 0u);
}

/// The oracle: a breakpoint that only counts records the hits a breakpoint that stops on every hit stops at, with the
/// same frame (across a frame boundary), T and PC; a watchpoint's record is taken inside its instruction
TEST_F(BreakpointHitLog_Test, CountingRecordsTheMomentsAStopOnEveryHitSees)
{
    const std::vector<Moment> execStops = StopEveryHit(BRK_MEM_EXECUTE);
    const std::vector<Moment> writeStops = StopEveryHit(BRK_MEM_WRITE);
    ASSERT_EQ(execStops.size(), kPasses);
    ASSERT_EQ(writeStops.size(), kPasses);
    ASSERT_NE(execStops.front().frame, execStops.back().frame) << "the passes cross a frame boundary";

    Emulator* emulator = Machine();
    ASSERT_NE(emulator, nullptr);
    const uint16_t execId = Add(emulator, Spec(BRK_MEM_EXECUTE, false));
    const uint16_t writeId = Add(emulator, Spec(BRK_MEM_WRITE, false));
    ASSERT_EQ(emulator->RunNCPUCycles(kPasses * kLoopInstructions, false), kPasses * kLoopInstructions)
        << "a breakpoint that only counts never stops";

    BreakpointManager& bpm = Breakpoints(emulator);
    const BreakpointManager::HitLogInfo exec = bpm.GetHitLog(execId, 0, 1000);
    const BreakpointManager::HitLogInfo write = bpm.GetHitLog(writeId, 0, 1000);
    ASSERT_TRUE(exec.found && exec.countOnly && exec.hasLog);
    ASSERT_TRUE(write.hasLog);
    EXPECT_EQ(exec.hitCount, kPasses);
    EXPECT_EQ(exec.view.capacity, BreakpointHitLog::kDefaultCapacity) << "the default record";
    ASSERT_EQ(exec.view.records.size(), execStops.size());
    ASSERT_EQ(write.view.records.size(), writeStops.size());
    for (size_t i = 0; i < execStops.size(); ++i)
    {
        const BreakpointHitRecord& r = exec.view.records[i];
        EXPECT_EQ(r.hit, i + 1);
        EXPECT_EQ(r.frame, execStops[i].frame) << "hit " << i;
        EXPECT_EQ(r.t, execStops[i].t) << "hit " << i;
        EXPECT_EQ(r.pc, kStore);
        EXPECT_EQ(r.address, kStore);
        EXPECT_EQ(r.kind, BRK_TAP_EXEC);
        EXPECT_EQ(r.cpuTime, -1);

        const BreakpointHitRecord& w = write.view.records[i];
        EXPECT_EQ(w.hit, i + 1);
        EXPECT_EQ(w.pc, kStore) << "the instruction doing the write";
        EXPECT_EQ(w.address, 0x9000);
        EXPECT_EQ(w.kind, BRK_TAP_WRITE);
        // Inside the instruction: the frame it began in (the frame ends only after an instruction), after it began,
        // no later than its end where the stop came (that one maybe already in the next frame)
        EXPECT_EQ(w.frame, execStops[i].frame) << "hit " << i;
        EXPECT_GT(w.t, execStops[i].t) << "hit " << i;
        const uint64_t frameT = emulator->GetContext()->config.frame;
        const uint64_t endT = writeStops[i].t + (writeStops[i].frame - execStops[i].frame) * frameT;
        EXPECT_LE(w.t, endT) << "hit " << i;
    }
}

/// Bounded: keep first holds the first N, keep last the newest N; a hit policy records only the hits it passes; a
/// condition only the matches; hit_log 0 keeps none; the reset empties the record with the count
TEST_F(BreakpointHitLog_Test, BoundsPolicyConditionAndReset)
{
    Emulator* emulator = Machine();
    ASSERT_NE(emulator, nullptr);
    BreakpointManager& bpm = Breakpoints(emulator);

    BreakpointSpec firstSpec = Spec(BRK_MEM_EXECUTE, false);
    firstSpec.hitLog = 5;
    BreakpointSpec lastSpec = firstSpec;
    lastSpec.hitLogKeep = BreakpointHitLogKeep::Last;
    BreakpointSpec everyThird = Spec(BRK_MEM_EXECUTE, false);
    std::string error;
    ASSERT_TRUE(BreakpointManager::ParseHitSpec("%3", everyThird.hitMode, everyThird.hitTarget, error)) << error;
    BreakpointSpec none = Spec(BRK_MEM_EXECUTE, false);
    none.hitLog = 0;
    BreakpointSpec conditional = Spec(BRK_MEM_WRITE, false);
    conditional.address = 0x9000;
    conditional.hasEnd = true;
    conditional.addressEnd = 0x9001;
    conditional.condition = "WR == #9001";
    // A stop on the same address keeps the record of the counting breakpoints apart (no adoption)
    const uint16_t firstId = Add(emulator, firstSpec);
    const uint16_t lastId = Add(emulator, lastSpec);
    const uint16_t thirdId = Add(emulator, everyThird);
    const uint16_t noneId = Add(emulator, none);
    const uint16_t condId = Add(emulator, conditional);
    ASSERT_EQ(emulator->RunNCPUCycles(kPasses * kLoopInstructions, false), kPasses * kLoopInstructions);

    const BreakpointManager::HitLogInfo first = bpm.GetHitLog(firstId, 0, 100);
    ASSERT_EQ(first.view.records.size(), 5u);
    EXPECT_EQ(first.view.records[0].hit, 1u);
    EXPECT_EQ(first.view.records[4].hit, 5u);
    EXPECT_EQ(first.view.dropped, kPasses - 5);

    const BreakpointManager::HitLogInfo last = bpm.GetHitLog(lastId, 0, 100);
    ASSERT_EQ(last.view.records.size(), 5u);
    EXPECT_EQ(last.view.records[0].hit, kPasses - 4);
    EXPECT_EQ(last.view.records[4].hit, kPasses);
    EXPECT_EQ(last.view.oldest, kPasses - 5);
    EXPECT_EQ(last.view.records[4].frame, first.view.records[0].frame + 1) << "the newest are past the boundary";

    const BreakpointManager::HitLogInfo third = bpm.GetHitLog(thirdId, 0, 100);
    EXPECT_EQ(third.hitCount, kPasses) << "every hit counts";
    ASSERT_EQ(third.view.records.size(), kPasses / 3) << "the policy's hits are recorded";
    EXPECT_EQ(third.view.records[0].hit, 3u);
    EXPECT_EQ(third.view.records[1].hit, 6u);

    const BreakpointManager::HitLogInfo noLog = bpm.GetHitLog(noneId, 0, 100);
    EXPECT_TRUE(noLog.found);
    EXPECT_EQ(noLog.hitCount, kPasses);
    EXPECT_FALSE(noLog.hasLog);

    const BreakpointManager::HitLogInfo cond = bpm.GetHitLog(condId, 0, 100);
    EXPECT_TRUE(cond.hasCondition);
    ASSERT_EQ(cond.view.records.size(), kPasses) << "the matches only: #9001, not #9000";
    EXPECT_EQ(cond.view.records[0].address, 0x9001);
    EXPECT_EQ(cond.view.records[0].pc, 0x8005);

    EXPECT_TRUE(bpm.ResetHitCount(firstId));
    EXPECT_EQ(bpm.GetHitLog(firstId).view.total, 0u);
    EXPECT_EQ(bpm.GetHitLog(lastId).view.total, kPasses) << "one reset leaves the others";
    bpm.ResetAllHitCounts();
    EXPECT_EQ(bpm.GetHitLog(lastId).view.total, 0u);
    EXPECT_EQ(bpm.GetHitLog(condId).view.stored, 0u);

    // The record goes with its breakpoint: a new one in the freed slot starts empty
    EXPECT_TRUE(bpm.RemoveBreakpointByID(firstId));
    EXPECT_FALSE(bpm.GetHitLog(firstId).found);
    ASSERT_EQ(emulator->RunNCPUCycles(kLoopInstructions, false), kLoopInstructions);
    const uint16_t again = Add(emulator, firstSpec);
    EXPECT_EQ(bpm.GetHitLog(again).view.total, 0u);
    EXPECT_EQ(bpm.GetHitLog(lastId).view.total, 1u);
    EXPECT_NE(bpm.FormatBreakpointInfo(lastId).find("log last 5"), std::string::npos) << bpm.FormatBreakpointInfo(lastId);
}

/// A full record of the first N stops hearing the hits (no record left taking them: the walk calls nothing, as a
/// breakpoint that only counts did before the records) and still reports the hits it dropped, policy included, from the
/// count; a reset or a record that takes hits brings the sink back
TEST_F(BreakpointHitLog_Test, AFullRecordOfTheFirstHitsStillCountsWhatItDrops)
{
    Emulator* emulator = Machine();
    ASSERT_NE(emulator, nullptr);
    BreakpointManager& bpm = Breakpoints(emulator);

    BreakpointSpec plain = Spec(BRK_MEM_EXECUTE, false);
    plain.hitLog = 3;
    BreakpointSpec everyFourth = plain;
    std::string error;
    ASSERT_TRUE(BreakpointManager::ParseHitSpec("%4", everyFourth.hitMode, everyFourth.hitTarget, error)) << error;
    BreakpointSpec fromTenth = plain;
    ASSERT_TRUE(BreakpointManager::ParseHitSpec(">=10", fromTenth.hitMode, fromTenth.hitTarget, error)) << error;
    BreakpointSpec writes = Spec(BRK_MEM_WRITE, false);
    writes.hitLog = 2;
    writes.condition = "WR == #9000";
    const uint16_t plainId = Add(emulator, plain);
    const uint16_t fourthId = Add(emulator, everyFourth);
    const uint16_t tenthId = Add(emulator, fromTenth);
    const uint16_t writesId = Add(emulator, writes);
    ASSERT_EQ(emulator->RunNCPUCycles(kPasses * kLoopInstructions, false), kPasses * kLoopInstructions);

    const auto expect = [&](uint16_t id, uint64_t told, uint32_t firstHit, uint32_t stored, const char* what) {
        const BreakpointManager::HitLogInfo info = bpm.GetHitLog(id, 0, 100);
        EXPECT_EQ(info.hitCount, kPasses) << what;
        EXPECT_EQ(info.view.total, told) << what;
        EXPECT_EQ(info.view.stored, stored) << what;
        EXPECT_EQ(info.view.dropped, told - stored) << what;
        ASSERT_FALSE(info.view.records.empty()) << what;
        EXPECT_EQ(info.view.records[0].hit, firstHit) << what;
    };
    expect(plainId, kPasses, 1, 3, "every hit");
    expect(fourthId, kPasses / 4, 4, 3, "every fourth");
    expect(tenthId, kPasses - 9, 10, 3, "from the tenth on");
    expect(writesId, kPasses, 1, 2, "the condition's hits");

    // A reset: the records take hits again, the drop counted anew
    bpm.ResetAllHitCounts();
    ASSERT_EQ(emulator->RunNCPUCycles(2 * kLoopInstructions, false), 2 * kLoopInstructions);
    EXPECT_EQ(bpm.GetHitLog(plainId).view.stored, 2u) << "recorded again after the reset";
    EXPECT_EQ(bpm.GetHitLog(plainId).view.dropped, 0u);
    ASSERT_EQ(emulator->RunNCPUCycles(kPasses * kLoopInstructions, false), kPasses * kLoopInstructions);
    EXPECT_EQ(bpm.GetHitLog(plainId).view.dropped, kPasses + 2 - 3);

    // One breakpoint's reset brings its record back while the others stay full
    EXPECT_TRUE(bpm.ResetHitCount(fourthId));
    ASSERT_EQ(emulator->RunNCPUCycles(8 * kLoopInstructions, false), 8 * kLoopInstructions);
    const BreakpointManager::HitLogInfo fourth = bpm.GetHitLog(fourthId, 0, 100);
    ASSERT_EQ(fourth.view.records.size(), 2u);
    EXPECT_EQ(fourth.view.records[1].hit, 8u);
    EXPECT_EQ(bpm.GetHitLog(plainId).view.dropped, kPasses + 10 - 3);

    // A record of the newest keeps the sink: the full ones count on through it, the same totals
    BreakpointSpec newest = plain;
    newest.hitLogKeep = BreakpointHitLogKeep::Last;
    const uint16_t newestId = Add(emulator, newest);
    ASSERT_EQ(emulator->RunNCPUCycles(4 * kLoopInstructions, false), 4 * kLoopInstructions);
    EXPECT_EQ(bpm.GetHitLog(newestId).view.total, 4u);
    EXPECT_EQ(bpm.GetHitLog(plainId).view.total, kPasses + 14);
    EXPECT_EQ(bpm.GetHitLog(plainId).view.dropped, kPasses + 14 - 3);
}

/// The refusals: a record on a breakpoint that stops, a record past the maximum; a breakpoint that stops reads with no
/// record
TEST_F(BreakpointHitLog_Test, ARecordNeedsABreakpointThatOnlyCounts)
{
    Emulator* emulator = Machine();
    ASSERT_NE(emulator, nullptr);
    BreakpointManager& bpm = Breakpoints(emulator);
    std::string error;
    BreakpointSpec stops = Spec(BRK_MEM_EXECUTE, true);
    stops.hitLog = 10;
    EXPECT_EQ(bpm.AddBreakpoint(stops, error), BRK_INVALID);
    EXPECT_NE(error.find("hit_log"), std::string::npos) << error;
    BreakpointSpec huge = Spec(BRK_MEM_EXECUTE, false);
    huge.hitLog = BreakpointHitLog::kMaxCapacity + 1;
    EXPECT_EQ(bpm.AddBreakpoint(huge, error), BRK_INVALID);
    EXPECT_EQ(bpm.GetBreakpointsCount(), 0u) << "nothing left behind";

    stops.hitLog = 0;
    const uint16_t id = Add(emulator, stops);
    const BreakpointManager::HitLogInfo info = bpm.GetHitLog(id);
    EXPECT_TRUE(info.found);
    EXPECT_FALSE(info.countOnly);
    EXPECT_FALSE(info.hasLog);

    BreakpointHitLogKeep keep = BreakpointHitLogKeep::First;
    EXPECT_TRUE(BreakpointManager::ParseHitLogKeep("last", keep));
    EXPECT_EQ(keep, BreakpointHitLogKeep::Last);
    EXPECT_FALSE(BreakpointManager::ParseHitLogKeep("newest", keep));
}

/// The verb every plane forwards to (WebAPI GET .../breakpoints/{bp}/hits, CLI bphits, Lua / Python bp_hit_log, MCP
/// bp_hits): the counts, the record, paging, the refusals
TEST_F(BreakpointHitLog_Test, MonitoringVerbReadsTheRecord)
{
    Emulator* emulator = Machine();
    ASSERT_NE(emulator, nullptr);
    BreakpointSpec spec = Spec(BRK_MEM_WRITE, false);
    spec.hitLog = 10;
    const uint16_t id = Add(emulator, spec);
    ASSERT_EQ(emulator->RunNCPUCycles(4 * kLoopInstructions, false), 4 * kLoopInstructions);

    StateNode args = StateNode::Object();
    args["instance"] = emulator->GetId();
    args["id"] = std::to_string(id);
    args["limit"] = "3";
    monitoring::Reply reply = monitoring::Call("breakpoint_hits", args);
    ASSERT_TRUE(reply.Ok()) << reply.status;
    const StateNode& body = reply.body;
    ASSERT_NE(body.find("hit_count"), nullptr);
    EXPECT_EQ(body.find("hit_count")->i, 4);
    EXPECT_FALSE(body.find("stop")->b);
    const StateNode* log = body.find("log");
    ASSERT_TRUE(log && log->kind == StateNode::Kind::Object);
    EXPECT_EQ(log->find("capacity")->i, 10);
    EXPECT_EQ(log->find("keep")->s, "first");
    EXPECT_EQ(log->find("stored")->i, 4);
    const StateNode* hits = body.find("hits");
    ASSERT_TRUE(hits && hits->items.size() == 3u);
    EXPECT_EQ(hits->items[0].find("pc")->i, kStore);
    EXPECT_EQ(hits->items[0].find("address")->i, 0x9000);
    EXPECT_EQ(hits->items[0].find("kind")->s, "write");
    EXPECT_EQ(hits->items[0].find("value")->i, 5) << "the byte written";
    EXPECT_EQ(hits->items[0].find("cpu_time"), nullptr) << "the main CPU has no clock of its own";
    EXPECT_TRUE(body.find("more")->b);
    EXPECT_EQ(body.find("next_from")->i, 3);

    args["from"] = "3";
    reply = monitoring::Call("breakpoint_hits", args);
    ASSERT_TRUE(reply.Ok());
    ASSERT_EQ(reply.body.find("hits")->items.size(), 1u);
    EXPECT_EQ(reply.body.find("hits")->items[0].find("hit")->i, 4);
    EXPECT_FALSE(reply.body.find("more")->b);

    args["id"] = "999";
    EXPECT_EQ(monitoring::Call("breakpoint_hits", args).status, 404);
    args["id"] = std::to_string(id);
    args["limit"] = "0";
    EXPECT_EQ(monitoring::Call("breakpoint_hits", args).status, 400);
    args["limit"] = "3";
    args["cpu"] = "zz";
    EXPECT_EQ(monitoring::Call("breakpoint_hits", args).status, 400);
}

/// `bphits <id>` only reads (no run control needed while another client holds the machine); `bphits reset` changes it
TEST(BreakpointHitLogGate_Test, ReadingTheHitsIsNoChange)
{
    EXPECT_FALSE(automation_gate::CliCommandChangesMachine("bphits", {"1"}));
    EXPECT_FALSE(automation_gate::CliCommandChangesMachine("bphits", {"1", "--limit", "5"}));
    EXPECT_FALSE(automation_gate::CliCommandChangesMachine("bphits", {}));
    EXPECT_TRUE(automation_gate::CliCommandChangesMachine("bphits", {"reset"}));
    EXPECT_TRUE(automation_gate::CliCommandChangesMachine("bphits", {"reset", "1"}));
}
