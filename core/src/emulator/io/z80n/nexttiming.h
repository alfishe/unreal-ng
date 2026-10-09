#pragma once

#include <cstdint>

/// The frame families of NR #03 bits 6:4 (and the machine type bits 2:0): 1 = 48K, 2 = 128K / +2, 3 = +2A / +3, 4 =
/// Pentagon (50 Hz only). Frame geometry in 3.5 MHz T-states, from the FPGA's zxula_timing.vhd
/// (research-fpga-vhdl.md section 1); the INT position and length follow the classic machines of the same family
/// so the raster the existing screen draws and the INT agree. 60 Hz needs the shorter raster (N6)
struct NextTiming
{
    uint32_t frame;
    uint32_t line;
    uint32_t intStart;
    uint32_t intLength;
};

inline bool NextTimingFor(uint8_t timing, NextTiming& out)
{
    switch (timing)
    {
        case 1:
            out = {69888, 224, 1811, 32};
            return true;
        case 2:
            out = {70908, 228, 1845, 36};
            return true;
        case 3:
            out = {70908, 228, 1845, 32};
            return true;
        case 4:
            out = {71680, 224, 71635, 32};
            return true;
        default:
            return false;
    }
}
