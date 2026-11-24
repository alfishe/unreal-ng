// The card CPU's profiler (gscardprofiler.h, gs-debugger X9 / X2 / X20 / P5.6): hand-built programs on the classic
// GS with known cycle counts give the exact ISR budget, overruns, a command's cost, the routines' inclusive and
// exclusive time across a nested call with interrupts inside, and the frame map's ISR bars, host accesses and DAC
// fetches in their periods. Off, nothing runs: no working state, no records. The real firmware on a machine (classic
// GS and NeoGS) gives a report on the REST shape with the firmware's labels.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/gscardprofiler.h"
#include "emulator/sound/chips/gs/gshostdriver.h"
#include "emulator/sound/chips/gs/gsprofilereport.h"
#include "emulator/sound/chips/gs/gsslotstats.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/statenodejson.h"

namespace
{
constexpr int64_t kPeriod = 320;  // classic GS: 12 MHz / 37.5 kHz

/// A classic GS with `program` at #0000 of its ROM (the rest zero = NOP)
struct ProgramCard
{
    EmulatorContext ctx{LoggerLevel::LogError};
    std::unique_ptr<SoundChip_GeneralSound> chip;

    explicit ProgramCard(const std::vector<uint8_t>& rom, const char* leaf)
    {
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<SoundChip_GeneralSound>(&ctx, 512, 44100);
        std::vector<uint8_t> image(SoundChip_GeneralSound::ROM_SIZE, 0x00);
        std::memcpy(image.data(), rom.data(), std::min(rom.size(), image.size()));
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(leaf);
        std::ofstream(path, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(image.data()),
                                                                      static_cast<std::streamsize>(image.size()));
        chip->loadROM(path);
    }

    void Frame()
    {
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
    }
    GSProfileSnapshot Snap() const { return chip->profiler().Snapshot(); }
};

/// The ROM image: `at` bytes placed at their addresses
struct Rom
{
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x400, 0x00);
    void At(uint16_t addr, std::initializer_list<uint8_t> code)
    {
        for (uint8_t b : code)
            bytes[addr++] = b;
    }
    void Nops(uint16_t addr, int count)
    {
        for (int i = 0; i < count; i++)
            bytes[static_cast<size_t>(addr + i)] = 0x00;
    }
};

/// DI, SP #5800, IM 1, EI, then the main loop at #0007; the ISR at #0038: `nops` NOPs, EI, RET
/// (IM 1 acknowledge 13 + 4 * nops + EI 4 + RET 10)
Rom IsrRom(int nops)
{
    Rom r;
    r.At(0x0000, {0xF3, 0x31, 0x00, 0x58, 0xED, 0x56, 0xFB});  // DI; LD SP,#5800; IM 1; EI
    r.At(0x0007, {0x00, 0x00, 0x18, 0xFC});                    // NOP; NOP; JR #0007
    r.Nops(0x0038, nops);
    r.At(static_cast<uint16_t>(0x0038 + nops), {0xFB, 0xC9});  // EI; RET
    return r;
}

const GSProfileRoutine* RoutineAt(const GSProfileSnapshot& s, uint16_t pc)
{
    for (const GSProfileRoutine& r : s.routines)
        if (r.pc == pc)
            return &r;
    return nullptr;
}
}  // namespace

/// ISR of 67 cycles (13 + 10 NOPs + EI + RET) every 320: every ISR exactly 67, no overrun, latency at most one
/// main-loop instruction (JR, 12), the frame's activity sums to the frame
TEST(GSCardProfiler_Test, IsrBudgetIsExact)
{
    ProgramCard card(IsrRom(10).bytes, "gsprof-isr.rom");
    card.chip->profiler().Start();
    card.Frame();
    card.Frame();
    const GSProfileSnapshot s = card.Snap();
    ASSERT_EQ(s.frames, 2u);
    EXPECT_EQ(s.periodUnits, kPeriod);
    ASSERT_GT(s.isrTotal.count, 1000u) << "a frame of 239,602 cycles holds ~749 periods";
    EXPECT_EQ(s.isrTotal.min, 67u);
    EXPECT_EQ(s.isrTotal.max, 67u);
    EXPECT_EQ(s.isrTotal.cycles, s.isrTotal.count * 67u);
    EXPECT_EQ(s.isrTotal.overruns, 0u);
    EXPECT_LE(s.isrTotal.latencyMax, 12u);
    EXPECT_EQ(s.isrLastFrame.min, 67u);
    EXPECT_GT(s.isrLastFrame.count, 740u);
    EXPECT_LT(s.isrLastFrame.count, 760u);

    ASSERT_EQ(s.activity.size(), 2u);
    const GSProfileActivity& a = s.activity.back();
    EXPECT_GT(a.cycles, 239000u);
    EXPECT_NEAR(static_cast<double>(a.isrCycles) / a.cycles, 67.0 / 320.0, 0.01) << "ISR busy share";
    EXPECT_EQ(a.isrMax, 67u);
    EXPECT_EQ(a.idleCycles, 0u) << "interrupts on: no idle sleep";
    EXPECT_EQ(a.overruns, 0u);

    // The ISR is a routine of its own, every call an interrupt
    const GSProfileRoutine* isr = RoutineAt(s, 0x0038);
    ASSERT_NE(isr, nullptr);
    EXPECT_EQ(isr->calls, isr->interrupts);
    EXPECT_EQ(isr->min, 67u);
    EXPECT_EQ(isr->max, 67u);
}

/// ISR of 347 cycles (80 NOPs) in a period of 320: every one ends after its period's end
TEST(GSCardProfiler_Test, OverrunsAreCounted)
{
    ProgramCard card(IsrRom(80).bytes, "gsprof-overrun.rom");
    card.chip->profiler().Start();
    card.Frame();
    const GSProfileSnapshot s = card.Snap();
    ASSERT_GT(s.isrTotal.count, 500u);
    EXPECT_EQ(s.isrTotal.min, 347u);
    EXPECT_EQ(s.isrTotal.max, 347u);
    EXPECT_EQ(s.isrTotal.overruns, s.isrTotal.count);
    EXPECT_EQ(s.activity.back().overruns, s.isrLastFrame.count);
    EXPECT_GT(s.isrTotal.lost, 0u) << "the ISRs drift past whole periods: requests merge";
}

/// The command loop polls FLAGS (IN A,(4)) with interrupts off, reads COMRG (IN A,(1)), acknowledges (IN A,(5))
/// and burns LD B,20 / DJNZ before polling again: from the read to the next poll = IN 11 + LD 7 + DJNZ 19 * 13 + 8
/// + JR 12 + IN 11 = 296 cycles; the poll loop is slept through between commands
TEST(GSCardProfiler_Test, CommandHandlerCost)
{
    Rom r;
    r.At(0x0000, {0xF3, 0x31, 0x00, 0x58});  // DI; LD SP,#5800
    r.At(0x0004, {0xDB, 0x04,                // IN A,(4)
                  0x0F,                      // RRCA
                  0x30, 0xFB,                // JR NC,#0004
                  0xDB, 0x01,                // IN A,(1)
                  0xDB, 0x05,                // IN A,(5): the acknowledge (RSCOM)
                  0x06, 0x14,                // LD B,20
                  0x10, 0xFE,                // DJNZ $
                  0x18, 0xF1});              // JR #0004
    ProgramCard card(r.bytes, "gsprof-command.rom");
    card.chip->profiler().Start();
    card.Frame();
    card.chip->handleFrameStart();
    for (uint8_t code : {0x2A, 0x2A, 0x2A, 0x40})
    {
        card.chip->portDeviceOutMethod(SoundChip_GeneralSound::PORT_COMMAND, code);
        card.chip->runFor(2000);
    }
    card.chip->handleFrameEnd(SAMPLES_PER_FRAME);
    const GSProfileSnapshot s = card.Snap();
    EXPECT_EQ(s.commands[0x2A].count, 3u);
    EXPECT_EQ(s.commands[0x2A].min, 296u);
    EXPECT_EQ(s.commands[0x2A].max, 296u);
    EXPECT_EQ(s.commands[0x2A].cycles, 3u * 296u);
    EXPECT_TRUE(s.commandOpen);
    EXPECT_EQ(s.commandCode, 0x40);
    EXPECT_EQ(s.commandCycles, 296u) << "the current command's cost so far";
    EXPECT_EQ(s.activity.back().commands, 4u);
    EXPECT_GT(s.activity.back().idleCycles, s.activity.back().cycles / 2) << "the poll loop slept";

    StateNode report = DeviceState::GsProfileOf(*card.chip, "gs", nullptr, {});
    const std::string json = StateNodeToJsonText(report);
    EXPECT_NE(json.find("\"code\":42"), std::string::npos) << json;
    EXPECT_NE(report["text"].s.find("#2A"), std::string::npos) << report["text"].s;
}

/// main: CALL #0100 / JR; #0100: CALL #0180, 20 NOPs, RET; #0180: 30 NOPs, RET; ISR 67 every 320. Inclusive time of
/// #0180 = 30 * 4 + RET 10 = 130 on every call though interrupts land inside; #0100 = CALL 17 + 130 + 80 + RET 10 =
/// 237, exclusive 107
TEST(GSCardProfiler_Test, RoutinesInclusiveExclusiveWithInterruptsInside)
{
    Rom r = IsrRom(10);
    r.At(0x0007, {0xCD, 0x00, 0x01, 0x18, 0xFB});  // CALL #0100; JR #0007
    r.At(0x0100, {0xCD, 0x80, 0x01});              // CALL #0180
    r.Nops(0x0103, 20);
    r.At(0x0117, {0xC9});
    r.Nops(0x0180, 30);
    r.At(0x019E, {0xC9});
    ProgramCard card(r.bytes, "gsprof-routines.rom");
    card.chip->profiler().Start();
    card.Frame();
    const GSProfileSnapshot s = card.Snap();

    const GSProfileRoutine* inner = RoutineAt(s, 0x0180);
    const GSProfileRoutine* outer = RoutineAt(s, 0x0100);
    const GSProfileRoutine* isr = RoutineAt(s, 0x0038);
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(isr, nullptr);
    EXPECT_GT(inner->calls, 500u);
    EXPECT_EQ(inner->min, 130u);
    EXPECT_EQ(inner->max, 130u) << "interrupts inside never leak into it";
    EXPECT_EQ(inner->inclusive, inner->calls * 130u);
    EXPECT_EQ(inner->exclusive, inner->inclusive);
    EXPECT_EQ(inner->interrupts, 0u);
    EXPECT_EQ(outer->min, 237u);
    EXPECT_EQ(outer->max, 237u);
    EXPECT_EQ(outer->exclusive, outer->calls * 107u);
    EXPECT_EQ(isr->min, 67u);
    EXPECT_EQ(isr->max, 67u);
    EXPECT_EQ(isr->calls, s.isrTotal.count);
    EXPECT_EQ(s.stackOverflows, 0u);
}

/// The frame map: one ISR bar a period, its DAC fetch (LD A,(#6000), first in the ISR) inside the bar, a host data
/// write at card cycle 1700 (the boundary after it) in period 5 at cycle 100+
TEST(GSCardProfiler_Test, EventMapBinsHostAccessesAndDacFetches)
{
    Rom r = IsrRom(0);
    r.At(0x0038, {0x3A, 0x00, 0x60, 0xFB, 0xC9});  // LD A,(#6000); EI; RET
    ProgramCard card(r.bytes, "gsprof-events.rom");
    card.chip->profiler().Start();
    card.chip->startPortTrace();
    card.chip->handleFrameStart();
    card.chip->runFor(1700);
    card.chip->portDeviceOutMethod(SoundChip_GeneralSound::PORT_DATA, 0x55);
    card.chip->handleFrameEnd(SAMPLES_PER_FRAME);

    GSEventsQuery q;
    q.count = 8;
    StateNode n = DeviceState::GsEventsOf(*card.chip, "gs", q);
    ASSERT_TRUE(n["available"].b) << StateNodeToJsonText(n);
    EXPECT_EQ(n["period_cycles"].i, kPeriod);
    EXPECT_GT(n["periods"].i, 740);
    StateNode& rows = n["rows"];
    ASSERT_EQ(rows.size(), 8u);
    bool host = false;
    for (size_t i = 1; i < rows.size(); i++)  // period 0 holds the program's start
    {
        StateNode& row = rows.items[i];
        ASSERT_EQ(row["isr"].size(), 1u) << i;
        const int64_t accept = row["isr"].items[0]["accept"].i;
        const int64_t end = row["isr"].items[0]["end"].i;
        EXPECT_EQ(end - accept, 13 + 13 + 4 + 10) << i;
        int dac = 0;
        for (size_t e = 0; e < row["events"].size(); e++)
        {
            const StateNode& ev = row["events"].items[e];
            if (ev.items[1].s == "dac")
            {
                dac++;
                EXPECT_GT(ev.items[0].i, accept);
                EXPECT_LT(ev.items[0].i, end);
                EXPECT_EQ(ev.items[3].i, 0) << "channel 0: #6000";
            }
            if (ev.items[1].s == "host_data_write")
            {
                host = true;
                EXPECT_EQ(i, 5u);
                EXPECT_GE(ev.items[0].i, 100);
                EXPECT_LT(ev.items[0].i, 112) << "the run stops at the first boundary from 1700";
                EXPECT_EQ(ev.items[2].i, 0x55);
            }
        }
        EXPECT_EQ(dac, 1) << i;
    }
    EXPECT_TRUE(host) << n["text"].s;
    EXPECT_EQ(n["totals"]["host_data_write"].i, 1);
}

/// Off (the default): the card never runs the record loop for it, no working state, nothing published; stopped,
/// nothing more
TEST(GSCardProfiler_Test, OffRecordsNothing)
{
    ProgramCard card(IsrRom(10).bytes, "gsprof-off.rom");
    card.Frame();
    EXPECT_FALSE(card.chip->profiler().On());
    EXPECT_FALSE(card.chip->profiler().HasWork()) << "no profiled run";
    const GSProfileSnapshot off = card.Snap();
    EXPECT_EQ(off.frames, 0u);
    EXPECT_EQ(off.isrTotal.count, 0u);
    EXPECT_TRUE(off.routines.empty());

    card.chip->profiler().Start();
    card.Frame();
    card.chip->profiler().Stop();
    const uint64_t isrs = card.Snap().isrTotal.count;
    EXPECT_GT(isrs, 0u);
    card.Frame();
    EXPECT_EQ(card.Snap().frames, 1u);
    EXPECT_EQ(card.Snap().isrTotal.count, isrs) << "stopped: the data kept, nothing added";

    card.chip->profiler().Clear();
    EXPECT_EQ(card.Snap().frames, 0u);
}

namespace
{
class GSCardProfilerMachine_Test : public ::testing::TestWithParam<GSTypeKind>
{
protected:
    std::unique_ptr<GeneralSoundFitScope> _fit;
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _fit = std::make_unique<GeneralSoundFitScope>(GetParam());
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};
}  // namespace

/// The real firmware on a machine: start through the verb, on from the reset (the POST's calls), then a command each
/// frame (the dispatcher's row): the report's routines, commands and activity. The stock firmware idles with
/// interrupts off (it enables them only to play: soundchip_gs_bootdiag_test uploads its own ISR), so its ISR budget is
/// empty and the period requests merge into a pending one (`lost`). The exact budget is the hand-built programs'
/// (IsrBudgetIsExact). The events of the frame; errors for an unknown CPU
TEST_P(GSCardProfilerMachine_Test, FirmwareReport)
{
    // On from the reset: the firmware's POST calls its routines; then a command each frame (the dispatcher's row)
    EXPECT_EQ(GSProfileControl(_context, "start", "").status, 200);
    GeneralSoundCard* gs = _context->pSoundManager->getGeneralSound();
    ASSERT_NE(gs, nullptr);
    for (int f = 0; f < 400 && !gs->isReadyForCommands(); f++)
        EmulatorTestHelper::RunFramesFast(_emulator, 1);
    ASSERT_TRUE(gs->isReadyForCommands());
    StateNode n;
    for (int frames = 0; frames < 50; frames++)
    {
        GSSendRequest send;
        send.command = 0x23;
        GSSend(_context, send, 0);
        EmulatorTestHelper::RunFramesFast(_emulator, 1);
        n = DeviceState::GsProfile(_context, {});
        if (n["commands"].size() > 0 && n["activity"].size() > 0)
            break;
    }
    ASSERT_TRUE(n["available"].b) << StateNodeToJsonText(n);
    EXPECT_TRUE(n["on"].b);
    EXPECT_GE(n["frames"].i, 1);
    EXPECT_GT(n["period_cycles"].i, 0);
    EXPECT_GT(n["routines"].size(), 0u) << n["text"].s;
    EXPECT_GT(n["commands"].size(), 0u) << "the dispatcher's row for #23: " << n["text"].s;
    ASSERT_GT(n["activity"].size(), 0u);
    const StateNode& last = n["activity"].items.back();
    EXPECT_EQ(n["isr"]["since_start"]["count"].i, 0) << "the idle firmware takes no interrupt: " << n["text"].s;
    EXPECT_EQ(last.find("interrupts")->i, 0);
    EXPECT_GT(last.find("lost")->i, 0) << "the requests merged while interrupts are off";

    StateNode e = DeviceState::GsEvents(_context, {});
    ASSERT_TRUE(e["available"].b);
    EXPECT_GT(e["periods"].i, 0) << e["text"].s;

    EXPECT_EQ(GSProfileControl(_context, "stop", "").status, 200);
    EXPECT_EQ(GSProfileControl(_context, "bogus", "").status, 400);
    EXPECT_EQ(GSProfileControl(_context, "start", "zz").status, 404);
    GSProfileQuery zz;
    zz.cpu = "zz";
    EXPECT_FALSE(DeviceState::GsProfile(_context, zz)["available"].b);
}

INSTANTIATE_TEST_SUITE_P(Cards, GSCardProfilerMachine_Test, ::testing::Values(GSTypeKind::Z80, GSTypeKind::NGS));

/// The lightweight player has no card CPU
TEST(GSCardProfilerLW_Test, NotAvailable)
{
    GeneralSoundFitScope fit{GSTypeKind::LW};
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
    ASSERT_NE(emulator, nullptr);
    StateNode n = DeviceState::GsProfile(emulator->GetContext(), {});
    EXPECT_FALSE(n["available"].b);
    EXPECT_NE(n["description"].s.find("no card CPU"), std::string::npos) << n["description"].s;
    EXPECT_EQ(GSProfileControl(emulator->GetContext(), "start", "").status, 404);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A/B of the card CPU loop, profiler off vs on, interleaved rounds (run by hand:
/// --gtest_also_run_disabled_tests --gtest_filter='*DISABLED_ProfilerCostAB*'). Two loads: the busy hand-built
/// program (no idle sleep, a call per instruction) and the gs105a firmware after boot (its idle loop slept through)
TEST(GSCardProfiler_Test, DISABLED_ProfilerCostAB)
{
    Rom busy = IsrRom(10);
    busy.At(0x0007, {0xCD, 0x00, 0x01, 0x18, 0xFB});
    busy.At(0x0100, {0xCD, 0x80, 0x01});
    busy.At(0x0117, {0xC9});
    busy.At(0x019E, {0xC9});
    struct Load
    {
        const char* name;
        std::unique_ptr<ProgramCard> card;
    };
    Load loads[2] = {{"busy program", std::make_unique<ProgramCard>(busy.bytes, "gsprof-ab.rom")},
                     {"gs105a idle", std::make_unique<ProgramCard>(std::vector<uint8_t>(1, 0x00), "gsprof-ab2.rom")}};
    loads[1].card->chip->loadROM("rom/gs105a.rom");
    for (int i = 0; i < 100; i++)
        loads[1].card->Frame();  // boot (POST) done: the firmware polls

    constexpr int kRounds = 8;
    constexpr int kFrames = 50;
    for (Load& load : loads)
    {
        double sum[2] = {};
        double best[2] = {1e30, 1e30};
        for (int round = 0; round < kRounds; round++)
            for (int on = 0; on < 2; on++)
            {
                const int mode = (round & 1) ? 1 - on : on;  // ABBA order
                if (mode)
                    load.card->chip->profiler().Start();
                else
                    load.card->chip->profiler().Stop();
                load.card->Frame();  // the switch applies at the next run
                const auto t0 = std::chrono::steady_clock::now();
                for (int f = 0; f < kFrames; f++)
                    load.card->Frame();
                const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / kFrames;
                sum[mode] += us;
                best[mode] = std::min(best[mode], us);
            }
        std::printf("[A/B] %-13s off %8.1f us/frame (best %8.1f)  on %8.1f us/frame (best %8.1f)  on/off %.2fx\n",
                    load.name, sum[0] / kRounds, best[0], sum[1] / kRounds, best[1], sum[1] / sum[0]);
    }
}
