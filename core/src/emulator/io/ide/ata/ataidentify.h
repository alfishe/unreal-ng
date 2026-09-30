#pragma once

/// @file ataidentify.h
/// @brief Writing IDENTIFY DEVICE / IDENTIFY PACKET DEVICE data: 256
/// little-endian words; ATA strings space-padded, two characters per word,
/// the first one in the high byte ("UNREAL" is stored as "NU", "ER", "LA").

#include <cstddef>
#include <cstdint>
#include <string>

namespace ata
{
    inline void PutWord(uint8_t* out, size_t word, uint16_t value)
    {
        out[word * 2] = static_cast<uint8_t>(value & 0xFF);
        out[word * 2 + 1] = static_cast<uint8_t>(value >> 8);
    }

    inline void PutString(uint8_t* out, size_t firstWord, size_t words, const std::string& text)
    {
        std::string padded = text.substr(0, words * 2);
        padded.resize(words * 2, ' ');
        for (size_t i = 0; i < words; i++)
            PutWord(out, firstWord + i,
                    static_cast<uint16_t>((static_cast<uint8_t>(padded[i * 2]) << 8) | static_cast<uint8_t>(padded[i * 2 + 1])));
    }
}  // namespace ata
