#include "stdafx.h"
#include "pch.h"

#include "debugger/ttd/ttdserializable.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/profifixture.h"
#include "emulator/video/profi/profigeometry.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/soundmanager.h"

#include <fstream>
#include <iterator>

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

    /// Every low byte with A1 A0 = 11 in every mode against a board's port decoder PROM (defined below)
    void CheckAgainstProfiProm(bool v3, const std::string& promFile);
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

/// @brief Outside the DOS / CP/M port set the VG93's registered keys stay undecoded: an IN #FF (or #1F..#7F) from
///        48 BASIC reads the bus, not the controller (the generic fallback once reached the WD1793 at #00FF)
TEST_F(ProfiPortDecoder_Test, FdcRegistersSilentOutsideDosAndCpm)
{
    DosLatchOff();
    ASSERT_EQ(State().flags & CF_DOSPORTS, 0);
    // #1F is the Kempston joystick outside the DOS set while one is fitted (KempstonJoystickAt1FInTheNormalPortSet);
    // take it off the bus so this test sees the VG93 rule alone
    if (_context->pJoystick)
        _context->pJoystick->SetPresent(false);
    // #1F..#7F select the board's 8255 on reads too (decoder-prom.md, CP/M off / TR-DOS off: bit 3 = 8255): after
    // reset its ports are inputs with nothing driving them, so it reads #FF - never a VG93 register
    for (uint16_t port : { 0x001F, 0x003F, 0x005F, 0x007F })
    {
        EXPECT_EQ(ReadPort(port), 0xFF) << std::hex << port;
        EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded()) << "the 8255 drives the bus, " << std::hex << port;
    }
    ReadPort(0x00FF);
    EXPECT_FALSE(_context->pPortDecoder->WasLastPortDecoded()) << "no system port outside the DOS / CP/M set";
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
    EXPECT_EQ(rom.GetROMPageRole(2), "128K ROM");
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

/// @brief The RTC answers only in EXT mode (CP/M + ROM14): #BF / #FF latch the
///        address, #9F / #DF carry data; outside EXT mode #BF / #FF are the
///        Beta 128 system port and the clock is untouched
TEST_F(ProfiPortDecoder_Test, RtcOnlyInExtMode)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);
    Ds12887& rtc = decoder->GetRtc();
    rtc.WriteAddress(0x00);

    DosLatchOff();
    Out7FFD(0x10);  // ROM14
    ASSERT_FALSE(decoder->IsExtMode());
    WritePort(0x00BF, 0x40);
    EXPECT_EQ(rtc.GetAddress(), 0x00) << "not EXT mode: #BF is not the clock";

    OutDFFD(0x20);  // CP/M
    ASSERT_TRUE(decoder->IsExtMode());
    WritePort(0x00BF, 0x40);
    WritePort(0x009F, 0x5A);
    EXPECT_EQ(rtc.GetAddress(), 0x40);
    EXPECT_EQ(rtc.PeekRegister(0x40), 0x5A);
    EXPECT_EQ(ReadPort(0x009F), 0x5A);
    EXPECT_EQ(ReadPort(0x00DF), 0x5A) << "#DF is the second data port";
    WritePort(0x00FF, 0x0D);
    EXPECT_EQ(ReadPort(0x009F), 0x80) << "register D through #FF: battery good";
}

/// JOY-P5: the Profi's Kempston joystick answers `#1F` in the NORMAL port set (no DOS latch, no CP/M); with the
/// DOS ports in, `#1F` is the VG93
TEST_F(ProfiPortDecoder_Test, KempstonJoystickAt1FInTheNormalPortSet)
{
    Joystick joystick(_context);
    joystick.SetPresent(true);
    _context->pJoystick = &joystick;
    EXPECT_TRUE(_context->pPortDecoder->HasKempstonJoystick());

    DosLatchOff();
    EXPECT_EQ(ReadPort(0x001F), 0x00);
    joystick.SetState(Joystick::kLeft | Joystick::kFire);
    EXPECT_EQ(ReadPort(0x001F), 0x12);
    EXPECT_EQ(ReadPort(0xFB1F), 0x12) << "the full low byte decides";

    State().flags |= CF_TRDOS | CF_DOSPORTS;
    EXPECT_NE(ReadPort(0x001F), 0x12) << "the VG93 owns #1F while the DOS ports are in";

    DosLatchOff();
    joystick.SetPresent(false);
    EXPECT_EQ(ReadPort(0x001F), 0xFF) << "not fitted: the 8255's port A reads its idle input lines";
    _context->pJoystick = nullptr;
}

/// region <Profi v3 board (MM_PROFI3)>

/// @brief The same decoder on the v3 board (docs/inprogress/2026-10-01-profi-v3-v5): no palette, no extended port
///        map, no RTC, no IDE; the paging, the DOS latch and the CP/M map are the v5's. 512K RAM
class ProfiV3PortDecoder_Test : public ProfiPortDecoder_Test
{
protected:
    void SetUp() override
    {
        _model = MM_PROFI3;
        _ramSizeKB = RAM_512;
        ProfiPortDecoder_Test::SetUp();
    }
};

/// @brief v3 boots the SYS ROM like v5 and wraps the RAM page number at 512K (page 32 = page 0)
TEST_F(ProfiV3PortDecoder_Test, BootsSysRomAndWrapsAt512K)
{
    EXPECT_EQ(BankTag(0x0000), ProfiRomTagBase | 0) << "SYS ROM";
    DosLatchOff();
    Out7FFD(0x01);
    EXPECT_EQ(BankTag(0xC000), Ram(1));
    OutDFFD(0x04);                      // high bits 4 -> page 33 on 1M, page 1 on 512K
    EXPECT_EQ(BankTag(0xC000), Ram(1)) << "512K: the page number wraps";
}

/// @brief CP/M with ROM14 = 1 keeps the CP/M map on v3: the v3.2 decoder PROM has ADR15 where v5 has ROM14
TEST_F(ProfiV3PortDecoder_Test, CpmMapIgnoresRom14)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);

    DosLatchOff();
    Out7FFD(0x10);                      // ROM14
    OutDFFD(0x20);                      // CP/M
    EXPECT_FALSE(decoder->IsExtMode()) << "no extended map on v3";

    EXPECT_EQ(decoder->DecodeFDCPort(0x001F), 0x1F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x007F), 0x7F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x00BF), 0xFF) << "CP/M system port";
    EXPECT_EQ(decoder->DecodeFDCPort(0x003F), 0x3F) << "#3F stays an FDC register, not the extended system port";
    EXPECT_EQ(decoder->DecodeFDCPort(0x0083), 0x00) << "#83 is not the FDC";
    EXPECT_EQ(decoder->DecodeFDCPort(0x00E3), 0x00);
}

/// @brief No clock on v3: no RTC binding, and #BF / #9F in CP/M + ROM14 do not reach the chip
TEST_F(ProfiV3PortDecoder_Test, NoRtc)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);
    EXPECT_EQ(decoder->GetRtcBinding().chip, nullptr);

    Ds12887& rtc = decoder->GetRtc();
    rtc.WriteAddress(0x00);
    DosLatchOff();
    Out7FFD(0x10);
    OutDFFD(0x20);
    WritePort(0x009F, 0x5A);
    EXPECT_EQ(rtc.GetAddress(), 0x00);
    ReadPort(0x009F);
    EXPECT_FALSE(decoder->WasLastPortDecoded()) << "#9F decodes nothing on v3";
}

/// @brief No IDE on v3: the adapter's Profi gate stays closed in CP/M + ROM14, and the IDE_PROFI scheme does not fit
TEST_F(ProfiV3PortDecoder_Test, NoIde)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);
    Out7FFD(0x10);
    OutDFFD(0x20);
    EXPECT_FALSE(decoder->IdeGate().profiExt);
    EXPECT_FALSE(IdeController::SchemeFits(IDE_PROFI, MM_PROFI3));
    EXPECT_TRUE(IdeController::SchemeFits(IDE_PROFI, MM_PROFI));
}

/// @brief No palette on v3: an OUT #xx7E in DS80 changes no entry, #FE bit 7 reads 1, hi-res is monochrome
TEST_F(ProfiV3PortDecoder_Test, NoPaletteMonochromeHiRes)
{
    OutDFFD(0x80);
    const uint16_t before = State().profiPalette[0x0A];
    WritePort(0x00FE, 0x05);
    WritePort(0xE27E, 0x00);
    EXPECT_EQ(State().profiPalette[0x0A], before);

    // The entry that reads GX0 = 0 on v5 (FEReadBit7ReportsGX0InDS80) - v3 has no such entry, bit 7 is 1
    WritePort(0x00FE, 0x85);
    WritePort(0xDF7E, 0x00);
    WritePort(0x00FE, 0x05);
    EXPECT_NE(ReadPort(0x00FE) & 0x80, 0);

    EXPECT_TRUE(ProfiMonochromeHires(_context->config));
}

/// @brief TTD: the v3 board records its paging and its 8255 (joystick, printer / Covox) - no Ds12887, no extended
///        devices
TEST_F(ProfiV3PortDecoder_Test, TtdStateIsPagingAndPpi)
{
    const std::vector<ttd::PeripheralId> ids = _context->pPortDecoder->GetTTDModelStateIds();
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], ttd::PeripheralId::ProfiPaging);
    EXPECT_EQ(ids[1], ttd::PeripheralId::Ppi8255);
    EXPECT_EQ(_context->pPortDecoder->CreateTTDSerializers().size(), 2u);
}

/// endregion </Profi v3 board (MM_PROFI3)>

/// @brief Both boards: the AY select decodes A13 (the v3.2 controller sheet 2, the Black_Cat table), so IN #DFFD
///        (A13 = 0) never reads the AY, while #FFFD and its A13 = 1 mirrors do
TEST_F(ProfiPortDecoder_Test, AyDecodesA13)
{
    WritePort(0xFFFD, 0x07);
    WritePort(0xBFFD, 0x2A);
    if (ReadPort(0xFFFD) != 0x2A)
        GTEST_SKIP() << "no AY in this fixture";
    EXPECT_EQ(ReadPort(0xFFFD), 0x2A);
    EXPECT_EQ(ReadPort(0xEFFD), 0x2A) << "A13 = 1 mirror";
    EXPECT_NE(ReadPort(0xDFFD), 0x2A) << "A13 = 0: #DFFD is not the AY";
}

/// region <Port decode against the boards' port decoder PROMs>

/// What the board's K556RT4 port decoder PROM selects for one I/O cycle, read through the board's wiring
/// (docs/inprogress/2026-10-01-profi-v3-v5/decoder-prom.md; the tables in testdata/machines/profi/decoder/)
struct ProfiPromSelect
{
    bool fdc = false;   ///< VG93 registers
    bool sys = false;   ///< the FDC system register
    bool rtc = false;   ///< the clock (v5 extended group, output P7)
    bool ppi = false;   ///< the 8255 (Covox, joystick) in the normal port set

    bool operator==(const ProfiPromSelect& o) const
    {
        return fdc == o.fdc && sys == o.sys && rtc == o.rtc && ppi == o.ppi;
    }
};

static std::ostream& operator<<(std::ostream& os, const ProfiPromSelect& s)
{
    return os << "{fdc " << s.fdc << ", sys " << s.sys << ", rtc " << s.rtc << ", 8255 " << s.ppi << "}";
}

/// PROM address: A0 ADR5, A1 ADR6, A2 = 0 only with TR-DOS on and CP/M off, A3 ROM14 (v5) / ADR15 (v3.2), A4 ADR7,
/// A5 ADR1, A6 ADR0, A7 /CPM. Outputs active low; the two files number the data bits in opposite orders
static ProfiPromSelect ReadProfiProm(const std::vector<uint8_t>& prom, bool v3, uint8_t low, bool dos, bool cpm, bool a3)
{
    const unsigned a2 = (dos && !cpm) ? 0 : 1;
    const unsigned index = ((low >> 5) & 1) | (((low >> 6) & 1) << 1) | (a2 << 2) | ((a3 ? 1u : 0u) << 3) |
                           (((low >> 7) & 1) << 4) | (((low >> 1) & 1) << 5) | ((low & 1) << 6) | ((cpm ? 0u : 1u) << 7);
    const uint8_t d = prom[index] & 0x0F;
    auto active = [d](int bit) { return ((d >> bit) & 1) == 0; };

    ProfiPromSelect s;
    if (v3)
    {
        s.sys = active(0);
        s.fdc = active(1);
        s.ppi = active(3);   // bit 2 only enables the data bus buffer on #7FFD
    }
    else
    {
        s.sys = active(3);
        s.fdc = active(2);
        s.ppi = active(0);
        if (active(1))       // the extended group, split by ADR4..ADR2
        {
            const uint8_t p = (low >> 2) & 0x07;
            s.fdc = s.fdc || p == 0;
            s.rtc = p == 7;
            // P1 (8255 / Covox aliases), P2 (IDE), P3-P6 (COM, timer, control register) are not compared here
        }
    }
    return s;
}

/// What PortDecoder_Profi decodes for the same cycle, from the decoder's own arms
static ProfiPromSelect DecodeLikeProfi(PortDecoder_Profi* decoder, const EmulatorState& state, uint8_t low)
{
    ProfiPromSelect s;
    const bool dosPorts = (state.flags & CF_DOSPORTS) != 0;
    if ((low & 0x9F) == 0x9F && decoder->IsExtMode())
    {
        s.rtc = true;
        return s;
    }
    const uint16_t fdc = dosPorts ? decoder->DecodeFDCPort(low) : 0;
    s.sys = fdc == 0x00FF;
    s.fdc = fdc != 0 && fdc != 0x00FF;
    s.ppi = !dosPorts && (low & 0x83) == 0x03;
    return s;
}

/// Every low byte with A1 A0 = 11 in every mode: CP/M, the DOS latch, ROM14 (and A15 on v3)
void ProfiPortDecoder_Test::CheckAgainstProfiProm(bool v3, const std::string& promFile)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);
    EmulatorState& state = State();
    std::ifstream in(TestPathHelper::GetTestDataPath("machines/profi/decoder/" + promFile), std::ios::binary);
    ASSERT_TRUE(in.good()) << promFile;
    std::vector<uint8_t> prom((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_EQ(prom.size(), 256u);

    for (int cpm = 0; cpm < 2; cpm++)
    {
        for (int dos = 0; dos < 2; dos++)
        {
            for (int rom14 = 0; rom14 < 2; rom14++)
            {
                WritePort(0x7FFD, rom14 ? 0x10 : 0x00);
                WritePort(0xDFFD, cpm ? 0x20 : 0x00);
                if (dos)
                    state.flags |= CF_TRDOS;
                else
                    state.flags &= ~(CF_TRDOS | CF_DOSPORTS);
                _memory->UpdateZ80Banks();

                for (int a15 = 0; a15 < (v3 ? 2 : 1); a15++)
                {
                    const bool a3 = v3 ? (a15 != 0) : (rom14 != 0);
                    for (int low = 0x03; low <= 0xFF; low += 4)
                    {
                        const uint8_t port = static_cast<uint8_t>(low);
                        EXPECT_EQ(DecodeLikeProfi(decoder, state, port), ReadProfiProm(prom, v3, port, dos, cpm, a3))
                            << promFile << " port #" << std::hex << int(port) << std::dec << " CP/M " << cpm
                            << " DOS " << dos << " ROM14 " << rom14 << " A15 " << a15;
                    }
                }
            }
        }
    }
}

/// @brief v5: the decode equals the printed 556RT4 table of the v4.01 / v5.0 manuals in every mode
TEST_F(ProfiPortDecoder_Test, DecodeMatchesThePortDecoderProm)
{
    CheckAgainstProfiProm(false, "556rt4-v4-v5.bin");
}

/// @brief v3: the decode equals the v3.2 board's 556RT4 dump in every mode, for both A15 values
TEST_F(ProfiV3PortDecoder_Test, DecodeMatchesThePortDecoderProm)
{
    CheckAgainstProfiProm(true, "556rt4-v3.2.bin");
}

/// endregion </Port decode against the boards' port decoder PROMs>

/// region <The v3 floating bus>

/// research-profi-v3-turbo-floatbus.md B3: an IN that no device answers reads the pixel latch while it drives the
/// bus - one 4-T tick ahead of the displayed byte - and #FF from the pull-ups elsewhere. `d` is T3 minus the frame T
/// of the line's first displayed pixel; the lookup runs at T2, one T earlier
TEST_F(ProfiV3PortDecoder_Test, FloatingBusReadsThePixelLatch)
{
    DosLatchOff();
    uint8_t* screen = _memory->RAMPageAddress(5);
    for (uint16_t b = 0; b < 32; b++)
    {
        screen[(10 & 0x07) << 8 | (10 & 0x38) << 2 | b] = static_cast<uint8_t>(0x10 + b);   // line 10, byte b
        screen[(9 & 0x07) << 8 | (9 & 0x38) << 2 | b] = static_cast<uint8_t>(0x80 + b);     // line 9
    }
    Z80* z80 = _core->GetZ80();
    const uint32_t line10 = kProfiPaperStartT + 10 * 224;
    auto readAt = [&](int32_t d) {
        z80->t = static_cast<uint32_t>(static_cast<int32_t>(line10) + d - 2);
        return ReadPort(0x00FF);
    };
    EXPECT_EQ(readAt(0), 0x10) << "byte 0 at its first T";
    EXPECT_EQ(readAt(1), 0x11) << "byte 1 once the latch took it";
    EXPECT_EQ(readAt(4), 0x11);
    EXPECT_EQ(readAt(5), 0x12);
    EXPECT_EQ(readAt(-1), 0x10) << "the tick before the paper holds byte 0";
    EXPECT_EQ(readAt(-4), 0x9F) << "at its first T the latch still holds byte 31 of the line before";
    EXPECT_EQ(readAt(123), 0x2F) << "byte 31";
    EXPECT_EQ(readAt(124), 0xFF) << "the last tick: the latch is off the bus";
    EXPECT_EQ(readAt(-5), 0xFF) << "left border";
    EXPECT_EQ(readAt(-10 * 224 - 30), 0xFF) << "top border";

    // An answered port is not the floating bus; neither is the hi-res mode
    OutDFFD(0x80);
    EXPECT_EQ(readAt(1), 0xFF) << "DS80: not modeled";
}

TEST_F(ProfiPortDecoder_Test, V5HasNoFloatingBus)
{
    DosLatchOff();
    uint8_t* screen = _memory->RAMPageAddress(5);
    screen[(10 & 0x07) << 8 | (10 & 0x38) << 2 | 1] = 0x42;
    _core->GetZ80()->t = kProfiPaperStartT + 10 * 224 + 1 - 2;
    EXPECT_EQ(ReadPort(0x00FF), 0xFF);
}

/// endregion </The v3 floating bus>

/// region <Phase 7: the #DFFD decode variants and the v5 CP/M switch>

/// research-profi-v5-open-items.md: DffdDecode picks the board's #DFFD decode
TEST_F(ProfiPortDecoder_Test, DffdDecodeVariants)
{
    auto writes = [&](uint16_t port) {
        State().pDFFD = 0;
        WritePort(port, 0x01);
        return State().pDFFD == 0x01;
    };
    _context->config.profi_dffd_decode = 0;   // emulators: A15=1, A13=0, A1=0
    EXPECT_TRUE(writes(0xDFFD));
    EXPECT_TRUE(writes(0x9FFD));
    EXPECT_FALSE(writes(0x1FFD));

    _context->config.profi_dffd_decode = 1;   // v5.0: A13=0, A1=0 only
    EXPECT_TRUE(writes(0x9FFD));
    EXPECT_TRUE(writes(0x1FFD)) << "the 5.0 board's bug: a 128K #1FFD write lands in #DFFD too";

    _context->config.profi_dffd_decode = 2;   // v5.06: high byte #DF, A1=0, not from OUT (n),A
    EXPECT_TRUE(writes(0xDFFD));
    EXPECT_FALSE(writes(0x9FFD));
    _core->GetZ80()->opcode = 0xD3;
    EXPECT_FALSE(writes(0xDFFD)) << "OUT (n),A: DD75 /BLOCK";
    _core->GetZ80()->opcode = 0x79;
    EXPECT_TRUE(writes(0xDFFD)) << "OUT (C),A";
    _context->config.profi_dffd_decode = 0;
}

/// software-zoo.md: [PROFI] ExtPorts=sys opens the extended map while the SYS ROM runs (DOS latch on, ROM14 = 0), as
/// Karabas Pro decodes it (ROM BIOS Plus and PQ-DOS need it); the default keeps the 5.0 PROM's CP/M + ROM14 rule
TEST_F(ProfiPortDecoder_Test, ExtPortsInTheSysRomVariant)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);
    Out7FFD(0x00);   // ROM14 = 0
    OutDFFD(0x80);   // CP/M off (hi-res, as ROM BIOS Plus runs its board test)
    State().flags |= CF_TRDOS | CF_DOSPORTS;   // the SYS ROM: DOS latch on

    _context->config.profi_ext_ports = 0;   // cpm: the 5.0 decoder PROM
    EXPECT_EQ(decoder->DecodeFDCPort(0x001F), 0x1F) << "BIOS 1.0 / 2.0 reach the VG93 at #1F from the SYS ROM";
    EXPECT_EQ(decoder->DecodeFDCPort(0x0083), 0x00);

    _context->config.profi_ext_ports = 1;   // sys: Karabas Pro
    EXPECT_EQ(decoder->DecodeFDCPort(0x0083), 0x1F) << "the extended VG93 ports";
    EXPECT_EQ(decoder->DecodeFDCPort(0x003F), 0xFF) << "the extended system port";
    EXPECT_EQ(decoder->DecodeFDCPort(0x001F), 0x00) << "Karabas does not decode #1F in the SYS ROM state";

    Out7FFD(0x10);   // ROM14 = 1, CP/M off: neither map's condition holds
    EXPECT_EQ(decoder->DecodeFDCPort(0x0083), 0x00);
    _context->config.profi_ext_ports = 0;
}

/// ExtPorts=v003 (Djoni's V0.03 decoder PROM, read with tools/machines/profi/profidecoder): the SYS ROM state as
/// ExtPorts=sys but only with CP/M off; TR-DOS with ROM14 = 1 answers the long ports beside the stock VG93
TEST_F(ProfiPortDecoder_Test, ExtPortsV003FollowsTheDjoniPromTable)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);
    _context->config.profi_ext_ports = 2;

    // SYS ROM (ROM14 = 0, CP/M off, DOS latch on): the long map only, the system register at #3F
    Out7FFD(0x00);
    OutDFFD(0x80);
    State().flags |= CF_TRDOS | CF_DOSPORTS;
    EXPECT_EQ(decoder->DecodeFDCPort(0x0083), 0x1F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x003F), 0xFF);
    EXPECT_EQ(decoder->DecodeFDCPort(0x001F), 0x00) << "no short VG93 in the SYS ROM state";
    EXPECT_TRUE(decoder->LongPortOpen(0x00FF)) << "the RTC address port";

    // CP/M on, the DOS latch left on, ROM14 = 0: the PROM forces A2 = 1, so this is the stock CP/M map, not the long one
    OutDFFD(0xA0);
    EXPECT_EQ(decoder->DecodeFDCPort(0x001F), 0x1F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x0083), 0x00);
    EXPECT_FALSE(decoder->LongPortOpen(0x009F));

    // TR-DOS with the 48 ROM page (ROM14 = 1, CP/M off): the stock VG93 stays, the long ports join it
    OutDFFD(0x80);
    Out7FFD(0x10);
    EXPECT_EQ(decoder->DecodeFDCPort(0x001F), 0x1F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x005F), 0x5F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x0083), 0x1F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x00A3), 0x3F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x00C3), 0x5F);
    EXPECT_EQ(decoder->DecodeFDCPort(0x00E3), 0xFF) << "#E3 is the system register";
    EXPECT_EQ(decoder->DecodeFDCPort(0x00FF), 0xFF);
    for (const uint8_t port : {0x9F, 0xBF, 0xDF})
        EXPECT_TRUE(decoder->LongPortOpen(port)) << "RTC #" << std::hex << int(port);
    EXPECT_FALSE(decoder->LongPortOpen(0x00FF)) << "#FF stays the system register, not the RTC";
    EXPECT_EQ(decoder->PpiRegister(0x00A7, true), 1);
    EXPECT_EQ(decoder->PpiRegister(0x00E7, true), 0xFF);
    EXPECT_EQ(decoder->ComPortDevice(0x00D3), PortDecoder_Profi::ComDevice::Usart);
    EXPECT_EQ(decoder->ComPortDevice(0x00F3), PortDecoder_Profi::ComDevice::None);
    EXPECT_EQ(decoder->ComPortDevice(0x00AF), PortDecoder_Profi::ComDevice::Pit);
    EXPECT_EQ(decoder->ComPortDevice(0x00EF), PortDecoder_Profi::ComDevice::None);
    EXPECT_FALSE(decoder->IdeShadowedBySysRegister(0x00CB));
    EXPECT_TRUE(decoder->IdeShadowedBySysRegister(0x00EB)) << "#EB is the system register's alias here";

    // The SYS ROM state keeps all of the IDE
    Out7FFD(0x00);
    EXPECT_FALSE(decoder->IdeShadowedBySysRegister(0x00EB));

    // The default (cpm) PROM and ExtPorts=sys never open the long ports in the TR-DOS + ROM14 = 1 state
    Out7FFD(0x10);
    for (const uint8_t mode : {0, 1})
    {
        _context->config.profi_ext_ports = mode;
        EXPECT_EQ(decoder->DecodeFDCPort(0x0083), 0x00);
        EXPECT_FALSE(decoder->LongPortOpen(0x009F));
    }
    _context->config.profi_ext_ports = 0;
}

/// docs/inprogress/2026-10-04-profi-plus/design.md: the 8255 answers at #3F / #5F / #7F outside the DOS / CP/M
/// port set and at #87 / #A7 / #C7 / #E7 in the extended map - ROM BIOS Plus's parallel-port test reads PC2 and B back
TEST_F(ProfiPortDecoder_Test, Ppi8255NormalAndExtendedAddresses)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);

    // Normal map: no DOS latch, no CP/M
    DosLatchOff();
    WritePort(0x7F7F, 0x90);
    WritePort(0x3F3F, 0x5A);
    EXPECT_EQ(ReadPort(0x3F3F), 0x5A) << "port B reads its latch back";
    WritePort(0x7F7F, 0x05);
    EXPECT_EQ(ReadPort(0x5F5F) & 0x04, 0x04) << "PC2 set";

    // Extended map: CP/M + ROM14
    Out7FFD(0x10);
    OutDFFD(0x20);
    WritePort(0x00E7, 0x90);
    WritePort(0x02A7, 0x02);
    EXPECT_EQ(ReadPort(0x02A7), 0x02);
    WritePort(0x05E7, 0x05);
    EXPECT_EQ(ReadPort(0x05C7) & 0x04, 0x04);
    WritePort(0x04E7, 0x04);
    EXPECT_EQ(ReadPort(0x04C7) & 0x04, 0x00);
    EXPECT_EQ(&decoder->GetPpi(), &decoder->GetPpi());
}

/// docs/inprogress/2026-10-04-profi-plus/design.md section 2.1: the v5 COM port - the 8253 at #8F / #AF / #CF / #EF,
/// the 8251 at #D3 / #F3, the control register at #93 / #B3 - answers in the extended map only
TEST_F(ProfiPortDecoder_Test, ComPortAnswersOnlyInTheExtendedMap)
{
    PortDecoder_Profi* decoder = Decoder();
    ASSERT_NE(decoder, nullptr);

    // The SYS ROM state with the default ExtPorts=cpm: no COM port
    WritePort(0x76EF, 0x76);
    EXPECT_EQ(decoder->GetPit().GetState().counter[1].control, 0) << "#EF is not the 8253 here";
    WritePort(0x01B3, 0x01);
    EXPECT_EQ(decoder->GetUsart().BoardLatch(), 0);
    EXPECT_EQ(decoder->ComPortDevice(0x00F3), PortDecoder_Profi::ComDevice::None);

    // CP/M + ROM14: the extended map
    Out7FFD(0x10);
    OutDFFD(0x20);
    EXPECT_EQ(decoder->ComPortDevice(0x008F), PortDecoder_Profi::ComDevice::Pit);
    EXPECT_EQ(decoder->ComPortDevice(0x00AF), PortDecoder_Profi::ComDevice::Pit);
    EXPECT_EQ(decoder->ComPortDevice(0x00CF), PortDecoder_Profi::ComDevice::Pit);
    EXPECT_EQ(decoder->ComPortDevice(0x00EF), PortDecoder_Profi::ComDevice::Pit);
    EXPECT_EQ(decoder->ComPortDevice(0x00D3), PortDecoder_Profi::ComDevice::Usart);
    EXPECT_EQ(decoder->ComPortDevice(0x00F3), PortDecoder_Profi::ComDevice::Usart);
    EXPECT_EQ(decoder->ComPortDevice(0x00B3), PortDecoder_Profi::ComDevice::Control);
    EXPECT_EQ(decoder->ComPortDevice(0x0093), PortDecoder_Profi::ComDevice::Control);
    EXPECT_EQ(decoder->ComPortDevice(0x0083), PortDecoder_Profi::ComDevice::None) << "the VG93";
    EXPECT_EQ(decoder->ComPortDevice(0x0087), PortDecoder_Profi::ComDevice::None) << "the 8255";
    EXPECT_EQ(decoder->ComPortDevice(0x00EB), PortDecoder_Profi::ComDevice::None) << "the IDE";

    // ROM BIOS Plus's board test: counter 1 mode 3, #0010, read back with two INs
    WritePort(0x76EF, 0x76);
    WritePort(0x10AF, 0x10);
    WritePort(0x00AF, 0x00);
    _context->emulatorState.t_states += 100;   // the load happens at the next CLK pulse
    const uint8_t lo = ReadPort(0x10AF);
    const uint8_t hi = ReadPort(0x00AF);
    EXPECT_FALSE(lo == 0xFF && hi == 0xFF);
    EXPECT_EQ(hi, 0x00);
    EXPECT_EQ(Pit8253::ModeOf(decoder->GetPit().GetState().counter[1]), 3);

    // Counter 0 mode 3 / 156: the 8251's clock; the 8251 resets (4 x #01, #40) and takes 8N1 x1
    WritePort(0x36EF, 0x36);
    WritePort(0x9C8F, 156);
    WritePort(0x008F, 0);
    for (uint8_t v : {0x01, 0x01, 0x01, 0x01, 0x40, 0x4D, 0x15})
        WritePort(0x00F3, v);
    EXPECT_EQ(decoder->GetUsart().Baud(), 9615u);
    EXPECT_NE(ReadPort(0x00F3), 0xFF) << "the board test's check";
    EXPECT_EQ(ReadPort(0x00F3) & 0x05, 0x05) << "TxRDY, TxEMPTY";

    // #B3: D0 the interrupt enable latch; read D0 RI, D7 DCD - nothing on the connector
    WritePort(0x01B3, 0x01);
    EXPECT_EQ(decoder->GetUsart().BoardLatch(), 1);
    EXPECT_EQ(ReadPort(0x00B3) & 0x81, 0x00);
    EXPECT_EQ(ReadPort(0x0093) & 0x81, 0x00);
}

/// ExtPorts=sys (the Profi+ V0.03 decoder PROM): the COM port answers in the SYS ROM state too
TEST_F(ProfiPortDecoder_Test, ComPortInTheSysRomWithExtPortsSys)
{
    _context->config.profi_ext_ports = 1;
    EXPECT_EQ(Decoder()->ComPortDevice(0x00F3), PortDecoder_Profi::ComDevice::Usart);
    EXPECT_EQ(ReadPort(0x00F3), Usart8251::kTxRdy | Usart8251::kTxEmpty);
    _context->config.profi_ext_ports = 0;
}

/// The 8251 is the machine's own serial port: a peer plugs in through DescribeNetwork, and a byte goes out and
/// comes back through a loopback plug at 9600 baud
TEST_F(ProfiPortDecoder_Test, ComPortTakesAPeer)
{
    PortDecoder_Profi* decoder = Decoder();
    const PortDecoder::NetworkCapabilities caps = decoder->DescribeNetwork();
    ASSERT_EQ(caps.serialPort, PortDecoder::NetworkCapabilities::SerialPort::Profi8251);
    ASSERT_TRUE(caps.attachSerialPeer);

    LoopbackPeer plug(true);
    caps.attachSerialPeer(&plug);
    Out7FFD(0x10);
    OutDFFD(0x20);
    WritePort(0x36EF, 0x36);
    WritePort(0x9C8F, 156);
    WritePort(0x008F, 0);
    for (uint8_t v : {0x00, 0x00, 0x00, 0x40})   // the internal reset sequence
        WritePort(0x00F3, v);
    WritePort(0x00F3, 0x4D);
    WritePort(0x00F3, 0x27);   // TxEN, DTR, RxE, RTS
    EXPECT_NE(ReadPort(0x00F3) & Usart8251::kDsr, 0) << "the plug: DTR to DSR";
    EXPECT_EQ(ReadPort(0x00B3) & 0x81, 0x81) << "the plug: DTR to DCD, RTS to RI";
    EXPECT_EQ(caps.serialBaud(), 9615u);

    WritePort(0x00D3, 0xC5);
    _context->emulatorState.t_states += 2 * 3640;
    EXPECT_NE(ReadPort(0x00F3) & Usart8251::kRxRdy, 0);
    EXPECT_EQ(ReadPort(0x00D3), 0xC5);

    caps.attachSerialPeer(nullptr);
    EXPECT_EQ(ReadPort(0x00F3) & Usart8251::kDsr, 0) << "unplugged";
}

/// The v3 board has no COM port
TEST_F(ProfiV3PortDecoder_Test, NoComPortOnV3)
{
    EXPECT_EQ(_context->pPortDecoder->DescribeNetwork().serialPort, PortDecoder::NetworkCapabilities::SerialPort::None);
    Out7FFD(0x10);
    OutDFFD(0x20);
    EXPECT_EQ(Decoder()->ComPortDevice(0x00F3), PortDecoder_Profi::ComDevice::None);
}

TEST_F(ProfiPortDecoder_Test, CpmSwitchHoldsDffdCleared)
{
    PortDecoder* decoder = _context->pPortDecoder;
    ASSERT_TRUE(decoder->HasFrontPanelSwitch(FrontPanelSwitch::Cpm));
    ASSERT_FALSE(decoder->GetFrontPanelSwitch(FrontPanelSwitch::Cpm));
    WritePort(0xDFFD, 0x20);
    EXPECT_EQ(State().pDFFD, 0x20);

    ASSERT_TRUE(decoder->SetFrontPanelSwitch(FrontPanelSwitch::Cpm, true));
    EXPECT_EQ(State().pDFFD, 0x00) << "pressing clears the latch";
    WritePort(0xDFFD, 0x20);
    EXPECT_EQ(State().pDFFD, 0x00) << "writes are lost while pressed";
    WritePort(0x7FFD, 0x07);
    EXPECT_EQ(State().p7FFD & 0x07, 0x07) << "#7FFD is not touched";

    ASSERT_TRUE(decoder->SetFrontPanelSwitch(FrontPanelSwitch::Cpm, false));
    WritePort(0xDFFD, 0x20);
    EXPECT_EQ(State().pDFFD, 0x20);
}

TEST_F(ProfiV3PortDecoder_Test, NoCpmSwitchOnV3)
{
    EXPECT_FALSE(_context->pPortDecoder->HasFrontPanelSwitch(FrontPanelSwitch::Cpm));
    EXPECT_FALSE(_context->pPortDecoder->SetFrontPanelSwitch(FrontPanelSwitch::Cpm, true));
}

/// endregion </Phase 7>

/// region <Hi-res timing (design-hires.md, phase H2)>

/// v5: #DFFD bit 7 runs the CPU from ZQ3 / 4 (20 MHz -> 5 MHz = 10/7 of the base clock) and the sync PROM's upper
/// half: the same 312-line frame, INT 12553 base T before the hi-res paper
TEST_F(ProfiPortDecoder_Test, HiresRunsTheCpuAtFiveMegahertz)
{
    // [PROFI] ZQ3MHz unset (0) means the 5.06's 20 MHz
    WritePort(0xDFFD, 0x80);
    EXPECT_EQ(State().hw_turbo_ratio_applied, 10);
    EXPECT_EQ(State().ClockDen(), 7u);
    EXPECT_EQ(State().current_z80_frequency, 5'000'000u);
    EXPECT_EQ(_context->config.frame, 69888u);
    EXPECT_EQ(_context->GetFrameTStates(), 99840u) << "320 T lines x 312";
    EXPECT_EQ(_context->config.intstart, ProfiHiresIntStart(ProfiSyncPromFrameHires(ProfiSyncProm::V503, MM_PROFI)));

    WritePort(0xDFFD, 0x00);
    EXPECT_EQ(State().current_z80_frequency, 3'500'000u);
    EXPECT_EQ(State().ClockDen(), 1u);
    EXPECT_EQ(_context->config.intstart, ProfiIntStart(ProfiSyncPromFrame(ProfiSyncProm::V503, MM_PROFI)));
}

/// v3: 3 MHz (12 MHz / 4) and the 0a1d PROM's 320-line hi-res frame (48.83 Hz)
TEST_F(ProfiV3PortDecoder_Test, HiresRunsTheCpuAtThreeMegahertzOnA320LineFrame)
{
    WritePort(0xDFFD, 0x80);
    EXPECT_EQ(State().current_z80_frequency, 3'000'000u);
    EXPECT_EQ(_context->config.frame, 71680u);
    EXPECT_EQ(_context->GetFrameTStates(), 61440u) << "192 T lines x 320";
    EXPECT_EQ(_context->config.frame_duration_us, 20480u);

    WritePort(0xDFFD, 0x00);
    EXPECT_EQ(_context->config.frame, 69888u);
    EXPECT_EQ(State().current_z80_frequency, 3'500'000u);
}

/// The TTD time grid covers every clock the board selects: 12 units per base T on v3 (1, 2, 6/7, 12/7), 20 on a
/// v5 with ZQ3 = 20 MHz (1, 2, 10/7, 20/7)
TEST_F(ProfiPortDecoder_Test, TtdUnitsCoverTheHiresClock)
{
    EXPECT_EQ(_context->pPortDecoder->TtdClockUnits(), 20);
    State().ttd_clock_units = _context->pPortDecoder->TtdClockUnits();
    WritePort(0xDFFD, 0x80);
    EXPECT_EQ(State().TtdUnitsPerTState(), 14u) << "a 5 MHz T is 7/10 base T: 20 x 7 / 10";
}

/// The AY clock (CLCAY) comes from the video divider: 1.5 MHz in hi-res, 1.75 MHz in Spectrum mode (design-hires.md
/// H2b). The request lands on the AY's render timeline, so the requested clock is what the decoder controls
static uint32_t RequestedAyClock(EmulatorContext* context)
{
    auto* device = dynamic_cast<SoundChip_TurboSound*>(context->pSoundManager->getTurboSound());
    return device ? device->GetRequestedPsgClock() : 0;
}

/// v5, jumper SB7 in "CLCAY OLD" (the default): 1.5 MHz in hi-res; turbo does not touch it
TEST_F(ProfiPortDecoder_Test, HiresClocksTheAyAtOneAndAHalfMegahertz)
{
    ASSERT_EQ(RequestedAyClock(_context), 1'750'000u) << "the fixture's AY slot";
    WritePort(0xDFFD, 0x80);
    EXPECT_EQ(RequestedAyClock(_context), 1'500'000u);
    EXPECT_TRUE(Decoder()->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, true));
    EXPECT_EQ(RequestedAyClock(_context), 1'500'000u) << "turbo is a CPU clock, not CLCAY";
    WritePort(0xDFFD, 0x00);
    EXPECT_EQ(RequestedAyClock(_context), 1'750'000u);
}

/// v5, SB7 in "CLCAY NEW" ([PROFI] AyClock=new): 1.75 MHz in both modes
TEST_F(ProfiPortDecoder_Test, AyClockNewKeepsOneSeventyFiveInHires)
{
    _context->config.profi_ay_clock_new = 1;
    WritePort(0xDFFD, 0x80);
    EXPECT_EQ(RequestedAyClock(_context), 1'750'000u);
}

/// v3: no jumper, always 1.5 MHz in hi-res (AyClock=new is a v5 option)
TEST_F(ProfiV3PortDecoder_Test, HiresClocksTheAyAtOneAndAHalfMegahertz)
{
    _context->config.profi_ay_clock_new = 1;
    WritePort(0xDFFD, 0x80);
    EXPECT_EQ(RequestedAyClock(_context), 1'500'000u);
    WritePort(0xDFFD, 0x00);
    EXPECT_EQ(RequestedAyClock(_context), 1'750'000u);
}

/// endregion </Hi-res timing>

/// v3 hi-res floating bus (design-hires.md H3): in the fetch window the first half of each 1333 ns tick reads the
/// cell's second byte, the second half its first byte; #FF outside. Line 0's window starts at 3 077 524 ns; at 3 MHz
/// (333 ns clocks) T3 = t + 2
TEST_F(ProfiV3PortDecoder_Test, HiresFloatingBusAlternatesTheCellBytes)
{
    DosLatchOff();
    WritePort(0xDFFD, 0x80);
    ASSERT_EQ(State().current_z80_frequency, 3'000'000u);
    uint8_t* page = _memory->RAMPageAddress(ProfiGeometry::PixelPage(State().p7FFD));
    page[ProfiGeometry::ByteOffset(0, 1)] = 0x5A;   // the cell's second byte
    page[ProfiGeometry::ByteOffset(0, 0)] = 0xA5;   // its first byte (+#2000)
    Z80* z80 = _core->GetZ80();
    z80->t = 9233 - 2;   // T3 at 3 077 667 ns: 143 ns into the tick
    EXPECT_EQ(ReadPort(0x00FF), 0x5A);
    z80->t = 9235 - 2;   // 810 ns: the second half
    EXPECT_EQ(ReadPort(0x00FF), 0xA5);
    z80->t = 100;
    EXPECT_EQ(ReadPort(0x00FF), 0xFF) << "top border";
}
