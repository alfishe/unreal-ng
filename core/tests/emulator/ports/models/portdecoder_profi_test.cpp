#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/profifixture.h"

/// @brief ZX Profi 1024 paging, ROM/DOS latch and port decode truth tables.
///        Reference behaviour: UnrealSpeccy (io.cpp / memory.cpp), cross-checked with ZXMAK2 and Xpeccy.
///        Design: docs/inprogress/2026-09-21-profi/technical-design.md sections 3, 4, 6.
///        The synthetic ROM tags pages SYS=0xC0, TR-DOS=0xC1, 128K=0xC2, 48K=0xC3; RAM page n = 0x40|n.
class ProfiPortDecoder_Test : public ProfiMachineFixture
{
protected:
    PortDecoder_Profi* Decoder()
    {
        return dynamic_cast<PortDecoder_Profi*>(_context->pPortDecoder);
    }

    EmulatorState& State()
    {
        return _context->emulatorState;
    }

    /// Full-address OUT forms used by real software (OUT (C),r with BC = port)
    void Out7FFD(uint8_t value) { WritePort(0x7FFD, value); }
    void OutDFFD(uint8_t value) { WritePort(0xDFFD, value); }

    /// Leave the SYS/TR-DOS session: DOS latch off, so ROM14 alone picks 128K / 48K
    void DosLatchOff()
    {
        State().flags &= ~(CF_TRDOS | CF_DOSPORTS);
        _memory->UpdateZ80Banks();
    }

    static constexpr uint8_t Ram(uint8_t page) { return static_cast<uint8_t>(ProfiRamTagBase | page); }
};

/// @brief Reset boots the SYS (service / menu) ROM with the DOS latch on; latches are clear
TEST_F(ProfiPortDecoder_Test, ResetBootsSysRom)
{
    EXPECT_EQ(State().p7FFD, 0x00);
    EXPECT_EQ(State().pDFFD, 0x00);
    EXPECT_NE(State().flags & CF_TRDOS, 0) << "DOS latch is on after reset";

    EXPECT_EQ(BankTag(0x0000), 0xC0) << "SYS ROM (page 0)";
    EXPECT_EQ(BankTag(0x4000), Ram(5));
    EXPECT_EQ(BankTag(0x8000), Ram(2));
    EXPECT_EQ(BankTag(0xC000), Ram(0));
}

/// @brief ROM index = {DOS latch ? 0 : 2} | 7FFD.4 - the 48K/DOS side is selected by bit 4 = 1
TEST_F(ProfiPortDecoder_Test, RomSelectionTruthTable)
{
    // DOS latch on
    Out7FFD(0x00);
    EXPECT_EQ(BankTag(0x0000), 0xC0) << "latch on, ROM14=0 -> SYS";
    Out7FFD(0x10);
    EXPECT_EQ(BankTag(0x0000), 0xC1) << "latch on, ROM14=1 -> TR-DOS";

    // DOS latch off
    DosLatchOff();
    Out7FFD(0x00);
    EXPECT_EQ(BankTag(0x0000), 0xC2) << "latch off, ROM14=0 -> 128K";
    Out7FFD(0x10);
    EXPECT_EQ(BankTag(0x0000), 0xC3) << "latch off, ROM14=1 -> 48K";
}

/// @brief #7FFD RAM bits select the low 3 bits of the #C000 page
TEST_F(ProfiPortDecoder_Test, Port7FFDSelectsLowRamBits)
{
    for (uint8_t bank = 0; bank < 8; bank++)
    {
        Out7FFD(bank);
        EXPECT_EQ(BankTag(0xC000), Ram(bank)) << "bank " << static_cast<int>(bank);
    }
}

/// @brief #DFFD bits 2:0 supply the high RAM bits: 64 pages (1 MB) reachable at #C000
TEST_F(ProfiPortDecoder_Test, AllSixtyFourPagesAtC000)
{
    EXPECT_EQ(_memory->GetRamMask(), 0x3F) << "1024 KB -> 64 RAM pages";

    for (uint8_t high = 0; high < 8; high++)
    {
        for (uint8_t low = 0; low < 8; low++)
        {
            OutDFFD(high);
            Out7FFD(low);
            const uint8_t page = static_cast<uint8_t>((high << 3) | low);
            EXPECT_EQ(BankTag(0xC000), Ram(page)) << "page " << static_cast<int>(page);
        }
    }
}

/// @brief SCO (DFFD.3) swaps the roles of #4000 and #C000: #4000 = selected page, #C000 = page 7
TEST_F(ProfiPortDecoder_Test, ScoSwapsWindows)
{
    OutDFFD(0x03);      // high bits = 3
    Out7FFD(0x02);      // low bits = 2 -> page 26
    EXPECT_EQ(BankTag(0x4000), Ram(5));
    EXPECT_EQ(BankTag(0xC000), Ram(26));

    OutDFFD(0x03 | 0x08);
    EXPECT_EQ(BankTag(0x4000), Ram(26)) << "SCO: selected page at #4000";
    EXPECT_EQ(BankTag(0xC000), Ram(7)) << "SCO: page 7 at #C000";
    EXPECT_EQ(BankTag(0x8000), Ram(2));
}

/// @brief SCR (DFFD.6) maps page 6 instead of 2 at #8000 - unconditionally (UnrealSpeccy, ZXMAK2)
TEST_F(ProfiPortDecoder_Test, ScrSelectsPage6At8000)
{
    OutDFFD(0x40);
    EXPECT_EQ(BankTag(0x8000), Ram(6));
    Out7FFD(0x08);      // screen bit set does not matter for the window
    EXPECT_EQ(BankTag(0x8000), Ram(6));
    OutDFFD(0x00);
    EXPECT_EQ(BankTag(0x8000), Ram(2));
}

/// @brief WOROM (DFFD.4) replaces the ROM at #0000 by writable RAM page 0
TEST_F(ProfiPortDecoder_Test, WoromMapsRamAtZero)
{
    OutDFFD(0x10);
    EXPECT_EQ(BankTag(0x0000), Ram(0));

    DirectWrite(0x0000, 0xA5);
    EXPECT_EQ(BankTag(0x0000), 0xA5) << "RAM at #0000 is writable";

    OutDFFD(0x00);
    EXPECT_GE(BankTag(0x0000), 0xC0) << "ROM is back";
}

/// @brief 7FFD bit 5 locks paging (every bit, including screen); DFFD.4 lifts the lock
TEST_F(ProfiPortDecoder_Test, LockAndWoromOverride)
{
    Out7FFD(0x20 | 0x03);
    EXPECT_EQ(BankTag(0xC000), Ram(3));

    Out7FFD(0x07 | 0x08);
    EXPECT_EQ(State().p7FFD, 0x23) << "locked write ignored completely";
    EXPECT_EQ(BankTag(0xC000), Ram(3));

    OutDFFD(0x10);      // WOROM lifts the lock
    Out7FFD(0x05);
    EXPECT_EQ(State().p7FFD, 0x05);
    EXPECT_EQ(BankTag(0xC000), Ram(5));
}

/// @brief Reset clears the lock and both latches
TEST_F(ProfiPortDecoder_Test, ResetClearsLatches)
{
    Out7FFD(0x20 | 0x07);
    OutDFFD(0xFF);

    _context->pPortDecoder->reset();
    EXPECT_EQ(State().p7FFD, 0x00);
    EXPECT_EQ(State().pDFFD, 0x00);
    EXPECT_EQ(BankTag(0x0000), 0xC0);

    Out7FFD(0x03);
    EXPECT_EQ(BankTag(0xC000), Ram(3)) << "unlocked after reset";
}

/// @brief #7FFD is A15=0 & A1=0 (A2 not decoded); #DFFD is A15=1 & A13=0 & A1=0; never both
TEST_F(ProfiPortDecoder_Test, PagingPortsAreMutuallyExclusive)
{
    unsigned count7FFD = 0;
    unsigned countDFFD = 0;

    for (uint32_t port = 0; port <= 0xFFFF; port++)
    {
        const bool is7FFD = PortDecoder_Profi::IsPort_7FFD(static_cast<uint16_t>(port));
        const bool isDFFD = PortDecoder_Profi::IsPort_DFFD(static_cast<uint16_t>(port));

        EXPECT_FALSE(is7FFD && isDFFD) << "port " << std::hex << port << " decodes to both latches";
        count7FFD += is7FFD ? 1 : 0;
        countDFFD += isDFFD ? 1 : 0;
    }

    EXPECT_EQ(count7FFD, 0x8000u / 2u) << "A15=0, A1=0";
    EXPECT_EQ(countDFFD, 0x8000u / 2u / 2u) << "A15=1, A13=0, A1=0";

    EXPECT_TRUE(PortDecoder_Profi::IsPort_7FFD(0x7FFD));
    EXPECT_TRUE(PortDecoder_Profi::IsPort_7FFD(0x7FF9)) << "A2 is not decoded";
    EXPECT_TRUE(PortDecoder_Profi::IsPort_7FFD(0x1FFD));
    EXPECT_TRUE(PortDecoder_Profi::IsPort_7FFD(0x5FFD));
    EXPECT_FALSE(PortDecoder_Profi::IsPort_DFFD(0x5FFD));
    EXPECT_TRUE(PortDecoder_Profi::IsPort_DFFD(0xDFFD));
    EXPECT_FALSE(PortDecoder_Profi::IsPort_DFFD(0xFFFD)) << "A13=1 is the AY register port";
    EXPECT_FALSE(PortDecoder_Profi::IsPort_DFFD(0xBFFD)) << "#BFFD (A13=1) is the AY data port";
}

/// @brief A port that decodes to #7FFD must not also reach #DFFD (was a double dispatch on #5FFD)
TEST_F(ProfiPortDecoder_Test, Port5FFDHitsOnlyPaging7FFD)
{
    WritePort(0x5FFD, 0x03);
    EXPECT_EQ(State().p7FFD, 0x03);
    EXPECT_EQ(State().pDFFD, 0x00);
}

/// @brief The FDC is on the bus with the DOS latch set; the WD1793 registers map #1F/#3F/#5F/#7F
///        and the system port #FF (UnrealSpeccy "BDI ports")
TEST_F(ProfiPortDecoder_Test, FdcPortsNormalMode)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);

    EXPECT_EQ(decoder->DecodeFDCPort(0x001F), 0x1F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x003F), 0x3F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x005F), 0x5F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x007F), 0x7F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x00FF), 0xFF);
    EXPECT_EQ(decoder->DecodeFDCPort(0x00E3), 0xFF) << "system port: (p & 0xE3) == 0xE3";
    EXPECT_EQ(decoder->DecodeFDCPort(0x0083), 0x00) << "A7=1 is not a register port in normal mode";
    EXPECT_EQ(decoder->DecodeFDCPort(0x00BF), 0x00) << "#BF is the system port only in CP/M mode";
}

/// @brief CP/M mode (DFFD.5) with ROM14=0 moves the system port to #BF and puts the FDC on the bus
TEST_F(ProfiPortDecoder_Test, FdcPortsCpmMode)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);

    DosLatchOff();
    EXPECT_EQ(State().flags & CF_DOSPORTS, 0) << "no FDC without DOS latch or CPM";

    OutDFFD(0x20);
    EXPECT_NE(State().flags & CF_DOSPORTS, 0) << "CPM puts the disk interface on the bus";

    EXPECT_EQ(decoder->DecodeFDCPort(0x00BF), 0xFF) << "CP/M system port";
    EXPECT_EQ(decoder->DecodeFDCPort(0x00FF), 0x00) << "#FF is not the system port in CP/M mode";
    EXPECT_EQ(decoder->DecodeFDCPort(0x001F), 0x1F);
}

/// @brief "Modified" ports (ROM14=1 and CPM): #83/#A3/#C3/#E3 registers, #3F system port
TEST_F(ProfiPortDecoder_Test, FdcPortsModifiedMode)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);

    Out7FFD(0x10);
    OutDFFD(0x20);

    EXPECT_EQ(decoder->DecodeFDCPort(0x0083), 0x1F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x00A3), 0x3F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x00C3), 0x5F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x00E3), 0x7F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x003F), 0xFF) << "modified-mode system port";
    EXPECT_EQ(decoder->DecodeFDCPort(0x001F), 0x00) << "normal-mode addresses are gone";
}

/// @brief Palette write: only with DS80, colour = ~A15..A8, index = (previous #FE value ^ 0xF)
TEST_F(ProfiPortDecoder_Test, PaletteWrite)
{
    // Without DS80 the write is ignored
    WritePort(0x00FE, 0x05);
    const uint16_t before = State().profiPalette[0x0A];
    WritePort(0xE27E, 0x00);
    EXPECT_EQ(State().profiPalette[0x0A], before);

    OutDFFD(0x80);
    WritePort(0x00FE, 0x05);            // index source: 0x05 ^ 0x0F = 0x0A; D7=0 -> blue LSB = 0
    WritePort(0xE27E, 0x00);            // colour = ~0xE2 = 0x1D -> entry = 0x1D << 1 | 0 = 0x3A
    EXPECT_EQ(State().profiPalette[0x0A], 0x3A);

    // A7=1 is not the palette port
    WritePort(0x00FE, 0x05);
    WritePort(0xFFFE, 0x00);            // A7=1: border write only
    EXPECT_EQ(State().profiPalette[0x0A], 0x3A);
}

/// @brief The extra blue LSB (9th palette bit) is latched from #FE.D7 of the write that supplies
/// the index, per Karabas video.vhd:205-208 (`palette[idx] <= (not A15..A8) & BORDER(7)`).
TEST_F(ProfiPortDecoder_Test, PaletteWriteCarriesExtraBlueBitFromFEBit7)
{
    OutDFFD(0x80);

    WritePort(0x00FE, 0x85);            // D7=1 -> blue LSB = 1; index = 0x85 ^ 0x0F = 0x0A
    WritePort(0xE27E, 0x00);            // colour = ~0xE2 = 0x1D -> entry = 0x1D << 1 | 1 = 0x3B
    EXPECT_EQ(State().profiPalette[0x0A], 0x3B);

    WritePort(0x00FE, 0x05);            // D7=0 -> blue LSB = 0; same index 0x0A
    WritePort(0xE27E, 0x00);
    EXPECT_EQ(State().profiPalette[0x0A], 0x3A) << "D7=0 clears the extra blue bit on the next write";
}

/// @brief Power-on palette matches the Karabas defaults (video.vhd:199-203): each active channel
/// is level 4/7 non-bright, 6/7 bright, out of the now-3-bit (0..7) range for G/R/B alike.
TEST_F(ProfiPortDecoder_Test, ResetPaletteMatchesHardwareDefaults)
{
    EXPECT_EQ(State().profiPalette[0x00], 0x000) << "black";
    EXPECT_EQ(State().profiPalette[0x01], 0x004) << "B4";
    EXPECT_EQ(State().profiPalette[0x02], 0x020) << "R4";
    EXPECT_EQ(State().profiPalette[0x04], 0x100) << "G4";
    EXPECT_EQ(State().profiPalette[0x07], 0x124) << "G4R4B4";
    EXPECT_EQ(State().profiPalette[0x08], 0x000) << "bright black stays black";
    EXPECT_EQ(State().profiPalette[0x0F], 0x1B6) << "G6R6B6 (bright white)";
}

/// @brief #FE bit 7 ("GX0"/UniCopy palette-present flag): in DS80, bit6 XOR bit0 of the palette
/// entry selected by the previous #FE write's index; outside DS80 it reads 1.
/// Karabas video.vhd:219, ZXMAK2 UlaProfi5XX.cs:34-53.
TEST_F(ProfiPortDecoder_Test, FEReadBit7ReportsGX0InDS80)
{
    // Outside DS80: bit 7 always reads 1 regardless of palette contents
    WritePort(0x00FE, 0x05);
    EXPECT_NE(ReadPort(0x00FE) & 0x80, 0) << "bit 7 pulled high outside DS80";

    OutDFFD(0x80);

    // Program index 0x0A (from #FE=0x05, D7=0) with colour 0x20 (G=001, R=000, B=00):
    // entry = (0x20 << 1) | 0 = 0x40 -> bit6=1 (G's LSB), bit0=0 (extra blue LSB) -> GX0 = 1^0 = 1
    WritePort(0x00FE, 0x05);
    WritePort(0xDF7E, 0x00);            // colour = ~0xDF = 0x20
    WritePort(0x00FE, 0x05);            // re-latch #FE so the same index (0x0A) is selected on read
    EXPECT_NE(ReadPort(0x00FE) & 0x80, 0) << "bit6=1 xor bit0=0 -> GX0=1";

    // Rewrite the same index with D7=1 this time: entry = (0x20 << 1) | 1 = 0x41 -> bit6=1, bit0=1
    WritePort(0x00FE, 0x85);            // D7=1 -> blue LSB=1, index still 0x0A
    WritePort(0xDF7E, 0x00);            // colour = ~0xDF = 0x20
    WritePort(0x00FE, 0x05);            // re-latch #FE (D7=0) so the read selects index 0x0A again
    EXPECT_EQ(ReadPort(0x00FE) & 0x80, 0) << "bit6=1 xor bit0=1 -> GX0=0";
}

/// @brief DS80 selects the Profi hi-res raster (Screen::DetectModeProfi)
TEST_F(ProfiPortDecoder_Test, Ds80SelectsHiResMode)
{
    OutDFFD(0x80);
    EXPECT_EQ(_context->pScreen->GetVideoMode(), M_PROFIHR);
    OutDFFD(0x00);
    EXPECT_EQ(_context->pScreen->GetVideoMode(), M_PROFI);
}

/// @brief Automation surfaces report the mode name and geometry (state/screen/mode, ROM page names)
TEST_F(ProfiPortDecoder_Test, AutomationReportsProfiHiResNameAndGeometry)
{
    OutDFFD(0x80);
    Screen* screen = _context->pScreen;
    EXPECT_EQ(Screen::GetVideoModeName(screen->GetVideoMode()), "PROFIHR");
    const FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
    EXPECT_EQ(fb.width, 608u);
    EXPECT_EQ(fb.height, 288u);

    OutDFFD(0x00);
    EXPECT_EQ(Screen::GetVideoModeName(screen->GetVideoMode()), "PROFI");

    // ROM page names come from the single-source layout table
    ROM rom(_context);
    EXPECT_EQ(rom.GetROMPageRole(0), "SYS/Menu ROM");
    EXPECT_EQ(rom.GetROMPageRole(2), "128K Editor + STS Monitor ROM");
}

/// @brief The guest switches DS80 repeatedly (the BIOS does it during boot), which reallocates the framebuffer
///        between 352x288 and 608x288. Consumers (Qt widgets) rely on: the descriptor always describes a valid
///        buffer of exactly width*height*4 bytes, and CopyPresentedFramebuffer rejects a destination of the old
///        size instead of copying into it. A null or stale buffer reaching the GPU upload crashed unreal-qt.
TEST_F(ProfiPortDecoder_Test, FramebufferStaysValidAcrossHiResSwitches)
{
    Screen* screen = _context->pScreen;
    ASSERT_NE(screen, nullptr);

    std::vector<uint8_t> standardSized(352u * 288u * 4u);
    std::vector<uint8_t> hiResSized(608u * 288u * 4u);

    for (int i = 0; i < 6; i++)
    {
        const bool hiRes = (i % 2) == 0;
        OutDFFD(hiRes ? 0x80 : 0x00);

        FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
        ASSERT_NE(fb.memoryBuffer, nullptr) << "switch " << i;
        EXPECT_EQ(fb.width, hiRes ? 608u : 352u);
        EXPECT_EQ(fb.height, 288u);
        EXPECT_EQ(fb.memoryBufferSize, static_cast<size_t>(fb.width) * fb.height * 4u);

        // Switching to hi-res: a destination still sized for the old 352x288 frame is too small and must be
        // refused (the widget keeps its previous frame until it re-attaches). A larger stale destination is
        // accepted (a memory-safe copy; at worst one garbled frame until the re-attach). Exact size always works.
        std::vector<uint8_t>& right = hiRes ? hiResSized : standardSized;
        if (hiRes)
            EXPECT_FALSE(screen->CopyPresentedFramebuffer(standardSized.data(), standardSized.size())) << "switch " << i;
        EXPECT_TRUE(screen->CopyPresentedFramebuffer(right.data(), right.size())) << "switch " << i;
    }
}
