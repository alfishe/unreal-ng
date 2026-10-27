// ZX-Evo BaseConf font RAM (evofontoverlay.h, docs/inprogress/2026-09-15-atm-baseconf-highres-ports/
// tdd-e8-wrprot-font-pal444-dosstall.md section 3.2): the power-on layout, the #BF bit 2 loader, the renderer, #0EBD.

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "debugger/ttd/atm/ttdevofontram.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_atm3.h"
#include "emulator/video/atm/atmfont.h"

namespace
{
class EvoFontOverlay_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    PortDecoder_ATM3* _decoder = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
        _decoder = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);

        // TR-DOS boot map: ROM in window 0, RAM in windows 1-3, the shadow ports open
        _decoder->ApplyBootROMDefaults(RM_DOS);
        _context->emulatorState.evo.pBF = 0x01;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void WriteBF(uint8_t value) { _decoder->DecodePortOut(0x00BF, value, 0x0000); }
    uint8_t& Font(unsigned index) { return _context->emulatorState.atm.fontRam[index]; }
};
}  // namespace

/// FNT-4: the font RAM starts as the built-in table, transposed to the FPGA's code * 8 + row address
TEST_F(EvoFontOverlay_Test, PowerOnFontIsTheBuiltInTableInCodeMajorOrder)
{
    for (unsigned code = 0; code < 256; code++)
        for (unsigned row = 0; row < 8; row++)
            ASSERT_EQ(Font(code * 8 + row), ATM_FONT[row * 256 + code]) << "code " << code << " row " << row;
}

/// FNT-3: the loader is on the bus only while bit 2 is set
TEST_F(EvoFontOverlay_Test, OverlayFollowsBitTwoOfBF)
{
    EXPECT_FALSE(_decoder->IsFontOverlayInstalled());
    WriteBF(0x05);
    EXPECT_TRUE(_decoder->IsFontOverlayInstalled());
    WriteBF(0x01);
    EXPECT_FALSE(_decoder->IsFontOverlayInstalled());
    WriteBF(0x05);
    _decoder->reset();
    EXPECT_FALSE(_decoder->IsFontOverlayInstalled()) << "reset clears #BF";
}

/// FNT-1: a write to #4000 + n lands in the RAM and in font byte n; the normal write still happens
TEST_F(EvoFontOverlay_Test, MemoryWriteAlsoWritesTheFont)
{
    WriteBF(0x05);
    const uint8_t before = Font(0x123);
    _z80->wd(0x4123, 0xA7);
    EXPECT_EQ(Font(0x123), 0xA7);
    EXPECT_EQ(_memory->MemoryReadFast(0x4123, false), 0xA7) << "the RAM byte too";
    EXPECT_NE(before, 0xA7);

    _z80->wd(0xC7FF, 0x3C);  // only A10..A0 select the font byte
    EXPECT_EQ(Font(0x7FF), 0x3C);
}

/// FNT-2: every memory write counts: a ROM window, and a write-protected RAM window
TEST_F(EvoFontOverlay_Test, RomAndProtectedWindowsStillFeedTheFont)
{
    WriteBF(0x05);
    _z80->wd(0x0042, 0x99);  // window 0 is ROM in this map
    EXPECT_EQ(Font(0x042), 0x99);

    _decoder->DecodePortOut(0x89F7, 0x01, 0x0000);  // protect window 2
    _context->emulatorState.p7FFD = 0x00;
    _decoder->DecodePortOut(0x89F7, 0x01, 0x0000);
    _z80->wd(0x8055, 0x5A);
    EXPECT_EQ(Font(0x055), 0x5A);
    EXPECT_NE(_memory->MemoryReadFast(0x8055, false), 0x5A) << "the protected RAM byte did not change";
}

/// FNT-3: with the bit clear nothing is mirrored
TEST_F(EvoFontOverlay_Test, NoMirrorWhileBitTwoIsClear)
{
    const uint8_t before = Font(0x200);
    _z80->wd(0x4200, static_cast<uint8_t>(before ^ 0xFF));
    EXPECT_EQ(Font(0x200), before);
}

/// FNT-8: a Z80 reset keeps the loaded font (the FPGA's RAM is not reset)
TEST_F(EvoFontOverlay_Test, ResetKeepsTheLoadedFont)
{
    Font(0x10) = 0x81;
    _decoder->reset();
    EXPECT_EQ(Font(0x10), 0x81);
}

/// FNT-6: #0EBD is #FF until a text frame has been drawn, then the last glyph byte the renderer fetched
TEST_F(EvoFontOverlay_Test, ReadbackStartsAtFF)
{
    _context->emulatorState.atm.fontByte = 0xFF;
    EXPECT_EQ(_decoder->DecodePortIn(0x0EBD, 0x0000), 0xFF);
    _context->emulatorState.atm.fontByte = 0x3E;
    EXPECT_EQ(_decoder->DecodePortIn(0x0EBD, 0x0000), 0x3E);
}

/// FNT-7: the blob round-trips the font and the glyph byte
TEST_F(EvoFontOverlay_Test, TtdBlobRoundTrip)
{
    ttd::TTDEvoFontRam blob(_context);
    Font(0x0) = 0x11;
    Font(0x7FF) = 0xEE;
    _context->emulatorState.atm.fontByte = 0x42;

    std::vector<uint8_t> saved(blob.TTDStateSize());
    blob.TTDSaveState(saved.data());
    const uint64_t hash = blob.TTDHashState();

    Font(0x0) = 0;
    Font(0x7FF) = 0;
    _context->emulatorState.atm.fontByte = 0;
    EXPECT_NE(blob.TTDHashState(), hash);

    blob.TTDLoadState(saved.data());
    EXPECT_EQ(Font(0x0), 0x11);
    EXPECT_EQ(Font(0x7FF), 0xEE);
    EXPECT_EQ(_context->emulatorState.atm.fontByte, 0x42);
    EXPECT_EQ(blob.TTDHashState(), hash);
}
