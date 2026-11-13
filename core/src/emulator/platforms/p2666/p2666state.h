#pragma once

#include <cstdint>

/// @file p2666state.h
/// @brief Pentagon 2.666 LE board state (MM_P2666): the FPGA's latches that are not in the generic EmulatorState
/// fields, written by PortDecoderP2666 and read by its memory map and the video.
///
/// `#7FFD` and `#EFF7` live in EmulatorState::p7FFD / pEFF7 (the video reads them there); DOS on = CF_TRDOS in
/// EmulatorState::flags. The palette is also kept as host colours in AtmState::palette, the bright border bit in
/// AtmState::borderBright, so the ATM and Pentagon 16 colour renderers work unchanged.
///
/// Embedded by value in EmulatorState::p2666: the emulator reset (EmulatorState{}) zeroes it, the decoder's
/// PowerCycle / reset fill in the board values. Plain values only, no pointers.
/// Sources: docs/inprogress/2026-11-11-pentagon-2666-le/hardware-reference.md (HR), PaE.vhd at e808402
struct P2666State
{
    uint8_t pxx77 = 0xAB;       // last ATM #xx77 value (HR §4.1): bits 2..0 video mode, bit 3 turbo1 (28 MHz)
    uint8_t cpm = 0;            // A9 of the last #xx77 write: 0 = DOS never switches off (HR §3)
    uint8_t pen2 = 1;           // A14 of the last #xx77 write: 1 = no palette writes in DOS
    uint8_t bf0 = 0;            // #xxBF bit 0: keep DOS on in RAM
    uint8_t dos1Off = 1;        // dos_1 as a "DOS off" level: cpm AND NOT bf0 latched at the end of every I/O cycle;
                                // not touched by the CPU reset (PaE.vhd:1207-1226), 1 at power-on
    uint8_t border3 = 0;        // NOT A3 of the last #FE write: the palette entry's bit 3 (bright border)
    uint8_t covox = 0;          // #FB DAC
    uint8_t powered = 0;        // 1 once the MCU upload (PowerCycle) ran: a CPU reset keeps RAM, palette, CMOS
    uint8_t reserved0[3] = {};  // explicit filler (TTD copies the struct field by field)
    uint16_t window[2][4] = {}; // cpuW_M, 10 bits each: [map][window], map = #7FFD bit 4 (HR §2.2)
    uint16_t palette[16] = {};  // raw palette words A15..8 : D7..0 (HR §6.2)

    /// region <Timing (frame-relative mainclk = 114 MHz edges, 2293760 per frame)>
    uint32_t intFlagClearFrame = 0; // frame_counter of the last OUT (FE),#20
    uint32_t intFlagClearEdge = 0;  // its frame-relative edge
    uint32_t intFrame = 0xFFFFFFFF; // frame the pulse fields below belong to (frame_counter)
    uint32_t intFall = 0;           // edge INT goes low this frame (HR §1.2)
    uint32_t intAnchor = 0;         // the next CPU rising edge >= intAnchor counts towards the pulse end
    uint32_t intRise = 0;           // edge INT goes high again; 0 = still counting from intAnchor
    uint8_t intRisesLeft = 32;      // CPU rising edges still to count from intAnchor
    uint8_t reserved1[3] = {};
    /// endregion </Timing>
};
