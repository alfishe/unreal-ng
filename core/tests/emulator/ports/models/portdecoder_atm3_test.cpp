#include "stdafx.h"
#include "pch.h"

#include "portdecoder_atm3_test.h"

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
    // Port: #37F7 (4MB Memory Manager)
    // Full 14-bit decode: (port & 0x3FFF) == 0x37F7

    EXPECT_TRUE(_portDecoder->IsPort_37F7(0x37F7));
    EXPECT_TRUE(_portDecoder->IsPort_37F7(0xB7F7));  // High bits don't matter
    EXPECT_TRUE(_portDecoder->IsPort_37F7(0xF7F7));

    // Should NOT match
    EXPECT_FALSE(_portDecoder->IsPort_37F7(0x17F7));
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

TEST_F(PortDecoder_ATM3_Test, InheritsPort_7FFD)
{
    // ATM3 should inherit 7FFD decoding from ATM710
    static const uint16_t mask_7FFD  = 0b1000'0000'0000'0110;
    static const uint16_t match_7FFD = 0b0000'0000'0000'0100;

    EXPECT_TRUE(_portDecoder->IsPort_7FFD(0x7FFD));
    EXPECT_TRUE(_portDecoder->IsPort_7FFD(0x7FF5));  // A2=1, A1=0
    EXPECT_FALSE(_portDecoder->IsPort_7FFD(0xFFFF));  // A15=1
}

TEST_F(PortDecoder_ATM3_Test, InheritsPort_EFF7)
{
    // ATM3 should inherit EFF7 decoding from ATM710
    EXPECT_TRUE(_portDecoder->IsPort_EFF7(0xEFF7));
    EXPECT_FALSE(_portDecoder->IsPort_EFF7(0xEFF6));
}

TEST_F(PortDecoder_ATM3_Test, IsPort_FFF7_NarrowerDecode)
{
    // ATM3 xFF7 decode is narrower than ATM710: A13:A12 must be set as well
    // (mask 0x3FFF, match 0x3FF7)
    uint8_t windowIndex;

    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0x3FF7, windowIndex));
    EXPECT_EQ(windowIndex, 0);

    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0x7FF7, windowIndex));
    EXPECT_EQ(windowIndex, 1);

    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0xBFF7, windowIndex));
    EXPECT_EQ(windowIndex, 2);

    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0xFFF7, windowIndex));
    EXPECT_EQ(windowIndex, 3);

    // Should NOT match: low byte F7 but A13:A12 not both set (these match on ATM710)
    EXPECT_FALSE(_portDecoder->IsPort_FFF7(0x00F7, windowIndex));
    EXPECT_FALSE(_portDecoder->IsPort_FFF7(0x17F7, windowIndex));
    EXPECT_FALSE(_portDecoder->IsPort_FFF7(0x27F7, windowIndex));
    EXPECT_FALSE(_portDecoder->IsPort_FFF7(0x37F7, windowIndex));  // 37F7 has its own handler
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
    state.nmi_in_progress = true;
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

    _portDecoder->DecodePortOut(0xEFF7, 0x10, 0x0000);
    EXPECT_EQ(state.hw_turbo_shift, 0) << "pEFF7.4 locks 3.5 MHz";

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

TEST_F(PortDecoder_ATM3_Test, PortBE_ReadbackRegisters)
{
    // #BE readback selected by A15..A8 (original io.cpp in(), MM_ATM3):
    //   0x0B = pEFF7 (xpeccy evoInCfg case 0x0b00)
    //   0x0D = the palette cell the 4-bit border points at, bits 2,3 read
    //        back as 1 (xpeccy case 0x0d00; the FPGA zports.v portbemux 5'hD
    //        round-trips to exactly (raw & 0xF3) | 0x0C)
    //   0x0F = the last #FE border color incl. the A3 bright bit
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
}

/// endregion </ATM palette port #FF tests - exact decode and manager gate>
