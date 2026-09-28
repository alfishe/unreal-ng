#pragma once

/// @file statebytes.h
/// @brief Little-endian fields for fixed-layout state snapshots (TTD blobs).

#include <cstdint>
#include <cstring>

namespace statebytes
{
inline void put16(uint8_t* dst, uint16_t value)
{
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
}

inline uint16_t get16(const uint8_t* src)
{
    return static_cast<uint16_t>(src[0] | (src[1] << 8));
}

inline void put32(uint8_t* dst, uint32_t value)
{
    for (int i = 0; i < 4; i++)
        dst[i] = static_cast<uint8_t>(value >> (8 * i));
}

inline uint32_t get32(const uint8_t* src)
{
    return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) | (static_cast<uint32_t>(src[2]) << 16) |
           (static_cast<uint32_t>(src[3]) << 24);
}

inline void put64(uint8_t* dst, int64_t value)
{
    const uint64_t raw = static_cast<uint64_t>(value);
    for (int i = 0; i < 8; i++)
        dst[i] = static_cast<uint8_t>(raw >> (8 * i));
}

inline int64_t get64(const uint8_t* src)
{
    uint64_t raw = 0;
    for (int i = 7; i >= 0; i--)
        raw = (raw << 8) | src[i];
    return static_cast<int64_t>(raw);
}

inline void putU64(uint8_t* dst, uint64_t value) { put64(dst, static_cast<int64_t>(value)); }
inline uint64_t getU64(const uint8_t* src) { return static_cast<uint64_t>(get64(src)); }

/// IEEE doubles are stored as their bit pattern
inline void putDouble(uint8_t* dst, double value)
{
    uint64_t raw = 0;
    memcpy(&raw, &value, sizeof raw);
    putU64(dst, raw);
}

inline double getDouble(const uint8_t* src)
{
    const uint64_t raw = getU64(src);
    double value = 0;
    memcpy(&value, &raw, sizeof value);
    return value;
}
} // namespace statebytes
