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
    // Same formula as ATM710 (ZX-Evo BaseConf hardware, Xpeccy pentevo.c):
    // FF77.3 -> x4 (14MHz), EFF7.4 -> x1 (3.5MHz), else x2 (7MHz)
    EmulatorState& state = _context->emulatorState;

    // Open the memory-manager gate: the first FF77 write latches cpm in
    // aFF77 (see Port_FF77_Out_ATM3), which would otherwise swallow the write
    state.pBF = 0x01;

    // FF77 bit 3 -> 14MHz
    _portDecoder->DecodePortOut(0xFF77, 0x08, 0x0000);
    EXPECT_EQ(state.next_z80_frequency_multiplier, 4);

    // Turbo off, EFF7.4 clear -> 7MHz
    _portDecoder->DecodePortOut(0xFF77, 0x00, 0x0000);
    EXPECT_EQ(state.next_z80_frequency_multiplier, 2);

    // EFF7 bit 4 -> 3.5MHz lock
    _portDecoder->DecodePortOut(0xEFF7, 0x10, 0x0000);
    EXPECT_EQ(state.next_z80_frequency_multiplier, 1);
}

/// endregion </Turbo mode tests>
