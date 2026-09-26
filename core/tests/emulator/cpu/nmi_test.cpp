/// @file Z80 non-maskable interrupt tests - acceptance cycle, IFF handling,
/// RETN restore, HALT release, INT priority and request coalescing.
///
/// Mirrors the acceptance-timing contract of int_test.cpp (implementation-plan
/// Task 6 of docs/inprogress/2026-09-07-scorpion-zs256-clone):
///   Acceptance: 11T (5T restart fetch M1 + 3T push PCH + 3T push PCL), vector $0066
///   - Return address pushed high byte first (SP-1 = PCH, SP-2 = PCL)
///   - IFF1 cleared, IFF2 unchanged (UM0080 table 1; Sean Young's nested-NMI
///     test); accepted regardless of IFF1 (non-maskable)
///   - accepted right after EI (the EI shadow is INT-only), refused right after
///     another NMI acknowledge and inside a split prefix chain
///   - the acknowledge M1 advances R; LD A,I/R + NMI keeps P/V
///   - HALT released: pushed return address points past the HALT opcode
///   - NMI has priority over a pending INT at the same instruction boundary
///   - Double requests coalesce into a single acceptance
///   - RETN restores IFF1 from IFF2 and ends the NMI session (plain RET does not)

#include "stdafx.h"
#include "pch.h"
#include "_helpers/emulatortesthelper.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

class NmiAcceptance_Test : public ::testing::Test
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

    /// Request an NMI and attempt acceptance at a controlled t-state;
    /// returns true if the NMI was taken
    bool acceptNmi()
    {
        _z80->RequestNonMaskedInterrupt();
        _z80->t = _intStart + 2;
        _z80->boundary = Z80_BOUNDARY_NONE;
        return _z80->ProcessInterrupts(false, _intStart, _intEnd);
    }
};

TEST_F(NmiAcceptance_Test, AcceptCycle11TVectorStackAndIFF)
{
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8123;
    _z80->sp = 0xA000;
    _z80->halted = 0;

    uint32_t t0 = _intStart + 2;
    ASSERT_TRUE(acceptNmi());

    EXPECT_EQ(_z80->t - t0, 11u) << "NMI acceptance is 11T";
    EXPECT_EQ(_z80->pc, 0x0066u) << "NMI restart vector";
    EXPECT_EQ(_z80->sp, 0x9FFEu);
    EXPECT_EQ(_z80->DirectRead(0x9FFF), 0x81) << "PCH pushed at SP-1";
    EXPECT_EQ(_z80->DirectRead(0x9FFE), 0x23) << "PCL pushed at SP-2";
    EXPECT_EQ(_z80->iff1, 0u) << "maskable interrupts disabled in the handler";
    EXPECT_EQ(_z80->iff2, 1u) << "IFF2 unchanged: keeps the pre-NMI state for RETN";
    EXPECT_TRUE(_z80->nmi_in_progress);

    // No second acceptance without a new request
    _z80->t = _intStart + 20;
    EXPECT_FALSE(_z80->ProcessInterrupts(false, _intStart, _intEnd));
    EXPECT_EQ(_z80->pc, 0x0066u);
    EXPECT_EQ(_z80->sp, 0x9FFEu) << "the return frame is not consumed twice";
}

TEST_F(NmiAcceptance_Test, AcceptedDespiteDisabledInterrupts)
{
    _z80->iff1 = _z80->iff2 = 0;  // DI
    _z80->pc = 0x8000;
    _z80->sp = 0xA000;

    ASSERT_TRUE(acceptNmi());

    EXPECT_EQ(_z80->pc, 0x0066u) << "NMI is non-maskable: accepted with IFF1 clear";
    EXPECT_EQ(_z80->iff1, 0u);
    EXPECT_EQ(_z80->iff2, 0u) << "IFF2 unchanged (was 0 before the NMI)";
}

TEST_F(NmiAcceptance_Test, HaltReleasedWithReturnPastHalt)
{
    _z80->iff1 = _z80->iff2 = 1;
    _z80->sp = 0xA000;

    // HALT at $8100; CPU parked on it
    _memory->DirectWriteToZ80Memory(0x8100, 0x76);
    _z80->pc = 0x8100;
    _z80->halted = 1;

    ASSERT_TRUE(acceptNmi());

    EXPECT_EQ(_z80->pc, 0x0066u);
    EXPECT_EQ(_z80->halted, 0u);
    // Return address must point PAST the HALT so RET resumes after it
    EXPECT_EQ(_z80->DirectRead(0x9FFF), 0x81);
    EXPECT_EQ(_z80->DirectRead(0x9FFE), 0x01);
}

TEST_F(NmiAcceptance_Test, PriorityOverPendingInt)
{
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8000;
    _z80->sp = 0xA000;
    _z80->int_pending = false;

    // Both an INT (inside the window) and an NMI pending at the same boundary
    _z80->RequestNonMaskedInterrupt();
    _z80->t = _intStart + 2;
    _z80->boundary = Z80_BOUNDARY_NONE;
    ASSERT_TRUE(_z80->ProcessInterrupts(false, _intStart, _intEnd));

    EXPECT_EQ(_z80->pc, 0x0066u) << "NMI wins over the maskable request";
    EXPECT_NE(_z80->pc, 0x0038u);
    EXPECT_FALSE(_z80->int_pending) << "the INT latch is dropped by the NMI accept";
}

TEST_F(NmiAcceptance_Test, DoubleRequestCoalesces)
{
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8000;
    _z80->sp = 0xA000;

    _z80->RequestNonMaskedInterrupt();
    _z80->RequestNonMaskedInterrupt();

    uint32_t t0 = _intStart + 2;
    _z80->t = t0;
    _z80->boundary = Z80_BOUNDARY_NONE;
    ASSERT_TRUE(_z80->ProcessInterrupts(false, _intStart, _intEnd));

    EXPECT_EQ(_z80->t - t0, 11u) << "exactly one acceptance";
    EXPECT_EQ(_z80->sp, 0x9FFEu) << "one return frame";

    // The coalesced second request must not fire again
    _z80->t = _intStart + 20;
    EXPECT_FALSE(_z80->ProcessInterrupts(false, _intStart, _intEnd));
    EXPECT_EQ(_z80->sp, 0x9FFEu);
}

TEST_F(NmiAcceptance_Test, RetnRestoresIFFAndEndsNmiSession)
{
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8123;
    _z80->sp = 0xA000;

    ASSERT_TRUE(acceptNmi());
    ASSERT_EQ(_z80->pc, 0x0066u);

    // Handler code runs from RAM (#0066 is ROM on Pentagon); place RETN at $8000
    _memory->DirectWriteToZ80Memory(0x8000, 0xED);
    _memory->DirectWriteToZ80Memory(0x8001, 0x45);
    _z80->pc = 0x8000;

    _z80->Z80Step();

    EXPECT_EQ(_z80->pc, 0x8123u) << "RETN pops the pushed return address";
    EXPECT_EQ(_z80->iff1, 1u) << "RETN restores IFF1 from IFF2";
    EXPECT_FALSE(_z80->nmi_in_progress) << "RETN ends the NMI session";
}

TEST_F(NmiAcceptance_Test, NestedNmiKeepsTheOuterIff2)
{
    // IFF2 is left alone by the acknowledge: a second NMI inside the handler
    // (IFF1 = 0, IFF2 = 1) must not overwrite it with IFF1, or the outer
    // RETN would return with interrupts disabled
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8123;
    _z80->sp = 0xA000;
    ASSERT_TRUE(acceptNmi());
    ASSERT_EQ(_z80->iff2, 1u);

    // Handler code in RAM (#0066 is ROM on Pentagon): NOP; RETN
    _memory->DirectWriteToZ80Memory(0x8000, 0x00);
    _memory->DirectWriteToZ80Memory(0x8001, 0xED);
    _memory->DirectWriteToZ80Memory(0x8002, 0x45);
    _z80->pc = 0x8000;
    _z80->Z80Step();  // NOP: an instruction between the two responses

    ASSERT_TRUE(acceptNmi()) << "nested NMI";
    EXPECT_EQ(_z80->iff1, 0u);
    EXPECT_EQ(_z80->iff2, 1u) << "IFF2 not overwritten by IFF1 = 0";

    _z80->pc = 0x8001;
    _z80->Z80Step();  // inner RETN -> back to $8001 (pushed at the nested acceptance)
    EXPECT_EQ(_z80->iff1, 1u) << "interrupts back on after the inner RETN";
    _z80->Z80Step();  // outer RETN -> $8123
    EXPECT_EQ(_z80->pc, 0x8123u);
    EXPECT_EQ(_z80->iff1, 1u);
    EXPECT_EQ(_z80->iff2, 1u);
}

TEST_F(NmiAcceptance_Test, NoSecondResponseWithoutAnInstructionBetween)
{
    // The chip takes no second NMI response before an instruction has run
    // (Sainz de Baranda 2022, Visual Z80): the request stays pending
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8123;
    _z80->sp = 0xA000;
    ASSERT_TRUE(acceptNmi());
    EXPECT_EQ(_z80->boundary, Z80_BOUNDARY_NMI_ACK);

    _z80->RequestNonMaskedInterrupt();
    EXPECT_FALSE(_z80->ProcessInterrupts(false, _intStart, _intEnd)) << "second NMI at the same boundary";
    EXPECT_EQ(_z80->sp, 0x9FFEu) << "one return frame";

    _memory->DirectWriteToZ80Memory(0x8000, 0x00);
    _z80->pc = 0x8000;
    _z80->Z80Step();  // one instruction
    EXPECT_TRUE(_z80->ProcessInterrupts(false, _intStart, _intEnd)) << "the pending request is taken now";
    EXPECT_EQ(_z80->sp, 0x9FFCu);
}

TEST_F(NmiAcceptance_Test, AcceptedRightAfterEi)
{
    // The EI shadow blocks INT only
    _z80->iff1 = _z80->iff2 = 0;
    _z80->sp = 0xA000;
    _z80->boundary = Z80_BOUNDARY_NONE;
    _memory->DirectWriteToZ80Memory(0x8000, 0xFB);
    _z80->pc = 0x8000;
    _z80->Z80Step();  // EI
    ASSERT_EQ(_z80->boundary, Z80_BOUNDARY_INT_SHADOW);

    _z80->RequestNonMaskedInterrupt();
    EXPECT_TRUE(_z80->ProcessInterrupts(false, _intStart, _intEnd));
    EXPECT_EQ(_z80->pc, 0x0066u);
    EXPECT_EQ(_z80->iff2, 1u) << "IFF2 keeps EI's state for RETN";
}

TEST_F(NmiAcceptance_Test, Acknowledge_AdvancesR)
{
    _z80->pc = 0x8000;
    _z80->sp = 0xA000;
    _z80->r_low = 0x7F;  // about to wrap, bit 7 clear
    ASSERT_TRUE(acceptNmi());
    EXPECT_EQ(_z80->r_low, 0x00) << "low 7 bits wrap, bit 7 kept";
}

TEST_F(NmiAcceptance_Test, LdAIrThenNmi_KeepsPV)
{
    // Only an INT acknowledge clears IFF2 under the LD A,I/R copy; the NMI
    // acknowledge leaves IFF2 alone, so P/V keeps IFF2 (Zilog Z80 Family Q&A
    // ties the quirk to an accepted INT; FUSE, redcode Z80, z80ex, openMSX)
    for (uint8_t op : {0x57, 0x5F})
    {
        _z80->iff1 = _z80->iff2 = 1;
        _z80->sp = 0xA000;
        _z80->boundary = Z80_BOUNDARY_NONE;
        _memory->DirectWriteToZ80Memory(0x8000, 0xED);
        _memory->DirectWriteToZ80Memory(0x8001, op);
        _z80->pc = 0x8000;
        _z80->Z80Step();
        ASSERT_TRUE(_z80->f & 0x04);
        _z80->RequestNonMaskedInterrupt();
        ASSERT_TRUE(_z80->ProcessInterrupts(false, _intStart, _intEnd));
        EXPECT_TRUE(_z80->f & 0x04) << "ED " << std::hex << int(op);
    }
}

TEST_F(NmiAcceptance_Test, BeforeUnexecutedHalt_ReturnsToTheHalt)
{
    _z80->iff1 = _z80->iff2 = 1;
    _z80->sp = 0xA000;
    _memory->DirectWriteToZ80Memory(0x8100, 0x76);
    _z80->pc = 0x8100;
    _z80->halted = 0;  // the HALT has not executed yet

    ASSERT_TRUE(acceptNmi());
    EXPECT_EQ(_z80->DirectRead(0x9FFF), 0x81);
    EXPECT_EQ(_z80->DirectRead(0x9FFE), 0x00) << "return address is the HALT itself";
}

/// TTD already carries nmi_in_progress in the checkpoint CPU state - assert the
/// round trip so a format regression cannot silently drop it (plan Task 6 step 3)
TEST_F(NmiAcceptance_Test, TtdCheckpointRoundTripsNmiState)
{
    Z80State state{};
    state.nmi_in_progress = true;

    ttd::TTDCpuState captured = ttd::CaptureCpuState(state);
    EXPECT_EQ(captured.nmi_in_progress, 1);

    Z80State restored{};
    restored.nmi_in_progress = false;
    ttd::RestoreCpuState(captured, &restored);
    EXPECT_TRUE(restored.nmi_in_progress);
}
