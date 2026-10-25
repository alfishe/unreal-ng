#pragma once

#include <cstdint>

/// The frame families of NR #03 bits 6:4 (and the machine type bits 2:0): 1 = 48K, 2 = 128K / +2, 3 = +2A / +3, 4 =
/// Pentagon (50 Hz only). Frame geometry in 3.5 MHz T-states, from the FPGA's zxula_timing.vhd
/// (research-fpga-vhdl.md section 1); the INT position and length follow the classic machines of the same family
/// so the raster the existing screen draws and the INT agree. 60 Hz (NR #05 bit 2) is the 264-line raster: the paper starts at
/// line 40 and the ULA interrupt sits at line 0 (zxula_timing.vhd), so the frame starts at the vertical blank and the interrupt follows at once
struct NextTiming
{
    uint32_t frame;
    uint32_t line;
    uint32_t intStart;
    uint32_t intLength;
    uint32_t intLine;  ///< the counter line and pixel clock (7 MHz domain) of the ULA interrupt, zxula_timing.vhd c_int_v / c_int_h
    uint32_t intHc;
    uint32_t minVactive;  ///< the paper's first line (vc) and pixel (hc): the line counter of NR #1E / #1F and the line interrupt is 0 there
    uint32_t minHactive;
    bool sixty = false;  ///< the 60 Hz frame
};

inline bool NextTimingFor(uint8_t timing, bool sixty, NextTiming& out)
{
    if (sixty)
    {
        // zxula_timing.vhd, i_50_60 = 1: 264 lines (c_max_vc 263), paper from line 40, interrupt on line 0 for every family. The frame
        // starts on the first blank line (vc 0): the interrupt is the 50 Hz one less its 8 leading lines (and the 128K's extra one)
        switch (timing)
        {
            case 1:
                out = {59136, 224, 19, 32, 0, 116, 40, 128, true};
                return true;
            case 2:
                out = {60192, 228, 21, 36, 0, 128, 40, 136, true};
                return true;
            case 3:
                out = {60192, 228, 21, 32, 0, 126, 40, 136, true};
                return true;
            default:
                break;  // the Pentagon is 50 Hz only
        }
    }
    switch (timing)
    {
        case 1:
            out = {69888, 224, 1811, 32, 0, 116, 64, 128};
            return true;
        case 2:
            out = {70908, 228, 1845, 36, 1, 128, 64, 136};
            return true;
        case 3:
            out = {70908, 228, 1845, 32, 1, 126, 64, 136};
            return true;
        case 4:
            out = {71680, 224, 71635, 36, 319, 439, 80, 128};  // pulse_count_end: 36 for 128K and Pentagon, 32 for 48K and +3
            return true;
        default:
            return false;
    }
}

inline bool NextTimingFor(uint8_t timing, NextTiming& out)
{
    return NextTimingFor(timing, false, out);
}
