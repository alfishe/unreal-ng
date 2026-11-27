// The GS debugger's twin timeline, phase 1 (debugger/timeline/twintimeline.h): the execution records it reads
// (a start time each, INT / NMI and the NeoGS clock change as records), the blocks built from them, the positions of
// both lanes at a stop, the markers, the lease that arms it and the density it samples while the machine runs.
//
// Runtime justification: the machine tests boot a Pentagon (or a 48K) with a General Sound card and run a few frames
// (about 0.1-0.4 s each); the running ones wait for a breakpoint or three frames in turbo mode.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testwaithelper.h"
#include "_helpers/ttdcardbench.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/history/exechistory.h"
#include "debugger/pchistory/pchistory.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/timeline/twintimeline.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gshostclock.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

namespace
{
const StateNode* Find(const StateNode& node, const std::string& key)
{
    const StateNode* found = node.find(key);
    EXPECT_NE(found, nullptr) << "no '" << key << "'";
    return found;
}

double Number(const StateNode& node, const std::string& key)
{
    const StateNode* found = node.find(key);
    if (!found)
        return -1e9;
    return found->kind == StateNode::Kind::Double ? found->d : static_cast<double>(found->i);
}

/// Main: DI, then LD A,#2A / OUT (#BB),A / a short DJNZ wait, again and again (about 170 T a round)
void WriteOutLoop(Z80* z80)
{
    static const uint8_t program[] = {
        0xF3,              // 8000 DI
        0x3E, 0x2A,        // 8001 LD A,#2A
        0xD3, 0xBB,        // 8003 OUT (#BB),A
        0x06, 0x0A,        // 8005 LD B,10
        0x10, 0xFE,        // 8007 DJNZ #8007
        0x18, 0xF6,        // 8009 JR #8001
    };
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
    z80->pc = 0x8000;
}

bool HistoryOn(EmulatorContext* context, const std::string& device)
{
    bool on = false;
    context->histories.With(device, [&](history::IDeviceHistory& h) { on = h.GetStats().config.mode != history::Mode::Off; });
    return on;
}
}  // namespace

TEST(TwinTimeline_Test, TheExecutionRecordKeepsSixteenBytes)
{
    EXPECT_EQ(sizeof(ExecRecord), 16u);
    ExecRecord r;
    r.kind = ExecRecord::kInt;
    r.idle = 5;
    EXPECT_TRUE(r.IsEvent());
    EXPECT_EQ(r.IdleIterations(), 0u) << "an event's `idle` is not iterations";
    r.kind = 1;
    EXPECT_FALSE(r.IsEvent());
    EXPECT_EQ(r.IdleIterations(), 5u);
}

/// A block lasts until the next record starts, across a frame end; a clock change takes no time; the newest lasts to
/// the lane's time now; records after it (left by a seek back) are not blocks
TEST(TwinTimeline_Test, BlocksLastUntilTheNextRecordStarts)
{
    const int64_t frameLength = 1000;
    std::vector<ExecRecord> records = {
        {7, 990, 0x10, 1, 0, 0},                       // 10 units to the frame end, then 4 of frame 8
        {8, 4, 0x11, 1, 0, 0},                         // 6
        {8, 10, 0x12, ExecRecord::kClock, 12, 24000000},  // 12 -> 24 MHz: no time of its own
        {8, 10, 0x12, 1, 0, 0},                        // 5
        {8, 15, 0x12, ExecRecord::kInt, 0, 0},         // the acknowledge: 20
        {8, 35, 0x38, 0, 0, 0},                        // open: up to 40
        {8, 90, 0x40, 0, 0, 0},                        // past the time now (a seek back)
    };
    const std::vector<timeline::Block> blocks = timeline::Blocks(records, 8, frameLength, 40, 24000000);
    ASSERT_EQ(blocks.size(), 5u);
    EXPECT_EQ(blocks[0].start, -10);
    EXPECT_EQ(blocks[0].duration, 14);
    EXPECT_EQ(blocks[1].duration, 6);
    EXPECT_EQ(blocks[2].index, 3u) << "the clock change is skipped";
    EXPECT_EQ(blocks[2].duration, 5);
    EXPECT_EQ(blocks[3].duration, 20) << "the INT acknowledge";
    EXPECT_EQ(blocks[4].duration, 5);
    EXPECT_TRUE(blocks[4].open);
    EXPECT_EQ(blocks[0].clockHz, 12000000u) << "before the change: the old clock";
    EXPECT_EQ(blocks[1].clockHz, 12000000u);
    EXPECT_EQ(blocks[2].clockHz, 24000000u);

    // Stopped before the newest instruction (recorded, not run): the next instruction, not a block
    records.resize(6);
    records.push_back({8, 40, 0x39, 0, 0, 0});
    EXPECT_EQ(timeline::Blocks(records, 8, frameLength, 40).size(), 5u);
}

/// The main CPU's records start where the CPU stood before each step: the durations are the measured T, a contended
/// instruction's and an INT acknowledge's included (48K, code in contended RAM)
TEST(TwinTimeline_Test, MainDurationsAreTheMeasuredTStates)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    Z80* z80 = context->pCore->GetZ80();
    emulator->RunNFrames(2);

    // #6000: LD A,(HL) / INC HL / JR #6000, interrupts on (IM 1: the ROM's handler at #0038)
    static const uint8_t program[] = {0x7E, 0x23, 0x18, 0xFC};
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x6000 + i), program[i]);
    z80->pc = 0x6000;
    z80->hl = 0x4000;
    z80->sp = 0xFF00;
    z80->im = 1;
    z80->iff1 = z80->iff2 = 1;

    history::DeviceHistory<ExecRecord>& exec = context->pDebugManager->GetPcHistory()->Exec();
    std::string error;
    ASSERT_TRUE(exec.Configure({.mode = history::Mode::Ring, .limit = 65536}, error)) << error;

    const uint32_t frameLength = context->GetFrameTStates();
    const uint64_t frame = context->emulatorState.frame_counter;
    std::vector<uint32_t> starts;
    std::vector<bool> interrupts;
    while (z80->t + 64 < frameLength && starts.size() < 20000)
    {
        starts.push_back(z80->t);
        interrupts.push_back(z80->StepInstruction(true).intAccepted);
    }
    starts.push_back(z80->t);
    ASSERT_EQ(context->emulatorState.frame_counter, frame) << "one frame";

    const std::vector<ExecRecord> records = exec.Snapshot();
    ASSERT_EQ(records.size() + 1, starts.size());
    size_t ints = 0;
    size_t contended = 0;
    for (size_t i = 0; i < records.size(); i++)
    {
        ASSERT_EQ(records[i].frame, static_cast<uint32_t>(frame)) << i;
        ASSERT_EQ(records[i].t, starts[i]) << "record " << i << " starts where the step started";
        ASSERT_EQ(records[i].kind == ExecRecord::kInt, static_cast<bool>(interrupts[i])) << i;
        if (interrupts[i])
        {
            ints++;
            EXPECT_GE(starts[i + 1] - starts[i], 13u) << "the IM 1 acknowledge";
            ASSERT_LT(i + 1, records.size());
        }
        // LD A,(HL) is 7 T uncontended: longer when the ULA held the bus
        if (records[i].pc == 0x6000 && records[i].kind == 1 && starts[i + 1] - starts[i] > 7)
            contended++;
    }
    EXPECT_EQ(ints, 1u) << "the frame's INT";
    EXPECT_GT(contended, 10u) << "contended steps in the screen area";

    // The blocks: each the measured T
    const std::vector<timeline::Block> blocks =
        timeline::Blocks(records, static_cast<uint32_t>(frame), frameLength, z80->t);
    ASSERT_EQ(blocks.size(), records.size());
    for (size_t i = 0; i < blocks.size(); i++)
        ASSERT_EQ(blocks[i].duration, static_cast<int64_t>(starts[i + 1] - starts[i])) << i;
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// NeoGS switches its clock (GSCFG0) while its loader runs: the change is a record, and every instruction's width in
/// card units is a whole number of its own clock's cycles on both sides of it
TEST(TwinTimeline_Test, CardWidthsAreExactAcrossTheNeoGSClockChange)
{
    EmulatorContext ctx{LoggerLevel::LogError};
    ctx.config.frame = 69888;
    ctx.config.frame_duration_us = 19968;
    ctx.emulatorState.current_z80_frequency_multiplier = 1;
    ctx.emulatorState.hw_turbo_ratio_applied = 1;
    NeoGSConfig config;
    auto chip = std::make_unique<SoundChip_NeoGS>(&ctx, config, 44100);
    chip->loadROM("rom/neogs/full_ngs.rom");
    history::DeviceHistory<ExecRecord> exec{"gs.cpu", "the card's instructions", 1u << 20};
    chip->attachExecHistory(&exec, &ctx);
    std::string error;
    ASSERT_TRUE(exec.Configure({.mode = history::Mode::Ring, .limit = 1u << 20}, error)) << error;

    std::vector<ExecRecord> records;
    size_t clock = SIZE_MAX;
    for (int f = 0; f < 120 && clock == SIZE_MAX; f++)
    {
        exec.Clear();
        chip->handleFrameStart();
        ctx.emulatorState.frame_counter++;  // as the machine's frame end does before the card's catch-up
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
        records = exec.Snapshot();
        for (size_t i = 0; i < records.size(); i++)
            if (records[i].kind == ExecRecord::kClock)
            {
                clock = i;
                break;
            }
    }
    chip->attachExecHistory(nullptr, nullptr);
    ASSERT_NE(clock, SIZE_MAX) << "the loader switched no clock in 120 frames";
    const ExecRecord& change = records[clock];
    const uint32_t fromHz = static_cast<uint32_t>(change.page) * 1000000u;
    const uint32_t toHz = change.idle;
    ASSERT_NE(fromHz, toHz);
    ASSERT_GT(clock, 4u);
    ASSERT_GT(records.size(), clock + 4);
    EXPECT_EQ(change.frame, records[clock - 1].frame) << "the frame the catch-up ran";

    const int64_t end = static_cast<int64_t>(records.back().t);
    const std::vector<timeline::Block> blocks = timeline::Blocks(records, records.back().frame, 1, end, toHz);
    size_t before = 0;
    size_t after = 0;
    for (const timeline::Block& b : blocks)
    {
        if (b.open || records[b.index].frame != change.frame)
            continue;
        const uint32_t hz = b.index < clock ? fromHz : toHz;
        EXPECT_EQ(b.clockHz, hz) << "record " << b.index;
        const int64_t ticksPerCycle = static_cast<int64_t>(SoundChip_NeoGS::TICKS_PER_SECOND / hz + 0.5);
        ASSERT_EQ(b.duration % ticksPerCycle, 0) << "record " << b.index << " pc " << records[b.index].pc << ": "
                                                 << b.duration << " ticks at " << hz << " Hz";
        (b.index < clock ? before : after)++;
    }
    EXPECT_GT(before, 0u);
    EXPECT_GT(after, 0u);
}

/// Not armed, nothing records: the histories stay off and the step-work bit down; arming switches on what it reads,
/// the last release switches back off only what it switched on
TEST(TwinTimeline_Test, NothingRecordsUntilArmedAndTheReleaseRestores)
{
    ttdtest::Bench b;
    ASSERT_TRUE(ttdtest::Build(b, ttdtest::Card::GS));
    timeline::Timeline& t = *b.context->pDebugManager->GetTimeline();
    PcHistory& pc = *b.context->pDebugManager->GetPcHistory();
    GeneralSoundCard* card = b.card();
    EXPECT_FALSE(t.Armed());
    b.emulator->RunNFrames(1);
    EXPECT_EQ(pc.Exec().GetStats().written, 0u);
    EXPECT_EQ(card->execHistory()->GetStats().written, 0u);
    EXPECT_EQ(b.context->stepWork.load() & EmulatorContext::kStepWorkExecHistory, 0u);

    // The command log on by hand stays on after the release
    std::string error;
    ASSERT_TRUE(card->commandLog()->History().Configure({.mode = history::Mode::Ring}, error));
    EXPECT_TRUE(t.Arm("a", 0));
    EXPECT_FALSE(t.Arm("b", 0)) << "a second lease: already armed";
    EXPECT_TRUE(pc.Exec().On());
    EXPECT_TRUE(card->execHistory()->On());
    EXPECT_NE(b.context->stepWork.load() & EmulatorContext::kStepWorkExecHistory, 0u);
    b.emulator->RunNFrames(1);
    EXPECT_GT(pc.Exec().GetStats().written, 1000u);
    EXPECT_GT(card->execHistory()->GetStats().written, 0u);

    t.Release("a");
    EXPECT_TRUE(t.Armed()) << "b holds it";
    t.Release("b");
    EXPECT_FALSE(t.Armed());
    EXPECT_FALSE(pc.Exec().On());
    EXPECT_FALSE(card->execHistory()->On());
    EXPECT_TRUE(card->commandLog()->History().On()) << "not the timeline's to switch off";
    EXPECT_EQ(b.context->stepWork.load() & EmulatorContext::kStepWorkExecHistory, 0u);
}

/// A remote client's lease ends when it stops asking
TEST(TwinTimeline_Test, ATimedLeaseRunsOut)
{
    ttdtest::Bench b;
    ASSERT_TRUE(ttdtest::Build(b, ttdtest::Card::GS));
    timeline::Timeline& t = *b.context->pDebugManager->GetTimeline();
    auto now = std::chrono::steady_clock::now();
    t.SetClockForTest([&now] { return now; });
    t.Arm("remote", 1000);
    EXPECT_TRUE(t.Armed());
    EXPECT_TRUE(HistoryOn(b.context, "cpu"));
    now += std::chrono::milliseconds(500);
    t.Arm("remote", 1000);  // renewed
    now += std::chrono::milliseconds(900);
    t.Expire();
    EXPECT_TRUE(t.Armed());
    now += std::chrono::milliseconds(200);
    b.emulator->RunNFrames(1);  // the frame end checks the lease
    EXPECT_FALSE(t.Armed());
    EXPECT_FALSE(HistoryOn(b.context, "cpu"));
    t.SetClockForTest(nullptr);
}

/// The paused node: the host OUT #BB as a marker inside the OUT instruction's block, a breakpoint's marker at its
/// instruction, both lanes with the card's next instruction and a NOW that crosses the frame end
TEST(TwinTimeline_Test, TheNodeMarksTheHostOutAndTheBreakpoint)
{
    ttdtest::Bench b;
    ASSERT_TRUE(ttdtest::Build(b, ttdtest::Card::GS));
    Emulator& emulator = *b.emulator;
    WriteOutLoop(b.z80());
    const uint16_t bp = b.context->pDebugManager->GetBreakpointsManager()->AddExecutionBreakpoint(0x8003);
    ASSERT_NE(bp, BRK_INVALID);
    timeline::Query query;
    query.spanT = 400;
    query.recent = 256;
    timeline::Reply armed = timeline::Read(&emulator, query);
    ASSERT_EQ(armed.status, 200) << armed.error;
    EXPECT_TRUE(Find(armed.body, "started_now")->b);
    emulator.RunNFrames(1);
    emulator.RunTStates(300);

    const timeline::Reply reply = timeline::Read(&emulator, query);
    ASSERT_EQ(reply.status, 200) << reply.error;
    const StateNode& node = reply.body;
    EXPECT_EQ(Find(node, "state")->s, "stopped");
    EXPECT_EQ(Find(node, "cpu")->s, "gs");
    ASSERT_EQ(Find(node, "lanes")->size(), 2u);
    EXPECT_EQ(node.find("lanes")->items[1].find("cpu")->s, "gs");
    const StateNode* recent = Find(node, "recent");
    ASSERT_TRUE(recent && recent->find("main") && recent->find("gs"));
    ASSERT_FALSE(recent->find("main")->items.empty());
    ASSERT_TRUE(Find(node, "next")->find("gs")) << "the card's next instruction";

    // The OUT's block
    const StateNode* out = nullptr;
    for (const StateNode& item : recent->find("main")->items)
        if (item.find("pc")->i == 0x8003 && !item.find("open"))
            out = &item;
    ASSERT_NE(out, nullptr) << "no OUT (#BB),A in the window";
    EXPECT_EQ(Find(*out, "duration_t")->i, 11) << "OUT (n),A on the Pentagon";
    const double outAt = Number(*out, "at");

    bool port = false;
    bool breakpoint = false;
    for (const StateNode& m : Find(node, "markers")->items)
    {
        const std::string kind = m.find("kind")->s;
        const double at = Number(m, "at");
        if (kind == "host_port" && m.find("port")->s == "#BB" && std::abs(at - outAt) < 12)
        {
            EXPECT_EQ(m.find("dir")->s, "out");
            EXPECT_EQ(m.find("value")->i, 0x2A);
            EXPECT_EQ(m.find("pc")->i, 0x8003);
            EXPECT_GE(at, outAt) << "the port write is inside the OUT";
            EXPECT_LE(at, outAt + 11);
            port = true;
        }
        if (kind == "breakpoint" && m.find("id")->i == bp && std::abs(at - outAt) < 0.001)
            breakpoint = true;
    }
    EXPECT_TRUE(port) << "no host OUT #BB marker inside the OUT block";
    EXPECT_TRUE(breakpoint) << "no breakpoint marker at the OUT";
    EXPECT_FALSE(Find(node, "text")->s.empty());
    timeline::SetArmed(&emulator, "local", false, 0);
}

/// A main CPU breakpoint stops the running machine: the stop carries the card's lane (its time, its next
/// instruction) and the gap, so NOW = main T - gap is defined
TEST(TwinTimeline_Test, AMainStopCarriesTheCardLaneAndTheGap)
{
    ttdtest::Bench b;
    ASSERT_TRUE(ttdtest::Build(b, ttdtest::Card::GS));
    Emulator& emulator = *b.emulator;
    const uint16_t bp = b.context->pDebugManager->GetBreakpointsManager()->AddExecutionBreakpoint(0x800B);
    ASSERT_NE(bp, BRK_INVALID);
    emulator.StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return emulator.IsPaused() && emulator.IsEmulationParked(); },
                              std::chrono::seconds(2)));
    const Emulator::DebugStop stop = emulator.LastStop();
    ASSERT_EQ(stop.reason, Emulator::DebugStop::Reason::Breakpoint);
    const StopPositions& positions = stop.breakpoint.positions;
    ASSERT_TRUE(positions.known);
    EXPECT_EQ(positions.mainPc, 0x800B);
    const CardLanePosition& lane = positions.cards[0];
    ASSERT_TRUE(lane.known) << "the card's lane at a main stop";
    EXPECT_FALSE(positions.cards[1].known) << "no MultiSound";
    EXPECT_GT(lane.unitsPerT, 0.0);
    EXPECT_GT(lane.clockHz, 0u);
    GeneralSoundCard* card = b.card();
    EXPECT_NEAR(lane.gapT, static_cast<double>(card->hostLead()) / lane.unitsPerT, 1e-9);
    EXPECT_GE(lane.gapT, 0.0) << "the card is lazy: main is ahead";
    Z80CpuRegisters regs{};
    ASSERT_TRUE(card->cardRegisters(regs));
    EXPECT_EQ(lane.pc, regs.pc);

    // The node from the paused machine: NOW = main T - gap
    timeline::Query query;
    const timeline::Reply reply = timeline::Read(&emulator, query);
    ASSERT_EQ(reply.status, 200) << reply.error;
    const StateNode* now = Find(reply.body, "now");
    ASSERT_NE(now, nullptr);
    EXPECT_NEAR(Number(*now, "t"), positions.mainT - lane.gapT, 0.001);
    EXPECT_NEAR(Number(*now, "gap_t"), lane.gapT, 0.001);
    timeline::SetArmed(&emulator, "local", false, 0);
    emulator.Stop();
}

/// While the machine runs: one density tick per frame with the game's GS port accesses and the card's work
TEST(TwinTimeline_Test, TheDensityRingFillsWhileRunning)
{
    ttdtest::Bench b;
    ASSERT_TRUE(ttdtest::Build(b, ttdtest::Card::GS));
    Emulator& emulator = *b.emulator;
    timeline::Timeline& t = *b.context->pDebugManager->GetTimeline();
    t.Arm("test", 0);
    emulator.EnableTurboMode();
    emulator.StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return t.Density(DebugCpuId::GS).size() >= 3; }, std::chrono::seconds(2)));
    const std::vector<timeline::DensityTick> ticks = t.Density(DebugCpuId::GS);
    const StateNode running = timeline::BuildNow(emulator, timeline::Query{});
    emulator.Pause();
    ASSERT_TRUE(TestWait::For([&] { return emulator.IsEmulationParked(); }, std::chrono::seconds(2)));
    for (size_t i = 0; i + 1 < ticks.size(); i++)
        EXPECT_EQ(ticks[i + 1].frame, ticks[i].frame + 1) << "one tick per frame";
    for (const timeline::DensityTick& tick : ticks)
    {
        EXPECT_GT(tick.hostStatusPolls, 0u) << "the program's IN A,(#BB) after each LDIR";
        EXPECT_GT(tick.cardSteps, 0u);
        EXPECT_LE(tick.cardIdleSteps, tick.cardSteps);
    }
    EXPECT_EQ(Find(running, "state")->s, "running");
    EXPECT_GE(Find(running, "density")->size(), 3u);
    const StateNode monitor = timeline::MonitorNode(emulator);
    EXPECT_TRUE(Find(monitor, "armed")->b);
    EXPECT_GE(Find(monitor, "density")->size(), 3u);
    t.Release("test");
    emulator.Stop();
}
