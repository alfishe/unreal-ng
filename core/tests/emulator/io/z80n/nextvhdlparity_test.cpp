// The Next board against the official zxnext.vhd (CORE 3.02.03): N1 MMU pages >= #E0, N2 the floating bus, N3 NR #03, N4 60 Hz,
// N5 NR #07 / #80, N6 NR #8E, N7 NR #04 / #0A, N8 the port contention (docs/inprogress/2026-10-07-zx-next/design-cpu.md)

#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/io/z80n/nexttiming.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/video/ulacontention.h"

namespace
{
constexpr uint16_t kPc = 0x8000;
constexpr uint8_t kRegMachineType = 0x03, kRegConfigMapping = 0x04, kRegVideo = 0x05, kRegCpuSpeed = 0x07, kRegPeripheral3 = 0x0A,
                  kRegMmu2 = 0x52, kRegMmu0 = 0x50, kRegExpansionBus = 0x80, kRegMemoryMapping = 0x8E;
}  // namespace

class NextVhdlParity_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    NextBoard& Board() { return _ports->Board(); }
    NextMemory& Mem() { return *static_cast<NextMemory*>(_context->pMemory); }
    void Nr(uint8_t reg, uint8_t value) { Board().WriteNextReg(reg, value); }
    void SetTiming(uint8_t timing)
    {
        Mem().SetConfigMode(true);
        Nr(kRegMachineType, static_cast<uint8_t>(0x80 | (timing << 4) | 0x01));  // config mode: type 1, timing as asked
        Mem().SetConfigMode(false);
    }
};

// ---- N1 ----------------------------------------------------------------------------------------------------------------

TEST_F(NextVhdlParity_Test, N1MmuPagesFromE0ShowTheRomOnlyInSlotsZeroAndOne)
{
    Nr(kRegMmu0, 0xE0);
    EXPECT_STREQ(Mem().SlotKind(0), "rom") << "slot 0: any page >= #E0 is the ROM";
    Nr(kRegMmu2, 0xFF);
    EXPECT_STREQ(Mem().SlotKind(2), "unmapped") << "slot 2: #FF is neither ROM nor RAM";
    Nr(kRegMmu2, 0xE5);
    EXPECT_STREQ(Mem().SlotKind(2), "unmapped");
    Nr(kRegMmu0, 0xFF);
    EXPECT_STREQ(Mem().SlotKind(0), "rom");
}

// ---- N2 ----------------------------------------------------------------------------------------------------------------

TEST_F(NextVhdlParity_Test, N2OnlyPortFFAndThePlus3PortDriveTheFloatingBus)
{
    SetTiming(1);
    _ports->OnFrameEnd();
    ASSERT_EQ(_context->emulatorState.ula_timing_class, 1);
    EXPECT_TRUE(_ports->FloatingBusDrives(0x40FF));
    EXPECT_FALSE(_ports->FloatingBusDrives(0x0001)) << "an odd port that is not #xxFF reads #FF";
    EXPECT_FALSE(_ports->FloatingBusDrives(0x001F));
    EXPECT_FALSE(_ports->FloatingBusDrives(0x0FFD)) << "the +3 port needs the +3 timing";
    SetTiming(3);
    _ports->OnFrameEnd();
    ASSERT_EQ(_context->emulatorState.ula_timing_class, 3);
    EXPECT_FALSE(_ports->FloatingBusDrives(0x40FF)) << "+3 timing: #xxFF reads #FF";
    EXPECT_TRUE(_ports->FloatingBusDrives(0x0FFD));
    Nr(0x82, 0xEF);  // NR #82 bit 4 off: the +3 floating bus port is not decoded
    EXPECT_FALSE(_ports->FloatingBusDrives(0x0FFD));
    SetTiming(4);
    _ports->OnFrameEnd();
    EXPECT_FALSE(_ports->FloatingBusDrives(0x40FF)) << "Pentagon timing: #FF everywhere";
}

// ---- N3 ----------------------------------------------------------------------------------------------------------------

TEST_F(NextVhdlParity_Test, N3Nr03TimingLockAndRemap)
{
    Mem().SetConfigMode(false);
    Nr(kRegMachineType, 0x80 | (0 << 4));
    EXPECT_EQ(Board().Timing(), 1) << "000 -> 48K";
    Nr(kRegMachineType, 0x80 | (6 << 4));
    EXPECT_EQ(Board().Timing(), 3) << "101-111 -> +3";
    Nr(kRegMachineType, 0x10 | 0x00);
    EXPECT_EQ(Board().Timing(), 3) << "without bit 7 the timing stays";
    Nr(kRegMachineType, 0x80 | (2 << 4) | 0x08);
    EXPECT_EQ(Board().Timing(), 3) << "bit 3 set: no change, and the lock toggles";
    EXPECT_NE(Board().Read(kRegMachineType) & 0x08, 0) << "dt lock reads back";
    Nr(kRegMachineType, 0x80 | (2 << 4));
    EXPECT_EQ(Board().Timing(), 3) << "locked";
    Nr(kRegMachineType, 0x08);
    EXPECT_EQ(Board().Read(kRegMachineType) & 0x08, 0) << "bit 3 toggles the lock back";
    Nr(kRegMachineType, 0x80 | (2 << 4));
    EXPECT_EQ(Board().Timing(), 2);
    EXPECT_EQ((Board().Read(kRegMachineType) >> 4) & 7, 2) << "the timing reads back";
}

TEST_F(NextVhdlParity_Test, N3Nr03TypeIsTakenInConfigModeOnlyAndOnlyOneToFour)
{
    const uint8_t before = Board().MachineType();
    Mem().SetConfigMode(true);
    Nr(kRegMachineType, 0x05);
    EXPECT_EQ(Board().MachineType(), before) << "5 is not a type: kept";
    EXPECT_FALSE(Mem().InConfigMode()) << "a non-zero value leaves config mode";
    Mem().SetConfigMode(true);
    Nr(kRegMachineType, 0x03);
    EXPECT_EQ(Board().MachineType(), 3);
    Nr(kRegMachineType, 0x01);
    EXPECT_EQ(Board().MachineType(), 3) << "outside config mode the type stays";
    Nr(kRegMachineType, 0x07);
    EXPECT_TRUE(Mem().InConfigMode()) << "111 enters config mode";
}

// ---- N4 ----------------------------------------------------------------------------------------------------------------

TEST_F(NextVhdlParity_Test, N4SixtyHzTablesFollowZxulaTiming)
{
    NextTiming t;
    ASSERT_TRUE(NextTimingFor(1, true, t));
    EXPECT_EQ(t.frame, 224u * 264u);
    EXPECT_EQ(t.minVactive, 40u);
    ASSERT_TRUE(NextTimingFor(2, true, t));
    EXPECT_EQ(t.frame, 228u * 264u);
    ASSERT_TRUE(NextTimingFor(3, true, t));
    EXPECT_EQ(t.frame, 228u * 264u);
    ASSERT_TRUE(NextTimingFor(4, true, t));
    EXPECT_EQ(t.frame, 71680u) << "the Pentagon has no 60 Hz";
    EXPECT_FALSE(t.sixty);
}

TEST_F(NextVhdlParity_Test, N4SixtyHzSwitchesAtTheFrameSyncAndNotUnderPentagon)
{
    SetTiming(1);
    _ports->OnFrameEnd();
    EXPECT_EQ(_context->config.frame, 69888u);
    Nr(kRegVideo, Board().Stored(kRegVideo) | 0x04);
    EXPECT_EQ(_context->config.frame, 69888u) << "not before the frame sync";
    _ports->OnFrameEnd();
    EXPECT_EQ(_context->config.frame, 59136u);
    EXPECT_EQ(_context->emulatorState.ula_sixty_hz, 1);
    SetTiming(4);
    _ports->OnFrameEnd();
    EXPECT_EQ(Board().Stored(kRegVideo) & 0x04, 0) << "the Pentagon forces bit 2 to 0";
    EXPECT_EQ(_context->emulatorState.ula_sixty_hz, 0);
}

// ---- N5 ----------------------------------------------------------------------------------------------------------------

TEST_F(NextVhdlParity_Test, N5ExpansionBusForcesTheBaseSpeed)
{
    Nr(kRegCpuSpeed, 0x03);
    EXPECT_EQ(Board().Read(kRegCpuSpeed), 0x33);
    Nr(kRegExpansionBus, 0x80);
    EXPECT_EQ(Board().Read(kRegCpuSpeed), 0x03) << "bits 5:4 = the speed in force (3.5 MHz), 1:0 the programmed one";
    Nr(kRegExpansionBus, 0x00);
    EXPECT_EQ(Board().Read(kRegCpuSpeed), 0x33);
}

// ---- N6 ----------------------------------------------------------------------------------------------------------------

TEST_F(NextVhdlParity_Test, N6Nr8eBankChangeClearsDffdBits31)
{
    _ports->DecodePortOut(0xDFFD, 0x03, kPc);
    EXPECT_EQ(Mem().GetExtendedBank(), 3);
    Nr(kRegMemoryMapping, 0x88);
    EXPECT_EQ(Mem().GetExtendedBank(), 1) << "00 & NR #8E bit 7";
    Nr(kRegMemoryMapping, 0x08);
    EXPECT_EQ(Mem().GetExtendedBank(), 0);
}

// ---- N7 ----------------------------------------------------------------------------------------------------------------

TEST_F(NextVhdlParity_Test, N7Nr04IsEightBitsAndReadsZeroNr0aMasksItsReadback)
{
    Nr(kRegConfigMapping, 0xC5);
    EXPECT_EQ(Board().Stored(kRegConfigMapping), 0xC5) << "the Issue 5 board keeps all 8 bits";
    EXPECT_EQ(Board().Read(kRegConfigMapping), 0x00) << "no read case in zxnext.vhd";
    Mem().SetConfigMode(true);
    Nr(kRegPeripheral3, 0xFF);
    EXPECT_EQ(Board().Read(kRegPeripheral3), 0xFB) << "bit 2 reads 0";
    Mem().SetConfigMode(false);
    Nr(kRegPeripheral3, 0x00);
    EXPECT_EQ(Board().Read(kRegPeripheral3), 0xE0) << "outside config mode bits 7:5 stay";
}

// ---- N8 ----------------------------------------------------------------------------------------------------------------

TEST_F(NextVhdlParity_Test, N8OddPortsThatContendAre7ffdAndUlaPlusOnly)
{
    SetTiming(2);
    _ports->OnFrameEnd();
    ASSERT_EQ(_context->emulatorState.ula_timing_class, 2);
    EXPECT_TRUE(_ports->OddPortContended(0x7FFD));
    EXPECT_FALSE(_ports->OddPortContended(0x1FFD)) << "#1FFD decodes as its own port: not contended";
    EXPECT_TRUE(_ports->OddPortContended(0xBF3B));
    EXPECT_TRUE(_ports->OddPortContended(0xFF3B));
    EXPECT_FALSE(_ports->OddPortContended(0x40FF));
    EXPECT_FALSE(_ports->OddPortContended(0x7F1F));
    Nr(0x82, 0xFD);  // NR #82 bit 1 off: #7FFD is not decoded
    EXPECT_FALSE(_ports->OddPortContended(0x7FFD));
    SetTiming(3);
    _ports->OnFrameEnd();
    EXPECT_FALSE(_ports->OddPortContended(0x7FFD)) << "the +3 timing has no I/O contention";
}

TEST_F(NextVhdlParity_Test, N8MemoryContentionDecidesPerEightKPage)
{
    UlaContention* ula = _context->pUlaContention;
    ASSERT_NE(ula, nullptr);
    SetTiming(1);
    _ports->OnFrameEnd();
    Nr(kRegMmu2, 10);  // 8K page 10 = the first half of 16K bank 5
    Nr(kRegMmu2 + 1, 0);  // slot 3 holds page 0, plain RAM, in the same 16K window
    EXPECT_TRUE(ula->IsContendedAt(0x4000));
    EXPECT_FALSE(ula->IsContendedAt(0x6000)) << "the neighbouring 8K page is plain RAM";
}
