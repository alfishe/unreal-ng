// SprinterMemory: the bank formula, graphics pages, the Spectrum screen
// shadow, the reset page, the ISA view and the loader layout (Sprinter
// test-plan §2.2 T-MEM-3..9; tdd-ports-memory §5).

#include "sprinterfixture.h"

#include "emulator/video/sprinter/sprintervideoram.h"

class SprinterMemory_Test : public SprinterFixture
{
protected:
    /// MAME update_memory window 0 (sprinter.cpp:320-357), transcribed independently of
    /// SprinterMemory: kind 0 = ROM page, 1 = fast RAM page, 2 = RAM from cell `value`
    struct Window0
    {
        int kind;
        uint8_t value;
        bool writable;
    };
    static Window0 MameWindow0(bool romSys, bool cashOn, bool ramSys, uint8_t sc, uint8_t pn, bool dos, bool arom16,
                               bool sysPg, uint8_t romRg)
    {
        const bool preRom = romSys || cashOn;
        const bool preCash = !cashOn;
        if (!preRom && preCash)
            return {0, static_cast<uint8_t>((romRg & 0x0F) ^ (!sysPg << 3)), false};
        if (preRom && !preCash)
            return {1, static_cast<uint8_t>(romRg & 3), true};
        const bool sc0 = sc & 1;
        const bool scLc = !(sc0 && ramSys);
        const uint8_t spr = (sc & 2) ? 0 : static_cast<uint8_t>((dos << 1) | (((pn >> 4) & 1) || !dos));
        const uint8_t pg0 = static_cast<uint8_t>(0x20 | ((sc0 || !ramSys) << 3) | ((arom16 && !(sc0 && ramSys)) << 2) |
                                                 ((((spr >> 1) & 1) && scLc) || !ramSys) << 1 |
                                                 (((spr & 1) && scLc) || !ramSys));
        return {2, pg0, sc0 && ramSys};
    }
};

// T-MEM-3: window 0 over romOff x cacheOn x ramSys x #1FFD b0/b1 x #7FFD b4 x DOS x arom16
TEST_F(SprinterMemory_Test, Window0_TruthTableMatchesMame)
{
    // Distinct pages in the vROM cells, so the chosen cell is visible
    for (uint8_t i = 0; i < 16; i++)
        Pld().cells[0x20 + i] = static_cast<uint8_t>(0x60 + i);

    int checked = 0;
    for (int bits = 0; bits < 256; bits++)
    {
        SprinterPldState& pld = Pld();
        pld.romOff = bits & 1;
        pld.cacheOn = (bits >> 1) & 1;
        pld.ramSys = (bits >> 2) & 1;
        pld.sc = static_cast<uint8_t>((bits >> 3) & 3);
        pld.pn = static_cast<uint8_t>(((bits >> 5) & 1) << 4);
        pld.dos = (bits >> 6) & 1;
        pld.arom16 = (bits >> 7) & 1;
        pld.sysPg = 0;
        pld.romRg = 0x02;
        _decoder->UpdateBanks();

        const Window0 expected = MameWindow0(pld.romOff, pld.cacheOn, pld.ramSys, pld.sc, pld.pn, pld.dos, pld.arom16,
                                             pld.sysPg, pld.romRg);
        switch (expected.kind)
        {
            case 0:
                ASSERT_EQ(_memory->GetMemoryBankMode(0), BANK_ROM) << bits;
                ASSERT_EQ(_memory->GetROMPageForBank(0), expected.value) << bits;
                break;
            case 1:
                ASSERT_EQ(_memory->GetMemoryBankMode(0), BANK_CACHE) << bits;
                break;
            default:
            {
                ASSERT_EQ(_memory->GetMemoryBankMode(0), BANK_RAM) << bits;
                ASSERT_EQ(_memory->GetRAMPageForBank0(), pld.cells[expected.value]) << bits;
                const uint8_t before = Ram(pld.cells[expected.value], 0x0100);
                Poke(0x0100, static_cast<uint8_t>(before ^ 0xFF));
                ASSERT_EQ(Ram(pld.cells[expected.value], 0x0100) != before, expected.writable) << bits;
                Ram(pld.cells[expected.value], 0x0100) = before;
                break;
            }
        }
        checked++;
    }
    EXPECT_EQ(checked, 256);
}

// ROM page select: page 8 after reset, page 0 with SYS_PG (the BIOS page stubs, OUT (#7C),1 / 0)
TEST_F(SprinterMemory_Test, Window0_BiosPageStubs)
{
    OpenDcp();
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 8);
    SetCode(0x007C, false, 0xC6);
    Out(0x007C, 0x01);
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 0) << "OUT (#7C),1: ROM page 0";
    Out(0x007C, 0x00);
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 8) << "OUT (#7C),0: ROM page 8";
    Out(0x003C, 0x00);
    EXPECT_TRUE(IsRam(0x0000)) << "OUT (#3C): Spectrum mode, a vROM RAM page";
}

// T-MEM-4 / T-MEM-5 / T-MEM-6: graphics pages #50-#5F
TEST_F(SprinterMemory_Test, GraphicsPage_VideoAddressTransparencyAndReads)
{
    OpenDcp();
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().portY = 3;

    // Plain graphics page #50: main RAM and video RAM at PORT_Y x 1024 + A[9:0]
    Pld().Cell(0xEA) = 0x50;
    _decoder->UpdateBanks();
    Poke(0x8405, 0x12);  // A[9:0] = #005
    EXPECT_EQ(vram.Read(3 * 1024 + 5), 0x12);
    EXPECT_EQ(Ram(0x50, 3 * 1024 + 5), 0x12);
    EXPECT_EQ(Ram(0x50, 0x0405), 0x50) << "the plain store must not land at the CPU address";
    EXPECT_EQ(Peek(0x8005), 0x12) << "T-MEM-6: reads come from main RAM at the video address";

    // Bit 3: #FF is transparent
    Pld().Cell(0xEA) = 0x58;
    _decoder->UpdateBanks();
    Poke(0x8005, 0xFF);
    EXPECT_EQ(vram.Read(3 * 1024 + 5), 0x12);
    Poke(0x8005, 0x34);
    EXPECT_EQ(vram.Read(3 * 1024 + 5), 0x34);

    // Bit 2: video RAM only
    Pld().Cell(0xEA) = 0x54;
    _decoder->UpdateBanks();
    Poke(0x8005, 0x56);
    EXPECT_EQ(vram.Read(3 * 1024 + 5), 0x56);
    EXPECT_EQ(Ram(0x50, 3 * 1024 + 5), 0x34) << "main RAM unchanged";

    // PORT_Y moves the line
    Pld().portY = 200;
    Pld().Cell(0xEA) = 0x50;
    _decoder->UpdateBanks();
    Poke(0x83FF, 0x77);
    EXPECT_EQ(vram.Read(200 * 1024 + 0x3FF), 0x77);
    EXPECT_EQ(Ram(0x50 + (200 * 1024 + 0x3FF) / PAGE_SIZE, (200 * 1024 + 0x3FF) % PAGE_SIZE), 0x77);
}

// T-MEM-7: the Spectrum screen shadow address (MAME :1212-1216)
TEST_F(SprinterMemory_Test, SpectrumShadow_Address)
{
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0x4000, 0, 0x35), 0u);
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0x57FF, 0, 0x35), (0xFFu << 10) | 0x17u);
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0x4000, 1, 0x35), 1u << 5) << "RGADR odd: the other half";
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0x4000, 2, 0x35), 1u << 6) << "RGADR bits 4-1 pick the block";
    // Window 3 with Spectrum page 7: zxA15 = pg3 bit 1
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0xC000, 0, 0x37), 1u << 5);
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0xC000, 0, 0x35), 0u);
}

TEST_F(SprinterMemory_Test, SpectrumShadow_WritesFollowAllModeAndPortY)
{
    OpenDcp();
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().allMode = 0;
    Pld().portY = 0;

    Poke(0x4123, 0x9A);
    EXPECT_EQ(vram.Read(SprinterMemory::ZxShadowAddress(0x4123, 0, Pld().pg3)), 0x9A);
    EXPECT_EQ(Ram(Pld().Cell(0xE9), 0x0123), 0x9A) << "the plain store lands too";

    // #6000-#7FFF only with PORT_Y bit 7
    Poke(0x6123, 0x11);
    EXPECT_EQ(vram.Read(SprinterMemory::ZxShadowAddress(0x6123, 0, Pld().pg3)), 0x00);
    Pld().portY = 0x80;
    Poke(0x6123, 0x22);
    EXPECT_EQ(vram.Read(SprinterMemory::ZxShadowAddress(0x6123, 0x80, Pld().pg3)), 0x22);

    // PORT_Y bit 6 disables, ALL_MODE bit 0 disables
    Pld().portY = 0x40;
    Poke(0x4500, 0x33);
    EXPECT_EQ(vram.Read(SprinterMemory::ZxShadowAddress(0x4500, 0x40, Pld().pg3)), 0x00);
    Pld().portY = 0;
    Pld().allMode = 1;
    Poke(0x4600, 0x44);
    EXPECT_EQ(vram.Read(SprinterMemory::ZxShadowAddress(0x4600, 0, Pld().pg3)), 0x00);
}

// T-MEM-8: a write to page #A0 with #1FFD = #10 resets the CPU (soft reset)
TEST_F(SprinterMemory_Test, ResetPage_WriteRequestsSoftReset)
{
    OpenDcp();
    Pld().sc = 0x10;  // #1FFD bit 4: window 3 shows cell #F8 (ComputePg3)
    _decoder->UpdateBanks();
    ASSERT_EQ(Pld().pg3, 0x38);
    Pld().cells[Pld().pg3] = 0xA0;
    _decoder->UpdateBanks();
    ASSERT_EQ(_sprinterMemory->GetBankAction(3), SprinterMemory::BankAction::ResetPage);

    _z80->pc = 0x1234;
    Poke(0xC000, 0x01);
    EXPECT_NE(Pld().resetPending, 0);
    _decoder->OnMachineStep(0);
    EXPECT_EQ(_z80->pc, 0x0000);
    EXPECT_EQ(Pld().starting, 1);
    EXPECT_EQ(Pld().configState, SprinterConfigState::Configured) << "the PLD stays configured";

    // Another #1FFD value: a normal write
    OpenDcp();
    Pld().sc = 0x00;
    Pld().cells[Pld().pg3] = 0xA0;
    _decoder->UpdateBanks();
    EXPECT_EQ(_sprinterMemory->GetBankAction(3), SprinterMemory::BankAction::Plain);
}

// T-MEM-9: window 3 = #D2 with #1FFD bit 4: the ISA view reads #FF, writes are ignored
TEST_F(SprinterMemory_Test, IsaView_ReadsFFWritesIgnored)
{
    OpenDcp();
    Pld().sc = 0x10;
    _decoder->UpdateBanks();
    Pld().cells[Pld().pg3] = 0xD2;
    _decoder->UpdateBanks();
    EXPECT_EQ(Peek(0xC010), 0xFF);
    Poke(0xC010, 0x12);
    EXPECT_EQ(Ram(0xD2, 0x0010), 0xD2);

    Pld().cells[Pld().pg3] = 0xD1;  // not an ISA pattern ((page & #F9) != #D0)
    _decoder->UpdateBanks();
    EXPECT_EQ(Peek(0xC010), 0xD1);
}

// Loader layout: before the PLD is configured the windows show ROM pages #C-#F;
// the Z84C15's CS0 boundary gives the top to the fast RAM
TEST_F(SprinterMemory_Test, LoaderLayout_RomPagesAndFastRamAboveCs0)
{
    _decoder->BeginLoading();
    _decoder->GetZ84().PowerOn();
    _decoder->UpdateBanks();
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 0x0C);
    EXPECT_EQ(Tag(0x4000), kRomTagBase + 0x0D);
    EXPECT_EQ(Tag(0x8000), kRomTagBase + 0x0E);
    EXPECT_EQ(Peek(0xFE10), kRomTagBase + 0x0F) << "CSBR = #FF at power-on: everything is CS0 (ROM)";

    // The loader: SCRP = 2, CSBR = #FE (CS0 = #0000-#EFFF)
    Out(0x00EE, 0x02);
    Out(0x00EF, 0xFE);
    _sprinterMemory->FastRam()[0xFE10] = 0x3C;
    EXPECT_EQ(Peek(0xFE10), 0x3C) << "above CS0: fast RAM";
    EXPECT_EQ(Peek(0xEFFF), kRomTagBase + 0x0F);
    Poke(0xFE20, 0x5D);
    EXPECT_EQ(_sprinterMemory->FastRam()[0xFE20], 0x5D);
    EXPECT_EQ(Pld().bitstreamCount, 1u) << "every write is a configuration bit";
}
