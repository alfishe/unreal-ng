#include "stdafx.h"
#include "pch.h"

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"

/// IO cycle phase tests.
///
/// The Z80 asserts IORQ at T2 of the 4T IO machine cycle (T1 carries no IORQ;
/// the CPU inserts one wait state before IO). Port reads/writes must land on
/// that T-state - not at the IO cycle entry - because peripherals timestamp
/// them: Pentagon renders the border every 1T, so a border OUT arriving 1T
/// early is visibly misaligned (2 pixels) in border-synced effects.
///
/// These tests pin the exact T-state offset of the port access within each
/// IO instruction, for single IN/OUT and all block-IO forms. Instruction
/// TOTALS are checked too, but totals alone cannot catch a phase regression
/// (moving the access within the cycle keeps the total constant).
///
/// Expected offsets from instruction start (Pentagon, no contention):
///   IN A,(nn) / OUT (nn),A: opcode fetch 4 + operand read 3 + 1 = 8
///   IN r,(C) / OUT (C),r:   ED fetch 4 + opcode fetch 4 + 1    = 9
///   INI/IND (+ repeats):    fetches 8 + M1 stall 1 + 1         = 10
///   OUTI/OUTD (+ repeats):  fetches 8 + stall 1 + mem read 3 + 1 = 13

namespace
{
class CapturePortDecoder : public PortDecoder
{
public:
    Z80* z80 = nullptr;
    uint32_t tAtAccess = 0;
    int accessCount = 0;

    CapturePortDecoder(EmulatorContext* context, Z80* cpu) : PortDecoder(context), z80(cpu) {}

    void reset() override {}

    uint8_t DecodePortIn(uint16_t addr, uint16_t pc) override
    {
        (void)addr;
        (void)pc;
        tAtAccess = z80->t;
        accessCount++;
        return 0xFF;
    }

    void DecodePortOut(uint16_t addr, uint8_t value, uint16_t pc) override
    {
        (void)addr;
        (void)value;
        (void)pc;
        tAtAccess = z80->t;
        accessCount++;
    }
};
}  // namespace

class IOPhase_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    PortDecoder* _originalDecoder = nullptr;
    CapturePortDecoder* _capture = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";

        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;

        _capture = new CapturePortDecoder(_context, _z80);
        _originalDecoder = _context->pPortDecoder;
        _context->pPortDecoder = _capture;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pPortDecoder = _originalDecoder;
            delete _capture;
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Place opcode bytes at $8000, execute one instruction, return
    /// {access offset from instruction start, total duration}
    struct Timing
    {
        uint32_t accessOffset;
        uint32_t total;
        int accesses;
    };

    Timing runInstruction(std::initializer_list<uint8_t> bytes, uint8_t regB = 1)
    {
        uint16_t addr = 0x8000;
        for (uint8_t byte : bytes)
        {
            _memory->DirectWriteToZ80Memory(addr++, byte);
        }

        _z80->pc = 0x8000;
        _z80->b = regB;       // Block IO repeat count (B=1: single iteration)
        _z80->c = 0xFD;
        _z80->hl = 0x9000;
        _z80->iff1 = 0;       // No interrupts during measurement
        _z80->t = 1000;       // Mid-frame, far from the INT window

        _capture->accessCount = 0;
        _capture->tAtAccess = 0;

        uint32_t t0 = _z80->t;
        _z80->Z80Step();

        return {_capture->tAtAccess - t0, _z80->t - t0, _capture->accessCount};
    }
};

/// region <Single IN/OUT>

TEST_F(IOPhase_Test, OUT_NN_A_AccessAtT2)
{
    auto timing = runInstruction({0xD3, 0xFE});  // OUT ($FE),A
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 11u);
    EXPECT_EQ(timing.accessOffset, 8u) << "fetch 4 + operand 3 + 1 (IORQ at T2)";
}

TEST_F(IOPhase_Test, IN_A_NN_AccessAtT2)
{
    auto timing = runInstruction({0xDB, 0xFE});  // IN A,($FE)
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 11u);
    EXPECT_EQ(timing.accessOffset, 8u);
}

TEST_F(IOPhase_Test, IN_R_C_AccessAtT2)
{
    auto timing = runInstruction({0xED, 0x78});  // IN A,(C)
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 12u);
    EXPECT_EQ(timing.accessOffset, 9u) << "ED 4 + opcode 4 + 1 (IORQ at T2)";
}

TEST_F(IOPhase_Test, OUT_C_R_AccessAtT2)
{
    auto timing = runInstruction({0xED, 0x79});  // OUT (C),A
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 12u);
    EXPECT_EQ(timing.accessOffset, 9u);
}

/// endregion </Single IN/OUT>

/// region <Block IO>

TEST_F(IOPhase_Test, INI_AccessAtT2)
{
    auto timing = runInstruction({0xED, 0xA2});  // INI
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 16u);
    EXPECT_EQ(timing.accessOffset, 10u) << "fetches 8 + M1 stall 1 + 1 (IORQ at T2)";
}

TEST_F(IOPhase_Test, IND_AccessAtT2)
{
    auto timing = runInstruction({0xED, 0xAA});  // IND
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 16u);
    EXPECT_EQ(timing.accessOffset, 10u);
}

TEST_F(IOPhase_Test, OUTI_AccessAtT2)
{
    auto timing = runInstruction({0xED, 0xA3});  // OUTI
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 16u);
    EXPECT_EQ(timing.accessOffset, 13u) << "fetches 8 + stall 1 + mem read 3 + 1 (IORQ at T2)";
}

TEST_F(IOPhase_Test, OUTD_AccessAtT2)
{
    auto timing = runInstruction({0xED, 0xAB});  // OUTD
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 16u);
    EXPECT_EQ(timing.accessOffset, 13u);
}

TEST_F(IOPhase_Test, INIR_LastIteration_16T)
{
    // B=1: no repeat, behaves like INI with INIR flags
    auto timing = runInstruction({0xED, 0xB2}, 1);
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 16u);
    EXPECT_EQ(timing.accessOffset, 10u);
}

TEST_F(IOPhase_Test, INIR_Repeating_21T)
{
    // B=2: repeats, +5T for the PC rewind; access phase unchanged
    auto timing = runInstruction({0xED, 0xB2}, 2);
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 21u);
    EXPECT_EQ(timing.accessOffset, 10u);
}

TEST_F(IOPhase_Test, OTIR_LastIteration_16T)
{
    auto timing = runInstruction({0xED, 0xB3}, 1);
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 16u);
    EXPECT_EQ(timing.accessOffset, 13u);
}

TEST_F(IOPhase_Test, OTIR_Repeating_21T)
{
    auto timing = runInstruction({0xED, 0xB3}, 2);
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 21u);
    EXPECT_EQ(timing.accessOffset, 13u);
}

TEST_F(IOPhase_Test, INDR_LastIteration_16T)
{
    auto timing = runInstruction({0xED, 0xBA}, 1);
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 16u);
    EXPECT_EQ(timing.accessOffset, 10u);
}

TEST_F(IOPhase_Test, OTDR_LastIteration_16T)
{
    auto timing = runInstruction({0xED, 0xBB}, 1);
    EXPECT_EQ(timing.accesses, 1);
    EXPECT_EQ(timing.total, 16u);
    EXPECT_EQ(timing.accessOffset, 13u);
}

/// endregion </Block IO>
