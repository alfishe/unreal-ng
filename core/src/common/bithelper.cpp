#include "stdafx.h"

#include "common/logger.h"

#include "bithelper.h"

/// Returns index [0..7] for the first bit set in value
/// If no bits were set - 0xFF will be returned
/// \param value
/// \return Position of first set bit [0..7], or 0xFF if no bits were set
uint8_t BitHelper::GetFirstSetBitPosition(uint8_t value)
{
    uint8_t result = 0xFF;
    constexpr uint8_t valueSizeInBits = sizeof(value) * 8;

    int i = 0;
    if (value != 0)
    {
        while (i < valueSizeInBits)
        {
            if (value & 0x01)
            {
                result = i;
                break;
            }

            value >>= 1;
        }
    }

    return result;
}

/// Returns index [0..15] for the first bit set in value
/// If no bits were set - 0xFF will be returned
/// \param value
/// \return Position of first set bit [0..15], or 0xFF if no bits were set
uint8_t BitHelper::GetFirstSetBitPosition(uint16_t value)
{
    uint8_t result = 0xFF;
    constexpr uint8_t valueSizeInBits = sizeof(value) * 8;

    int i = 0;
    if (value != 0)
    {
        while (i < valueSizeInBits)
        {
            if (value & 0x01)
            {
                result = i;
                break;
            }

            value >>= 1;
        }
    }

    return result;
}

/// Returns index [0..7] for the last bit set in value
/// If no bits were set - 0xFF will be returned
/// \param value
/// \return Position of first set bit [0..7], or 0xFF if no bits were set
uint8_t BitHelper::GetLastSetBitPosition(uint8_t value)
{
    uint8_t result = 0xFF;
    constexpr uint8_t valueSizeInBits = sizeof(value) * 8;

    int i = 0;
    if (value != 0)
    {
        while (i < valueSizeInBits)
        {
            if (value & 0x01)
            {
                result = valueSizeInBits - i;
                break;
            }

            value <<= 1;
        }
    }

    return result;
}

/// Returns index [0..15] for the last bit set in value
/// If no bits were set - 0xFF will be returned
/// \param value
/// \return Position of first set bit [0..15], or 0xFF if no bits were set
uint8_t BitHelper::GetLastSetBitPosition(uint16_t value)
{
    uint8_t result = 0xFF;
    constexpr uint8_t valueSizeInBits = sizeof(value) * 8;

    int i = 0;
    if (value != 0)
    {
        while (i < valueSizeInBits)
        {
            if (value & 0x01)
            {
                result = valueSizeInBits - i;
                break;
            }

            value <<= 1;
        }
    }

    return result;
}