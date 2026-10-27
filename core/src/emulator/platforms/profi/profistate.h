#pragma once

#include <cstdint>

/// @file profistate.h
/// @brief Profi state: front-panel switches and the hi-res palette, written by PortDecoder_Profi
/// and read by the Profi video.
///
/// Embedded by value in EmulatorState::profi: the emulator reset (EmulatorState{}) zeroes it and
/// the TTD Profi paging blob copies it field by field. Plain values only, no pointers.
/// Design: docs/inprogress/2026-10-07-model-state/tdd.md
struct ProfiState
{
    uint8_t turboSwitch = 0;    // Front-panel TURBO switch: 1 = pressed. The clock is 7 MHz while it is
                                // pressed and (v3) the VG93's HLD is low (PortDecoder_Profi::SyncTurbo)
    uint8_t cpmSwitch = 0;      // v5 front-panel CP/M switch: 1 = pressed. Holds #DFFD at #00 (the
                                // latches' clear input) and every #DFFD write is lost while pressed
    uint16_t palette[0x10] = {};    // Hi-res palette: 9-bit GGGRRRBBB (bits 8:6 G, 5:3 R, 2:0 B;
                                    // B0 is the extra blue LSB latched from #FE.D7 on the previous OUT),
                                    // index = {bright,G,R,B}
};
