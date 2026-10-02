// PortDecoder_Sprinter: the port table lookup, the start-up gate, the code
// semantics of the standard configuration and the cells (Sprinter test-plan
// §2.1 T-DCP, §2.2 T-MEM-1/2, §2.7 T-RTC; tdd-ports-memory §3-§4).

#include "sprinterfixture.h"

#include "emulator/io/rtc/ds12887.h"

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

/// endregion </T-MEM-1 / T-MEM-2>

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

TEST_F(PortDecoderSprinter_Test, Ttd_DeclaresThePldStateItCannotRecordYet)
{
    const auto ids = _decoder->GetTTDModelStateIds();
    EXPECT_NE(std::find(ids.begin(), ids.end(), ttd::PeripheralId::SprinterPld), ids.end());
    const auto serializers = _decoder->CreateTTDSerializers();
    for (const auto& s : serializers)
        EXPECT_NE(s->TTDPeripheralId(), ttd::PeripheralId::SprinterPld) << "the PLD serializer is phase S7";
}

/// endregion </Surfaces>
