#pragma once

#include <cstdint>

/// @file gmxstate.h
/// @brief Scorpion GMX card state (Graphic Memory eXpander, MOA 1998): the register file the card adds to the
/// host Scorpion ZS-256, written by PortDecoder_ScorpionGMX and read by ScorpionMemory and the video.
///
/// The card's other two latches live where the host Scorpion keeps them: #7EFD in ScorpionState::p7EFD (the ROM
/// plane in ScorpionState::profromBank) and #DFFD in EmulatorState::pDFFD.
///
/// Embedded by value in EmulatorState::gmx: the emulator reset (EmulatorState{}) zeroes it and the TTD GMX blob
/// copies it field by field. Plain values only, no pointers.
/// Design: docs/inprogress/2026-10-07-gmx/design.md section 2.2
struct GmxState
{
    uint8_t p00 = 0;            // #xx00 global configuration: bits 0-2 scheme, 3 RECONF, 4 FIXROM, 5 BLKEXT,
                                // 6 BLKEX2, 7 ST/PENT. RESET leaves it as it is (manual section 2)
    uint8_t p78FD = 0;          // #78FD as written (bits 0-6): RAM page (value ^ 2) at #8000-#BFFF
    uint8_t p7AFD = 0;          // #7AFD as written: the scroll start address bits 7-4 (bits 3-0 forced 0)
    uint8_t p7CFD = 0;          // #7CFD as written: the scroll start address bits 13-8 (& #3F)
    uint8_t magicShift = 0;     // #88 | scheme after a RECONF; IN #78FD returns bit 0 in its bit 0 and shifts
    uint8_t magicLock = 0;      // 1 = the Magic button froze the read-backs; a read of #FF releases them
    uint8_t lock7AFD = 0;       // IN #7AFD bits 0-6 frozen at the Magic press
    uint8_t lock7EFD = 0;       // IN #7EFD bits 0-3 and 6 frozen at the Magic press
};
