#pragma once

#include <cstdint>

/// @file evostate.h
/// @brief ATM3 / ZX-Evo BaseConf latches owned by PortDecoder_ATM3 (the ATM palette, font and
/// memory map shared with the other ATM decoders live in AtmState).
///
/// Embedded by value in EmulatorState::evo: the emulator reset (EmulatorState{}) zeroes it and
/// the TTD paging blob copies it field by field. Plain values only, no pointers.
/// Design: docs/inprogress/2026-10-07-model-state/tdd.md
struct EvoState
{
    // ATM3: pBD word, pBDb.l / pBDb.h bytes (named to satisfy ISO C++)
    union
    {
        uint16_t pBD;
        struct
        {
            uint8_t l;
            uint8_t h;
        } pBDb;
    };
    uint8_t pBE = 0;
    uint8_t pBF = 0;
    uint8_t fddMask = 0;        // ZX-Evo #13BD: bit n = drive n emulated in software (trdemu FPGA only)
    // ZX-Evo virtual TR-DOS (zdos.v): bit 0 = RAM page #FE swapped into #0000-#3FFF,
    // bit 1 = swap due before the next opcode fetch
    uint8_t trdemu = 0;
    uint8_t vgSys = 0;          // D5..D0 of the last OUT (#FF): the VG93 system latch (drive D1..D0, reset, HLT, side)
    // ZX-Evo #xBF7 write protect: bit i = window i of map 0, bit 4 + i = window i of map 1 (the #12BD order)
    uint8_t wrProt = 0;
    // ZX-Evo clock select written, taken over by the CPU clock at the next M1 refresh (zclock.v int_turbo)
    uint8_t turboPending = 0;
    // ZX-Evo board NMI (znmi.v): inNmi = RAM page #FF forced into #0000-#3FFF;
    // nmiEntry = the next NMI the Z80 accepts is the board's own (NOP at #0066, page switch).
    // EmulatorState::nmiAtIntStartPending (model-neutral, read by the Z80) holds a board NMI for the frame INT
    bool inNmi = false;
    bool nmiEntry = false;
};
