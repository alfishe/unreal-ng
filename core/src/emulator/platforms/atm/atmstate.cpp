#include "atmstate.h"

#include "emulator/video/atm/atmfont.h"

/// The built-in font is stored row * 256 + code; the FPGA's font RAM is addressed code * 8 + row
void AtmState::InitFont()
{
    for (unsigned code = 0; code < 256; code++)
        for (unsigned row = 0; row < 8; row++)
            fontRam[code * 8 + row] = ATM_FONT[row * 256 + code];
    fontByte = 0xFF;
}
