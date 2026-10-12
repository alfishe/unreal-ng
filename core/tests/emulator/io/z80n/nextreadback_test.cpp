// NextREG read-back values checked against zxnext.vhd's port_253b_dat (2.19 core): #09 (bit 3 reads 0), #34 (the mirrored sprite id,
// bit 7 clear), #41 (palette value bits 8:1), #44 (priority and bit 0), #8E (the 128K memory mapping as the paging ports left it)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
constexpr uint8_t kRegAudio = 0x09, kRegMirrorSprite = 0x34, kRegPaletteIndex = 0x40, kRegPaletteValue8 = 0x41, kRegPaletteControl = 0x43,
                  kRegPaletteValue9 = 0x44, kRegMemoryMapping = 0x8E;
constexpr uint8_t kLayer2FirstNoIncrement = 0x90,  // write select 001 (Layer 2, first palette), auto increment off
                   kPaletteEntry = 5, kValue8 = 0xAB, kSecondByte = 0x81, kSprite = 0x45;
constexpr uint8_t kAudioWritten = 0xFF, kAudioRead = 0xF7;  // bit 3 reads 0
constexpr uint16_t kPort7ffd = 0x7FFD, kPc = 0x8000;
constexpr uint8_t kPaging = 0x13;     // ROM 1, bank 3
constexpr uint8_t kMappingFor13 = 0x39;  // dffd(0)=0, 7ffd(2:0)=3 -> 0x30, bit 3 = 1, 1ffd bits 0, bit 0 = 7ffd bit 4
}  // namespace

class NextReadBack_Test : public ::testing::Test
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
};

TEST_F(NextReadBack_Test, TheRegistersReadBackWhatTheVhdlReturns)
{
    NextBoard& board = _ports->Board();
    board.WriteNextReg(kRegAudio, kAudioWritten);
    EXPECT_EQ(board.Read(kRegAudio), kAudioRead);

    board.WriteNextReg(kRegMirrorSprite, kSprite);
    EXPECT_EQ(board.Read(kRegMirrorSprite), kSprite);

    board.WriteNextReg(kRegPaletteControl, kLayer2FirstNoIncrement);
    board.WriteNextReg(kRegPaletteIndex, kPaletteEntry);
    board.WriteNextReg(kRegPaletteValue8, kValue8);
    EXPECT_EQ(board.Read(kRegPaletteValue8), kValue8);
    board.WriteNextReg(kRegPaletteValue9, kValue8);  // NR #44 takes the two bytes of an entry in turn
    board.WriteNextReg(kRegPaletteValue9, kSecondByte);
    EXPECT_EQ(board.Read(kRegPaletteValue9), kSecondByte) << "priority (bit 7) and the ninth colour bit";

    _ports->DecodePortOut(kPort7ffd, kPaging, kPc);
    EXPECT_EQ(board.Read(kRegMemoryMapping), kMappingFor13);
}
