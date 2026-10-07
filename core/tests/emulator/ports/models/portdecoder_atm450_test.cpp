#include "stdafx.h"
#include "pch.h"

#include "portdecoder_atm450_test.h"

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

// Requirements and reference pins: docs/inprogress/2026-10-01-atm450/requirements.md (R2-R6),
// test list: docs/inprogress/2026-10-01-atm450/tdd-plan.md (phase 1)

/// region <SetUp / TearDown>

void PortDecoder_ATM450_Test::SetUp()
{
    _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("", "ATM450", LoggerLevel::LogError);
    ASSERT_TRUE(_emulator);
    _context = _emulator->GetContext();
    _memory = _context->pMemory;
    _portDecoder = dynamic_cast<PortDecoder_ATM450*>(_context->pPortDecoder);
    ASSERT_NE(_portDecoder, nullptr);

    // Neutral start: ZX screen with ROM at #0000 (OUT #FE), CPSYS off (IN #7B), 128 ROM, no extension
    EmulatorState& state = _context->emulatorState;
    state.flags &= ~(CF_TRDOS | CF_DOSPORTS);
    _portDecoder->DecodePortOut(0x00FE, 0x00, 0x0000);
    _portDecoder->DecodePortIn(0x007B, 0x0000);
    _portDecoder->DecodePortOut(0xFDFD, 0x00, 0x0000);
    _portDecoder->DecodePortOut(0x7FFD, 0x00, 0x0000);
}

void PortDecoder_ATM450_Test::TearDown()
{
    if (_emulator)
        EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
}

/// endregion </SetUp / TearDown>

namespace
{
    constexpr uint16_t kRomSys = 0;
    constexpr uint16_t kRomDos = 1;
    constexpr uint16_t kRom128 = 2;
    constexpr uint16_t kRomSos = 3;
}  // namespace

/// region <Write decode groups>

// T1.1 / OQ-3: the four A1 = 0 write groups split on A15 / A9 and never overlap
TEST_F(PortDecoder_ATM450_Test, WriteGroupsAreDisjointOnA15A9)
{
    for (uint32_t i = 0; i <= 0xFFFF; i++)
    {
        const uint16_t port = static_cast<uint16_t>(i);
        const int hits = (_portDecoder->IsPort_7DFD(port) ? 1 : 0) + (_portDecoder->IsPort_7FFD(port) ? 1 : 0) +
                         (_portDecoder->IsPort_FDFD(port) ? 1 : 0) +
                         (((port & 0x8202) == 0x8200) ? 1 : 0);  // AY group

        if ((port & 0x0002) == 0)
            ASSERT_EQ(hits, 1) << "port #" << std::hex << port << " must hit exactly one A1 = 0 group";
        else
            ASSERT_EQ(hits, 0) << "port #" << std::hex << port << " has A1 = 1";
    }

    // Reference addresses
    EXPECT_TRUE(_portDecoder->IsPort_7DFD(0x7DFD));
    EXPECT_TRUE(_portDecoder->IsPort_7FFD(0x7FFD));
    EXPECT_TRUE(_portDecoder->IsPort_FDFD(0xFDFD));
    EXPECT_FALSE(_portDecoder->IsPort_FDFD(0xFFFD)) << "#FFFD (A9 = 1) is the AY, not #FDFD";
    EXPECT_FALSE(_portDecoder->IsPort_FDFD(0xBFFD)) << "#BFFD (A9 = 1) is the AY, not #FDFD";
    EXPECT_FALSE(_portDecoder->IsPort_7FFD(0x7DFD)) << "#7DFD (A9 = 0) is the palette, not #7FFD";
}

// T1.1: #FDFD group latches pFDFD (#FDF5 and #FCFD are members), #7DFD / #F3FD / #FFFD are not
TEST_F(PortDecoder_ATM450_Test, FDFDGroupLatchesPFDFD)
{
    EmulatorState& state = _context->emulatorState;

    _portDecoder->DecodePortOut(0xFDFD, 0x01, 0x0000);
    EXPECT_EQ(state.pFDFD, 0x01);
    _portDecoder->DecodePortOut(0xFDF5, 0x02, 0x0000);
    EXPECT_EQ(state.pFDFD, 0x02);
    _portDecoder->DecodePortOut(0xFCFD, 0x03, 0x0000);
    EXPECT_EQ(state.pFDFD, 0x03);

    _portDecoder->DecodePortOut(0x7DFD, 0x00, 0x0000);
    _portDecoder->DecodePortOut(0xF3FD, 0x00, 0x0000);
    _portDecoder->DecodePortOut(0xFFFD, 0x00, 0x0000);
    _portDecoder->DecodePortOut(0xBFFD, 0x00, 0x0000);
    EXPECT_EQ(state.pFDFD, 0x03) << "#7DFD, #F3FD, #FFFD, #BFFD must not write pFDFD";
}

// T1.12: the 48K lock blocks #7FFD but not #FDFD (UnrealSpeccy io.cpp has no lock on the #FDFD arm)
TEST_F(PortDecoder_ATM450_Test, PagingLockBlocks7FFDButNotFDFD)
{
    EmulatorState& state = _context->emulatorState;

    _portDecoder->DecodePortOut(0x7FFD, 0x20 | 0x03, 0x0000);
    EXPECT_EQ(state.p7FFD, 0x23);

    _portDecoder->DecodePortOut(0x7FFD, 0x05, 0x0000);
    EXPECT_EQ(state.p7FFD, 0x23) << "locked";

    _portDecoder->DecodePortOut(0xFDFD, 0x02, 0x0000);
    EXPECT_EQ(state.pFDFD, 0x02) << "#FDFD ignores the 48K lock";
}

/// endregion </Write decode groups>

/// region <RAM mapping>

// T1.2: page at #C000 = (7FFD & 7) | (FDFD bits 1-0) << 3 - the board's 512 KiB (EA16 / EA17)
TEST_F(PortDecoder_ATM450_Test, C000PageExtendedByFDFD)
{
    _portDecoder->DecodePortOut(0x7FFD, 0x05, 0x0000);
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 5);

    _portDecoder->DecodePortOut(0xFDFD, 0x01, 0x0000);
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 8 + 5);

    _portDecoder->DecodePortOut(0xFDFD, 0x03, 0x0000);
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 24 + 5);

    // D2 is ROM A16 (RA16), not a RAM bit: the page stays below 512 KiB
    _portDecoder->DecodePortOut(0xFDFD, 0x07, 0x0000);
    EXPECT_EQ(_memory->GetRAMPageForBank3(), 24 + 5);

    // Windows 1 and 2 keep the 128K pages
    EXPECT_EQ(_memory->GetRAMPageForBank1(), 5);
    EXPECT_EQ(_memory->GetRAMPageForBank2(), 2);
}

// T1.3: aFE latches the LOW address byte of #xxFE writes; A7 = 0 puts RAM 0 / RAM 4 into windows 0 / 1
TEST_F(PortDecoder_ATM450_Test, FELatchTakesLowAddressByte)
{
    EmulatorState& state = _context->emulatorState;

    _portDecoder->DecodePortOut(0x12FE, 0x00, 0x0000);
    EXPECT_EQ(state.atm.aFE, 0xFE) << "the high byte (#12) is not latched";

    _portDecoder->DecodePortOut(0xFF7E, 0x00, 0x0000);
    EXPECT_EQ(state.atm.aFE, 0x7E);

    // Odd ports do not reach the latch
    _portDecoder->DecodePortOut(0x00FF, 0x00, 0x0000);
    _portDecoder->DecodePortOut(0x7FFD, 0x00, 0x0000);
    EXPECT_EQ(state.atm.aFE, 0x7E);
}

TEST_F(PortDecoder_ATM450_Test, FELatchA7SwitchesRamAtZero)
{
    _portDecoder->DecodePortOut(0x00FE, 0x00, 0x0000);  // A7 = 1
    EXPECT_EQ(_memory->GetMemoryBankMode(0), MemoryBankModeEnum::BANK_ROM);
    EXPECT_EQ(_memory->GetRAMPageForBank1(), 5);

    _portDecoder->DecodePortOut(0x007E, 0x00, 0x0000);  // A7 = 0: CPUS on
    EXPECT_EQ(_memory->GetMemoryBankMode(0), MemoryBankModeEnum::BANK_RAM);
    EXPECT_EQ(_memory->GetRAMPageForBank0(), 0);
    EXPECT_EQ(_memory->GetRAMPageForBank1(), 4) << "window 1 is RAM page 4 in the CP/M user map, not 5";

    // Window 0 is writable RAM page 0
    _memory->DirectWriteToZ80Memory(0x0010, 0xA5);
    EXPECT_EQ(_memory->RAMPageAddress(0)[0x0010], 0xA5);
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0x0010), 0xA5);

    _portDecoder->DecodePortOut(0x00FE, 0x00, 0x0000);  // back to ROM
    EXPECT_EQ(_memory->GetMemoryBankMode(0), MemoryBankModeEnum::BANK_ROM);
    EXPECT_EQ(_memory->GetRAMPageForBank1(), 5);
}

// T1.7: bright border from A3 of the #FE write
TEST_F(PortDecoder_ATM450_Test, BrightBorderFromA3)
{
    EmulatorState& state = _context->emulatorState;

    _portDecoder->DecodePortOut(0x00F6, 0x02, 0x0000);
    EXPECT_EQ(state.atm.borderBright, 1);
    _portDecoder->DecodePortOut(0x00FE, 0x02, 0x0000);
    EXPECT_EQ(state.atm.borderBright, 0);
}

/// endregion </RAM mapping>

/// region <ROM arbitration>

// T1.4: the ROM at #0000, one row of the UnrealSpeccy set_banks() MM_ATM450 priority list per block
TEST_F(PortDecoder_ATM450_Test, RomPriorityMatrix)
{
    EmulatorState& state = _context->emulatorState;

    // Plain 128K rule: 7FFD.4 selects 128 / 48
    _portDecoder->DecodePortOut(0x7FFD, 0x00, 0x0000);
    EXPECT_EQ(_memory->GetROMPage(), kRom128);
    _portDecoder->DecodePortOut(0x7FFD, 0x10, 0x0000);
    EXPECT_EQ(_memory->GetROMPage(), kRomSos);

    // TR-DOS session: the dos ROM whatever 7FFD.4 says
    state.flags |= CF_TRDOS;
    _portDecoder->DecodePortOut(0x7FFD, 0x00, 0x0000);
    EXPECT_EQ(_memory->GetROMPage(), kRomDos);
    _portDecoder->DecodePortOut(0x7FFD, 0x10, 0x0000);
    EXPECT_EQ(_memory->GetROMPage(), kRomDos);

    // CPSYS beats TR-DOS
    _portDecoder->DecodePortIn(0x00FB, 0x0000);
    EXPECT_EQ(_memory->GetROMPage(), kRomSys);

    // CPSYS off again
    _portDecoder->DecodePortIn(0x007B, 0x0000);
    EXPECT_EQ(_memory->GetROMPage(), kRomDos);

    // CPNET inside a TR-DOS session raises CPSYS (sticky)
    _portDecoder->DecodePortOut(0xFDFD, 0x08, 0x0000);
    EXPECT_EQ(_memory->GetROMPage(), kRomSys);
    EXPECT_EQ(state.atm.aFB & 0x80, 0x80);

    // CPNET outside a session does nothing new, the latch stays raised
    state.flags &= ~CF_TRDOS;
    _portDecoder->DecodePortOut(0xFDFD, 0x08, 0x0000);
    EXPECT_EQ(_memory->GetROMPage(), kRomSys);
}

TEST_F(PortDecoder_ATM450_Test, PagingLockClearsCpsys)
{
    EmulatorState& state = _context->emulatorState;

    _portDecoder->DecodePortIn(0x00FB, 0x0000);
    EXPECT_EQ(_memory->GetROMPage(), kRomSys);

    // 7FFD.5 (the 48K lock, Z48 on the schematic) resets the CPSYS flip-flop
    _portDecoder->DecodePortOut(0x7FFD, 0x30, 0x0000);
    EXPECT_EQ(state.atm.aFB & 0x80, 0x00);
    EXPECT_EQ(_memory->GetROMPage(), kRomSos);

    // ... and keeps it down: a later CPSYS read is overridden on the next rebuild
    _portDecoder->DecodePortIn(0x00FB, 0x0000);
    EXPECT_EQ(_memory->GetROMPage(), kRomSos);

    // CPNET inside a TR-DOS session still wins over the lock
    state.flags |= CF_TRDOS;
    _portDecoder->DecodePortOut(0xFDFD, 0x08, 0x0000);
    EXPECT_EQ(_memory->GetROMPage(), kRomSys);
}

// RAM at #0000 beats every ROM rule
TEST_F(PortDecoder_ATM450_Test, RamAtZeroBeatsRomRules)
{
    _portDecoder->DecodePortIn(0x00FB, 0x0000);
    _portDecoder->DecodePortOut(0x007E, 0x00, 0x0000);
    EXPECT_EQ(_memory->GetMemoryBankMode(0), MemoryBankModeEnum::BANK_RAM);
}

/// endregion </ROM arbitration>

/// region <aFB latch>

// T1.5: aFB latches the LOW byte of an unclaimed A2 = 0 read and the bus reads #FF
TEST_F(PortDecoder_ATM450_Test, FBLatchOnUnclaimedA2ZeroRead)
{
    EmulatorState& state = _context->emulatorState;

    EXPECT_EQ(_portDecoder->DecodePortIn(0x12FB, 0x0000), 0xFF);
    EXPECT_EQ(state.atm.aFB, 0xFB) << "low byte, not the high byte #12";

    EXPECT_EQ(_portDecoder->DecodePortIn(0xFF7B, 0x0000), 0xFF);
    EXPECT_EQ(state.atm.aFB, 0x7B);

    // A2 = 1 reads do not latch
    _portDecoder->DecodePortIn(0x00FF, 0x0000);
    _portDecoder->DecodePortIn(0x00DF, 0x0000);
    EXPECT_EQ(state.atm.aFB, 0x7B);
}

// T1.5 / OQ-6: a GS status read (#BB: A2 = 0, A7 = 1) is answered by the card (the shipped config fits
// a NeoGS) and never reaches the aFB latch - UnrealSpeccy claims #B3 / #BB long before the A2 = 0 arm
TEST_F(PortDecoder_ATM450_Test, GsReadIsClaimedBeforeFBLatch)
{
    EmulatorState& state = _context->emulatorState;
    ASSERT_EQ(state.atm.aFB & 0x80, 0x00);

    _portDecoder->DecodePortIn(0x00BB, 0x0000);
    _portDecoder->DecodePortIn(0x00B3, 0x0000);
    EXPECT_EQ(state.atm.aFB & 0x80, 0x00) << "the GS reads must not flip CPSYS";
    EXPECT_EQ(_memory->GetROMPage(), kRom128);
}

/// endregion </aFB latch>

/// region <#FE read PAL marker>

// T1.6: UnrealSpeccy atm450_z - three zero windows of 40 T, 0x80 elsewhere
TEST_F(PortDecoder_ATM450_Test, PalMarkerWindows)
{
    // In-window, including both first and last T of each window
    EXPECT_EQ(PortDecoder_ATM450::PalMarker(7200), 0x00);
    EXPECT_EQ(PortDecoder_ATM450::PalMarker(7239), 0x00);
    EXPECT_EQ(PortDecoder_ATM450::PalMarker(7284), 0x00);
    EXPECT_EQ(PortDecoder_ATM450::PalMarker(7330), 0x00) << "inside both the 2nd and the 3rd window";
    EXPECT_EQ(PortDecoder_ATM450::PalMarker(7365), 0x00);

    // Out of window
    EXPECT_EQ(PortDecoder_ATM450::PalMarker(0), 0x80);
    EXPECT_EQ(PortDecoder_ATM450::PalMarker(7199), 0x80);
    EXPECT_EQ(PortDecoder_ATM450::PalMarker(7240), 0x80);
    EXPECT_EQ(PortDecoder_ATM450::PalMarker(7283), 0x80);
    EXPECT_EQ(PortDecoder_ATM450::PalMarker(7366), 0x80);
    EXPECT_EQ(PortDecoder_ATM450::PalMarker(69887), 0x80);
}

// The windows are INT-relative (UnrealSpeccy cpu.t starts at the INT edge); this core raises INT at
// intstart + 1 of its frame, so a real #FE read at frame T = intstart + 1 + 7200 sees the first zero. The
// system ROM's copy protection samples Z after HALT and decrypts its CP/M loader with the result - with a
// frame-relative window the key was wrong and the CP/M menu entry fell back to the menu
TEST_F(PortDecoder_ATM450_Test, PalMarkerIsIntRelativeOnPortRead)
{
    Z80* z80 = _context->pCore->GetZ80();
    const uint32_t intT = _context->config.intstart + 1;

    z80->t = intT + 7200;
    EXPECT_EQ(_portDecoder->DecodePortIn(0x7FFE, 0x0000) & 0x80, 0x00) << "first zero window, INT + 7200";
    z80->t = intT + 7330;
    EXPECT_EQ(_portDecoder->DecodePortIn(0x7FFE, 0x0000) & 0x80, 0x00);
    z80->t = intT + 7199;
    EXPECT_EQ(_portDecoder->DecodePortIn(0x7FFE, 0x0000) & 0x80, 0x80);
    z80->t = 7200;
    EXPECT_EQ(_portDecoder->DecodePortIn(0x7FFE, 0x0000) & 0x80, 0x80) << "frame-relative 7200 is not a window";
}

/// endregion </#FE read PAL marker>

/// region <Palette>

// T1.8: #7DFD group writes the cell = 4-bit border color, ATM1 layout --grbGRB, active low
TEST_F(PortDecoder_ATM450_Test, PaletteATM1BitLayout)
{
    EmulatorState& state = _context->emulatorState;
    state.border_attr = 0;
    state.atm.borderBright = 0;

    struct Case
    {
        uint8_t value;
        uint32_t abgr;
        const char* what;
    };
    const Case cases[] = {
        {0x00, 0xFFFFFFFFu, "all lines released -> white"},
        {0x3F, 0xFF000000u, "all six lines pulled -> black"},
        {0xC0, 0xFFFFFFFFu, "bits 7-6 are not wired"},
        {0xFE, 0xFFAA0000u, "B (bit 0) high blue"},
        {0xFD, 0xFF0000AAu, "R (bit 1) high red"},
        {0xFB, 0xFF00AA00u, "G (bit 2) high green"},
        {0xF7, 0xFF550000u, "b (bit 3) low blue"},
        {0xEF, 0xFF000055u, "r (bit 4) low red"},
        {0xDF, 0xFF005500u, "g (bit 5) low green"},
    };

    for (const Case& c : cases)
    {
        // Rewrite to black first so a missing write cannot pass on a stale value
        _portDecoder->DecodePortOut(0x7DFD, c.value == 0x3F ? 0x00 : 0x3F, 0x0000);
        _portDecoder->DecodePortOut(0x7DFD, c.value, 0x0000);
        EXPECT_EQ(state.atm.palette[0], c.abgr) << c.what;
        EXPECT_EQ(state.atm.paletteRegs[0], c.value);
    }
}

TEST_F(PortDecoder_ATM450_Test, PaletteCellIsFourBitBorderAndNoGate)
{
    EmulatorState& state = _context->emulatorState;

    // Border 2 + bright (A3 = 0 on the #FE write) -> cell 10
    _portDecoder->DecodePortOut(0x00F6, 0x02, 0x0000);
    const uint32_t cell2Before = state.atm.palette[2];

    // pen2-style gate of 7.10 does not exist on 4.50, no DOS session needed
    state.atm.aFF77 = 0x4000;
    state.flags = 0;
    _portDecoder->DecodePortOut(0x7DFD, 0x00, 0x0000);
    EXPECT_EQ(state.atm.palette[10], 0xFFFFFFFFu);
    EXPECT_EQ(state.atm.palette[2], cell2Before);

    // The 7.10 #xx9F/#xxFF palette group is not a palette port on 4.50
    _portDecoder->DecodePortOut(0x00FF, 0x3F, 0x0000);
    _portDecoder->DecodePortOut(0x009F, 0x3F, 0x0000);
    EXPECT_EQ(state.atm.palette[10], 0xFFFFFFFFu);
}

/// endregion </Palette>

/// region <Negative space>

// T1.9: the ATM 7.10 / ZX-Evo register file does not decode on 4.50
TEST_F(PortDecoder_ATM450_Test, NoAtm710RegisterFile)
{
    EmulatorState& state = _context->emulatorState;
    _portDecoder->DecodePortOut(0x7FFD, 0x03, 0x0000);

    const uint8_t aFE = state.atm.aFE;
    const uint8_t aFB = state.atm.aFB;
    const uint8_t pFDFD = state.pFDFD;
    const uint8_t p7FFD = state.p7FFD;
    const uint16_t page3 = _memory->GetRAMPageForBank3();
    const uint16_t rom = _memory->GetROMPage();

    // (#xxBE is not in the list: it is even, so on 4.50 it is the #FE group - OUT (#BE) selects hi-res)
    for (uint16_t port : {0xFF77, 0x3F77, 0x00F7, 0x7FF7, 0xFFF7, 0xEFF7, 0x00BF, 0xDFF7, 0xBEF7, 0x0077, 0x0057})
    {
        _portDecoder->DecodePortOut(port, 0x00, 0x0000);
        _portDecoder->DecodePortOut(port, 0xFF, 0x0000);
    }

    EXPECT_EQ(state.atm.aFE, aFE);
    EXPECT_EQ(state.atm.aFB, aFB);
    EXPECT_EQ(state.pFDFD, pFDFD);
    EXPECT_EQ(state.p7FFD, p7FFD);
    EXPECT_EQ(state.pFF77, 0x00);
    EXPECT_EQ(state.pEFF7, 0x00);
    EXPECT_EQ(_memory->GetRAMPageForBank3(), page3);
    EXPECT_EQ(_memory->GetROMPage(), rom);
    EXPECT_EQ(_portDecoder->TtdClockUnits(), 1);
    EXPECT_EQ(state.hw_turbo_ratio, 1);
}

/// endregion </Negative space>

/// region <Reset>

// T1.10: reset clears pFDFD; RM_DOS -> aFE #E0, aFB 0; every other mode -> aFE #80, aFB #80 (system ROM)
TEST_F(PortDecoder_ATM450_Test, ResetAndBootDefaults)
{
    EmulatorState& state = _context->emulatorState;

    state.pFDFD = 0x0F;
    _portDecoder->reset();
    EXPECT_EQ(state.pFDFD, 0x00);

    _portDecoder->ApplyBootROMDefaults(RM_128);
    EXPECT_EQ(state.atm.aFE, 0x80);
    EXPECT_EQ(state.atm.aFB, 0x80);
    EXPECT_EQ(_memory->GetROMPage(), kRomSys) << "the machine starts in the system ROM";
    // atm1.rom page 0 opens with DI; JP #3F00 (the R3 page-order pin)
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0x0000), 0xF3);
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0x0001), 0xC3);
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0x0002), 0x00);
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0x0003), 0x3F);

    _portDecoder->ApplyBootROMDefaults(RM_DOS);
    EXPECT_EQ(state.atm.aFE, 0xE0);
    EXPECT_EQ(state.atm.aFB, 0x00);
}

// R6: RM_DOS keeps 7FFD.4 on 4.50 (Memory::SetROMMode) and lands in the dos ROM
TEST_F(PortDecoder_ATM450_Test, RomModeDosKeeps7FFDBit4)
{
    EmulatorState& state = _context->emulatorState;

    _portDecoder->reset();
    _portDecoder->ApplyBootROMDefaults(RM_DOS);
    _memory->SetROMMode(RM_DOS);

    EXPECT_EQ(state.p7FFD & 0x10, 0x10);
    EXPECT_NE(state.flags & CF_TRDOS, 0);
    EXPECT_EQ(_memory->GetROMPage(), kRomDos);
}

/// endregion </Reset>
