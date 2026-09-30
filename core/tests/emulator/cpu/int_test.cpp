/// @file Z80 maskable interrupt tests - acceptance timing and pending-flag
/// lifetime across frame wrap.
///
/// Consolidated from int_acceptance_test.cpp and int_pending_wrap_test.cpp.
/// Suite and test names are unchanged.

#include "stdafx.h"
#include "pch.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// region <From int_acceptance_test.cpp>

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
///
/// Boundary rules (Z80State::boundary, real-silicon behaviour):
///   - INT refused right after EI (DD/FD FB too, not CB FB / ED FB) and right
///     after a RETN/RETI that set IFF1 (Weissflog 2021, Sainz de Baranda 2022)
///   - INT right after LD A,I / LD A,R clears P/V (NMOS)
///   - the acknowledge M1 advances R; IM2 pushes PC before reading the table
///   - an INT at the boundary before a not-yet-executed HALT returns to it

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
        _z80->boundary = Z80_BOUNDARY_NONE;
        return _z80->ProcessInterrupts(false, _intStart, _intEnd);
    }

    /// Offer the INT at the current boundary without touching the boundary
    /// state (a window covering any t): true if accepted
    bool offerInterrupt() { return _z80->ProcessInterrupts(false, 0, 0xFFFFFFFFu); }

    void load(uint16_t addr, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _memory->DirectWriteToZ80Memory(addr++, b);
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
    _z80->iff1 = _z80->iff2 = 0;
    _z80->sp = 0xA000;
    _z80->boundary = Z80_BOUNDARY_NONE;
    load(0x8000, {0xFB, 0x00});  // EI; NOP
    _z80->pc = 0x8000;
    _z80->t = _intStart + 2;

    _z80->Z80Step();  // EI
    EXPECT_EQ(_z80->boundary, Z80_BOUNDARY_INT_SHADOW);
    EXPECT_FALSE(_z80->ProcessInterrupts(false, _intStart, _intEnd))
        << "INT must not be accepted at the boundary right after EI";

    // One instruction later the same pending INT must be accepted
    _z80->Z80Step();  // NOP
    EXPECT_TRUE(_z80->ProcessInterrupts(false, _intStart, _intEnd));
    EXPECT_EQ(_z80->pc, 0x0038u);
}

TEST_F(IntAcceptance_Test, EIShadow_OnlyForRealEi)
{
    // DD/FD FB are EI; CB FB (SET 7,E) and ED FB (NOP) share the byte but
    // must not shadow the next boundary
    struct Case
    {
        std::initializer_list<uint8_t> code;
        bool shadow;
    };
    for (const Case& c : {Case{{0xDD, 0xFB}, true}, Case{{0xFD, 0xFB}, true}, Case{{0xCB, 0xFB}, false},
                          Case{{0xED, 0xFB}, false}})
    {
        _z80->im = 1;
        _z80->iff1 = _z80->iff2 = 1;
        _z80->sp = 0xA000;
        _z80->boundary = Z80_BOUNDARY_NONE;
        load(0x8000, c.code);
        _z80->pc = 0x8000;
        _z80->Z80Step();
        EXPECT_EQ(offerInterrupt(), !c.shadow) << "opcode " << std::hex << int(*(c.code.begin() + 1));
    }
}

TEST_F(IntAcceptance_Test, RetnRetiThatSetIff1_ShadowTheNextBoundary)
{
    // RETN/RETI copy IFF2 to IFF1 too late for the next boundary's INT
    // sampling: when that copy sets IFF1 (inside an NMI handler) INT is
    // refused there, and accepted one instruction later. Every ED 45-family
    // alias shares the two bodies
    for (uint8_t op : {0x45, 0x4D, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D})
    {
        _z80->im = 1;
        _z80->iff1 = 0;
        _z80->iff2 = 1;
        _z80->sp = 0xA000;
        _z80->boundary = Z80_BOUNDARY_NONE;
        _memory->DirectWriteToZ80Memory(0xA000, 0x00);  // return to $9000: NOP
        _memory->DirectWriteToZ80Memory(0xA001, 0x90);
        load(0x9000, {0x00});
        load(0x8000, {0xED, op});
        _z80->pc = 0x8000;

        _z80->Z80Step();
        EXPECT_EQ(_z80->iff1, 1u);
        EXPECT_EQ(_z80->boundary, Z80_BOUNDARY_INT_SHADOW) << "ED " << std::hex << int(op);
        EXPECT_FALSE(offerInterrupt()) << "ED " << std::hex << int(op);
        _z80->Z80Step();  // NOP at $9000
        EXPECT_TRUE(offerInterrupt()) << "ED " << std::hex << int(op);
    }

    // IFF1 already set (EI; RETI in an IM2 handler): no shadow
    _z80->iff1 = _z80->iff2 = 1;
    _z80->sp = 0xA000;
    _z80->boundary = Z80_BOUNDARY_NONE;
    _memory->DirectWriteToZ80Memory(0xA000, 0x00);
    _memory->DirectWriteToZ80Memory(0xA001, 0x90);
    load(0x8000, {0xED, 0x4D});
    _z80->pc = 0x8000;
    _z80->Z80Step();
    EXPECT_EQ(_z80->boundary, Z80_BOUNDARY_NONE);
    EXPECT_TRUE(offerInterrupt());
}

TEST_F(IntAcceptance_Test, LdAIr_IntAcceptedNextClearsPV)
{
    // NMOS: LD A,I / LD A,R copy IFF2 into P/V late; an INT accepted at the
    // very next boundary clears IFF2 before the copy settles - P/V reads 0.
    // Not after any other instruction
    for (uint8_t op : {0x57, 0x5F})
    {
        _z80->im = 1;
        _z80->iff1 = _z80->iff2 = 1;
        _z80->sp = 0xA000;
        _z80->boundary = Z80_BOUNDARY_NONE;
        load(0x8000, {0xED, op});
        _z80->pc = 0x8000;
        _z80->Z80Step();
        EXPECT_TRUE(_z80->f & 0x04) << "P/V = IFF2 = 1";
        EXPECT_EQ(_z80->boundary, Z80_BOUNDARY_LD_A_IR);
        ASSERT_TRUE(offerInterrupt());
        EXPECT_FALSE(_z80->f & 0x04) << "cleared by the acknowledge, ED " << std::hex << int(op);
    }

    _z80->iff1 = _z80->iff2 = 1;
    _z80->sp = 0xA000;
    load(0x8000, {0xED, 0x57, 0x00});  // LD A,I; NOP
    _z80->pc = 0x8000;
    _z80->Z80Step();
    _z80->Z80Step();
    ASSERT_TRUE(offerInterrupt());
    EXPECT_TRUE(_z80->f & 0x04) << "one instruction later P/V stays";
}

TEST_F(IntAcceptance_Test, Acknowledge_AdvancesR)
{
    // The acknowledge M1 is a refresh cycle: R advances by one in every
    // mode, bit 7 kept
    for (uint8_t im : {1, 2})
    {
        _z80->im = im;
        _z80->i = 0xBE;
        _z80->iff1 = _z80->iff2 = 1;
        _z80->sp = 0xA000;
        _z80->pc = 0x8000;
        _z80->r_low = 0xFF;  // bit 7 set, low 7 bits about to wrap
        ASSERT_TRUE(acceptInterrupt());
        EXPECT_EQ(_z80->r_low, 0x80) << "IM" << int(im) << ": low 7 bits wrap, bit 7 kept";
    }
}

TEST_F(IntAcceptance_Test, IM2_PushesPcBeforeReadingTheVectorTable)
{
    // Machine-cycle order M2/M3 push, then M4/M5 table read: a stack that
    // overlaps the table supplies the freshly pushed PC as the handler address
    _z80->im = 2;
    _z80->i = 0x9F;       // table entry at $9FFF (open bus vector $FF)
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0xC234;
    _z80->sp = 0xA001;    // push writes $A000 (PCH) and $9FFF (PCL)
    _memory->DirectWriteToZ80Memory(0x9FFF, 0x11);  // stale table bytes
    _memory->DirectWriteToZ80Memory(0xA000, 0x22);

    ASSERT_TRUE(acceptInterrupt());
    EXPECT_EQ(_z80->pc, 0xC234u) << "handler = the pushed return address (table read after the push)";
}

TEST_F(IntAcceptance_Test, BeforeUnexecutedHalt_ReturnsToTheHalt)
{
    // PC at a HALT that has not executed yet (latch clear): the INT returns
    // to the HALT, which then executes - it must not be skipped
    _z80->im = 1;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->sp = 0xA000;
    _memory->DirectWriteToZ80Memory(0x8100, 0x76);
    _z80->pc = 0x8100;
    _z80->halted = 0;

    ASSERT_TRUE(acceptInterrupt());
    EXPECT_EQ(_z80->DirectRead(0x9FFF), 0x81);
    EXPECT_EQ(_z80->DirectRead(0x9FFE), 0x00) << "return address is the HALT itself";
}

TEST_F(IntAcceptance_Test, NoAcceptanceOutsideWindow)
{
    _z80->im = 1;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8000;
    _z80->int_pending = false;

    // Exactly at int_start: strict sampling means not yet visible
    _z80->t = _intStart;
    _z80->boundary = Z80_BOUNDARY_NONE;
    EXPECT_FALSE(_z80->ProcessInterrupts(false, _intStart, _intEnd));

    // Past the window end with no pending latch: no acceptance
    _z80->int_pending = false;
    _z80->t = _intEnd + 10;
    EXPECT_FALSE(_z80->ProcessInterrupts(false, _intStart, _intEnd));
}

/// INT pulse vs a handler shorter than the pulse (EI; RET: 13 T acknowledge
/// + 14 T). A ULA/Pentagon pulse is fixed-length, so the handler is taken
/// again while the pulse lasts - as on the real machine. ZX-Evo (ATM3 model)
/// ends the pulse at the acknowledge (baseconf zint.v: IORQ+M1), so it is
/// taken once per pulse however long the turbo-scaled window, and again at
/// the next pulse.
static int CountIntsInOnePulse(const char* model, int& acceptedAfterPulse)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
    EXPECT_NE(emulator, nullptr);
    if (!emulator)
        return -1;
    Z80* z80 = emulator->GetContext()->pCore->GetZ80();
    Memory* memory = emulator->GetContext()->pMemory;

    // IM2 handler at $9000: EI; RET. Table entry at $BEFF (I = BE, open bus FF)
    memory->DirectWriteToZ80Memory(0xBEFF, 0x00);
    memory->DirectWriteToZ80Memory(0xBF00, 0x90);
    memory->DirectWriteToZ80Memory(0x9000, 0xFB);
    memory->DirectWriteToZ80Memory(0x9001, 0xC9);
    for (uint16_t a = 0x8000; a < 0x8100; a++)
        memory->DirectWriteToZ80Memory(a, 0x00);  // NOPs to return to

    z80->im = 2;
    z80->i = 0xBE;
    z80->iff1 = z80->iff2 = 1;
    z80->sp = 0xA000;
    z80->pc = 0x8000;
    z80->boundary = Z80_BOUNDARY_NONE;
    z80->int_pending = false;
    z80->int_acked_in_pulse = 0;

    // A 128 T pulse window (a 32 T pulse at 4x turbo), driven step by step
    constexpr unsigned kStart = 1000, kEnd = 1128;
    z80->t = kStart + 1;
    int accepted = 0;
    while (z80->t < kEnd)
    {
        if (z80->ProcessInterrupts(false, kStart, kEnd))
            accepted++;
        else
            z80->Z80Step();
    }

    // Past the pulse, then the next pulse
    z80->t = kEnd + 100;
    z80->ProcessInterrupts(false, kStart, kEnd);
    z80->t = kStart + 1 + 71680;  // same window positions, next frame's pulse
    const unsigned nextStart = kStart + 71680, nextEnd = kEnd + 71680;
    acceptedAfterPulse = 0;
    while (z80->t < nextEnd && acceptedAfterPulse == 0)
    {
        if (z80->ProcessInterrupts(false, nextStart, nextEnd))
            acceptedAfterPulse++;
        else
            z80->Z80Step();
    }

    EmulatorTestHelper::CleanupEmulator(emulator);
    return accepted;
}

TEST(IntPulse_Test, ZxEvoAcknowledgeEndsThePulse)
{
    int next = 0;
    EXPECT_EQ(CountIntsInOnePulse("ATM3", next), 1) << "ZX-Evo: one INT per pulse, the acknowledge ends it";
    EXPECT_EQ(next, 1) << "the next pulse is taken again";
}

TEST(IntPulse_Test, FixedLengthPulseRetakesAShortHandler)
{
    int next = 0;
    EXPECT_GT(CountIntsInOnePulse("PENTAGON", next), 1) << "fixed-length pulse: EI;RET is taken again while it lasts";
    EXPECT_EQ(next, 1);
}

/// endregion </From int_acceptance_test.cpp>

/// region <Machine interrupt source (IInterruptSource, PLAN #60(a))>

/// A machine that owns its INT logic (TSConf, Sprinter) registers an
/// IInterruptSource: the pin is what the source says, the acknowledge takes
/// the source's bus byte as the IM2 vector, and RETI is reported to it. The
/// CPU keeps IFF1, the EI shadow and the prefix rule. Without a source every
/// IntAcceptance_Test above is the unchanged path.
namespace
{
struct FakeInterruptSource : IInterruptSource
{
    uint32_t assertFrom = 0;
    uint32_t assertTo = 0;  // asserted for t in [assertFrom, assertTo)
    uint8_t vector = 0xFB;
    int acks = 0;
    int retis = 0;
    uint32_t lastAckT = 0;

    bool IsIntAsserted(uint32_t t) override { return t >= assertFrom && t < assertTo; }
    uint8_t AcknowledgeInterrupt(uint32_t t) override
    {
        acks++;
        lastAckT = t;
        assertTo = 0;  // the served source clears
        return vector;
    }
    void OnReti() override { retis++; }
};
} // namespace

class InterruptSource_Test : public IntAcceptance_Test
{
protected:
    FakeInterruptSource _source;

    void SetUp() override
    {
        IntAcceptance_Test::SetUp();
        _z80->SetInterruptSource(&_source);
    }

    void TearDown() override
    {
        if (_z80)
            _z80->SetInterruptSource(nullptr);
        IntAcceptance_Test::TearDown();
    }

    bool offerAt(uint32_t t)
    {
        _z80->t = t;
        return _z80->ProcessInterrupts(false, _intStart, _intEnd);
    }
};

TEST_F(InterruptSource_Test, TheSourceAloneDecidesThePin)
{
    _z80->im = 1;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8000;
    _z80->sp = 0xA000;

    // Inside the ULA window, source quiet: no INT
    EXPECT_FALSE(offerAt(_intStart + 2));
    EXPECT_FALSE(_z80->int_pending);
    EXPECT_EQ(_source.acks, 0);

    // Far from the ULA window, source asserted: INT
    _source.assertFrom = 30000;
    _source.assertTo = 30100;
    EXPECT_TRUE(offerAt(30050));
    EXPECT_EQ(_source.acks, 1);
    EXPECT_EQ(_source.lastAckT, 30050u) << "acknowledged at the boundary, before the 13 T";
    EXPECT_EQ(_z80->pc, 0x0038u);
    EXPECT_EQ(_z80->t, 30050u + 13u);
}

TEST_F(InterruptSource_Test, Im2TakesTheSourcesVector)
{
    _z80->im = 2;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->i = 0xBE;
    _z80->pc = 0x8000;
    _z80->sp = 0xA000;
    _memory->DirectWriteToZ80Memory(0xBEFB, 0x34);  // I*256 + #FB: handler #C234
    _memory->DirectWriteToZ80Memory(0xBEFC, 0xC2);
    _source.assertTo = 0x10000;

    ASSERT_TRUE(offerAt(1000));
    EXPECT_EQ(_z80->pc, 0xC234u) << "vector #FB from the source, not the open-bus #FF";
    EXPECT_EQ(_z80->t, 1000u + 19u);
}

TEST_F(InterruptSource_Test, TheCpuRulesStillApply)
{
    _z80->im = 1;
    _z80->pc = 0x8000;
    _z80->sp = 0xA000;
    _source.assertTo = 0x10000;

    _z80->iff1 = 0;
    EXPECT_FALSE(offerAt(100)) << "DI";
    EXPECT_TRUE(_z80->int_pending) << "the pin is still reported";

    _z80->iff1 = _z80->iff2 = 1;
    _z80->boundary = Z80_BOUNDARY_INT_SHADOW;
    EXPECT_FALSE(offerAt(100)) << "EI shadow";
    _z80->boundary = Z80_BOUNDARY_PREFIX_DD;
    EXPECT_FALSE(offerAt(100)) << "inside a prefixed instruction";
    EXPECT_EQ(_source.acks, 0) << "no acknowledge while refused";

    _z80->boundary = Z80_BOUNDARY_NONE;
    EXPECT_TRUE(offerAt(100));
}

TEST_F(InterruptSource_Test, RetiAndItsMirrorsReachTheSourceRetnDoesNot)
{
    for (uint8_t op : {0x4D, 0x5D, 0x6D, 0x7D, 0x45, 0x55})
    {
        const int before = _source.retis;
        _z80->iff1 = _z80->iff2 = 1;
        _z80->sp = 0xA000;
        _memory->DirectWriteToZ80Memory(0xA000, 0x00);
        _memory->DirectWriteToZ80Memory(0xA001, 0x90);
        load(0x8000, {0xED, op});
        _z80->pc = 0x8000;
        _z80->Z80Step();
        const bool reti = (op & 0x08) != 0;  // ED 4D/5D/6D/7D; ED 45/55 are RETN
        EXPECT_EQ(_source.retis - before, reti ? 1 : 0) << "ED " << std::hex << int(op);
        EXPECT_EQ(_z80->pc, 0x9000u);
    }
}

TEST_F(InterruptSource_Test, WithoutASourceRetiIsAPlainReturn)
{
    _z80->SetInterruptSource(nullptr);
    _z80->sp = 0xA000;
    _memory->DirectWriteToZ80Memory(0xA000, 0x00);
    _memory->DirectWriteToZ80Memory(0xA001, 0x90);
    load(0x8000, {0xED, 0x4D});
    _z80->pc = 0x8000;
    _z80->Z80Step();
    EXPECT_EQ(_z80->pc, 0x9000u);
    EXPECT_EQ(_source.retis, 0);
}

/// endregion </Machine interrupt source>

/// region <Device INT lines (Z80::SetDeviceIntLine)>

/// A device on the bus (the ZXNETUSB card) pulls the shared /INT low as a
/// level, wired-OR with the machine's own INT: taken at any T-state while
/// held, no vector of its own (the bus reads #FF), gone the moment it is
/// released. The per-step work bit is up only while a line is low, so every
/// other machine keeps its plain step.
class DeviceInt_Test : public IntAcceptance_Test
{
protected:
    void TearDown() override
    {
        if (_z80)
            _z80->SetDeviceIntLine(0xFFFFFFFFu, false);
        IntAcceptance_Test::TearDown();
    }

    bool offerAt(uint32_t t)
    {
        _z80->t = t;
        _z80->boundary = Z80_BOUNDARY_NONE;
        return _z80->ProcessInterrupts(false, _intStart, _intEnd);
    }

    bool DeviceWork() const { return _context->HasStepWork(EmulatorContext::kStepWorkDeviceInt); }
};

TEST_F(DeviceInt_Test, WorkBitOnlyWhileALineIsLow)
{
    EXPECT_FALSE(DeviceWork()) << "no device: the plain step";
    _z80->SetDeviceIntLine(Z80::kDeviceIntZxNetUsb, true);
    EXPECT_TRUE(DeviceWork());
    _z80->SetDeviceIntLine(1u << 5, true);
    _z80->SetDeviceIntLine(Z80::kDeviceIntZxNetUsb, false);
    EXPECT_TRUE(DeviceWork()) << "another device still holds the line";
    _z80->SetDeviceIntLine(1u << 5, false);
    EXPECT_FALSE(DeviceWork());
    EXPECT_EQ(_z80->GetDeviceIntLines(), 0u);
}

TEST_F(DeviceInt_Test, TakenOutsideTheFrameWindowWithTheFFVector)
{
    _z80->im = 2;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->i = 0xBE;
    _z80->pc = 0x8000;
    _z80->sp = 0xA000;
    _z80->int_pending = false;
    _memory->DirectWriteToZ80Memory(0xBEFF, 0x34);  // I*256 + #FF: handler #C234
    _memory->DirectWriteToZ80Memory(0xBF00, 0xC2);

    const uint32_t far = _intEnd + 30000;
    EXPECT_FALSE(offerAt(far)) << "no line, no frame INT";

    _z80->SetDeviceIntLine(Z80::kDeviceIntZxNetUsb, true);
    ASSERT_TRUE(offerAt(far));
    EXPECT_EQ(_z80->pc, 0xC234u) << "the bus reads #FF";
    EXPECT_EQ(_z80->t, far + 19u);
}

TEST_F(DeviceInt_Test, ReleasedLineLeavesNoLatch)
{
    _z80->im = 1;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8000;
    _z80->sp = 0xA000;
    _z80->int_pending = false;

    _z80->SetDeviceIntLine(Z80::kDeviceIntZxNetUsb, true);
    _z80->iff1 = 0;
    EXPECT_FALSE(offerAt(_intEnd + 1000)) << "DI";
    _z80->SetDeviceIntLine(Z80::kDeviceIntZxNetUsb, false);
    _z80->iff1 = _z80->iff2 = 1;
    EXPECT_FALSE(offerAt(_intEnd + 1000)) << "a level: nothing remembered once released";
    EXPECT_FALSE(_z80->int_pending);
}

TEST_F(DeviceInt_Test, TheCpuRulesStillApply)
{
    _z80->im = 1;
    _z80->pc = 0x8000;
    _z80->sp = 0xA000;
    _z80->SetDeviceIntLine(Z80::kDeviceIntZxNetUsb, true);

    _z80->iff1 = _z80->iff2 = 1;
    _z80->t = _intEnd + 1000;
    _z80->boundary = Z80_BOUNDARY_INT_SHADOW;
    EXPECT_FALSE(_z80->ProcessInterrupts(false, _intStart, _intEnd)) << "EI shadow";
    _z80->boundary = Z80_BOUNDARY_PREFIX_FD;
    EXPECT_FALSE(_z80->ProcessInterrupts(false, _intStart, _intEnd)) << "inside a prefixed instruction";
    _z80->boundary = Z80_BOUNDARY_NONE;
    EXPECT_TRUE(_z80->ProcessInterrupts(false, _intStart, _intEnd));
    EXPECT_EQ(_z80->pc, 0x0038u);
}

TEST_F(DeviceInt_Test, TheStepTakesItThroughTheWorkPath)
{
    // HALT outside the frame window: only the device line can release it
    _z80->im = 1;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->sp = 0xA000;
    load(0x8000, {0x76});
    _z80->pc = 0x8000;
    _z80->t = _intEnd + 1000;
    _z80->boundary = Z80_BOUNDARY_NONE;
    _z80->int_pending = false;
    EXPECT_FALSE(_z80->StepInstruction(true).intAccepted);
    EXPECT_TRUE(_z80->halted);

    _z80->SetDeviceIntLine(Z80::kDeviceIntZxNetUsb, true);
    EXPECT_TRUE(_z80->StepInstruction(true).intAccepted);
    EXPECT_EQ(_z80->pc, 0x0038u);
    EXPECT_FALSE(_z80->halted);
}

TEST_F(DeviceInt_Test, WithAMachineSourceTheVectorComesFromWhoAsserts)
{
    FakeInterruptSource source;
    source.vector = 0xFB;
    _z80->SetInterruptSource(&source);
    _z80->im = 2;
    _z80->i = 0xBE;
    _z80->sp = 0xA000;
    _memory->DirectWriteToZ80Memory(0xBEFF, 0x34);  // #FF: handler #C234
    _memory->DirectWriteToZ80Memory(0xBF00, 0xC2);
    _memory->DirectWriteToZ80Memory(0xBEFB, 0x78);  // #FB: handler #5678
    _memory->DirectWriteToZ80Memory(0xBEFC, 0x56);

    _z80->SetDeviceIntLine(Z80::kDeviceIntZxNetUsb, true);
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8000;
    ASSERT_TRUE(offerAt(1000));
    EXPECT_EQ(_z80->pc, 0xC234u) << "the source is quiet: the device alone, #FF";
    EXPECT_EQ(source.acks, 0) << "the source was not acknowledged";

    source.assertTo = 0x10000;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8000;
    ASSERT_TRUE(offerAt(2000));
    EXPECT_EQ(_z80->pc, 0x5678u) << "the source asserts too: its vector";
    EXPECT_EQ(source.acks, 1);

    _z80->SetInterruptSource(nullptr);
}

/// endregion </Device INT lines>

/// region <From int_pending_wrap_test.cpp>

/// Regression tests for the stale INT latch across frame wrap.
///
/// ProcessInterrupts clears cpu.int_pending via "t >= int_end", but when an
/// instruction (typically the INT acceptance itself) carries t across the
/// frame boundary, that clear never fires. The stale flag then delivered a
/// SECOND interrupt in the new frame as soon as the program executed EI -
/// observed as variable 1.5-2x music speedup in EI:HALT-synced IM2 demos
/// (Insult megademo) on Pentagon, where the INT window [71635, 71667) ends
/// only 13 t-states before the 71680 t-state frame wrap.
/// Fix: Core::AdjustFrameCounters drops int_pending on frame wrap.
class IntPendingWrap_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Core* _core = nullptr;
    Z80* _z80 = nullptr;
    uint32_t _frame = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";

        _core = _emulator->GetContext()->pCore;
        _z80 = _core->GetZ80();
        _frame = _emulator->GetContext()->config.frame;
        ASSERT_EQ(_frame, 71680u);  // Pentagon
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

TEST_F(IntPendingWrap_Test, FrameWrap_ClearsStaleIntPending)
{
    // INT accepted at t=71664 (inside the window), HandleINT added ~19T:
    // t crossed the frame boundary without ever satisfying "t >= int_end"
    _z80->t = _frame + 3;
    _z80->int_pending = true;

    _core->AdjustFrameCounters();

    EXPECT_EQ(_z80->t, 3u) << "t must wrap by exactly one frame";
    EXPECT_FALSE(_z80->int_pending)
        << "Stale INT latch must not survive the frame wrap - it would deliver "
           "a second interrupt after the program's next EI";
}

TEST_F(IntPendingWrap_Test, NoWrap_LeavesIntPendingUntouched)
{
    // Mid-frame, inside the INT window: the request must stay pending
    // (program may still be in DI and accept later within the window)
    _z80->t = 71640;  // Inside [71635, 71667)
    _z80->int_pending = true;

    _core->AdjustFrameCounters();  // t < frame: early-return, no changes

    EXPECT_EQ(_z80->t, 71640u);
    EXPECT_TRUE(_z80->int_pending) << "In-window INT request must not be dropped mid-frame";
}

/// endregion </From int_pending_wrap_test.cpp>
