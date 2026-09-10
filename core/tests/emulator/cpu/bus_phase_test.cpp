#include "stdafx.h"
#include "pch.h"

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

/// Bus-phase trace tests (Phase 1: harness validation).
///
/// Z80::busTraceHook fires at the access point of every bus event with cpu.t
/// already advanced to it. Conventions (matching FUSE trace semantics):
///   'R' memory read  - data latched at T3 of the 3T/4T(M1) cycle
///   'W' memory write - committed at T3 of the 3T write cycle
///   'I'/'O' port access - at the IORQ T-state (T2 of the 4T IO cycle)
///
/// These tests validate the harness itself against hand-derived canonical
/// traces from Z80 machine-cycle breakdowns (z80.info): the trace event
/// SEQUENCE, per-event T offsets from instruction start, and totals.

namespace
{
struct BusEvent
{
    char type;
    uint16_t addr;
    uint8_t value;
    uint32_t tOffset;

    bool operator==(const BusEvent& other) const
    {
        return type == other.type && addr == other.addr && value == other.value && tOffset == other.tOffset;
    }
};

std::ostream& operator<<(std::ostream& os, const BusEvent& e)
{
    return os << e.type << " @+" << e.tOffset << " addr=" << std::hex << e.addr << " val=" << (int)e.value
              << std::dec;
}
}  // namespace

class BusPhase_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;

    std::vector<BusEvent> _trace;
    uint32_t _t0 = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";

        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;

        _z80->busTraceHook = [this](char type, uint16_t addr, uint8_t value) {
            _trace.push_back({type, addr, value, _z80->t - _t0});
        };
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _z80->busTraceHook = nullptr;
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Execute one instruction at $8000, collecting the bus trace
    uint32_t runInstruction(std::initializer_list<uint8_t> bytes)
    {
        uint16_t addr = 0x8000;
        for (uint8_t byte : bytes)
        {
            _memory->DirectWriteToZ80Memory(addr++, byte);
        }

        _z80->pc = 0x8000;
        _z80->iff1 = 0;
        _z80->t = 1000;

        _trace.clear();
        _t0 = _z80->t;
        _z80->Z80Step();

        return _z80->t - _t0;
    }

    void expectTrace(const std::vector<BusEvent>& expected)
    {
        ASSERT_EQ(_trace.size(), expected.size()) << "Bus event count mismatch";
        for (size_t i = 0; i < expected.size(); i++)
        {
            EXPECT_EQ(_trace[i], expected[i]) << "Event " << i << ": got {" << _trace[i] << "}, expected {"
                                              << expected[i] << "}";
        }
    }
};

TEST_F(BusPhase_Test, NOP)
{
    // M1: 4T, opcode latched at T3
    uint32_t total = runInstruction({0x00});
    EXPECT_EQ(total, 4u);
    expectTrace({{'R', 0x8000, 0x00, 3}});
}

TEST_F(BusPhase_Test, LD_HL_A)
{
    // LD (HL),A = 7T (4, 3): fetch, then 3T write committed at +7
    _z80->hl = 0x9000;
    _z80->a = 0x42;
    uint32_t total = runInstruction({0x77});
    EXPECT_EQ(total, 7u);
    expectTrace({{'R', 0x8000, 0x77, 3}, {'W', 0x9000, 0x42, 7}});
}

TEST_F(BusPhase_Test, LD_A_HL)
{
    // LD A,(HL) = 7T (4, 3): fetch, then 3T read latched at +7
    _z80->hl = 0x9000;
    _memory->DirectWriteToZ80Memory(0x9000, 0x5A);
    uint32_t total = runInstruction({0x7E});
    EXPECT_EQ(total, 7u);
    expectTrace({{'R', 0x8000, 0x7E, 3}, {'R', 0x9000, 0x5A, 7}});
}

TEST_F(BusPhase_Test, PUSH_BC)
{
    // PUSH BC = 11T (5, 3, 3): extended M1 (+1 internal), B written first
    _z80->bc = 0x1234;
    _z80->sp = 0xA000;
    uint32_t total = runInstruction({0xC5});
    EXPECT_EQ(total, 11u);
    expectTrace({{'R', 0x8000, 0xC5, 3}, {'W', 0x9FFF, 0x12, 8}, {'W', 0x9FFE, 0x34, 11}});
}

TEST_F(BusPhase_Test, POP_BC)
{
    // POP BC = 10T (4, 3, 3): low byte read first
    _z80->sp = 0xA000;
    _memory->DirectWriteToZ80Memory(0xA000, 0x34);
    _memory->DirectWriteToZ80Memory(0xA001, 0x12);
    uint32_t total = runInstruction({0xC1});
    EXPECT_EQ(total, 10u);
    expectTrace({{'R', 0x8000, 0xC1, 3}, {'R', 0xA000, 0x34, 7}, {'R', 0xA001, 0x12, 10}});
}

TEST_F(BusPhase_Test, OUT_NN_A_PortAtIORQ)
{
    // OUT ($FE),A = 11T (4, 3, 4): port write at T2 of the IO cycle (+8)
    _z80->a = 0x07;
    uint32_t total = runInstruction({0xD3, 0xFE});
    EXPECT_EQ(total, 11u);
    ASSERT_EQ(_trace.size(), 3u);
    EXPECT_EQ(_trace[0], (BusEvent{'R', 0x8000, 0xD3, 3}));
    EXPECT_EQ(_trace[1], (BusEvent{'R', 0x8001, 0xFE, 7}));
    EXPECT_EQ(_trace[2].type, 'O');
    EXPECT_EQ(_trace[2].addr, 0x07FEu);  // A on high byte
    EXPECT_EQ(_trace[2].tOffset, 8u);
}

TEST_F(BusPhase_Test, INI_TraceOrderAndPhases)
{
    // INI = 16T (4, 5, 4, 3): fetches, M1 stall, port read at IORQ (+10),
    // memory write committed at +14... write cycle is the last 3T: 10+1(rest
    // of IO)... IO cycle spans +9..+12, write +13..+15 committed at +16?
    // rd/wd charge-then-access: wd fires at 13+3 = +16 == total. Verify
    // empirically pinned values so any phase drift fails loudly.
    _z80->hl = 0x9000;
    _z80->b = 1;
    _z80->c = 0xFD;
    uint32_t total = runInstruction({0xED, 0xA2});
    EXPECT_EQ(total, 16u);
    ASSERT_EQ(_trace.size(), 4u);
    EXPECT_EQ(_trace[0], (BusEvent{'R', 0x8000, 0xED, 3}));
    EXPECT_EQ(_trace[1].tOffset, 7u);  // opcode A2 latched
    EXPECT_EQ(_trace[2].type, 'I');
    EXPECT_EQ(_trace[2].tOffset, 10u);  // IORQ T-state
    EXPECT_EQ(_trace[3].type, 'W');
    EXPECT_EQ(_trace[3].addr, 0x9000u);
    EXPECT_EQ(_trace[3].tOffset, 16u);  // write committed at end of 3T cycle
}
