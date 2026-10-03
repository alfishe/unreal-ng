// PortDecoder_Sprinter: the port table lookup, the start-up gate, the code
// semantics of the standard configuration and the cells (Sprinter test-plan
// §2.1 T-DCP, §2.2 T-MEM-1/2, §2.7 T-RTC; tdd-ports-memory §3-§4).

#include "sprinterfixture.h"

#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/io/storage/memorydisk.h"

class PortDecoderSprinter_Test : public SprinterFixture
{
};

/// region <T-DCP: the lookup>

// T-DCP-1, MAN p. 28: port #7785, write, DOS on, PN5 = 0, map 0 -> index #09D
TEST_F(PortDecoderSprinter_Test, LookupIndex_ManualWorkedExample)
{
    Pld().dos = 0;
    Pld().pn = 0;
    Pld().cnf = 0;
    EXPECT_EQ(_decoder->LookupIndex(0x7785, false), 0x009D);
    EXPECT_EQ(_decoder->LookupIndex(0x7785, true), 0x029D) << "reads add #200";
    Pld().dos = 1;
    EXPECT_EQ(_decoder->LookupIndex(0x7785, false), 0x049D) << "DOS off adds #400";
}

// T-DCP-2: every index bit alone; A3, A4, A8-A12 never take part
TEST_F(PortDecoderSprinter_Test, LookupIndex_EachSignalMovesOneBit)
{
    Pld().dos = 0;
    Pld().pn = 0;
    Pld().cnf = 0;
    const uint16_t base = _decoder->LookupIndex(0x0000, false);
    ASSERT_EQ(base, 0);

    const struct
    {
        uint16_t port;
        uint16_t bit;
    } addressBits[] = {
        {0x8000, 1u << 8}, {0x4000, 1u << 7}, {0x0040, 1u << 6}, {0x0020, 1u << 5}, {0x2000, 1u << 4},
        {0x0080, 1u << 3}, {0x0004, 1u << 2}, {0x0002, 1u << 1}, {0x0001, 1u << 0},
    };
    for (const auto& a : addressBits)
        EXPECT_EQ(_decoder->LookupIndex(a.port, false), a.bit) << std::hex << a.port;

    for (uint16_t ignored : {0x0008, 0x0010, 0x0100, 0x0200, 0x0400, 0x0800, 0x1000})
        EXPECT_EQ(_decoder->LookupIndex(ignored, false), 0) << std::hex << ignored;

    EXPECT_EQ(_decoder->LookupIndex(0x0000, true), 1u << 9);
    Pld().dos = 1;
    EXPECT_EQ(_decoder->LookupIndex(0x0000, false), 1u << 10);
    Pld().dos = 0;
    Pld().pn = 0x20;
    EXPECT_EQ(_decoder->LookupIndex(0x0000, false), 1u << 11);
    Pld().pn = 0;
    Pld().cnf = 0x08;
    EXPECT_EQ(_decoder->LookupIndex(0x0000, false), 1u << 12);
    Pld().cnf = 0x10;
    EXPECT_EQ(_decoder->LookupIndex(0x0000, false), 1u << 13);
}

// T-DCP-3 / T-DCP-7: the BIOS 3.04 table (hardware-reference §4.4, checked in S0)
TEST_F(PortDecoderSprinter_Test, Table304_StandardMapRows)
{
    if (!LoadTable304())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";

    Pld().cnf = 0;
    Pld().pn = 0;
    Pld().dos = 0;  // DOS on
    EXPECT_EQ(_decoder->LookupCode(0x21BC, false), 0x2B) << "IDE primary";
    EXPECT_EQ(_decoder->LookupCode(0x01BC, false), 0x2A) << "IDE secondary";
    EXPECT_EQ(_decoder->LookupCode(0x00BD, false), 0x16) << "720 KB";
    EXPECT_EQ(_decoder->LookupCode(0x20BD, false), 0x17) << "1.44 MB";
    EXPECT_EQ(_decoder->LookupCode(0x001F, true), 0x10) << "WD1793 status (DOS on)";
    EXPECT_EQ(_decoder->LookupCode(0x007F, false), 0x13) << "WD1793 data";
    EXPECT_EQ(_decoder->LookupCode(0x00FF, false), 0x14) << "Beta system register";

    Pld().dos = 1;  // DOS off
    EXPECT_EQ(_decoder->LookupCode(0x1FFD, false), 0xC0);
    EXPECT_EQ(_decoder->LookupCode(0x7FFD, false), 0xC1);
    EXPECT_EQ(_decoder->LookupCode(0x7FFD, true), 0xC1);
    EXPECT_EQ(_decoder->LookupCode(0x00FE, true), 0x40) << "keyboard";
    EXPECT_EQ(_decoder->LookupCode(0x00FE, false), 0xC2) << "border";
    EXPECT_EQ(_decoder->LookupCode(0xFFFD, true), 0x52);
    EXPECT_EQ(_decoder->LookupCode(0xFFFD, false), 0x90);
    EXPECT_EQ(_decoder->LookupCode(0xBFFD, false), 0x91);
    EXPECT_EQ(_decoder->LookupCode(0xDFBD, false), 0x1D) << "CMOS address";
    EXPECT_EQ(_decoder->LookupCode(0xBFBD, false), 0x1E) << "CMOS data write";
    EXPECT_EQ(_decoder->LookupCode(0xFFBD, true), 0x1C) << "CMOS data read";
    EXPECT_EQ(_decoder->LookupCode(0x0082, false), 0xE8);
    EXPECT_EQ(_decoder->LookupCode(0x00A2, false), 0xE9);
    EXPECT_EQ(_decoder->LookupCode(0x00C2, false), 0xEA);
    EXPECT_EQ(_decoder->LookupCode(0x00E2, false), 0xF0);
    EXPECT_EQ(_decoder->LookupCode(0x00E2, true), 0xF0);
    EXPECT_EQ(_decoder->LookupCode(0x0089, false), 0xC4) << "PORT_Y";
    EXPECT_EQ(_decoder->LookupCode(0x00C9, false), 0xC5) << "RGMOD";
    EXPECT_EQ(_decoder->LookupCode(0x007C, false), 0xC6) << "SYS";
    EXPECT_EQ(_decoder->LookupCode(0x0024, false), 0xC6) << "CNF";
    EXPECT_EQ(_decoder->LookupCode(0x41BD, false), 0x2C) << "320 lines";
    EXPECT_EQ(_decoder->LookupCode(0x61BD, false), 0x2D) << "312 lines";
    EXPECT_EQ(_decoder->LookupCode(0x40BC, false), 0x2E) << "PLD reload";
    EXPECT_EQ(_decoder->LookupCode(0x204E, false), 0xC3) << "ALL_MODE";
    EXPECT_EQ(_decoder->LookupCode(0x204E, true), 0x00) << "3.04: no ALL_MODE read-back";
    EXPECT_EQ(_decoder->LookupCode(0x00FC, false), 0xC7) << "3.04: SCALE on #FC";
    EXPECT_EQ(_decoder->LookupCode(0x001F, false), 0x88) << "3.04: Covox on #1F with DOS off";
    EXPECT_EQ(_decoder->LookupCode(0x0050, true), 0x20) << "IDE data";
    EXPECT_EQ(_decoder->LookupCode(0x4053, true), 0x27) << "IDE status";

    // The 48K lock: with PN5 = 1 and DOS off, #7FFD is hidden (dcp-table.py selftest)
    Pld().pn = 0x20;
    EXPECT_EQ(_decoder->LookupCode(0x7FFD, false), 0x00);
}

// T-DCP-4: port writes are ignored until the first port read opens the decoder
TEST_F(PortDecoderSprinter_Test, StartupGate_WritesIgnoredUntilFirstIn)
{
    ASSERT_EQ(Pld().configState, SprinterConfigState::Configured);
    ASSERT_EQ(Pld().starting, 1);
    SetCode(0x00A2, false, 0xE9);
    const uint8_t before = Pld().Cell(0xE9);

    Out(0x00A2, 0x33);
    EXPECT_EQ(Pld().Cell(0xE9), before) << "a write before the first IN must be ignored";
    EXPECT_EQ(_decoder->DcpOpenedFrame(), -1);

    OpenDcp();
    EXPECT_EQ(Pld().starting, 0);
    EXPECT_GE(_decoder->DcpOpenedFrame(), 0);
    Out(0x00A2, 0x33);
    EXPECT_EQ(Pld().Cell(0xE9), 0x33);
    EXPECT_EQ(Tag(0x4000), 0x33) << "window 1 follows cell #E9";
}

// Window 3 shows page #40 while starting, then the cell of Spectrum page 0
TEST_F(PortDecoderSprinter_Test, StartupGate_Window3IsPortTableUntilRemap)
{
    Ram(0x40, 0x10) = 0x77;
    EXPECT_EQ(Tag(0xC000), 0x77) << "page #40 in window 3 after the PLD reset";
    OpenDcp();
    EXPECT_EQ(Tag(0xC000), 0x77) << "MAME: the first IN alone does not remap";
    SetCode(0x00E2, false, 0xF0);
    Out(0x00E2, 0x12);
    EXPECT_EQ(Tag(0xC000), 0x12);
}

// T-DCP-5: the Z84C15's ports never reach the table on reads; the PLD sees their writes (MAME tap)
TEST_F(PortDecoderSprinter_Test, Z84C15Ports_ReadsBypassTheTable)
{
    OpenDcp();
    for (uint16_t low : {0x10, 0x13, 0x18, 0x1B, 0x1C, 0x1F, 0xEE, 0xEF, 0xF0, 0xF1, 0xF4})
    {
        for (uint16_t high : {0x00, 0x7F, 0xFF})
        {
            const uint16_t port = static_cast<uint16_t>((high << 8) | low);
            SetCode(port, true, 0xC2);  // would be the border cell
            Pld().Cell(0xC2) = 0xA5;
            EXPECT_NE(In(port), 0xA5) << std::hex << port << ": the table must not answer";
        }
    }
    // A table read would return the cell; the Z84C15 answers its own (SIO A control: Tx empty)
    SetCode(0x0019, true, 0xC2);
    EXPECT_EQ(In(0x0019) & 0x04, 0x04);
    EXPECT_NE(In(0x0019), 0xA5);

    // OUT to a Z84C15 address also runs the PLD write (MAME sprinter.cpp:1445-1458)
    SetCode(0x001C, false, 0xC4);
    Out(0x001C, 0x5B);
    EXPECT_EQ(_decoder->GetZ84().pio.GetPort(0).output, 0x5B);
    EXPECT_EQ(Pld().portY, 0x5B);
}

// T-DCP-6: IN #FB / #7B switch fast RAM into window 0 before the lookup
TEST_F(PortDecoderSprinter_Test, FastRamSwitch_FB7B)
{
    OpenDcp();
    _sprinterMemory->FastRam()[0x10] = 0x99;
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 8) << "BIOS ROM page 8 after the start";
    In(0x00FB);
    EXPECT_EQ(Pld().cacheOn, 1);
    EXPECT_EQ(Tag(0x0000), 0x99);
    EXPECT_EQ(_memory->GetMemoryBankMode(0), BANK_CACHE);
    Poke(0x0020, 0x42);
    EXPECT_EQ(_sprinterMemory->FastRam()[0x20], 0x42) << "fast RAM is writable";
    In(0x007B);
    EXPECT_EQ(Pld().cacheOn, 0);
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 8);
}

// T-DCP-8: a program edits a table byte; the next access uses the new code
TEST_F(PortDecoderSprinter_Test, TableEdit_TakesEffectAtTheNextAccess)
{
    OpenDcp();
    SetCode(0x1234, false, 0xC4);
    Out(0x1234, 0x11);
    EXPECT_EQ(Pld().portY, 0x11);
    SetCode(0x1234, false, 0xC5);
    Out(0x1234, 0x01);
    EXPECT_EQ(Pld().rgMod, 0x01);
    EXPECT_EQ(Pld().portY, 0x11);
}

/// endregion </T-DCP>

/// region <T-MEM-1 / T-MEM-2: cells>

// T-MEM-1: the CNF clean rules on #7FFD (MAME :835-842)
TEST_F(PortDecoderSprinter_Test, Port7FFD_CleanRules)
{
    OpenDcp();
    SetCodeAll(0x7FFD, false, 0xC1);
    SetCodeAll(0x003C, false, 0xC6);
    SetCodeAll(0x1FFD, false, 0xC0);

    Out(0x7FFD, 0xFF);
    EXPECT_EQ(Pld().pn, 0x3F) << "CNF bit 7 = 0: bits 7-6 cleaned";
    EXPECT_EQ(Pld().Cell(0xC1), 0xFF) << "the cell keeps the raw value";

    Out(0x003C, 0x24);  // CNF bit 2 = 1: CNF = #24 (bit 5: keep only bits 7-5)
    Out(0x7FFD, 0xFF);
    EXPECT_EQ(Pld().pn, 0x00) << "CNF bit 5 with bit 7 = 0: bit 5 and bits 4-0 cleaned too";

    Out(0x003C, 0x84);  // Pentagon-512 enable (bit 7)
    Out(0x7FFD, 0xFF);
    EXPECT_EQ(Pld().pn, 0xFF);

    Out(0x003C, 0xC4);  // bit 6: "SC clean"
    Out(0x1FFD, 0x11);
    EXPECT_EQ(Pld().sc, 0x00);
}

// T-MEM-2: writes to codes #F0-#FF land in the cell of the current Spectrum page
TEST_F(PortDecoderSprinter_Test, Page3Cells_FollowThe7FFDPage)
{
    OpenDcp();
    SetCode(0x7FFD, false, 0xC1);
    SetCode(0x00E2, false, 0xF0);
    SetCode(0x00E2, true, 0xF0);

    Out(0x7FFD, 0x03);
    Out(0x00E2, 0x23);
    EXPECT_EQ(Pld().Cell(0xF3), 0x23);
    EXPECT_EQ(Tag(0xC000), 0x23);
    EXPECT_EQ(In(0x00E2), 0x23) << "page cells read back (MAN §13.2)";

    Out(0x7FFD, 0x00);
    EXPECT_EQ(Tag(0xC000), Pld().Cell(0xF0));
    Out(0x7FFD, 0x03);
    EXPECT_EQ(Tag(0xC000), 0x23) << "Spectrum page 3 stays backed by #23";
}

// Turbo: CNF/SYS with bit 1 = 1 switches 21 MHz (hw_turbo_ratio 6)
TEST_F(PortDecoderSprinter_Test, Turbo_SysBit1SelectsRatio6)
{
    OpenDcp();
    SetCode(0x007C, false, 0xC6);
    Out(0x007C, 0x03);
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio, 6);
    Out(0x007C, 0x02);
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio, 1);

    // The front-panel turbo off keeps 3.5 MHz
    Pld().turboHard = 0;
    Out(0x007C, 0x03);
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio, 1);
}

// The Z84C15's CTC on the board (MAME sprinter.cpp:1993-2008): TRG0-TRG2 are X_SP / 48 = 875 kHz in real time and
// ZC/TO2 drives TRG3, so the 48.83 Hz tick of channels 2 + 3 (Bad Apple, dontBlink) is the same at 3.5 and 21 MHz;
// a timer counts the CPU clock (MAME derives the CTC clock from the scaled CPU clock) and runs six times faster
TEST_F(PortDecoderSprinter_Test, Ctc_TriggersAreRealTimeTimersFollowTheCpuClock)
{
    Z84Lib::Z84Ctc& ctc = _decoder->GetZ84().ctc;
    const auto wait = [&](uint64_t baseT) { _context->emulatorState.t_states += baseT; };
    _z80->t = 0;  // the load on an edge of the 875 kHz grid (an edge every 4 base T-states from the power-on)
    Out(0x0010, 0x00);  // vector base
    Out(0x0012, 0x57);
    Out(0x0012, 112);
    Out(0x0013, 0xD7);
    Out(0x0013, 160);
    Out(0x0011, 0x05);  // timer, prescaler 16, 256
    Out(0x0011, 0x00);
    EXPECT_DOUBLE_EQ(ctc.OutputHz(3), 875000.0 / 112 / 160);
    EXPECT_DOUBLE_EQ(ctc.OutputHz(1), 3500000.0 / 16 / 256);

    constexpr uint64_t kTickT = 112 * 160 * 4;  // 71 680 base T-states = 20.48 ms
    const uint64_t zeros = ctc.ZeroCounts(3);
    wait(kTickT - 1);
    EXPECT_EQ(ctc.ZeroCounts(3), zeros);
    wait(1);
    EXPECT_EQ(ctc.ZeroCounts(3), zeros + 1);
    EXPECT_TRUE(_decoder->GetZ84().IntPending());
    EXPECT_EQ(_decoder->GetZ84().AcknowledgeInterrupt(), 0x06);
    _decoder->GetZ84().OnReti();

    // 21 MHz: the same tick in real time; the timer six times faster, its count kept through the switch
    const uint8_t before = ctc.Read(1);
    OpenDcp();
    SetCode(0x007C, false, 0xC6);
    Out(0x007C, 0x03);
    ASSERT_EQ(_context->emulatorState.current_z80_frequency_multiplier, 6);
    EXPECT_EQ(ctc.Read(1), before);
    EXPECT_DOUBLE_EQ(ctc.OutputHz(3), 875000.0 / 112 / 160);
    EXPECT_DOUBLE_EQ(ctc.OutputHz(1), 21000000.0 / 16 / 256);
    wait(kTickT - 1);
    EXPECT_EQ(ctc.ZeroCounts(3), zeros + 1);
    wait(1);
    EXPECT_EQ(ctc.ZeroCounts(3), zeros + 2);
    EXPECT_TRUE(_decoder->GetZ84().IntPending());
    EXPECT_EQ(_decoder->GetZ84().AcknowledgeInterrupt(), 0x06);
}

/// endregion </T-MEM-1 / T-MEM-2>

/// region <T-FDD: the floppy controller behind the port table (phase S3a)>

// T-FDD-4: the board's #BD latch owns the WD1793 clock; a reset starts at 720 KB (1 MHz, 250 kbit/s)
TEST_F(PortDecoderSprinter_Test, Fdc_LatchedClockPolicy_DdAfterReset)
{
    WD1793* fdc = _context->pBetaDisk;
    ASSERT_NE(fdc, nullptr);
    EXPECT_EQ(_decoder->DefaultFdcClockPolicy(), FdcClockPolicy::Latched);
    EXPECT_EQ(fdc->GetClockPolicy(), FdcClockPolicy::Latched);
    EXPECT_TRUE(fdc->IsBaseClockTimeBase()) << "the disk keeps 300 rpm at 21 MHz";
    EXPECT_EQ(WD1793::ResolveClockPolicy(_decoder->DefaultFdcClockPolicy(), 1), FdcClockPolicy::Latched)
        << "[Beta128] TurboVG=1 does not override the latch";
    EXPECT_EQ(fdc->GetClock(), FdcClock::Clock1MHz);
    EXPECT_EQ(fdc->GetDataRate(), FdcDataRate::Rate250Kbps);
    EXPECT_FALSE(_decoder->IsFdcHighDensity());

    ASSERT_TRUE(fdc->SetLatchedClock(FdcClock::Clock2MHz, FdcDataRate::Rate500Kbps));
    Pld().fdcHd = 1;
    _core->Reset();
    EXPECT_EQ(fdc->GetClock(), FdcClock::Clock1MHz) << "RESET: the latch is back at 720 KB";
    EXPECT_EQ(fdc->GetDataRate(), FdcDataRate::Rate250Kbps);
    EXPECT_FALSE(_decoder->IsFdcHighDensity());
}

// T-FDD-5: OUT (#BD),A - A13 of the address (A on A15-A8: #01 / #21) picks codes #16 / #17 in the BIOS 3.04 table;
// the data bit 1 switches the FDC codes off (MAME)
TEST_F(PortDecoderSprinter_Test, Fdc_DensityLatch_CodesSixteenAndSeventeen)
{
    if (!LoadTable304())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";
    WD1793* fdc = _context->pBetaDisk;
    ASSERT_NE(fdc, nullptr);
    OpenDcp();
    Pld().dos = 0;  // TR-DOS on: the BIOS switches the density with the DOS ports open
    ASSERT_EQ(_decoder->LookupCode(0x21BD, false), 0x17);
    ASSERT_EQ(_decoder->LookupCode(0x01BD, false), 0x16);

    Out(0x21BD, 0x21);  // FddSetDensityHD (ROM page 0 #097C)
    EXPECT_TRUE(_decoder->IsFdcHighDensity());
    EXPECT_EQ(fdc->GetClock(), FdcClock::Clock2MHz);
    EXPECT_EQ(fdc->GetDataRate(), FdcDataRate::Rate500Kbps);

    Out(0x01BD, 0x01);  // FddSetDensityDD (#0977)
    EXPECT_FALSE(_decoder->IsFdcHighDensity());
    EXPECT_EQ(fdc->GetClock(), FdcClock::Clock1MHz);
    EXPECT_EQ(fdc->GetDataRate(), FdcDataRate::Rate250Kbps);

    // The data byte does not choose the density, the address does
    Out(0x21BD, 0x01);
    EXPECT_TRUE(_decoder->IsFdcHighDensity());

    // Bit 1 of the data: the FDC codes read #FF and drop writes until the next density write without it
    Out(0x007F, 0x5A);  // WD1793 data register (code #13)
    ASSERT_EQ(In(0x007F), 0x5A);
    Out(0x21BD, 0x23);
    EXPECT_EQ(In(0x007F), 0xFF);
    Out(0x007F, 0x11);
    Out(0x21BD, 0x21);
    EXPECT_EQ(In(0x007F), 0x5A) << "the write while off never reached the chip";
}

// Code #15: WD1793 INTRQ / DRQ in bits 7-6 and the Kempston joystick below (MAME state_r() & joy_ctrl_r(1))
TEST_F(PortDecoderSprinter_Test, Fdc_Code15_IntrqDrqAndJoystick)
{
    OpenDcp();
    SetCodeAll(0x00FF, true, 0x15);
    WD1793* fdc = _context->pBetaDisk;
    ASSERT_NE(fdc, nullptr);
    const uint8_t beta = static_cast<uint8_t>(_decoder->PeripheralPortIn(0xFF));
    EXPECT_EQ(In(0x00FF), static_cast<uint8_t>((beta & 0xC0) | (_decoder->Default_Port_KempstonJoystick_In() & 0x3F)));
    EXPECT_EQ(In(0x00FF) & 0x3F, 0x00) << "no joystick fitted: the low bits read 0";
    EXPECT_TRUE(_decoder->HasKempstonJoystick());
}

// T-FDD-6: the #1F operand rewrite. OUT (#1F),A from RAM reaches the table as port #xx0F (here code #10, the
// WD1793 command register); the same instruction in the system ROM, and OUT (C),A with C = #1F anywhere, reach
// the Z84C15 PIO port B control (8-bit decoded #1F)
TEST_F(PortDecoderSprinter_Test, Fdc_OperandRewrite_RamOnly_UnprefixedOnly)
{
    OpenDcp();
    SetCodeAll(0xD00F, false, 0x10);  // A4 is not decoded: #D01F has the same table entry, the Z84C15 takes it first
    WD1793* fdc = _context->pBetaDisk;
    ASSERT_NE(fdc, nullptr);
    ASSERT_TRUE(IsRam(0x8000));

    // LD A,#D0 : OUT (#1F),A from RAM -> code #10: FORCE INTERRUPT reaches the WD1793, the PIO keeps its vector
    RunCode({0x3E, 0xD0, 0xD3, 0x1F});
    EXPECT_EQ(static_cast<const WD1793*>(fdc)->getCommandRegister(), 0xD0);
    EXPECT_NE(_decoder->GetZ84().pio.GetPort(1).vector, 0xD0);

    // LD BC,#D01F : LD A,#D0 : OUT (C),A - no rewrite: the PIO port B takes #D0 as its interrupt vector
    RunCode({0x01, 0x1F, 0xD0, 0x3E, 0xD0, 0xED, 0x79});
    EXPECT_EQ(_decoder->GetZ84().pio.GetPort(1).vector, 0xD0);

    // The same OUT (#1F),A in the system ROM (window 0): not rewritten, the PIO again
    _decoder->GetZ84().pio.Reset();
    ASSERT_FALSE(IsRam(0x0000));
    const uint8_t romPage = static_cast<uint8_t>(Tag(0x0000) - kRomTagBase);
    uint8_t* rom = _memory->ROMPageHostAddress(romPage);
    const uint8_t code[] = {0x3E, 0xD0, 0xD3, 0x1F};
    std::memcpy(rom + 0x0100, code, sizeof(code));
    _z80->pc = 0x0100;
    for (int i = 0; i < 2; i++)
        _z80->Z80Step();
    ASSERT_EQ(_z80->pc, 0x0104);
    EXPECT_EQ(_decoder->GetZ84().pio.GetPort(1).vector, 0xD0) << "system ROM: no rewrite";
}

// T-FDD-7: the TR-DOS signal follows the opcode fetches (MAME map_fetch): on at #3Dxx while #7FFD bit 4 selects
// the 48 BASIC vROM, off from #4000 up; the floppy ports exist only while it is on (BIOS 3.04 table, map 0)
TEST_F(PortDecoderSprinter_Test, Dos_M1HookOpensAndClosesTheFloppyPorts)
{
    if (!LoadTable304())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";
    OpenDcp();
    Pld().cnf = 0;
    Pld().pn = 0x10;  // 48 BASIC
    Pld().dos = 1;

    _decoder->BeforeMachineM1(0x3D2F);
    EXPECT_EQ(Pld().dos, 0) << "fetch at #3Dxx: DOS on";
    EXPECT_EQ(_decoder->LookupIndex(0x001F, true) & 0x0400, 0) << "index bit 10 (/DOS) low";
    EXPECT_EQ(_decoder->LookupCode(0x001F, true), 0x10) << "WD1793 status";
    EXPECT_EQ(_decoder->LookupCode(0x00FF, true), 0x15);

    _decoder->BeforeMachineM1(0x1234);
    EXPECT_EQ(Pld().dos, 0) << "below #4000 the signal stays";
    _decoder->BeforeMachineM1(0x4000);
    EXPECT_EQ(Pld().dos, 1) << "fetch from #4000 up: DOS off";
    EXPECT_EQ(_decoder->LookupCode(0x001F, true), 0x15) << "#1F / #0F read: the Kempston view";
    EXPECT_EQ(_decoder->LookupCode(0x003F, true), 0x00) << "the WD1793 track register is gone";

    Pld().pn = 0x00;  // 128 BASIC in window 0: #3Dxx is not TR-DOS's entry
    _decoder->BeforeMachineM1(0x3D2F);
    EXPECT_EQ(Pld().dos, 1);
}

/// endregion </T-FDD>

/// region <T-RTC: the DS12887A behind codes #1C / #1D / #1E>

// T-RTC-1: address #DFBD, data write #BFBD, data read #FFBD
TEST_F(PortDecoderSprinter_Test, Cmos_AddressAndDataPorts)
{
    if (!LoadTable304())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";
    OpenDcp();

    Out(0xDFBD, 0x20);
    Out(0xBFBD, 0x5C);
    EXPECT_EQ(_decoder->GetRtc().GetCell(0x20), 0x5C);
    Out(0xDFBD, 0x20);
    EXPECT_EQ(In(0xFFBD), 0x5C);
    EXPECT_EQ(_decoder->GetRtc().GetCellCount(), 128u);
}

// T-RTC-2: the clock registers in BCD from a fixed time; century #32 (DS12887A)
TEST_F(PortDecoderSprinter_Test, Cmos_FixedTimeBcdAndCentury)
{
    if (!LoadTable304())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";
    OpenDcp();
    _decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC (host local time on read)

    Out(0xDFBD, Ds12887::kRegB);
    EXPECT_EQ(In(0xFFBD) & Ds12887::kBBinary, 0) << "BCD after power-on";
    Out(0xDFBD, Ds12887::kSeconds);
    EXPECT_EQ(In(0xFFBD), 0x30);
    Out(0xDFBD, 0x32);
    const uint8_t century = In(0xFFBD);
    EXPECT_TRUE(century == 0x20) << std::hex << +century;
}

// T-RTC-3: [SPRINTER] CmosFile is read at power-on and written when the machine goes
TEST_F(PortDecoderSprinter_Test, Cmos_FileRoundTrip)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("sprinter-cmos.bin");
    {
        Ds12887 chip(128);
        chip.SetCell(0x40, 0xAB);
        ASSERT_TRUE(chip.SaveNvram(path));
    }

    _cmosPath = path;
    ASSERT_TRUE(Rebuild());
    EXPECT_EQ(_decoder->GetRtc().GetCell(0x40), 0xAB) << "loaded at power-on";
    _decoder->GetRtc().SetCell(0x41, 0xCD);
    Destroy();

    Ds12887 chip(128);
    ASSERT_TRUE(chip.LoadNvram(path));
    EXPECT_EQ(chip.GetCell(0x40), 0xAB);
    EXPECT_EQ(chip.GetCell(0x41), 0xCD) << "saved when the machine was destroyed";
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

/// endregion </T-RTC>

/// region <Surfaces>

TEST_F(PortDecoderSprinter_Test, TraceCodeTable_NamesTheCodes)
{
    const auto table = _decoder->GetPortTraceCodeTable();
    auto nameOf = [&](uint16_t code) {
        for (const auto& row : table)
            if (row.code == code)
                return row.name;
        return std::string();
    };
    EXPECT_EQ(nameOf(0x2B), "IdePrimary");
    EXPECT_EQ(nameOf(0xC1), "7FFD");
    EXPECT_EQ(nameOf(PortDecoder_Sprinter::kTraceZ84Base + 0x19), "Z84 SIO A control");
}

TEST_F(PortDecoderSprinter_Test, Ttd_EveryDeclaredStateHasASerializer)
{
    const auto ids = _decoder->GetTTDModelStateIds();
    const auto serializers = _decoder->CreateTTDSerializers();
    for (ttd::PeripheralId id : {ttd::PeripheralId::SprinterPld, ttd::PeripheralId::Ds12887, ttd::PeripheralId::SprinterVideoRam,
                                 ttd::PeripheralId::Z84C15, ttd::PeripheralId::SprinterFastRam, ttd::PeripheralId::SprinterInput, ttd::PeripheralId::Wd1793Context})
    {
        EXPECT_NE(std::find(ids.begin(), ids.end(), id), ids.end()) << "declared: id " << int(id);
        EXPECT_TRUE(std::any_of(serializers.begin(), serializers.end(), [id](const auto& s) { return s->TTDPeripheralId() == id; }))
            << "serialized: id " << int(id);
    }
}

/// endregion </Surfaces>

/// region <T-IDE: the IDE codes through the port table and the CPU (tdd-storage §3)>

namespace
{
    /// Sector n of the test disk: byte i = n * 16 + i (mod 256), so every byte of a sector differs from its neighbor
    void FillDisk(MemoryDisk& disk)
    {
        for (uint64_t lba = 0; lba < disk.SectorCount(); lba++)
            for (size_t i = 0; i < 512; i++)
                disk.Data()[lba * 512 + i] = static_cast<uint8_t>(lba * 16 + i);
    }
}  // namespace

class PortDecoderSprinterIde_Test : public PortDecoderSprinter_Test
{
protected:
    MemoryDisk _disk{64};

    /// [HDD] Scheme=SPRINTER with a disk on ide0.master, the BIOS 3.04 port table, the decoder open
    bool FitIde()
    {
        if (!LoadTable304())
            return false;
        _context->config.ide_scheme = IDE_SPRINTER;
        _core->RefitIde();
        if (!_context->pIdeController || !_context->pIdeController->Enabled())
            return false;
        FillDisk(_disk);
        _context->pIdeController->Channel(0).Unit(0)->AttachMedium(_disk, {});
        OpenDcp();
        return true;
    }

    /// The BIOS's command sequence (ROM page 0 PRESET #0C40): count #0152, LBA #0153-#0155, device #4152, command #4153
    static std::vector<uint8_t> Command(uint8_t command, uint8_t lba)
    {
        return {0x3E, 0x21, 0xD3, 0xBC,                    // LD A,#21 : OUT (#BC),A - primary channel
                0x01, 0x52, 0x41, 0x3E, 0xE0, 0xED, 0x79,  // LD BC,#4152 : LD A,#E0 : OUT (C),A - master, LBA
                0x01, 0x52, 0x01, 0x3E, 0x01, 0xED, 0x79,  // LD BC,#0152 : LD A,1 : OUT (C),A - one sector
                0x01, 0x53, 0x01, 0x3E, lba, 0xED, 0x79,   // LD BC,#0153 : LD A,lba : OUT (C),A
                0x01, 0x54, 0x01, 0xAF, 0xED, 0x79,        // LD BC,#0154 : XOR A : OUT (C),A
                0x01, 0x55, 0x01, 0xED, 0x79,              // LD BC,#0155 : OUT (C),A
                0x01, 0x53, 0x41, 0x3E, command, 0xED, 0x79};  // LD BC,#4153 : LD A,command : OUT (C),A
    }
};

// The BIOS 3.04 table maps the IDE ports to codes #20-#2B (hardware-reference §4.4) in both DOS states
TEST_F(PortDecoderSprinterIde_Test, Table304_IdeCodes)
{
    if (!LoadTable304())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";
    for (uint8_t dos : {0, 1})
    {
        Pld().dos = dos;
        EXPECT_EQ(_decoder->LookupCode(0x0050, true), 0x20);
        EXPECT_EQ(_decoder->LookupCode(0xFF50, true), 0x20) << "INI's B on A15-A8 keeps the data code";
        EXPECT_EQ(_decoder->LookupCode(0x0150, false), 0x20);
        EXPECT_EQ(_decoder->LookupCode(0x0153, false), 0x23);
        EXPECT_EQ(_decoder->LookupCode(0x4053, true), 0x27);
        EXPECT_EQ(_decoder->LookupCode(0x4152, false), 0x26);
        EXPECT_EQ(_decoder->LookupCode(0x4054, true), 0x28);
        EXPECT_EQ(_decoder->LookupCode(0x01BC, false), 0x2A);
        EXPECT_EQ(_decoder->LookupCode(0x21BC, false), 0x2B);
    }
}

// The Z84C15 engine drives B on A15-A8 of an INI / OUTI cycle the way the Z80 does: INI with the value before the
// decrement, OUTI with the value after it (Zilog UM0080 "INI", "OUTI"). Two port-table cells tell the bus addresses
// apart: port #E000 (A15-A13 = 111) reads cell #EC, #C000 / #DF00 (110) reads / writes cell #ED, #BF00 (101) cell #EB
TEST_F(PortDecoderSprinterIde_Test, Z84C15_IniOutiPutBOnTheHighAddressByte)
{
    OpenDcp();
    SetCodeAll(0xE000, true, 0xEC);
    SetCodeAll(0xC000, true, 0xED);
    SetCodeAll(0xC000, false, 0xED);
    SetCodeAll(0xA000, false, 0xEB);
    Pld().Cell(0xEC) = 0xAA;
    Pld().Cell(0xED) = 0xBB;

    // LD HL,#9000 : LD BC,#E000 : INI - reads port #E000 (B = #E0 before the decrement): cell #EC
    RunCode({0x21, 0x00, 0x90, 0x01, 0x00, 0xE0, 0xED, 0xA2});
    EXPECT_EQ(Peek(0x9000), 0xAA) << "INI must put B before its decrement on A15-A8";
    EXPECT_EQ(_z80->b, 0xDF);

    // LD HL,#9000 : LD (HL),#5C : LD BC,#C000 : OUTI - writes port #BF00 (B = #BF after the decrement): cell #EB
    RunCode({0x21, 0x00, 0x90, 0x36, 0x5C, 0x01, 0x00, 0xC0, 0xED, 0xA3});
    EXPECT_EQ(Pld().Cell(0xEB), 0x5C) << "OUTI must put B after its decrement on A15-A8";
    EXPECT_EQ(Pld().Cell(0xED), 0xBB);
}

// T-IDE-5: the BIOS sector loop LD BC,#0050 : INI x 512 (ROM page 0 RDS003 #0A8F: 32 x 16 INI) reads a whole
// sector in order: B runs #00, #FF, #FE ... so A8 alternates word low byte / latched high byte
TEST_F(PortDecoderSprinterIde_Test, IniLoopReadsASectorInOrder)
{
    if (!FitIde())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";
    std::vector<uint8_t> code = Command(0x20, 9);  // READ SECTORS, LBA 9
    const std::vector<uint8_t> setup = {0x21, 0x00, 0xA0, 0x01, 0x50, 0x00};  // LD HL,#A000 : LD BC,#0050
    code.insert(code.end(), setup.begin(), setup.end());
    for (int i = 0; i < 512; i++)
        code.insert(code.end(), {0xED, 0xA2});  // INI
    RunCode(code);
    for (uint16_t i = 0; i < 512; i++)
        ASSERT_EQ(Peek(static_cast<uint16_t>(0xA000 + i)), _disk.Data()[9 * 512 + i]) << "byte " << i;
    EXPECT_EQ(In(0x4053), 0x50) << "DRDY | DSC: the sector is done";
}

// T-IDE-6: the BIOS write loop LD BC,#0150 : OUTI x 512 (ROM page 0 WRS003 #0B97): OUTI decrements B first, so the
// ports are #0050 (low byte to the latch), #FF50 (word), ...; the image gets the bytes in order
TEST_F(PortDecoderSprinterIde_Test, OutiLoopWritesASectorInOrder)
{
    if (!FitIde())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";
    for (uint16_t i = 0; i < 512; i++)
        Poke(static_cast<uint16_t>(0xA000 + i), static_cast<uint8_t>(0xA5 ^ i ^ (i >> 8)));
    std::vector<uint8_t> code = Command(0x30, 5);  // WRITE SECTORS, LBA 5
    const std::vector<uint8_t> setup = {0x21, 0x00, 0xA0, 0x01, 0x50, 0x01};  // LD HL,#A000 : LD BC,#0150
    code.insert(code.end(), setup.begin(), setup.end());
    for (int i = 0; i < 512; i++)
        code.insert(code.end(), {0xED, 0xA3});  // OUTI
    RunCode(code);
    for (uint16_t i = 0; i < 512; i++)
        ASSERT_EQ(_disk.Data()[5 * 512 + i], static_cast<uint8_t>(0xA5 ^ i ^ (i >> 8))) << "byte " << i;
    EXPECT_EQ(In(0x4053), 0x50);
}

// The channel select through the table: OUT (#BC),#01 reaches the secondary channel (empty: DD7 pulled down, #7F),
// OUT (#BC),#21 the primary again; the latch is the IDE adapter's (the AtaChannel TTD blob)
TEST_F(PortDecoderSprinterIde_Test, ChannelSelectThroughTheTable)
{
    if (!FitIde())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";
    EXPECT_EQ(In(0x4053), 0x50);
    Out(0x01BC, 0x01);
    EXPECT_EQ(_decoder->GetIdeAdapter().State().channel, 1);
    EXPECT_EQ(In(0x4053), 0x7F) << "secondary: no drive, BSY = 0";
    Out(0x21BC, 0x21);
    EXPECT_EQ(In(0x4053), 0x50);

    // The PLD's reset (the RESET button) selects the primary channel
    Out(0x01BC, 0x01);
    _core->Reset();
    EXPECT_EQ(_decoder->GetIdeAdapter().State().channel, 0);
}

/// endregion </T-IDE>
