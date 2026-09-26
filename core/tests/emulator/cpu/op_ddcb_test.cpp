/// @file DD/FD prefix-chain tests (ddfd_prefixes in op_ddcb.cpp, continued by
/// Z80::Z80Step).
///
/// Real-silicon rules pinned here:
///   - DD/FD followed by another DD/FD is a redundant prefix: an instruction
///     of its own (4 T, R+1). The step ends after the next prefix's M1 with
///     that prefix pending (Z80_BOUNDARY_PREFIX_DD/FD); the next Z80Step runs
///     the instruction it introduces
///   - no INT and no NMI while a prefix is pending - the chain is one
///     instruction for the interrupt logic, however long it is
///   - every step is bounded: a long chain returns to the frame loop every 4 T
///   - DD ED xx (DD ignored) and DD CB d op are single steps
///   - SCF/CCF behind DD/FD see Q = 0: the prefix M1 writes no flags (Tony
///     Brewer's hardware finding; FUSE, MAME, redcode Z80 agree)

#include "stdafx.h"
#include "pch.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

class DdfdPrefixChain_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);

        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;

        _z80->im = 1;
        _z80->iff1 = _z80->iff2 = 1;
        _z80->sp = 0xA000;
        _z80->t = 1000;
        _z80->boundary = Z80_BOUNDARY_NONE;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    void load(uint16_t addr, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _memory->DirectWriteToZ80Memory(addr++, b);
        _z80->pc = static_cast<uint16_t>(addr - bytes.size());
    }

    /// One step; returns the T it took
    uint32_t step()
    {
        const uint32_t t0 = _z80->t;
        _z80->Z80Step();
        return _z80->t - t0;
    }

    /// Offer INT (window covering any t) at the current boundary
    bool offerInterrupt() { return _z80->ProcessInterrupts(false, 0, 0xFFFFFFFFu); }

    uint8_t observableR() const { return static_cast<uint8_t>((_z80->r_low & 0x7F) | (_z80->r_hi & 0x80)); }
};

TEST_F(DdfdPrefixChain_Test, RedundantPrefixIsAStepOfItsOwn)
{
    // DD FD 21 34 12: the DD is redundant; LD IY,1234h (14 T) follows
    load(0x8000, {0xDD, 0xFD, 0x21, 0x34, 0x12});
    _z80->r_low = 0;
    _z80->r_hi = 0;

    EXPECT_EQ(step(), 8u) << "4 T redundant DD + the 4 T M1 of the FD";
    EXPECT_EQ(_z80->pc, 0x8002u);
    EXPECT_EQ(_z80->boundary, Z80_BOUNDARY_PREFIX_FD);
    EXPECT_EQ(observableR(), 2);

    // Inside the instruction: neither INT nor NMI
    _z80->RequestNonMaskedInterrupt();
    EXPECT_FALSE(offerInterrupt()) << "no INT/NMI while a prefix is pending";
    EXPECT_EQ(_z80->pc, 0x8002u) << "nothing pushed, no handler entered";
    EXPECT_EQ(_z80->sp, 0xA000u);

    EXPECT_EQ(step(), 10u) << "LD IY,nn: 14 T, the FD's 4 already spent";
    EXPECT_EQ(_z80->iy, 0x1234u);
    EXPECT_EQ(_z80->pc, 0x8005u);
    EXPECT_EQ(_z80->boundary, Z80_BOUNDARY_NONE);
    EXPECT_EQ(observableR(), 3);

    // The NMI request survived the refusal and is taken at the real boundary
    EXPECT_TRUE(offerInterrupt());
    EXPECT_EQ(_z80->pc, 0x0066u);
}

TEST_F(DdfdPrefixChain_Test, DdEdAndDdCbAreSingleSteps)
{
    load(0x8000, {0x3E, 0x01, 0xDD, 0xED, 0x44});  // LD A,1; NEG behind an ignored DD
    step();
    EXPECT_EQ(step(), 12u);
    EXPECT_EQ(_z80->a, 0xFFu);
    EXPECT_EQ(_z80->boundary, Z80_BOUNDARY_NONE);

    _z80->ix = 0x9000;
    _memory->DirectWriteToZ80Memory(0x9000, 0x81);
    load(0x8100, {0xDD, 0xCB, 0x00, 0x06});  // RLC (IX+0)
    EXPECT_EQ(step(), 23u);
    EXPECT_EQ(_z80->DirectRead(0x9000), 0x03);
    EXPECT_EQ(_z80->boundary, Z80_BOUNDARY_NONE);
}

TEST_F(DdfdPrefixChain_Test, LongChainStepsAreBoundedAndBlockInterrupts)
{
    // 1000 prefixes then LD IX,nn: every step returns (8 T first, 4 T after),
    // R counts every prefix, INT and NMI are refused until the chain ends
    constexpr int kChain = 1000;
    for (int i = 0; i < kChain; i++)
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), (i & 3) ? 0xDD : 0xFD);
    load(static_cast<uint16_t>(0x8000 + kChain), {0x21, 0xCD, 0xAB});  // last prefix is DD: LD IX,ABCDh
    _z80->pc = 0x8000;
    _z80->r_low = 0;
    _z80->r_hi = 0;

    int badT = 0;
    int accepted = 0;
    for (int i = 0; i < kChain - 1; i++)
    {
        badT += step() != (i == 0 ? 8u : 4u);
        _z80->RequestNonMaskedInterrupt();
        accepted += offerInterrupt() ? 1 : 0;
    }
    EXPECT_EQ(badT, 0);
    EXPECT_EQ(accepted, 0);
    EXPECT_EQ(_z80->boundary, Z80_BOUNDARY_PREFIX_DD);
    EXPECT_EQ(observableR(), kChain & 0x7F);

    EXPECT_EQ(step(), 10u);
    EXPECT_EQ(_z80->ix, 0xABCDu);
    EXPECT_EQ(_z80->boundary, Z80_BOUNDARY_NONE);
    EXPECT_TRUE(offerInterrupt()) << "the held NMI is taken at the end of the chain";
    EXPECT_EQ(_z80->pc, 0x0066u);
}

TEST_F(DdfdPrefixChain_Test, ScfCcfBehindAPrefixSeeQZero)
{
    // CP B takes X/Y from the operand: after it F has bits 3/5 set, Q = F,
    // A = 0. Plain SCF: X/Y = (A | (F & ~Q)) = 0. Behind DD/FD: Q = 0, so
    // X/Y = A | F = 28h
    struct Case
    {
        std::initializer_list<uint8_t> tail;
        int steps;
        uint8_t xy;
    };
    for (const Case& c : {Case{{0x37}, 1, 0x00}, Case{{0xDD, 0x37}, 1, 0x28}, Case{{0xFD, 0xDD, 0x3F}, 2, 0x28}})
    {
        load(0x8000, {0x06, 0x28, 0x3E, 0x00, 0xB8});  // LD B,28h; LD A,0; CP B
        uint16_t addr = 0x8005;
        for (uint8_t b : c.tail)
            _memory->DirectWriteToZ80Memory(addr++, b);
        _z80->pc = 0x8000;
        _z80->f = 0;
        _z80->q = 0;
        for (int i = 0; i < 3 + c.steps; i++)
            step();
        EXPECT_EQ(_z80->f & 0x28, c.xy) << "tail starting " << std::hex << int(*c.tail.begin());
    }
}
