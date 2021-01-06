#pragma once
#include "stdafx.h"

class BitHelper
{
public:
    static uint8_t GetFirstSetBitPosition(uint8_t value);
    static uint8_t GetFirstSetBitPosition(uint16_t value);
    static uint8_t GetLastSetBitPosition(uint8_t value);
    static uint8_t GetLastSetBitPosition(uint16_t value);

    static uint8_t CountSetBits(uint8_t value);
    static uint8_t CountSetBits(uint16_t value);

    template<typename BitField>
    static bool IsBitSet(BitField value, uint8_t bitNumber)
    {
        return value & (1 << bitNumber);
    }
};
