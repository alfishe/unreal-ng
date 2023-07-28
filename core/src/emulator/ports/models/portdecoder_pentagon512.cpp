#include "portdecoder_pentagon512.h"

/// Pentagon 512k RAM page switching. Uses 32 pages [0..31] via port #7FFD bits [0..2] + bits [6..7]
void PortDecoder_Pentagon512::switchRAMPage(uint8_t value)
{
    Memory& memory = *_context->pMemory;

    // 5 bit RAM page index is created from bits [0..2][6..7]
    uint8_t bankRAM = value & 0b0000'0111;      // Bits [0:2]
    bankRAM |= ((value & 0b1100'0000) >> 3);    // Bits [6:7]

    memory.SetRAMPageToBank3(bankRAM);
}
