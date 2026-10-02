#pragma once

#include <cstddef>
#include <cstdint>

/// @file sprinterpldstate.h
/// @brief The Sprinter Sp2000 PLD state (Sprinter tdd-ports-memory §2).
///
/// Owned by PortDecoder_Sprinter and nowhere else: no shared struct carries a
/// Sprinter field. Plain fixed-width fields only (no bitfields, no pointers)
/// and no implicit padding, so the TTD blob (phase S7) is the struct itself.
/// The port table is not here: it is ordinary RAM page #40.
///
/// The field names follow MAME's `sprinter_state` (src/mame/sinclair/sprinter.cpp),
/// the reference the bank formula is taken from.

/// PLD configuration state (tdd-ports-memory §6)
namespace SprinterConfigState
{
    constexpr uint8_t Unconfigured = 0;  ///< power-on: nothing loaded yet (never seen by the CPU: the loader starts at once)
    constexpr uint8_t Loading = 1;       ///< the CPU runs the ROM's loader; memory writes are configuration bits
    constexpr uint8_t Configured = 2;    ///< the PLD runs its configuration (the port table, the windows ...)
}

/// Internal device codes of the standard configuration (hardware-reference §4.3)
namespace SprinterCode
{
    constexpr uint8_t None = 0x00;
    constexpr uint8_t FdcCommand = 0x10;   ///< #10-#13 WD1793 command/status, track, sector, data
    constexpr uint8_t FdcData = 0x13;
    constexpr uint8_t BetaSystem = 0x14;   ///< W: Beta system register
    constexpr uint8_t BetaState = 0x15;    ///< R: joystick + DRQ/INTRQ
    constexpr uint8_t DensityDD = 0x16;    ///< W: 720 KB (#01BD)
    constexpr uint8_t DensityHD = 0x17;    ///< W: 1.44 MB (#21BD)
    constexpr uint8_t IsaControl = 0x1B;   ///< W: #9FBD
    constexpr uint8_t CmosRead = 0x1C;     ///< R: #FFBD
    constexpr uint8_t CmosAddress = 0x1D;  ///< W: #DFBD
    constexpr uint8_t CmosWrite = 0x1E;    ///< W: #BFBD
    constexpr uint8_t IdeData = 0x20;
    constexpr uint8_t IdeAltStatus = 0x28;
    constexpr uint8_t IdeDriveAddress = 0x29;
    constexpr uint8_t IdeSecondary = 0x2A;
    constexpr uint8_t IdePrimary = 0x2B;
    constexpr uint8_t Frame320 = 0x2C;
    constexpr uint8_t Frame312 = 0x2D;
    constexpr uint8_t PldReload = 0x2E;
    constexpr uint8_t Keyboard = 0x40;     ///< R: #FE
    constexpr uint8_t AyRead = 0x52;
    constexpr uint8_t KempstonMouse = 0x58;
    constexpr uint8_t Covox = 0x88;
    constexpr uint8_t CovoxBlaster = 0x89;
    constexpr uint8_t RomPage = 0x8F;
    constexpr uint8_t AyAddress = 0x90;
    constexpr uint8_t AyData = 0x91;
    constexpr uint8_t Port1FFD = 0xC0;     ///< and the copy #C8
    constexpr uint8_t Port7FFD = 0xC1;     ///< and the copy #C9
    constexpr uint8_t Border = 0xC2;
    constexpr uint8_t AllMode = 0xC3;
    constexpr uint8_t PortY = 0xC4;        ///< and #CC
    constexpr uint8_t RgMod = 0xC5;        ///< and #CD
    constexpr uint8_t SysCnf = 0xC6;       ///< and #CE
    constexpr uint8_t Scale = 0xC7;        ///< and #CF (accelerator, phase S5)
    constexpr uint8_t Hold = 0xCB;
    constexpr uint8_t Page0 = 0xE8;
    constexpr uint8_t Page1 = 0xE9;
    constexpr uint8_t Page2 = 0xEA;
    constexpr uint8_t Page3 = 0xF0;        ///< #F0-#FF: the cell of the current Spectrum page
}

struct SprinterPldState
{
    /// Internal cells #C0-#FF (index = code - #C0). #C0/#C1 hold the raw #1FFD /
    /// #7FFD writes; `sc` / `pn` below are the values after the CNF clean rules
    uint8_t cells[64];

    uint8_t sc;           ///< #1FFD (Scorpion) after the clean rules (MAME m_sc)
    uint8_t pn;           ///< #7FFD (Pentagon) after the clean rules (MAME m_pn)
    uint8_t cnf;          ///< last CNF/SYS write with bit 2 = 1: map (bits 4-3), clean rules (bits 7-5)
    uint8_t romRg;        ///< ROM / fast RAM page register (#5C, code #8F)
    uint8_t allMode;      ///< ALL_MODE (code #C3)
    uint8_t portY;        ///< PORT_Y / RGADR (code #C4)
    uint8_t rgMod;        ///< RGMOD (code #C5)
    uint8_t hold;         ///< HOLD (code #CB)

    uint8_t isaAddrExt;   ///< #9FBD bits 5-0
    uint8_t romOff;       ///< 1 after a write to #3C (system ROM out of window 0), 0 after #7C (MAME m_rom_sys)
    uint8_t ramSys;       ///< 1 after #24/#3C (code #C6 with A6 = 0), 0 after #74/#7C
    uint8_t sysPg;        ///< BIOS half: 0 = ROM page 8, 1 = ROM page 0 (ROM_RG bit 3 inverted unless set)
    uint8_t arom16;       ///< vROM set (#E0-#E3 vs #E4-#E7)
    uint8_t turbo;        ///< CNF/SYS bit 0 with bit 1 = 1
    uint8_t turboHard;    ///< front-panel turbo allowed (MAME F12; [SPRINTER] Turbo=)
    uint8_t cacheOn;      ///< fast RAM in window 0 (IN #FB on, IN #7B off)

    uint8_t dos;          ///< 1 = TR-DOS off (MAME m_dos); switched by the M1 hook (phase S3a)
    uint8_t starting;     ///< 1 after a PLD reset until the first port read (window 3 = page #40)
    uint8_t configState;  ///< SprinterConfigState
    uint8_t configModule; ///< registry index of the active configuration module (0 = Standard)
    uint8_t frameLines;   ///< 0 = 320 lines, 1 = 312 lines
    uint8_t pg3;          ///< cell index (0-63) that window 3 shows: #D0-#FF by #7FFD / #1FFD
    uint8_t reservedIde[2]; ///< unused: the IDE channel select and data latch live in IdeAdapterState (the AtaChannel blob)

    uint8_t resetPending; ///< a PLD-driven CPU reset waits for the instruction boundary
    uint8_t fdcHd;        ///< density latch (codes #16 / #17): 0 = 720 KB (1 MHz, 250 kbit/s), 1 = 1.44 MB (2 MHz, 500)
    uint8_t fdcOff;       ///< 1 = the WD1793 codes #10-#15 are off (bit 1 of the last density write; MAME, unverified)
    uint8_t reserved[5];

    uint32_t bitstreamCount;     ///< configuration writes seen by the sink
    uint32_t bitstreamHashHead;  ///< MAME-compatible hash of the first 4 096 writes
    uint32_t bitstreamHashFull;  ///< FNV-1a over every written byte of the stream
    uint32_t loadWatchdog;       ///< frames left before an unfinished load is abandoned

    uint8_t Cell(uint8_t code) const { return cells[(code - 0xC0) & 0x3F]; }
    uint8_t& Cell(uint8_t code) { return cells[(code - 0xC0) & 0x3F]; }
};

static_assert(sizeof(SprinterPldState) == 64 + 32 + 16, "SprinterPldState must stay padding-free (TTD blob)");
