// The 128K / grey +2 port decoder: reads that the paging latch sees, and the AY's #BFFD
// (docs/inprogress/2026-09-30-fusetest-core-defects/research.md claims 2 and 3).

#include <gtest/gtest.h>

#include "_helpers/soundcardscope.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

namespace
{
class PortDecoderSpectrum128_Test : public ::testing::TestWithParam<const char*>
{
protected:
    SoundCardScope _ay{TestSound::TurboSound};  // the machine's AY (the test runner leaves the slot empty)
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;

    void Create(const char* model)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("spectrum128-ports", model, LoggerLevel::LogError);
        ASSERT_TRUE(_emulator);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
        _context->config.floatbus = 1;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    /// `IN A,(C)` with BC = `port`, run by the CPU from uncontended RAM in the top border (the bus reads #FF there)
    uint8_t InFromCode(uint16_t port)
    {
        _memory->DirectWriteToZ80Memory(0x8000, 0xED);
        _memory->DirectWriteToZ80Memory(0x8001, 0x78);
        _z80->pc = 0x8000;
        _z80->bc = port;
        _z80->iff1 = 0;
        _z80->t = 100;
        _z80->Z80Step();
        return _z80->a;
    }
};
}  // namespace

/// The 128K's HAL decodes the paging latch from IORQ with RD or WR: an IN from #7FFD writes the bus byte, #FF in the
/// border, bits 0-5 of it: RAM 7 at #C000, the shadow screen, ROM 1 and the lock (the "PRINT IN 32765" crash)
TEST_P(PortDecoderSpectrum128_Test, ReadOfThe7FFDDecodeWritesTheBusByte)
{
    Create(GetParam());
    _context->pPortDecoder->DecodePortOut(0x7FFD, 0x00, 0x8000);
    EXPECT_EQ(InFromCode(0x7FFD), 0xFF) << "the border's floating bus";
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x3F) << "the latch holds six bits";
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 7);
    EXPECT_EQ(_memory->GetROMPage(), 1);

    _context->pPortDecoder->DecodePortOut(0x7FFD, 0x00, 0x8000);
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x3F) << "the read set the lock bit";
}

TEST_P(PortDecoderSpectrum128_Test, ReadOfAMirrorWritesItToo)
{
    Create(GetParam());
    _context->pPortDecoder->DecodePortOut(0x7FFD, 0x00, 0x8000);
    InFromCode(0x3FFD);  // A15 = 0, A1 = 0
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x3F);
}

TEST_P(PortDecoderSpectrum128_Test, LockedLatchIgnoresReads)
{
    Create(GetParam());
    _context->pPortDecoder->DecodePortOut(0x7FFD, 0x21, 0x8000);  // page 1, locked
    InFromCode(0x7FFD);
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x21);
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 1);
}

TEST_P(PortDecoderSpectrum128_Test, OtherPortsDoNotTouchTheLatch)
{
    Create(GetParam());
    _context->pPortDecoder->DecodePortOut(0x7FFD, 0x03, 0x8000);
    InFromCode(0x40FF);  // A1 = 1
    InFromCode(0xFFFD);  // A15 = 1
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x03);
}

/// On the 128K the AY stays off the bus for a read of #BFFD (the service manual's BDIR / BC1 table): the read is
/// left undecoded, for the floating bus
TEST_P(PortDecoderSpectrum128_Test, BFFDReadDoesNotReadTheAyRegister)
{
    Create(GetParam());
    _context->pPortDecoder->DecodePortOut(0xFFFD, 11, 0x8000);
    _context->pPortDecoder->DecodePortOut(0xBFFD, 0x55, 0x8000);
    EXPECT_EQ(_context->pPortDecoder->DecodePortIn(0xFFFD, 0x8000), 0x55) << "the register was written";
    _context->pPortDecoder->DecodePortIn(0xBFFD, 0x8000);
    EXPECT_FALSE(_context->pPortDecoder->WasLastPortDecoded()) << "nothing drives the bus: the floating bus";
}

INSTANTIATE_TEST_SUITE_P(Models, PortDecoderSpectrum128_Test, ::testing::Values("128k", "PLUS2"));

/// The +2A / +3 gate array decodes #7FFD on writes only
TEST(PortDecoderSpectrum3ReadCycle_Test, ReadOf7FFDLeavesTheLatch)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("plus3-ports", "PLUS3", LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    Z80* z80 = context->pCore->GetZ80();
    context->pPortDecoder->DecodePortOut(0x7FFD, 0x00, 0x8000);
    context->pMemory->DirectWriteToZ80Memory(0x8000, 0xED);
    context->pMemory->DirectWriteToZ80Memory(0x8001, 0x78);
    z80->pc = 0x8000;
    z80->bc = 0x7FFD;
    z80->iff1 = 0;
    z80->t = 100;
    z80->Z80Step();
    EXPECT_EQ(context->emulatorState.p7FFD, 0x00);
    EXPECT_EQ(z80->readCycleLatch, nullptr) << "no read-cycle latch on the +3";
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}
