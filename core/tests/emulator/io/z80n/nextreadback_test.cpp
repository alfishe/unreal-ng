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
    EXPECT_EQ(board.Read(kRegAudio), kAudioRead & 0xFC) << "the scanline bits 1:0 are the effective copy, from the last frame sync";
    _ports->OnFrameEnd();
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

// zxnext.vhd 6048: NR #44 reads nr_palette_dat(10:9) & "00000" & nr_palette_dat(0): the two priority bits of the second byte are
// stored with every entry of every palette (4920, 6972); NR #41 writes store 00; NR #28 reads nr_stored_palette_value (6004)
TEST_F(NextReadBack_Test, TheSecondPaletteByteKeepsItsPriorityBitsInEveryPalette)
{
    NextBoard& board = _ports->Board();
    board.WriteNextReg(0x43, 0x00);  // the ULA's first palette, auto increment on
    board.WriteNextReg(0x40, 9);
    board.WriteNextReg(0x44, 0xAA);
    board.WriteNextReg(0x44, 0xC1);
    board.WriteNextReg(0x40, 9);
    EXPECT_EQ(board.Read(0x44), 0xC1) << "bits 7 and 6 are stored even for a palette that does not use them, bit 0 is blue's low bit";
    EXPECT_EQ(board.Read(0x41), 0xAA);
    EXPECT_EQ(board.Read(0x28), 0xAA) << "the stored byte of the first NR #44 write";
    board.WriteNextReg(0x28, 0x77);
    EXPECT_EQ(board.Read(0x28), 0xAA) << "a write of NR #28 is the keymap address, not the palette's byte";

    board.WriteNextReg(0x41, 0x55);  // an 8-bit write stores priority 00 and low blue = B1 | B0
    board.WriteNextReg(0x40, 9);
    EXPECT_EQ(board.Read(0x44), 0x01);
}

// sprites.vhd 599-614: with NR #09 bit 4 (tie) the mirror follows the port (#303B and a completed sprite of #57); untied it is NR #34's
TEST_F(NextReadBack_Test, TheSpriteMirrorFollowsThePortWhileTied)
{
    NextBoard& board = _ports->Board();
    NextSprites& sprites = board.Sprites();
    board.WriteNextReg(0x09, 0x10);
    sprites.WriteSlotSelect(0x23);
    EXPECT_EQ(board.Read(0x34), 0x23);
    for (unsigned i = 0; i < 4; i++)
        sprites.WriteAttribute(0x00);  // four bytes (bit 6 of the fourth clear) complete a sprite
    EXPECT_EQ(board.Read(0x34), 0x24);

    board.WriteNextReg(0x09, 0x00);
    sprites.WriteSlotSelect(0x30);
    EXPECT_EQ(board.Read(0x34), 0x24) << "untied: the port does not move it any more, it keeps the number it had followed";
    board.WriteNextReg(0x34, 7);
    EXPECT_EQ(board.Read(0x34), 7);
    sprites.WriteSlotSelect(0x31);
    EXPECT_EQ(board.Read(0x34), 7);
}

// zxnext.vhd 6139-6157, 6180-6181: the reset-type bit and the four enables, the propagate word, the Pi's output enables
TEST_F(NextReadBack_Test, TheEnableWordsReadTheirNarrowFields)
{
    NextBoard& board = _ports->Board();
    board.WriteNextReg(0x85, 0xFF);
    board.WriteNextReg(0x89, 0xFF);
    board.WriteNextReg(0x8A, 0xFF);
    board.WriteNextReg(0x90, 0xFF);
    board.WriteNextReg(0x93, 0xFF);
    EXPECT_EQ(board.Read(0x85), 0x8F);
    EXPECT_EQ(board.Read(0x89), 0x8F);
    EXPECT_EQ(board.Read(0x8A), 0x3F);
    EXPECT_EQ(board.Read(0x90), 0xFC) << "GPIO 1:0 are never outputs";
    EXPECT_EQ(board.Read(0x93), 0x0F);
}

// zxnext.vhd 5896, 5909, 6126, 6700-6710: NR #05 bits 2 / 0 and NR #09 bits 1:0 read the effective copies the video logic took at the
// last frame sync; NR #81 reads ROMCS_n (high with nothing on the bus) in bit 7, the stored bits 6:3, bit 2 = 0 and the speed bits 00
TEST_F(NextReadBack_Test, TheVideoBitsAreTheOnesTheLastFrameSyncTookAndNr81HasItsFixedBits)
{
    NextBoard& board = _ports->Board();
    board.Reset(true);
    const uint8_t before = board.Read(0x05) & 0x05;
    board.WriteNextReg(0x05, static_cast<uint8_t>(board.Stored(0x05) ^ 0x05));  // 50 / 60 Hz and the scandoubler flipped
    EXPECT_EQ(board.Read(0x05) & 0x05, before) << "bits 2 and 0 are the effective copies: unchanged until the frame sync";
    EXPECT_EQ(board.Read(0x05) & 0xFA, board.Stored(0x05) & 0xFA) << "the joystick bits are not delayed";
    board.WriteNextReg(0x09, 0x03);
    EXPECT_EQ(board.Read(0x09) & 0x03, 0x00);
    _ports->OnFrameEnd();
    EXPECT_EQ(board.Read(0x05) & 0x05, board.Stored(0x05) & 0x05);
    EXPECT_EQ(board.Read(0x09) & 0x03, 0x03);

    board.WriteNextReg(0x81, 0xFF);
    EXPECT_EQ(board.Read(0x81), 0x78) << "bit 7 ROMCS_n of an empty bus (0 on the real board), bits 6:3 stored, bit 2 and the speed 00";
    board.WriteNextReg(0x81, 0x00);
    EXPECT_EQ(board.Read(0x81), 0x00) << "the board's NextReg_defaults: #81 reads #00";
}
