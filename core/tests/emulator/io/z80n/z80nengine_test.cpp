// Z80NEngine (core/src/emulator/io/z80n/z80nengine.h): the unreal-next-z80 library as a machine's CPU engine. The adapter is
// checked on a plain Pentagon with the engine installed, as the Z84C15 adapter is: the FUSE vectors through the host's bus
// paths (bus trace at the same T-states as the native core), the TTD CPU-state capture / restore with the zero-copy register
// file, the interrupt acknowledge, and what is the Next's alone: the NEXTREG instructions reach the machine without a port
// cycle, RETI reaches the interrupt source.

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
#include "emulator/io/z80n/z80nengine.h"
#include "emulator/memory/memory.h"

namespace
{
/// A port decoder that counts the port cycles it sees
class CountingPortDecoder : public PortDecoder
{
public:
    explicit CountingPortDecoder(EmulatorContext* context) : PortDecoder(context) {}
    void reset() override {}
    uint8_t DecodePortIn(uint16_t, uint16_t) override
    {
        ++ins;
        _lastPortDecoded = true;
        return 0xFF;
    }
    void DecodePortOut(uint16_t, uint8_t, uint16_t) override
    {
        ++outs;
        _lastPortDecoded = true;
    }
    int ins = 0, outs = 0;
};

struct RecordingNextRegHost : INextRegHost
{
    void WriteNextReg(uint8_t reg, uint8_t value) override { writes.emplace_back(reg, value); }
    std::vector<std::pair<uint8_t, uint8_t>> writes;
};

struct InterruptSourceGuard
{
    InterruptSourceGuard(Z80* z, IInterruptSource* source) : _z(z), _old(z->GetInterruptSource()) { z->SetInterruptSource(source); }
    ~InterruptSourceGuard() { _z->SetInterruptSource(_old); }
    Z80* _z;
    IInterruptSource* _old;
};

struct RecordingSource : IInterruptSource
{
    bool IsIntAsserted(uint32_t) override { return false; }
    uint8_t AcknowledgeInterrupt(uint32_t) override { return 0xFF; }
    void OnReti() override { ++retis; }
    int retis = 0;
};
}  // namespace

class Z80NEngine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    std::unique_ptr<Z80NEngine> _engine;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
        _memory->SetRAMPageToBank0(6);  // RAM at #0000 too
        _engine = std::make_unique<Z80NEngine>(_context, _z80);
        _engine->Install();
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

// The FUSE vectors through the adapter: every memory and port event reaches the host's paths at the T-state the native core
// puts it at, the final state matches (the NMOS baseline of the library: OUT (C),0 writes 0, and unreal-z80's MEMPTR after
// repeating block I/O). ~1 300 short cases; internal cycles are not reported to the host (no ULA contention to apply)
TEST_F(Z80NEngine_Test, FuseVectorsThroughTheHostBus)
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

// TTD's CPU state is the whole CPU for the engine too: the registers are the library's own (zero copy) and the boundary is
// mirrored into Z80State after every step. A capture taken behind a pending prefix, after EI and after LD A,I replays identically
TEST_F(Z80NEngine_Test, CpuStateCaptureRestoreReplaysExactly)
{
    Load(0x8000, {0xFB,                          // EI
                  0xDD, 0xDD, 0x21, 0x34, 0x12,  // DD; LD IX,#1234
                  0xED, 0x57,                    // LD A,I
                  0x3C,                          // INC A
                  0xDD, 0x23,                    // INC IX
                  0xF3,                          // DI
                  0x18, 0xF2});                  // JR #8000
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
    const uint32_t savedT = _z80->t;
    const std::vector<Point> first = run(40);

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

// IM2 through the host's source: the vector the host gives, the pushes through the host's memory
TEST_F(Z80NEngine_Test, InterruptAcknowledgeGoesThroughTheEngine)
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

// NEXTREG n,nn and NEXTREG n,A call the machine and make no port cycle; the instruction takes its table time (20 and 17 T)
TEST_F(Z80NEngine_Test, NextRegInstructionsReachTheMachineWithoutAPortCycle)
{
    CountingPortDecoder counting(_context);
    PortDecoder* original = _context->pPortDecoder;
    _context->pPortDecoder = &counting;
    RecordingNextRegHost host;
    _engine->SetNextRegHost(&host);

    Load(0x8000, {0xED, 0x91, 0x07, 0x03,   // NEXTREG 7,3
                  0x3E, 0x55,               // LD A,#55
                  0xED, 0x92, 0x12});       // NEXTREG #12,A
    _z80->pc = 0x8000;
    _z80->t = 3000;
    _z80->EngineStep();
    EXPECT_EQ(_z80->t, 3020u);
    EXPECT_EQ(_z80->pc, 0x8004);
    _z80->EngineStep();
    _z80->t = 3100;
    _z80->EngineStep();
    EXPECT_EQ(_z80->t, 3117u);
    EXPECT_EQ(_z80->pc, 0x8009);

    ASSERT_EQ(host.writes.size(), 2u);
    EXPECT_EQ(host.writes[0], (std::pair<uint8_t, uint8_t>{0x07, 0x03}));
    EXPECT_EQ(host.writes[1], (std::pair<uint8_t, uint8_t>{0x12, 0x55}));
    EXPECT_EQ(counting.ins + counting.outs, 0) << "NEXTREG makes no I/O cycle";
    _context->pPortDecoder = original;
}

// The machine may change what the CPU sees while the instruction runs: a NEXTREG that adds wait states advances the clock
TEST_F(Z80NEngine_Test, ATimeAddedByTheMachineInsideNextRegIsKept)
{
    struct SlowHost : INextRegHost
    {
        explicit SlowHost(Z80* z) : z80(z) {}
        void WriteNextReg(uint8_t, uint8_t) override { z80->tt += static_cast<uint32_t>(5) << 8; }
        Z80* z80;
    } host(_z80);
    _engine->SetNextRegHost(&host);
    Load(0x8000, {0xED, 0x91, 0x07, 0x03});
    _z80->pc = 0x8000;
    _z80->t = 4000;
    _z80->EngineStep();
    EXPECT_EQ(_z80->t, 4025u) << "20 T and 5 T of the machine's own";
}

// RETI reaches the host's interrupt source as it does from the native interpreter
TEST_F(Z80NEngine_Test, RetiReachesTheInterruptSource)
{
    RecordingSource source;
    InterruptSourceGuard guard(_z80, &source);
    Load(0x8000, {0xED, 0x4D});
    _z80->pc = 0x8000;
    _z80->sp = 0xC000;
    _memory->DirectWriteToZ80Memory(0xC000, 0x34);
    _memory->DirectWriteToZ80Memory(0xC001, 0x12);
    _z80->EngineStep();
    EXPECT_EQ(_z80->pc, 0x1234);
    EXPECT_EQ(source.retis, 1);
}

// Stackless NMI (NR #C0 bit 3): the acknowledge writes no stack, the return address goes to the store (NR #C2 / #C3), SP still falls by
// 2; RETN reads the address back from the store - and what the machine changed in it in between (the Multiface ROM sets the address
// it restores to)
TEST_F(Z80NEngine_Test, StacklessNmiKeepsTheReturnAddressInTheStore)
{
    struct Store : INmiReturnStore
    {
        void StoreNmiReturn(uint8_t low, uint8_t high) override { value = static_cast<uint16_t>(low | (high << 8)); stores++; }
        uint16_t LoadNmiReturn() const override { return value; }
        uint16_t value = 0;
        int stores = 0;
    } store;
    _engine->SetStacklessNmi(true, &store);

    Load(0x0066, {0xED, 0x45});  // the NMI handler: RETN
    _z80->pc = 0x8123;
    _z80->sp = 0xC000;
    _memory->DirectWriteToZ80Memory(0xBFFE, 0x77);
    _memory->DirectWriteToZ80Memory(0xBFFF, 0x66);
    _z80->iff1 = _z80->iff2 = 1;
    _z80->t = 1000;
    _engine->AcknowledgeNmi();
    EXPECT_EQ(_z80->pc, 0x0066);
    EXPECT_EQ(_z80->sp, 0xBFFE);
    EXPECT_EQ(store.stores, 1);
    EXPECT_EQ(store.value, 0x8123) << "the return address is in the store";
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0xBFFE), 0x77) << "and not on the stack";
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0xBFFF), 0x66);

    store.value = 0x9000;  // the handler changed where to return
    _z80->EngineStep();    // RETN
    EXPECT_EQ(_z80->pc, 0x9000);
    EXPECT_EQ(_z80->sp, 0xC000);
}
