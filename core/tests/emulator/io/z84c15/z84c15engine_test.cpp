// Z84C15Engine (core/src/emulator/io/z84c15/z84c15engine.h): the Z84C15 library as a machine's CPU
// engine. The adapter is machine neutral, so it is checked on a plain Pentagon with the engine
// installed: the FUSE vectors through the host's bus paths (bus trace at the same T-states as the
// native core), the TTD CPU-state capture / restore with the zero-copy register file, and the
// chip's programmed waits seen by the host's clock.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/fusevectors.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z84c15/z84c15engine.h"
#include "emulator/memory/memory.h"

class Z84C15Engine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    Z84Lib::Z84C15 _chip;
    std::unique_ptr<Z84C15Engine> _engine;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
        _memory->SetRAMPageToBank0(6);  // RAM at #0000 too
        _engine = std::make_unique<Z84C15Engine>(_context, _z80, _chip);
        _engine->Install(nullptr);
        // The chip without programmed waits (WCR = 0, as after the Sprinter BIOS's InitCpuPorts)
        _chip.Write(0xEE, 0x00);
        _chip.Write(0xEF, 0x00);
    }

    void TearDown() override
    {
        _engine.reset();
        if (_emulator)
        {
            _z80->busTraceHook = nullptr;
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    void Load(uint16_t addr, const std::vector<uint8_t>& code)
    {
        for (uint8_t b : code)
            _memory->DirectWriteToZ80Memory(addr++, b);
    }
};

// The FUSE vectors through the adapter: every memory and port event reaches the host's paths at the
// T-state the native core puts it at (FusePhase_Test's mapping), the final state matches - with the
// CMOS core's OUT (C),0 = #FF and unreal-z80's MEMPTR after repeating block I/O (PC + 1).
// ~1 300 short cases; internal cycles are not reported to the host (no ULA contention to apply)
TEST_F(Z84C15Engine_Test, FuseVectorsThroughTheHostBus)
{
    auto cases = FuseVectors::LoadCases();
    ASSERT_GT(cases.size(), 1000u);
    FuseVectors::FusePortDecoder fuseDecoder(_context);
    PortDecoder* original = _context->pPortDecoder;
    _context->pPortDecoder = &fuseDecoder;

    std::vector<FuseVectors::BusEvent> trace;
    uint32_t t0 = 0;
    _z80->busTraceHook = [&](char type, uint16_t addr, uint8_t value) { trace.push_back({type, addr, value, _z80->t - t0}); };

    const std::set<std::string>& skipAF = FuseVectors::SkipAFCases();
    const std::set<std::string> repeatIoMemptr = {"edb2_1", "edb3_1", "edba_1", "edbb_1"};
    int passed = 0;
    int failed = 0;
    std::string report;
    for (auto& [name, tc] : cases)
    {
        if (tc.expTotal == 0)
            continue;
        if (name.rfind("ed71", 0) == 0)
            for (FuseVectors::BusEvent& e : tc.expectedTrace)
                if (e.type == 'O')
                    e.value = 0xFF;  // CMOS
        if (repeatIoMemptr.count(name))
            tc.expRegs[12] = static_cast<uint16_t>(tc.expRegs[11] + 1);

        FuseVectors::LoadState(_z80, _memory, tc);
        trace.clear();
        _z80->t = 10000;
        t0 = _z80->t;
        int steps = 0;
        while (_z80->t - t0 < tc.expTotal && steps++ < 64)
            _z80->EngineStep();

        std::vector<std::string> issues;
        if (_z80->t - t0 != tc.expTotal)
            issues.push_back("total " + std::to_string(_z80->t - t0) + " != " + std::to_string(tc.expTotal));
        FuseVectors::CompareTrace(trace, tc, issues);
        FuseVectors::CompareFinalState(_z80, tc, skipAF.count(name) > 0, issues);
        if (issues.empty())
            passed++;
        else
        {
            failed++;
            if (report.size() < 6000)
            {
                report += "\n[" + name + "] ";
                for (size_t i = 0; i < issues.size() && i < 4; i++)
                    report += issues[i] + "; ";
            }
        }
    }
    _context->pPortDecoder = original;
    EXPECT_EQ(failed, 0) << passed << " passed" << report;
}

// TTD's CPU state (ttd::CaptureCpuState / RestoreCpuState) is the whole CPU for the engine too: the
// registers are the library's own (zero copy) and the boundary is mirrored into Z80State after every
// step. A capture taken behind a pending prefix, after EI and after LD A,I replays identically
TEST_F(Z84C15Engine_Test, CpuStateCaptureRestoreReplaysExactly)
{
    // A loop with every boundary kind: EI (INT shadow), DD DD (a pending prefix), LD A,I, HALT-free
    Load(0x8000, {0xFB,                    // EI
                  0xDD, 0xDD, 0x21, 0x34, 0x12,  // DD; LD IX,#1234
                  0xED, 0x57,              // LD A,I
                  0x3C,                    // INC A
                  0xDD, 0x23,              // INC IX
                  0xF3,                    // DI
                  0x18, 0xF2});            // JR #8000
    _z80->pc = 0x8000;
    _z80->sp = 0xC000;

    struct Point
    {
        uint16_t pc, af, ix;
        uint8_t boundary;
        uint32_t t;
    };
    auto run = [&](int steps) {
        std::vector<Point> points;
        for (int i = 0; i < steps; i++)
        {
            _z80->EngineStep();
            points.push_back({_z80->pc, _z80->af, _z80->ix, _z80->boundary, _z80->t});
        }
        return points;
    };

    run(2);  // EI, then the redundant DD: a prefix is pending
    ASSERT_EQ(_z80->boundary, Z80_BOUNDARY_PREFIX_DD);
    const ttd::TTDCpuState saved = ttd::CaptureCpuState(*_z80);
    const uint32_t savedT = _z80->t;  // the frame T-state is TTD's machine-state part (cpu_t_in_frame)
    const std::vector<Point> first = run(40);

    // Something else runs in between (and leaves another boundary in the library)
    run(5);
    ttd::RestoreCpuState(saved, _z80);
    _z80->t = savedT;
    const std::vector<Point> second = run(40);

    ASSERT_EQ(first.size(), second.size());
    for (size_t i = 0; i < first.size(); i++)
    {
        EXPECT_EQ(first[i].pc, second[i].pc) << "step " << i;
        EXPECT_EQ(first[i].af, second[i].af) << "step " << i;
        EXPECT_EQ(first[i].ix, second[i].ix) << "step " << i;
        EXPECT_EQ(first[i].boundary, second[i].boundary) << "step " << i;
        EXPECT_EQ(first[i].t, second[i].t) << "step " << i;
    }
}

// The chip's programmed waits reach the host's clock, and the board's /WAIT from the host (a
// decoder's AddWaitStates inside the port callback) adds to them
TEST_F(Z84C15Engine_Test, ProgrammedWaitsAdvanceTheHostClock)
{
    Load(0x8000, {0x00, 0x3A, 0x00, 0x90});  // NOP; LD A,(#9000)
    _z80->pc = 0x8000;
    _z80->t = 1000;
    _z80->EngineStep();
    EXPECT_EQ(_z80->t, 1004u);

    _chip.Write(0xEE, 0x00);
    _chip.Write(0xEF, 0x14);  // one memory wait + the M1 extension
    _z80->EngineStep();
    EXPECT_EQ(_z80->t, 1004u + 13 + 4 + 1) << "four memory cycles, one M1";
    EXPECT_EQ(_z80->pc, 0x8004);
}

// The engine owns the INT acknowledge: IM2 through the chip's chain vector, pushes through the host
TEST_F(Z84C15Engine_Test, InterruptAcknowledgeGoesThroughTheEngine)
{
    Load(0x80FE, {0x00, 0x90});  // vector table: I = #80, vector #FE -> #9000
    _z80->pc = 0x8000;
    _z80->sp = 0xC000;
    _z80->i = 0x80;
    _z80->im = 2;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->t = 2000;
    _z80->HandleINT(0xFE);
    EXPECT_EQ(_z80->pc, 0x9000);
    EXPECT_EQ(_z80->t, 2019u);
    EXPECT_EQ(_z80->sp, 0xBFFE);
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0xBFFF), 0x80);
    EXPECT_EQ(_z80->iff1, 0);
}
