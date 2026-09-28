#include "stdafx.h"
#include "pch.h"

#include "portdecoder_atm3_test.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/atm/cmos.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/emulatormanager.h"
#include "emulator/emulator.h"

/// region <SetUp / TearDown>

void PortDecoder_ATM3_Test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);

    // Memory must be attached before the decoder: PortDecoder caches the pointer in its constructor
    _memory = new Memory(_context);
    _context->pMemory = _memory;

    _portDecoder = new PortDecoder_ATM3(_context);

    // Mirror production wiring (Core::Init): port handlers delegate to
    // Memory::UpdateZ80Banks(), which dispatches window mapping through
    // EmulatorContext::pPortDecoder for the configured memory model
    _context->config.mem_model = MM_ATM3;
    _context->pPortDecoder = _portDecoder;
}

void PortDecoder_ATM3_Test::TearDown()
{
    if (_portDecoder != nullptr)
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }

    if (_memory != nullptr)
    {
        delete _memory;
        _memory = nullptr;
    }

    if (_context != nullptr)
    {
        _context->pMemory = nullptr;
        _context->pPortDecoder = nullptr;
        delete _context;
        _context = nullptr;
    }
}

/// endregion </Setup / TearDown>

/// region <Port FF77 tests - ATM3 has partial decode>

TEST_F(PortDecoder_ATM3_Test, IsPort_FF77_PartialDecode)
{
    // ATM3: Partial decode - any port with low byte 0x77 (original io.cpp: `p1 == 0x77`).
    // The BaseConf service ROM enables the memory manager via 0xBC77, which the
    // old 0x0FFF/0x0F77 mask missed.

    // Should match - any high byte, low byte 0x77
    EXPECT_TRUE(_portDecoder->IsPort_FF77(0xFF77));
    EXPECT_TRUE(_portDecoder->IsPort_FF77(0x0F77));
    EXPECT_TRUE(_portDecoder->IsPort_FF77(0x1F77));
    EXPECT_TRUE(_portDecoder->IsPort_FF77(0xAF77));
    EXPECT_TRUE(_portDecoder->IsPort_FF77(0xEF77));
    EXPECT_TRUE(_portDecoder->IsPort_FF77(0xBC77));  // BaseConf manager-enable write
    EXPECT_TRUE(_portDecoder->IsPort_FF77(0x0E77));

    // Should NOT match - wrong low byte
    EXPECT_FALSE(_portDecoder->IsPort_FF77(0xFF76));
    EXPECT_FALSE(_portDecoder->IsPort_FF77(0xFFFF));
    EXPECT_FALSE(_portDecoder->IsPort_FF77(0x0076));
}

/// endregion </Port FF77 tests>

/// region <Port 37F7 tests - 4MB memory manager>

TEST_F(PortDecoder_ATM3_Test, IsPort_37F7)
{
    // Port: #x7F7 (4MB Memory Manager): low byte F7, A8=1, A11:A10=01

    EXPECT_TRUE(_portDecoder->IsPort_37F7(0x37F7));
    EXPECT_TRUE(_portDecoder->IsPort_37F7(0xB7F7));  // High bits don't matter
    EXPECT_TRUE(_portDecoder->IsPort_37F7(0xF7F7));

    // A13:A12 are not decoded on the board (atm_pager.v:206-210 keys on A11:A10 only)
    EXPECT_TRUE(_portDecoder->IsPort_37F7(0x17F7));
    EXPECT_TRUE(_portDecoder->IsPort_37F7(0x07F7));

    // Should NOT match
    EXPECT_FALSE(_portDecoder->IsPort_37F7(0x3FF7)) << "A11:A10=11 is #xFF7";
    EXPECT_FALSE(_portDecoder->IsPort_37F7(0x36F7)) << "A8=0";
    EXPECT_FALSE(_portDecoder->IsPort_37F7(0x37F6));
    EXPECT_FALSE(_portDecoder->IsPort_37F7(0x37FF));
}

/// endregion </Port 37F7 tests>

/// region <Port BF tests - shaden control>

TEST_F(PortDecoder_ATM3_Test, IsPort_BF)
{
    // Port: #BF (ATM3 Control - shaden)
    // Low byte = 0xBF

    EXPECT_TRUE(_portDecoder->IsPort_BF(0x00BF));
    EXPECT_TRUE(_portDecoder->IsPort_BF(0xFFBF));
    EXPECT_TRUE(_portDecoder->IsPort_BF(0x12BF));

    // Should NOT match
    EXPECT_FALSE(_portDecoder->IsPort_BF(0x00BE));
    EXPECT_FALSE(_portDecoder->IsPort_BF(0x00FF));
}

/// endregion </Port BF tests>

/// region <Reset test>

TEST_F(PortDecoder_ATM3_Test, Reset)
{
    EmulatorState& state = _context->emulatorState;

    // Set some values
    state.p7FFD = 0x12;
    state.pFF77 = 0x34;
    state.pEFF7 = 0x56;
    state.pBF = 0x01;
    state.atmMemSwapped = true;

    // Reset
    _portDecoder->reset();

    // Mode-neutral base reset: generic port registers cleared, FF77 untouched
    // (boot defaults are applied separately via ApplyBootROMDefaults)
    EXPECT_EQ(state.p7FFD, 0x00);
    EXPECT_EQ(state.pEFF7, 0x00);
    EXPECT_EQ(state.pFF77, 0x34);
    EXPECT_TRUE(state.atmMemSwapped);

    // ATM3-specific registers
    EXPECT_EQ(state.pBF, 0x00);
    EXPECT_EQ(state.pBE, 0x00);
}

TEST_F(PortDecoder_ATM3_Test, ApplyBootROMDefaults_InheritedFromATM710)
{
    EmulatorState& state = _context->emulatorState;
    state.flags = 0x00;
    state.pFF77 = 0x00;

    // RM_DOS boot: inherited ATM710 memory-manager defaults
    _portDecoder->ApplyBootROMDefaults(RM_DOS);

    EXPECT_EQ(state.pFF77, 0x80 | 0x40 | 0x20 | 3);  // video mode 3 (ZX), INT gate on
    EXPECT_EQ(state.pFFF7[0], 0x0100 | 1);
    EXPECT_EQ(state.pFFF7[1], 0x0200 | 5);
    EXPECT_EQ(state.pFFF7[2], 0x0200 | 2);
    EXPECT_EQ(state.pFFF7[3], 0x0200 | 0);

    // Non-DOS boot: manager disabled (shared ATM behavior)
    _portDecoder->ApplyBootROMDefaults(RM_128);
    EXPECT_EQ(state.aFF77, 0x0000);
    EXPECT_EQ(state.pFF77, 0x00);
}

/// endregion </Reset test>

/// region <Inheritance tests - verify ATM3 extends ATM710>

TEST_F(PortDecoder_ATM3_Test, Port7FFD_FullLowByteDecode)
{
    // BaseConf decodes #7FFD as A15=0 with low byte #FD or #FC (zports.v:484,694)
    // - not the ATM710 A15/A2/A1 partial decode, so #7FF5 is no longer paging
    using Arm = PortDecoder_ATM3::PortArm;
    EXPECT_EQ(_portDecoder->ClassifyPort(0x7FFD, true), Arm::Paging7FFD);
    EXPECT_EQ(_portDecoder->ClassifyPort(0x1FFD, true), Arm::Paging7FFD) << "no #1FFD on BaseConf: an A15=0 alias";
    EXPECT_EQ(_portDecoder->ClassifyPort(0x7FFC, true), Arm::BorderAnd7FFD);
    EXPECT_NE(_portDecoder->ClassifyPort(0x7FF5, true), Arm::Paging7FFD);
    EXPECT_EQ(_portDecoder->ClassifyPort(0xFFFD, true), Arm::Ay);
    EXPECT_EQ(_portDecoder->ClassifyPort(0xBFFD, true), Arm::Ay);
}

TEST_F(PortDecoder_ATM3_Test, InheritsPort_EFF7)
{
    // ATM3 should inherit EFF7 decoding from ATM710
    EXPECT_TRUE(_portDecoder->IsPort_EFF7(0xEFF7));
    EXPECT_FALSE(_portDecoder->IsPort_EFF7(0xEFF6));
}

TEST_F(PortDecoder_ATM3_Test, IsPort_FFF7_PagerDecode)
{
    // BaseConf pager: low byte F7, A8=1, A11:A10=11, window = A15:A14
    // (atm_pager.v:200-204). A13:A12 are not decoded; Unreal's 0x3FFF mask
    // was narrower than the board
    uint8_t windowIndex;

    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0x3FF7, windowIndex));
    EXPECT_EQ(windowIndex, 0);
    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0x7FF7, windowIndex));
    EXPECT_EQ(windowIndex, 1);
    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0xBFF7, windowIndex));
    EXPECT_EQ(windowIndex, 2);
    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0xFFF7, windowIndex));
    EXPECT_EQ(windowIndex, 3);
    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0x0DF7, windowIndex)) << "A13:A12 not decoded";
    EXPECT_EQ(windowIndex, 0);
    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0xEFF7, windowIndex)) << "#EFF7 is window 3 in shadow";
    EXPECT_EQ(windowIndex, 3);

    EXPECT_FALSE(_portDecoder->IsPort_FFF7(0x00F7, windowIndex)) << "A8=0";
    EXPECT_FALSE(_portDecoder->IsPort_FFF7(0x37F7, windowIndex)) << "A11:A10=01 is #x7F7";
    EXPECT_FALSE(_portDecoder->IsPort_FFF7(0x3BF7, windowIndex)) << "A11:A10=10 is #xBF7";
    EXPECT_FALSE(_portDecoder->IsPort_FFF7(0x3FF6, windowIndex));
}

/// endregion </Inheritance tests>

/// region <37F7 memory manager tests>

TEST_F(PortDecoder_ATM3_Test, Port_37F7_Encoding_PreservesRAMType)
{
    EmulatorState& state = _context->emulatorState;
    state.p7FFD = 0x00;

    // Window 2 register (0xB7F7): type preserved, page = val ^ 0xFF
    state.pFFF7[2] = 0x0205;  // RAM from FFF7, page 5
    _portDecoder->DecodePortOut(0xB7F7, 0x00, 0x0000);
    EXPECT_EQ(state.pFFF7[2], 0x02FF);  // RAM from FFF7, page 0xFF (4MB top page)

    // ROM-from-7FFD type degrades to RAM (bit 8 cleared - the port always selects RAM)
    state.pFFF7[0] = 0x0101;
    _portDecoder->DecodePortOut(0x37F7, 0x00, 0x0000);
    EXPECT_EQ(state.pFFF7[0], 0x00FF);

    // Register set selection via 7FFD bit 4
    state.p7FFD = 0x10;
    state.pFFF7[7] = 0x0200;
    _portDecoder->DecodePortOut(0xF7F7, 0xF0, 0x0000);  // Window 3 of set 1
    EXPECT_EQ(state.pFFF7[7], 0x020F);
}

TEST_F(PortDecoder_ATM3_Test, Port_37F7_MapsTopRAMPage)
{
    EmulatorState& state = _context->emulatorState;
    state.aFF77 = PortDecoder_ATM3::ATM_AFF77_PEN | PortDecoder_ATM3::ATM_AFF77_CPM;
    // CP/M mode (~cpm inactive): open the memory-manager gate via shaden
    // (original io.cpp: manager ports live inside the CF_DOSPORTS block)
    state.pBF = 0x01;
    state.p7FFD = 0x00;
    state.pFFF7[1] = 0x0200;  // RAM from FFF7, page 0

    // Window 1 <- RAM page 0xFF (val 0x00 inverts to 0xFF)
    _portDecoder->DecodePortOut(0x77F7, 0x00, 0x0000);
    EXPECT_EQ(_memory->GetRAMPageForBank1(), 0xFF);
    EXPECT_EQ(_memory->GetMemoryBankMode(1), MemoryBankModeEnum::BANK_RAM);
}

TEST_F(PortDecoder_ATM3_Test, NMI_ForcesTopRAMPageAtWindow0)
{
    EmulatorState& state = _context->emulatorState;

    _portDecoder->reset();
    _portDecoder->ApplyBootROMDefaults(RM_DOS);  // Manager on, windows = ROM 0 / RAM 5 / RAM 2 / RAM 0

    // RM_DOS defaults latch cpm (aFF77.9): open the memory-manager gate via
    // shaden so the window write reaches the decoder (original io.cpp CF_DOSPORTS)
    state.pBF = 0x01;
    state.evoInNmi = true;
    state.p7FFD = 0x00;
    _portDecoder->DecodePortOut(0x3FF7, 0x7F, 0x0000);  // Any manager write re-runs the mapping

    EXPECT_EQ(_memory->GetMemoryBankMode(0), MemoryBankModeEnum::BANK_RAM);
    EXPECT_EQ(_memory->GetRAMPageForBank0(), 0xFF);
}

/// endregion </37F7 memory manager tests>

/// region <Turbo mode tests>

TEST_F(PortDecoder_ATM3_Test, Turbo_FF77Bit3_EFF7Bit4_MultiplierSelect)
{
    // ZX Evo baseconf / Pentevo clock select - three states, unlike the
    // two-state ATM 7.10 base (reference: Xpeccy pentevo.c evoOut77d,
    // `compSetHwTurbo(comp, (val & 0x08) ? 4 : ((comp->pEFF7 & 0x10) ? 1 : 2))`).
    //
    // Asserted on hw_turbo_shift: next_z80_frequency_multiplier is the HOST
    // speed control and Z80::ApplyQueuedFrequencyMultiplier composes
    // current = next << hw_turbo_shift, so the decoder must not write it.
    EmulatorState& state = _context->emulatorState;

    const uint8_t hostSpeed = 3;
    state.next_z80_frequency_multiplier = hostSpeed;

    // Open the memory-manager gate: the first FF77 write latches cpm in
    // aFF77 (see Port_FF77_Out_ATM3), which would otherwise swallow the write
    state.pBF = 0x01;

    _portDecoder->DecodePortOut(0xFF77, 0x08, 0x0000);
    EXPECT_EQ(state.hw_turbo_shift, 2) << "pFF77.3 set is 14 MHz";

    _portDecoder->DecodePortOut(0xFF77, 0x00, 0x0000);
    EXPECT_EQ(state.hw_turbo_shift, 1) << "turbo clear with pEFF7.4 clear is the 7 MHz default";

    // #EFF7 is written only outside shadow (zports.v:716 "EEF7 in shadow mode
    // is abandoned"): drop shaden and leave CP/M so the DOS line is off too
    state.pBF = 0x00;
    state.aFF77 = PortDecoder_ATM3::ATM_AFF77_PEN | PortDecoder_ATM3::ATM_AFF77_CPM;
    state.flags &= ~CF_TRDOS;
    _portDecoder->DecodePortOut(0xEFF7, 0x10, 0x0000);
    EXPECT_EQ(state.hw_turbo_shift, 0) << "pEFF7.4 locks 3.5 MHz";

    state.pBF = 0x01;
    _portDecoder->DecodePortOut(0xFF77, 0x08, 0x0000);
    EXPECT_EQ(state.hw_turbo_shift, 2) << "pFF77.3 overrides the 3.5 MHz lock";

    EXPECT_EQ(state.next_z80_frequency_multiplier, hostSpeed)
        << "the decoder must never write the host speed control";
}

/// endregion </Turbo mode tests>

/// region <ATM palette port #FF tests - exact decode and manager gate>

TEST_F(PortDecoder_ATM3_Test, IsPort_ATM_Palette_ExactFFDecode)
{
    // The ZX-Evo FPGA palette latch sees one decoded #FF address (xpeccy
    // evoPortMap {0x00ff, 0x00ff}); the #xx9F / #xxBF / #xxDF aliases the
    // ATM710 DAC also matches belong to the older board
    EXPECT_TRUE(_portDecoder->IsPort_ATM_Palette(0x00FF));
    EXPECT_TRUE(_portDecoder->IsPort_ATM_Palette(0xFBFF));
    EXPECT_TRUE(_portDecoder->IsPort_ATM_Palette(0xFFFF));

    EXPECT_FALSE(_portDecoder->IsPort_ATM_Palette(0x009F));
    EXPECT_FALSE(_portDecoder->IsPort_ATM_Palette(0x00BF));
    EXPECT_FALSE(_portDecoder->IsPort_ATM_Palette(0x00DF));
    EXPECT_FALSE(_portDecoder->IsPort_ATM_Palette(0x00FE));
}

TEST_F(PortDecoder_ATM3_Test, PaletteFF_ManagerGate)
{
    // The palette entry sits behind the manager/shaden gate (xpeccy gates it
    // on the dos line; IsManagerEnabled is the ATM3 analog). At reset
    // aFF77 = 0 -> ~cpm -> open; cpm set + no shaden -> closed
    EmulatorState& state = _context->emulatorState;
    state.flags = 0x00;
    state.border_attr = 0x00;
    state.atmBorderBright = 0;

    // aFF77 = 0 at reset: gate open, pen2 clear
    _portDecoder->DecodePortOut(0x00FF, 0x00, 0x0000);
    EXPECT_EQ(state.atmPalette[0], 0xFFFFFFFFu);

    // cpm set, shaden clear, no TR-DOS session -> blocked (pen2 also set -
    // the gate must close before the latch is even reached)
    state.atmPalette[0] = 0x12345678;  // sentinel
    state.aFF77 = PortDecoder_ATM3::ATM_AFF77_CPM | PortDecoder_ATM3::ATM_AFF77_PEN2;
    _portDecoder->DecodePortOut(0x00FF, 0x00, 0x0000);
    EXPECT_EQ(state.atmPalette[0], 0x12345678u);

    // shaden (pBF.0) reopens it - even with cpm still set
    state.pBF = 0x01;
    state.aFF77 = PortDecoder_ATM3::ATM_AFF77_CPM;
    _portDecoder->DecodePortOut(0x00FF, 0x00, 0x0000);
    EXPECT_EQ(state.atmPalette[0], 0xFFFFFFFFu);
}

TEST_F(PortDecoder_ATM3_Test, Port_7FFD_LockOnlyWithEFF7Lockmem)
{
    // xpeccy pentevo.c evoOut7FFD: `if ((pEFF7 & 4) && (p7FFD & 0x20)) return;`
    // - the 7FFD lock bit only counts while EFF7 lockmem holds the manager in
    // 128K mode. With lockmem clear (P1024 mode) 7FFD stays writable - bits
    // 5..7 then extend the RAM page number, so a sticky latch would brick
    // the machine after the first P1024 lock write
    EmulatorState& state = _context->emulatorState;
    state.flags = 0x00;

    // lockmem clear: a set lock bit does NOT block (P1024 page extension)
    state.pEFF7 = 0x00;
    state.p7FFD = 0x20;  // lock bit already set
    _portDecoder->DecodePortOut(0x7FFD, 0x21, 0x0000);
    EXPECT_EQ(state.p7FFD, 0x21);

    // lockmem set: the lock bit now blocks further writes
    state.pEFF7 = PortDecoder_ATM3::ATM_EFF7_LOCKMEM;
    state.p7FFD = 0x20;
    _portDecoder->DecodePortOut(0x7FFD, 0x00, 0x0000);
    EXPECT_EQ(state.p7FFD, 0x20) << "EFF7 lockmem + 7FFD.5 blocks further writes";

    // clearing lockmem through EFF7 reopens it (the lock bit cannot clear
    // itself, but the EFF7 condition can)
    state.pEFF7 = 0x00;
    _portDecoder->DecodePortOut(0x7FFD, 0x00, 0x0000);
    EXPECT_EQ(state.p7FFD, 0x00);
}

TEST_F(PortDecoder_ATM3_Test, PortBE_ReadbackRegisters_LegacyFpga)
{
    // Legacy BaseConf tree: the Evo registers read on #xxBE, index A12..A8
    // (fpga/baseconf/trunk zports.v portbemux):
    //   0x0B = pEFF7
    //   0x0D = the palette cell the 4-bit border points at, bits 2,3 read
    //        back as 1 (the FPGA round-trips to exactly (raw & 0xF3) | 0x0C)
    //   0x0F = the last #FE border color incl. the A3 bright bit
    _context->config.atm.evo_legacy_fpga = 1;
    EmulatorState& state = _context->emulatorState;
    state.flags = 0x00;
    state.aFF77 = 0x0000;  // manager open at reset
    state.border_attr = 0x04;
    state.atmBorderBright = 1;  // cell = 4 | (1 << 3) = 12
    state.atmPaletteRegs[12] = 0xA5;
    state.pEFF7 = 0x5A;

    EXPECT_EQ(_portDecoder->DecodePortIn(0x0BBE, 0x0000), 0x5A) << "#BE.0B = pEFF7";
    EXPECT_EQ(_portDecoder->DecodePortIn(0x0DBE, 0x0000), 0xAD) << "#BE.0D = (0xA5 & 0xF3) | 0x0C";
    EXPECT_EQ(_portDecoder->DecodePortIn(0x0FBE, 0x0000), 0x0C) << "#BE.0F = border | bright << 3";
    EXPECT_EQ(_portDecoder->DecodePortIn(0x0BBD, 0x0000), 0xFF) << "#xxBD is write-only on the legacy tree";
    EXPECT_EQ(_portDecoder->DecodePortIn(0x13BE, 0x0000), 0xFF) << "no virtual-drive mask on the legacy tree";
}

/// Current (trdemu) tree: the same register table moved to #xxBD and #xxBE
/// became write-only (git 663b8cf2 "removed completely xxBE read ports")
TEST_F(PortDecoder_ATM3_Test, PortBD_ReadbackRegisters_TrdemuFpga)
{
    EmulatorState& state = _context->emulatorState;
    ASSERT_EQ(_context->config.atm.evo_legacy_fpga, 0) << "trdemu is the default";
    state.pEFF7 = 0x5A;

    EXPECT_EQ(_portDecoder->DecodePortIn(0x0BBD, 0x0000), 0x5A);
    EXPECT_EQ(_portDecoder->DecodePortIn(0x0BBE, 0x0000), 0xFF) << "#xxBE has no read path on the current tree";
    EXPECT_EQ(_portDecoder->DecodePortIn(0xEBBD, 0x0000), 0x5A) << "only A12..A8 select the register";
}

/// Every index of the table (zports.v portbdmux)
TEST_F(PortDecoder_ATM3_Test, EvoRegisterTable_AllIndices)
{
    EmulatorState& state = _context->emulatorState;
    state.pFFF7[0] = 0x0305;  // ROM, page 5 from the register
    state.pFFF7[1] = 0x0240;  // RAM, page 0x40 from the register
    state.pFFF7[4] = 0x0100;  // ROM with dos7ffd (map 1)
    state.pFFF7[5] = 0x0003;  // RAM with dos7ffd (map 1)
    state.pFFF7[2] = 0x0200;
    state.pFFF7[3] = 0x0200;
    state.pFFF7[6] = 0x0200;
    state.pFFF7[7] = 0x0200;
    state.p7FFD = 0x17;
    state.pEFF7 = 0x84;
    state.aFF77 = PortDecoder_ATM3::ATM_AFF77_PEN2 | PortDecoder_ATM3::ATM_AFF77_PEN;  // A14=1, A9=0, A8=1
    state.pFF77 = 0x0B;                                                                // turbo + mode 3
    state.flags = CF_TRDOS;
    state.pBD = 0x1234;
    state.evoFddMask = 0x06;

    auto rd = [this](uint8_t index) { return _portDecoder->DecodePortIn(static_cast<uint16_t>((index << 8) | 0xBD), 0x0000); };

    EXPECT_EQ(rd(0x00), 0xFA) << "page register reads back as written to #x7F7 (~page)";
    EXPECT_EQ(rd(0x01), 0xBF);
    EXPECT_EQ(rd(0x08), 0xEE) << "RAM bits: windows 1,2,3 (map 0) and 5,6,7 (map 1)";
    EXPECT_EQ(rd(0x09), 0x30) << "dos7ffd bits: map-1 windows 0 and 1";
    EXPECT_EQ(rd(0x0A), 0x17);
    EXPECT_EQ(rd(0x0B), 0x84);
    EXPECT_EQ(rd(0x0C), 0x80 | 0x20 | 0x10 | 0x0B) << "{~pen2=A14, cpm_n=A9, ~pen=A8, DOS, turbo, mode}";
    EXPECT_EQ(rd(0x10), 0x34);
    EXPECT_EQ(rd(0x11), 0x12);
    EXPECT_EQ(rd(0x12), 0x00) << "no window is write-protected (#xBF7 lands in plan E8)";
    EXPECT_EQ(rd(0x13), 0x06);
    EXPECT_EQ(rd(0x0E), 0xFF) << "font RAM readback lands in plan E8";
    EXPECT_EQ(rd(0x14), 0xFF) << "undefined index";

    state.flags = 0;
    EXPECT_EQ(rd(0x0C) & 0x10, 0x00) << "bit 4 is the live DOS signal, not pFF77 bit 4";
}

/// Breakpoint address: #10BD / #11BD on the current tree (A12..A9 = 8, byte by
/// A8, zports.v:504-512); any #xxBD with A8 selecting the byte on the legacy one
TEST_F(PortDecoder_ATM3_Test, BreakpointAddressWrites_BothTrees)
{
    EmulatorState& state = _context->emulatorState;

    state.pBD = 0x0000;
    _portDecoder->DecodePortOut(0x10BD, 0x34, 0x0000);
    _portDecoder->DecodePortOut(0x11BD, 0x12, 0x0000);
    EXPECT_EQ(state.pBD, 0x1234);
    _portDecoder->DecodePortOut(0x00BD, 0x99, 0x0000);
    EXPECT_EQ(state.pBD, 0x1234) << "#00BD is not a breakpoint register on the current tree";
    _portDecoder->DecodePortOut(0xF0BD, 0x56, 0x0000);
    EXPECT_EQ(state.pBD, 0x1256) << "A15..A13 are not decoded: #F0BD = #10BD";

    _context->config.atm.evo_legacy_fpga = 1;
    state.pBD = 0x0000;
    _portDecoder->DecodePortOut(0x00BD, 0x78, 0x0000);
    _portDecoder->DecodePortOut(0x01BD, 0x56, 0x0000);
    EXPECT_EQ(state.pBD, 0x5678);
    _portDecoder->DecodePortOut(0x2ABD, 0x11, 0x0000);  // A8=0: low byte, rest undecoded
    EXPECT_EQ(state.pBD, 0x5611);
    EXPECT_EQ(_portDecoder->DecodePortIn(0x10BE, 0x0000), 0x11) << "legacy readback #10BE / #11BE";
    EXPECT_EQ(_portDecoder->DecodePortIn(0x11BE, 0x0000), 0x56);
}

/// #13BD virtual-drive mask: 4 bits, read/write, cleared by reset; the legacy
/// tree has no such register (zports.v:519-525)
TEST_F(PortDecoder_ATM3_Test, FddMask13BD_ReadWriteResetAndLegacyAbsent)
{
    EmulatorState& state = _context->emulatorState;

    _portDecoder->DecodePortOut(0x13BD, 0xFA, 0x0000);
    EXPECT_EQ(state.evoFddMask, 0x0A);
    EXPECT_EQ(_portDecoder->DecodePortIn(0x13BD, 0x0000), 0x0A) << "the ERS FPGA check writes %1010 and reads it back";

    _portDecoder->reset();
    EXPECT_EQ(state.evoFddMask, 0x00);

    _context->config.atm.evo_legacy_fpga = 1;
    _portDecoder->DecodePortOut(0x13BD, 0x05, 0x0000);  // a breakpoint write on the legacy tree
    EXPECT_EQ(state.evoFddMask, 0x00);
}

/// #xxBF reads back only the defined bits: 5..0 (bit 5 = 4:4:4 palette) on
/// the current tree, 4..0 on the legacy one (zports.v:466-468)
TEST_F(PortDecoder_ATM3_Test, PortBF_ReadbackMasksUndefinedBits)
{
    EmulatorState& state = _context->emulatorState;
    state.pBF = 0xFF;
    EXPECT_EQ(_portDecoder->DecodePortIn(0x00BF, 0x0000), 0x3F);
    _context->config.atm.evo_legacy_fpga = 1;
    EXPECT_EQ(_portDecoder->DecodePortIn(0x00BF, 0x0000), 0x1F);
}

/// endregion </ATM palette port #FF tests - exact decode and manager gate>

/// region <General Sound port delegation tests>

namespace
{
    // Minimal PortDevice registered under the canonical GS keys the same way
    // SoundManager::attachToPorts registers SoundChip_GeneralSound
    class GsPortMockDevice : public PortDevice
    {
    public:
        uint8_t portDeviceInMethod(uint16_t port) override
        {
            lastPort = port;
            return static_cast<uint8_t>(port ^ 0xFF);
        }

        void portDeviceOutMethod(uint16_t port, uint8_t value) override
        {
            lastPort = port;
            lastValue = value;
        }

        uint16_t lastPort = 0;
        uint8_t lastValue = 0;
    };
}  // namespace

TEST_F(PortDecoder_ATM3_Test, GSHostPortsReachBaseDecodeThroughOverrides)
{
    // The ATM3 overrides (#57 Z-Controller, #xBE/#xBF, CMOS windows, the
    // memory-manager gate) must not swallow the GS family: the arms live in
    // the ATM710 base decode and are reached through the delegation tails
    // of DecodePortIn/DecodePortOut
    GsPortMockDevice gs;
    ASSERT_TRUE(_portDecoder->RegisterPortHandler(0x00B3, &gs, static_cast<PortTagSet>(PortTag::SoundGs)));
    ASSERT_TRUE(_portDecoder->RegisterPortHandler(0x00BB, &gs, static_cast<PortTagSet>(PortTag::SoundGs)));
    ASSERT_TRUE(_portDecoder->RegisterPortHandler(0x0033, &gs, static_cast<PortTagSet>(PortTag::SoundGs)));

    // Manager gate open (aFF77=0 at reset): the x7F7/xx77/xFF7 group stays
    // hungry but must leave the GS family alone
    EmulatorState& state = _context->emulatorState;
    state.aFF77 = 0x0000;

    EXPECT_EQ(_portDecoder->DecodePortIn(0x02B3, 0x0000), 0x4C) << "#02B3 read answers through key #00B3";
    EXPECT_EQ(gs.lastPort, 0x00B3);

    _portDecoder->DecodePortOut(0x01BB, 0xC3, 0x0000);
    EXPECT_EQ(gs.lastPort, 0x00BB);
    EXPECT_EQ(gs.lastValue, 0xC3);
    _portDecoder->DecodePortOut(0x0033, 0x80, 0x0000);
    EXPECT_EQ(gs.lastPort, 0x0033);
    EXPECT_EQ(gs.lastValue, 0x80);
}

/// endregion </General Sound port delegation tests>

/// region <BaseConf decode (ZX-Evo plan phase E0)>

namespace
{
    /// The FPGA "porthit" predicate, transcribed from
    /// pentevo fpga/base_trdemu/trunk/z80/zports.v:331-359: true when the
    /// mainboard owns the I/O cycle (the ZX-Bus cards never see it)
    bool RtlPortHit(uint8_t loa, bool shadow)
    {
        const bool nideRegs = (loa & 0x07) == 0 && (((loa >> 3) & 1) != ((loa >> 4) & 1));
        const bool nide = nideRegs || loa == 0x11;
        return loa == 0xFE || loa == 0xF6 || loa == 0xFD || loa == 0xFC || nide || loa == 0xDF ||
               ((loa == 0x1F || loa == 0x3F || loa == 0x5F || loa == 0x7F || loa == 0xFF) && shadow) ||
               (loa == 0x1F && !shadow) || (loa == 0xF7 && !shadow) || (loa == 0x77 && !shadow) || loa == 0x57 ||
               (loa == 0xF7 && shadow) || (loa == 0x77 && shadow) ||
               loa == 0xBF || loa == 0xBE || loa == 0xBD || loa == 0xEF || loa == 0x3B;
    }

    void SetShadow(EmulatorState& state, bool on)
    {
        // Shadow = TR-DOS (DOS line) or #BF bit 0. CP/M set (A9=1) keeps the DOS
        // line from being forced, PEN set keeps the pager on
        state.aFF77 = PortDecoder_ATM3::ATM_AFF77_PEN | PortDecoder_ATM3::ATM_AFF77_CPM;
        state.flags &= ~CF_TRDOS;
        state.pBF = on ? 0x01 : 0x00;
    }
}  // namespace

/// DEC-1: every one of the 65 536 ports, in and out of shadow, in both
/// directions, lands on exactly the arm the FPGA porthit list gives it. The
/// old ATM710-inherited decode matched #FE on A0 alone, which made every even
/// port - all NemoIDE ports included - a border/beeper write
TEST_F(PortDecoder_ATM3_Test, Sweep_EveryPortMatchesFpgaPortHit)
{
    using Arm = PortDecoder_ATM3::PortArm;
    EmulatorState& state = _context->emulatorState;

    for (bool shadow : {false, true})
    {
        SetShadow(state, shadow);
        ASSERT_EQ(_portDecoder->IsManagerEnabled(), shadow);

        for (uint32_t p = 0; p <= 0xFFFF; p++)
        {
            const uint16_t port = static_cast<uint16_t>(p);
            const uint8_t loa = static_cast<uint8_t>(port & 0xFF);
            for (bool isWrite : {false, true})
            {
                const Arm arm = _portDecoder->ClassifyPort(port, isWrite);
                const bool mainboard = arm != Arm::ZxBus && arm != Arm::Covox;
                if (mainboard != RtlPortHit(loa, shadow))
                {
                    ADD_FAILURE() << "port #" << std::hex << port << (isWrite ? " write" : " read")
                                  << (shadow ? " in" : " outside") << " shadow: arm " << std::dec
                                  << static_cast<int>(arm);
                    return;  // one diagnostic is enough; the sweep would repeat it per alias
                }

                const bool border = arm == Arm::KeyboardBorder || arm == Arm::BorderAnd7FFD;
                if (border != (loa == 0xFE || loa == 0xF6 || loa == 0xFC))
                {
                    ADD_FAILURE() << "port #" << std::hex << port << " border arm mismatch";
                    return;
                }
            }
        }
    }
}

/// DEC-1b: the ports the next phases wire (NemoIDE, SD, ULA+, RS-232, #BD)
/// are claimed by the board already, so nothing leaks to border or ZX-Bus
TEST_F(PortDecoder_ATM3_Test, BoardPortsReservedForLaterPhases)
{
    using Arm = PortDecoder_ATM3::PortArm;
    for (uint16_t port : {0x0010, 0x0011, 0x0030, 0x0050, 0x0070, 0x0090, 0x00B0, 0x00D0, 0x00F0, 0x00C8, 0x0008, 0x00E8})
        EXPECT_EQ(_portDecoder->ClassifyPort(port, true), Arm::NemoIde) << std::hex << port;
    for (uint16_t port : {0x0018, 0x0038, 0x0020, 0x00C0})
        EXPECT_NE(_portDecoder->ClassifyPort(port, true), Arm::NemoIde) << std::hex << port << " is not an IDE register";

    EXPECT_EQ(_portDecoder->ClassifyPort(0x0057, false), Arm::SdData);
    EXPECT_EQ(_portDecoder->ClassifyPort(0xBF3B, true), Arm::UlaPlus);
    EXPECT_EQ(_portDecoder->ClassifyPort(0xF8EF, true), Arm::ComPort);
    EXPECT_EQ(_portDecoder->ClassifyPort(0x13BD, true), Arm::EvoReadback);
}

/// The FDC answers only in shadow and only on its exact low bytes; outside
/// shadow #1F is the Kempston joystick (zports.v:342, :444-445)
TEST_F(PortDecoder_ATM3_Test, Fdc_OnlyInShadow_JoystickOutside)
{
    GsPortMockDevice fdc;  // any recording PortDevice will do
    ASSERT_TRUE(_portDecoder->RegisterPortHandler(0x001F, &fdc, static_cast<PortTagSet>(PortTag::StorageFdc)));
    EmulatorState& state = _context->emulatorState;

    SetShadow(state, true);
    fdc.lastPort = 0;
    EXPECT_EQ(_portDecoder->DecodePortIn(0x001F, 0x0000), static_cast<uint8_t>(0x1F ^ 0xFF));
    EXPECT_EQ(fdc.lastPort, 0x001F) << "WD1793 status in shadow";

    fdc.lastPort = 0;
    _portDecoder->DecodePortOut(0x000F, 0x08, 0x0000);
    EXPECT_EQ(fdc.lastPort, 0) << "#0F is not an FDC port on BaseConf (exact low-byte decode)";

    SetShadow(state, false);
    fdc.lastPort = 0;
    EXPECT_EQ(_portDecoder->DecodePortIn(0x001F, 0x0000), 0x00) << "Kempston joystick, nothing pressed";
    EXPECT_TRUE(_portDecoder->WasLastPortDecoded());
    _portDecoder->DecodePortOut(0x001F, 0xD0, 0x0000);
    EXPECT_EQ(fdc.lastPort, 0) << "the WD1793 must not see #1F outside shadow";
}

/// #EFF7 is written only outside shadow, on any #F7 port with A8=1 and A12=0;
/// it is write-only (no read mux entry) - zports.v:490-491, :714-720
TEST_F(PortDecoder_ATM3_Test, Eff7_WrittenOnlyOutsideShadow_WriteOnly)
{
    EmulatorState& state = _context->emulatorState;

    SetShadow(state, false);
    _portDecoder->DecodePortOut(0xEFF7, 0x10, 0x0000);
    EXPECT_EQ(state.pEFF7, 0x10);
    _portDecoder->DecodePortOut(0xE1F7, 0x14, 0x0000);
    EXPECT_EQ(state.pEFF7, 0x14) << "A12=0 alias";
    EXPECT_EQ(_portDecoder->DecodePortIn(0xEFF7, 0x0000), 0xFF) << "#EFF7 has no read path";

    SetShadow(state, true);
    state.pFFF7[3] = 0x0000;
    _portDecoder->DecodePortOut(0xEFF7, 0x00, 0x0000);
    EXPECT_EQ(state.pEFF7, 0x14) << "in shadow #EFF7 is ignored";
    EXPECT_EQ(state.pFFF7[3], 0x033F) << "... and reaches pager window 3 instead (value 0 -> ROM, page 0x3F)";
}

/// The Gluk clock needs #EFF7 bit 7 outside shadow and is always on in shadow,
/// where it moves to the A8=0 aliases #DEF7 / #BEF7 (zports.v:455-460, :739)
TEST_F(PortDecoder_ATM3_Test, Gluk_GatedByEff7Bit7OutsideShadow)
{
    EmulatorState& state = _context->emulatorState;
    CMOS& cmos = _portDecoder->GetCMOS();
    cmos.SetCMOSType(Dallas);

    SetShadow(state, false);
    state.pEFF7 = 0x00;
    cmos.SetCMOSAddress(0x20);
    _portDecoder->DecodePortOut(0xDFF7, 0x30, 0x0000);
    EXPECT_EQ(cmos.GetCMOSAddress(), 0x20) << "clock ports closed until #EFF7 bit 7";
    EXPECT_EQ(_portDecoder->DecodePortIn(0xBFF7, 0x0000), 0xFF);

    _portDecoder->DecodePortOut(0xEFF7, 0x80, 0x0000);
    _portDecoder->DecodePortOut(0xDFF7, 0x30, 0x0000);
    EXPECT_EQ(cmos.GetCMOSAddress(), 0x30);
    _portDecoder->DecodePortOut(0xBFF7, 0x5A, 0x0000);
    EXPECT_EQ(_portDecoder->DecodePortIn(0xBFF7, 0x0000), 0x5A);

    SetShadow(state, true);
    state.pEFF7 = 0x00;
    _portDecoder->DecodePortOut(0xDEF7, 0x31, 0x0000);
    EXPECT_EQ(cmos.GetCMOSAddress(), 0x31) << "#DEF7 in shadow, no #EFF7 bit 7 needed";
    _portDecoder->DecodePortOut(0xBEF7, 0xA5, 0x0000);
    EXPECT_EQ(_portDecoder->DecodePortIn(0xBEF7, 0x0000), 0xA5);
    EXPECT_EQ(_portDecoder->DecodePortIn(0xBFF7, 0x0000), 0xFF) << "#BFF7 has A8=1: in shadow it is not the clock";
}

/// Pentagon-1024 mode (#EFF7 bit 2 = 0, the reset state) takes page bits 5:3
/// from #7FFD bits 7:5; 128K mode only bits 2:0 (atm_pager.v:147-156)
TEST_F(PortDecoder_ATM3_Test, Mapping_7FFDPageBits_1MegVs128KMode)
{
    EmulatorState& state = _context->emulatorState;
    _context->config.ramsize = 4096;
    SetShadow(state, false);
    state.p7FFD = 0xE3;          // bits 7:5 = 111, bits 2:0 = 011, map 0
    state.pFFF7[3] = 0x0040;     // RAM, page bits from #7FFD, register page 0x40

    state.pEFF7 = 0x00;
    _memory->UpdateZ80Banks();
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 0x7B) << "{reg[7:6], 7FFD[7:5], 7FFD[2:0]} = 01 111 011";

    state.pEFF7 = PortDecoder_ATM3::ATM_EFF7_LOCKMEM;
    _memory->UpdateZ80Banks();
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 0x43) << "{reg[7:3], 7FFD[2:0]} = 01000 011";
}

/// #EFF7 bit 3 puts RAM page 0 at #0000 over the page register; pager off
/// (#xx77 A8=0) still wins (atm_pager.v:114-137)
TEST_F(PortDecoder_ATM3_Test, Mapping_Eff7Bit3_Ram0AtWindow0)
{
    EmulatorState& state = _context->emulatorState;
    _context->config.ramsize = 4096;
    SetShadow(state, false);
    state.p7FFD = 0x00;
    state.pFFF7[0] = 0x0301;  // ROM page 1

    state.pEFF7 = PortDecoder_ATM3::ATM_EFF7_ROCACHE;
    _memory->UpdateZ80Banks();
    EXPECT_EQ(_memory->GetMemoryBankMode(0), MemoryBankModeEnum::BANK_RAM);
    EXPECT_EQ(_memory->GetRAMPageForBank0(), 0);

    state.aFF77 = PortDecoder_ATM3::ATM_AFF77_CPM;  // PEN off
    _memory->UpdateZ80Banks();
    EXPECT_EQ(_memory->GetMemoryBankMode(0), MemoryBankModeEnum::BANK_ROM) << "pager off beats #EFF7 bit 3";
}

/// Z-Controller: #77 reads #00 outside shadow ("card inserted, R/W" -
/// zports.v:449-450); #57 reads #FF while no SD card model exists
TEST_F(PortDecoder_ATM3_Test, ZController_ConfigReadsZero_DataIdle)
{
    EmulatorState& state = _context->emulatorState;
    SetShadow(state, false);
    EXPECT_EQ(_portDecoder->DecodePortIn(0x0077, 0x0000), 0x00);
    EXPECT_EQ(_portDecoder->DecodePortIn(0x0057, 0x0000), 0xFF);
    EXPECT_TRUE(_portDecoder->WasLastPortDecoded());
}

namespace
{
    class SelfDecodingMock : public PortDevice
    {
    public:
        uint8_t portDeviceInMethod(uint16_t) override { return 0xFF; }
        void portDeviceOutMethod(uint16_t, uint8_t) override {}
        bool tryClaimOut(uint16_t rawPort, uint8_t value) override
        {
            lastPort = rawPort;
            lastValue = value;
            return true;
        }

        uint16_t lastPort = 0;
        uint8_t lastValue = 0;
    };
}  // namespace

/// Covox on #FB reaches the self-decoding Covox device (it never did on ATM3:
/// the ATM decoders did not dispatch self-decoding devices at all)
TEST_F(PortDecoder_ATM3_Test, Covox_FbReachesSelfDecodingDevice)
{
    SelfDecodingMock covox;
    ASSERT_TRUE(_portDecoder->RegisterSelfDecodingDevice(&covox));

    _portDecoder->DecodePortOut(0x00FB, 0x80, 0x0000);
    EXPECT_EQ(covox.lastPort, 0x00FB);
    EXPECT_EQ(covox.lastValue, 0x80);

    covox.lastPort = 0;
    _portDecoder->DecodePortOut(0x001F, 0x80, 0x0000);  // SounDrive mode-1 address: not on this board
    EXPECT_EQ(covox.lastPort, 0) << "only #FB exists on BaseConf";

    _portDecoder->UnregisterSelfDecodingDevice(&covox);
}

/// endregion </BaseConf decode (ZX-Evo plan phase E0)>

/// region <BaseConf full-stack tests (ZX-Evo plan phase E0)>

class PortDecoder_ATM3_Machine_Test : public ::testing::Test
{
protected:
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_ATM3* _decoder = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("", "ATM3", LoggerLevel::LogError);
        ASSERT_TRUE(_emulator);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
    }
};

/// The CPU leaves reset at 7 MHz (#EFF7 bit 4 = 0 and #xx77 bit 3 = 0 give
/// turbo = {0, ~0} = 7 MHz, top.v:401)
TEST_F(PortDecoder_ATM3_Machine_Test, Reset_Runs7MHz)
{
    EXPECT_EQ(_context->emulatorState.hw_turbo_shift, 1);
}

/// #F6 sets border colors 8-15 and leaves the beeper alone; #FC sets the
/// border and (A15=0) #7FFD (zports.v:533-538, :944)
TEST_F(PortDecoder_ATM3_Machine_Test, BorderPortsF6AndFC)
{
    EmulatorState& state = _context->emulatorState;

    _decoder->DecodePortOut(0x00FE, 0x10, 0x0000);  // border 0, beeper on
    ASSERT_EQ(state.pFE & 0x10, 0x10);

    _decoder->DecodePortOut(0x00F6, 0x05, 0x0000);
    EXPECT_EQ(state.border_attr, 0x05);
    EXPECT_EQ(state.atmBorderBright, 1) << "#F6 has A3=0: bright border half";
    EXPECT_EQ(state.pFE & 0x18, 0x10) << "#F6 must not touch beeper / MIC";

    _decoder->DecodePortOut(0x7FFC, 0x03, 0x0000);
    EXPECT_EQ(state.border_attr, 0x03);
    EXPECT_EQ(state.atmBorderBright, 0);
    EXPECT_EQ(state.p7FFD, 0x03) << "#FC with A15=0 also writes #7FFD";
    EXPECT_EQ(state.pFE & 0x18, 0x10) << "#FC does not drive the beeper";
}

/// Kempston mouse on #xxDF with the BaseConf sub-decode, not gated by TR-DOS
/// (zports.v:446-447, zkbdmus.v:118-120)
TEST_F(PortDecoder_ATM3_Machine_Test, KempstonMouse_Decoded)
{
    Mouse* mouse = _context->pMouse;
    ASSERT_NE(mouse, nullptr);
    mouse->SetPresent(true);
    mouse->SetCounters(0x40, 0x6A);

    _context->emulatorState.flags |= CF_TRDOS;  // TR-DOS active: still the mouse on this board
    EXPECT_EQ(_decoder->DecodePortIn(0xFBDF, 0x0000), 0x40);
    EXPECT_EQ(_decoder->DecodePortIn(0xFFDF, 0x0000), 0x6A);
    EXPECT_EQ(_decoder->DecodePortIn(0xFADF, 0x0000) & 0x07, 0x07) << "no buttons pressed (active low)";

    mouse->SetPresent(false);
    EXPECT_EQ(_decoder->DecodePortIn(0xFBDF, 0x0000), 0xFF) << "no mouse: the AVR answers #FF";
}

/// endregion </BaseConf full-stack tests (ZX-Evo plan phase E0)>

/// region <Board NMI (ZX-Evo plan phase E3)>

/// ZX-Evo board NMI (fpga/base_trdemu/trunk/z80/znmi.v, zbreak.v): sources
/// #BF bit 3 (1 -> 0 edge) and the Magic button - both released at the next
/// frame INT - and the M1 breakpoint (immediate); entry forces NOP at #0066 and
/// maps RAM page #FF into #0000-#3FFF; OUT (#BE) leaves after two more M1s
class ZXEvoNmi_Test : public ::testing::Test
{
protected:
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_ATM3* _decoder = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    unsigned _intStart = 0;
    unsigned _intEnd = 0;

    static constexpr uint16_t kReturnPc = 0x8123;  // interrupted code (window 2)
    static constexpr uint16_t kStack = 0xA000;

    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("", "ATM3", LoggerLevel::LogError);
        ASSERT_TRUE(_emulator);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
        _intStart = _context->config.intstart;
        _intEnd = _context->config.intstart + _context->config.intlen;

        // Pager on, CP/M off (no forced DOS), shadow off; window 0 = ROM page 28
        // (BASIC48), windows 1-3 = RAM 5 / 2 / 0 - map 0
        EmulatorState& state = _context->emulatorState;
        state.aFF77 = PortDecoder_ATM3::ATM_AFF77_PEN | PortDecoder_ATM3::ATM_AFF77_CPM;
        state.flags = 0;
        state.pBF = 0;
        state.p7FFD = 0;
        state.pEFF7 = PortDecoder_ATM3::ATM_EFF7_LOCKMEM;
        state.pFFF7[0] = 0x300 | 28;
        state.pFFF7[1] = 0x200 | 5;
        state.pFFF7[2] = 0x200 | 2;
        state.pFFF7[3] = 0x200 | 0;
        _memory->UpdateZ80Banks();

        _z80->pc = kReturnPc;
        _z80->sp = kStack;
        _z80->iff1 = _z80->iff2 = 0;
        _z80->halted = 0;
        _z80->boundary = Z80_BOUNDARY_NONE;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
    }

    /// Handler bytes at #0067 of the NMI page (RAM #FF)
    void InstallHandler(std::initializer_list<uint8_t> code)
    {
        uint8_t* page = _memory->RAMPageAddress(0xFF);
        ASSERT_NE(page, nullptr);
        uint16_t offset = 0x0067;
        for (uint8_t byte : code)
            page[offset++] = byte;
    }

    /// Offer the CPU a boundary at t; returns true when an NMI was accepted
    bool BoundaryAt(unsigned t)
    {
        _z80->t = t;
        _z80->boundary = Z80_BOUNDARY_NONE;
        const uint16_t pcBefore = _z80->pc;
        const bool handled = _z80->ProcessInterrupts(true, _intStart, _intEnd);
        return handled && _z80->pc != pcBefore && (_z80->pc == 0x0066 || _z80->pc == 0x0067);
    }

    bool NmiPageIn() const
    {
        return _memory->GetMemoryBankMode(0) == MemoryBankModeEnum::BANK_RAM && _memory->GetRAMPageForBank0() == 0xFF;
    }
};

/// NMI-1: a #BF bit-3 1 -> 0 edge waits for the frame INT, then enters at
/// #0067 (the board fed NOP to the #0066 fetch) with RAM #FF in window 0
TEST_F(ZXEvoNmi_Test, BfEdgeNmiWaitsForIntThenEntersPageFF)
{
    _decoder->DecodePortOut(0x00BF, 0x08, 0x0000);
    _decoder->DecodePortOut(0x00BF, 0x00, 0x0000);
    ASSERT_TRUE(_context->emulatorState.nmiAtIntStartPending);

    EXPECT_FALSE(BoundaryAt(_intEnd + 100)) << "not before the frame INT";

    const uint8_t r0 = _z80->r_low;
    const unsigned t0 = _intStart + 2;
    ASSERT_TRUE(BoundaryAt(t0));
    EXPECT_EQ(_z80->pc, 0x0067) << "the forced NOP at #0066 has run";
    EXPECT_EQ(_z80->t - t0, 11u + 4u) << "acknowledge (11T) + forced NOP M1 (4T)";
    EXPECT_EQ(static_cast<uint8_t>((_z80->r_low - r0) & 0x7F), 2) << "two refresh cycles: acknowledge + NOP";
    EXPECT_TRUE(NmiPageIn());
    EXPECT_TRUE(_context->emulatorState.evoInNmi);
    EXPECT_EQ(_z80->DirectRead(kStack - 1), kReturnPc >> 8);
    EXPECT_EQ(_z80->DirectRead(kStack - 2), kReturnPc & 0xFF);
}

/// NMI-2: OUT (#BE),A : RETN - RETN still runs from the NMI page (it is the
/// second M1 after the write), returns through the restored map
TEST_F(ZXEvoNmi_Test, ExitAfterTwoM1sRetnRunsFromPageFF)
{
    InstallHandler({0xD3, 0xBE, 0xED, 0x45});  // OUT (#BE),A : RETN
    ASSERT_NE(_memory->ROMPageHostAddress(28)[0x0069], 0xED) << "ROM must not hold RETN at #0069";

    _decoder->RequestBoardNmi();
    ASSERT_TRUE(BoundaryAt(_intStart + 2));

    _z80->Z80Step(true);  // OUT (#BE),A
    EXPECT_TRUE(NmiPageIn()) << "still in after the write";
    _z80->Z80Step(true);  // RETN (two M1s: ED, 45)
    EXPECT_EQ(_z80->pc, kReturnPc) << "RETN fetched from the NMI page";
    EXPECT_FALSE(_context->emulatorState.evoInNmi);
    EXPECT_EQ(_memory->GetMemoryBankMode(0), MemoryBankModeEnum::BANK_ROM);
    EXPECT_EQ(_memory->GetROMPage(), 28u);
}

/// NMI-2b: the two M1s are counted, not instructions: OUT (#BE) : NOP : RET
/// runs both NOP and RET from the NMI page
TEST_F(ZXEvoNmi_Test, ExitCountsM1sNotInstructions)
{
    InstallHandler({0xD3, 0xBE, 0x00, 0xC9});  // OUT (#BE),A : NOP : RET
    _decoder->RequestBoardNmi();
    ASSERT_TRUE(BoundaryAt(_intStart + 2));

    _z80->Z80Step(true);  // OUT
    _z80->Z80Step(true);  // NOP (M1 #1)
    EXPECT_TRUE(NmiPageIn());
    _z80->Z80Step(true);  // RET (M1 #2: fetched from #FF, page leaves at its refresh)
    EXPECT_EQ(_z80->pc, kReturnPc);
    EXPECT_FALSE(_context->emulatorState.evoInNmi);
}

/// NMI-3: the M1 breakpoint fires immediately (no INT wait) on every pass
TEST_F(ZXEvoNmi_Test, BreakpointNmiIsImmediateAndStaysArmed)
{
    InstallHandler({0xD3, 0xBE, 0xED, 0x45});
    uint8_t* code = _memory->RAMPageAddress(2);  // window 2 = #8000
    code[0x0123] = 0x00;                          // NOP at kReturnPc
    code[0x0124] = 0x18;                          // JR -3 -> back to #8123
    code[0x0125] = 0xFD;

    _decoder->DecodePortOut(0x10BD, kReturnPc & 0xFF, 0x0000);
    _decoder->DecodePortOut(0x11BD, kReturnPc >> 8, 0x0000);
    _decoder->DecodePortOut(0x00BF, 0x10, 0x0000);  // breakpoint enable

    for (int pass = 0; pass < 2; pass++)
    {
        _z80->t = _intEnd + 200;  // far from the frame INT
        _z80->Z80Step(true);      // NOP at the breakpoint address: M1 compare
        EXPECT_TRUE(BoundaryAt(_intEnd + 210)) << "pass " << pass << ": immediate NMI";
        EXPECT_TRUE(NmiPageIn());
        _z80->Z80Step(true);  // OUT (#BE)
        _z80->Z80Step(true);  // RETN -> #8124
        EXPECT_EQ(_z80->pc, 0x8124);
        _z80->Z80Step(true);  // JR back to the breakpoint
        ASSERT_EQ(_z80->pc, kReturnPc);
    }
}

/// NMI-4: the Magic button on ZX-Evo is the board's INT-synchronized NMI
TEST_F(ZXEvoNmi_Test, MagicButtonIsIntSynchronized)
{
    _emulator->RequestMNI();
    EXPECT_FALSE(BoundaryAt(_intEnd + 100)) << "the AVR NMI waits for the frame INT";
    ASSERT_TRUE(BoundaryAt(_intStart + 2));
    EXPECT_EQ(_z80->pc, 0x0067);
    EXPECT_TRUE(NmiPageIn());
}

/// NMI-5: no nested board NMI while the NMI page is in (nmi_start && !in_nmi)
TEST_F(ZXEvoNmi_Test, NoNestedBoardNmi)
{
    _decoder->RequestBoardNmi();
    ASSERT_TRUE(BoundaryAt(_intStart + 2));
    _z80->pc = 0x0067;

    _decoder->RequestBoardNmi();
    EXPECT_FALSE(BoundaryAt(_intStart + 4)) << "vetoed while in the NMI page";
    EXPECT_FALSE(_context->emulatorState.nmiAtIntStartPending) << "the request is consumed, not kept";
}

/// NMI-6: an /NMI that did not come from the board is a plain Z80 NMI: #0066
/// of whatever is mapped, no page switch
TEST_F(ZXEvoNmi_Test, PlainNmiDoesNotSwitchPages)
{
    _z80->RequestNonMaskedInterrupt();
    ASSERT_TRUE(BoundaryAt(_intEnd + 100));
    EXPECT_EQ(_z80->pc, 0x0066);
    EXPECT_FALSE(NmiPageIn());
    EXPECT_EQ(_memory->GetROMPage(), 28u);
}

/// DOS-1: executing the NMI handler (RAM #FF over a window programmed as ROM)
/// keeps the DOS signal on; a window programmed as RAM closes it (atm_pager.v ram_exec_stb)
TEST_F(ZXEvoNmi_Test, DosStaysOnInsideTheNmiPage)
{
    InstallHandler({0x00, 0x00});  // NOP : NOP
    EmulatorState& state = _context->emulatorState;
    state.flags |= CF_TRDOS;
    _memory->UpdateZ80Banks();
    ASSERT_TRUE(state.flags & CF_LEAVEDOSRAM);

    _decoder->RequestBoardNmi();
    ASSERT_TRUE(BoundaryAt(_intStart + 2));
    _z80->Z80Step(true);
    EXPECT_TRUE(state.flags & CF_TRDOS) << "the programmed window 0 is ROM: DOS stays on";

    state.pFFF7[0] = 0x200 | 7;  // now program window 0 as RAM
    state.evoInNmi = false;
    _memory->UpdateZ80Banks();
    _z80->pc = 0x0000;
    _memory->RAMPageAddress(7)[0] = 0x00;
    _z80->Z80Step(true);
    EXPECT_FALSE(state.flags & CF_TRDOS) << "execution from a window programmed as RAM closes DOS";
}

/// endregion </Board NMI>
