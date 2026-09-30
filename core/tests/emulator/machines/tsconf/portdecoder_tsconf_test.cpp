// TS-Conf port decoder: registers, #7FFD / LCK128, FM window, DOS trap, CMOS
// gating, CPU clock (TSConf implementation-plan phase 1; hardware-spec
// sections in each test's comment).

#include "tsconffixture.h"

#include "emulator/memory/atm/evoavr.h"

class PortDecoder_TSConf_Test : public TsConfFixture
{
};

/// DEC-1 (td §3.7): the factory builds the TS-Conf decoder and memory, the model is creatable
TEST_F(PortDecoder_TSConf_Test, DEC1_FactoryAndCreatable)
{
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_TSL));
    EXPECT_NE(dynamic_cast<TsConfMemory*>(_memory), nullptr);
    EXPECT_EQ(dynamic_cast<TsConfMemory*>(_memory)->GetState(), &_decoder->GetState());

    const TMemModel* model = Config::FindModelByShortName("TSL");
    ASSERT_NE(model, nullptr);
    EXPECT_TRUE(Config::IsModelCreatable(*model));
    EXPECT_EQ(_decoder->TtdClockUnits(), 4);
}

/// RST-1 (hs §10): the warm-reset layout
TEST_F(PortDecoder_TSConf_Test, RST1_WarmResetValues)
{
    Reg(TsConfReg::Page2, 0x33);
    Reg(TsConfReg::MemConfig, 0x0E);
    Reg(TsConfReg::SysConfig, 0x02);
    _decoder->reset();

    EXPECT_EQ(In(0x12AF), 0x02);
    EXPECT_EQ(In(0x13AF), 0x00);
    EXPECT_FALSE(IsRam(0x0000));
    EXPECT_EQ(Tag(0x0000), 0x00) << "ROM page 0 = TS-BIOS";
    EXPECT_EQ(Tag(0x4000), 0x05);
    EXPECT_EQ(Tag(0x8000), 0x02);
    EXPECT_EQ(Tag(0xC000), 0x00);
    EXPECT_TRUE(IsRam(0x4000) && IsRam(0x8000) && IsRam(0xC000));

    const TsConfState& ts = _decoder->GetState();
    EXPECT_EQ(ts.regs[TsConfReg::IntMask], 0x01);
    EXPECT_EQ(ts.regs[TsConfReg::TConfig], 0x00);
    EXPECT_EQ(ts.regs[TsConfReg::PalSel], 0x0F);
    EXPECT_EQ(ts.regs[TsConfReg::MemConfig], 0x04);
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio, 1) << "3.5 MHz";
}

/// RST-2 (hs §4.3, §10): a warm reset keeps BORDER, T_MAP_PAGE, SG_PAGE, CRAM,
/// SFILE; power-on zeroes them and loads CRAM from the firmware table
TEST_F(PortDecoder_TSConf_Test, RST2_WarmResetKeepsTheNotResetRegisters)
{
    TsConfState& ts = _decoder->GetState();
    EXPECT_EQ(ts.cram[0xF1], 0x0010) << "ZX blue, normal level";
    EXPECT_EQ(ts.cram[0x00], 0x0000) << "RGB222 black";
    EXPECT_EQ(ts.regs[TsConfReg::Border], 0x00);

    Reg(TsConfReg::Border, 0xA5);
    Reg(TsConfReg::TMapPage, 0x20);
    Reg(TsConfReg::SGPage, 0x21);
    ts.cram[3] = 0x1234;
    ts.sfile[4] = 0x5678;
    _decoder->reset();
    EXPECT_EQ(ts.regs[TsConfReg::Border], 0xA5);
    EXPECT_EQ(ts.regs[TsConfReg::TMapPage], 0x20);
    EXPECT_EQ(ts.regs[TsConfReg::SGPage], 0x21);
    EXPECT_EQ(ts.cram[3], 0x1234);
    EXPECT_EQ(ts.sfile[4], 0x5678);

    _decoder->PowerOn();
    _decoder->reset();
    EXPECT_EQ(ts.regs[TsConfReg::Border], 0x00);
    EXPECT_EQ(ts.regs[TsConfReg::TMapPage], 0x00);
    EXPECT_EQ(ts.cram[3], 0x0018);
    EXPECT_EQ(ts.sfile[4], 0x0000);
}

/// REG-1 (hs §3.2): only 0x00, 0x12, 0x13, 0x27 are readable
TEST_F(PortDecoder_TSConf_Test, REG1_OnlyFourRegistersAreReadable)
{
    Reg(TsConfReg::Page2, 0x42);
    Reg(TsConfReg::Page3, 0x43);
    EXPECT_EQ(In(0x12AF), 0x42);
    EXPECT_EQ(In(0x13AF), 0x43);
    EXPECT_EQ(In(0x27AF), 0x00) << "DMA idle";
    for (uint8_t reg : {0x01, 0x07, 0x0F, 0x10, 0x11, 0x15, 0x20, 0x21, 0x2A, 0x2B, 0x40, 0x80, 0xFF})
        EXPECT_EQ(In(static_cast<uint16_t>((reg << 8) | 0xAF)), 0xFF) << "register " << int(reg);
}

/// REG-2 (hs §3.3): STATUS PWR_UP is set until the first read
TEST_F(PortDecoder_TSConf_Test, REG2_StatusPowerUpClearsAfterTheFirstRead)
{
    EXPECT_EQ(In(0x00AF), 0x40 | _decoder->VdacVersion());
    EXPECT_EQ(In(0x00AF), _decoder->VdacVersion());
    _decoder->reset();
    EXPECT_EQ(In(0x00AF), _decoder->VdacVersion()) << "a warm reset does not set PWR_UP";
}

/// P7F-1 (hs §2.3): 512K mode
TEST_F(PortDecoder_TSConf_Test, P7F1_Mode512K)
{
    Reg(TsConfReg::MemConfig, 0x04);  // LCK128 = 00
    Out(0x7FFD, 0xC7);
    const TsConfState& ts = _decoder->GetState();
    EXPECT_EQ(ts.regs[TsConfReg::Page3], 0x1F);
    EXPECT_EQ(ts.MemConfig() & TsConfMemConfig::Rom128, 0);
    EXPECT_EQ(ts.regs[TsConfReg::VPage], 0x05);
    EXPECT_EQ(Tag(0xC000), 0x1F);
}

/// P7F-2: 128K mode
TEST_F(PortDecoder_TSConf_Test, P7F2_Mode128K)
{
    Reg(TsConfReg::MemConfig, 0x44);
    Out(0x7FFD, 0xC7);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::Page3], 0x07);
}

/// P7F-3: 1024K mode, D5 is a page bit, no lock
TEST_F(PortDecoder_TSConf_Test, P7F3_Mode1024KHasNoLock)
{
    Reg(TsConfReg::MemConfig, 0xC4);
    Out(0x7FFD, 0xE7);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::Page3], 0x3F);
    Out(0x7FFD, 0x01);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::Page3], 0x01) << "a following write is still accepted";
}

/// P7F-4: auto mode takes the rule from the opcode latched at M1
TEST_F(PortDecoder_TSConf_Test, P7F4_AutoModeFollowsTheOpcode)
{
    Reg(TsConfReg::MemConfig, 0x84);
    RunCode({0x3E, 0x47, 0xD3, 0xFD});  // LD A,#47 : OUT (#FD),A  (A = #47 -> port #47FD)
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::Page3], 0x07) << "OUT (n),A: 128K rule";

    RunCode({0x01, 0xFD, 0x7F, 0x3E, 0x47, 0xED, 0x79});  // LD BC,#7FFD : LD A,#47 : OUT (C),A
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::Page3], 0x0F) << "OUT (C),r: 512K rule";
}

/// P7F-5: lock48 blocks 7FFD (ROM128 and V_PAGE too) until reset; #xxAF still works
TEST_F(PortDecoder_TSConf_Test, P7F5_Lock48)
{
    Out(0x7FFD, 0x20);
    Out(0x7FFD, 0x1F);
    const TsConfState& ts = _decoder->GetState();
    EXPECT_EQ(ts.regs[TsConfReg::Page3], 0x00);
    EXPECT_EQ(ts.regs[TsConfReg::VPage], 0x05);
    EXPECT_EQ(ts.MemConfig() & TsConfMemConfig::Rom128, 0);
    EXPECT_TRUE(_decoder->IsPagingLocked());

    Reg(TsConfReg::Page3, 0x07);
    EXPECT_EQ(Tag(0xC000), 0x07);

    _decoder->reset();
    EXPECT_FALSE(_decoder->IsPagingLocked());
    Out(0x7FFD, 0x03);
    EXPECT_EQ(ts.regs[TsConfReg::Page3], 0x03);
}

/// P7F-6: the decode is A15 = 0 and low byte #FD
TEST_F(PortDecoder_TSConf_Test, P7F6_Decode)
{
    Out(0xFFFD, 0x07);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::Page3], 0x00) << "#FFFD is the AY";
    Out(0x7FFC, 0x07);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::Page3], 0x00) << "#7FFC is not #FD";
    Out(0x7EFD, 0x06);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::Page3], 0x06);
}

/// P7F-7: bit 3 switches V_PAGE to 7 at once
TEST_F(PortDecoder_TSConf_Test, P7F7_ScreenBitSetsVideoPageImmediately)
{
    Out(0x7FFD, 0x08);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::VPage], 0x07);
    Out(0x7FFD, 0x00);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::VPage], 0x05);
}

/// FM-1 (hs §2.4): a CRAM word through the FM window, the bytes land in RAM too
TEST_F(PortDecoder_TSConf_Test, FM1_CramThroughTheWindow)
{
    Reg(TsConfReg::FMaps, 0x14);  // MEN, window #4000
    Poke(0x4000, 0x34);
    Poke(0x4001, 0x12);
    EXPECT_EQ(_decoder->GetState().cram[0], 0x1234);
    EXPECT_EQ(Ram(5, 0x0000), 0x34);
    EXPECT_EQ(Ram(5, 0x0001), 0x12);
    EXPECT_EQ(Peek(0x4000), 0x34) << "reads see memory";
}

/// FM-2: SFILE; an odd write without its even half uses the stale stash
TEST_F(PortDecoder_TSConf_Test, FM2_SfileAndTheStash)
{
    Reg(TsConfReg::FMaps, 0x14);
    Poke(0x4202, 0x11);
    Poke(0x4203, 0x80);
    EXPECT_EQ(_decoder->GetState().sfile[1], 0x8011);
    Poke(0x4205, 0x22);
    EXPECT_EQ(_decoder->GetState().sfile[2], 0x2211) << "stale stash";
}

/// FM-3: the register window is OUT (n << 8 | #AF); beyond 0x4FF nothing
TEST_F(PortDecoder_TSConf_Test, FM3_RegisterWindow)
{
    Reg(TsConfReg::FMaps, 0x14);
    Poke(0x4400 + TsConfReg::Page3, 0x07);
    EXPECT_EQ(Tag(0xC000), 0x07);
    Poke(0x4500 + TsConfReg::Page3, 0x09);
    Poke(0x4800 + TsConfReg::Page3, 0x0A);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::Page3], 0x07);
}

/// FM-4: MEN clear -> no effect; reset clears MEN, keeps the address
TEST_F(PortDecoder_TSConf_Test, FM4_EnableBit)
{
    Reg(TsConfReg::FMaps, 0x04);
    Poke(0x4000, 0x34);
    Poke(0x4001, 0x12);
    EXPECT_EQ(_decoder->GetState().cram[0], 0x0000);

    Reg(TsConfReg::FMaps, 0x14);
    EXPECT_EQ(_core->GetBusOverlayCount(), 1u);
    _decoder->reset();
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::FMaps], 0x04);
    EXPECT_EQ(_core->GetBusOverlayCount(), 0u);
}

/// DOS trap (hs §2.2): #3Dxx in mapped mode with ROM128 = 1 pages TR-DOS for
/// that very fetch; a fetch at >= #4000 closes it
TEST_F(PortDecoder_TSConf_Test, DosTrapAndExit)
{
    Reg(TsConfReg::MemConfig, 0x01);  // mapped, ROM, ROM128 = 1
    EXPECT_EQ(Tag(0x0000), 0x03) << "BASIC-48";

    _memory->DirectWriteToZ80Memory(0x8000, 0xCD);  // CALL #3D00
    _memory->DirectWriteToZ80Memory(0x8001, 0x00);
    _memory->DirectWriteToZ80Memory(0x8002, 0x3D);
    _z80->pc = 0x8000;
    _z80->sp = 0x9000;
    _z80->Z80Step();  // CALL #3D00
    EXPECT_EQ(_z80->pc, 0x3D00);
    EXPECT_EQ(_decoder->GetState().dos, 0);

    _z80->Z80Step();  // the #3D00 fetch pages TR-DOS: RET from ROM page 1
    EXPECT_EQ(_decoder->GetState().dos, 1);
    EXPECT_EQ(Tag(0x0000), 0x01) << "TR-DOS";
    EXPECT_NE(_context->emulatorState.flags & CF_TRDOS, 0);
    EXPECT_EQ(_z80->pc, 0x8003);

    _z80->Z80Step();  // a fetch at #8003 closes the session
    EXPECT_EQ(_decoder->GetState().dos, 0);
    EXPECT_EQ(Tag(0x0000), 0x03);
}

/// MEM-3 companion: DOS active and ROM128 cleared -> the service page; the
/// trap is off in normal mode and with ROM128 = 0
TEST_F(PortDecoder_TSConf_Test, DosTrapNeedsMappedModeAndRom128)
{
    Reg(TsConfReg::MemConfig, 0x05);  // normal mode, ROM128 = 1
    _decoder->BeforeMachineM1(0x3D00);
    EXPECT_EQ(_decoder->GetState().dos, 0);
    Reg(TsConfReg::MemConfig, 0x00);  // mapped, ROM128 = 0
    _decoder->BeforeMachineM1(0x3D00);
    EXPECT_EQ(_decoder->GetState().dos, 0);

    Reg(TsConfReg::MemConfig, 0x01);
    _decoder->BeforeMachineM1(0x3D2F);
    EXPECT_EQ(_decoder->GetState().dos, 1);
    Reg(TsConfReg::MemConfig, 0x00);
    EXPECT_EQ(Tag(0x0000), 0x00) << "service page";
}

/// Beta-128 ports answer only while DOS or FDD_VIRT[7]; #1F is the joystick otherwise (hs §8.2, §9)
TEST_F(PortDecoder_TSConf_Test, BetaPortsGatedByDosOrVgOpen)
{
    EXPECT_EQ(_decoder->ClassifyPort(0x001F), PortDecoder_TSConf::PortArm::Joystick);
    EXPECT_EQ(_decoder->ClassifyPort(0x00FF), PortDecoder_TSConf::PortArm::ZxBus);
    Reg(TsConfReg::FddVirt, 0x80);
    EXPECT_EQ(_decoder->ClassifyPort(0x001F), PortDecoder_TSConf::PortArm::Fdc);
    EXPECT_EQ(_decoder->ClassifyPort(0x12FF), PortDecoder_TSConf::PortArm::Fdc);
    EXPECT_NE(_context->emulatorState.flags & CF_DOSPORTS, 0);
    EXPECT_EQ(_decoder->ClassifyPort(0x009F), PortDecoder_TSConf::PortArm::ZxBus) << "there is no #9F";
}

/// Gluk CMOS (hs §9): reachable after #EFF7 bit 7, never from the TR-DOS ROM
TEST_F(PortDecoder_TSConf_Test, CmosGating)
{
    Out(0xDFF7, 0x0E);
    Out(0xBFF7, 0x77);
    Out(0xEFF7, 0x80);
    Out(0xDFF7, 0x0E);
    Out(0xBFF7, 0x5A);
    Out(0xDFF7, 0x0E);
    EXPECT_EQ(In(0xBFF7), 0x5A);
    Out(0xEFF7, 0x00);
    EXPECT_EQ(In(0xBFF7), 0xFF) << "gated off again";

    Out(0xEFF7, 0x80);
    _decoder->GetState().dos = 1;
    EXPECT_EQ(In(0xBFF7), 0xFF) << "not from the TR-DOS ROM";
    Out(0xEFF7, 0x00);
    EXPECT_EQ(_decoder->GetState().eff7, 0x80) << "#EFF7 is not writable inside DOS";
}

/// #FE: BORDER = {PAL_SEL[3:0], 0, c} (hs §3.4)
TEST_F(PortDecoder_TSConf_Test, BorderWriteUsesPalSel)
{
    Reg(TsConfReg::PalSel, 0x0A);
    Out(0x00FE, 0x05);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::Border], 0xA5);
}

/// CLK (hs §3.2, §11): SYS_CONFIG[1:0] -> 3.5 / 7 / 14 / 14 MHz at once
TEST_F(PortDecoder_TSConf_Test, SysConfigClock)
{
    const uint8_t expected[4] = {1, 2, 4, 4};
    for (uint8_t clock = 0; clock < 4; clock++)
    {
        Reg(TsConfReg::SysConfig, clock);
        EXPECT_EQ(_context->emulatorState.hw_turbo_ratio, expected[clock]) << int(clock);
    }
    Reg(TsConfReg::SysConfig, 0);
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio, 1);
}

/// ROM-1 (hs §10): a 64 KB ts-bios image loads, pages 4-31 read erased flash;
/// smaller images are refused
TEST_F(PortDecoder_TSConf_Test, ROM1_SixtyFourKilobyteImage)
{
    ASSERT_TRUE(RebuildWithRomPages(4));
    EXPECT_EQ(Tag(0x0000), 0x00);
    Reg(TsConfReg::Page0, 0x03);
    EXPECT_EQ(Tag(0x0000), 0x03);
    Reg(TsConfReg::Page0, 0x04);
    EXPECT_EQ(Tag(0x0000), 0xFF);
    Reg(TsConfReg::Page0, 0x1F);
    EXPECT_EQ(Tag(0x0000), 0xFF);

    EXPECT_FALSE(RebuildWithRomPages(2)) << "32 KB is not a TS-Conf ROM";
}

/// BOOT-0 (hs §2.2): the real zxevo.rom maps TS-BIOS at reset
TEST_F(PortDecoder_TSConf_Test, BOOT0_RealRomShowsTsBios)
{
    const auto romPath = TestPathHelper::FindProjectRoot() / "data" / "rom" / "zxevo.rom";
    if (!std::filesystem::exists(romPath))
        GTEST_SKIP() << "data/rom/zxevo.rom not present";

    std::strncpy(_context->config.tsl_rom_path, romPath.string().c_str(), sizeof(_context->config.tsl_rom_path) - 1);
    ROM rom(_context);
    ASSERT_TRUE(rom.LoadROM());
    _decoder->reset();

    std::string text;
    for (uint16_t i = 0; i < 7; i++)
        text += static_cast<char>(_memory->DirectReadFromZ80Memory(static_cast<uint16_t>(0x0B05 + i)));
    EXPECT_EQ(text, "TS-BIOS");
}

/// DBG-2: the port trace names the TS registers and the decode arms
TEST_F(PortDecoder_TSConf_Test, DBG2_PortTraceCodeNames)
{
    const auto table = _decoder->GetPortTraceCodeTable();
    auto nameOf = [&](uint16_t code) {
        for (const auto& entry : table)
            if (entry.code == code)
                return entry.name;
        return std::string();
    };
    EXPECT_EQ(nameOf(PortDecoder_TSConf::kTraceRegisterBase + 0x01), "V_PAGE");
    EXPECT_EQ(nameOf(PortDecoder_TSConf::kTraceRegisterBase + 0x21), "MEM_CONFIG");
    EXPECT_EQ(nameOf(PortDecoder_TSConf::kTraceRegisterBase + 0x27), "DMA_CTRL");
    EXPECT_EQ(nameOf(static_cast<uint16_t>(PortDecoder_TSConf::PortArm::Paging7FFD)), "Paging7FFD");
    EXPECT_EQ(nameOf(PortDecoder_TSConf::kTraceRegisterBase + 0x14), "") << "register 0x14 is not built";
}

/// TIM-2: at 14 MHz an I/O cycle to the AY or an open VG93 port stalls the
/// CPU 8 fclk = 4 clocks (IN and OUT); other ports, #FF and lower clocks do not
TEST_F(PortDecoder_TSConf_Test, TIM2_ExternalIoStallAt14MHz)
{
    auto clocks = [&](auto access) {
        const uint32_t before = _z80->tt;
        access();
        return (_z80->tt - before) / _z80->rate;
    };
    Reg(TsConfReg::SysConfig, 0x02);
    EXPECT_EQ(clocks([&] { Out(0xFFFD, 0x07); }), 4u) << "AY register select";
    EXPECT_EQ(clocks([&] { In(0xFFFD); }), 4u) << "AY read";
    EXPECT_EQ(clocks([&] { Out(0xBFFD, 0x00); }), 4u) << "AY data";
    EXPECT_EQ(clocks([&] { Out(0x7FFD, 0x10); }), 0u) << "#7FFD is on the board";
    EXPECT_EQ(clocks([&] { Out(0x00FE, 0x00); }), 0u);
    EXPECT_EQ(clocks([&] { In(0x001F); }), 0u) << "#1F outside DOS is the joystick";
    Reg(TsConfReg::FddVirt, 0x80);  // VG_OPEN
    EXPECT_EQ(clocks([&] { In(0x001F); }), 4u) << "VG93 status";
    EXPECT_EQ(clocks([&] { Out(0x007F, 0x00); }), 4u) << "VG93 data";
    EXPECT_EQ(clocks([&] { In(0x00FF); }), 0u) << "the Beta system register is not external";

    Reg(TsConfReg::SysConfig, 0x01);  // 7 MHz
    EXPECT_EQ(clocks([&] { Out(0xFFFD, 0x07); }), 0u);
}

/// PS2-1: the AVR is the board's PS/2 keyboard controller, as on ATM3 - a
/// host key reaches its scan code log (AVR extension 2 behind the Gluk cell
/// #F0), which Wild Commander and NedoOS read instead of the ZX matrix
TEST_F(PortDecoder_TSConf_Test, PS21_HostKeysReachTheAvrPs2Log)
{
    ASSERT_NE(_context->pKeyboard, nullptr);
    EXPECT_EQ(_context->pKeyboard->GetPs2Sink(), &_decoder->GetEvoAvr());
    Out(0xEFF7, 0x80);                   // the Gluk ports on
    Out(0xDFF7, 0xF0);
    Out(0xBFF7, EvoAvr::kExtPs2Log);     // cell #F0 = the PS/2 log
    Out(0xDFF7, 0xF0);
    EXPECT_EQ(In(0xBFF7), 0x00) << "empty log";
    _context->pKeyboard->ApplyPcKey(PcKey::A, true);  // the TTD apply point, live and replay
    Out(0xDFF7, 0xF0);
    EXPECT_EQ(In(0xBFF7), 0x1C) << "set 2 make code of A";
}
