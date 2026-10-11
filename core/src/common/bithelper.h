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

    /// A bit past the field's width is not set (GetFirstSetBitPosition(0) gives 0xFF)
    template<typename BitField>
    static bool IsBitSet(BitField value, uint8_t bitNumber)
    {
        if (bitNumber >= sizeof(BitField) * 8)
            return false;

        return (static_cast<uint64_t>(value) >> bitNumber) & 1;
    }
};
