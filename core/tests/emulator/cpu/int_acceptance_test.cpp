#include "stdafx.h"
#include "pch.h"

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// Interrupt acceptance sequence tests (tier 5 of the phase-test plan).
///
/// Guards the HandleINT timing/state contract:
///   IM0/IM1: 13T (7T INT ack M1 + 3T push PCH + 3T push PCL), handler $0038
///   IM2:     19T (as above + 3T vector low + 3T vector high), handler from
///            table at I*256 + vector (0xFF on Pentagon's open bus)
///   - Return address pushed high byte first (SP-1 = PCH, SP-2 = PCL)
///   - IFF1/IFF2 cleared on acceptance
///   - HALT released: pushed return address points past the HALT opcode
///   - EI delay: the instruction immediately after EI cannot be interrupted

class IntAcceptance_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    unsigned _intStart = 0;
    unsigned _intEnd = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);

        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
        _intStart = _context->config.intstart;
        _intEnd = _context->config.intstart + _context->config.intlen;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Position CPU inside the INT window with interrupts enabled and
    /// attempt acceptance; returns true if the interrupt was taken
    bool acceptInterrupt()
    {
        _z80->t = _intStart + 2;  // Inside the window (strict > int_start)
        _z80->eipos = 0;
        return _z80->ProcessInterrupts(false, _intStart, _intEnd);
    }
};

TEST_F(IntAcceptance_Test, IM1_Duration13T_HandlerAndStack)
{
    _z80->im = 1;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8123;
    _z80->sp = 0xA000;
    _z80->halted = 0;

    uint32_t t0 = _intStart + 2;
    ASSERT_TRUE(acceptInterrupt());

    EXPECT_EQ(_z80->t - t0, 13u) << "IM1 acceptance is 13T";
    EXPECT_EQ(_z80->pc, 0x0038u);
    EXPECT_EQ(_z80->sp, 0x9FFEu);
    EXPECT_EQ(_z80->DirectRead(0x9FFF), 0x81) << "PCH pushed at SP-1";
    EXPECT_EQ(_z80->DirectRead(0x9FFE), 0x23) << "PCL pushed at SP-2";
    EXPECT_EQ(_z80->iff1, 0u);
    EXPECT_EQ(_z80->iff2, 0u);
}

TEST_F(IntAcceptance_Test, IM2_Duration19T_VectorFetch)
{
    _z80->im = 2;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->i = 0xBE;
    _z80->pc = 0x8000;
    _z80->sp = 0xA000;
    _z80->halted = 0;

    // Vector table entry at $BEFF (I=BE, bus=FF): handler $C000
    _memory->DirectWriteToZ80Memory(0xBEFF, 0x00);
    _memory->DirectWriteToZ80Memory(0xBF00, 0xC0);

    uint32_t t0 = _intStart + 2;
    ASSERT_TRUE(acceptInterrupt());

    EXPECT_EQ(_z80->t - t0, 19u) << "IM2 acceptance is 19T";
    EXPECT_EQ(_z80->pc, 0xC000u) << "Handler address from vector table at I*256+0xFF";
    EXPECT_EQ(_z80->sp, 0x9FFEu);
    EXPECT_EQ(_z80->DirectRead(0x9FFF), 0x80);
    EXPECT_EQ(_z80->DirectRead(0x9FFE), 0x00);
    EXPECT_EQ(_z80->iff1, 0u);
}

TEST_F(IntAcceptance_Test, HALT_ReleasedWithReturnPastHalt)
{
    _z80->im = 1;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->sp = 0xA000;

    // HALT at $8100; CPU parked on it
    _memory->DirectWriteToZ80Memory(0x8100, 0x76);
    _z80->pc = 0x8100;
    _z80->halted = 1;

    ASSERT_TRUE(acceptInterrupt());

    EXPECT_EQ(_z80->pc, 0x0038u);
    // Return address must point PAST the HALT so RET resumes after it
    EXPECT_EQ(_z80->DirectRead(0x9FFF), 0x81);
    EXPECT_EQ(_z80->DirectRead(0x9FFE), 0x01);
}

TEST_F(IntAcceptance_Test, EIDelay_BlocksAcceptanceAtEIPos)
{
    _z80->im = 1;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8000;

    // eipos == t models "the instruction right after EI is executing":
    // acceptance must be deferred by exactly one instruction
    _z80->t = _intStart + 2;
    _z80->eipos = _z80->t;

    bool taken = _z80->ProcessInterrupts(false, _intStart, _intEnd);
    EXPECT_FALSE(taken) << "INT must not be accepted at the EI position";

    // One instruction later the same pending INT must be accepted
    _z80->t = _intStart + 6;  // Still inside the window
    taken = _z80->ProcessInterrupts(false, _intStart, _intEnd);
    EXPECT_TRUE(taken);
}

TEST_F(IntAcceptance_Test, NoAcceptanceOutsideWindow)
{
    _z80->im = 1;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8000;
    _z80->int_pending = false;

    // Exactly at int_start: strict sampling means not yet visible
    _z80->t = _intStart;
    _z80->eipos = 0;
    EXPECT_FALSE(_z80->ProcessInterrupts(false, _intStart, _intEnd));

    // Past the window end with no pending latch: no acceptance
    _z80->int_pending = false;
    _z80->t = _intEnd + 10;
    EXPECT_FALSE(_z80->ProcessInterrupts(false, _intStart, _intEnd));
}
