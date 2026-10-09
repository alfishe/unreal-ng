#pragma once

#include <cstdint>

/// @file scorpionstate.h
/// @brief Scorpion ZS-256 state: Turbo+, ProfROM and the SMUC latches, written by
/// PortDecoder_Scorpion256 and read by ScorpionMemory / ScorpionRomWindow and the video.
///
/// Embedded by value in EmulatorState::scorpion: the emulator reset (EmulatorState{}) zeroes it
/// and the TTD ProfROM / SMUC blobs copy it field by field. Plain values only, no pointers.
/// Design: docs/inprogress/2026-10-07-model-state/tdd.md
struct ScorpionState
{
    uint8_t turbo = 0;          // ZS-256 Turbo+ hardware turbo flip-flop (hardware-reference 13):
                                // 1 = 7 MHz. Set by IN from the #7FFD-family decode, cleared by IN from
                                // the #1FFD-family decode and by reset. Composes with the host speed
                                // multiplier at the frame boundary - see Z80::Z80FrameCycle()
    uint8_t p7EFD = 0;          // ProfROM window-select latch
    uint8_t profromBank = 0;    // ProfROM plane (quadrant of the ProfROM image)

    // Magic-button DOS trigger (DD50.1 "1-DOS/0-SOS", hardware-reference §9):
    // armed together with the NMI pulse (DD50.2), it forces page 3 (TR-DOS) of the
    // current ProfROM plane over the #0000-#3FFF window - without touching the #1FFD
    // latch (the service bit still outranks it) or the plane register. Released by
    // the first CPU read from #4000-#FFFF (the Beta128 "leave the ROM window"
    // strobe); cleared by reset. Like profromBank it is not reproducible from
    // ports, so TTD checkpoints and the divergence hash carry it explicitly
    uint8_t dosTrigger = 0;

    uint8_t pFFBA = 0;          // SMUC
    uint8_t p7FBA = 0;          // SMUC
};
