#include "stdafx.h"
#include "pch.h"

#include "portdecoder_atm710_test.h"

/// region <SetUp / TearDown>

void PortDecoder_ATM710_Test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);

    // Memory must be attached before the decoder: PortDecoder caches the pointer in its constructor
    _memory = new Memory(_context);
    _context->pMemory = _memory;

    _portDecoder = new PortDecoder_ATM710(_context);
}

void PortDecoder_ATM710_Test::TearDown()
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
        delete _context;
        _context = nullptr;
    }
}

/// endregion </Setup / TearDown>

/// region <Port 7FFD tests>

TEST_F(PortDecoder_ATM710_Test, IsPort_7FFD)
{
    // Port: #7FFD
    // Sensitivity: 0xxxxxxx xxxxx10x (A15=0, A2=1, A1=0)
    // Same decoding as Pentagon/Spectrum128
    static const uint16_t mask_7FFD  = 0b1000'0000'0000'0110;  // A15, A2, A1
    static const uint16_t match_7FFD = 0b0000'0000'0000'0100;  // A15=0, A2=1, A1=0
    static char message[256];

    for (int i = 0; i <= 0xFFFF; i++)
    {
        uint16_t port = i & 0xFFFF;
        bool referenceIs_7FFD = ((port & mask_7FFD) == match_7FFD);
        bool is_7FFD = _portDecoder->IsPort_7FFD(port);

        if (referenceIs_7FFD != is_7FFD)
        {
            snprintf(message, sizeof message, "port: #%04X. Expected: %d, returned: %d", port, referenceIs_7FFD, is_7FFD);
            FAIL() << message << std::endl;
        }
    }
}

/// endregion </Port 7FFD tests>

/// region <Port FF77 tests>

TEST_F(PortDecoder_ATM710_Test, IsPort_FF77)
{
    // Port: #FF77 (ATM Control)
    // ATM710: Full decode of low byte
    // Mask: 0x00FF, Match: 0x0077

    // Should match
    EXPECT_TRUE(_portDecoder->IsPort_FF77(0xFF77));
    EXPECT_TRUE(_portDecoder->IsPort_FF77(0x0077));
    EXPECT_TRUE(_portDecoder->IsPort_FF77(0x1077));
    EXPECT_TRUE(_portDecoder->IsPort_FF77(0xAB77));

    // Should NOT match (different low byte)
    EXPECT_FALSE(_portDecoder->IsPort_FF77(0xFF76));
    EXPECT_FALSE(_portDecoder->IsPort_FF77(0xFF78));
    EXPECT_FALSE(_portDecoder->IsPort_FF77(0xFFFF));
    EXPECT_FALSE(_portDecoder->IsPort_FF77(0x00F7));
}

/// endregion </Port FF77 tests>

/// region <Port FFF7 tests>

TEST_F(PortDecoder_ATM710_Test, IsPort_FFF7)
{
    // Port: #xFFF7 group (memory manager registers)
    // Low byte = 0xF7, window index = A15:A14

    uint8_t windowIndex;

    // Window 0 (A15:A14 = 00) - any high byte with low byte F7
    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0x00F7, windowIndex));
    EXPECT_EQ(windowIndex, 0);
    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0x3FF7, windowIndex));
    EXPECT_EQ(windowIndex, 0);
    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0x17F7, windowIndex));
    EXPECT_EQ(windowIndex, 0);

    // Window 1 (A15:A14 = 01)
    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0x7FF7, windowIndex));
    EXPECT_EQ(windowIndex, 1);

    // Window 2 (A15:A14 = 10)
    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0xBFF7, windowIndex));
    EXPECT_EQ(windowIndex, 2);

    // Window 3 (A15:A14 = 11)
    EXPECT_TRUE(_portDecoder->IsPort_FFF7(0xFFF7, windowIndex));
    EXPECT_EQ(windowIndex, 3);

    // Should NOT match - wrong low byte
    EXPECT_FALSE(_portDecoder->IsPort_FFF7(0x3FF6, windowIndex));
    EXPECT_FALSE(_portDecoder->IsPort_FFF7(0x3FFF, windowIndex));
    EXPECT_FALSE(_portDecoder->IsPort_FFF7(0x00E7, windowIndex));
}

/// endregion </Port FFF7 tests>

/// region <Port EFF7 tests>

TEST_F(PortDecoder_ATM710_Test, IsPort_EFF7)
{
    // Port: #EFF7 (Extended Control)
    // Full decode

    EXPECT_TRUE(_portDecoder->IsPort_EFF7(0xEFF7));

    // Should NOT match
    EXPECT_FALSE(_portDecoder->IsPort_EFF7(0xEFF6));
    EXPECT_FALSE(_portDecoder->IsPort_EFF7(0xEFF8));
    EXPECT_FALSE(_portDecoder->IsPort_EFF7(0xFFFF));
    EXPECT_FALSE(_portDecoder->IsPort_EFF7(0x0FF7));
}

/// endregion </Port EFF7 tests>

/// region <Port FE tests>

TEST_F(PortDecoder_ATM710_Test, IsPort_FE)
{
    // Port: #FE (A0 = 0)

    EXPECT_TRUE(_portDecoder->IsPort_FE(0x00FE));
    EXPECT_TRUE(_portDecoder->IsPort_FE(0xFFFE));
    EXPECT_TRUE(_portDecoder->IsPort_FE(0x1234));  // Any even port

    EXPECT_FALSE(_portDecoder->IsPort_FE(0x00FF));  // A0 = 1
    EXPECT_FALSE(_portDecoder->IsPort_FE(0x0001));
}

/// endregion </Port FE tests>

/// region <AY port tests>

TEST_F(PortDecoder_ATM710_Test, IsPort_BFFD)
{
    // Port: #BFFD (AY Data)
    // A15=1, A14=0, A1=0

    EXPECT_TRUE(_portDecoder->IsPort_BFFD(0xBFFD));
    EXPECT_TRUE(_portDecoder->IsPort_BFFD(0x8000));  // Minimal match

    EXPECT_FALSE(_portDecoder->IsPort_BFFD(0xFFFF));  // A14=1
    EXPECT_FALSE(_portDecoder->IsPort_BFFD(0x3FFD));  // A15=0
}

TEST_F(PortDecoder_ATM710_Test, IsPort_FFFD)
{
    // Port: #FFFD (AY Register Select)
    // A15=1, A14=1, A1=0

    EXPECT_TRUE(_portDecoder->IsPort_FFFD(0xFFFD));
    EXPECT_TRUE(_portDecoder->IsPort_FFFD(0xC000));  // Minimal match

    EXPECT_FALSE(_portDecoder->IsPort_FFFD(0xBFFD));  // A14=0
    EXPECT_FALSE(_portDecoder->IsPort_FFFD(0x7FFD));  // A15=0
}

/// endregion </AY port tests>

/// region <Reset test>

TEST_F(PortDecoder_ATM710_Test, Reset)
{
    EmulatorState& state = _context->emulatorState;

    // Set some values
    state.p7FFD = 0x12;
    state.pFF77 = 0x35;
    state.pEFF7 = 0x56;
    state.aFF77 = 0x1234;
    state.atmMemSwapped = true;
    state.pFFF7[0] = 0x100;

    // Reset
    _portDecoder->reset();

    // Mode-neutral reset (original reset(mode) clears the generic port
    // registers only): FF77/pFFF7 keep their values - the boot defaults are
    // mode-dependent and applied separately by ApplyBootROMDefaults()
    EXPECT_EQ(state.p7FFD, 0x00);
    EXPECT_EQ(state.pEFF7, 0x00);
    EXPECT_EQ(state.pFF77, 0x35);
    EXPECT_EQ(state.aFF77, 0x1234);
    EXPECT_TRUE(state.atmMemSwapped);
    EXPECT_EQ(state.pFFF7[0], 0x100);
}

/// endregion </Reset test>

/// region <ApplyBootROMDefaults tests>

TEST_F(PortDecoder_ATM710_Test, ApplyBootROMDefaults_NonDOS_ManagerDisabled)
{
    EmulatorState& state = _context->emulatorState;
    state.flags = 0x00;

    // Consistent pre-state: video mode 3 with memswap active (pFF77 bit 0 set)
    state.pFF77 = 0x80 | 0x40 | 0x20 | 3;
    state.aFF77 = 0x4000 | 0x200 | 0x100;
    state.atmMemSwapped = true;

    // Any non-DOS boot mode (RESET=128/48/SYS, original reset(mode) default branch)
    _portDecoder->ApplyBootROMDefaults(RM_128);

    // aFF77 = 0: memory manager off, ~CPM active, palette gate off; pFF77 = 0
    EXPECT_EQ(state.aFF77, 0x0000);
    EXPECT_EQ(state.pFF77, 0x00);

    // pFF77 bit0 1->0 transition physically unswaps RAM
    EXPECT_FALSE(state.atmMemSwapped);

    // ~CPM=0 raises CF_TRDOS (original set_banks() ATM branch)
    EXPECT_TRUE(state.flags & CF_TRDOS);

    // PEN off: all four windows read the last ROM page (ATM BIOS boot)
    for (int bank = 0; bank < 4; bank++)
    {
        EXPECT_EQ(_memory->GetMemoryBankMode(bank), MemoryBankModeEnum::BANK_ROM) << "bank " << bank;
    }
}

TEST_F(PortDecoder_ATM710_Test, ApplyBootROMDefaults_DOS_ManagerDefaults)
{
    EmulatorState& state = _context->emulatorState;
    state.flags = 0x00;
    state.pFF77 = 0x00;

    _portDecoder->ApplyBootROMDefaults(RM_DOS);

    // RM_DOS defaults (original reset(mode) RM_DOS block)
    EXPECT_EQ(state.aFF77, 0x4000 | 0x200 | 0x100);
    EXPECT_EQ(state.pFF77, 0x80 | 0x40 | 0x20 | 3);

    // pFF77 bit0 0->1 transition swaps RAM: RM_DOS boots memswapped
    EXPECT_TRUE(state.atmMemSwapped);

    // Both pFFF7 register sets (7FFD.4 = 0 / 1) initialized identically
    EXPECT_EQ(state.pFFF7[0], 0x0100 | 1);  // ROM from 7FFD, page pair 0/1 (sys / trdos)
    EXPECT_EQ(state.pFFF7[1], 0x0200 | 5);  // RAM from FFF7, page 5
    EXPECT_EQ(state.pFFF7[2], 0x0200 | 2);  // RAM from FFF7, page 2
    EXPECT_EQ(state.pFFF7[3], 0x0200 | 0);  // RAM from FFF7, page 0
    EXPECT_EQ(state.pFFF7[4], 0x0100 | 1);  // Second register set: same mapping
    EXPECT_EQ(state.pFFF7[7], 0x0200 | 0);

    // Manager on: window 0 = sys/TR-DOS ROM pair, windows 1-3 = RAM 5/2/0
    EXPECT_EQ(_memory->GetMemoryBankMode(0), MemoryBankModeEnum::BANK_ROM);
    EXPECT_EQ(_memory->GetRAMPageForBank1(), 5);
    EXPECT_EQ(_memory->GetRAMPageForBank2(), 2);
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 0);
}

/// endregion </ApplyBootROMDefaults tests>

/// region <FFF7 memory manager tests>

TEST_F(PortDecoder_ATM710_Test, FFF7_RegisterEncoding_FromDataBus)
{
    // Register contents come from the data bus only: type in bits 6-7,
    // page in bits 0-5 (active-low). Stored: [9:8] = type, [7:0] = page
    EmulatorState& state = _context->emulatorState;
    state.p7FFD = 0x00;  // Register set 0

    _portDecoder->DecodePortOut(0x00F7, 0xFF, 0x0000);
    EXPECT_EQ(state.pFFF7[0], 0x000);  // Type 0 (RAM from 7FFD), page 0

    _portDecoder->DecodePortOut(0x00F7, 0xBF, 0x0000);
    EXPECT_EQ(state.pFFF7[0], 0x100);  // Type 1 (ROM from 7FFD), page 0

    _portDecoder->DecodePortOut(0x00F7, 0x7F, 0x0000);
    EXPECT_EQ(state.pFFF7[0], 0x200);  // Type 2 (RAM from FFF7), page 0

    _portDecoder->DecodePortOut(0x00F7, 0x3F, 0x0000);
    EXPECT_EQ(state.pFFF7[0], 0x300);  // Type 3 (ROM from FFF7), page 0

    // Page bits: val 0xB8 -> type 1, page ~0x38 & 0x3F = 7
    _portDecoder->DecodePortOut(0x00F7, 0xB8, 0x0000);
    EXPECT_EQ(state.pFFF7[0], 0x107);  // Type 1, page 7
}

TEST_F(PortDecoder_ATM710_Test, FFF7_WindowAndRegisterSetSelection)
{
    EmulatorState& state = _context->emulatorState;
    state.p7FFD = 0x00;

    // Window selected by A15:A14
    _portDecoder->DecodePortOut(0x00F7, 0x7F, 0x0000);  // Window 0
    _portDecoder->DecodePortOut(0x7FF7, 0xFF, 0x0000);  // Window 1
    _portDecoder->DecodePortOut(0xBFF7, 0xBF, 0x0000);  // Window 2
    _portDecoder->DecodePortOut(0xFFF7, 0x3F, 0x0000);  // Window 3

    EXPECT_EQ(state.pFFF7[0], 0x200);
    EXPECT_EQ(state.pFFF7[1], 0x000);
    EXPECT_EQ(state.pFFF7[2], 0x100);
    EXPECT_EQ(state.pFFF7[3], 0x300);

    // 7FFD bit 4 switches to the second register set
    state.p7FFD = 0x10;
    _portDecoder->DecodePortOut(0x00F7, 0x3F, 0x0000);  // Window 0 of set 1 -> pFFF7[4]
    EXPECT_EQ(state.pFFF7[4], 0x300);
    EXPECT_EQ(state.pFFF7[0], 0x200);  // Set 0 stays untouched
}

TEST_F(PortDecoder_ATM710_Test, FFF7_MemoryBanks_RAMFromFFF7)
{
    EmulatorState& state = _context->emulatorState;
    state.aFF77 = PortDecoder_ATM710::ATM_AFF77_PEN | PortDecoder_ATM710::ATM_AFF77_CPM;  // Manager on, TR-DOS select off
    state.p7FFD = 0x00;

    // Window 1 <- RAM page 10: stored 0x20A comes from val 0x40 | (0x3F ^ 0x0A) = 0x75
    _portDecoder->DecodePortOut(0x7FF7, 0x75, 0x0000);
    EXPECT_EQ(_memory->GetRAMPageForBank1(), 10);

    // Window 3 <- RAM page 0 (val 0x7F -> stored 0x200)
    _portDecoder->DecodePortOut(0xFFF7, 0x7F, 0x0000);
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 0);

    EXPECT_EQ(_memory->GetMemoryBankMode(1), MemoryBankModeEnum::BANK_RAM);
}

TEST_F(PortDecoder_ATM710_Test, FFF7_MemoryBanks_RAMFrom7FFD)
{
    EmulatorState& state = _context->emulatorState;
    state.aFF77 = PortDecoder_ATM710::ATM_AFF77_PEN | PortDecoder_ATM710::ATM_AFF77_CPM;
    state.p7FFD = 0x03;  // Low 3 page bits = 3

    // Window 3 <- RAM from 7FFD: stored 0x038 comes from val 0xC7.
    // Final page = (p7FFD & 7) | (stored & 0xF8) = 3 | 0x38 = 0x3B
    _portDecoder->DecodePortOut(0xFFF7, 0xC7, 0x0000);
    EXPECT_EQ(state.pFFF7[3], 0x038);
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 0x3B);
}

TEST_F(PortDecoder_ATM710_Test, FFF7_MemoryBanks_ROMTypes)
{
    EmulatorState& state = _context->emulatorState;
    state.aFF77 = PortDecoder_ATM710::ATM_AFF77_PEN | PortDecoder_ATM710::ATM_AFF77_CPM;
    state.p7FFD = 0x00;

    // Window 0 <- ROM from 7FFD (val 0xBF -> stored 0x100)
    _portDecoder->DecodePortOut(0x00F7, 0xBF, 0x0000);
    EXPECT_EQ(_memory->GetMemoryBankMode(0), MemoryBankModeEnum::BANK_ROM);

    // Window 1 <- ROM from FFF7 (val 0x3F -> stored 0x300)
    _portDecoder->DecodePortOut(0x7FF7, 0x3F, 0x0000);
    EXPECT_EQ(_memory->GetMemoryBankMode(1), MemoryBankModeEnum::BANK_ROM);
}

TEST_F(PortDecoder_ATM710_Test, FFF7_PEN_Disabled_AllROM)
{
    EmulatorState& state = _context->emulatorState;
    _portDecoder->reset();  // Mode-neutral: pFFF7 content is irrelevant with PEN=0

    // PEN=0: manager disabled - all four windows must read the last ROM page
    state.aFF77 = 0x0000;
    state.p7FFD = 0x00;
    _portDecoder->DecodePortOut(0x00F7, 0x7F, 0x0000);  // Any manager write re-runs the mapping

    for (int bank = 0; bank < 4; bank++)
    {
        EXPECT_EQ(_memory->GetMemoryBankMode(bank), MemoryBankModeEnum::BANK_ROM) << "bank " << bank;
    }
}

TEST_F(PortDecoder_ATM710_Test, FFF7_TRDOSFlag_FromCPMBit)
{
    EmulatorState& state = _context->emulatorState;
    state.flags = 0x00;

    // ~CPM=0 (aFF77 bit 9 clear) raises CF_TRDOS (selects the TR-DOS ROM half)
    state.aFF77 = PortDecoder_ATM710::ATM_AFF77_PEN;
    _portDecoder->DecodePortOut(0x00F7, 0x7F, 0x0000);
    EXPECT_TRUE(state.flags & CF_TRDOS);
}

TEST_F(PortDecoder_ATM710_Test, FFF7_RAMPageMask)
{
    EmulatorState& state = _context->emulatorState;
    state.aFF77 = PortDecoder_ATM710::ATM_AFF77_PEN | PortDecoder_ATM710::ATM_AFF77_CPM;
    state.p7FFD = 0x00;

    // 256KB machine: 16 RAM pages -> mask 0x0F
    _context->config.ramsize = 256;

    // Window 0 <- RAM page 0x3F (val 0x40 -> stored 0x23F), masked down to 0x0F
    _portDecoder->DecodePortOut(0x00F7, 0x40, 0x0000);
    EXPECT_EQ(state.pFFF7[0], 0x23F);
    EXPECT_EQ(_memory->GetRAMPageForBank0(), 0x0F);
}

TEST_F(PortDecoder_ATM710_Test, Reset_ReferenceBootMapping)
{
    EmulatorState& state = _context->emulatorState;
    state.flags = 0x00;

    // Full boot sequence for RESET=128 (data/configs/atm710/unreal.ini):
    // mode-neutral decoder reset, then the mode-dependent FF77 defaults
    _portDecoder->reset();
    _portDecoder->ApplyBootROMDefaults(RM_128);

    // Non-DOS boot: manager disabled, all four windows read the last ROM page
    // (atm2.rom page 3 = ATM BIOS, which then drives FF77 itself during boot)
    EXPECT_EQ(state.aFF77, 0x0000);
    EXPECT_EQ(state.pFF77, 0x00);
    for (int bank = 0; bank < 4; bank++)
    {
        EXPECT_EQ(_memory->GetMemoryBankMode(bank), MemoryBankModeEnum::BANK_ROM) << "bank " << bank;
    }

    // RESET=DOS boots the TR-DOS memory-manager defaults instead
    _portDecoder->ApplyBootROMDefaults(RM_DOS);
    EXPECT_EQ(state.aFF77, 0x4000 | 0x200 | 0x100);
    EXPECT_EQ(state.pFF77, 0x80 | 0x40 | 0x20 | 3);

    // Window 0: ROM pair page 0/1 (sys / trdos), windows 1-3: RAM 5/2/0
    EXPECT_EQ(_memory->GetMemoryBankMode(0), MemoryBankModeEnum::BANK_ROM);
    EXPECT_EQ(_memory->GetRAMPageForBank1(), 5);
    EXPECT_EQ(_memory->GetRAMPageForBank2(), 2);
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 0);
}

/// endregion </FFF7 memory manager tests>
