// NextREG read-back values checked against zxnext.vhd's port_253b_dat (2.19 core): #09 (bit 3 reads 0), #34 (the mirrored sprite id,
// bit 7 clear), #41 (palette value bits 8:1), #44 (priority and bit 0), #8E (the 128K memory mapping as the paging ports left it)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/memory/next/nextmemory.h"
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

// zxnext.vhd 5878-6290 (port_253b_dat): the multiplexer returns only the bits each register keeps, at their places, and 0 for a
// register it has no case for (`when others`). Every register below is written #FF and read back
TEST_F(NextReadBack_Test, NarrowAndUnknownRegistersReadWhatTheMultiplexerReturns)
{
    struct Case
    {
        uint8_t reg;
        uint8_t read;
    };
    static constexpr Case kCases[] = {
        {0x0B, 0xB1},  // 5915: joy_iomode_en & '0' & joy_iomode & "000" & joy_iomode_0
        {0x12, 0x7F},  // 5930: '0' & nr_12_layer2_active_bank
        {0x13, 0x7F},  // 5933
        {0x2F, 0x03},  // 6018: "000000" & nr_30_tm_scrollx(9 downto 8)
        {0x4C, 0x0F},  // 6057: "0000" & nr_4c_tm_transparent_index
        {0x68, 0xFD},  // 6093: ... & nr_68_ula_fine_scroll_x & '0' & nr_68_ula_stencil_mode
        {0x6A, 0x3F},  // 6099: "00" & radastan & radastan_xor & lores palette offset
        {0x70, 0x3F},  // 6114: "00" & layer 2 resolution & palette offset
        {0x71, 0x01},  // 6117: "0000000" & nr_71_layer2_scrollx_msb
        {0x8F, 0x03},  // 6162: "000000" & nr_8f_mapping_mode
        {0xD8, 0x01},  // 6266: "0000000" & nr_d8_io_trap_fdc_en
        // whole bytes
        {0x14, 0xFF}, {0x15, 0xFF}, {0x16, 0xFF}, {0x17, 0xFF}, {0x26, 0xFF}, {0x27, 0xFF}, {0x30, 0xFF}, {0x31, 0xFF}, {0x32, 0xFF},
        {0x33, 0xFF}, {0x4A, 0xFF}, {0x4B, 0xFF}, {0x6B, 0xFF}, {0x6C, 0xFF}, {0x7F, 0xFF}, {0x82, 0xFF}, {0x83, 0xFF}, {0x84, 0xFF},
        {0x86, 0xFF}, {0x87, 0xFF}, {0x88, 0xFF}, {0x91, 0xFF}, {0x92, 0xFF}, {0xB8, 0xFF}, {0xB9, 0xFF}, {0xBA, 0xFF}, {0xBB, 0xFF},
        {0xD9, 0xFF},
        // no case in the multiplexer: the key joystick map (#29-#2B), the commented-out #A3, the gaps
        {0x0C, 0x00}, {0x0D, 0x00}, {0x24, 0x00}, {0x29, 0x00}, {0x2A, 0x00}, {0x2B, 0x00}, {0x3A, 0x00}, {0xA3, 0x00}, {0xC1, 0x00},
        {0xC7, 0x00}, {0xE0, 0x00}, {0xFF, 0x00},
    };
    NextBoard& board = _ports->Board();
    for (const Case& c : kCases)
    {
        board.WriteNextReg(c.reg, 0xFF);
        EXPECT_EQ(board.Read(c.reg), c.read) << "NR #" << std::hex << int(c.reg);
    }
    EXPECT_EQ(board.Stored(0x70), 0xFF) << "the stored byte keeps what was written: only the read path masks";
}

// zxnext.vhd 5167-5169: NR #06 bit 2 (the PS/2 mode) is written in config mode only; the other bits always (5900 reads them all)
TEST_F(NextReadBack_Test, Nr06TakesThePs2ModeInConfigModeOnly)
{
    NextBoard& board = _ports->Board();
    NextMemory* memory = dynamic_cast<NextMemory*>(_context->pMemory);
    ASSERT_NE(memory, nullptr);
    memory->SetConfigMode(false);
    board.WriteNextReg(0x06, 0x00);
    board.WriteNextReg(0x06, 0xFF);
    EXPECT_EQ(board.Read(0x06), 0xFB);
    memory->SetConfigMode(true);
    board.WriteNextReg(0x06, 0xFF);
    EXPECT_EQ(board.Read(0x06), 0xFF);
    memory->SetConfigMode(false);
    board.WriteNextReg(0x06, 0x00);
    EXPECT_EQ(board.Read(0x06), 0x04) << "outside config mode the PS/2 bit keeps its value";
}
